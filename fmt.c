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

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The cwm-style bar: "logo | widget | widget |". */
#define SEPARATOR	" |"

/*
 * English day and month abbreviations for the date widget.  The bar
 * interface is U.S. English only and must not depend on libc locale
 * tables, so the names are hard-coded here and strftime(3) is not
 * used for them.
 */
static const char *const day_abbrev[7] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *const month_abbrev[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/*
 * Pin the C locale before anything is formatted or parsed.  openbar
 * never reads LANG or LC_* and offers no translated interface; the
 * C locale keeps libc formatting deterministic everywhere: ASCII
 * decimal point, no digit grouping and English libc diagnostics.
 * Returns 0, or -1 if the C locale is unavailable (impossible on a
 * conforming C17 host, but the caller must not continue without it).
 */
int
locale_init(void)
{
	return setlocale(LC_ALL, "C") == NULL ? -1 : 0;
}

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
		struct tm	tm;

		if (app->now == (time_t)-1 ||
		    localtime_r(&app->now, &tm) == NULL ||
		    tm.tm_wday < 0 || tm.tm_wday > 6 ||
		    tm.tm_mon < 0 || tm.tm_mon > 11) {
			snprintf(out->text, sizeof(out->text), "N/A");
		} else {
			snprintf(out->text, sizeof(out->text),
			    "%s %02d %s %02d:%02d", day_abbrev[tm.tm_wday],
			    tm.tm_mday, month_abbrev[tm.tm_mon],
			    tm.tm_hour, tm.tm_min);
		}
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

/*
 * Largest prefix of src[0..keep) that ends on a UTF-8 character
 * boundary: a truncation that would split a multibyte sequence drops
 * the partial sequence entirely, so the result is always valid UTF-8.
 */
static size_t
utf8_truncate_boundary(const char *src, size_t keep)
{
	size_t	i, last = 0;

	for (i = 0; i < keep;) {
		unsigned char	c = (unsigned char)src[i];
		size_t		clen, j;

		if (c < 0x80) {
			i++;
			last = i;
			continue;
		}
		if (c < 0xC2 || c > 0xF4)
			break;		/* stray or invalid lead byte */
		clen = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
		if (i + clen > keep)
			break;		/* sequence crosses the limit */
		for (j = 1; j < clen; j++) {
			if (((unsigned char)src[i + j] & 0xC0) != 0x80)
				break;
		}
		if (j < clen)
			break;		/* malformed sequence */
		i += clen;
		last = i;
	}
	return last;
}

/*
 * Append src to dst, truncating at the buffer end without splitting a
 * UTF-8 sequence, so the rendered line is always valid UTF-8.
 */
static void
bappend(char *dst, size_t dstsz, const char *src)
{
	size_t	pos = strlen(dst);
	size_t	keep = strlen(src);

	if (dstsz == 0 || pos + 1 >= dstsz)
		return;
	if (keep > dstsz - 1 - pos)
		keep = dstsz - 1 - pos;
	keep = utf8_truncate_boundary(src, keep);
	memcpy(dst + pos, src, keep);
	dst[pos + keep] = '\0';
}

/*
 * Copy src into dst as a fresh string, never splitting a UTF-8
 * sequence at the truncation boundary.  dst must be a valid buffer;
 * a zero-sized dstsz leaves it untouched.
 */
void
utf8_bounded_copy(char *dst, const char *src, size_t dstsz)
{
	if (dstsz > 0)
		dst[0] = '\0';
	bappend(dst, dstsz, src);
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
