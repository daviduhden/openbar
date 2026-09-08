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
 * cwm-style configuration parser.
 *
 * This translation unit is pure ISO C17: it performs no I/O beyond
 * reading the configuration file, so it can be unit-tested on any
 * host.  It owns the strings of struct conf; callers must call
 * conf_free() when done.
 */

#include "openbar.h"

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONF_LINE_MAX		4096	/* per-line bound */
#define HOME_MAX		1024	/* $HOME length bound */
#define BARHEIGHT_MIN		12
#define BARHEIGHT_MAX		60

const char *const default_colors[COLOR_NITEMS] = {
	"#000000",		/* COLOR_FG */
	"#CCCCCC",		/* COLOR_BG */
	"#FC8814",		/* COLOR_URGENT */
};

const char default_font[] = "sans-serif:pixelsize=14:bold";

static const char *const color_names[COLOR_NITEMS] = {
	"barfg",		/* COLOR_FG */
	"barbg",		/* COLOR_BG */
	"urgent",		/* COLOR_URGENT */
};

static const char *const widget_names[WIDGET_NITEMS] = {
	[WIDGET_HOSTNAME]	= "hostname",
	[WIDGET_DATE]		= "date",
	[WIDGET_CPU]		= "cpu",
	[WIDGET_MEM]		= "mem",
	[WIDGET_LOAD]		= "load",
	[WIDGET_BAT]		= "bat",
	[WIDGET_VPN]		= "vpn",
	[WIDGET_NET]		= "net",
};

static char	*xstrdup(const char *);
static void	 setstr(char **, const char *);
static int	 conf_parse_line(const char *, size_t, char *, struct conf *);
static int	 parse_int(const char *, size_t, const char *, long long,
    long long, int *);
static int	 parse_gaps(const char *, size_t, char *, struct conf *);
static int	 parse_color(const char *, size_t, char *, struct conf *);
static int	 parse_logo(const char *, size_t, const char *, struct conf *);
static int	 parse_interface(const char *, size_t, const char *,
    struct conf *);
static char	*strip_quotes(char *, const char **);
static char	*trim(char *);

static char *
xstrdup(const char *str)
{
	char	*p;

	if ((p = strdup(str)) == NULL)
		err(1, "strdup");
	return p;
}

/*
 * ASCII predicates for the interface-name grammar.  Interface names
 * are a fixed, machine-readable syntax: classification must not
 * depend on the locale, so <ctype.h> is deliberately avoided.
 */
static int
ident_start(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	    c == '_';
}

static int
ident_char(unsigned char c)
{
	return ident_start(c) || (c >= '0' && c <= '9') || c == '-' ||
	    c == '.';
}

static void
setstr(char **dest, const char *value)
{
	free(*dest);
	*dest = xstrdup(value);
}

static char *
trim(char *s)
{
	char	*end;

	while (*s == ' ' || *s == '\t')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
		end--;
	*end = '\0';
	return s;
}

/*
 * Handle quoting.  If arg starts with a quote character the remainder
 * of the argument must be one quoted string; the quote is removed and
 * a pointer to the (possibly empty) contents is returned.  Trailing
 * garbage after the closing quote is an error.  Unquoted arguments are
 * returned trimmed.
 */
static char *
strip_quotes(char *arg, const char **errmsg)
{
	char	quote, *end;

	arg = trim(arg);
	if (*arg != '"' && *arg != '\'')
		return arg;

	quote = *arg;
	end = strchr(arg + 1, quote);
	if (end == NULL) {
		*errmsg = "unterminated quote";
		return NULL;
	}
	*end = '\0';
	if (*trim(end + 1) != '\0') {
		*errmsg = "trailing characters after quoted string";
		return NULL;
	}
	return arg + 1;
}

static int
parse_int(const char *path, size_t lineno, const char *value, long long min,
    long long max, int *out)
{
	const char	*errstr;
	long long	 v;

	v = strtonum(value, min, max, &errstr);
	if (errstr != NULL) {
		warnx("%s:%zu: invalid value '%s' (expected %lld-%lld)",
		    path, lineno, value, min, max);
		return -1;
	}
	*out = (int)v;
	return 0;
}

static int
parse_gaps(const char *path, size_t lineno, char *arg, struct conf *c)
{
	char		*saveptr = NULL, *token;
	const char	*errstr;
	int		 gaps[4], n = 0;

	for (token = strtok_r(arg, " \t", &saveptr); token != NULL;
	    token = strtok_r(NULL, " \t", &saveptr)) {
		long long v;

		if (n == 4) {
			warnx("%s:%zu: gap takes exactly four values",
			    path, lineno);
			return -1;
		}
		v = strtonum(token, 0, INT_MAX, &errstr);
		if (errstr != NULL) {
			warnx("%s:%zu: invalid gap value '%s'", path, lineno,
			    token);
			return -1;
		}
		gaps[n++] = (int)v;
	}
	if (n != 4) {
		warnx("%s:%zu: gap takes exactly four values", path,
		    lineno);
		return -1;
	}
	memcpy(c->gap, gaps, sizeof(c->gap));
	return 0;
}

static int
parse_color(const char *path, size_t lineno, char *arg, struct conf *c)
{
	char		*space, *value, *unquoted;
	const char	*errmsg;
	unsigned int	 i;

	space = strpbrk(arg, " \t");
	if (space == NULL) {
		warnx("%s:%zu: color requires a slot and a value", path,
		    lineno);
		return -1;
	}
	*space = '\0';
	value = trim(space + 1);
	unquoted = strip_quotes(value, &errmsg);
	if (unquoted == NULL) {
		warnx("%s:%zu: %s", path, lineno, errmsg);
		return -1;
	}
	if (*unquoted == '\0') {
		warnx("%s:%zu: color requires a slot and a value", path,
		    lineno);
		return -1;
	}
	if (strlen(unquoted) > 63) {
		warnx("%s:%zu: color name too long", path, lineno);
		return -1;
	}
	for (i = 0; i < COLOR_NITEMS; i++) {
		if (strcmp(arg, color_names[i]) == 0) {
			setstr(&c->colors[i], unquoted);
			return 0;
		}
	}
	warnx("%s:%zu: unknown color slot '%s'", path, lineno, arg);
	return -1;
}

static int
parse_logo(const char *path, size_t lineno, const char *value,
    struct conf *c)
{
	size_t	len = strlen(value);

	if (len == 0) {
		warnx("%s:%zu: logo must not be empty", path, lineno);
		return -1;
	}
	if (len > LOGO_MAX) {
		warnx("%s:%zu: logo exceeds %d characters", path, lineno,
		    LOGO_MAX);
		return -1;
	}
	setstr(&c->logo, value);
	return 0;
}

static int
parse_interface(const char *path, size_t lineno, const char *value,
    struct conf *c)
{
	size_t	len = strlen(value);

	if (len == 0 || len > IFNAME_MAX) {
		warnx("%s:%zu: invalid interface name '%s'", path, lineno,
		    value);
		return -1;
	}
	if (!ident_start((unsigned char)value[0])) {
		warnx("%s:%zu: invalid interface name '%s'", path, lineno,
		    value);
		return -1;
	}
	for (size_t i = 1; i < len; i++) {
		if (!ident_char((unsigned char)value[i])) {
			warnx("%s:%zu: invalid interface name '%s'", path,
			    lineno, value);
			return -1;
		}
	}
	setstr(&c->interface, value);
	return 0;
}

static int
conf_parse_line(const char *path, size_t lineno, char *line, struct conf *c)
{
	const char	*errmsg;
	char		*kw, *arg, *value;

	line[strcspn(line, "\n")] = '\0';
	kw = trim(line);
	if (*kw == '\0' || *kw == '#')
		return 0;

	arg = strpbrk(kw, " \t");
	if (arg != NULL) {
		*arg++ = '\0';
		arg = trim(arg);
	}

	if (strcmp(kw, "show") == 0 || strcmp(kw, "hide") == 0) {
		int	w;

		if (arg == NULL || *arg == '\0') {
			warnx("%s:%zu: %s requires a widget name", path,
			    lineno, kw);
			return -1;
		}
		if ((w = widget_lookup(arg)) == -1) {
			warnx("%s:%zu: unknown widget '%s'", path, lineno,
			    arg);
			return -1;
		}
		c->enabled[w] = (strcmp(kw, "show") == 0);
		return 0;
	}

	if (arg == NULL || *arg == '\0') {
		warnx("%s:%zu: missing argument for '%s'", path, lineno,
		    kw);
		return -1;
	}

	if (strcmp(kw, "barheight") == 0)
		return parse_int(path, lineno, arg, BARHEIGHT_MIN,
		    BARHEIGHT_MAX, &c->barheight);
	if (strcmp(kw, "gap") == 0)
		return parse_gaps(path, lineno, arg, c);
	if (strcmp(kw, "color") == 0)
		return parse_color(path, lineno, arg, c);
	if (strcmp(kw, "logo") == 0) {
		value = strip_quotes(arg, &errmsg);
		if (value == NULL) {
			warnx("%s:%zu: %s", path, lineno, errmsg);
			return -1;
		}
		return parse_logo(path, lineno, value, c);
	}
	if (strcmp(kw, "interface") == 0)
		return parse_interface(path, lineno, arg, c);
	if (strcmp(kw, "fontname") == 0) {
		value = strip_quotes(arg, &errmsg);
		if (value == NULL) {
			warnx("%s:%zu: %s", path, lineno, errmsg);
			return -1;
		}
		if (*value == '\0') {
			warnx("%s:%zu: fontname must not be empty", path,
			    lineno);
			return -1;
		}
		setstr(&c->fontname, value);
		return 0;
	}

	warnx("%s:%zu: ignoring unknown keyword '%s'", path, lineno, kw);
	return 0;
}

int
widget_lookup(const char *name)
{
	unsigned int	i;

	for (i = 0; i < WIDGET_NITEMS; i++) {
		if (strcmp(name, widget_names[i]) == 0)
			return (int)i;
	}
	return -1;
}

void
conf_defaults(struct conf *c)
{
	unsigned int	i;

	memset(c, 0, sizeof(*c));
	c->barheight = 24;
	c->fontname = xstrdup(default_font);
	for (i = 0; i < COLOR_NITEMS; i++)
		c->colors[i] = xstrdup(default_colors[i]);
}

void
conf_free(struct conf *c)
{
	unsigned int	i;

	free(c->logo);
	c->logo = NULL;
	free(c->interface);
	c->interface = NULL;
	free(c->fontname);
	c->fontname = NULL;
	for (i = 0; i < COLOR_NITEMS; i++) {
		free(c->colors[i]);
		c->colors[i] = NULL;
	}
}

/*
 * Load configuration.  On success conf_defaults() has been applied and
 * directives override it (last occurrence wins).  Returns 0 on success;
 * on failure a diagnostic including the file name and line number has
 * been printed, and conf_free() has been called, so the caller can
 * exit without further cleanup.
 */
int
conf_load(const char *path, struct conf *c)
{
	FILE	*fp = NULL;
	char	*line = NULL;
	size_t	 cap = 0, lineno = 0;
	int	 rc = -1;

	conf_defaults(c);

	fp = fopen(path, "r");
	if (fp == NULL) {
		warn("%s", path);
		goto out;
	}

	while (getline(&line, &cap, fp) != -1) {
		lineno++;
		if (strlen(line) > CONF_LINE_MAX) {
			warnx("%s:%zu: line exceeds %d characters", path,
			    lineno, CONF_LINE_MAX);
			goto out;
		}
		if (conf_parse_line(path, lineno, line, c) == -1)
			goto out;
	}
	if (ferror(fp)) {
		warn("%s", path);
		goto out;
	}
	if (c->logo == NULL) {
		warnx("%s: no 'logo' directive found", path);
		goto out;
	}

	rc = 0;
out:
	free(line);
	if (fp != NULL)
		fclose(fp);
	if (rc == -1)
		conf_free(c);
	return rc;
}

/*
 * Resolve the configuration path:
 * 1. the path given with -c;
 * 2. ~/.openbarrc if it exists and is readable;
 * 3. /etc/openbarrc.
 * The caller owns the returned string.
 */
char *
conf_resolve_path(const char *override_path)
{
	const char	*home;

	if (override_path != NULL)
		return xstrdup(override_path);

	home = getenv("HOME");
	if (home != NULL && home[0] != '\0' &&
	    strlen(home) < HOME_MAX) {
		size_t	 len = strlen(home) + sizeof("/.openbarrc");
		char	*p = malloc(len);

		if (p == NULL)
			err(1, "malloc");
		snprintf(p, len, "%s/.openbarrc", home);
		if (access(p, R_OK) == 0)
			return p;
		free(p);
	}
	return xstrdup("/etc/openbarrc");
}
