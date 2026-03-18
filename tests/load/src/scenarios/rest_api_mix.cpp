#include "rest_api_mix.h"
#include "../setup.h"

#include <iostream>
#include <mutex>

static std::once_flag rest_once;
static RestApiMixShared* rest_shared = nullptr;

RestApiMixShared& ensure_rest_api_mix_setup(const std::string& base_url, StatsCollector& stats) {
  std::call_once(rest_once, [&]() {
    rest_shared = new RestApiMixShared();

    HttpClient setup_http(base_url, stats);
    auto& admin = ensure_admin_setup(setup_http);

    rest_shared->channel_id = create_public_channel(setup_http, admin.token, "load-rest");
    rest_shared->space_id = create_space(setup_http, admin.token, "Load REST Space");
    enable_space_tools(setup_http, admin.token, rest_shared->space_id);

    rest_shared->board_id = create_task_board(setup_http, admin.token, rest_shared->space_id);
    rest_shared->board_default_column_id = get_board_default_column(
        setup_http, admin.token, rest_shared->space_id, rest_shared->board_id);

    for (int i = 0; i < 3; i++) {
      create_wiki_page(setup_http, admin.token, rest_shared->space_id,
                       "Load Wiki " + std::to_string(i));
      create_calendar_event(setup_http, admin.token, rest_shared->space_id,
                            "Load Event " + std::to_string(i));
    }

    std::cerr << "  REST API mix setup complete\n";
  });
  return *rest_shared;
}

RestApiMixUser::RestApiMixUser(const std::string& base_url, StatsCollector& stats)
    : VirtualUser(base_url, stats) {}

void RestApiMixUser::setup() {
  auto& shared = ensure_rest_api_mix_setup(http_.base_url(), stats_);
  channel_id_ = shared.channel_id;
  space_id_ = shared.space_id;
  board_id_ = shared.board_id;
  column_id_ = shared.board_default_column_id;

  identity_ = PkiIdentity();
  register_and_login(http_, identity_);
  join_channel(http_, channel_id_);
  join_space(http_, space_id_);
}

std::vector<WeightedTask> RestApiMixUser::get_tasks() {
  return {
      {[this]() { list_channels(); }, 5, "list_channels"},
      {[this]() { get_channel_messages(); }, 5, "get_channel_messages"},
      {[this]() { list_spaces(); }, 3, "list_spaces"},
      {[this]() { get_space_details(); }, 3, "get_space_details"},
      {[this]() { list_task_boards(); }, 2, "list_task_boards"},
      {[this]() { create_and_update_task(); }, 1, "create_and_update_task"},
      {[this]() { list_wiki_pages(); }, 2, "list_wiki_pages"},
      {[this]() { create_wiki_page(); }, 1, "create_wiki_page"},
      {[this]() { update_wiki_page(); }, 1, "update_wiki_page"},
      {[this]() { list_calendar_events(); }, 2, "list_calendar_events"},
      {[this]() { create_calendar_event(); }, 1, "create_calendar_event"},
      {[this]() { list_notifications(); }, 2, "list_notifications"},
      {[this]() { get_user_profile(); }, 2, "get_user_profile"},
      {[this]() { list_users(); }, 1, "list_users"},
      {[this]() { search(); }, 1, "search"},
  };
}

void RestApiMixUser::list_channels() {
  http_.get("/api/channels", {}, "/api/channels");
}

void RestApiMixUser::get_channel_messages() {
  http_.get("/api/channels/" + channel_id_ + "/messages?limit=50", {}, "/api/channels/:id/messages");
}

void RestApiMixUser::list_spaces() {
  http_.get("/api/spaces", {}, "/api/spaces");
}

void RestApiMixUser::get_space_details() {
  http_.get("/api/spaces/" + space_id_, {}, "/api/spaces/:id");
}

void RestApiMixUser::list_task_boards() {
  http_.get("/api/spaces/" + space_id_ + "/tasks/boards", {}, "/api/spaces/:id/tasks/boards");
}

void RestApiMixUser::create_and_update_task() {
  if (column_id_.empty()) return;

  auto r = http_.post_json(
      "/api/spaces/" + space_id_ + "/tasks/boards/" + board_id_ + "/tasks",
      {{"title", "Load task " + random_hex(6)}, {"column_id", column_id_}}, {},
      "/api/spaces/:id/tasks/boards/:id/tasks [create]");

  if (r.ok()) {
    auto j = r.json_body();
    std::string task_id = j.value("id", "");
    if (!task_id.empty()) {
      task_ids_.push_back(task_id);

      http_.put_json("/api/spaces/" + space_id_ + "/tasks/boards/" + board_id_ + "/tasks/" +
                         task_id,
                     {{"title", "Updated " + random_hex(6)}}, {},
                     "/api/spaces/:id/tasks/boards/:id/tasks/:id [update]");
    }
  }
}

void RestApiMixUser::list_wiki_pages() {
  http_.get("/api/spaces/" + space_id_ + "/wiki/pages", {}, "/api/spaces/:id/wiki/pages");
}

void RestApiMixUser::create_wiki_page() {
  auto r = http_.post_json("/api/spaces/" + space_id_ + "/wiki/pages",
                           {{"title", "Load Wiki " + random_hex(6)},
                            {"content", "Performance test wiki content."}},
                           {}, "/api/spaces/:id/wiki/pages [create]");
  if (r.ok()) {
    auto j = r.json_body();
    std::string page_id = j.value("id", "");
    if (!page_id.empty()) wiki_page_ids_.push_back(page_id);
  }
}

void RestApiMixUser::update_wiki_page() {
  if (wiki_page_ids_.empty()) return;
  auto& page_id = wiki_page_ids_.back();
  http_.put_json("/api/spaces/" + space_id_ + "/wiki/pages/" + page_id,
                 {{"content", "Updated content " + random_hex(8)}}, {},
                 "/api/spaces/:id/wiki/pages/:id [update]");
}

void RestApiMixUser::list_calendar_events() {
  http_.get("/api/spaces/" + space_id_ +
                "/calendar/events?start=2026-01-01T00:00:00Z&end=2026-12-31T23:59:59Z",
            {}, "/api/spaces/:id/calendar/events");
}

void RestApiMixUser::create_calendar_event() {
  http_.post_json("/api/spaces/" + space_id_ + "/calendar/events",
                  {{"title", "Load Event " + random_hex(6)},
                   {"start_time", "2026-04-01T10:00:00Z"},
                   {"end_time", "2026-04-01T11:00:00Z"}},
                  {}, "/api/spaces/:id/calendar/events [create]");
}

void RestApiMixUser::list_notifications() {
  http_.get("/api/notifications", {}, "/api/notifications");
}

void RestApiMixUser::get_user_profile() {
  http_.get("/api/users/me", {}, "/api/users/me");
}

void RestApiMixUser::list_users() {
  http_.get("/api/users", {}, "/api/users");
}

void RestApiMixUser::search() {
  http_.get("/api/search?q=load&type=messages&limit=20", {}, "/api/search");
}
