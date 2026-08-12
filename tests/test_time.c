#include "harness.h"

#include "data/timeutil.h"

void testTime(void)
{
	TEST_CASE("leap years");
	CHECK(fcIsLeapYear(2024));
	CHECK(fcIsLeapYear(2000));
	CHECK(!fcIsLeapYear(1900));
	CHECK(!fcIsLeapYear(2023));
	CHECK_EQ_INT(fcDaysInMonth(2024, 2), 29);
	CHECK_EQ_INT(fcDaysInMonth(2023, 2), 28);
	CHECK_EQ_INT(fcDaysInMonth(2023, 9), 30);

	TEST_CASE("epoch to civil");
	{
		FcDateTime d;
		fcCivilFromEpoch(0, &d);
		CHECK_EQ_INT(d.year, 1970);
		CHECK_EQ_INT(d.month, 1);
		CHECK_EQ_INT(d.day, 1);
		CHECK_EQ_INT(d.hour, 0);

		fcCivilFromEpoch(1786458605, &d);
		CHECK_EQ_INT(d.year, 2026);
		CHECK_EQ_INT(d.month, 8);
		CHECK_EQ_INT(d.day, 11);
		CHECK_EQ_INT(d.hour, 14);
		CHECK_EQ_INT(d.minute, 30);
		CHECK_EQ_INT(d.second, 5);
	}

	TEST_CASE("civil to epoch round trip");
	{
		const time_t stamps[] = { 0, 1, 86399, 86400, 1786458605, 951782400 };
		for (size_t i = 0; i < sizeof stamps / sizeof stamps[0]; i++) {
			FcDateTime d;
			fcCivilFromEpoch(stamps[i], &d);
			CHECK_EQ_INT(fcEpochFromCivil(&d), stamps[i]);
		}
	}

	TEST_CASE("before the epoch");
	{
		FcDateTime d;
		fcCivilFromEpoch(-1, &d);
		CHECK_EQ_INT(d.year, 1969);
		CHECK_EQ_INT(d.month, 12);
		CHECK_EQ_INT(d.day, 31);
		CHECK_EQ_INT(d.hour, 23);
		CHECK_EQ_INT(d.minute, 59);
		CHECK_EQ_INT(d.second, 59);
	}

	TEST_CASE("weekday");
	CHECK_EQ_INT(fcWeekday(1970, 1, 1), 4);
	CHECK_EQ_INT(fcWeekday(2026, 8, 11), 2);
	CHECK_EQ_INT(fcWeekday(2000, 2, 29), 2);
	CHECK_EQ_INT(fcWeekday(1969, 7, 20), 0);
	CHECK_EQ_STR(fcWeekdayName(fcWeekday(2026, 8, 11), true), "Tue");
	CHECK_EQ_STR(fcMonthName(8, false), "August");
	CHECK_EQ_STR(fcMonthName(13, true), "?");

	TEST_CASE("local day floors");
	{
		CHECK_EQ_INT(fcLocalDay(1, -7 * 3600), -1);
		CHECK_EQ_INT(fcLocalDay(86400, 0), 1);
		CHECK_EQ_INT(fcLocalDay(86399, 0), 0);
	}
}
