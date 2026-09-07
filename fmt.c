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
 * Widget formatting and bar line composition.
 *
 * Pure ISO C17: takes collected metrics from struct openbar and
 * produces display strings.  No X11 or kernel interfaces.
 */

#include "openbar.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* The cwm-style bar: "logo | widget | widget |". */
#define SEPARATOR	" |"

void
fmt_widget(const struct openbar *app, enum widget w, struct witem *out)
{
	out->urgent = false;

	switch (w) {
	case WIDGET_HOSTNAME:
		snprintf(out->text, sizeof(out->text), "%s",
		    app->hostname);
		break;
	case WIDGET_DATE: {
		char		buf[64];
		struct tm	tm;

		if (app->now == (time_t)-1 ||
		    localtime_r(&app->now, &tm) == NULL ||
		    strftime(buf, sizeof(buf), "%a %d %b %H:%M", &tm) == 0)
			snprintf(out->text, sizeof(out->text), "N/A");
		else
			snprintf(out->text, sizeof(out->text), "%s", buf);
		break;
	}
	case WIDGET_CPU:
		if (app->cpu_have_freq && app->cpu_have_temp)
			snprintf(out->text, sizeof(out->text),
			    "CPU: %4uMHz (%d C)", app->cpu_mhz,
			    app->cpu_temp);
		else if (app->cpu_have_freq)
			snprintf(out->text, sizeof(out->text),
			    "CPU: %4uMHz (x)", app->cpu_mhz);
		else if (app->cpu_have_temp)
			snprintf(out->text, sizeof(out->text),
			    "CPU: N/A (%d C)", app->cpu_temp);
		else
			snprintf(out->text, sizeof(out->text),
			    "CPU: N/A (x)");
		break;
	case WIDGET_MEM:
		if (app->mem_valid)
			snprintf(out->text, sizeof(out->text),
			    "Mem: %llu MB", app->mem_free_mb);
		else
			snprintf(out->text, sizeof(out->text),
			    "Mem: N/A");
		break;
	case WIDGET_LOAD:
		if (app->load_valid)
			snprintf(out->text, sizeof(out->text),
			    "Load: %.2f", app->load1);
		else
			snprintf(out->text, sizeof(out->text),
			    "Load: N/A");
		break;
	case WIDGET_BAT:
		if (app->bat_pct < 0)
			snprintf(out->text, sizeof(out->text), "Bat: N/A");
		else {
			snprintf(out->text, sizeof(out->text),
			    "Bat: %d%%", app->bat_pct);
			if (app->bat_pct <= 15)
				out->urgent = true;
		}
		break;
	case WIDGET_VPN:
		snprintf(out->text, sizeof(out->text), "VPN: %s",
		    app->vpn_up ? "VPN" : "No VPN");
		break;
	case WIDGET_NET:
		snprintf(out->text, sizeof(out->text),
		    "IPs: %s | %s ~ %s", app->pub_ip4, app->pub_ip6,
		    app->int_ip4);
		break;
	default:
		out->text[0] = '\0';
		break;
	}
}

/* Append src to dst, truncating at the buffer end. */
static void
bappend(char *dst, size_t dstsz, const char *src)
{
	size_t	pos = strlen(dst);

	if (pos + 1 >= dstsz)
		return;
	snprintf(dst + pos, dstsz - pos, "%s", src);
}

/*
 * Rebuild the complete bar line from the enabled widgets in display
 * order.  The previous line is kept; when nothing changed the dirty
 * flag stays clear so the renderer skips the redraw.
 */
void
compose_bar(struct openbar *app)
{
	char		line[BAR_TEXT_MAX];
	unsigned int	i;

	line[0] = '\0';
	if (app->conf.logo != NULL && app->conf.logo[0] != '\0') {
		bappend(line, sizeof(line), app->conf.logo);
		bappend(line, sizeof(line), SEPARATOR);
	}
	for (i = 0; i < WIDGET_NITEMS; i++) {
		if (!app->conf.enabled[i])
			continue;
		fmt_widget(app, (enum widget)i, &app->seg[i]);
		bappend(line, sizeof(line), " ");
		bappend(line, sizeof(line), app->seg[i].text);
		bappend(line, sizeof(line), SEPARATOR);
	}

	if (strcmp(line, app->bar_text) != 0) {
		snprintf(app->bar_text, sizeof(app->bar_text), "%s", line);
		app->dirty = true;
	}
}
