#include "upload_manager.h"
#include "handlers/format_utils.h"

#include <fcntl.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {
// RAII wrapper for a POSIX file descriptor so the fd is always closed,
// even on early-return or exception paths.
struct FdGuard {
  int fd;
  explicit FdGuard(int f) : fd(f) {}
  ~FdGuard() {
    if (fd >= 0) ::close(fd);
  }
  FdGuard(const FdGuard&) = delete;
  FdGuard& operator=(const FdGuard&) = delete;
};
}  // namespace

UploadManager::UploadManager(const std::string& upload_dir)
  : upload_dir_(upload_dir), tmp_dir_(upload_dir + "/tmp") {
  fs::create_directories(tmp_dir_);
}

std::string UploadManager::create_session(
  const std::string& user_id,
  int64_t total_size,
  int chunk_count,
  int64_t chunk_size,
  const nlohmann::json& metadata) {
  // cleanup_stale() takes the lock itself; call it before we lock.
  cleanup_stale();

  std::lock_guard<std::mutex> lock(mutex_);

  std::string upload_id = format_utils::random_hex(16);
  std::string tmp_path = tmp_dir_ + "/" + upload_id + ".dat";

  // Create the temp file
  int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return "";
  close(fd);

  UploadSession session;
  session.upload_id = upload_id;
  session.user_id = user_id;
  session.total_size = total_size;
  session.chunk_count = chunk_count;
  session.chunk_size = chunk_size;
  session.tmp_path = tmp_path;
  session.created_at = std::chrono::steady_clock::now();
  session.metadata = metadata;

  sessions_[upload_id] = std::move(session);
  return upload_id;
}

std::optional<UploadSession> UploadManager::get_session(const std::string& upload_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(upload_id);
  if (it == sessions_.end()) return std::nullopt;
  return it->second;
}

static std::string sha256_hex(std::string_view data) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
  std::ostringstream oss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
  }
  return oss.str();
}

std::string UploadManager::store_chunk_err(
  const std::string& upload_id,
  int index,
  std::string_view data,
  const std::string& expected_hash) {
  // Phase 1: take the lock just long enough to validate the session + index
  // and copy out the fields we need to do the write. We deliberately do NOT
  // hold the lock across disk I/O.
  std::string tmp_path;
  int64_t chunk_size = 0;
  int chunk_count = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(upload_id);
    if (it == sessions_.end()) return "session_not_found";
    tmp_path = it->second.tmp_path;
    chunk_size = it->second.chunk_size;
    chunk_count = it->second.chunk_count;
  }

  if (index < 0 || index >= chunk_count) return "invalid_index";

  // Overflow-safe offset computation: index * chunk_size must fit in int64_t.
  int64_t offset = 0;
  if (__builtin_mul_overflow(static_cast<int64_t>(index), chunk_size, &offset)) {
    return "invalid_index";
  }
  if (offset < 0) return "invalid_index";

  // Verify SHA-256 hash if provided (no lock needed — purely local work).
  if (!expected_hash.empty()) {
    std::string actual_hash = sha256_hex(data);
    if (actual_hash != expected_hash) return "hash_mismatch";
  }

  // Phase 2: do the pwrite with no lock held. Use RAII so we always close fd.
  FdGuard guard(open(tmp_path.c_str(), O_WRONLY));
  if (guard.fd < 0) return "open_failed";

  ssize_t written = pwrite(guard.fd, data.data(), data.size(), offset);
  if (written < 0) return "io_error";
  if (static_cast<size_t>(written) < data.size()) return "short_write";

  // Phase 3: re-acquire the lock to record receipt. The session may have
  // been removed while we were writing — if so, report that explicitly.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(upload_id);
    if (it == sessions_.end()) return "session_gone";
    it->second.received_chunks.insert(index);
  }
  return "";
}

bool UploadManager::store_chunk(
  const std::string& upload_id,
  int index,
  std::string_view data,
  const std::string& expected_hash) {
  return store_chunk_err(upload_id, index, data, expected_hash).empty();
}

bool UploadManager::is_complete(const std::string& upload_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(upload_id);
  if (it == sessions_.end()) return false;
  return static_cast<int>(it->second.received_chunks.size()) == it->second.chunk_count;
}

int64_t UploadManager::assemble(const std::string& upload_id, const std::string& dest_path) {
  // Hold the lock across the file_size check AND the rename so a racing
  // chunk write cannot touch the temp file between those two operations.
  // (store_chunk_err releases the lock during pwrite, but it re-acquires
  // the lock to update received_chunks; so any in-flight write must finish
  // before we can get here, provided the caller first checks is_complete.)
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(upload_id);
  if (it == sessions_.end()) return -1;

  std::error_code ec;
  auto actual_size = static_cast<int64_t>(fs::file_size(it->second.tmp_path, ec));
  if (ec || actual_size != it->second.total_size) return -1;

  // Instant rename (same filesystem)
  fs::rename(it->second.tmp_path, dest_path, ec);
  if (ec) return -1;

  return it->second.total_size;
}

void UploadManager::remove_session(const std::string& upload_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(upload_id);
  if (it != sessions_.end()) {
    std::error_code ec;
    fs::remove(it->second.tmp_path, ec);
    sessions_.erase(it);
  }
}

void UploadManager::cleanup_stale(int max_age_seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto now = std::chrono::steady_clock::now();
  auto it = sessions_.begin();
  while (it != sessions_.end()) {
    auto age =
      std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created_at).count();
    if (age > max_age_seconds) {
      std::error_code ec;
      fs::remove(it->second.tmp_path, ec);
      it = sessions_.erase(it);
    } else {
      ++it;
    }
  }
}
