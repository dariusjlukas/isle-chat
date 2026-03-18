#include "file_upload.h"
#include "../setup.h"

#include <openssl/rand.h>

#include <iostream>
#include <mutex>

static std::once_flag file_once;
static FileUploadShared* file_shared = nullptr;

FileUploadShared& ensure_file_upload_setup(const std::string& base_url, StatsCollector& stats) {
  std::call_once(file_once, [&]() {
    file_shared = new FileUploadShared();

    HttpClient setup_http(base_url, stats);
    auto& admin = ensure_admin_setup(setup_http);

    file_shared->space_id = create_space(setup_http, admin.token, "Load Files Space");
    enable_space_tools(setup_http, admin.token, file_shared->space_id);

    std::cerr << "  File upload setup complete\n";
  });
  return *file_shared;
}

FileUploadUser::FileUploadUser(const std::string& base_url, StatsCollector& stats)
    : VirtualUser(base_url, stats) {}

void FileUploadUser::setup() {
  auto& shared = ensure_file_upload_setup(http_.base_url(), stats_);
  space_id_ = shared.space_id;

  identity_ = PkiIdentity();
  register_and_login(http_, identity_);
  join_space(http_, space_id_);
}

std::vector<WeightedTask> FileUploadUser::get_tasks() {
  return {
      {[this]() { small_file_upload(); }, 5, "small_file_upload"},
      {[this]() { medium_file_upload(); }, 2, "medium_file_upload"},
      {[this]() { download_file(); }, 3, "download_file"},
      {[this]() { list_files(); }, 2, "list_files"},
  };
}

void FileUploadUser::upload_file(int size, const std::string& tag) {
  std::string content(size, '\0');
  RAND_bytes(reinterpret_cast<unsigned char*>(content.data()), size);

  std::string filename = "loadtest_" + random_hex(8) + ".bin";
  std::string path = "/api/spaces/" + space_id_ + "/files/upload?filename=" + filename +
                     "&content_type=application/octet-stream";

  auto r = http_.post_raw(path, content, "application/octet-stream", {},
                          "/api/spaces/:id/files/upload [" + tag + "]");

  if (r.ok()) {
    auto j = r.json_body();
    std::string file_id = j.value("id", "");
    if (!file_id.empty()) {
      uploaded_file_ids_.push_back(file_id);
      if (uploaded_file_ids_.size() > 50) {
        uploaded_file_ids_.erase(uploaded_file_ids_.begin(),
                                 uploaded_file_ids_.begin() + 25);
      }
    }
  }
}

void FileUploadUser::small_file_upload() {
  std::uniform_int_distribution<int> dist(1, 10);
  int size_kb = dist(rng_);
  upload_file(size_kb * 1024, "small");
}

void FileUploadUser::medium_file_upload() {
  std::uniform_int_distribution<int> dist(50, 200);
  int size_kb = dist(rng_);
  upload_file(size_kb * 1024, "medium");
}

void FileUploadUser::download_file() {
  if (uploaded_file_ids_.empty()) return;

  std::uniform_int_distribution<size_t> dist(0, uploaded_file_ids_.size() - 1);
  auto& file_id = uploaded_file_ids_[dist(rng_)];

  http_.get("/api/spaces/" + space_id_ + "/files/" + file_id + "/download", {},
            "/api/spaces/:id/files/:id/download");
}

void FileUploadUser::list_files() {
  http_.get("/api/spaces/" + space_id_ + "/files", {}, "/api/spaces/:id/files");
}
