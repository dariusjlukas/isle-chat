#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <iostream>
#include <filesystem>
#include "db/database.h"
#include "config.h"

using json = nlohmann::json;

struct WsUserData {
    std::string user_id;
    std::string username;
    std::string role;
};

template <bool SSL>
class WsHandler {
public:
    Database& db;
    const Config& config;

    WsHandler(Database& db, const Config& config);

    void register_routes(uWS::TemplatedApp<SSL>& app);
    void close_all();
    void disconnect_user(const std::string& user_id);
    void send_to_user(const std::string& user_id, const std::string& message);
    void subscribe_user_to_channel(const std::string& user_id, const std::string& channel_id);
    void unsubscribe_user_from_channel(const std::string& user_id, const std::string& channel_id);
    void broadcast_to_channel(const std::string& channel_id, const std::string& message);
    void subscribe_admins_to_channel(Database& database, const std::string& channel_id);
    void subscribe_user_to_space(const std::string& user_id, const std::string& space_id);
    void unsubscribe_user_from_space(const std::string& user_id, const std::string& space_id);
    void broadcast_to_presence(const std::string& message);
    void broadcast_to_space(const std::string& space_id, const std::string& message);
    void subscribe_admins_to_space(Database& database, const std::string& space_id);
    void disconnect_non_admins(const std::string& notify_message);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_set<uWS::WebSocket<SSL, true, WsUserData>*>> user_sockets_;

    void handle_message(uWS::WebSocket<SSL, true, WsUserData>* ws, std::string_view raw);
    void handle_send_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                              WsUserData* data, const json& j);
    void handle_edit_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                              WsUserData* data, const json& j);
    void handle_delete_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                                WsUserData* data, const json& j);
    void handle_typing(uWS::WebSocket<SSL, true, WsUserData>* ws,
                        WsUserData* data, const json& j);
    void handle_mark_read(uWS::WebSocket<SSL, true, WsUserData>* ws,
                           WsUserData* data, const json& j);
    void handle_add_reaction(uWS::WebSocket<SSL, true, WsUserData>* ws,
                              WsUserData* data, const json& j);
    void handle_remove_reaction(uWS::WebSocket<SSL, true, WsUserData>* ws,
                                 WsUserData* data, const json& j);

    static std::vector<std::string> parse_mentions(const std::string& content,
                                                     const std::vector<Database::ChannelMemberUsername>& members);
    static json message_to_json(const Message& msg);
};
