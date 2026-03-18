#pragma once

#include "../pki_identity.h"
#include "../virtual_user.h"

#include <string>
#include <vector>

struct FileUploadShared {
  std::string space_id;
};

FileUploadShared& ensure_file_upload_setup(const std::string& base_url, StatsCollector& stats);

class FileUploadUser : public VirtualUser {
 public:
  FileUploadUser(const std::string& base_url, StatsCollector& stats);

  void setup() override;
  std::vector<WeightedTask> get_tasks() override;
  std::string scenario_name() const override { return "file_upload"; }

 private:
  void small_file_upload();
  void medium_file_upload();
  void download_file();
  void list_files();
  void upload_file(int size, const std::string& tag);

  PkiIdentity identity_;
  std::string space_id_;
  std::vector<std::string> uploaded_file_ids_;
};
