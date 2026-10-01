#include "period.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool tm_is_leap_year(int32_t year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static uint8_t tm_days_in_month(int32_t year, uint8_t month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12) {
        return 0;
    }
    if (month == 2 && tm_is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

bool tm_date_is_valid(tm_date date)
{
    uint8_t days = tm_days_in_month(date.year, date.month);
    return date.year >= 1 && days != 0 && date.day >= 1 && date.day <= days;
}

bool tm_date_parse(const char *text, tm_date *date)
{
    int year;
    unsigned int month;
    unsigned int day;
    char extra;
    tm_date parsed;
    if (text == NULL || date == NULL ||
        sscanf(text, "%d-%u-%u%c", &year, &month, &day, &extra) != 3 || year < 1 ||
        month > UINT8_MAX || day > UINT8_MAX) {
        return false;
    }
    parsed = (tm_date){(int32_t)year, (uint8_t)month, (uint8_t)day};
    if (!tm_date_is_valid(parsed)) {
        return false;
    }
    *date = parsed;
    return true;
}

bool tm_date_format(tm_date date, char output[11])
{
    if (output == NULL || !tm_date_is_valid(date) || date.year > 9999) {
        return false;
    }
    return snprintf(output, 11, "%04d-%02u-%02u", date.year, date.month, date.day) == 10;
}

uint32_t tm_date_encode(tm_date date)
{
    if (!tm_date_is_valid(date) || date.year > 9999) {
        return 0;
    }
    return (uint32_t)date.year * 10000U + (uint32_t)date.month * 100U + date.day;
}

bool tm_date_decode(uint32_t encoded, tm_date *date)
{
    tm_date decoded;
    if (date == NULL) {
        return false;
    }
    decoded.year = (int32_t)(encoded / 10000U);
    decoded.month = (uint8_t)((encoded / 100U) % 100U);
    decoded.day = (uint8_t)(encoded % 100U);
    if (!tm_date_is_valid(decoded)) {
        return false;
    }
    *date = decoded;
    return true;
}

static int64_t tm_days_from_civil(tm_date date)
{
    int64_t year = date.year;
    unsigned int month = date.month;
    unsigned int day = date.day;
    int64_t era;
    unsigned int year_of_era;
    unsigned int day_of_year;
    unsigned int day_of_era;

    year -= month <= 2;
    era = year >= 0 ? year / 400 : (year - 399) / 400;
    year_of_era = (unsigned int)(year - era * 400);
    day_of_year = (153U * (month > 2 ? month - 3 : month + 9) + 2U) / 5U + day - 1U;
    day_of_era = year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return era * 146097 + (int64_t)day_of_era;
}

static tm_date tm_civil_from_days(int64_t days)
{
    int64_t era = days >= 0 ? days / 146097 : (days - 146096) / 146097;
    unsigned int day_of_era = (unsigned int)(days - era * 146097);
    unsigned int year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U - day_of_era / 146096U) /
        365U;
    int64_t year = (int64_t)year_of_era + era * 400;
    unsigned int day_of_year =
        day_of_era - (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
    unsigned int month_prime = (5U * day_of_year + 2U) / 153U;
    unsigned int day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    unsigned int month = month_prime < 10 ? month_prime + 3 : month_prime - 9;
    tm_date date;

    year += month <= 2;
    date.year = (int32_t)year;
    date.month = (uint8_t)month;
    date.day = (uint8_t)day;
    return date;
}

static tm_date tm_monthly_boundary(int32_t year, uint8_t month, int8_t configured_day)
{
    uint8_t days = tm_days_in_month(year, month);
    int day = configured_day > 0 ? configured_day : (int)days + configured_day + 1;
    tm_date result;
    if (day < 1) {
        day = 1;
    }
    if (day > days) {
        day = days;
    }
    result.year = year;
    result.month = month;
    result.day = (uint8_t)day;
    return result;
}

static void tm_adjacent_month(int32_t year, uint8_t month, int direction, int32_t *next_year,
                              uint8_t *next_month)
{
    int value = (int)month + direction;
    *next_year = year;
    if (value == 0) {
        *next_year = year - 1;
        value = 12;
    } else if (value == 13) {
        *next_year = year + 1;
        value = 1;
    }
    *next_month = (uint8_t)value;
}

bool tm_period_rule_parse(const char *text, tm_period_rule *rule)
{
    char *end = NULL;
    long monthly_day;
    int year;
    unsigned int month;
    unsigned int day;
    unsigned int interval;
    char extra;

    if (text == NULL || rule == NULL) {
        return false;
    }
    errno = 0;
    monthly_day = strtol(text, &end, 10);
    if (errno == 0 && end != text && *end == '\0' && monthly_day != 0 && monthly_day >= -31 &&
        monthly_day <= 31) {
        memset(rule, 0, sizeof(*rule));
        rule->kind = TM_PERIOD_MONTHLY;
        rule->monthly_day = (int8_t)monthly_day;
        return true;
    }
    if (sscanf(text, "%d-%u-%u/%u%c", &year, &month, &day, &interval, &extra) != 4 ||
        year < 1 || year > INT32_MAX || month > UINT8_MAX || day > UINT8_MAX || interval == 0) {
        return false;
    }
    memset(rule, 0, sizeof(*rule));
    rule->kind = TM_PERIOD_FIXED_DAYS;
    rule->anchor = (tm_date){(int32_t)year, (uint8_t)month, (uint8_t)day};
    rule->interval_days = interval;
    return tm_date_is_valid(rule->anchor);
}

bool tm_period_for_date(const tm_period_rule *rule, tm_date current, tm_period *period)
{
    if (rule == NULL || period == NULL || !tm_date_is_valid(current)) {
        return false;
    }
    if (rule->kind == TM_PERIOD_MONTHLY) {
        int32_t start_year = current.year;
        uint8_t start_month = current.month;
        int32_t end_year;
        uint8_t end_month;
        tm_date candidate;
        if (rule->monthly_day == 0 || rule->monthly_day < -31 || rule->monthly_day > 31) {
            return false;
        }
        candidate = tm_monthly_boundary(start_year, start_month, rule->monthly_day);
        if (tm_days_from_civil(current) < tm_days_from_civil(candidate)) {
            tm_adjacent_month(start_year, start_month, -1, &start_year, &start_month);
        }
        tm_adjacent_month(start_year, start_month, 1, &end_year, &end_month);
        period->start = tm_monthly_boundary(start_year, start_month, rule->monthly_day);
        period->end = tm_monthly_boundary(end_year, end_month, rule->monthly_day);
        return true;
    }
    if (rule->kind == TM_PERIOD_FIXED_DAYS && rule->interval_days != 0 &&
        tm_date_is_valid(rule->anchor)) {
        int64_t anchor_days = tm_days_from_civil(rule->anchor);
        int64_t difference = tm_days_from_civil(current) - anchor_days;
        int64_t interval = rule->interval_days;
        int64_t quotient = difference / interval;
        int64_t start_days;
        if (difference < 0 && difference % interval != 0) {
            quotient--;
        }
        start_days = anchor_days + quotient * interval;
        period->start = tm_civil_from_days(start_days);
        period->end = tm_civil_from_days(start_days + interval);
        return true;
    }
    return false;
}
