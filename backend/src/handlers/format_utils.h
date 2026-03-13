#pragma once

#include <cstdint>
#include <string>

namespace format_utils {

std::string random_hex(int bytes);
std::string format_size(int64_t bytes);

}  // namespace format_utils
