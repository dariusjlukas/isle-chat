#include <gtest/gtest.h>
#include "handlers/file_handler.h"
#include "handlers/format_utils.h"

// --- format_size ---

TEST(FileHandler_FormatSize, Bytes) {
    EXPECT_EQ(format_utils::format_size(0), "0 B");
    EXPECT_EQ(format_utils::format_size(1), "1 B");
    EXPECT_EQ(format_utils::format_size(512), "512 B");
    EXPECT_EQ(format_utils::format_size(1023), "1023 B");
}

TEST(FileHandler_FormatSize, Kilobytes) {
    EXPECT_EQ(format_utils::format_size(1024), "1 KB");
    EXPECT_EQ(format_utils::format_size(2048), "2 KB");
    EXPECT_EQ(format_utils::format_size(1024 * 500), "500 KB");
    EXPECT_EQ(format_utils::format_size(1024 * 1024 - 1), "1023 KB");
}

TEST(FileHandler_FormatSize, Megabytes) {
    EXPECT_EQ(format_utils::format_size(1024 * 1024), "1 MB");
    EXPECT_EQ(format_utils::format_size(1024 * 1024 * 5), "5 MB");
    EXPECT_EQ(format_utils::format_size(1024 * 1024 * 100), "100 MB");
    EXPECT_EQ(format_utils::format_size(1024LL * 1024 * 1024 - 1), "1023 MB");
}

TEST(FileHandler_FormatSize, Gigabytes) {
    EXPECT_EQ(format_utils::format_size(1024LL * 1024 * 1024), "1 GB");
    EXPECT_EQ(format_utils::format_size(1024LL * 1024 * 1024 * 5), "5 GB");
}

// --- random_hex ---

TEST(FileHandler_RandomHex, CorrectLength) {
    // Each byte becomes 2 hex characters
    EXPECT_EQ(format_utils::random_hex(1).size(), 2);
    EXPECT_EQ(format_utils::random_hex(4).size(), 8);
    EXPECT_EQ(format_utils::random_hex(16).size(), 32);
    EXPECT_EQ(format_utils::random_hex(32).size(), 64);
}

TEST(FileHandler_RandomHex, AllHexCharacters) {
    auto result = format_utils::random_hex(32);
    for (char c : result) {
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)))
            << "Non-hex character: " << c;
    }
}

TEST(FileHandler_RandomHex, DifferentCallsProduceDifferentValues) {
    auto a = format_utils::random_hex(16);
    auto b = format_utils::random_hex(16);
    EXPECT_NE(a, b);
}

TEST(FileHandler_RandomHex, ZeroBytes) {
    EXPECT_EQ(format_utils::random_hex(0), "");
}
