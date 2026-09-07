/*
 * Widget formatting and bar composition tests.  Pure C17, host-runnable.
 */

#include "test.h"

#include "../openbar.h"

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
test_fmt_cpu(void)
{
	struct openbar	app;
	struct witem	it;

	fresh_app(&app);
	app.cpu_mhz = 2400;
	app.cpu_temp = 45;
	app.cpu_have_freq = true;
	app.cpu_have_temp = true;
	fmt_widget(&app, WIDGET_CPU, &it);
	CHECK_STR(it.text, "CPU: 2400MHz (45 C)");
	CHECK(!it.urgent);

	app.cpu_have_temp = false;
	fmt_widget(&app, WIDGET_CPU, &it);
	CHECK_STR(it.text, "CPU: 2400MHz (x)");

	app.cpu_have_freq = false;
	fmt_widget(&app, WIDGET_CPU, &it);
	CHECK_STR(it.text, "CPU: N/A (x)");

	app.cpu_have_temp = true;
	app.cpu_temp = -10;
	fmt_widget(&app, WIDGET_CPU, &it);
	CHECK_STR(it.text, "CPU: N/A (-10 C)");
	conf_free(&app.conf);
}

static void
test_fmt_mem_load(void)
{
	struct openbar	app;
	struct witem	it;

	fresh_app(&app);
	app.mem_valid = true;
	app.mem_free_mb = 1024;
	fmt_widget(&app, WIDGET_MEM, &it);
	CHECK_STR(it.text, "Mem: 1024 MB");

	app.mem_valid = false;
	fmt_widget(&app, WIDGET_MEM, &it);
	CHECK_STR(it.text, "Mem: N/A");

	app.load_valid = true;
	app.load1 = 0.42;
	fmt_widget(&app, WIDGET_LOAD, &it);
	CHECK_STR(it.text, "Load: 0.42");

	app.load_valid = false;
	fmt_widget(&app, WIDGET_LOAD, &it);
	CHECK_STR(it.text, "Load: N/A");
	conf_free(&app.conf);
}

static void
test_fmt_bat(void)
{
	struct openbar	app;
	struct witem	it;

	fresh_app(&app);
	app.bat_pct = -1;
	fmt_widget(&app, WIDGET_BAT, &it);
	CHECK_STR(it.text, "Bat: N/A");
	CHECK(!it.urgent);

	app.bat_pct = 100;
	fmt_widget(&app, WIDGET_BAT, &it);
	CHECK_STR(it.text, "Bat: 100%");
	CHECK(!it.urgent);

	app.bat_pct = 15;
	fmt_widget(&app, WIDGET_BAT, &it);
	CHECK_STR(it.text, "Bat: 15%");
	CHECK(it.urgent);

	app.bat_pct = 0;
	fmt_widget(&app, WIDGET_BAT, &it);
	CHECK_STR(it.text, "Bat: 0%");
	CHECK(it.urgent);
	conf_free(&app.conf);
}

static void
test_fmt_vpn_net(void)
{
	struct openbar	app;
	struct witem	it;

	fresh_app(&app);
	app.vpn_up = true;
	fmt_widget(&app, WIDGET_VPN, &it);
	CHECK_STR(it.text, "VPN: VPN");
	app.vpn_up = false;
	fmt_widget(&app, WIDGET_VPN, &it);
	CHECK_STR(it.text, "VPN: No VPN");

	strcpy(app.pub_ip4, "203.0.113.7");
	strcpy(app.pub_ip6, "2001:db8::1");
	strcpy(app.int_ip4, "192.168.1.10");
	fmt_widget(&app, WIDGET_NET, &it);
	CHECK_STR(it.text, "IPs: 203.0.113.7 | 2001:db8::1 ~ 192.168.1.10");

	strcpy(app.pub_ip4, "N/A");
	strcpy(app.pub_ip6, "N/A");
	strcpy(app.int_ip4, "N/A");
	fmt_widget(&app, WIDGET_NET, &it);
	CHECK_STR(it.text, "IPs: N/A | N/A ~ N/A");
	conf_free(&app.conf);
}

static void
test_fmt_date(void)
{
	struct openbar	app;
	struct witem	it;
	struct tm	tm;

	fresh_app(&app);
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 120;	/* 2020 */
	tm.tm_mon = 0;
	tm.tm_mday = 6;
	tm.tm_hour = 12;
	tm.tm_min = 34;
	tm.tm_sec = 56;
	app.now = mktime(&tm);
	fmt_widget(&app, WIDGET_DATE, &it);
	CHECK_STR(it.text, "Mon 06 Jan 12:34");

	app.now = (time_t)-1;
	fmt_widget(&app, WIDGET_DATE, &it);
	CHECK_STR(it.text, "N/A");
	conf_free(&app.conf);
}

static void
test_fmt_hostname(void)
{
	struct openbar	app;
	struct witem	it;

	fresh_app(&app);
	strcpy(app.hostname, "puffy");
	fmt_widget(&app, WIDGET_HOSTNAME, &it);
	CHECK_STR(it.text, "puffy");
	conf_free(&app.conf);
}

static void
test_compose(void)
{
	struct openbar	app;

	fresh_app(&app);
	conf_free(&app.conf);
	memset(&app.conf, 0, sizeof(app.conf));
	app.conf.logo = strdup("OpenBar");
	app.conf.enabled[WIDGET_HOSTNAME] = true;
	app.conf.enabled[WIDGET_MEM] = true;
	strcpy(app.hostname, "puffy");
	app.mem_valid = true;
	app.mem_free_mb = 512;

	compose_bar(&app);
	CHECK_STR(app.bar_text, "OpenBar | puffy | Mem: 512 MB |");
	CHECK(app.dirty);

	/* recomposing unchanged state must not mark it dirty again */
	app.dirty = false;
	compose_bar(&app);
	CHECK(!app.dirty);

	/* a metric change rebuilds the line */
	app.mem_free_mb = 513;
	compose_bar(&app);
	CHECK_STR(app.bar_text, "OpenBar | puffy | Mem: 513 MB |");
	CHECK(app.dirty);

	/* nothing enabled, no logo: empty line */
	app.dirty = false;
	app.conf.enabled[WIDGET_HOSTNAME] = false;
	app.conf.enabled[WIDGET_MEM] = false;
	free(app.conf.logo);
	app.conf.logo = NULL;
	compose_bar(&app);
	CHECK_STR(app.bar_text, "");
	CHECK(app.dirty);

	/* disabling a widget shrinks the line */
	app.conf.logo = strdup("OpenBar");
	app.conf.enabled[WIDGET_BAT] = true;
	app.bat_pct = 42;
	compose_bar(&app);
	CHECK_STR(app.bar_text, "OpenBar | Bat: 42% |");
	CHECK(app.dirty);
	free(app.conf.logo);
}

static void
run_tests(void)
{
	setenv("TZ", "UTC0", 1);
	tzset();
	test_fmt_cpu();
	test_fmt_mem_load();
	test_fmt_bat();
	test_fmt_vpn_net();
	test_fmt_date();
	test_fmt_hostname();
	test_compose();
}

TEST_MAIN()
