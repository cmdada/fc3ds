#pragma once

#include <stdio.h>
#include <string.h>

extern int g_tests;
extern int g_failures;
extern const char *g_currentCase;

#define TEST_CASE(name) \
	do { g_currentCase = (name); } while (0)

#define CHECK(cond) \
	do { \
		g_tests++; \
		if (!(cond)) { \
			g_failures++; \
			printf("  FAIL  %s\n        %s:%d  %s\n", \
			       g_currentCase, __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ_INT(got, want) \
	do { \
		g_tests++; \
		long long _g = (long long)(got), _w = (long long)(want); \
		if (_g != _w) { \
			g_failures++; \
			printf("  FAIL  %s\n        %s:%d  %s\n" \
			       "        got %lld, want %lld\n", \
			       g_currentCase, __FILE__, __LINE__, #got, _g, _w); \
		} \
	} while (0)

#define CHECK_EQ_STR(got, want) \
	do { \
		g_tests++; \
		const char *_g = (got), *_w = (want); \
		if (!_g || !_w || strcmp(_g, _w) != 0) { \
			g_failures++; \
			printf("  FAIL  %s\n        %s:%d  %s\n" \
			       "        got \"%s\", want \"%s\"\n", \
			       g_currentCase, __FILE__, __LINE__, #got, \
			       _g ? _g : "(null)", _w ? _w : "(null)"); \
		} \
	} while (0)

void testTime(void);
void testSha256(void);
void testLz11(void);
void testBcfnt(void);
void testCia(void);
void testCatalog(void);
void testBcfntBuild(void);
void testGFonts(void);
void testCiaBuild(void);
void testBcfntEdit(void);
