#include "harness.h"

#include "data/gfonts.h"

#include <stdlib.h>
#include <string.h>

void testGFonts(void)
{
	TEST_CASE("gfonts: the baked table parses");
	{
		const char *tsv =
			"Sans Serif\tABeeZee\n"
			"Serif\tAbhaya Libre\n"
			"Display\tAbril Fatface\n"
			"Monospace\tRoboto Mono\n";

		FcGFontList list;
		CHECK(fcGFontsParse(tsv, strlen(tsv), &list));
		CHECK_EQ_INT(list.count, 4);
		CHECK_EQ_STR(list.items[0].family, "ABeeZee");
		CHECK_EQ_STR(list.items[0].category, "Sans Serif");
		CHECK_EQ_STR(list.items[1].family, "Abhaya Libre");
		CHECK_EQ_STR(list.items[3].family, "Roboto Mono");
		fcGFontsFree(&list);
	}

	TEST_CASE("gfonts: a table without a trailing newline still parses");
	{
		const char *tsv = "Serif\tLora";
		FcGFontList list;
		CHECK(fcGFontsParse(tsv, strlen(tsv), &list));
		CHECK_EQ_INT(list.count, 1);
		CHECK_EQ_STR(list.items[0].family, "Lora");
		fcGFontsFree(&list);
	}

	TEST_CASE("gfonts: malformed rows are skipped, not fatal");
	{
		const char *tsv =
			"no tab here\n"
			"Serif\t\n"
			"\n"
			"Sans Serif\tInter\n";

		FcGFontList list;
		CHECK(fcGFontsParse(tsv, strlen(tsv), &list));
		CHECK_EQ_INT(list.count, 1);
		CHECK_EQ_STR(list.items[0].family, "Inter");
		fcGFontsFree(&list);
	}

	TEST_CASE("gfonts: an empty table reports why");
	{
		FcGFontList list;
		CHECK(!fcGFontsParse("", 0, &list));
		CHECK_EQ_STR(list.error, "empty font list");
		CHECK(!fcGFontsParse("no tabs at all\n", 15, &list));
		CHECK_EQ_STR(list.error, "font list has no usable rows");
	}

	TEST_CASE("gfonts: search is a case-insensitive substring match");
	{
		const char *tsv =
			"Sans Serif\tRoboto\n"
			"Serif\tRoboto Slab\n"
			"Monospace\tRoboto Mono\n"
			"Sans Serif\tOpen Sans\n"
			"Display\tLobster\n";

		FcGFontList list;
		CHECK(fcGFontsParse(tsv, strlen(tsv), &list));

		int hits[8];

		CHECK_EQ_INT(fcGFontsSearch(&list, "roboto", hits, 8), 3);
		CHECK_EQ_INT(fcGFontsSearch(&list, "ROBOTO", hits, 8), 3);

		CHECK_EQ_INT(fcGFontsSearch(&list, "slab", hits, 8), 1);
		CHECK_EQ_INT(hits[0], 1);

		CHECK_EQ_INT(fcGFontsSearch(&list, "", hits, 8), 5);
		CHECK_EQ_INT(fcGFontsSearch(&list, NULL, hits, 8), 5);

		CHECK_EQ_INT(fcGFontsSearch(&list, "", hits, 2), 2);

		CHECK_EQ_INT(fcGFontsSearch(&list, "helvetica", hits, 8), 0);

		fcGFontsFree(&list);
	}

	TEST_CASE("gfonts: CSS URLs escape what they must");
	{
		char url[256];

		CHECK(fcGFontsCssUrl("Roboto", 400, url, sizeof url));
		CHECK_EQ_STR(url, "https://fonts.googleapis.com/css2?family=Roboto");

		CHECK(fcGFontsCssUrl("Roboto Slab", 400, url, sizeof url));
		CHECK_EQ_STR(url, "https://fonts.googleapis.com/css2?family=Roboto+Slab");

		CHECK(fcGFontsCssUrl("Inter", 700, url, sizeof url));
		CHECK_EQ_STR(url, "https://fonts.googleapis.com/css2?family=Inter:wght@700");

		CHECK(fcGFontsCssUrl("Zilla Slab", 0, url, sizeof url));
		CHECK_EQ_STR(url, "https://fonts.googleapis.com/css2?family=Zilla+Slab");

		char tiny[20];
		CHECK(!fcGFontsCssUrl("Roboto", 400, tiny, sizeof tiny));
		CHECK(!fcGFontsCssUrl("", 400, url, sizeof url));
		CHECK(!fcGFontsCssUrl(NULL, 400, url, sizeof url));
	}

	TEST_CASE("gfonts: the TTF URL comes out of the stylesheet");
	{
		const char *css =
			"@font-face {\n"
			"  font-family: 'Roboto Slab';\n"
			"  font-style: normal;\n"
			"  font-weight: 400;\n"
			"  src: url(https://fonts.gstatic.com/s/robotoslab/v36/Bng.ttf)"
			" format('truetype');\n"
			"}\n";

		char url[256];
		CHECK(fcGFontsTtfUrlFromCss(css, url, sizeof url));
		CHECK_EQ_STR(url, "https://fonts.gstatic.com/s/robotoslab/v36/Bng.ttf");
	}

	TEST_CASE("gfonts: family names come out of specimen links");
	{
		char f[64];

		CHECK(fcGFontsFamilyFromUrl("https://fonts.google.com/specimen/Roboto",
		                              f, sizeof f));
		CHECK_EQ_STR(f, "Roboto");

		CHECK(fcGFontsFamilyFromUrl(
		        "https://fonts.google.com/specimen/Roboto+Slab", f, sizeof f));
		CHECK_EQ_STR(f, "Roboto Slab");

		CHECK(fcGFontsFamilyFromUrl(
		        "https://fonts.google.com/specimen/Noto%20Sans", f, sizeof f));
		CHECK_EQ_STR(f, "Noto Sans");

		CHECK(fcGFontsFamilyFromUrl(
		        "https://fonts.google.com/specimen/Inter?query=inter",
		        f, sizeof f));
		CHECK_EQ_STR(f, "Inter");
		CHECK(fcGFontsFamilyFromUrl(
		        "https://fonts.google.com/specimen/Lora/about", f, sizeof f));
		CHECK_EQ_STR(f, "Lora");

		CHECK(fcGFontsFamilyFromUrl("http://www.fonts.google.com/specimen/Abel",
		                              f, sizeof f));
		CHECK_EQ_STR(f, "Abel");

		CHECK(!fcGFontsFamilyFromUrl("https://fonts.google.com/", f, sizeof f));
		CHECK(!fcGFontsFamilyFromUrl("https://example.org/specimen/Roboto",
		                               f, sizeof f));
		CHECK(!fcGFontsFamilyFromUrl("https://fonts.google.com/specimen/",
		                               f, sizeof f));
		CHECK(!fcGFontsFamilyFromUrl("fonts.google.com/specimen/Roboto",
		                               f, sizeof f));
		CHECK(!fcGFontsFamilyFromUrl(NULL, f, sizeof f));

		CHECK(!fcGFontsFamilyFromUrl("https://fonts.google.com/specimen/A%ZZ",
		                               f, sizeof f));
	}

	TEST_CASE("gfonts: a WOFF2-only stylesheet is refused");
	{
		const char *css =
			"@font-face { src: url(https://fonts.gstatic.com/s/x/y.woff2)"
			" format('woff2'); }";

		char url[256];
		CHECK(!fcGFontsTtfUrlFromCss(css, url, sizeof url));
		CHECK_EQ_STR(url, "");
	}

	TEST_CASE("gfonts: an EOT stylesheet is refused");
	{
		const char *css =
			"@font-face { src: url(https://fonts.gstatic.com/l/font?kit=5DCX"
			"&skey=c13f&v=v2) format('embedded-opentype'); }";

		char url[256];
		CHECK(!fcGFontsTtfUrlFromCss(css, url, sizeof url));
	}

	TEST_CASE("gfonts: the format token decides, not the extension");
	{
		char url[256];

		const char *noExt =
			"src: url(https://fonts.gstatic.com/l/font?kit=abc&v=v2)"
			" format('truetype');";
		CHECK(fcGFontsTtfUrlFromCss(noExt, url, sizeof url));
		CHECK_EQ_STR(url, "https://fonts.gstatic.com/l/font?kit=abc&v=v2");

		const char *mixed =
			"src: url(https://x/a.woff2) format('woff2'),"
			"     url(https://x/b.ttf) format('truetype');";
		CHECK(fcGFontsTtfUrlFromCss(mixed, url, sizeof url));
		CHECK_EQ_STR(url, "https://x/b.ttf");
	}

	TEST_CASE("gfonts: quoted and OTF sources are handled");
	{
		char url[256];

		const char *quoted = "src: url('https://x/y.ttf') format('truetype');";
		CHECK(fcGFontsTtfUrlFromCss(quoted, url, sizeof url));
		CHECK_EQ_STR(url, "https://x/y.ttf");

		const char *otf = "src: url(https://x/z.otf);";
		CHECK(fcGFontsTtfUrlFromCss(otf, url, sizeof url));
		CHECK_EQ_STR(url, "https://x/z.otf");

		char tiny[15];
		CHECK(!fcGFontsTtfUrlFromCss(quoted, tiny, sizeof tiny));

		CHECK(!fcGFontsTtfUrlFromCss("no urls here", url, sizeof url));
		CHECK(!fcGFontsTtfUrlFromCss(NULL, url, sizeof url));
	}
}
