#include "recurrence.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace recurrence {

static const int MAX_OCCURRENCES = 1000;  // Safety limit per expansion

std::tm parse_iso8601(const std::string& s) {
    std::tm t{};
    // Handle formats: "2026-03-15T10:00:00Z", "2026-03-15T10:00:00+00:00",
    // "2026-03-15 10:00:00+00", "2026-03-15"
    if (s.size() >= 10) {
        t.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        t.tm_mon = std::stoi(s.substr(5, 2)) - 1;
        t.tm_mday = std::stoi(s.substr(8, 2));
    }
    if (s.size() >= 19) {
        t.tm_hour = std::stoi(s.substr(11, 2));
        t.tm_min = std::stoi(s.substr(14, 2));
        t.tm_sec = std::stoi(s.substr(17, 2));
    }
    t.tm_isdst = 0;
    return t;
}

std::string format_iso8601(const std::tm& t) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

static time_t tm_to_utc(std::tm t) {
    t.tm_isdst = 0;
    return timegm(&t);
}

static std::tm utc_to_tm(time_t tt) {
    std::tm t{};
    gmtime_r(&tt, &t);
    return t;
}

static bool tm_before(const std::tm& a, const std::tm& b) {
    if (a.tm_year != b.tm_year) return a.tm_year < b.tm_year;
    if (a.tm_mon != b.tm_mon) return a.tm_mon < b.tm_mon;
    if (a.tm_mday != b.tm_mday) return a.tm_mday < b.tm_mday;
    if (a.tm_hour != b.tm_hour) return a.tm_hour < b.tm_hour;
    if (a.tm_min != b.tm_min) return a.tm_min < b.tm_min;
    return a.tm_sec < b.tm_sec;
}

static bool tm_equal(const std::tm& a, const std::tm& b) {
    return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday
        && a.tm_hour == b.tm_hour && a.tm_min == b.tm_min && a.tm_sec == b.tm_sec;
}

static bool tm_before_or_equal(const std::tm& a, const std::tm& b) {
    return tm_before(a, b) || tm_equal(a, b);
}

static Weekday parse_weekday(const std::string& s) {
    if (s == "SU") return Weekday::SU;
    if (s == "MO") return Weekday::MO;
    if (s == "TU") return Weekday::TU;
    if (s == "WE") return Weekday::WE;
    if (s == "TH") return Weekday::TH;
    if (s == "FR") return Weekday::FR;
    if (s == "SA") return Weekday::SA;
    return Weekday::MO;  // fallback
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string part;
    while (std::getline(iss, part, delim)) {
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

RRule parse_rrule(const std::string& rrule_str) {
    RRule rule;
    if (rrule_str.empty()) return rule;

    // Strip "RRULE:" prefix if present
    std::string s = rrule_str;
    if (s.rfind("RRULE:", 0) == 0) s = s.substr(6);

    auto parts = split(s, ';');
    for (const auto& part : parts) {
        auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        std::string key = part.substr(0, eq);
        std::string val = part.substr(eq + 1);

        if (key == "FREQ") {
            if (val == "DAILY") rule.freq = Freq::DAILY;
            else if (val == "WEEKLY") rule.freq = Freq::WEEKLY;
            else if (val == "MONTHLY") rule.freq = Freq::MONTHLY;
            else if (val == "YEARLY") rule.freq = Freq::YEARLY;
        } else if (key == "INTERVAL") {
            rule.interval = std::max(1, std::stoi(val));
        } else if (key == "COUNT") {
            rule.count = std::stoi(val);
        } else if (key == "UNTIL") {
            // UNTIL can be in YYYYMMDD or YYYYMMDDTHHMMSSZ format
            std::string normalized;
            if (val.size() >= 8) {
                normalized = val.substr(0, 4) + "-" + val.substr(4, 2) + "-" + val.substr(6, 2);
                if (val.size() >= 15) {
                    normalized += "T" + val.substr(9, 2) + ":" + val.substr(11, 2) + ":" + val.substr(13, 2) + "Z";
                } else {
                    normalized += "T23:59:59Z";
                }
            }
            if (!normalized.empty()) rule.until = parse_iso8601(normalized);
        } else if (key == "BYDAY") {
            auto days = split(val, ',');
            for (const auto& d : days) {
                // Strip numeric prefix if present (e.g., "2MO" for "second Monday")
                std::string dayStr = d;
                while (!dayStr.empty() && (dayStr[0] == '-' || (dayStr[0] >= '0' && dayStr[0] <= '9')))
                    dayStr = dayStr.substr(1);
                if (dayStr.size() == 2) rule.by_day.push_back(parse_weekday(dayStr));
            }
        } else if (key == "BYMONTHDAY") {
            auto nums = split(val, ',');
            for (const auto& n : nums) rule.by_monthday.push_back(std::stoi(n));
        } else if (key == "BYMONTH") {
            auto nums = split(val, ',');
            for (const auto& n : nums) rule.by_month.push_back(std::stoi(n));
        }
    }
    return rule;
}

// Get last day of month for a given year/month
static int days_in_month(int year, int month) {
    // month is 0-based (tm_mon)
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 1) {
        int y = year + 1900;
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) return 29;
    }
    return days[month];
}

// Advance a tm by the rule's frequency and interval
static std::tm advance(const std::tm& t, Freq freq, int interval) {
    std::tm next = t;
    switch (freq) {
        case Freq::DAILY: {
            time_t tt = tm_to_utc(next);
            tt += 86400 * interval;
            return utc_to_tm(tt);
        }
        case Freq::WEEKLY: {
            time_t tt = tm_to_utc(next);
            tt += 86400 * 7 * interval;
            return utc_to_tm(tt);
        }
        case Freq::MONTHLY: {
            next.tm_mon += interval;
            while (next.tm_mon >= 12) { next.tm_mon -= 12; next.tm_year++; }
            // Clamp day to valid range for new month
            int max_day = days_in_month(next.tm_year, next.tm_mon);
            if (next.tm_mday > max_day) next.tm_mday = max_day;
            return next;
        }
        case Freq::YEARLY: {
            next.tm_year += interval;
            // Handle Feb 29 in non-leap years
            int max_day = days_in_month(next.tm_year, next.tm_mon);
            if (next.tm_mday > max_day) next.tm_mday = max_day;
            return next;
        }
    }
    return next;
}

// Check if a tm matches BYDAY/BYMONTHDAY/BYMONTH constraints
static bool matches_by_rules(const std::tm& t, const RRule& rule) {
    if (!rule.by_month.empty()) {
        int month = t.tm_mon + 1;  // 1-based
        if (std::find(rule.by_month.begin(), rule.by_month.end(), month) == rule.by_month.end())
            return false;
    }
    if (!rule.by_monthday.empty()) {
        int mday = t.tm_mday;
        bool found = false;
        for (int d : rule.by_monthday) {
            if (d > 0 && d == mday) { found = true; break; }
            if (d < 0) {
                int max_day = days_in_month(t.tm_year, t.tm_mon);
                if (max_day + 1 + d == mday) { found = true; break; }
            }
        }
        if (!found) return false;
    }
    // BYDAY is checked differently depending on frequency
    // For WEEKLY: the candidate dates are generated per-week, so BYDAY is applied as a filter
    // For other frequencies: BYDAY filters individual dates
    if (!rule.by_day.empty() && rule.freq != Freq::WEEKLY) {
        // Compute weekday for this date
        std::tm tmp = t;
        time_t tt = tm_to_utc(tmp);
        std::tm resolved{};
        gmtime_r(&tt, &resolved);
        int wday = resolved.tm_wday;
        bool found = false;
        for (auto d : rule.by_day) {
            if (static_cast<int>(d) == wday) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

std::vector<std::string> expand_rrule(const RRule& rule, const std::string& dtstart,
                                       const std::string& range_start, const std::string& range_end) {
    std::vector<std::string> results;
    std::tm start = parse_iso8601(dtstart);
    std::tm rs = parse_iso8601(range_start);
    std::tm re = parse_iso8601(range_end);
    int total_count = 0;

    // For WEEKLY with BYDAY, we iterate week-by-week and emit matching days
    if (rule.freq == Freq::WEEKLY && !rule.by_day.empty()) {
        // Align to start of week (Monday)
        std::tm week_start = start;
        // Find the Monday of the start week
        time_t st_tt = tm_to_utc(week_start);
        std::tm resolved{};
        gmtime_r(&st_tt, &resolved);
        int wday = resolved.tm_wday;  // 0=Sun
        int days_since_mon = (wday + 6) % 7;  // 0=Mon
        st_tt -= days_since_mon * 86400;
        week_start = utc_to_tm(st_tt);
        // Keep the original time
        week_start.tm_hour = start.tm_hour;
        week_start.tm_min = start.tm_min;
        week_start.tm_sec = start.tm_sec;

        std::tm current_week = week_start;
        while (true) {
            // For each day in this week, check if it matches BYDAY
            for (auto day : rule.by_day) {
                int target_wday = static_cast<int>(day);
                int mon_offset = (target_wday + 6) % 7;  // offset from Monday
                time_t week_tt = tm_to_utc(current_week);
                time_t day_tt = week_tt + mon_offset * 86400;
                std::tm candidate = utc_to_tm(day_tt);
                candidate.tm_hour = start.tm_hour;
                candidate.tm_min = start.tm_min;
                candidate.tm_sec = start.tm_sec;

                // Must be on or after dtstart
                if (tm_before(candidate, start)) continue;
                // Check UNTIL
                if (rule.until && tm_before(*rule.until, candidate)) return results;
                // Check COUNT
                if (rule.count && total_count >= *rule.count) return results;
                // Past range end?
                if (!tm_before(candidate, re)) return results;

                total_count++;

                // Within display range?
                if (tm_before_or_equal(rs, candidate)) {
                    results.push_back(format_iso8601(candidate));
                }

                if (static_cast<int>(results.size()) >= MAX_OCCURRENCES) return results;
            }
            // Advance by interval weeks
            time_t wt = tm_to_utc(current_week);
            wt += 86400 * 7 * rule.interval;
            current_week = utc_to_tm(wt);
            current_week.tm_hour = start.tm_hour;
            current_week.tm_min = start.tm_min;
            current_week.tm_sec = start.tm_sec;
        }
    }

    // For other frequencies (DAILY, MONTHLY, YEARLY, WEEKLY without BYDAY)
    std::tm current = start;
    int original_mday = start.tm_mday;  // remember original day for monthly/yearly clamping
    while (true) {
        // Check UNTIL
        if (rule.until && tm_before(*rule.until, current)) break;
        // Check COUNT
        if (rule.count && total_count >= *rule.count) break;
        // Past range end?
        if (!tm_before(current, re)) break;

        if (matches_by_rules(current, rule)) {
            total_count++;
            if (tm_before_or_equal(rs, current)) {
                results.push_back(format_iso8601(current));
            }
            if (static_cast<int>(results.size()) >= MAX_OCCURRENCES) break;
        }

        current = advance(current, rule.freq, rule.interval);

        // For monthly/yearly: restore original day-of-month then clamp,
        // so Jan 31 → Feb 28 → Mar 31 (not Mar 28)
        if (rule.freq == Freq::MONTHLY || rule.freq == Freq::YEARLY) {
            current.tm_mday = original_mday;
            int max_day = days_in_month(current.tm_year, current.tm_mon);
            if (current.tm_mday > max_day) current.tm_mday = max_day;
        }
    }

    return results;
}

}  // namespace recurrence
