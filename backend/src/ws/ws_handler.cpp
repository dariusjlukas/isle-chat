#include "ws/ws_handler.h"

using json = nlohmann::json;

template <bool SSL>
WsHandler<SSL>::WsHandler(Database& db, const Config& config) : db(db), config(config) {}

template <bool SSL>
void WsHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
    app.template ws<WsUserData>("/ws", {
        .compression = uWS::DISABLED,
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

            // Reject non-admin users during lockdown
            if (db.is_server_locked_down() && user->role != "admin" && user->role != "owner") {
                res->writeStatus("403")->end("Server is in lockdown mode");
                return;
            }

            res->template upgrade<WsUserData>(
                {.user_id = user->id, .username = user->username, .role = user->role},
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
            if (user && (user->role == "admin" || user->role == "owner")) {
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

            // Send initial unread notification count
            {
                int notif_count = db.get_unread_notification_count(data->user_id);
                json notif_msg = {
                    {"type", "notification_count"},
                    {"unread_count", notif_count}
                };
                ws->send(notif_msg.dump(), uWS::OpCode::TEXT);
            }

            // Subscribe to presence before broadcasting so other users' events are received
            ws->subscribe("presence");

            // Broadcast online status (publish sends to others, send to self)
            json online_msg = {{"type", "user_online"},
                                {"user_id", data->user_id},
                                {"username", data->username}};
            auto online_str = online_msg.dump();
            ws->publish("presence", online_str);
            ws->send(online_str, uWS::OpCode::TEXT);
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

template <bool SSL>
void WsHandler<SSL>::close_all() {
    std::vector<uWS::WebSocket<SSL, true, WsUserData>*> to_close;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [uid, sockets] : user_sockets_) {
            for (auto* ws : sockets) {
                to_close.push_back(ws);
            }
        }
    }
    for (auto* ws : to_close) {
        ws->close();
    }
}

template <bool SSL>
void WsHandler<SSL>::disconnect_user(const std::string& user_id) {
    std::vector<uWS::WebSocket<SSL, true, WsUserData>*> to_close;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_sockets_.find(user_id);
        if (it != user_sockets_.end()) {
            for (auto* ws : it->second) {
                to_close.push_back(ws);
            }
        }
    }
    for (auto* ws : to_close) {
        ws->close();
    }
}

template <bool SSL>
void WsHandler<SSL>::send_to_user(const std::string& user_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_sockets_.find(user_id);
    if (it != user_sockets_.end()) {
        for (auto* ws : it->second) {
            ws->send(message, uWS::OpCode::TEXT);
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::subscribe_user_to_channel(const std::string& user_id, const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_sockets_.find(user_id);
    if (it != user_sockets_.end()) {
        for (auto* ws : it->second) {
            ws->subscribe("channel:" + channel_id);
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::unsubscribe_user_from_channel(const std::string& user_id, const std::string& channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_sockets_.find(user_id);
    if (it != user_sockets_.end()) {
        for (auto* ws : it->second) {
            ws->unsubscribe("channel:" + channel_id);
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::broadcast_to_channel(const std::string& channel_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string topic = "channel:" + channel_id;
    for (auto& [uid, sockets] : user_sockets_) {
        if (!sockets.empty()) {
            auto* ws = *sockets.begin();
            ws->publish(topic, message);
            // publish() excludes the publishing socket, so send to it directly
            if (ws->isSubscribed(topic)) {
                ws->send(message, uWS::OpCode::TEXT);
            }
            return;
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::subscribe_admins_to_channel(Database& database, const std::string& channel_id) {
    auto users = database.list_users();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& u : users) {
        if (u.role == "admin" || u.role == "owner") {
            auto it = user_sockets_.find(u.id);
            if (it != user_sockets_.end()) {
                for (auto* ws : it->second) {
                    ws->subscribe("channel:" + channel_id);
                }
            }
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::subscribe_user_to_space(const std::string& user_id, const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_sockets_.find(user_id);
    if (it != user_sockets_.end()) {
        for (auto* ws : it->second) {
            ws->subscribe("space:" + space_id);
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::unsubscribe_user_from_space(const std::string& user_id, const std::string& space_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_sockets_.find(user_id);
    if (it != user_sockets_.end()) {
        for (auto* ws : it->second) {
            ws->unsubscribe("space:" + space_id);
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::broadcast_to_presence(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [uid, sockets] : user_sockets_) {
        for (auto* ws : sockets) {
            ws->send(message, uWS::OpCode::TEXT);
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::broadcast_to_space(const std::string& space_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string topic = "space:" + space_id;
    for (auto& [uid, sockets] : user_sockets_) {
        if (!sockets.empty()) {
            auto* ws = *sockets.begin();
            ws->publish(topic, message);
            // publish() excludes the publishing socket, so send to it directly
            if (ws->isSubscribed(topic)) {
                ws->send(message, uWS::OpCode::TEXT);
            }
            return;
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::subscribe_admins_to_space(Database& database, const std::string& space_id) {
    auto users = database.list_users();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& u : users) {
        if (u.role == "admin" || u.role == "owner") {
            auto it = user_sockets_.find(u.id);
            if (it != user_sockets_.end()) {
                for (auto* ws : it->second) {
                    ws->subscribe("space:" + space_id);
                }
            }
        }
    }
}

template <bool SSL>
void WsHandler<SSL>::disconnect_non_admins(const std::string& notify_message) {
    std::vector<uWS::WebSocket<SSL, true, WsUserData>*> to_close;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [uid, sockets] : user_sockets_) {
            for (auto* ws : sockets) {
                auto* data = ws->getUserData();
                if (data->role != "admin" && data->role != "owner") {
                    if (!notify_message.empty()) {
                        ws->send(notify_message, uWS::OpCode::TEXT);
                    }
                    to_close.push_back(ws);
                }
            }
        }
    }
    for (auto* ws : to_close) {
        ws->close();
    }
}

template <bool SSL>
void WsHandler<SSL>::handle_message(uWS::WebSocket<SSL, true, WsUserData>* ws, std::string_view raw) {
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
        } else if (type == "add_reaction") {
            handle_add_reaction(ws, data, j);
        } else if (type == "remove_reaction") {
            handle_remove_reaction(ws, data, j);
        } else if (type == "ping") {
            json pong = {{"type", "pong"}};
            ws->send(pong.dump(), uWS::OpCode::TEXT);
        }
    } catch (const std::exception& e) {
        json err = {{"type", "error"}, {"message", e.what()}};
        ws->send(err.dump(), uWS::OpCode::TEXT);
    }
}

template <bool SSL>
void WsHandler<SSL>::handle_send_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                          WsUserData* data, const json& j) {
    std::string channel_id = j.at("channel_id");
    std::string content = j.at("content");

    if (content.empty()) return;

    // Check if server is archived
    if (db.is_server_archived()) {
        json err = {{"type", "error"}, {"message", "Server is archived. No new content can be created."}};
        ws->send(err.dump(), uWS::OpCode::TEXT);
        return;
    }

    // Check if channel is archived
    auto ch = db.find_channel_by_id(channel_id);
    if (ch && ch->is_archived) {
        json err = {{"type", "error"}, {"message", "This channel is archived"}};
        ws->send(err.dump(), uWS::OpCode::TEXT);
        return;
    }

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

    std::string reply_to;
    if (j.contains("reply_to_message_id") && j["reply_to_message_id"].is_string()) {
        reply_to = j["reply_to_message_id"].get<std::string>();
    }

    auto msg = db.create_message(channel_id, data->user_id, content, reply_to);

    // Detect and store @mentions
    auto members = db.get_channel_member_usernames(channel_id);
    auto mentioned = parse_mentions(content, members);
    if (!mentioned.empty()) {
        db.store_mentions(msg.id, channel_id, content, members, data->user_id);
    }

    // Create notifications for mentions
    std::string preview = content.size() > 200 ? content.substr(0, 200) + "..." : content;
    for (const auto& mention : mentioned) {
        if (mention == "@channel") {
            // Notify all channel members except sender
            for (const auto& m : members) {
                if (m.user_id != data->user_id) {
                    auto nid = db.create_notification(m.user_id, "mention",
                        data->user_id, channel_id, msg.id, preview);
                    json notif = {{"type", "new_notification"}, {"notification", {
                        {"id", nid}, {"user_id", m.user_id}, {"type", "mention"},
                        {"source_user_id", data->user_id}, {"source_username", data->username},
                        {"channel_id", channel_id}, {"channel_name", msg.channel_id},
                        {"message_id", msg.id}, {"space_id", ""},
                        {"content", preview}, {"created_at", msg.created_at}, {"is_read", false}
                    }}};
                    send_to_user(m.user_id, notif.dump());
                }
            }
        } else {
            // Individual mention
            for (const auto& m : members) {
                if (m.username == mention && m.user_id != data->user_id) {
                    auto nid = db.create_notification(m.user_id, "mention",
                        data->user_id, channel_id, msg.id, preview);
                    json notif = {{"type", "new_notification"}, {"notification", {
                        {"id", nid}, {"user_id", m.user_id}, {"type", "mention"},
                        {"source_user_id", data->user_id}, {"source_username", data->username},
                        {"channel_id", channel_id}, {"channel_name", ""},
                        {"message_id", msg.id}, {"space_id", ""},
                        {"content", preview}, {"created_at", msg.created_at}, {"is_read", false}
                    }}};
                    send_to_user(m.user_id, notif.dump());
                    break;
                }
            }
        }
    }

    // Create notification for reply (if replying to someone else's message)
    if (!reply_to.empty()) {
        auto reply_owner = db.get_message_ownership(reply_to);
        if (reply_owner && reply_owner->user_id != data->user_id) {
            // Check if this user was already notified via mention
            bool already_notified = false;
            for (const auto& mention : mentioned) {
                for (const auto& m : members) {
                    if (m.username == mention && m.user_id == reply_owner->user_id) {
                        already_notified = true;
                        break;
                    }
                }
                if (already_notified) break;
            }
            if (!already_notified) {
                auto nid = db.create_notification(reply_owner->user_id, "reply",
                    data->user_id, channel_id, msg.id, preview);
                json notif = {{"type", "new_notification"}, {"notification", {
                    {"id", nid}, {"user_id", reply_owner->user_id}, {"type", "reply"},
                    {"source_user_id", data->user_id}, {"source_username", data->username},
                    {"channel_id", channel_id}, {"channel_name", ""},
                    {"message_id", msg.id}, {"space_id", ""},
                    {"content", preview}, {"created_at", msg.created_at}, {"is_read", false}
                }}};
                send_to_user(reply_owner->user_id, notif.dump());
            }
        }
    }

    // Create notifications for DM messages (for recipients not already notified)
    if (ch && ch->is_direct) {
        // Collect user IDs already notified via mention or reply
        std::unordered_set<std::string> already_notified;
        for (const auto& mention : mentioned) {
            if (mention == "@channel") {
                for (const auto& m : members) {
                    if (m.user_id != data->user_id) already_notified.insert(m.user_id);
                }
            } else {
                for (const auto& m : members) {
                    if (m.username == mention && m.user_id != data->user_id) {
                        already_notified.insert(m.user_id);
                    }
                }
            }
        }
        if (!reply_to.empty()) {
            auto reply_owner = db.get_message_ownership(reply_to);
            if (reply_owner && reply_owner->user_id != data->user_id) {
                already_notified.insert(reply_owner->user_id);
            }
        }

        for (const auto& m : members) {
            if (m.user_id != data->user_id && already_notified.count(m.user_id) == 0) {
                auto nid = db.create_notification(m.user_id, "direct_message",
                    data->user_id, channel_id, msg.id, preview);
                json notif = {{"type", "new_notification"}, {"notification", {
                    {"id", nid}, {"user_id", m.user_id}, {"type", "direct_message"},
                    {"source_user_id", data->user_id}, {"source_username", data->username},
                    {"channel_id", channel_id}, {"channel_name", ""},
                    {"message_id", msg.id}, {"space_id", ""},
                    {"content", preview}, {"created_at", msg.created_at}, {"is_read", false}
                }}};
                send_to_user(m.user_id, notif.dump());
            }
        }
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

template <bool SSL>
void WsHandler<SSL>::handle_edit_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
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

template <bool SSL>
void WsHandler<SSL>::handle_delete_message(uWS::WebSocket<SSL, true, WsUserData>* ws,
                            WsUserData* data, const json& j) {
    std::string message_id = j.at("message_id");

    // Look up the message to determine ownership and channel
    auto ownership = db.get_message_ownership(message_id);
    if (!ownership) {
        json err = {{"type", "error"}, {"message", "Message not found"}};
        ws->send(err.dump(), uWS::OpCode::TEXT);
        return;
    }

    Message msg;
    if (ownership->user_id == data->user_id) {
        // Own message — always allowed
        msg = db.delete_message(message_id, data->user_id);
    } else {
        // Not own message — check if user has admin/owner effective role in this channel
        std::string effective = db.get_effective_role(ownership->channel_id, data->user_id);
        if (effective != "admin" && effective != "owner") {
            json err = {{"type", "error"}, {"message", "You don't have permission to delete this message"}};
            ws->send(err.dump(), uWS::OpCode::TEXT);
            return;
        }

        // Check that the message author is lower-ranked (server role hierarchy)
        auto author = db.find_user_by_id(ownership->user_id);
        if (author) {
            bool blocked = false;
            if (author->role == "owner") {
                // Nobody can delete an owner's messages (except themselves)
                blocked = true;
            } else if (author->role == "admin" && data->role != "owner") {
                // Only owners can delete admin messages
                blocked = true;
            }
            if (blocked) {
                json err = {{"type", "error"}, {"message", "You don't have permission to delete this message"}};
                ws->send(err.dump(), uWS::OpCode::TEXT);
                return;
            }
        }

        msg = db.admin_delete_message(message_id);
    }

    // Delete file from disk if message had an attachment
    if (!msg.file_id.empty()) {
        std::string path = config.upload_dir + "/" + msg.file_id;
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    json broadcast = {
        {"type", "message_deleted"},
        {"message", message_to_json(msg)}
    };

    ws->publish("channel:" + msg.channel_id, broadcast.dump());
    ws->send(broadcast.dump(), uWS::OpCode::TEXT);
}

template <bool SSL>
void WsHandler<SSL>::handle_typing(uWS::WebSocket<SSL, true, WsUserData>* ws,
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

template <bool SSL>
void WsHandler<SSL>::handle_mark_read(uWS::WebSocket<SSL, true, WsUserData>* ws,
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

template <bool SSL>
void WsHandler<SSL>::handle_add_reaction(uWS::WebSocket<SSL, true, WsUserData>* ws,
                          WsUserData* data, const json& j) {
    std::string message_id = j.at("message_id");
    std::string emoji = j.at("emoji");

    auto channel_id = db.get_message_channel_id(message_id);
    db.add_reaction(message_id, data->user_id, emoji);

    json broadcast = {
        {"type", "reaction_added"},
        {"message_id", message_id},
        {"channel_id", channel_id},
        {"emoji", emoji},
        {"user_id", data->user_id},
        {"username", data->username}
    };
    ws->publish("channel:" + channel_id, broadcast.dump());
    ws->send(broadcast.dump(), uWS::OpCode::TEXT);
}

template <bool SSL>
void WsHandler<SSL>::handle_remove_reaction(uWS::WebSocket<SSL, true, WsUserData>* ws,
                             WsUserData* data, const json& j) {
    std::string message_id = j.at("message_id");
    std::string emoji = j.at("emoji");

    auto channel_id = db.get_message_channel_id(message_id);
    db.remove_reaction(message_id, data->user_id, emoji);

    json broadcast = {
        {"type", "reaction_removed"},
        {"message_id", message_id},
        {"channel_id", channel_id},
        {"emoji", emoji},
        {"user_id", data->user_id},
        {"username", data->username}
    };
    ws->publish("channel:" + channel_id, broadcast.dump());
    ws->send(broadcast.dump(), uWS::OpCode::TEXT);
}

template <bool SSL>
std::vector<std::string> WsHandler<SSL>::parse_mentions(const std::string& content,
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

template <bool SSL>
json WsHandler<SSL>::message_to_json(const Message& msg) {
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
    if (!msg.reply_to_message_id.empty()) {
        j["reply_to_message_id"] = msg.reply_to_message_id;
        j["reply_to_username"] = msg.reply_to_username;
        j["reply_to_content"] = msg.reply_to_content;
        j["reply_to_is_deleted"] = msg.reply_to_is_deleted;
    }
    return j;
}

// Explicit template instantiations
template class WsHandler<false>;
template class WsHandler<true>;
