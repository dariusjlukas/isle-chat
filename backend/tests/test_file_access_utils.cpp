#include <gtest/gtest.h>
#include "handlers/file_access_utils.h"

TEST(FileAccessUtils, ParsesSizeSettingsWithFallbacks) {
    EXPECT_EQ(file_access_utils::parse_max_file_size(std::optional<std::string>{"2048"}, 12), 2048);
    EXPECT_EQ(file_access_utils::parse_max_file_size(std::optional<std::string>{"bad"}, 12), 12);
    EXPECT_EQ(file_access_utils::parse_max_storage_size(std::optional<std::string>{"8192"}), 8192);
    EXPECT_EQ(file_access_utils::parse_max_storage_size(std::optional<std::string>{"bad"}), 0);
    EXPECT_EQ(file_access_utils::parse_space_storage_limit(std::optional<std::string>{"4096"}), 4096);
    EXPECT_EQ(file_access_utils::parse_space_storage_limit(std::nullopt), 0);
}

TEST(FileAccessUtils, DetectsStorageLimitExceededOnlyWhenLimited) {
    EXPECT_FALSE(file_access_utils::exceeds_storage_limit(0, 100, 50));
    EXPECT_FALSE(file_access_utils::exceeds_storage_limit(200, 100, 100));
    EXPECT_TRUE(file_access_utils::exceeds_storage_limit(199, 100, 100));
}

TEST(FileAccessUtils, ValidatesHexIds) {
    EXPECT_FALSE(file_access_utils::is_valid_hex_id(""));
    EXPECT_TRUE(file_access_utils::is_valid_hex_id("a0B9ff"));
    EXPECT_FALSE(file_access_utils::is_valid_hex_id("abc-123"));
    EXPECT_FALSE(file_access_utils::is_valid_hex_id("../etc/passwd"));
}

TEST(FileAccessUtils, BuildsMessagesAndDispositionHeaders) {
    EXPECT_EQ(file_access_utils::file_too_large_message(1024), "File too large (max 1 KB)");
    EXPECT_EQ(file_access_utils::inline_disposition("photo.png"),
              "inline; filename=\"photo.png\"");
    EXPECT_EQ(file_access_utils::attachment_disposition("report.pdf"),
              "attachment; filename=\"report.pdf\"");
    EXPECT_EQ(file_access_utils::versioned_attachment_disposition(3, "report.pdf"),
              "attachment; filename=\"v3_report.pdf\"");
}

TEST(FileAccessUtils, SanitizesFilenamesInDispositionHeaders) {
    EXPECT_EQ(file_access_utils::inline_disposition("file\"name.txt"),
              "inline; filename=\"file'name.txt\"");
    EXPECT_EQ(file_access_utils::attachment_disposition("bad\r\nname.txt"),
              "attachment; filename=\"badname.txt\"");
}
