#include "data/timeutil.h"

static const char *kWeekdaysShort[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *kWeekdaysLong[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *kMonthsShort[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *kMonthsLong[] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
};

bool fcIsLeapYear(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int fcDaysInMonth(int year, int month)
{
	static const int kDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (month < 1 || month > 12)
		return 0;
	if (month == 2 && fcIsLeapYear(year))
		return 29;
	return kDays[month - 1];
}

int64_t fcDaysFromCivil(int year, int month, int day)
{
	int64_t y = year;
	y -= month <= 2;

	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const int64_t yoe = y - era * 400;
	const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return era * 146097 + doe - 719468;
}

static void civilFromDays(int64_t z, int *year, int *month, int *day)
{
	z += 719468;

	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const int64_t doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t y   = yoe + era * 400;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp  = (5 * doy + 2) / 153;
	const int64_t d   = doy - (153 * mp + 2) / 5 + 1;
	const int64_t m   = mp + (mp < 10 ? 3 : -9);

	*year  = (int)(y + (m <= 2));
	*month = (int)m;
	*day   = (int)d;
}

void fcCivilFromEpoch(time_t t, FcDateTime *out)
{
	const int64_t secs = (int64_t)t;

	int64_t days = secs / 86400;
	int64_t rem  = secs % 86400;
	if (rem < 0) {
		rem += 86400;
		days -= 1;
	}

	int y = 0, m = 0, d = 0;
	civilFromDays(days, &y, &m, &d);

	out->year   = (int16_t)y;
	out->month  = (int8_t)m;
	out->day    = (int8_t)d;
	out->hour   = (int8_t)(rem / 3600);
	out->minute = (int8_t)((rem / 60) % 60);
	out->second = (int8_t)(rem % 60);
}

time_t fcEpochFromCivil(const FcDateTime *dt)
{
	const int64_t days = fcDaysFromCivil(dt->year, dt->month, dt->day);
	return (time_t)(days * 86400 + dt->hour * 3600 + dt->minute * 60 + dt->second);
}

int fcWeekday(int year, int month, int day)
{
	const int64_t days = fcDaysFromCivil(year, month, day);

	int wd = (int)((days + 4) % 7);
	if (wd < 0)
		wd += 7;
	return wd;
}

int64_t fcLocalDay(time_t utc, int offsetSec)
{
	const int64_t shifted = (int64_t)utc + offsetSec;
	int64_t day = shifted / 86400;
	if (shifted < 0 && shifted % 86400 != 0)
		day--;
	return day;
}

const char *fcWeekdayName(int weekday, bool shortForm)
{
	if (weekday < 0 || weekday > 6)
		return "?";
	return shortForm ? kWeekdaysShort[weekday] : kWeekdaysLong[weekday];
}

const char *fcMonthName(int month, bool shortForm)
{
	if (month < 1 || month > 12)
		return "?";
	return shortForm ? kMonthsShort[month - 1] : kMonthsLong[month - 1];
}
