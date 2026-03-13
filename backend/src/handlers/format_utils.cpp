#include "handlers/format_utils.h"

#include <iomanip>
#include <sstream>
#include <vector>
#include <openssl/rand.h>

namespace format_utils {

std::string random_hex(int bytes) {
    if (bytes <= 0) return "";
    std::vector<unsigned char> buf(bytes);
    RAND_bytes(buf.data(), bytes);
    std::ostringstream oss;
    for (int i = 0; i < bytes; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(buf[i]);
    }
    return oss.str();
}

std::string format_size(int64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / (1024LL * 1024 * 1024)) + " GB";
}

}  // namespace format_utils
