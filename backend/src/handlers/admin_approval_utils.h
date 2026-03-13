#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace admin_approval_utils {

struct ParsedCredentialData {
    std::optional<std::string> pki_public_key;
    std::vector<std::string> recovery_key_hashes;

    std::optional<std::string> passkey_credential_id;
    std::vector<unsigned char> passkey_public_key;
    uint32_t passkey_sign_count = 0;
    std::string passkey_transports = "[]";

    std::optional<std::string> password_hash;
};

ParsedCredentialData parse_credential_data(
    const std::string& auth_method,
    const std::string& credential_data,
    const std::function<std::pair<std::vector<std::string>, std::vector<std::string>>()>&
        recovery_key_generator);

}  // namespace admin_approval_utils
