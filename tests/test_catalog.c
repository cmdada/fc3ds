#include "harness.h"

#include "data/catalog.h"
#include "data/fontslot.h"

#include <string.h>

static bool parse(const char *json, FcCatalog *out)
{
	return fcCatalogParse(json, strlen(json), out);
}

void testCatalog(void)
{
	FcCatalog cat;

	TEST_CASE("catalog: a full entry parses");
	{
		const char *json =
			"{\"version\":1,\"name\":\"Kitsune fonts\",\"fonts\":["
			"{\"id\":\"atkinson\",\"name\":\"Atkinson Hyperlegible\","
			"\"author\":\"Braille Institute\",\"license\":\"OFL-1.1\","
			"\"notes\":\"Drawn to be told apart.\",\"slot\":\"std\","
			"\"cia\":{\"url\":\"https://example.org/a.cia\",\"size\":1442816,"
			"\"sha256\":\"AABBCC\"},"
			"\"preview\":{\"url\":\"https://example.org/a.bcfnt.lz\","
			"\"size\":402118}}]}";

		CHECK(parse(json, &cat));
		CHECK_EQ_INT(cat.count, 1);
		CHECK_EQ_STR(cat.name, "Kitsune fonts");

		const FcFontEntry *e = &cat.items[0];
		CHECK_EQ_STR(e->id, "atkinson");
		CHECK_EQ_STR(e->name, "Atkinson Hyperlegible");
		CHECK_EQ_STR(e->author, "Braille Institute");
		CHECK_EQ_STR(e->license, "OFL-1.1");
		CHECK_EQ_INT(e->slot, FC_SLOT_STD);
		CHECK_EQ_STR(e->cia.url, "https://example.org/a.cia");
		CHECK_EQ_INT(e->cia.size, 1442816);
		CHECK_EQ_INT(e->preview.size, 402118);

		CHECK_EQ_STR(e->cia.sha256, "aabbcc");
		CHECK_EQ_STR(e->preview.sha256, "");

		CHECK(fcFontEntryInstallable(e));
		CHECK(fcFontEntryPreviewable(e));
	}

	TEST_CASE("catalog: slots other than std are honoured");
	{
		const char *json =
			"{\"fonts\":[{\"name\":\"Hangul\",\"slot\":\"kor\","
			"\"cia\":{\"url\":\"https://example.org/k.cia\"}}]}";

		CHECK(parse(json, &cat));
		CHECK_EQ_INT(cat.items[0].slot, FC_SLOT_KOR);
	}

	TEST_CASE("catalog: unusable entries are dropped, not fatal");
	{
		const char *json =
			"{\"fonts\":["
			"{\"name\":\"No downloads\"},"
			"{\"cia\":{\"url\":\"https://example.org/x.cia\"}},"
			"{\"name\":\"Unknown slot\",\"slot\":\"vulcan\","
			"\"cia\":{\"url\":\"https://example.org/v.cia\"}},"
			"\"not an object\","
			"{\"name\":\"Good\",\"cia\":{\"url\":\"https://example.org/g.cia\"}}"
			"]}";

		CHECK(parse(json, &cat));
		CHECK_EQ_INT(cat.count, 1);
		CHECK_EQ_STR(cat.items[0].name, "Good");

		CHECK_EQ_STR(cat.items[0].id, "Good");
	}

	TEST_CASE("catalog: only http(s) is offered as a download");
	{
		const char *json =
			"{\"fonts\":[{\"name\":\"Local\","
			"\"cia\":{\"url\":\"file:///etc/passwd\"},"
			"\"preview\":{\"url\":\"sdmc:/font.bcfnt\"}}]}";

		CHECK(parse(json, &cat));
		CHECK_EQ_INT(cat.count, 1);
		CHECK(!fcFontEntryInstallable(&cat.items[0]));
		CHECK(!fcFontEntryPreviewable(&cat.items[0]));
	}

	TEST_CASE("catalog: bad documents report why");
	{
		CHECK(!parse("", &cat));
		CHECK_EQ_STR(cat.error, "empty catalog");

		CHECK(!parse("{\"fonts\":", &cat));
		CHECK_EQ_STR(cat.error, "catalog is not valid JSON");

		CHECK(!parse("[1,2,3]", &cat));
		CHECK_EQ_STR(cat.error, "catalog is not an object");

		CHECK(!parse("{\"version\":1}", &cat));
		CHECK_EQ_STR(cat.error, "catalog has no font list");

		CHECK(!parse("{\"fonts\":[]}", &cat));
		CHECK_EQ_STR(cat.error, "catalog lists no usable fonts");
	}

	TEST_CASE("catalog: the entry cap is enforced");
	{
		char json[16384];
		size_t n = (size_t)snprintf(json, sizeof json, "{\"fonts\":[");
		for (int i = 0; i < FC_CATALOG_MAX + 20; i++)
			n += (size_t)snprintf(json + n, sizeof json - n,
			                      "%s{\"name\":\"F%d\","
			                      "\"cia\":{\"url\":\"https://e.org/%d.cia\"}}",
			                      i ? "," : "", i, i);
		snprintf(json + n, sizeof json - n, "]}");

		CHECK(parse(json, &cat));
		CHECK_EQ_INT(cat.count, FC_CATALOG_MAX);
	}

	TEST_CASE("catalog: a specimen link becomes a Google entry");
	{
		FcFontEntry e;

		CHECK(fcFontEntryFromUrl(&e,
		        "https://fonts.google.com/specimen/Roboto+Slab", FC_SLOT_STD));
		CHECK_EQ_STR(e.googleFamily, "Roboto Slab");
		CHECK_EQ_STR(e.name, "Roboto Slab");
		CHECK(fcFontEntryPreviewable(&e));
		CHECK(fcFontEntryInstallable(&e));
	}

	TEST_CASE("catalog: a face address is merged, a prebuilt font is not");
	{
		FcFontEntry e;

		CHECK(fcFontEntryFromUrl(&e, "https://example.org/MyFace.ttf",
		                           FC_SLOT_STD));
		CHECK_EQ_STR(e.ttf.url, "https://example.org/MyFace.ttf");
		CHECK(fcFontEntryInstallable(&e));
		CHECK(fcFontEntryPreviewable(&e));

		CHECK(fcFontEntryFromUrl(&e, "https://example.org/x.bcfnt.lz",
		                           FC_SLOT_STD));
		CHECK_EQ_STR(e.preview.url, "https://example.org/x.bcfnt.lz");
		CHECK(!fcFontEntryInstallable(&e));

		CHECK(fcFontEntryFromUrl(&e, "https://example.org/x.bcfnt", FC_SLOT_STD));
		CHECK_EQ_STR(e.preview.url, "https://example.org/x.bcfnt");

		CHECK(fcFontEntryFromUrl(&e, "https://example.org/l/font?kit=abc",
		                           FC_SLOT_STD));
		CHECK(e.ttf.url[0] != '\0');
	}

	TEST_CASE("catalog: a bare URL becomes an entry");
	{
		FcFontEntry e;

		CHECK(fcFontEntryFromUrl(&e, "https://example.org/fonts/Comic.cia",
		                           FC_SLOT_STD));
		CHECK_EQ_STR(e.name, "Comic.cia");
		CHECK_EQ_STR(e.cia.url, "https://example.org/fonts/Comic.cia");
		CHECK(fcFontEntryInstallable(&e));
		CHECK(!fcFontEntryPreviewable(&e));

		CHECK(fcFontEntryFromUrl(&e, "https://example.org/x/Sans.bcfnt.lz",
		                           FC_SLOT_KOR));
		CHECK_EQ_STR(e.name, "Sans.bcfnt.lz");
		CHECK_EQ_INT(e.slot, FC_SLOT_KOR);
		CHECK(fcFontEntryPreviewable(&e));
		CHECK(!fcFontEntryInstallable(&e));

		CHECK(!fcFontEntryFromUrl(&e, "sdmc:/font.cia", FC_SLOT_STD));
		CHECK(!fcFontEntryFromUrl(&e, NULL, FC_SLOT_STD));

		char huge[400];
		memset(huge, 'a', sizeof huge);
		memcpy(huge, "https://example.org/", 20);
		huge[sizeof huge - 1] = '\0';
		CHECK(!fcFontEntryFromUrl(&e, huge, FC_SLOT_STD));
	}
}
