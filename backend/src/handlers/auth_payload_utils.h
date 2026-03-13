#pragma once

#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "db/database.h"
#include "handlers/handler_utils.h"

namespace auth_payload_utils {

std::string extract_challenge_from_client_data_b64(const std::string& client_data_json_b64);

json parse_challenge_extra(const std::string& extra_data_json);
bool challenge_has_type(const json& extra, const std::string& expected_type);

json build_registration_challenge_extra(const std::string& type,
                                        const std::string& username,
                                        const std::string& display_name,
                                        const std::string& user_handle,
                                        const std::optional<std::string>& invite_token = std::nullopt);
json build_authentication_challenge_extra();
json build_pki_challenge_extra(const std::string& type,
                               const std::optional<std::string>& public_key = std::nullopt);
json build_device_passkey_challenge_extra(const std::string& user_id,
                                          const std::string& device_token);

json build_passkey_registration_options(const Config& config,
                                        const std::string& challenge,
                                        const std::string& user_handle,
                                        const std::string& username,
                                        const std::string& display_name,
                                        const json& exclude_credentials = json::array());
json build_passkey_login_options(const Config& config, const std::string& challenge);
json build_challenge_response(const std::string& challenge);
json build_exclude_credentials(const std::vector<Database::WebAuthnCredential>& credentials);

json build_token_user_response(const std::string& token,
                               const json& user_json,
                               const json& extra = json::object());
json build_totp_setup_response(const std::string& secret, const std::string& uri);
json build_join_request_status_response(const std::string& status,
                                        const std::optional<std::string>& token = std::nullopt,
                                        const std::optional<json>& user = std::nullopt);

}  // namespace auth_payload_utils
