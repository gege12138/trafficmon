#ifndef TRAFFICMON_PERIOD_H
#define TRAFFICMON_PERIOD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t year;
    uint8_t month;
    uint8_t day;
} tm_date;

typedef enum {
    TM_PERIOD_MONTHLY,
    TM_PERIOD_FIXED_DAYS,
} tm_period_kind;

typedef struct {
    tm_period_kind kind;
    int8_t monthly_day;
    tm_date anchor;
    uint32_t interval_days;
} tm_period_rule;

typedef struct {
    tm_date start;
    tm_date end;
} tm_period;

bool tm_date_is_valid(tm_date date);
bool tm_date_parse(const char *text, tm_date *date);
bool tm_date_format(tm_date date, char output[11]);
uint32_t tm_date_encode(tm_date date);
bool tm_date_decode(uint32_t encoded, tm_date *date);
bool tm_period_rule_parse(const char *text, tm_period_rule *rule);
bool tm_period_for_date(const tm_period_rule *rule, tm_date current, tm_period *period);

#endif
