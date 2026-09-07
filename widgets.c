/*
 * Copyright (c) 2024 Gonzalo Rodriguez <gonzalo@x61.sh>
 * Copyright (c) 2024-2026 David Uhden Collado <david@uhden.dev>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Kernel metric collection for the widget set.
 *
 * Everything here uses OpenBSD libc/kernel interfaces directly; no
 * external commands are executed.  Failures of individual metrics
 * (missing sensors, missing battery, dead interfaces) degrade the
 * corresponding widget to "N/A" and are never fatal.
 */

#include "openbar.h"

#include <sys/ioctl.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <machine/apmvar.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uvm/uvmexp.h>

/* Upper bound of sensor devices scanned for a CPU temperature. */
#define SENSOR_DEV_MAX		64

/* Sensor value is expressed in microkelvin. */
#define MICROKELVIN_FREEZING	273150000LL

static void	collect_hostname(struct openbar *);
static void	collect_date(struct openbar *);
static void	collect_cpu(struct openbar *);
static void	collect_mem(struct openbar *);
static void	collect_load(struct openbar *);
static void	collect_bat(struct openbar *);
static void	collect_ifaddrs(struct openbar *);
static int	cpu_sensor_find(struct openbar *);

void
collect_widget(struct openbar *app, enum widget w)
{
	switch (w) {
	case WIDGET_HOSTNAME:
		collect_hostname(app);
		break;
	case WIDGET_DATE:
		collect_date(app);
		break;
	case WIDGET_CPU:
		collect_cpu(app);
		break;
	case WIDGET_MEM:
		collect_mem(app);
		break;
	case WIDGET_LOAD:
		collect_load(app);
		break;
	case WIDGET_BAT:
		collect_bat(app);
		break;
	case WIDGET_VPN:
	case WIDGET_NET:
		/* both read interface state through getifaddrs(3) */
		collect_ifaddrs(app);
		break;
	default:
		break;
	}
}

static void
collect_hostname(struct openbar *app)
{
	if (gethostname(app->hostname, sizeof(app->hostname)) == -1)
		strlcpy(app->hostname, "N/A", sizeof(app->hostname));
	app->hostname[sizeof(app->hostname) - 1] = '\0';
}

static void
collect_date(struct openbar *app)
{
	app->now = time(NULL);
}

/*
 * CPU frequency.  hw.cpuspeed is not readable under any pledge(2)
 * promise, so this runs once before the display process pledges; the
 * value is then displayed statically.  The temperature sensor below
 * stays live.
 */
void
collect_cpu_init(struct openbar *app)
{
	int	 mib[2] = {CTL_HW, HW_CPUSPEED};
	size_t	 len = sizeof(app->cpu_mhz);

	if (sysctl(mib, 2, &app->cpu_mhz, &len, NULL, 0) == -1) {
		app->cpu_have_freq = false;
		return;
	}
	app->cpu_have_freq = true;
}

/*
 * Locate the CPU temperature sensor device (hw.sensors.cpu0.temp0 on
 * Intel machines, hw.sensors.km0.temp0 on AMD).  The device index is
 * cached in app->cpu_sensor: 0 not yet searched, -1 none, otherwise
 * index+1.
 */
static int
cpu_sensor_find(struct openbar *app)
{
	struct sensordev	sd;
	int			mib[3], dev;

	if (app->cpu_sensor != 0)
		return app->cpu_sensor > 0 ? app->cpu_sensor - 1 : -1;

	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;
	for (dev = 0; dev < SENSOR_DEV_MAX; dev++) {
		size_t	len = sizeof(sd);

		mib[2] = dev;
		if (sysctl(mib, 3, &sd, &len, NULL, 0) == -1) {
			if (errno == ENOENT)
				break;		/* end of device list */
			continue;		/* ENXIO: hole in the list */
		}
		if (strncmp(sd.xname, "cpu", 3) == 0 ||
		    strncmp(sd.xname, "km", 2) == 0) {
			app->cpu_sensor = dev + 1;
			return dev;
		}
	}
	app->cpu_sensor = -1;
	return -1;
}

static void
collect_cpu(struct openbar *app)
{
	struct sensor	s;
	size_t		len = sizeof(s);
	int		dev, mib[5];

	dev = cpu_sensor_find(app);
	mib[0] = CTL_HW;
	mib[1] = HW_SENSORS;
	mib[2] = dev;
	mib[3] = SENSOR_TEMP;
	mib[4] = 0;
	if (dev >= 0 && sysctl(mib, 5, &s, &len, NULL, 0) != -1 &&
	    (s.flags & SENSOR_FINVALID) == 0) {
		app->cpu_temp = (int)((s.value - MICROKELVIN_FREEZING) /
		    1000000);
		app->cpu_have_temp = true;
		return;
	}
	app->cpu_have_temp = false;
}

static void
collect_mem(struct openbar *app)
{
	struct uvmexp	uv;
	int		 mib[2] = {CTL_VM, VM_UVMEXP};
	size_t		 len = sizeof(uv);

	if (sysctl(mib, 2, &uv, &len, NULL, 0) == -1) {
		app->mem_valid = false;
		return;
	}
	/*
	 * Free physical memory in MiB.  free is a page count and
	 * pagesize is in bytes, so wide arithmetic avoids any overflow
	 * before the division.
	 */
	app->mem_free_mb = (unsigned long long)uv.free *
	    (unsigned long long)uv.pagesize / (1024ULL * 1024ULL);
	app->mem_valid = true;
}

static void
collect_load(struct openbar *app)
{
	app->load_valid = getloadavg(&app->load1, 1) == 1;
}

static void
collect_bat(struct openbar *app)
{
	struct apm_power_info	pi;

	app->bat_pct = -1;
	if (app->apm_fd == -1)
		return;
	if (ioctl(app->apm_fd, APM_IOC_GETPOWER, &pi) == -1)
		return;
	if (pi.battery_state == APM_BATT_UNKNOWN ||
	    pi.battery_life == APM_BATT_UNKNOWN || pi.battery_life > 100)
		return;
	app->bat_pct = pi.battery_life;
}

/*
 * Interface state: the private IPv4 address of the configured
 * interface and WireGuard presence.  A WireGuard tunnel counts as
 * active when a wg* interface is up, running and has an address;
 * wg(4) sets IFF_RUNNING once its UDP socket is bound.
 */
static void
collect_ifaddrs(struct openbar *app)
{
	struct ifaddrs		*ifap, *ifa;
	struct sockaddr_in	*sa;

	app->vpn_up = false;
	strlcpy(app->int_ip4, "N/A", sizeof(app->int_ip4));

	if (getifaddrs(&ifap) == -1)
		return;

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_name == NULL || ifa->ifa_addr == NULL)
			continue;

		if (app->conf.interface != NULL &&
		    strcmp(ifa->ifa_name, app->conf.interface) == 0 &&
		    ifa->ifa_addr->sa_family == AF_INET) {
			char	buf[ADDR4_STRLEN];

			sa = (struct sockaddr_in *)ifa->ifa_addr;
			if (inet_ntop(AF_INET, &sa->sin_addr, buf,
			    sizeof(buf)) != NULL)
				strlcpy(app->int_ip4, buf,
				    sizeof(app->int_ip4));
		}

		if (strncmp(ifa->ifa_name, "wg", 2) == 0 &&
		    (ifa->ifa_flags & IFF_UP) &&
		    (ifa->ifa_flags & IFF_RUNNING) &&
		    (ifa->ifa_addr->sa_family == AF_INET ||
		    ifa->ifa_addr->sa_family == AF_INET6))
			app->vpn_up = true;
	}
	freeifaddrs(ifap);
}

/*
 * Open the APM device.  Called before pledge(2)/unveil(2); the
 * descriptor stays open so later APM_IOC_GETPOWER calls need no
 * filesystem access.  Returns a descriptor or -1.
 */
int
apm_open(void)
{
	return open("/dev/apm", O_RDONLY);
}
