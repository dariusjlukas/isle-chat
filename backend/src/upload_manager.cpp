#include "upload_manager.h"
#include "handlers/format_utils.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>

namespace fs = std::filesystem;

UploadManager::UploadManager(const std::string& upload_dir)
    : upload_dir_(upload_dir), tmp_dir_(upload_dir + "/tmp") {
    fs::create_directories(tmp_dir_);
}

std::string UploadManager::create_session(
    const std::string& user_id, int64_t total_size,
    int chunk_count, int64_t chunk_size,
    const nlohmann::json& metadata) {

    cleanup_stale();

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

UploadSession* UploadManager::get_session(const std::string& upload_id) {
    auto it = sessions_.find(upload_id);
    return it != sessions_.end() ? &it->second : nullptr;
}

static std::string sha256_hex(std::string_view data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string UploadManager::store_chunk_err(const std::string& upload_id, int index,
                                            std::string_view data,
                                            const std::string& expected_hash) {
    auto* session = get_session(upload_id);
    if (!session) return "session_not_found";
    if (index < 0 || index >= session->chunk_count) return "invalid_index";

    // Verify SHA-256 hash if provided
    if (!expected_hash.empty()) {
        std::string actual_hash = sha256_hex(data);
        if (actual_hash != expected_hash) return "hash_mismatch";
    }

    int64_t offset = static_cast<int64_t>(index) * session->chunk_size;

    int fd = open(session->tmp_path.c_str(), O_WRONLY);
    if (fd < 0) return "io_error";

    ssize_t written = pwrite(fd, data.data(), data.size(), offset);
    close(fd);

    if (written != static_cast<ssize_t>(data.size())) return "io_error";

    session->received_chunks.insert(index);
    return "";
}

bool UploadManager::store_chunk(const std::string& upload_id, int index,
                                std::string_view data,
                                const std::string& expected_hash) {
    return store_chunk_err(upload_id, index, data, expected_hash).empty();
}

bool UploadManager::is_complete(const std::string& upload_id) const {
    auto it = sessions_.find(upload_id);
    if (it == sessions_.end()) return false;
    return static_cast<int>(it->second.received_chunks.size()) == it->second.chunk_count;
}

int64_t UploadManager::assemble(const std::string& upload_id,
                                const std::string& dest_path) {
    auto* session = get_session(upload_id);
    if (!session) return -1;

    // Verify file size matches expected total
    std::error_code ec;
    auto actual_size = static_cast<int64_t>(fs::file_size(session->tmp_path, ec));
    if (ec || actual_size != session->total_size) return -1;

    // Instant rename (same filesystem)
    fs::rename(session->tmp_path, dest_path, ec);
    if (ec) return -1;

    return session->total_size;
}

void UploadManager::remove_session(const std::string& upload_id) {
    auto it = sessions_.find(upload_id);
    if (it != sessions_.end()) {
        std::error_code ec;
        fs::remove(it->second.tmp_path, ec);
        sessions_.erase(it);
    }
}

void UploadManager::cleanup_stale(int max_age_seconds) {
    auto now = std::chrono::steady_clock::now();
    auto it = sessions_.begin();
    while (it != sessions_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.created_at).count();
        if (age > max_age_seconds) {
            std::error_code ec;
            fs::remove(it->second.tmp_path, ec);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}
