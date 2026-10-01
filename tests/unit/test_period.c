#include "period.h"
#include "test.h"

static void expect_date(tm_date actual, int32_t year, uint8_t month, uint8_t day)
{
    TM_EXPECT_EQ_U64((uint64_t)year, (uint64_t)actual.year);
    TM_EXPECT_EQ_U64(month, actual.month);
    TM_EXPECT_EQ_U64(day, actual.day);
}

void test_period(void)
{
    tm_period_rule rule;
    tm_period period;

    TM_EXPECT_TRUE(tm_period_rule_parse("15", &rule));
    TM_EXPECT_TRUE(tm_period_for_date(&rule, (tm_date){2026, 9, 14}, &period));
    expect_date(period.start, 2026, 8, 15);
    expect_date(period.end, 2026, 9, 15);

    TM_EXPECT_TRUE(tm_period_rule_parse("-2", &rule));
    TM_EXPECT_TRUE(tm_period_for_date(&rule, (tm_date){2024, 2, 28}, &period));
    expect_date(period.start, 2024, 2, 28);
    expect_date(period.end, 2024, 3, 30);

    TM_EXPECT_TRUE(tm_period_rule_parse("2026-01-01/30", &rule));
    TM_EXPECT_TRUE(tm_period_for_date(&rule, (tm_date){2025, 12, 31}, &period));
    expect_date(period.start, 2025, 12, 2);
    expect_date(period.end, 2026, 1, 1);
    TM_EXPECT_TRUE(tm_period_for_date(&rule, (tm_date){2026, 1, 31}, &period));
    expect_date(period.start, 2026, 1, 31);
    expect_date(period.end, 2026, 3, 2);

    TM_EXPECT_TRUE(!tm_period_rule_parse("0", &rule));
    TM_EXPECT_TRUE(!tm_period_rule_parse("2026-02-30/7", &rule));
}
