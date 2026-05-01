#include <gtest/gtest.h>
#include <sys/stat.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "upload_manager.h"

namespace fs = std::filesystem;

namespace {

// Creates a unique temp directory rooted under the system temp dir so each
// test case gets its own UploadManager working directory. The caller is
// responsible for removing it (the fixture does this).
std::string make_temp_dir(const std::string& label) {
  auto base = fs::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 rng(rd());
  std::string name = "upload_mgr_test_" + label + "_" + std::to_string(rng());
  auto p = base / name;
  fs::create_directories(p);
  return p.string();
}

class UploadManagerTest : public ::testing::Test {
protected:
  void SetUp() override { dir_ = make_temp_dir("base"); }
  void TearDown() override {
    std::error_code ec;
    // Best-effort: restore permissions before removing in case a test chmod'd.
    fs::permissions(
      dir_ + "/tmp",
      fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
      fs::perm_options::replace,
      ec);
    fs::remove_all(dir_, ec);
  }
  std::string dir_;
};

}  // namespace

TEST_F(UploadManagerTest, ConcurrentStoreChunk) {
  UploadManager mgr(dir_);
  constexpr int kChunkCount = 64;
  constexpr int64_t kChunkSize = 1024;
  constexpr int64_t kTotal = static_cast<int64_t>(kChunkCount) * kChunkSize;

  std::string upload_id =
    mgr.create_session("user-1", kTotal, kChunkCount, kChunkSize, nlohmann::json::object());
  ASSERT_FALSE(upload_id.empty());

  // Partition the 64 chunks across 8 threads, writing disjoint subsets.
  constexpr int kThreads = 8;
  std::atomic<int> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&, t]() {
      std::string payload(static_cast<size_t>(kChunkSize), static_cast<char>('A' + t));
      for (int i = t; i < kChunkCount; i += kThreads) {
        std::string err = mgr.store_chunk_err(upload_id, i, payload, /*expected_hash=*/"");
        if (!err.empty()) {
          ++failures;
        }
      }
    });
  }
  for (auto& w : workers) w.join();

  EXPECT_EQ(failures.load(), 0);
  EXPECT_TRUE(mgr.is_complete(upload_id));

  std::string dest = dir_ + "/assembled.bin";
  int64_t size = mgr.assemble(upload_id, dest);
  EXPECT_EQ(size, kTotal);

  // Assembled file should exist with exactly the expected total size.
  std::error_code ec;
  auto sz = fs::file_size(dest, ec);
  ASSERT_FALSE(ec);
  EXPECT_EQ(static_cast<int64_t>(sz), kTotal);
}

TEST_F(UploadManagerTest, OffsetOverflow) {
  UploadManager mgr(dir_);
  constexpr int64_t kHugeChunk = std::numeric_limits<int64_t>::max() / 2;
  // chunk_count must be > 4 so index=4 is in-range and overflow is exercised.
  constexpr int kChunkCount = 10;

  std::string upload_id = mgr.create_session(
    "user-overflow", kHugeChunk, kChunkCount, kHugeChunk, nlohmann::json::object());
  ASSERT_FALSE(upload_id.empty());

  // A tiny body is fine — we should never reach pwrite because offset computation overflows.
  std::string err = mgr.store_chunk_err(upload_id, /*index=*/4, std::string(16, 'x'), "");
  EXPECT_EQ(err, "invalid_index");
}

TEST_F(UploadManagerTest, NegativeIndex) {
  UploadManager mgr(dir_);
  std::string upload_id =
    mgr.create_session("u", /*total_size=*/1024, /*chunk_count=*/4, /*chunk_size=*/256, {});
  ASSERT_FALSE(upload_id.empty());

  std::string err = mgr.store_chunk_err(upload_id, /*index=*/-1, std::string(256, 'a'), "");
  EXPECT_EQ(err, "invalid_index");
}

TEST_F(UploadManagerTest, IndexOutOfRange) {
  UploadManager mgr(dir_);
  std::string upload_id =
    mgr.create_session("u", /*total_size=*/1024, /*chunk_count=*/4, /*chunk_size=*/256, {});
  ASSERT_FALSE(upload_id.empty());

  // index == chunk_count is out of range (valid indices are [0, chunk_count))
  std::string err = mgr.store_chunk_err(upload_id, /*index=*/4, std::string(256, 'a'), "");
  EXPECT_EQ(err, "invalid_index");

  err = mgr.store_chunk_err(upload_id, /*index=*/99, std::string(256, 'a'), "");
  EXPECT_EQ(err, "invalid_index");
}

TEST_F(UploadManagerTest, OpenFailure) {
  // Skip this test if running as root — root bypasses DAC_OVERRIDE and can
  // always open the file regardless of mode bits.
  if (::geteuid() == 0) {
    GTEST_SKIP() << "Cannot test open() failure when running as root";
  }

  UploadManager mgr(dir_);
  std::string upload_id =
    mgr.create_session("u", /*total_size=*/1024, /*chunk_count=*/4, /*chunk_size=*/256, {});
  ASSERT_FALSE(upload_id.empty());

  // Make the on-disk temp file unreadable/unwritable so open(O_WRONLY) fails.
  auto session = mgr.get_session(upload_id);
  ASSERT_TRUE(session.has_value());
  std::string tmp_path = session->tmp_path;
  ASSERT_EQ(::chmod(tmp_path.c_str(), 0), 0) << "chmod failed: " << strerror(errno);

  std::string err = mgr.store_chunk_err(upload_id, /*index=*/0, std::string(256, 'a'), "");

  // Restore permissions BEFORE asserting so a failure doesn't break teardown.
  ::chmod(tmp_path.c_str(), 0644);

  EXPECT_EQ(err, "open_failed");
}
