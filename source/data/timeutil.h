#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
	int16_t year;
	int8_t  month;
	int8_t  day;
	int8_t  hour;
	int8_t  minute;
	int8_t  second;
} FcDateTime;

bool fcIsLeapYear(int year);
int  fcDaysInMonth(int year, int month);

int64_t fcDaysFromCivil(int year, int month, int day);

void fcCivilFromEpoch(time_t t, FcDateTime *out);

time_t fcEpochFromCivil(const FcDateTime *dt);

int fcWeekday(int year, int month, int day);

int64_t fcLocalDay(time_t utc, int offsetSec);

const char *fcWeekdayName(int weekday, bool shortForm);
const char *fcMonthName(int month, bool shortForm);
