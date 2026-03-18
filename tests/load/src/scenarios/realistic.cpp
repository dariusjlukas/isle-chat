#include "realistic.h"
#include "../setup.h"

#include <openssl/rand.h>

#include <iostream>
#include <mutex>

static std::once_flag realistic_once;
static RealisticShared* realistic_shared = nullptr;

static const std::vector<std::string> REALISTIC_SEARCH_TERMS = {
    "load", "test", "wiki", "task", "mixed",
};

static const std::vector<std::string> REACTION_EMOJIS = {
    "\xF0\x9F\x91\x8D",  // thumbs up
    "\xE2\x9D\xA4\xEF\xB8\x8F",  // heart
    "\xF0\x9F\x8E\x89",  // party
    "\xF0\x9F\x91\x80",  // eyes
};

RealisticShared& ensure_realistic_setup(const std::string& base_url, StatsCollector& stats) {
  std::call_once(realistic_once, [&]() {
    realistic_shared = new RealisticShared();

    HttpClient setup_http(base_url, stats);
    auto& admin = ensure_admin_setup(setup_http);

    realistic_shared->channel_id = create_public_channel(setup_http, admin.token, "load-mixed");
    realistic_shared->space_id = create_space(setup_http, admin.token, "Load Mixed Space");
    enable_space_tools(setup_http, admin.token, realistic_shared->space_id);

    realistic_shared->board_id =
        create_task_board(setup_http, admin.token, realistic_shared->space_id);
    realistic_shared->board_default_column_id = get_board_default_column(
        setup_http, admin.token, realistic_shared->space_id, realistic_shared->board_id);

    for (int i = 0; i < 3; i++) {
      create_wiki_page(setup_http, admin.token, realistic_shared->space_id,
                       "Mixed Wiki " + std::to_string(i));
      create_calendar_event(setup_http, admin.token, realistic_shared->space_id,
                            "Mixed Event " + std::to_string(i));
    }

    std::cerr << "  Realistic setup complete\n";
  });
  return *realistic_shared;
}

RealisticUser::RealisticUser(const std::string& base_url, StatsCollector& stats)
    : VirtualUser(base_url, stats) {}

void RealisticUser::setup() {
  auto& shared = ensure_realistic_setup(http_.base_url(), stats_);
  channel_id_ = shared.channel_id;
  space_id_ = shared.space_id;
  board_id_ = shared.board_id;
  column_id_ = shared.board_default_column_id;

  identity_ = PkiIdentity();
  register_and_login(http_, identity_);
  join_channel(http_, channel_id_);
  join_space(http_, space_id_);

  // Open WebSocket
  std::string ws_url = http_.base_url();
  if (ws_url.substr(0, 7) == "http://")
    ws_url = "ws://" + ws_url.substr(7);
  else if (ws_url.substr(0, 8) == "https://")
    ws_url = "wss://" + ws_url.substr(8);

  ws_ = std::make_unique<WsClient>(stats_);
  ws_->connect(ws_url + "/ws?token=" + http_.auth_token());
  ws_->start_receiver();
}

void RealisticUser::teardown() {
  if (ws_) ws_->close();
}

std::vector<WeightedTask> RealisticUser::get_tasks() {
  return {
      // WebSocket messaging (30%)
      {[this]() { send_chat_message(); }, 6, "send_chat_message"},
      {[this]() { send_typing(); }, 3, "send_typing"},
      {[this]() { mark_read(); }, 2, "mark_read"},
      {[this]() { add_reaction(); }, 1, "add_reaction"},

      // Channel/space browsing (25%)
      {[this]() { list_channels(); }, 4, "list_channels"},
      {[this]() { get_channel_messages(); }, 4, "get_channel_messages"},
      {[this]() { list_spaces(); }, 2, "list_spaces"},

      // Task/wiki/calendar CRUD (20%)
      {[this]() { create_task(); }, 2, "create_task"},
      {[this]() { list_wiki_pages(); }, 2, "list_wiki_pages"},
      {[this]() { list_calendar_events(); }, 1, "list_calendar_events"},
      {[this]() { list_task_boards(); }, 1, "list_task_boards"},

      // Notifications + profile (10%)
      {[this]() { list_notifications(); }, 2, "list_notifications"},
      {[this]() { get_user_profile(); }, 2, "get_user_profile"},

      // Search (10%)
      {[this]() { search_general(); }, 2, "search_general"},
      {[this]() { search_composite(); }, 2, "search_composite"},

      // File upload/download (5%)
      {[this]() { upload_small_file(); }, 1, "upload_small_file"},
      {[this]() { download_file(); }, 1, "download_file"},
  };
}

// --- WebSocket messaging (30%) ---

void RealisticUser::send_chat_message() {
  if (ws_) ws_->send_message(channel_id_);
}

void RealisticUser::send_typing() {
  if (ws_) ws_->send_typing(channel_id_);
}

void RealisticUser::mark_read() {
  if (ws_) ws_->mark_read(channel_id_);
}

void RealisticUser::add_reaction() {
  if (!ws_) return;
  std::uniform_int_distribution<size_t> dist(0, REACTION_EMOJIS.size() - 1);
  ws_->add_reaction(REACTION_EMOJIS[dist(rng_)]);
}

// --- Channel/space browsing (25%) ---

void RealisticUser::list_channels() {
  http_.get("/api/channels", {}, "/api/channels");
}

void RealisticUser::get_channel_messages() {
  http_.get("/api/channels/" + channel_id_ + "/messages?limit=50", {},
            "/api/channels/:id/messages");
}

void RealisticUser::list_spaces() {
  http_.get("/api/spaces", {}, "/api/spaces");
}

// --- Task/wiki/calendar CRUD (20%) ---

void RealisticUser::create_task() {
  if (column_id_.empty()) return;

  auto r = http_.post_json(
      "/api/spaces/" + space_id_ + "/tasks/boards/" + board_id_ + "/tasks",
      {{"title", "Mixed task " + random_hex(6)}, {"column_id", column_id_}}, {},
      "/api/spaces/:id/tasks/boards/:id/tasks [create]");

  if (r.ok()) {
    std::string task_id = r.json_body().value("id", "");
    if (!task_id.empty()) task_ids_.push_back(task_id);
  }
}

void RealisticUser::list_wiki_pages() {
  http_.get("/api/spaces/" + space_id_ + "/wiki/pages", {}, "/api/spaces/:id/wiki/pages");
}

void RealisticUser::list_calendar_events() {
  http_.get("/api/spaces/" + space_id_ +
                "/calendar/events?start=2026-01-01T00:00:00Z&end=2026-12-31T23:59:59Z",
            {}, "/api/spaces/:id/calendar/events");
}

void RealisticUser::list_task_boards() {
  http_.get("/api/spaces/" + space_id_ + "/tasks/boards", {}, "/api/spaces/:id/tasks/boards");
}

// --- Notifications + profile (10%) ---

void RealisticUser::list_notifications() {
  http_.get("/api/notifications", {}, "/api/notifications");
}

void RealisticUser::get_user_profile() {
  http_.get("/api/users/me", {}, "/api/users/me");
}

// --- Search (10%) ---

void RealisticUser::search_general() {
  std::uniform_int_distribution<size_t> dist(0, REALISTIC_SEARCH_TERMS.size() - 1);
  std::string term = REALISTIC_SEARCH_TERMS[dist(rng_)];
  http_.get("/api/search?q=" + term + "&type=messages&limit=20", {}, "/api/search");
}

void RealisticUser::search_composite() {
  std::uniform_int_distribution<size_t> dist(0, REALISTIC_SEARCH_TERMS.size() - 1);
  std::string term = REALISTIC_SEARCH_TERMS[dist(rng_)];
  http_.get("/api/search/composite?filters=channels:" + term + "&result_type=messages&limit=20",
            {}, "/api/search/composite");
}

// --- File upload/download (5%) ---

void RealisticUser::upload_small_file() {
  std::uniform_int_distribution<int> dist(1, 5);
  int size = dist(rng_) * 1024;

  std::string content(size, '\0');
  RAND_bytes(reinterpret_cast<unsigned char*>(content.data()), size);

  std::string filename = "mixed_" + random_hex(8) + ".bin";
  auto r = http_.post_raw("/api/spaces/" + space_id_ + "/files/upload?filename=" + filename +
                              "&content_type=application/octet-stream",
                          content, "application/octet-stream", {},
                          "/api/spaces/:id/files/upload");

  if (r.ok()) {
    std::string file_id = r.json_body().value("id", "");
    if (!file_id.empty()) {
      uploaded_file_ids_.push_back(file_id);
      if (uploaded_file_ids_.size() > 20) {
        uploaded_file_ids_.erase(uploaded_file_ids_.begin(),
                                 uploaded_file_ids_.begin() + 10);
      }
    }
  }
}

void RealisticUser::download_file() {
  if (uploaded_file_ids_.empty()) return;

  std::uniform_int_distribution<size_t> dist(0, uploaded_file_ids_.size() - 1);
  auto& file_id = uploaded_file_ids_[dist(rng_)];
  http_.get("/api/spaces/" + space_id_ + "/files/" + file_id + "/download", {},
            "/api/spaces/:id/files/:id/download");
}
