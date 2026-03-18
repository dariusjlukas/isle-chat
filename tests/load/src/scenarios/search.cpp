#include "search.h"
#include "../setup.h"

#include <iostream>
#include <mutex>

static const std::vector<std::string> SEARCH_TERMS = {
    "load", "test", "performance", "wiki", "task",
    "meeting", "update", "review", "deploy", "config",
};

static const std::vector<std::string> SEARCH_TYPES = {
    "messages", "files", "users", "channels", "wiki", "spaces",
};

static std::once_flag search_once;
static SearchShared* search_shared = nullptr;

SearchShared& ensure_search_setup(const std::string& base_url, StatsCollector& stats) {
  std::call_once(search_once, [&]() {
    search_shared = new SearchShared();

    HttpClient setup_http(base_url, stats);
    auto& admin = ensure_admin_setup(setup_http);

    search_shared->channel_id = create_public_channel(setup_http, admin.token, "load-search");
    search_shared->space_id = create_space(setup_http, admin.token, "Load Search Space");
    enable_space_tools(setup_http, admin.token, search_shared->space_id);

    for (auto& term : SEARCH_TERMS) {
      create_wiki_page(setup_http, admin.token, search_shared->space_id,
                       "Guide: " + term + " procedures",
                       "Detailed documentation about " + term +
                           " processes and best practices for the team.");
    }

    std::cerr << "  Search setup complete\n";
  });
  return *search_shared;
}

SearchUser::SearchUser(const std::string& base_url, StatsCollector& stats)
    : VirtualUser(base_url, stats) {}

void SearchUser::setup() {
  auto& shared = ensure_search_setup(http_.base_url(), stats_);
  channel_id_ = shared.channel_id;
  space_id_ = shared.space_id;

  identity_ = PkiIdentity();
  register_and_login(http_, identity_);
  join_channel(http_, channel_id_);
  join_space(http_, space_id_);
}

std::vector<WeightedTask> SearchUser::get_tasks() {
  return {
      {[this]() { search_messages(); }, 4, "search_messages"},
      {[this]() { search_composite(); }, 3, "search_composite"},
      {[this]() { search_with_type_filter(); }, 2, "search_with_type_filter"},
      {[this]() { list_channel_messages(); }, 1, "list_channel_messages"},
  };
}

std::string SearchUser::random_term() {
  std::uniform_int_distribution<size_t> dist(0, SEARCH_TERMS.size() - 1);
  return SEARCH_TERMS[dist(rng_)];
}

std::string SearchUser::random_search_type() {
  std::uniform_int_distribution<size_t> dist(0, SEARCH_TYPES.size() - 1);
  return SEARCH_TYPES[dist(rng_)];
}

void SearchUser::search_messages() {
  std::string term = random_term();
  http_.get("/api/search?q=" + term + "&type=messages&limit=20", {}, "/api/search [messages]");
}

void SearchUser::search_composite() {
  std::string term = random_term();
  http_.get("/api/search/composite?filters=channels:" + term + "&result_type=messages&limit=20",
            {}, "/api/search/composite");
}

void SearchUser::search_with_type_filter() {
  std::string term = random_term();
  std::string type = random_search_type();
  http_.get("/api/search?q=" + term + "&type=" + type + "&limit=20", {},
            "/api/search [" + type + "]");
}

void SearchUser::list_channel_messages() {
  http_.get("/api/channels/" + channel_id_ + "/messages?limit=50", {},
            "/api/channels/:id/messages");
}
