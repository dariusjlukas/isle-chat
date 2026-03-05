#pragma once
#include <App.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <iostream>
#include "db/database.h"

using json = nlohmann::json;

struct WsUserData {
    std::string user_id;
    std::string username;
};

template <bool SSL>
class WsHandler {
public:
    Database& db;

    explicit WsHandler(Database& db) : db(db) {}

    void register_routes(uWS::TemplatedApp<SSL>& app) {
        app.template ws<WsUserData>("/ws", {
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 64 * 1024,
            .idleTimeout = 120,
            .maxBackpressure = 1 * 1024 * 1024,

            .upgrade = [this](auto* res, auto* req, auto* context) {
                std::string token(req->getQuery("token"));
                auto user_id = db.validate_session(token);
                if (!user_id) {
                    res->writeStatus("401")->end("Unauthorized");
                    return;
                }

                auto user = db.find_user_by_id(*user_id);
                if (!user) {
                    res->writeStatus("401")->end("Unauthorized");
                    return;
                }

                res->template upgrade<WsUserData>(
                    {.user_id = user->id, .username = user->username},
                    req->getHeader("sec-websocket-key"),
                    req->getHeader("sec-websocket-protocol"),
                    req->getHeader("sec-websocket-extensions"),
                    context
                );
            },

            .open = [this](auto* ws) {
                auto* data = ws->getUserData();
                std::cout << "[WS] User connected: " << data->username << std::endl;

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    user_sockets_[data->user_id].insert(ws);
                }

                db.set_user_online(data->user_id, true);

                // Subscribe to user's channels
                auto channels = db.list_user_channels(data->user_id);
                for (const auto& ch : channels) {
                    ws->subscribe("channel:" + ch.id);
                }

                // Subscribe to user's spaces
                auto spaces = db.list_user_spaces(data->user_id);
                for (const auto& sp : spaces) {
                    ws->subscribe("space:" + sp.id);
                }

                // Server admins subscribe to all non-DM channels and all spaces
                auto user = db.find_user_by_id(data->user_id);
                if (user && user->role == "admin") {
                    auto all_channels = db.list_all_channels();
                    for (const auto& ch : all_channels) {
                        ws->subscribe("channel:" + ch.id);
                    }
                    auto all_spaces = db.list_all_spaces();
                    for (const auto& sp : all_spaces) {
                        ws->subscribe("space:" + sp.id);
                    }
                }

                // Send initial unread counts
                {
                    auto unread = db.get_unread_counts(data->user_id);
                    auto mention_unread = db.get_mention_unread_counts(data->user_id);
                    json counts_msg = {
                        {"type", "unread_counts"},
                        {"counts", json::object()},
                        {"mention_counts", json::object()}
                    };
                    for (const auto& uc : unread) {
                        counts_msg["counts"][uc.channel_id] = uc.count;
                    }
                    for (const auto& mc : mention_unread) {
                        counts_msg["mention_counts"][mc.channel_id] = mc.count;
                    }
                    ws->send(counts_msg.dump(), uWS::OpCode::TEXT);
                }

                // Subscribe to presence before broadcasting so other users' events are received
                ws->subscribe("presence");

                // Broadcast online status
                json online_msg = {{"type", "user_online"},
                                    {"user_id", data->user_id},
                                    {"username", data->username}};
                ws->publish("presence", online_msg.dump());
            },

            .message = [this](auto* ws, std::string_view message, uWS::OpCode) {
                handle_message(ws, message);
            },

            .close = [this](auto* ws, int, std::string_view) {
                auto* data = ws->getUserData();
                std::cout << "[WS] User disconnected: " << data->username << std::endl;

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = user_sockets_.find(data->user_id);
                    if (it != user_sockets_.end()) {
                        it->second.erase(ws);
                        if (it->second.empty()) {
                            user_sockets_.erase(it);
                        }
                    }
                }

                // Only mark offline if no other connections
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (user_sockets_.find(data->user_id) == user_sockets_.end()) {
                        db.set_user_online(data->user_id, false);
                        auto offline_user = db.find_user_by_id(data->user_id);
                        std::string last_seen = offline_user ? offline_user->last_seen : "";
                        json offline_msg = {{"type", "user_offline"},
                                             {"user_id", data->user_id},
                                             {"last_seen", last_seen}};
                        // uWS publish() excludes the sending socket, so we pick any
                        // connected socket to publish from, then send() directly to
                        // all sockets of that same user so they also receive it.
                        std::string msg_str = offline_msg.dump();
                        bool published = false;
                        for (auto& [uid, sockets] : user_sockets_) {
                            if (!sockets.empty()) {
                                auto* sender = *sockets.begin();
                                sender->publish("presence", msg_str);
                                // Direct send to all of this user's sockets (publish skipped them)
                                for (auto* s : sockets) {
                                    s->send(msg_str, uWS::OpCode::TEXT);
                                }
                                published = true;
                                break;
                            }
                        }
                        if (!published) {
                            ws->publish("presence", msg_str);
                        }
                    }
                }
            }
        });
    }

    void close_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [uid, sockets] : user_sockets_) {
            for (auto* ws : sockets) {
                ws->close();
            }
        }
    }

    void send_to_user(const std::string& user_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_sockets_.find(user_id);
        if (it != user_sockets_.end()) {
            for (auto* ws : it->second) {
                ws->send(message, uWS::OpCode::TEXT);
            }
        }
    }

    void subscribe_user_to_channel(const std::string& user_id, const std::string& channel_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_sockets_.find(user_id);
        if (it != user_sockets_.end()) {
            for (auto* ws : it->second) {
                ws->subscribe("channel:" + channel_id);
            }
        }
    }

    void unsubscribe_user_from_channel(const std::string& user_id, const std::string& channel_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_sockets_.find(user_id);
        if (it != user_sockets_.end()) {
            for (auto* ws : it->second) {
                ws->unsubscribe("channel:" + channel_id);
            }
        }
    }

    void broadcast_to_channel(const std::string& channel_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [uid, sockets] : user_sockets_) {
            if (!sockets.empty()) {
                (*sockets.begin())->publish("channel:" + channel_id, message);
                return;
            }
        }
    }

    void subscribe_admins_to_channel(Database& database, const std::string& channel_id) {
        auto users = database.list_users();
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& u : users) {
            if (u.role == "admin") {
                auto it = user_sockets_.find(u.id);
                if (it != user_sockets_.end()) {
                    for (auto* ws : it->second) {
                        ws->subscribe("channel:" + channel_id);
                    }
                }
            }
        }
    }

    void subscribe_user_to_space(const std::string& user_id, const std::string& space_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_sockets_.find(user_id);
        if (it != user_sockets_.end()) {
            for (auto* ws : it->second) {
                ws->subscribe("space:" + space_id);
            }
        }
    }

    void unsubscribe_user_from_space(const std::string& user_id, const std::string& space_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_sockets_.find(user_id);
        if (it != user_sockets_.end()) {
            for (auto* ws : it->second) {
                ws->unsubscribe("space:" + space_id);
            }
        }
    }

    void broadcast_to_presence(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [uid, sockets] : user_sockets_) {
            for (auto* ws : sockets) {
                ws->send(message, uWS::OpCode::TEXT);
            }
        }
    }

    void broadcast_to_space(const std::string& space_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [uid, sockets] : user_sockets_) {
            if (!sockets.empty()) {
                (*sockets.begin())->publish("space:" + space_id, message);
                return;
            }
        }
    }

    void subscribe_admins_to_space(Database& database, const std::string& space_id) {
        auto users = database.list_users();
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& u : users) {
            if (u.role == "admin") {
                auto it = user_sockets_.find(u.id);
                if (it != user_sockets_.end()) {
                    for (auto* ws : it->second) {
                        ws->subscribe("space:" + space_id);
                    }
                }
            }
        }
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::unordered_set<uWS::WebSocket<SSL, true, WsUserData>*>> user_sockets_;

    void handle_message(uWS::WebSocket<SSL, true, WsUserData>* ws, std::string_view raw) {
        auto* data = ws->getUserData();
        try {
            auto j = json::parse(raw);
            std::string type = j.at("type");

            if (type == "send_message") {
                handle_send_message(ws, data, j);
            } else if (type == "edit_message") {
                handle_edit_message(ws, data, j);
            } else if (type == "delete_message") {
                handle_delete_message(ws, data, j);
            } else if (type == "typing") {
                handle_typing(ws, data, j);
            } else if (type == "mark_read") {
                handle_mark_read(ws, data, j);
            } else if (type == "ping") {
                json pong = {{"type", "pong"}};
                ws->send(pong.dump(), uWS::OpCode::TEXT);
            }
        } catch (const std::exception& e) {
            json err = {{"type", "error"}, {"message", e.what()}};
            ws->send(err.dump(), uWS::OpCode::TEXT);
        }
    }

    void handle_send_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                              WsUserData* data, const json& j) {
        std::string channel_id = j.at("channel_id");
        std::string content = j.at("content");

        if (content.empty()) return;

        std::string role = db.get_effective_role(channel_id, data->user_id);
        if (role.empty()) {
            json err = {{"type", "error"}, {"message", "Not a member of this channel"}};
            ws->send(err.dump(), uWS::OpCode::TEXT);
            return;
        }
        if (role == "read") {
            json err = {{"type", "error"}, {"message", "You don't have permission to send messages in this channel"}};
            ws->send(err.dump(), uWS::OpCode::TEXT);
            return;
        }

        auto msg = db.create_message(channel_id, data->user_id, content);

        // Detect and store @mentions
        auto members = db.get_channel_member_usernames(channel_id);
        auto mentioned = parse_mentions(content, members);
        if (!mentioned.empty()) {
            db.store_mentions(msg.id, channel_id, content, members);
        }

        json broadcast = {
            {"type", "new_message"},
            {"message", message_to_json(msg)}
        };
        if (!mentioned.empty()) {
            broadcast["mentions"] = mentioned;
        }

        ws->publish("channel:" + channel_id, broadcast.dump());
        ws->send(broadcast.dump(), uWS::OpCode::TEXT);
    }

    void handle_edit_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                              WsUserData* data, const json& j) {
        std::string message_id = j.at("message_id");
        std::string content = j.at("content");

        if (content.empty()) return;

        auto msg = db.edit_message(message_id, data->user_id, content);

        json broadcast = {
            {"type", "message_edited"},
            {"message", message_to_json(msg)}
        };

        ws->publish("channel:" + msg.channel_id, broadcast.dump());
        ws->send(broadcast.dump(), uWS::OpCode::TEXT);
    }

    void handle_delete_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                                WsUserData* data, const json& j) {
        std::string message_id = j.at("message_id");

        auto msg = db.delete_message(message_id, data->user_id);

        json broadcast = {
            {"type", "message_deleted"},
            {"message", message_to_json(msg)}
        };

        ws->publish("channel:" + msg.channel_id, broadcast.dump());
        ws->send(broadcast.dump(), uWS::OpCode::TEXT);
    }

    void handle_typing(uWS::WebSocket<SSL, true, WsUserData>* ws,
                        WsUserData* data, const json& j) {
        std::string channel_id = j.at("channel_id");

        json broadcast = {
            {"type", "typing"},
            {"channel_id", channel_id},
            {"user_id", data->user_id},
            {"username", data->username}
        };

        ws->publish("channel:" + channel_id, broadcast.dump());
    }

    void handle_mark_read(uWS::WebSocket<SSL, true, WsUserData>* ws,
                           WsUserData* data, const json& j) {
        std::string channel_id = j.at("channel_id");
        std::string message_id = j.at("message_id");
        std::string timestamp = j.at("timestamp");

        db.update_read_state(channel_id, data->user_id, message_id, timestamp);

        json broadcast = {
            {"type", "read_receipt"},
            {"channel_id", channel_id},
            {"user_id", data->user_id},
            {"username", data->username},
            {"last_read_message_id", message_id},
            {"last_read_at", timestamp}
        };
        ws->publish("channel:" + channel_id, broadcast.dump());
    }

    static std::vector<std::string> parse_mentions(const std::string& content,
                                                     const std::vector<Database::ChannelMemberUsername>& members) {
        std::vector<std::string> mentioned;
        size_t pos = 0;
        while (pos < content.size()) {
            auto at = content.find('@', pos);
            if (at == std::string::npos) break;
            size_t start = at + 1;
            size_t end = start;
            while (end < content.size() && (std::isalnum(content[end]) || content[end] == '_' || content[end] == '-')) {
                ++end;
            }
            if (end > start) {
                std::string token = content.substr(start, end - start);
                if (token == "channel") {
                    mentioned.push_back("@channel");
                } else {
                    for (const auto& m : members) {
                        if (m.username == token) {
                            mentioned.push_back(token);
                            break;
                        }
                    }
                }
            }
            pos = end;
        }
        return mentioned;
    }

    static json message_to_json(const Message& msg) {
        json j = {
            {"id", msg.id},
            {"channel_id", msg.channel_id},
            {"user_id", msg.user_id},
            {"username", msg.username},
            {"content", msg.content},
            {"created_at", msg.created_at},
            {"is_deleted", msg.is_deleted}
        };
        if (!msg.edited_at.empty()) {
            j["edited_at"] = msg.edited_at;
        }
        if (!msg.file_id.empty()) {
            j["file_id"] = msg.file_id;
            j["file_name"] = msg.file_name;
            j["file_size"] = msg.file_size;
            j["file_type"] = msg.file_type;
        }
        return j;
    }
};
