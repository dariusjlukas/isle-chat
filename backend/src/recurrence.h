#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <optional>

namespace recurrence {

enum class Freq { DAILY, WEEKLY, MONTHLY, YEARLY };

// Weekday constants matching tm_wday (0=Sun, 1=Mon, ..., 6=Sat)
enum class Weekday { SU = 0, MO = 1, TU = 2, WE = 3, TH = 4, FR = 5, SA = 6 };

struct RRule {
    Freq freq = Freq::WEEKLY;
    int interval = 1;
    std::optional<int> count;
    std::optional<std::tm> until;
    std::vector<Weekday> by_day;        // BYDAY (weekdays)
    std::vector<int> by_monthday;       // BYMONTHDAY (1-31, or -1 for last)
    std::vector<int> by_month;          // BYMONTH (1-12)
};

// Parse an RRULE string like "FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE,FR"
RRule parse_rrule(const std::string& rrule_str);

// Expand an RRULE into occurrence start times within [range_start, range_end).
// dtstart is the original event start time (ISO 8601 string).
// Returns ISO 8601 timestamp strings.
std::vector<std::string> expand_rrule(const RRule& rule, const std::string& dtstart,
                                       const std::string& range_start, const std::string& range_end);

// Utility: parse ISO 8601 timestamp to std::tm (UTC)
std::tm parse_iso8601(const std::string& s);

// Utility: format std::tm as ISO 8601 string
std::string format_iso8601(const std::tm& t);

}  // namespace recurrence
