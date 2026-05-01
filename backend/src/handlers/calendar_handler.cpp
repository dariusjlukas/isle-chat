#include "handlers/calendar_handler.h"
#include <algorithm>
#include <pqxx/pqxx>
#include "handlers/cors_utils.h"
#include "recurrence.h"

using json = nlohmann::json;

static json event_to_json(const CalendarEvent& e) {
  return {
    {"id", e.id},
    {"space_id", e.space_id},
    {"title", e.title},
    {"description", e.description},
    {"location", e.location},
    {"color", e.color},
    {"start_time", e.start_time},
    {"end_time", e.end_time},
    {"all_day", e.all_day},
    {"rrule", e.rrule},
    {"created_by", e.created_by},
    {"created_by_username", e.created_by_username},
    {"created_at", e.created_at},
    {"updated_at", e.updated_at}};
}

static json rsvp_to_json(const CalendarEventRsvp& r) {
  return {
    {"user_id", r.user_id},
    {"username", r.username},
    {"display_name", r.display_name},
    {"status", r.status},
    {"responded_at", r.responded_at}};
}

static json permission_to_json(const CalendarPermission& p) {
  return {
    {"id", p.id},
    {"space_id", p.space_id},
    {"user_id", p.user_id},
    {"username", p.username},
    {"display_name", p.display_name},
    {"permission", p.permission},
    {"granted_by", p.granted_by},
    {"granted_by_username", p.granted_by_username},
    {"created_at", p.created_at}};
}

// Compute the duration of the original event in seconds
static long long event_duration_seconds(const std::string& start_str, const std::string& end_str) {
  auto s = recurrence::parse_iso8601(start_str);
  auto e = recurrence::parse_iso8601(end_str);
  time_t st = timegm(&s);
  time_t et = timegm(&e);
  return static_cast<long long>(et - st);
}

template <bool SSL>
void CalendarHandler<SSL>::register_routes(uWS::TemplatedApp<SSL>& app) {
  // List events in date range (with recurrence expansion)
  app.get("/api/spaces/:id/calendar/events", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto range_start = std::string(req->getQuery("start"));
    auto range_end = std::string(req->getQuery("end"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  range_start = std::move(range_start),
                  range_end = std::move(range_end),
                  origin]() {
      auto user_id = get_user_id(res, aborted, token, origin);
      if (user_id.empty()) return;

      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;

      if (range_start.empty() || range_end.empty()) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("400")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"start and end query parameters required"})");
        });
        return;
      }

      auto events = db.list_calendar_events(space_id, range_start, range_end);
      std::string my_perm = get_access_level(space_id, user_id);

      json arr = json::array();

      for (const auto& e : events) {
        if (e.rrule.empty()) {
          // Non-recurring event: return as-is
          json obj = event_to_json(e);
          // Include user's RSVP
          auto rsvp = db.get_user_rsvp(e.id, user_id, "1970-01-01 00:00:00+00");
          obj["my_rsvp"] = rsvp ? *rsvp : "";
          obj["occurrence_date"] = nullptr;
          obj["is_exception"] = false;
          arr.push_back(obj);
        } else {
          // Recurring event: expand RRULE
          auto rule = recurrence::parse_rrule(e.rrule);
          auto occurrences = recurrence::expand_rrule(rule, e.start_time, range_start, range_end);
          auto exceptions = db.get_event_exceptions(e.id);

          long long duration = event_duration_seconds(e.start_time, e.end_time);

          for (const auto& occ_start : occurrences) {
            // Check if this occurrence has an exception
            bool is_deleted = false;
            bool is_exception = false;
            json obj = event_to_json(e);

            for (const auto& ex : exceptions) {
              auto ex_date = recurrence::parse_iso8601(ex.original_date);
              auto occ_date = recurrence::parse_iso8601(occ_start);
              if (
                ex_date.tm_year == occ_date.tm_year && ex_date.tm_mon == occ_date.tm_mon &&
                ex_date.tm_mday == occ_date.tm_mday && ex_date.tm_hour == occ_date.tm_hour &&
                ex_date.tm_min == occ_date.tm_min) {
                if (ex.is_deleted) {
                  is_deleted = true;
                  break;
                }
                // Apply overrides
                is_exception = true;
                if (!ex.title.empty()) obj["title"] = ex.title;
                if (!ex.description.empty()) obj["description"] = ex.description;
                if (!ex.location.empty()) obj["location"] = ex.location;
                if (!ex.color.empty()) obj["color"] = ex.color;
                if (!ex.start_time.empty()) obj["start_time"] = ex.start_time;
                if (!ex.end_time.empty()) obj["end_time"] = ex.end_time;
                obj["all_day"] = ex.all_day;
                break;
              }
            }

            if (is_deleted) continue;

            if (!is_exception) {
              // Set the occurrence's start/end based on the expansion
              obj["start_time"] = occ_start;
              // Compute end time by adding the original duration
              auto occ_tm = recurrence::parse_iso8601(occ_start);
              time_t occ_tt = timegm(&occ_tm);
              occ_tt += duration;
              std::tm end_tm{};
              gmtime_r(&occ_tt, &end_tm);
              obj["end_time"] = recurrence::format_iso8601(end_tm);
            }

            obj["occurrence_date"] = occ_start;
            obj["is_exception"] = is_exception;

            // Include user's RSVP for this occurrence
            auto rsvp = db.get_user_rsvp(e.id, user_id, occ_start);
            obj["my_rsvp"] = rsvp ? *rsvp : "";

            arr.push_back(obj);
          }
        }
      }

      // Sort by start_time
      std::sort(arr.begin(), arr.end(), [origin](const json& a, const json& b) {
        return a["start_time"].get<std::string>() < b["start_time"].get<std::string>();
      });

      json resp = {{"events", arr}, {"my_permission", my_perm}};
      auto resp_str = resp.dump();
      loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
      });
    });
  });

  // Get single event
  app.get("/api/spaces/:id/calendar/events/:eventId", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto event_id = std::string(req->getParameter("eventId"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  event_id = std::move(event_id),
                  origin]() {
      auto user_id = get_user_id(res, aborted, token, origin);
      if (user_id.empty()) return;

      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;

      auto event = db.find_calendar_event(event_id);
      if (!event || event->space_id != space_id) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Event not found"})");
        });
        return;
      }

      json resp = event_to_json(*event);
      auto rsvp = db.get_user_rsvp(event_id, user_id, "1970-01-01 00:00:00+00");
      resp["my_rsvp"] = rsvp ? *rsvp : "";
      resp["my_permission"] = get_access_level(space_id, user_id);

      auto resp_str = resp.dump();
      loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
      });
    });
  });

  // Create event
  app.post("/api/spaces/:id/calendar/events", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    body = std::move(body),
                    origin]() {
        auto user_id = get_user_id(res, aborted, token, origin);
        if (user_id.empty()) return;

        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_permission(res, aborted, space_id, user_id, "edit", origin)) return;

        try {
          auto j = json::parse(body);
          std::string title = j.at("title");
          std::string description = j.value("description", "");
          std::string location = j.value("location", "");
          std::string color = j.value("color", "blue");
          std::string start_time = j.at("start_time");
          std::string end_time = j.at("end_time");
          bool all_day = j.value("all_day", false);
          std::string rrule = j.value("rrule", "");

          if (title.empty() || title.length() > 255) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"json({"error":"Title is required (max 255 characters)"})json");
            });
            return;
          }

          auto event = db.create_calendar_event(
            space_id,
            title,
            description,
            location,
            color,
            start_time,
            end_time,
            all_day,
            rrule,
            user_id);
          auto creator = db.find_user_by_id(user_id);
          event.created_by_username = creator ? creator->username : "";
          json resp = event_to_json(event);
          resp["my_rsvp"] = "";
          auto resp_str = resp.dump();
          loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Update event
  app.put("/api/spaces/:id/calendar/events/:eventId", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto event_id = std::string(req->getParameter("eventId"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 event_id = std::move(event_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    event_id = std::move(event_id),
                    body = std::move(body),
                    origin]() {
        auto user_id = get_user_id(res, aborted, token, origin);
        if (user_id.empty()) return;

        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_permission(res, aborted, space_id, user_id, "edit", origin)) return;

        auto existing = db.find_calendar_event(event_id);
        if (!existing || existing->space_id != space_id) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Event not found"})");
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string title = j.value("title", existing->title);
          std::string description = j.value("description", existing->description);
          std::string location = j.value("location", existing->location);
          std::string color = j.value("color", existing->color);
          std::string start_time = j.value("start_time", existing->start_time);
          std::string end_time = j.value("end_time", existing->end_time);
          bool all_day = j.value("all_day", existing->all_day);
          std::string rrule = j.value("rrule", existing->rrule);

          if (title.empty() || title.length() > 255) {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"json({"error":"Title is required (max 255 characters)"})json");
            });
            return;
          }

          auto event = db.update_calendar_event(
            event_id, title, description, location, color, start_time, end_time, all_day, rrule);
          auto creator = db.find_user_by_id(event.created_by);
          event.created_by_username = creator ? creator->username : "";
          json resp = event_to_json(event);
          auto resp_str = resp.dump();
          loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Delete event
  app.del("/api/spaces/:id/calendar/events/:eventId", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto event_id = std::string(req->getParameter("eventId"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  event_id = std::move(event_id),
                  origin]() {
      auto user_id = get_user_id(res, aborted, token, origin);
      if (user_id.empty()) return;

      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;

      auto event = db.find_calendar_event(event_id);
      if (!event || event->space_id != space_id) {
        loop_->defer([res, aborted, origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeStatus("404")
            ->writeHeader("Content-Type", "application/json")
            ->end(R"({"error":"Event not found"})");
        });
        return;
      }

      // Creator can delete their own events; otherwise need owner permission
      if (event->created_by != user_id) {
        if (!require_permission(res, aborted, space_id, user_id, "owner", origin)) return;
      }

      db.delete_calendar_event(event_id);
      loop_->defer([res, aborted, origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      });
    });
  });

  // Create/update exception (edit or delete single occurrence of recurring event)
  app.post("/api/spaces/:id/calendar/events/:eventId/exception", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto event_id = std::string(req->getParameter("eventId"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 event_id = std::move(event_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    event_id = std::move(event_id),
                    body = std::move(body),
                    origin]() {
        auto user_id = get_user_id(res, aborted, token, origin);
        if (user_id.empty()) return;

        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_permission(res, aborted, space_id, user_id, "edit", origin)) return;

        auto event = db.find_calendar_event(event_id);
        if (!event || event->space_id != space_id) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Event not found"})");
          });
          return;
        }

        if (event->rrule.empty()) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Event is not recurring"})");
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string original_date = j.at("original_date");
          bool is_deleted = j.value("is_deleted", false);
          std::string title = j.value("title", "");
          std::string description = j.value("description", "");
          std::string location = j.value("location", "");
          std::string color = j.value("color", "");
          std::string start_time = j.value("start_time", "");
          std::string end_time = j.value("end_time", "");
          bool all_day = j.value("all_day", false);

          auto ex = db.create_event_exception(
            event_id,
            original_date,
            is_deleted,
            title,
            description,
            location,
            color,
            start_time,
            end_time,
            all_day);
          json resp = {
            {"id", ex.id},
            {"event_id", ex.event_id},
            {"original_date", ex.original_date},
            {"is_deleted", ex.is_deleted}};
          auto resp_str = resp.dump();
          loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Set RSVP
  app.post("/api/spaces/:id/calendar/events/:eventId/rsvp", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto event_id = std::string(req->getParameter("eventId"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 event_id = std::move(event_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    event_id = std::move(event_id),
                    body = std::move(body),
                    origin]() {
        auto user_id = get_user_id(res, aborted, token, origin);
        if (user_id.empty()) return;

        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;

        auto event = db.find_calendar_event(event_id);
        if (!event || event->space_id != space_id) {
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("404")
              ->writeHeader("Content-Type", "application/json")
              ->end(R"({"error":"Event not found"})");
          });
          return;
        }

        try {
          auto j = json::parse(body);
          std::string status = j.at("status");
          std::string occurrence_date = j.value("occurrence_date", "1970-01-01 00:00:00+00");

          if (status != "yes" && status != "no" && status != "maybe") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Status must be yes, no, or maybe"})");
            });
            return;
          }

          db.set_event_rsvp(event_id, user_id, occurrence_date, status);
          auto resp_str = json({{"status", status}}).dump();
          loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(resp_str);
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Get RSVPs for an event
  app.get("/api/spaces/:id/calendar/events/:eventId/rsvps", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto event_id = std::string(req->getParameter("eventId"));
    auto occurrence_date = std::string(req->getQuery("date"));
    if (occurrence_date.empty()) occurrence_date = "1970-01-01 00:00:00+00";
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  event_id = std::move(event_id),
                  occurrence_date = std::move(occurrence_date),
                  origin]() {
      auto user_id = get_user_id(res, aborted, token, origin);
      if (user_id.empty()) return;

      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;

      auto rsvps = db.get_event_rsvps(event_id, occurrence_date);

      json arr = json::array();
      for (const auto& r : rsvps) arr.push_back(rsvp_to_json(r));

      auto resp_str = json({{"rsvps", arr}}).dump();
      loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(resp_str);
      });
    });
  });

  // List calendar permissions
  app.get("/api/spaces/:id/calendar/permissions", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit(
      [this, res, aborted, token = std::move(token), space_id = std::move(space_id), origin]() {
        auto user_id = get_user_id(res, aborted, token, origin);
        if (user_id.empty()) return;

        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;

        auto perms = db.get_calendar_permissions(space_id);
        json arr = json::array();
        for (const auto& p : perms) arr.push_back(permission_to_json(p));

        std::string my_perm = get_access_level(space_id, user_id);

        json resp = {{"permissions", arr}, {"my_permission", my_perm}};
        auto resp_str = resp.dump();
        loop_->defer([res, aborted, resp_str = std::move(resp_str), origin]() {
          if (*aborted) return;
          cors::apply(res, origin);
          res->writeHeader("Content-Type", "application/json")->end(resp_str);
        });
      });
  });

  // Set calendar permission
  app.post("/api/spaces/:id/calendar/permissions", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    std::string body;
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    res->onData([this,
                 res,
                 aborted,
                 token = std::move(token),
                 space_id = std::move(space_id),
                 body = std::move(body),
                 origin](std::string_view data, bool last) mutable {
      body.append(data);
      if (!last) return;
      pool_.submit([this,
                    res,
                    aborted,
                    token = std::move(token),
                    space_id = std::move(space_id),
                    body = std::move(body),
                    origin]() {
        auto user_id = get_user_id(res, aborted, token, origin);
        if (user_id.empty()) return;

        if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
        if (!require_permission(res, aborted, space_id, user_id, "owner", origin)) return;

        try {
          auto j = json::parse(body);
          std::string target_user_id = j.at("user_id");
          std::string permission = j.at("permission");

          if (permission != "owner" && permission != "edit" && permission != "view") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Permission must be owner, edit, or view"})");
            });
            return;
          }

          // Personal spaces: only view and edit allowed, not owner
          auto space_perm_check = db.find_space_by_id(space_id);
          if (space_perm_check && space_perm_check->is_personal && permission == "owner") {
            loop_->defer([res, aborted, origin]() {
              if (*aborted) return;
              cors::apply(res, origin);
              res->writeStatus("400")
                ->writeHeader("Content-Type", "application/json")
                ->end(R"({"error":"Cannot assign owner permission in a personal space"})");
            });
            return;
          }

          db.set_calendar_permission(space_id, target_user_id, permission, user_id);
          loop_->defer([res, aborted, origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
          });
        } catch (const std::exception& e) {
          auto err = json({{"error", e.what()}}).dump();
          loop_->defer([res, aborted, err = std::move(err), origin]() {
            if (*aborted) return;
            cors::apply(res, origin);
            res->writeStatus("400")->writeHeader("Content-Type", "application/json")->end(err);
          });
        }
      });
    });
  });

  // Remove calendar permission
  app.del("/api/spaces/:id/calendar/permissions/:userId", [this](auto* res, auto* req) {
    std::string origin(req->getHeader("origin"));
    auto token = extract_bearer_token(req);
    auto space_id = std::string(req->getParameter("id"));
    auto target_user_id = std::string(req->getParameter("userId"));
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted, origin]() { *aborted = true; });
    pool_.submit([this,
                  res,
                  aborted,
                  token = std::move(token),
                  space_id = std::move(space_id),
                  target_user_id = std::move(target_user_id),
                  origin]() {
      auto user_id = get_user_id(res, aborted, token, origin);
      if (user_id.empty()) return;

      if (!check_space_access(res, aborted, space_id, user_id, origin)) return;
      if (!require_permission(res, aborted, space_id, user_id, "owner", origin)) return;

      db.remove_calendar_permission(space_id, target_user_id);
      loop_->defer([res, aborted, origin]() {
        if (*aborted) return;
        cors::apply(res, origin);
        res->writeHeader("Content-Type", "application/json")->end(R"({"ok":true})");
      });
    });
  });
}

template <bool SSL>
std::string CalendarHandler<SSL>::get_user_id(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& token,
  const std::string& origin) {
  auto user_id = db.validate_session(token);
  if (!user_id) {
    loop_->defer([res, aborted, origin]() {
      if (*aborted) return;
      cors::apply(res, origin);
      res->writeStatus("401")
        ->writeHeader("Content-Type", "application/json")
        ->end(R"({"error":"Unauthorized"})");
    });
    return "";
  }
  return *user_id;
}

template <bool SSL>
bool CalendarHandler<SSL>::check_space_access(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& space_id,
  const std::string& user_id,
  const std::string& origin) {
  if (db.is_space_member(space_id, user_id)) return true;
  auto user = db.find_user_by_id(user_id);
  if (user && (user->role == "admin" || user->role == "owner")) return true;

  // Allow access to personal spaces if user has per-resource permissions
  auto space = db.find_space_by_id(space_id);
  if (space && space->is_personal) {
    if (db.has_resource_permission_in_space(space_id, user_id, "calendar")) return true;
  }

  loop_->defer([res, aborted, origin]() {
    if (*aborted) return;
    cors::apply(res, origin);
    res->writeStatus("403")
      ->writeHeader("Content-Type", "application/json")
      ->end(R"({"error":"Not a member of this space"})");
  });
  return false;
}

template <bool SSL>
std::string CalendarHandler<SSL>::get_access_level(
  const std::string& space_id, const std::string& user_id) {
  // Server admin/owner → full access
  auto user = db.find_user_by_id(user_id);
  if (user && (user->role == "admin" || user->role == "owner")) return "owner";

  // Space admin/owner → full access
  auto space_role = db.get_space_member_role(space_id, user_id);
  if (space_role == "admin" || space_role == "owner") return "owner";

  // "user" role members default to "view"; calendar-level permissions can escalate
  auto cal_perm = db.get_calendar_permission(space_id, user_id);
  if (!cal_perm.empty()) {
    return cal_perm;
  }

  return "view";
}

template <bool SSL>
bool CalendarHandler<SSL>::require_permission(
  uWS::HttpResponse<SSL>* res,
  std::shared_ptr<bool> aborted,
  const std::string& space_id,
  const std::string& user_id,
  const std::string& required_level,
  const std::string& origin) {
  auto level = get_access_level(space_id, user_id);
  if (perm_rank(level) >= perm_rank(required_level)) return true;

  auto err = json({{"error", "Requires " + required_level + " permission"}}).dump();
  loop_->defer([res, aborted, err = std::move(err), origin]() {
    if (*aborted) return;
    cors::apply(res, origin);
    res->writeStatus("403")->writeHeader("Content-Type", "application/json")->end(err);
  });
  return false;
}

template <bool SSL>
int CalendarHandler<SSL>::perm_rank(const std::string& p) {
  if (p == "owner") return 2;
  if (p == "edit") return 1;
  return 0;
}

template struct CalendarHandler<false>;
template struct CalendarHandler<true>;
