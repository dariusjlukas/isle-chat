#include <gtest/gtest.h>
#include "recurrence.h"

using namespace recurrence;

// --- parse_iso8601 ---

TEST(RecurrenceParseISO, FullTimestamp) {
    auto t = parse_iso8601("2026-03-15T10:30:00Z");
    EXPECT_EQ(t.tm_year, 126);  // 2026 - 1900
    EXPECT_EQ(t.tm_mon, 2);     // March (0-based)
    EXPECT_EQ(t.tm_mday, 15);
    EXPECT_EQ(t.tm_hour, 10);
    EXPECT_EQ(t.tm_min, 30);
    EXPECT_EQ(t.tm_sec, 0);
}

TEST(RecurrenceParseISO, DateOnly) {
    auto t = parse_iso8601("2026-01-05");
    EXPECT_EQ(t.tm_year, 126);
    EXPECT_EQ(t.tm_mon, 0);
    EXPECT_EQ(t.tm_mday, 5);
    EXPECT_EQ(t.tm_hour, 0);
    EXPECT_EQ(t.tm_min, 0);
    EXPECT_EQ(t.tm_sec, 0);
}

TEST(RecurrenceParseISO, TimestampWithOffset) {
    // Should parse the date/time portion, ignoring the offset
    auto t = parse_iso8601("2026-06-20 14:00:00+00");
    EXPECT_EQ(t.tm_year, 126);
    EXPECT_EQ(t.tm_mon, 5);   // June
    EXPECT_EQ(t.tm_mday, 20);
    EXPECT_EQ(t.tm_hour, 14);
    EXPECT_EQ(t.tm_min, 0);
}

// --- format_iso8601 ---

TEST(RecurrenceFormatISO, RoundTrip) {
    std::string input = "2026-03-15T10:30:00Z";
    auto t = parse_iso8601(input);
    auto output = format_iso8601(t);
    EXPECT_EQ(output, input);
}

TEST(RecurrenceFormatISO, MidnightRoundTrip) {
    auto t = parse_iso8601("2026-01-01T00:00:00Z");
    EXPECT_EQ(format_iso8601(t), "2026-01-01T00:00:00Z");
}

// --- parse_rrule ---

TEST(RecurrenceParseRRule, EmptyString) {
    auto rule = parse_rrule("");
    EXPECT_EQ(rule.freq, Freq::WEEKLY);  // default
    EXPECT_EQ(rule.interval, 1);
    EXPECT_FALSE(rule.count.has_value());
    EXPECT_FALSE(rule.until.has_value());
    EXPECT_TRUE(rule.by_day.empty());
}

TEST(RecurrenceParseRRule, DailyFrequency) {
    auto rule = parse_rrule("FREQ=DAILY");
    EXPECT_EQ(rule.freq, Freq::DAILY);
    EXPECT_EQ(rule.interval, 1);
}

TEST(RecurrenceParseRRule, WeeklyWithInterval) {
    auto rule = parse_rrule("FREQ=WEEKLY;INTERVAL=2");
    EXPECT_EQ(rule.freq, Freq::WEEKLY);
    EXPECT_EQ(rule.interval, 2);
}

TEST(RecurrenceParseRRule, MonthlyFrequency) {
    auto rule = parse_rrule("FREQ=MONTHLY");
    EXPECT_EQ(rule.freq, Freq::MONTHLY);
}

TEST(RecurrenceParseRRule, YearlyFrequency) {
    auto rule = parse_rrule("FREQ=YEARLY");
    EXPECT_EQ(rule.freq, Freq::YEARLY);
}

TEST(RecurrenceParseRRule, WithRRulePrefix) {
    auto rule = parse_rrule("RRULE:FREQ=DAILY;INTERVAL=3");
    EXPECT_EQ(rule.freq, Freq::DAILY);
    EXPECT_EQ(rule.interval, 3);
}

TEST(RecurrenceParseRRule, Count) {
    auto rule = parse_rrule("FREQ=DAILY;COUNT=5");
    EXPECT_EQ(rule.freq, Freq::DAILY);
    ASSERT_TRUE(rule.count.has_value());
    EXPECT_EQ(*rule.count, 5);
}

TEST(RecurrenceParseRRule, UntilShort) {
    auto rule = parse_rrule("FREQ=DAILY;UNTIL=20260401");
    ASSERT_TRUE(rule.until.has_value());
    EXPECT_EQ(rule.until->tm_year, 126);
    EXPECT_EQ(rule.until->tm_mon, 3);   // April
    EXPECT_EQ(rule.until->tm_mday, 1);
    EXPECT_EQ(rule.until->tm_hour, 23);  // defaults to end of day
    EXPECT_EQ(rule.until->tm_min, 59);
    EXPECT_EQ(rule.until->tm_sec, 59);
}

TEST(RecurrenceParseRRule, UntilFull) {
    auto rule = parse_rrule("FREQ=DAILY;UNTIL=20260401T120000Z");
    ASSERT_TRUE(rule.until.has_value());
    EXPECT_EQ(rule.until->tm_hour, 12);
    EXPECT_EQ(rule.until->tm_min, 0);
}

TEST(RecurrenceParseRRule, ByDay) {
    auto rule = parse_rrule("FREQ=WEEKLY;BYDAY=MO,WE,FR");
    ASSERT_EQ(rule.by_day.size(), 3u);
    EXPECT_EQ(rule.by_day[0], Weekday::MO);
    EXPECT_EQ(rule.by_day[1], Weekday::WE);
    EXPECT_EQ(rule.by_day[2], Weekday::FR);
}

TEST(RecurrenceParseRRule, ByDaySingleDay) {
    auto rule = parse_rrule("FREQ=WEEKLY;BYDAY=TU");
    ASSERT_EQ(rule.by_day.size(), 1u);
    EXPECT_EQ(rule.by_day[0], Weekday::TU);
}

TEST(RecurrenceParseRRule, ByMonthDay) {
    auto rule = parse_rrule("FREQ=MONTHLY;BYMONTHDAY=15");
    ASSERT_EQ(rule.by_monthday.size(), 1u);
    EXPECT_EQ(rule.by_monthday[0], 15);
}

TEST(RecurrenceParseRRule, ByMonthDayNegative) {
    auto rule = parse_rrule("FREQ=MONTHLY;BYMONTHDAY=-1");
    ASSERT_EQ(rule.by_monthday.size(), 1u);
    EXPECT_EQ(rule.by_monthday[0], -1);
}

TEST(RecurrenceParseRRule, ByMonth) {
    auto rule = parse_rrule("FREQ=YEARLY;BYMONTH=1,6,12");
    ASSERT_EQ(rule.by_month.size(), 3u);
    EXPECT_EQ(rule.by_month[0], 1);
    EXPECT_EQ(rule.by_month[1], 6);
    EXPECT_EQ(rule.by_month[2], 12);
}

TEST(RecurrenceParseRRule, ComplexRule) {
    auto rule = parse_rrule("FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE,FR;COUNT=10");
    EXPECT_EQ(rule.freq, Freq::WEEKLY);
    EXPECT_EQ(rule.interval, 2);
    ASSERT_EQ(rule.by_day.size(), 3u);
    ASSERT_TRUE(rule.count.has_value());
    EXPECT_EQ(*rule.count, 10);
}

// --- expand_rrule: DAILY ---

TEST(RecurrenceExpandDaily, BasicDaily) {
    auto rule = parse_rrule("FREQ=DAILY");
    auto results = expand_rrule(rule,
        "2026-03-10T09:00:00Z",
        "2026-03-10T00:00:00Z",
        "2026-03-15T00:00:00Z");

    EXPECT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0], "2026-03-10T09:00:00Z");
    EXPECT_EQ(results[1], "2026-03-11T09:00:00Z");
    EXPECT_EQ(results[4], "2026-03-14T09:00:00Z");
}

TEST(RecurrenceExpandDaily, EveryOtherDay) {
    auto rule = parse_rrule("FREQ=DAILY;INTERVAL=2");
    auto results = expand_rrule(rule,
        "2026-03-01T10:00:00Z",
        "2026-03-01T00:00:00Z",
        "2026-03-10T00:00:00Z");

    EXPECT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0], "2026-03-01T10:00:00Z");
    EXPECT_EQ(results[1], "2026-03-03T10:00:00Z");
    EXPECT_EQ(results[4], "2026-03-09T10:00:00Z");
}

TEST(RecurrenceExpandDaily, WithCount) {
    auto rule = parse_rrule("FREQ=DAILY;COUNT=3");
    auto results = expand_rrule(rule,
        "2026-03-01T09:00:00Z",
        "2026-03-01T00:00:00Z",
        "2026-12-31T23:59:59Z");

    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[2], "2026-03-03T09:00:00Z");
}

TEST(RecurrenceExpandDaily, WithUntil) {
    auto rule = parse_rrule("FREQ=DAILY;UNTIL=20260305T235959Z");
    auto results = expand_rrule(rule,
        "2026-03-01T09:00:00Z",
        "2026-03-01T00:00:00Z",
        "2026-12-31T23:59:59Z");

    EXPECT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0], "2026-03-01T09:00:00Z");
    EXPECT_EQ(results[4], "2026-03-05T09:00:00Z");
}

TEST(RecurrenceExpandDaily, RangeStartAfterDtstart) {
    // Event starts March 1, but we're querying March 5-10
    auto rule = parse_rrule("FREQ=DAILY");
    auto results = expand_rrule(rule,
        "2026-03-01T09:00:00Z",
        "2026-03-05T00:00:00Z",
        "2026-03-10T00:00:00Z");

    EXPECT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0], "2026-03-05T09:00:00Z");
    EXPECT_EQ(results[4], "2026-03-09T09:00:00Z");
}

// --- expand_rrule: WEEKLY ---

TEST(RecurrenceExpandWeekly, SimpleWeekly) {
    auto rule = parse_rrule("FREQ=WEEKLY");
    auto results = expand_rrule(rule,
        "2026-03-02T10:00:00Z",  // Monday
        "2026-03-02T00:00:00Z",
        "2026-04-01T00:00:00Z");

    // Should get 4 occurrences (Mar 2, 9, 16, 23, 30)
    EXPECT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0], "2026-03-02T10:00:00Z");
    EXPECT_EQ(results[1], "2026-03-09T10:00:00Z");
}

TEST(RecurrenceExpandWeekly, WithByDay) {
    auto rule = parse_rrule("FREQ=WEEKLY;BYDAY=MO,WE,FR");
    auto results = expand_rrule(rule,
        "2026-03-02T09:00:00Z",  // Monday March 2
        "2026-03-02T00:00:00Z",
        "2026-03-09T00:00:00Z");

    // Week of March 2: Mon(2), Wed(4), Fri(6)
    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], "2026-03-02T09:00:00Z");
    EXPECT_EQ(results[1], "2026-03-04T09:00:00Z");
    EXPECT_EQ(results[2], "2026-03-06T09:00:00Z");
}

TEST(RecurrenceExpandWeekly, BiweeklyWithByDay) {
    auto rule = parse_rrule("FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH");
    auto results = expand_rrule(rule,
        "2026-03-03T14:00:00Z",  // Tuesday March 3
        "2026-03-03T00:00:00Z",
        "2026-03-20T00:00:00Z");

    // Week 1 (Mar 2): Tue(3), Thu(5)
    // Week 2 (Mar 9): skipped (interval=2)
    // Week 3 (Mar 16): Tue(17), Thu(19)
    EXPECT_EQ(results.size(), 4u);
    EXPECT_EQ(results[0], "2026-03-03T14:00:00Z");
    EXPECT_EQ(results[1], "2026-03-05T14:00:00Z");
    EXPECT_EQ(results[2], "2026-03-17T14:00:00Z");
    EXPECT_EQ(results[3], "2026-03-19T14:00:00Z");
}

TEST(RecurrenceExpandWeekly, WithCount) {
    auto rule = parse_rrule("FREQ=WEEKLY;BYDAY=MO;COUNT=3");
    auto results = expand_rrule(rule,
        "2026-03-02T09:00:00Z",
        "2026-03-02T00:00:00Z",
        "2026-12-31T23:59:59Z");

    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], "2026-03-02T09:00:00Z");
    EXPECT_EQ(results[1], "2026-03-09T09:00:00Z");
    EXPECT_EQ(results[2], "2026-03-16T09:00:00Z");
}

// --- expand_rrule: MONTHLY ---

TEST(RecurrenceExpandMonthly, SimpleMonthly) {
    auto rule = parse_rrule("FREQ=MONTHLY");
    auto results = expand_rrule(rule,
        "2026-01-15T10:00:00Z",
        "2026-01-01T00:00:00Z",
        "2026-06-01T00:00:00Z");

    EXPECT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0], "2026-01-15T10:00:00Z");
    EXPECT_EQ(results[1], "2026-02-15T10:00:00Z");
    EXPECT_EQ(results[4], "2026-05-15T10:00:00Z");
}

TEST(RecurrenceExpandMonthly, EndOfMonthClamp) {
    // Start on Jan 31 — should clamp to Feb 28 in non-leap year
    auto rule = parse_rrule("FREQ=MONTHLY");
    auto results = expand_rrule(rule,
        "2026-01-31T10:00:00Z",
        "2026-01-01T00:00:00Z",
        "2026-04-01T00:00:00Z");

    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], "2026-01-31T10:00:00Z");
    EXPECT_EQ(results[1], "2026-02-28T10:00:00Z");
    EXPECT_EQ(results[2], "2026-03-31T10:00:00Z");
}

TEST(RecurrenceExpandMonthly, WithByMonthDay) {
    auto rule = parse_rrule("FREQ=MONTHLY;BYMONTHDAY=1");
    auto results = expand_rrule(rule,
        "2026-01-01T08:00:00Z",
        "2026-01-01T00:00:00Z",
        "2026-04-01T00:00:00Z");

    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], "2026-01-01T08:00:00Z");
    EXPECT_EQ(results[1], "2026-02-01T08:00:00Z");
    EXPECT_EQ(results[2], "2026-03-01T08:00:00Z");
}

TEST(RecurrenceExpandMonthly, LastDayOfMonth) {
    auto rule = parse_rrule("FREQ=MONTHLY;BYMONTHDAY=-1");
    auto results = expand_rrule(rule,
        "2026-01-31T12:00:00Z",
        "2026-01-01T00:00:00Z",
        "2026-04-01T00:00:00Z");

    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], "2026-01-31T12:00:00Z");
    EXPECT_EQ(results[1], "2026-02-28T12:00:00Z");
    EXPECT_EQ(results[2], "2026-03-31T12:00:00Z");
}

// --- expand_rrule: YEARLY ---

TEST(RecurrenceExpandYearly, SimpleYearly) {
    auto rule = parse_rrule("FREQ=YEARLY");
    auto results = expand_rrule(rule,
        "2025-06-15T09:00:00Z",
        "2025-01-01T00:00:00Z",
        "2028-01-01T00:00:00Z");

    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], "2025-06-15T09:00:00Z");
    EXPECT_EQ(results[1], "2026-06-15T09:00:00Z");
    EXPECT_EQ(results[2], "2027-06-15T09:00:00Z");
}

TEST(RecurrenceExpandYearly, LeapYearClamp) {
    // Feb 29 in leap year → Feb 28 in non-leap years
    auto rule = parse_rrule("FREQ=YEARLY");
    auto results = expand_rrule(rule,
        "2024-02-29T10:00:00Z",
        "2024-01-01T00:00:00Z",
        "2027-01-01T00:00:00Z");

    EXPECT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], "2024-02-29T10:00:00Z");
    EXPECT_EQ(results[1], "2025-02-28T10:00:00Z");
    EXPECT_EQ(results[2], "2026-02-28T10:00:00Z");
}

// --- Edge cases ---

TEST(RecurrenceExpandEdge, EmptyRangeReturnsEmpty) {
    auto rule = parse_rrule("FREQ=DAILY");
    auto results = expand_rrule(rule,
        "2026-03-15T10:00:00Z",
        "2026-03-10T00:00:00Z",
        "2026-03-10T00:00:00Z");

    EXPECT_EQ(results.size(), 0u);
}

TEST(RecurrenceExpandEdge, DtstartAfterRangeEnd) {
    auto rule = parse_rrule("FREQ=DAILY");
    auto results = expand_rrule(rule,
        "2026-06-01T10:00:00Z",
        "2026-03-01T00:00:00Z",
        "2026-03-31T23:59:59Z");

    EXPECT_EQ(results.size(), 0u);
}

TEST(RecurrenceExpandEdge, CountZeroReturnsEmpty) {
    auto rule = parse_rrule("FREQ=DAILY;COUNT=0");
    auto results = expand_rrule(rule,
        "2026-03-01T09:00:00Z",
        "2026-03-01T00:00:00Z",
        "2026-12-31T23:59:59Z");

    EXPECT_EQ(results.size(), 0u);
}

TEST(RecurrenceExpandEdge, CountOneReturnsSingle) {
    auto rule = parse_rrule("FREQ=WEEKLY;COUNT=1");
    auto results = expand_rrule(rule,
        "2026-03-02T09:00:00Z",
        "2026-03-01T00:00:00Z",
        "2026-12-31T23:59:59Z");

    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], "2026-03-02T09:00:00Z");
}

TEST(RecurrenceExpandEdge, UntilBeforeDtstart) {
    auto rule = parse_rrule("FREQ=DAILY;UNTIL=20260228T235959Z");
    auto results = expand_rrule(rule,
        "2026-03-01T09:00:00Z",
        "2026-03-01T00:00:00Z",
        "2026-12-31T23:59:59Z");

    EXPECT_EQ(results.size(), 0u);
}

TEST(RecurrenceExpandEdge, PreservesTime) {
    auto rule = parse_rrule("FREQ=DAILY;COUNT=2");
    auto results = expand_rrule(rule,
        "2026-03-01T14:30:45Z",
        "2026-03-01T00:00:00Z",
        "2026-03-10T00:00:00Z");

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], "2026-03-01T14:30:45Z");
    EXPECT_EQ(results[1], "2026-03-02T14:30:45Z");
}
