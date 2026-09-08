/*
 * Locale policy tests.
 *
 * openbar pins the C locale at startup and never reads LANG or LC_*,
 * so its interface language (U.S. English) and its formatting must be
 * identical no matter what the environment says.  These tests set
 * hostile locale variables and verify that the invariants hold.
 */

#include "test.h"

#include "../openbar.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
fresh_app(struct openbar *app)
{
	memset(app, 0, sizeof(*app));
	app->apm_fd = -1;
	app->ipc_fd = -1;
	app->ipc_pid = -1;
	app->bat_pct = -1;
	conf_defaults(&app->conf);
}

static void
test_locale_env_ignored(void)
{
	setenv("LANG", "de_DE.UTF-8", 1);
	setenv("LANGUAGE", "fr_FR:de_DE", 1);
	setenv("LC_ALL", "es_ES.UTF-8", 1);
	setenv("LC_MESSAGES", "fr_FR.UTF-8", 1);
	setenv("LC_NUMERIC", "de_DE.UTF-8", 1);
	setenv("LC_TIME", "nl_NL.UTF-8", 1);

	/* what the production main() runs: pin, then format */
	CHECK(locale_init() == 0);
	CHECK_STR(setlocale(LC_ALL, NULL), "C");
}

static void
test_decimal_separator(void)
{
	struct openbar	app;
	struct witem	it;

	setenv("LC_NUMERIC", "de_DE.UTF-8", 1);

	fresh_app(&app);
	app.load_valid = true;
	app.load1 = 0.42;
	fmt_widget(&app, WIDGET_LOAD, &it);
	CHECK_STR(it.text, "Load: 0.42");
	conf_free(&app.conf);
}

static void
check_date(struct openbar *app, int year, int mon, int mday, int hour,
    int min, const char *expected)
{
	struct tm	tm;
	struct witem	it;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon = mon;
	tm.tm_mday = mday;
	tm.tm_hour = hour;
	tm.tm_min = min;
	app->now = mktime(&tm);
	fmt_widget(app, WIDGET_DATE, &it);
	CHECK_STR(it.text, expected);
}

static void
test_date_names_english(void)
{
	struct openbar	app;

	/* translated month/day names would differ under these locales */
	setenv("LC_ALL", "de_DE.UTF-8", 1);
	setenv("TZ", "UTC0", 1);
	tzset();

	fresh_app(&app);
	check_date(&app, 2020, 0, 6, 12, 34, "Mon 06 Jan 12:34");
	check_date(&app, 2020, 2, 7, 21, 43, "Sat 07 Mar 21:43");
	check_date(&app, 2020, 11, 31, 23, 59, "Thu 31 Dec 23:59");
	conf_free(&app.conf);
}

static void
run_tests(void)
{
	test_locale_env_ignored();
	test_decimal_separator();
	test_date_names_english();
}

TEST_MAIN()
