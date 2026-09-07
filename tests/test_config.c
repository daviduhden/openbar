/*
 * Configuration parser tests.  Run on any C17 host; exercises
 * defaults, directives, error reporting and the discovery fallbacks.
 */

#include "test.h"

#include "../openbar.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *
write_conf(const char *content)
{
	char	 template[] = "/tmp/openbar-conf-XXXXXX";
	char	*path;
	FILE	*fp;
	int	 fd;

	fd = mkstemp(template);
	if (fd == -1) {
		perror("mkstemp");
		exit(1);
	}
	path = strdup(template);
	if (path == NULL)
		exit(1);
	fp = fdopen(fd, "w");
	if (fp == NULL)
		exit(1);
	if (fputs(content, fp) == EOF) {
		perror("fputs");
		exit(1);
	}
	if (fclose(fp) != 0) {
		perror("fclose");
		exit(1);
	}
	return path;
}

static void
quiet(void)
{
	if (freopen("/dev/null", "w", stderr) == NULL) {
		perror("freopen");
		exit(1);
	}
}

static void
test_defaults(void)
{
	struct conf	c;

	conf_defaults(&c);
	CHECK(c.logo == NULL);
	CHECK(c.interface == NULL);
	CHECK_STR(c.fontname, "sans-serif:pixelsize=14:bold");
	CHECK_STR(c.colors[COLOR_FG], "#000000");
	CHECK_STR(c.colors[COLOR_BG], "#CCCCCC");
	CHECK_STR(c.colors[COLOR_URGENT], "#FC8814");
	CHECK(c.barheight == 24);
	CHECK(c.gap[0] == 0 && c.gap[1] == 0 && c.gap[2] == 0 &&
	    c.gap[3] == 0);
	for (int i = 0; i < WIDGET_NITEMS; i++)
		CHECK(!c.enabled[i]);
	conf_free(&c);
}

static void
test_valid(void)
{
	const char	*text =
	    "# full configuration\n"
	    "\n"
	    "  logo  \"Open Bar\"  \n"
	    "fontname \"DejaVu Sans Mono:size=12\"\n"
	    "barheight 30\n"
	    "gap 1 2 3 4\n"
	    "color barbg \"#ABCDEF\"\n"
	    "color barfg white\n"
	    "color urgent '#123'\n"
	    "interface iwm0\n"
	    "show hostname\n"
	    "show date\n"
	    "show cpu\n"
	    "show mem\n"
	    "show load\n"
	    "show bat\n"
	    "show vpn\n"
	    "show net\n"
	    "hide bat\n";
	char		*path = write_conf(text);
	struct conf	 c;
	int		 rc;

	rc = conf_load(path, &c);
	CHECK(rc == 0);
	CHECK_STR(c.logo, "Open Bar");
	CHECK_STR(c.fontname, "DejaVu Sans Mono:size=12");
	CHECK(c.barheight == 30);
	CHECK(c.gap[0] == 1 && c.gap[1] == 2 && c.gap[2] == 3 &&
	    c.gap[3] == 4);
	CHECK_STR(c.colors[COLOR_BG], "#ABCDEF");
	CHECK_STR(c.colors[COLOR_FG], "white");
	CHECK_STR(c.colors[COLOR_URGENT], "#123");
	CHECK_STR(c.interface, "iwm0");
	for (int i = 0; i < WIDGET_NITEMS; i++) {
		if ((enum widget)i == WIDGET_BAT)
			CHECK(!c.enabled[i]);
		else
			CHECK(c.enabled[i]);
	}
	conf_free(&c);
	unlink(path);
	free(path);
}

static void
test_missing_logo(void)
{
	char		*path = write_conf("barheight 20\n");
	struct conf	 c;

	quiet();
	CHECK(conf_load(path, &c) == -1);
	unlink(path);
	free(path);
}

static void
test_duplicate_last_wins(void)
{
	char		*path = write_conf(
	    "logo first\n"
	    "barheight 20\n"
	    "barheight 40\n"
	    "logo second\n");
	struct conf	 c;

	CHECK(conf_load(path, &c) == 0);
	CHECK_STR(c.logo, "second");
	CHECK(c.barheight == 40);
	conf_free(&c);
	unlink(path);
	free(path);
}

static void
expect_fail(const char *text)
{
	char		*path = write_conf(text);
	struct conf	 c;

	quiet();
	CHECK(conf_load(path, &c) == -1);
	unlink(path);
	free(path);
}

static void
test_invalid(void)
{
	/* numeric errors */
	expect_fail("logo x\nbarheight 0\n");
	expect_fail("logo x\nbarheight 61\n");
	expect_fail("logo x\nbarheight abc\n");
	expect_fail("logo x\nbarheight 3.5\n");
	expect_fail("logo x\nbarheight\n");
	/* gap errors */
	expect_fail("logo x\ngap 1 2 3\n");
	expect_fail("logo x\ngap 1 2 3 4 5\n");
	expect_fail("logo x\ngap -1 0 0 0\n");
	expect_fail("logo x\ngap 0 0 0 x\n");
	/* widget errors */
	expect_fail("logo x\nshow bogus\n");
	expect_fail("logo x\nhide\n");
	expect_fail("logo x\nshow\n");
	/* color errors */
	expect_fail("logo x\ncolor noslot #fff\n");
	expect_fail("logo x\ncolor\n");
	expect_fail("logo x\ncolor barfg\n");
	/* quoting errors */
	expect_fail("logo x\nfontname \"unterminated\n");
	expect_fail("logo x\nfontname \"closed\" trailing\n");
	expect_fail("logo \"\"\n");
	/* interface errors */
	expect_fail("logo x\ninterface looooooooooooong0\n");
	expect_fail("logo x\ninterface \"bad name\"\n");
	expect_fail("logo x\ninterface 0bad\n");
	/* missing argument */
	expect_fail("logo x\ninterface\n");
	/* logo too long */
	expect_fail("logo "
	    "01234567890123456789012345678901234567890123456789"
	    "01234567890123456789012345678901234567890123456789"
	    "012345678901234567890123456789\n");
}

static void
test_unknown_keyword_ignored(void)
{
	char		*path = write_conf("logo x\nfrobnicate 42\n");
	struct conf	 c;

	quiet();
	CHECK(conf_load(path, &c) == 0);
	conf_free(&c);
	unlink(path);
	free(path);
}

static void
test_widget_lookup(void)
{
	CHECK(widget_lookup("hostname") == WIDGET_HOSTNAME);
	CHECK(widget_lookup("date") == WIDGET_DATE);
	CHECK(widget_lookup("cpu") == WIDGET_CPU);
	CHECK(widget_lookup("mem") == WIDGET_MEM);
	CHECK(widget_lookup("load") == WIDGET_LOAD);
	CHECK(widget_lookup("bat") == WIDGET_BAT);
	CHECK(widget_lookup("vpn") == WIDGET_VPN);
	CHECK(widget_lookup("net") == WIDGET_NET);
	CHECK(widget_lookup("bogus") == -1);
	CHECK(widget_lookup("") == -1);
	CHECK(widget_lookup("CPU") == -1);
}

static void
test_line_too_long(void)
{
	char	buf[8192];

	memset(buf, 'x', sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	{
		char	*path = write_conf("logo ");
		/* build a file with a huge line in one go */
		char	*tmp = malloc(strlen(path) + sizeof(buf) + 2);
		FILE	*fp;

		snprintf(tmp, strlen(path) + sizeof(buf) + 2, "%s", path);
		fp = fopen(path, "a");
		if (fp == NULL)
			exit(1);
		if (fputs(buf, fp) == EOF || fclose(fp) != 0)
			exit(1);
		free(tmp);

		{
			struct conf c;

			quiet();
			CHECK(conf_load(path, &c) == -1);
		}
		unlink(path);
		free(path);
	}
}

static void
run_tests(void)
{
	test_defaults();
	test_valid();
	test_missing_logo();
	test_duplicate_last_wins();
	test_invalid();
	test_unknown_keyword_ignored();
	test_widget_lookup();
	test_line_too_long();
}

TEST_MAIN()
