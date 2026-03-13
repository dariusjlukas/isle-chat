#include "handlers/admin_approval_utils.h"

#include <nlohmann/json.hpp>
#include "auth/webauthn.h"

using json = nlohmann::json;

namespace admin_approval_utils {

ParsedCredentialData parse_credential_data(
    const std::string& auth_method,
    const std::string& credential_data,
    const std::function<std::pair<std::vector<std::string>, std::vector<std::string>>()>&
        recovery_key_generator) {
    ParsedCredentialData parsed;
    if (credential_data.empty()) return parsed;

    auto credential = json::parse(credential_data);

    if (auth_method == "pki") {
        parsed.pki_public_key = credential.at("public_key").get<std::string>();
        auto generated = recovery_key_generator();
        parsed.recovery_key_hashes = std::move(generated.second);
        return parsed;
    }

    if (auth_method == "passkey") {
        parsed.passkey_credential_id = credential.at("credential_id").get<std::string>();
        parsed.passkey_public_key = webauthn::base64url_decode(
            credential.at("public_key").get<std::string>());
        parsed.passkey_sign_count = credential.value("sign_count", 0);
        parsed.passkey_transports = credential.value("transports", "[]");
        return parsed;
    }

    if (auth_method == "password") {
        parsed.password_hash = credential.at("password_hash").get<std::string>();
    }

    return parsed;
}

}  // namespace admin_approval_utils
