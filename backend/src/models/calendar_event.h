#pragma once
#include <string>

struct CalendarEvent {
    std::string id;
    std::string space_id;
    std::string title;
    std::string description;
    std::string location;
    std::string color;
    std::string start_time;
    std::string end_time;
    bool all_day = false;
    std::string rrule;
    std::string created_by;
    std::string created_by_username;
    std::string created_at;
    std::string updated_at;
};

struct CalendarEventException {
    std::string id;
    std::string event_id;
    std::string original_date;
    bool is_deleted = false;
    std::string title;
    std::string description;
    std::string location;
    std::string color;
    std::string start_time;
    std::string end_time;
    bool all_day = false;
    std::string created_at;
};

struct CalendarEventRsvp {
    std::string event_id;
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string occurrence_date;
    std::string status;
    std::string responded_at;
};

struct CalendarPermission {
    std::string id;
    std::string space_id;
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string permission;
    std::string granted_by;
    std::string granted_by_username;
    std::string created_at;
};
