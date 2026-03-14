#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <nlohmann/json.hpp>

struct UploadSession {
    std::string upload_id;
    std::string user_id;
    int64_t total_size;
    int chunk_count;
    int64_t chunk_size;       // bytes per chunk (last chunk may be smaller)
    std::string tmp_path;     // single temp file written at chunk offsets
    std::set<int> received_chunks;
    std::chrono::steady_clock::time_point created_at;
    nlohmann::json metadata;
};

class UploadManager {
public:
    explicit UploadManager(const std::string& upload_dir);

    // Create a new upload session, returns upload_id
    std::string create_session(const std::string& user_id, int64_t total_size,
                               int chunk_count, int64_t chunk_size,
                               const nlohmann::json& metadata);

    // Get session by ID, returns nullptr if not found
    UploadSession* get_session(const std::string& upload_id);

    // Write chunk data directly at its offset in the temp file.
    // If expected_hash is non-empty, verifies SHA-256 of data matches.
    bool store_chunk(const std::string& upload_id, int index,
                     std::string_view data, const std::string& expected_hash);

    // Returns "hash_mismatch" if hash failed, empty string on success,
    // or other error string on failure.
    std::string store_chunk_err(const std::string& upload_id, int index,
                                std::string_view data, const std::string& expected_hash);

    // Check if all chunks have been received
    bool is_complete(const std::string& upload_id) const;

    // Move temp file to dest_path (instant rename), returns total_size or -1
    int64_t assemble(const std::string& upload_id, const std::string& dest_path);

    // Remove session and clean up temp file
    void remove_session(const std::string& upload_id);

    // Clean up sessions older than max_age_seconds
    void cleanup_stale(int max_age_seconds = 3600);

private:
    std::string upload_dir_;
    std::string tmp_dir_;
    std::map<std::string, UploadSession> sessions_;
};
