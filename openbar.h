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
 * openbar: an OpenBSD-only status bar for cwm(1) and other X11 window
 * managers.
 *
 * This header contains only ISO C declarations so that the portable
 * translation units (config.c, fmt.c, ipc.c) can be compiled and
 * tested on any C17 host.  OpenBSD- and X11-specific details live in
 * widgets.c, net.c and openbar.c.
 */

#ifndef OPENBAR_H
#define OPENBAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>		/* pid_t */
#include <time.h>		/* time_t, struct timespec */

#define BAR_TEXT_MAX	1024	/* whole bar line, incl. logo and widgets */
#define WITEM_TEXT_MAX	160	/* one widget segment */
#define LOGO_MAX	127	/* logo length limit */
#define IFNAME_MAX	15	/* IFNAMSIZ - 1, validated at parse time */
#define HOSTNAME_MAX	127	/* display bound for the host name */

#define ADDR4_STRLEN	16	/* INET_ADDRSTRLEN */
#define ADDR6_STRLEN	46	/* INET6_ADDRSTRLEN */

enum color_slot {
	COLOR_FG,
	COLOR_BG,
	COLOR_URGENT,
	COLOR_NITEMS
};

enum widget {
	WIDGET_HOSTNAME,
	WIDGET_DATE,
	WIDGET_CPU,
	WIDGET_MEM,
	WIDGET_LOAD,
	WIDGET_BAT,
	WIDGET_VPN,
	WIDGET_NET,
	WIDGET_NITEMS
};

struct conf {
	char	*logo;
	char	*interface;
	char	*fontname;
	char	*colors[COLOR_NITEMS];
	int	 barheight;	/* pixels, 12..60 */
	int	 gap[4];	/* top, bottom, left, right */
	bool	 enabled[WIDGET_NITEMS];
};

/* One formatted widget segment, rendered with the urgent colour when
 * the widget carries a warning (e.g. critically low battery). */
struct witem {
	char	text[WITEM_TEXT_MAX];
	bool	urgent;
};

/*
 * Worker IPC protocol.
 *
 * The parent sends a single command byte.  The worker answers with a
 * fixed-size response containing a magic byte, per-family status codes
 * and NUL-terminated address strings.  Both peers run the same program
 * image (fork(2), no exec), so padding is irrelevant, but the layout
 * is still checked at compile time.
 */
#define IPC_CMD_FETCH	0x01
#define IPC_MAGIC	0x4f	/* 'O' */

enum net_status {
	NET_OK,
	NET_ERR_DNS,
	NET_ERR_CONNECT,
	NET_ERR_RECV,
	NET_ERR_PARSE,
	NET_ERR_SYS,
	NET_STATUS_NITEMS
};

struct net_response {
	uint8_t	magic;
	uint8_t	status_v4;
	uint8_t	status_v6;
	char	addr_v4[ADDR4_STRLEN];
	char	addr_v6[ADDR6_STRLEN];
};

enum ipc_state {
	IPC_IDLE,	/* waiting for the next fetch deadline */
	IPC_FETCHING,	/* request sent, awaiting the response */
	IPC_BROKEN	/* worker is gone; no more public addresses */
};

struct openbar {
	struct conf		conf;

	/* network worker */
	int			ipc_fd;
	pid_t			ipc_pid;
	enum ipc_state		ipc_state;
	unsigned char		ipc_rbuf[sizeof(struct net_response)];
	size_t			ipc_rlen;
	struct timespec		ipc_deadline;

	/* refresh scheduling, CLOCK_MONOTONIC based */
	struct timespec		due[WIDGET_NITEMS];
	struct timespec		fetch_due;

	/* collected metrics */
	char			hostname[HOSTNAME_MAX + 1];
	time_t			now;
	unsigned int		cpu_mhz;
	int			cpu_temp;	/* degrees Celsius */
	int			cpu_sensor;	/* 0 unknown, >0 dev+1, -1 none */
	bool			cpu_have_freq;
	bool			cpu_have_temp;
	bool			mem_valid;
	unsigned long long	mem_free_mb;
	bool			load_valid;
	double			load1;
	int			bat_pct;	/* -1 when unavailable */
	bool			vpn_up;
	char			pub_ip4[ADDR4_STRLEN];
	char			pub_ip6[ADDR6_STRLEN];
	char			int_ip4[ADDR4_STRLEN];

	/* rendering */
	struct witem		seg[WIDGET_NITEMS];
	char			bar_text[BAR_TEXT_MAX];
	bool			dirty;

	/* pre-opened devices */
	int			apm_fd;
};

/* config.c */
void			 conf_defaults(struct conf *);
void			 conf_free(struct conf *);
int			 conf_load(const char *, struct conf *);
char			*conf_resolve_path(const char *);
int			 widget_lookup(const char *);
extern const char *const default_colors[COLOR_NITEMS];
extern const char	 default_font[];

/* ipc.c */
ssize_t	 read_full(int, void *, size_t);
ssize_t	 write_full(int, const void *, size_t);
int	 valid_ip(const char *, int);
int	 ipc_send_fetch(int);
void	 ipc_encode(struct net_response *, int, const char *, int,
    const char *);
int	 ipc_decode(const unsigned char *, size_t, struct net_response *);

/* fmt.c */
int	 locale_init(void);
void	 fmt_widget(const struct openbar *, enum widget, struct witem *);
void	 compose_bar(struct openbar *);
void	 utf8_bounded_copy(char *, const char *, size_t);

/* widgets.c (OpenBSD) */
void	 collect_widget(struct openbar *, enum widget);
void	 collect_cpu_init(struct openbar *);
int	 apm_open(void);

/* net.c (OpenBSD network worker) */
void	 net_worker(int);
int	 net_worker_start(struct openbar *);

#endif /* OPENBAR_H */
