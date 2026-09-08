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
 * Display process: command line, X11/Xft rendering, refresh
 * scheduling, worker supervision and the OpenBSD sandbox.
 *
 * Lifecycle:
 *   pin the C locale (English interface, deterministic formatting)
 *   parse arguments
 *   -> load configuration
 *   -> fork the network worker (the only process keeping inet/dns)
 *   -> open X11, allocate colours, open the font, create the window
 *   -> open /dev/apm when the battery widget is enabled
 *   -> unveil(2) the few paths X11/fontconfig still need and lock
 *      the filesystem view
 *   -> pledge(2) the display process
 *   -> poll(2)-driven event loop
 *   -> cleanup
 *
 * Fonts and colours are opened before the unveil lock: fontconfig
 * reads its configuration and cache files during initialization.
 * Font fallback for glyphs missing from the configured font may still
 * load font files later, so the standard font directories and the
 * "rpath" promise are kept; both are bounded by the unveil policy.
 */

#include "openbar.h"

#include <sys/socket.h>
#include <sys/wait.h>

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FETCH_INTERVAL	300	/* seconds between public IP lookups */
#define FETCH_TIMEOUT	30	/* seconds to wait for a worker reply */

struct xstate {
	Display		*dpy;
	Window		 win;
	XftDraw		*draw;
	XftFont		*font;
	XftColor	 colors[COLOR_NITEMS];
	unsigned int	 ncolors;
	Visual		*visual;
	Colormap	 cmap;
	int		 screen;
	int		 w, h;
};

static volatile sig_atomic_t	quit_flag;
static volatile sig_atomic_t	chld_flag;

static void	 usage(FILE *);
static void	 setup_signals(void);
static void	 quit_handler(int);
static void	 chld_handler(int);
static int	 xerror_handler(Display *, XErrorEvent *);
static int	 xio_handler(Display *);
static char	*resolve_xauthority(void);
static int	 unveil_parent(const struct conf *, const char *);
static void	 build_pledge(char *, size_t, const struct conf *);
static int	 xstate_open(struct xstate *, const struct conf *);
static void	 xstate_close(struct xstate *);
static int	 window_create(struct xstate *, const struct conf *);
static void	 redraw(struct openbar *, struct xstate *);
static void	 draw_line(struct openbar *, struct xstate *);
static void	 handle_x_events(struct openbar *, struct xstate *);
static void	 collect_due(struct openbar *);
static void	 schedule(struct openbar *, enum widget,
    const struct timespec *);
static int	 next_timeout_ms(struct openbar *);
static int	 timespec_cmp(const struct timespec *,
    const struct timespec *);
static void	 reap_children(struct openbar *);
static void	 ipc_tick(struct openbar *);
static void	 ipc_read(struct openbar *);
static void	 apply_response(struct openbar *);
static void	 worker_lost(struct openbar *);

/* refresh periods in seconds; the date widget re-aligns to minute
 * boundaries, so its nominal interval is one second */
static const int widget_intervals[WIDGET_NITEMS] = {
	[WIDGET_HOSTNAME]	= 60,
	[WIDGET_DATE]		= 1,
	[WIDGET_CPU]		= 5,
	[WIDGET_MEM]		= 2,
	[WIDGET_LOAD]		= 2,
	[WIDGET_BAT]		= 30,
	[WIDGET_VPN]		= 10,
	[WIDGET_NET]		= 10,
};

static void
usage(FILE *f)
{
	fprintf(f, "usage: %s [-1] [-c path] [-h]\n", getprogname());
}

/*
 * Signal handlers only set flags; all processing happens in the main
 * loop where no async-signal-safety restrictions apply.
 */
static void
quit_handler(int signo)
{
	(void)signo;
	quit_flag = 1;
}

static void
chld_handler(int signo)
{
	(void)signo;
	chld_flag = 1;
}

static void
setup_signals(void)
{
	struct sigaction	sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1)
		err(1, "sigaction(SIGPIPE)");

	sa.sa_handler = quit_handler;
	if (sigaction(SIGTERM, &sa, NULL) == -1 ||
	    sigaction(SIGINT, &sa, NULL) == -1)
		err(1, "sigaction");

	sa.sa_handler = chld_handler;
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		err(1, "sigaction(SIGCHLD)");
}

/*
 * Keep X error handling minimal: ignore errors against windows that
 * disappeared, treat everything else as fatal.
 */
static int
xerror_handler(Display *dpy, XErrorEvent *ev)
{
	(void)dpy;
	if (ev->error_code == BadWindow || ev->error_code == BadDrawable)
		return 0;
	warnx("X error: request %d.%d, error %d", ev->request_code,
	    ev->minor_code, ev->error_code);
	exit(EXIT_FAILURE);
}

/*
 * Called by Xlib when the server connection dies.  The worker notices
 * the closed IPC descriptor and exits on its own.
 */
static noreturn int
xio_handler(Display *dpy)
{
	(void)dpy;
	warnx("lost connection to X server");
	_exit(EXIT_FAILURE);
}

/* Resolve the X authority file for the unveil policy. */
static char *
resolve_xauthority(void)
{
	const char	*auth, *home;
	char		 resolved[PATH_MAX];

	auth = getenv("XAUTHORITY");
	if (auth != NULL && auth[0] != '\0') {
		if (realpath(auth, resolved) == NULL)
			return NULL;
		return strdup(resolved);
	}
	home = getenv("HOME");
	if (home != NULL && home[0] != '\0' && strlen(home) < 1024) {
		size_t	 len = strlen(home) + sizeof("/.Xauthority");
		char	*candidate = malloc(len);

		if (candidate == NULL)
			err(1, "malloc");
		snprintf(candidate, len, "%s/.Xauthority", home);
		if (realpath(candidate, resolved) == NULL) {
			free(candidate);
			return NULL;
		}
		free(candidate);
		return strdup(resolved);
	}
	return NULL;
}

/* Unveil an existing path for reading; absent paths are fine. */
static int
unveil_read(const char *path)
{
	if (access(path, F_OK) == -1)
		return 0;
	if (unveil(path, "r") == -1) {
		warn("unveil %s", path);
		return -1;
	}
	return 0;
}

/*
 * Display process filesystem policy, applied after all initialization:
 *  - the X11 socket directory and the X authority file;
 *  - /dev/apm when the battery widget is enabled;
 *  - the fontconfig configuration, cache and font directories (lazy
 *    fallback font loading after the lock).
 * Everything else, including the configuration file, becomes
 * invisible.
 */
static int
unveil_parent(const struct conf *c, const char *xauth_path)
{
	static const char *const font_dirs[] = {
		"/etc/fonts",
		"/var/cache/fontconfig",
		"/usr/X11R6/lib/X11/fonts",
		"/usr/local/share/fonts",
	};
	static const char *const user_dirs[] = {
		"/.fonts",
		"/.local/share/fonts",
		"/.cache/fontconfig",
	};
	const char		*home;
	unsigned int		 i;

	if (unveil("/tmp/.X11-unix", "rw") == -1) {
		warn("unveil /tmp/.X11-unix");
		return -1;
	}
	if (xauth_path != NULL && unveil_read(xauth_path) == -1)
		return -1;
	if (c->enabled[WIDGET_BAT] && unveil_read("/dev/apm") == -1)
		return -1;
	for (i = 0; i < sizeof(font_dirs) / sizeof(font_dirs[0]); i++) {
		if (unveil_read(font_dirs[i]) == -1)
			return -1;
	}
	home = getenv("HOME");
	if (home != NULL && home[0] != '\0' && strlen(home) < 1024) {
		for (i = 0; i < sizeof(user_dirs) / sizeof(user_dirs[0]);
		    i++) {
			size_t	 len = strlen(home) + strlen(user_dirs[i]) + 1;
			char	*path = malloc(len);

			if (path == NULL)
				err(1, "malloc");
			snprintf(path, len, "%s%s", home, user_dirs[i]);
			if (unveil_read(path) == -1) {
				free(path);
				return -1;
			}
			free(path);
		}
	}
	if (unveil(NULL, NULL) == -1) {
		warn("unveil");
		return -1;
	}
	return 0;
}

/*
 * Steady-state pledge(2) for the display process, derived from the
 * enabled widgets:
 *  - stdio: X11 socket I/O (read/write/poll/ioctl), the IPC
 *    descriptor, signal and child management, timing;
 *  - rpath: lazy fontconfig fallback loading, bounded by unveil;
 *  - vminfo: VM_UVMEXP for the memory widget;
 *  - route: getifaddrs(3) (NET_RT_IFLIST) for the network/VPN widgets.
 * HW_SENSORS (CPU temperature), gethostname(3) and getloadavg(3) are
 * permitted under any promise set.  hw.cpuspeed is not readable under
 * pledge at all, so the CPU frequency is sampled once before pledging.
 */
static void
build_pledge(char *buf, size_t bufsz, const struct conf *c)
{
	strlcpy(buf, "stdio rpath", bufsz);
	if (c->enabled[WIDGET_MEM])
		strlcat(buf, " vminfo", bufsz);
	if (c->enabled[WIDGET_NET] || c->enabled[WIDGET_VPN])
		strlcat(buf, " route", bufsz);
}

static int
window_create(struct xstate *x, const struct conf *c)
{
	Atom		wm_state, wm_state_above, wm_bypass, wm_type;
	Atom		wm_type_dock, wm_skip_taskbar, wm_skip_pager, wm_sticky;
	Atom		wm_state_atoms[4];
	unsigned long	bypass = 1;
	int		sw = DisplayWidth(x->dpy, x->screen);
	int		sh = DisplayHeight(x->dpy, x->screen);
	int		w = sw - c->gap[2] - c->gap[3];
	int		h = c->barheight;

	if (w < 1) {
		warnx("horizontal gaps exceed screen width");
		return -1;
	}
	if (h > sh)
		h = sh;

	x->win = XCreateSimpleWindow(x->dpy,
	    RootWindow(x->dpy, x->screen), c->gap[2], c->gap[0],
	    (unsigned int)w, (unsigned int)h, 0,
	    BlackPixel(x->dpy, x->screen), WhitePixel(x->dpy, x->screen));
	XSelectInput(x->dpy, x->win, ExposureMask);

	/* EWMH: a dock window on every desktop, kept above */
	wm_state = XInternAtom(x->dpy, "_NET_WM_STATE", False);
	wm_state_above = XInternAtom(x->dpy, "_NET_WM_STATE_ABOVE", False);
	wm_bypass = XInternAtom(x->dpy, "_NET_WM_BYPASS_COMPOSITOR", False);
	wm_type = XInternAtom(x->dpy, "_NET_WM_WINDOW_TYPE", False);
	wm_type_dock = XInternAtom(x->dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	wm_skip_taskbar =
	    XInternAtom(x->dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
	wm_skip_pager = XInternAtom(x->dpy, "_NET_WM_STATE_SKIP_PAGER", False);
	wm_sticky = XInternAtom(x->dpy, "_NET_WM_STATE_STICKY", False);
	wm_state_atoms[0] = wm_state_above;
	wm_state_atoms[1] = wm_skip_taskbar;
	wm_state_atoms[2] = wm_skip_pager;
	wm_state_atoms[3] = wm_sticky;
	XChangeProperty(x->dpy, x->win, wm_state, XA_ATOM, 32,
	    PropModeReplace, (unsigned char *)wm_state_atoms, 4);
	XChangeProperty(x->dpy, x->win, wm_type, XA_ATOM, 32,
	    PropModeReplace, (unsigned char *)&wm_type_dock, 1);
	XChangeProperty(x->dpy, x->win, wm_bypass, XA_CARDINAL, 32,
	    PropModeReplace, (unsigned char *)&bypass, 1);

	XSetWindowBackground(x->dpy, x->win, x->colors[COLOR_BG].pixel);

	x->draw = XftDrawCreate(x->dpy, x->win, x->visual, x->cmap);
	if (x->draw == NULL) {
		warnx("cannot create Xft draw");
		XDestroyWindow(x->dpy, x->win);
		x->win = 0;
		return -1;
	}

	XClearWindow(x->dpy, x->win);
	XMapRaised(x->dpy, x->win);
	x->w = w;
	x->h = h;
	return 0;
}

/*
 * Open the X connection, allocate the palette, load the font (before
 * the unveil lock) and create the bar window.
 */
static int
xstate_open(struct xstate *x, const struct conf *c)
{
	unsigned int	i;

	memset(x, 0, sizeof(*x));
	x->dpy = XOpenDisplay(NULL);
	if (x->dpy == NULL) {
		warnx("cannot open display");
		return -1;
	}
	XSetErrorHandler(xerror_handler);
	XSetIOErrorHandler(xio_handler);
	x->screen = DefaultScreen(x->dpy);
	x->visual = DefaultVisual(x->dpy, x->screen);
	x->cmap = DefaultColormap(x->dpy, x->screen);

	for (i = 0; i < COLOR_NITEMS; i++) {
		if (XftColorAllocName(x->dpy, x->visual, x->cmap,
		    c->colors[i], &x->colors[i])) {
			x->ncolors++;
			continue;
		}
		warnx("cannot allocate color '%s'; falling back to '%s'",
		    c->colors[i], default_colors[i]);
		if (!XftColorAllocName(x->dpy, x->visual, x->cmap,
		    default_colors[i], &x->colors[i])) {
			warnx("cannot allocate default color");
			goto fail;
		}
		x->ncolors++;
	}

	x->font = XftFontOpenName(x->dpy, x->screen, c->fontname);
	if (x->font == NULL) {
		warnx("cannot open font '%s'; falling back to '%s'",
		    c->fontname, default_font);
		x->font = XftFontOpenName(x->dpy, x->screen, default_font);
	}
	if (x->font == NULL) {
		warnx("no usable font");
		goto fail;
	}

	if (window_create(x, c) == -1)
		goto fail;
	return 0;
fail:
	xstate_close(x);
	return -1;
}

static void
xstate_close(struct xstate *x)
{
	unsigned int	i;

	if (x->dpy == NULL)
		return;
	if (x->draw != NULL) {
		XftDrawDestroy(x->draw);
		x->draw = NULL;
	}
	if (x->font != NULL) {
		XftFontClose(x->dpy, x->font);
		x->font = NULL;
	}
	for (i = 0; i < x->ncolors; i++)
		XftColorFree(x->dpy, x->visual, x->cmap, &x->colors[i]);
	x->ncolors = 0;
	if (x->win != 0) {
		XDestroyWindow(x->dpy, x->win);
		x->win = 0;
	}
	XCloseDisplay(x->dpy);
	x->dpy = NULL;
}

/*
 * Render the cached bar line.  The logo and each widget are drawn as
 * separate chunks so that urgent segments (e.g. a critically low
 * battery) can use the urgent colour; the whole line stays centred as
 * in the original design.
 */
static void
draw_line(struct openbar *app, struct xstate *x)
{
	struct chunk {
		const char	*text;
		bool		 urgent;
	};
	struct chunk	chunks[2 * WIDGET_NITEMS + 2];
	char		texts[2 * WIDGET_NITEMS + 2][WITEM_TEXT_MAX + 3];
	XGlyphInfo	ext[2 * WIDGET_NITEMS + 2];
	unsigned int	i;
	int		nchunks = 0, total = 0, cx, cy;

	if (app->conf.logo != NULL && app->conf.logo[0] != '\0') {
		chunks[nchunks].text = app->conf.logo;
		chunks[nchunks].urgent = false;
		nchunks++;
		snprintf(texts[nchunks], sizeof(texts[nchunks]), " |");
		chunks[nchunks].text = texts[nchunks];
		chunks[nchunks].urgent = false;
		nchunks++;
	}
	for (i = 0; i < WIDGET_NITEMS; i++) {
		if (!app->conf.enabled[i])
			continue;
		snprintf(texts[nchunks], sizeof(texts[nchunks]), " %s",
		    app->seg[i].text);
		chunks[nchunks].text = texts[nchunks];
		chunks[nchunks].urgent = app->seg[i].urgent;
		nchunks++;
		snprintf(texts[nchunks], sizeof(texts[nchunks]), " |");
		chunks[nchunks].text = texts[nchunks];
		chunks[nchunks].urgent = false;
		nchunks++;
	}

	for (i = 0; i < (unsigned int)nchunks; i++) {
		XftTextExtentsUtf8(x->dpy, x->font,
		    (const FcChar8 *)chunks[i].text,
		    (int)strlen(chunks[i].text), &ext[i]);
		total += ext[i].xOff;
	}
	cx = (x->w - total) / 2;
	if (cx < 0)
		cx = 0;
	cy = (x->h + x->font->ascent - x->font->descent) / 2;
	for (i = 0; i < (unsigned int)nchunks; i++) {
		const XftColor	*color = chunks[i].urgent ?
		    &x->colors[COLOR_URGENT] : &x->colors[COLOR_FG];

		XftDrawStringUtf8(x->draw, color, x->font, cx, cy,
		    (const FcChar8 *)chunks[i].text,
		    (int)strlen(chunks[i].text));
		cx += ext[i].xOff;
	}
}

/* Repaint from cached state: cheap enough to run on every expose. */
static void
redraw(struct openbar *app, struct xstate *x)
{
	XftDrawRect(x->draw, &x->colors[COLOR_BG], 0, 0,
	    (unsigned int)x->w, (unsigned int)x->h);
	draw_line(app, x);
	XFlush(x->dpy);
	app->dirty = false;
}

static void
handle_x_events(struct openbar *app, struct xstate *x)
{
	XEvent	ev;

	while (XPending(x->dpy) > 0) {
		XNextEvent(x->dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0)
			redraw(app, x);
	}
}

static int
timespec_cmp(const struct timespec *a, const struct timespec *b)
{
	if (a->tv_sec != b->tv_sec)
		return a->tv_sec < b->tv_sec ? -1 : 1;
	if (a->tv_nsec != b->tv_nsec)
		return a->tv_nsec < b->tv_nsec ? -1 : 1;
	return 0;
}

/* Set the next refresh deadline for one widget. */
static void
schedule(struct openbar *app, enum widget w, const struct timespec *now)
{
	app->due[w] = *now;
	app->due[w].tv_sec += widget_intervals[w];
	app->due[w].tv_nsec = 0;
	if (w == WIDGET_DATE) {
		/* align with the next minute boundary */
		app->due[w].tv_sec += 60 - (app->due[w].tv_sec % 60);
	}
}

/* Collect every widget whose deadline has passed, then recompose. */
static void
collect_due(struct openbar *app)
{
	struct timespec	now;
	unsigned int	i;
	int		changed = 0;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (i = 0; i < WIDGET_NITEMS; i++) {
		if (!app->conf.enabled[i])
			continue;
		if (timespec_cmp(&app->due[i], &now) > 0)
			continue;
		collect_widget(app, (enum widget)i);
		schedule(app, (enum widget)i, &now);
		changed = 1;
	}
	if (changed)
		compose_bar(app);
}

/*
 * Milliseconds until the next widget deadline or IPC event, capped at
 * one second so that poll(2) stays responsive to X11 events.
 */
static int
next_timeout_ms(struct openbar *app)
{
	struct timespec	now;
	long long	ms = 1000, delta;
	unsigned int	i;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (i = 0; i < WIDGET_NITEMS; i++) {
		if (!app->conf.enabled[i])
			continue;
		delta = (long long)(app->due[i].tv_sec - now.tv_sec) * 1000 +
		    (app->due[i].tv_nsec - now.tv_nsec) / 1000000;
		if (delta < ms)
			ms = delta;
	}
	if (app->ipc_state == IPC_FETCHING) {
		delta = (long long)(app->ipc_deadline.tv_sec -
		    now.tv_sec) * 1000 +
		    (app->ipc_deadline.tv_nsec - now.tv_nsec) / 1000000;
		if (delta < ms)
			ms = delta;
	}
	if (ms < 0)
		ms = 0;
	return (int)ms;
}

static void
reap_children(struct openbar *app)
{
	pid_t	r;
	int	status;

	if (!chld_flag)
		return;
	chld_flag = 0;
	while ((r = waitpid(-1, &status, WNOHANG)) > 0) {
		if (app->ipc_pid > 0 && r == app->ipc_pid) {
			app->ipc_pid = -1;
			if (app->ipc_state != IPC_BROKEN)
				worker_lost(app);
		}
	}
}

/*
 * The worker failed and cannot be restarted after pledge(2) (fork
 * requires 'proc', deliberately not retained): drop the public address
 * functionality and keep the bar running.
 */
static void
worker_lost(struct openbar *app)
{
	if (app->ipc_fd != -1) {
		close(app->ipc_fd);
		app->ipc_fd = -1;
	}
	if (app->ipc_pid > 0)
		kill(app->ipc_pid, SIGTERM);
	app->ipc_state = IPC_BROKEN;
	strlcpy(app->pub_ip4, "N/A", sizeof(app->pub_ip4));
	strlcpy(app->pub_ip6, "N/A", sizeof(app->pub_ip6));
	compose_bar(app);
}

/* Fetch state machine: rate-limit requests and enforce the deadline. */
static void
ipc_tick(struct openbar *app)
{
	struct timespec	now;

	if (app->ipc_fd == -1 || app->ipc_state == IPC_BROKEN)
		return;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (app->ipc_state == IPC_IDLE) {
		if (timespec_cmp(&app->fetch_due, &now) > 0)
			return;
		if (ipc_send_fetch(app->ipc_fd) == -1) {
			worker_lost(app);
			return;
		}
		app->ipc_state = IPC_FETCHING;
		app->ipc_rlen = 0;
		app->ipc_deadline = now;
		app->ipc_deadline.tv_sec += FETCH_TIMEOUT;
	} else if (timespec_cmp(&app->ipc_deadline, &now) <= 0) {
		worker_lost(app);	/* stalled worker */
	}
}

/*
 * Read the response without blocking: the display process must stay
 * responsive even when the worker stalls.  EOF or a hard error kills
 * the worker connection.
 */
static void
ipc_read(struct openbar *app)
{
	struct timespec	now;
	ssize_t		n;

	n = recv(app->ipc_fd, app->ipc_rbuf + app->ipc_rlen,
	    sizeof(app->ipc_rbuf) - app->ipc_rlen, MSG_DONTWAIT);
	if (n > 0) {
		app->ipc_rlen += (size_t)n;
		if (app->ipc_rlen < sizeof(app->ipc_rbuf))
			return;
		apply_response(app);
		app->ipc_state = IPC_IDLE;
		app->ipc_rlen = 0;
		clock_gettime(CLOCK_MONOTONIC, &now);
		app->fetch_due = now;
		app->fetch_due.tv_sec += FETCH_INTERVAL;
		return;
	}
	if (n == 0 ||
	    (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK))
		worker_lost(app);
}

static void
apply_response(struct openbar *app)
{
	struct net_response	resp;

	if (ipc_decode(app->ipc_rbuf, sizeof(app->ipc_rbuf), &resp) == -1) {
		strlcpy(app->pub_ip4, "N/A", sizeof(app->pub_ip4));
		strlcpy(app->pub_ip6, "N/A", sizeof(app->pub_ip6));
	} else {
		if (resp.status_v4 == NET_OK)
			strlcpy(app->pub_ip4, resp.addr_v4,
			    sizeof(app->pub_ip4));
		else
			strlcpy(app->pub_ip4, "N/A", sizeof(app->pub_ip4));
		if (resp.status_v6 == NET_OK)
			strlcpy(app->pub_ip6, resp.addr_v6,
			    sizeof(app->pub_ip6));
		else
			strlcpy(app->pub_ip6, "N/A", sizeof(app->pub_ip6));
	}
	compose_bar(app);
}

int
main(int argc, char *argv[])
{
	struct openbar	 app;
	struct xstate	 x;
	const char	*confpath = NULL;
	char		*resolved = NULL, *xauth = NULL;
	char		 pledgestr[128];
	struct pollfd	 pfds[2];
	int		 opt, run_once = 0, xfd;

	memset(&app, 0, sizeof(app));
	memset(&x, 0, sizeof(x));
	app.apm_fd = -1;
	app.ipc_fd = -1;
	app.ipc_pid = -1;
	app.bat_pct = -1;
	strlcpy(app.pub_ip4, "N/A", sizeof(app.pub_ip4));
	strlcpy(app.pub_ip6, "N/A", sizeof(app.pub_ip6));

	if (locale_init() == -1)
		errx(1, "cannot set the C locale");
	/* read the zoneinfo file now; later calls use the cached data */
	tzset();

	while ((opt = getopt(argc, argv, "1c:h")) != -1) {
		switch (opt) {
		case '1':
			run_once = 1;
			break;
		case 'c':
			confpath = optarg;
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 1;
		}
	}
	if (optind != argc) {
		warnx("unexpected argument: %s", argv[optind]);
		usage(stderr);
		return 1;
	}

	resolved = conf_resolve_path(confpath);
	if (conf_load(resolved, &app.conf) == -1)
		exit(EXIT_FAILURE);
	free(resolved);

	setup_signals();

	if (app.conf.enabled[WIDGET_NET] && net_worker_start(&app) == -1)
		err(1, "network worker");

	xauth = resolve_xauthority();

	if (xstate_open(&x, &app.conf) == -1)
		goto fail;

	if (app.conf.enabled[WIDGET_BAT] &&
	    (app.apm_fd = apm_open()) == -1)
		warn("cannot open /dev/apm; battery display unavailable");

	if (unveil_parent(&app.conf, xauth) == -1)
		goto fail;
	free(xauth);
	xauth = NULL;

	/* hw.cpuspeed is not pledge-readable; sample it once now */
	if (app.conf.enabled[WIDGET_CPU])
		collect_cpu_init(&app);

	if (app.apm_fd != -1) {
		/*
		 * APM_IOC_GETPOWER is rejected by pledge(2) under every
		 * promise set, so a battery-enabled bar keeps its
		 * filesystem lock but cannot pledge.
		 */
		warnx("battery display requires APM_IOC_GETPOWER; "
		    "display process remains unpledged");
	} else {
		build_pledge(pledgestr, sizeof(pledgestr), &app.conf);
		if (pledge(pledgestr, NULL) == -1)
			err(1, "pledge");
	}

	xfd = ConnectionNumber(x.dpy);

	while (!quit_flag) {
		int	nfds = 1, pr;

		reap_children(&app);
		collect_due(&app);
		if (app.dirty)
			redraw(&app, &x);

		pfds[0].fd = xfd;
		pfds[0].events = POLLIN;
		pfds[0].revents = 0;
		if (app.ipc_fd != -1 && app.ipc_state != IPC_BROKEN) {
			pfds[1].fd = app.ipc_fd;
			pfds[1].events = POLLIN;
			pfds[1].revents = 0;
			nfds = 2;
		}
		pr = poll(pfds, (nfds_t)nfds, next_timeout_ms(&app));
		if (pr == -1) {
			if (errno == EINTR)
				continue;
			err(1, "poll");
		}
		if (pfds[0].revents & (POLLHUP | POLLERR | POLLNVAL))
			errx(1, "connection to X server lost");
		if (pfds[0].revents & POLLIN)
			handle_x_events(&app, &x);
		if (nfds == 2) {
			if (pfds[1].revents & (POLLHUP | POLLERR | POLLNVAL))
				worker_lost(&app);
			else if (pfds[1].revents & POLLIN)
				ipc_read(&app);
		}
		ipc_tick(&app);

		if (run_once)
			break;
	}

	if (app.ipc_fd != -1)
		close(app.ipc_fd);
	if (app.apm_fd != -1)
		close(app.apm_fd);
	xstate_close(&x);
	conf_free(&app.conf);
	return 0;
fail:
	if (app.ipc_fd != -1)
		close(app.ipc_fd);
	free(xauth);
	if (x.dpy != NULL)
		xstate_close(&x);
	conf_free(&app.conf);
	return 1;
}
