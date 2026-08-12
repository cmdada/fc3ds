#include "harness.h"

#include <stdio.h>

int g_tests;
int g_failures;
const char *g_currentCase = "(none)";

int main(void)
{
	printf("\n== fc3ds data layer ==\n\n");

	testTime();
	testSha256();
	testLz11();
	testBcfnt();
	testCia();
	testCatalog();
	testBcfntBuild();
	testGFonts();
	testCiaBuild();
	testBcfntEdit();

	printf("\n%d checks, %d failures\n\n", g_tests, g_failures);
	return g_failures == 0 ? 0 : 1;
}
