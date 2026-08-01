/*
 * Copyright (c) 2024 Gonzalo Rodriguez <gonzalo@x61.sh>
 * Copyright (c) 2024-2026 David David Uhden Collado <david@uhden.dev>
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

#include <sys/ioctl.h>
#include <sys/sensors.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>

#include <net/if.h>
#include <netinet/in.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <arpa/inet.h>
#include <err.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <locale.h>
#include <machine/apmvar.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#define INET_ADDRSTRLEN		16
#define INET6_ADDRSTRLEN	46
#define MAX_IP_LENGTH		64
#define MAX_LINE_LENGTH		256
#define MAX_OUTPUT_LENGTH	16
#define HOSTNAME_MAX_LENGTH	256

/*
 * IPC protocol between the main (display) process and the network worker.
 *
 * The parent sends a one-byte command: IPC_CMD_FETCH.
 * The child responds with a fixed-size message: struct net_response.
 *
 * All fields use fixed-width types.  No raw pointers or native
 * structs with padding cross the boundary.
 */
#define IPC_CMD_FETCH	0x01

struct net_response {
	uint8_t		status_v4;	/* 0 = success */
	uint8_t		status_v6;
	char		addr_v4[MAX_IP_LENGTH];
	char		addr_v6[INET6_ADDRSTRLEN];
};

/*
 * Network‑worker error codes (sent in status_v4 or status_v6).
 * 0 means the corresponding address was fetched successfully.
 */
#define NET_OK		  0
#define NET_ERR_DNS	  1
#define NET_ERR_CONNECT	  2
#define NET_ERR_RECV	  3
#define NET_ERR_PARSE	  4
#define NET_ERR_SYS	  5

static char		battery_percent[32];
static char		cpu_temp[32];
static char		cpu_base_speed[32];
static char		cpu_avg_speed[32];
static char		datetime[32];
static char		public_ip[MAX_IP_LENGTH];
static char		public_ipv6[INET6_ADDRSTRLEN];
static char		internal_ip[INET_ADDRSTRLEN];
static char		vpn_status[16];
double			system_load[3];
unsigned long long	free_memory;

struct Config {
	char		*logo;
	char		*interface;
	char		*font;
	char		*foreground;
	char		*background;
	int		 show_hostname;
	int		 show_date;
	int		 show_cpu;
	int		 show_mem;
	int		 show_bat;
	int		 show_load;
	int		 show_net;
	int		 show_vpn;
};

static ssize_t
xread(int fd, void *buf, size_t n)
{
	size_t	 left = n;
	char	*p = buf;

	while (left > 0) {
		ssize_t r = read(fd, p, left);
		if (r == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			break;
		left -= (size_t)r;
		p += r;
	}
	return (ssize_t)(n - left);
}

static ssize_t
xwrite(int fd, const void *buf, size_t n)
{
	size_t		 left = n;
	const char	*p = buf;

	while (left > 0) {
		ssize_t r = write(fd, p, left);
		if (r == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		left -= (size_t)r;
		p += r;
	}
	return (ssize_t)n;
}

char *
extract_logo(const char *line)
{
	if (strstr(line, "logo=")) {
		const char *logo_start = strchr(line, '=') + 1;
		const char *logo_end = NULL;

		const char *cursor = logo_start;
		while (*cursor && *cursor != ' ' && *cursor != '\n')
			cursor++;
		logo_end = cursor;

		size_t logo_length = (size_t)(logo_end - logo_start);

		char *logo = malloc(logo_length + 1);
		if (logo == NULL) {
			perror("Failed to allocate memory for logo");
			exit(EXIT_FAILURE);
		}
		memcpy(logo, logo_start, logo_length);
		logo[logo_length] = '\0';
		return logo;
	}
	return NULL;
}

void
free_config(struct Config *config)
{
	free(config->logo);
	config->logo = NULL;
	free(config->interface);
	config->interface = NULL;
	free(config->font);
	config->font = NULL;
	free(config->foreground);
	config->foreground = NULL;
	free(config->background);
	config->background = NULL;
}

static char *
resolve_config_path(const char *override_path)
{
	char		 buffer[PATH_MAX];
	const char	*home;
	int		 length;

	if (override_path != NULL)
		return strdup(override_path);

	home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		length = snprintf(buffer, sizeof(buffer), "%s/.openbar.conf",
			 home);
		if (length > 0 && (size_t)length < sizeof(buffer) &&
		    access(buffer, R_OK) == 0)
			return strdup(buffer);
	}

	return strdup("/etc/openbar.conf");
}

struct Config
config_file(const char *config_file_path)
{
	struct Config config = {
		.logo		= NULL,
		.interface	= NULL,
		.font		= NULL,
		.foreground	= NULL,
		.background	= NULL,
		.show_hostname	= 0,
		.show_date	= 0,
		.show_cpu	= 0,
		.show_mem	= 0,
		.show_bat	= 0,
		.show_load	= 0,
		.show_net	= 0,
		.show_vpn	= 0,
	};

	config.font = strdup("fixed");
	config.foreground = strdup("black");
	config.background = strdup("white");
	if (config.font == NULL || config.foreground == NULL ||
	    config.background == NULL) {
		perror("Failed to allocate memory for defaults");
		exit(EXIT_FAILURE);
	}

	FILE *file = fopen(config_file_path, "r");
	if (file == NULL)
		err(EXIT_FAILURE, "Unable to open config file at %s",
		    config_file_path);

	char line[MAX_LINE_LENGTH];

	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\n")] = '\0';

		char *logo = extract_logo(line);
		if (logo != NULL) {
			free(config.logo);
			config.logo = logo;
			continue;
		}
		if (strstr(line, "interface=")) {
			const char *istart = strchr(line, '=') + 1;
			size_t ilen = strlen(istart);
			free(config.interface);
			config.interface = malloc(ilen + 1);
			if (config.interface == NULL)
				err(EXIT_FAILURE,
				    "Failed to allocate memory for interface");
			memcpy(config.interface, istart, ilen);
			config.interface[ilen] = '\0';
		}
		if (strstr(line, "date=yes"))
			config.show_date = 1;
		else if (strstr(line, "cpu=yes"))
			config.show_cpu = 1;
		else if (strstr(line, "load=yes"))
			config.show_load = 1;
		else if (strstr(line, "bat=yes"))
			config.show_bat = 1;
		else if (strstr(line, "net=yes"))
			config.show_net = 1;
		else if (strstr(line, "mem=yes"))
			config.show_mem = 1;
		else if (strstr(line, "hostname=yes"))
			config.show_hostname = 1;
		else if (strstr(line, "vpn=yes"))
			config.show_vpn = 1;
	}

	fclose(file);
	return config;
}

static void
set_config_string(char **dest, const char *value)
{
	char *dup;

	if (value == NULL || *value == '\0')
		return;

	dup = strdup(value);
	if (dup == NULL) {
		perror("Failed to allocate memory for Xresources");
		exit(EXIT_FAILURE);
	}
	free(*dest);
	*dest = dup;
}

static void
load_xresources(Display *display, struct Config *config)
{
	char		*resource_string;
	XrmDatabase	 db;
	XrmValue	 value;
	char		*type;

	XrmInitialize();
	resource_string = XResourceManagerString(display);
	if (resource_string == NULL)
		return;

	db = XrmGetStringDatabase(resource_string);
	if (db == NULL)
		return;

	if (XrmGetResource(db, "openbar.font", "Openbar.Font", &type, &value) ==
	    True)
		set_config_string(&config->font, value.addr);
	if (XrmGetResource(db, "openbar.foreground", "Openbar.Foreground",
	    &type, &value) == True)
		set_config_string(&config->foreground, value.addr);
	if (XrmGetResource(db, "openbar.background", "Openbar.Background",
	    &type, &value) == True)
		set_config_string(&config->background, value.addr);
	(void)type;

	XrmDestroyDatabase(db);
}

/*
 * update_public_ip – fetch the public IPv4 address from ifconfig.me.
 *
 * Returns NET_OK on success, or an error code on failure.
 * Never calls exit(); the caller handles the error.
 * Must only be called from the network worker which holds "inet dns".
 */
static int
update_public_ip(void)
{
	char		 buffer[1024];
	struct addrinfo	 hints, *res;
	int		 sockfd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo("ifconfig.me", "http", &hints, &res) != 0 ||
	    res == NULL)
		return NET_ERR_DNS;

	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd == -1) {
		freeaddrinfo(res);
		return NET_ERR_SYS;
	}

	if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
		close(sockfd);
		freeaddrinfo(res);
		return NET_ERR_CONNECT;
	}

	const char *request =
	    "GET /ip HTTP/1.1\r\nHost: "
	    "ifconfig.me\r\nConnection: close\r\n\r\n";
	ssize_t total_sent = 0;
	ssize_t request_len = (ssize_t)strlen(request);
	while (total_sent < request_len) {
		ssize_t sent = send(sockfd, request + total_sent,
		    request_len - total_sent, 0);
		if (sent == -1) {
			close(sockfd);
			freeaddrinfo(res);
			return NET_ERR_SYS;
		}
		total_sent += sent;
	}

	ssize_t	bytes_received;
	size_t	total_bytes_received = 0;
	while ((bytes_received = recv(sockfd,
	       buffer + total_bytes_received,
	       sizeof(buffer) - 1 - total_bytes_received, 0)) > 0)
		total_bytes_received += (size_t)bytes_received;
	if (bytes_received == -1) {
		close(sockfd);
		freeaddrinfo(res);
		return NET_ERR_RECV;
	}

	buffer[total_bytes_received] = '\0';
	char *ip_start = strstr(buffer, "\r\n\r\n");
	if (ip_start != NULL) {
		ip_start += 4;
		strncpy(public_ip, ip_start, MAX_IP_LENGTH - 1);
		public_ip[MAX_IP_LENGTH - 1] = '\0';
		public_ip[strcspn(public_ip, "\r\n")] = '\0';
	} else {
		return NET_ERR_PARSE;
	}

	close(sockfd);
	freeaddrinfo(res);
	return NET_OK;
}

/*
 * update_public_ipv6 – fetch the public IPv6 address from ifconfig.me.
 *
 * Returns NET_OK on success, or an error code on failure.
 * Never calls exit(); the caller handles the error.
 * Must only be called from the network worker which holds "inet dns".
 */
static int
update_public_ipv6(void)
{
	char		 buffer[1024];
	struct addrinfo	 hints, *res;
	int		 sockfd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo("ifconfig.me", "http", &hints, &res) != 0 ||
	    res == NULL)
		return NET_ERR_DNS;

	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd == -1) {
		freeaddrinfo(res);
		return NET_ERR_SYS;
	}

	if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
		close(sockfd);
		freeaddrinfo(res);
		return NET_ERR_CONNECT;
	}

	const char *request =
	    "GET /ip HTTP/1.1\r\nHost: "
	    "ifconfig.me\r\nConnection: close\r\n\r\n";
	ssize_t total_sent = 0;
	ssize_t request_len = (ssize_t)strlen(request);
	while (total_sent < request_len) {
		ssize_t sent = send(sockfd, request + total_sent,
		    request_len - total_sent, 0);
		if (sent == -1) {
			close(sockfd);
			freeaddrinfo(res);
			return NET_ERR_SYS;
		}
		total_sent += sent;
	}

	ssize_t	bytes_received;
	size_t	total_bytes_received = 0;
	while ((bytes_received = recv(sockfd,
	       buffer + total_bytes_received,
	       sizeof(buffer) - 1 - total_bytes_received, 0)) > 0)
		total_bytes_received += (size_t)bytes_received;
	if (bytes_received == -1) {
		close(sockfd);
		freeaddrinfo(res);
		return NET_ERR_RECV;
	}

	buffer[total_bytes_received] = '\0';
	char *ip_start = strstr(buffer, "\r\n\r\n");
	if (ip_start != NULL) {
		ip_start += 4;
		strncpy(public_ipv6, ip_start, sizeof(public_ipv6) - 1);
		public_ipv6[sizeof(public_ipv6) - 1] = '\0';
		public_ipv6[strcspn(public_ipv6, "\r\n")] = '\0';
	} else {
		return NET_ERR_PARSE;
	}

	close(sockfd);
	freeaddrinfo(res);
	return NET_OK;
}

/*
 * network_worker – child‑process entry point.
 *
 * Receives a one‑byte IPC_CMD_FETCH command from the parent,
 * fetches both IPv4 and IPv6 public addresses, and sends a
 * fixed‑size net_response back.
 *
 * This process is sandboxed with pledge("stdio inet dns", NULL)
 * and an unveil locked to the DNS resolution files only.  It never
 * accesses the X11 display, sysctl, battery, filesystem beyond
 * DNS helpers, or any other parent resource.
 */
static void
network_worker(int fd)
{
	struct net_response	resp;
	uint8_t			cmd;
	ssize_t			n;

	signal(SIGPIPE, SIG_IGN);

	for (;;) {
		n = xread(fd, &cmd, sizeof(cmd));
		if (n != (ssize_t)sizeof(cmd))
			_exit(0);

		if (cmd != IPC_CMD_FETCH)
			continue;

		memset(&resp, 0, sizeof(resp));
		resp.status_v4 = (uint8_t)update_public_ip();
		if (resp.status_v4 == NET_OK) {
			strlcpy(resp.addr_v4, public_ip,
			    sizeof(resp.addr_v4));
		}
		resp.status_v6 = (uint8_t)update_public_ipv6();
		if (resp.status_v6 == NET_OK) {
			strlcpy(resp.addr_v6, public_ipv6,
			    sizeof(resp.addr_v6));
		}

		if (xwrite(fd, &resp, sizeof(resp)) !=
		    (ssize_t)sizeof(resp))
			_exit(1);
	}
}

char *
get_hostname(void)
{
	static char hostname[HOSTNAME_MAX_LENGTH];

	if (gethostname(hostname, HOSTNAME_MAX_LENGTH) == -1) {
		perror("gethostname");
		exit(EXIT_FAILURE);
	}

	return hostname;
}

void
update_internal_ip(struct Config config)
{
	struct ifaddrs		*ifap, *ifa;
	struct sockaddr_in	*sa;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	bool found_interface = false;
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (config.interface != NULL &&
		    strcmp(ifa->ifa_name, config.interface) == 0 &&
		    ifa->ifa_addr != NULL &&
		    ifa->ifa_addr->sa_family == AF_INET) {
			sa = (struct sockaddr_in *)ifa->ifa_addr;
			inet_ntop(AF_INET, &(sa->sin_addr), internal_ip,
			    sizeof(internal_ip));
			found_interface = true;
			break;
		}
	}

	if (!found_interface)
		strlcpy(internal_ip, "lo0", sizeof(internal_ip));

	freeifaddrs(ifap);
}

void
update_vpn(void)
{
	struct ifaddrs	*ifap, *ifa;
	int		 has_wg_interface = 0;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strncmp(ifa->ifa_name, "wg", 2) == 0 &&
		    ifa->ifa_flags & IFF_UP) {
			has_wg_interface = 1;
			break;
		}
	}

	freeifaddrs(ifap);

	if (has_wg_interface)
		snprintf(vpn_status, sizeof(vpn_status), "VPN");
	else
		snprintf(vpn_status, sizeof(vpn_status), "No VPN");
}

unsigned long long
update_mem(void)
{
	int	mib[2] = {CTL_VM, VM_UVMEXP};
	size_t	len;

	len = sizeof(struct uvmexp);

	struct uvmexp uvm_stats;

	if (sysctl(mib, 2, &uvm_stats, &len, NULL, 0) == -1) {
		perror("sysctl");
		exit(EXIT_FAILURE);
	}

	unsigned long long freemem = (unsigned long long)uvm_stats.free *
	    (unsigned long long)uvm_stats.pagesize / (1024 * 1024);

	return freemem;
}

void
update_cpu_base_speed(void)
{
	int	temp = 0;
	size_t	templen = sizeof(temp);
	int	mib[5] = {CTL_HW, HW_CPUSPEED};

	if (sysctl(mib, 2, &temp, &templen, NULL, 0) == -1)
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "error");
	else
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "%4dMhz",
		    temp);
}

void
update_cpu_avg_speed(void)
{
	uint64_t	freq = 0;
	size_t		len = sizeof(freq);
	int		mib[2] = {CTL_HW, HW_CPUSPEED};

	if (sysctl(mib, 2, &freq, &len, NULL, 0) == -1) {
		fprintf(stderr, "Error: Failed to get CPU average speed\n");
		return;
	}
	snprintf(cpu_avg_speed, sizeof(cpu_avg_speed), "%4lluMhz", freq);
}

void
update_system_load(double *load_avg)
{
	double load[3];

	if (getloadavg(load, 3) == -1) {
		perror("getloadavg");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < 3; i++)
		load_avg[i] = load[i];
}

void
update_cpu_temp(void)
{
	struct sensor	sensor;
	size_t		templen = sizeof(sensor);
	int		temp = -1;

	static int temp_mib = -1;

	if (temp_mib == -1) {
		for (temp_mib = 0; temp_mib < 20; temp_mib++) {
			int mib[5] = {CTL_HW, HW_SENSORS, temp_mib,
				SENSOR_TEMP, 0};
			if (sysctl(mib, 5, &sensor, &templen, NULL, 0) != -1)
				break;
		}
	}

	if (temp_mib != -1) {
		int mib[5] = {CTL_HW, HW_SENSORS, temp_mib, SENSOR_TEMP, 0};
		if (sysctl(mib, 5, &sensor, &templen, NULL, 0) != -1) {
			temp = (sensor.value - 273150000) / 1000000.0;
			snprintf(cpu_temp, sizeof(cpu_temp), "%d C", temp);
			return;
		}
	}
	snprintf(cpu_temp, sizeof(cpu_temp), "x");
}

void
update_battery(void)
{
	int			fd;
	struct apm_power_info	pi;

	if ((fd = open("/dev/apm", O_RDONLY)) == -1 ||
	    ioctl(fd, APM_IOC_GETPOWER, &pi) == -1 || close(fd) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}

	snprintf(battery_percent, sizeof(battery_percent), "%d%%",
	    pi.battery_life);
}

void
update_datetime(void)
{
	time_t		 rawtime;
	struct tm	*timeinfo;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(datetime, sizeof(datetime), "%a %d %b %H:%M", timeinfo);
}

void
create_window(Display *display, Window *window, GC *gc, int screen,
    const struct Config *config)
{
	int screen_width = DisplayWidth(display, screen);
	int window_width = screen_width;
	int window_height = 30;

	*window = XCreateSimpleWindow(display, RootWindow(display, screen), 0,
		  0, window_width, window_height, 1,
		  BlackPixel(display, screen), WhitePixel(display, screen));

	XSelectInput(display, *window, ExposureMask | KeyPressMask);
	XMapWindow(display, *window);

	Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
	Atom wm_state_above = XInternAtom(display, "_NET_WM_STATE_ABOVE",
	    False);
	Atom wm_bypass_wm = XInternAtom(display,
	    "_NET_WM_BYPASS_COMPOSITOR", False);
	Atom wm_state_skip_taskbar =
	    XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
	Atom wm_state_skip_pager =
	    XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
	Atom wm_state_sticky =
	    XInternAtom(display, "_NET_WM_STATE_STICKY", False);
	XMoveWindow(display, *window, 0, 0);

	Atom wm_state_atoms[] = {wm_state_above, wm_bypass_wm,
		wm_state_skip_taskbar, wm_state_skip_pager, wm_state_sticky};
	XChangeProperty(display, *window, wm_state, XA_ATOM, 32,
	    PropModeReplace, (unsigned char *)wm_state_atoms, 5);

	*gc = XCreateGC(display, *window, 0, NULL);
	if (*gc == NULL) {
		fprintf(stderr, "Cannot create graphics context\n");
		exit(1);
	}

	XFontStruct *font_info = XLoadQueryFont(
	    display, config->font != NULL ? config->font : "fixed");
	if (!font_info)
		font_info = XLoadQueryFont(display, "fixed");
	if (!font_info) {
		fprintf(stderr, "Error: Failed to load font\n");
		XFreeGC(display, *gc);
		XCloseDisplay(display);
		exit(1);
	}
	XSetFont(display, *gc, font_info->fid);

	Colormap	colormap = DefaultColormap(display, screen);
	XColor		fg, bg;
	unsigned long	fg_pixel = BlackPixel(display, screen);
	unsigned long	bg_pixel = WhitePixel(display, screen);

	if (config->foreground != NULL &&
	    XAllocNamedColor(
	    display, colormap, config->foreground, &fg, &fg))
		fg_pixel = fg.pixel;
	if (config->background != NULL &&
	    XAllocNamedColor(
	    display, colormap, config->background, &bg, &bg))
		bg_pixel = bg.pixel;

	XSetForeground(display, *gc, fg_pixel);
	XSetBackground(display, *gc, bg_pixel);
	XSetWindowBackground(display, *window, bg_pixel);
	XClearWindow(display, *window);
	XMapRaised(display, *window);
}

void
draw_text(Display *display, Window window, GC gc, const char *text)
{
	XClearWindow(display, window);

	XWindowAttributes window_attributes;
	XGetWindowAttributes(display, window, &window_attributes);
	int window_width = window_attributes.width;

	XFontStruct *font_info = XQueryFont(display, XGContextFromGC(gc));
	if (font_info == NULL) {
		fprintf(stderr, "Error: Failed to query font information\n");
		return;
	}
	int text_width = XTextWidth(font_info, text, (int)strlen(text));

	int x_position = (window_width - text_width) / 2;
	int y_position = 20;

	XDrawString(
	    display, window, gc, x_position, y_position, text,
	    (int)strlen(text));
	XFlush(display);
}

/*
 * net_fetch – request IP data from the network worker.
 *
 * Sends IPC_CMD_FETCH, reads the response, validates the result,
 * and copies valid addresses into the caller's buffers.
 *
 * Returns 0 on success (at least one address available),
 * -1 if the worker is dead or the protocol is violated.
 */
static int
net_fetch(int fd, char *v4buf, size_t v4len, char *v6buf, size_t v6len)
{
	struct net_response	resp;
	uint8_t			cmd = IPC_CMD_FETCH;
	ssize_t			n;

	if (fd < 0)
		return -1;

	if (xwrite(fd, &cmd, sizeof(cmd)) != (ssize_t)sizeof(cmd))
		return -1;

	n = xread(fd, &resp, sizeof(resp));
	if (n != (ssize_t)sizeof(resp))
		return -1;

	resp.addr_v4[sizeof(resp.addr_v4) - 1] = '\0';
	resp.addr_v6[sizeof(resp.addr_v6) - 1] = '\0';

	if (resp.status_v4 == NET_OK && validate_ip(resp.addr_v4) == 0) {
		strlcpy(v4buf, resp.addr_v4, v4len);
	} else {
		strlcpy(v4buf, "N/A", v4len);
	}

	if (resp.status_v6 == NET_OK && validate_ip(resp.addr_v6) == 0) {
		strlcpy(v6buf, resp.addr_v6, v6len);
	} else {
		strlcpy(v6buf, "N/A", v6len);
	}

	return 0;
}

/*
 * build_pledge – construct the minimal steady‑state pledge string.
 *
 * Always included:  "stdio unix".
 * Conditionally:    "sysctl"  – CPU or memory sensors
 *                   "rpath ioctl" – battery status
 *                   "route"   – internal IP and VPN
 *
 * The buffer must be at least 128 bytes.
 * Does NOT include "inet dns proc" – those are never needed
 * by the parent after initialization.
 */
static void
build_pledge(char *buf, size_t bufsz, const struct Config *cfg)
{
	snprintf(buf, bufsz, "stdio unix");
	if (cfg->show_cpu || cfg->show_mem)
		strlcat(buf, " sysctl", bufsz);
	if (cfg->show_bat)
		strlcat(buf, " rpath ioctl", bufsz);
	if (cfg->show_vpn || cfg->show_net)
		strlcat(buf, " route", bufsz);
}

/*
 * unveil_parent – set up the main‑process filesystem view and lock it.
 *
 * Unveiled paths:
 *   config_path     r  – configuration file
 *   /tmp/.X11-unix   rw – X11 display socket
 *   /dev/apm         r  – battery status (only if the node exists)
 *
 * The parent does NOT unveil /etc/hosts, /etc/resolv.conf or
 * /etc/services because DNS resolution is handled by the child.
 */
static int
unveil_parent(const char *config_path)
{
	if (unveil(config_path, "r") == -1) {
		warn("unveil %s", config_path);
		return -1;
	}
	if (unveil("/tmp/.X11-unix", "rw") == -1) {
		warn("unveil /tmp/.X11-unix");
		return -1;
	}
	if (access("/dev/apm", R_OK) == 0 &&
	    unveil("/dev/apm", "r") == -1) {
		warn("unveil /dev/apm");
		return -1;
	}
	if (unveil(NULL, NULL) == -1) {
		warn("unveil lock");
		return -1;
	}
	return 0;
}

/*
 * unveil_child – set up the network‑worker filesystem view and lock it.
 *
 * Only DNS resolution helper files are unveiled.  The child has
 * no access to the configuration, X11 socket, battery device,
 * or any other parent resource.
 */
static int
unveil_child(void)
{
	if (unveil("/etc/hosts", "r") == -1) {
		warn("unveil /etc/hosts");
		return -1;
	}
	if (unveil("/etc/resolv.conf", "r") == -1) {
		warn("unveil /etc/resolv.conf");
		return -1;
	}
	if (unveil("/etc/services", "r") == -1) {
		warn("unveil /etc/services");
		return -1;
	}
	if (unveil(NULL, NULL) == -1) {
		warn("unveil lock");
		return -1;
	}
	return 0;
}

/*
 * validate_ip – basic sanity check for received IP strings.
 *
 * Ensures the string is NUL‑terminated, not empty, and contains
 * only characters valid in IPv4 / IPv6 textual representation.
 * Returns 0 if acceptable, -1 otherwise.
 */
static int
validate_ip(const char *s)
{
	size_t i;

	if (s == NULL || s[0] == '\0')
		return -1;
	for (i = 0; s[i] != '\0'; i++) {
		if (i >= MAX_IP_LENGTH)
			return -1;
		if (s[i] != '.' && s[i] != ':' &&
		    (s[i] < '0' || s[i] > '9') &&
		    (s[i] < 'a' || s[i] > 'f') &&
		    (s[i] < 'A' || s[i] > 'F'))
			return -1;
	}
	return 0;
}

int
main(int argc, const char *argv[])
{
	setlocale(LC_CTYPE, "C");
	setlocale(LC_ALL, "en_US.UTF-8");

	Display		*display;
	Window		 window;
	GC		 gc;
	int		 screen;
	int		 opt;
	int		 run_once = 0;
	const char	*config_override = NULL;
	char		*config_path;
	int		 sv[2];
	pid_t		 pid;
	char		 steadystr[128];

	while ((opt = getopt(argc, (char *const *)argv, "1c:")) != -1) {
		switch (opt) {
		case '1':
			run_once = 1;
			break;
		case 'c':
			config_override = optarg;
			break;
		default:
			fprintf(stderr, "Usage: openbar [-1] [-c path]\n");
			return 1;
		}
	}

	config_path = resolve_config_path(config_override);
	if (config_path == NULL)
		errx(EXIT_FAILURE, "Failed to resolve config path");

	/*
	 * Create the IPC channel before forking, so both parent
	 * and child inherit the socketpair.  SOCK_STREAM avoids
	 * datagram truncation and provides reliable EOF detection.
	 */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
		err(EXIT_FAILURE, "socketpair");

	pid = fork();
	if (pid == -1)
		err(EXIT_FAILURE, "fork");

	if (pid == 0) {
		/*
		 * CHILD – network worker.
		 *
		 * Inherited but immediately closed:
		 *   sv[1] – parent's socketpair endpoint
		 *   stdin, stdout – not used
		 *
		 * Retained:
		 *   stderr   – for error reporting
		 *   sv[0]    – IPC channel to parent
		 */
		close(sv[1]);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);

		if (unveil_child() == -1)
			_exit(1);

		if (pledge("stdio inet dns", NULL) == -1)
			err(EXIT_FAILURE, "pledge (child)");
		/*
		 * Child promises (REQUIRES RUNTIME VERIFICATION):
		 *   stdio  – read/write on the IPC socketpair,
		 *            memory allocation, string formatting
		 *   inet   – TCP sockets for HTTP to ifconfig.me
		 *   dns    – getaddrinfo() for ifconfig.me
		 *
		 * Not retained:
		 *   rpath, wpath, cpath – no filesystem access
		 *                         beyond DNS helpers
		 *   unix  – no X11 or local sockets (AF_UNIX
		 *           socketpair inherited from parent)
		 *   sysctl, ioctl, route – no kernel queries
		 *   proc, exec – no child processes
		 *   id, getpw – no user database access
		 */

		network_worker(sv[0]);
		_exit(0);
	}

	/*
	 * PARENT – from here onward.
	 *
	 * sv[1] is the parent's IPC endpoint to the child.
	 * sv[0] is unused in the parent.
	 */
	close(sv[0]);

	if (unveil_parent(config_path) == -1) {
		close(sv[1]);
		free(config_path);
		return 1;
	}

	/*
	 * Initial pledge: everything needed for startup
	 * (config reading, X11 display open, window creation).
	 *
	 * Promises retained during init:
	 *   stdio  – basic I/O, memory allocation
	 *   rpath  – fopen(config_path), access(/dev/apm)
	 *   unix   – XOpenDisplay, X11 protocol
	 *
	 * Promises NOT included in init pledge:
	 *   inet, dns  – network delegated to child
	 *   sysctl      – only needed in steady state
	 *   ioctl       – only needed in steady state
	 *   route       – only needed in steady state
	 *   proc        – fork already completed
	 *   exec, id, getpw – never needed
	 */
	if (pledge("stdio rpath unix", NULL) == -1) {
		warn("pledge (init)");
		close(sv[1]);
		free(config_path);
		return 1;
	}
	/*
	 * Init pledge (REQUIRES RUNTIME VERIFICATION):
	 *   "stdio rpath unix"
	 */

	struct Config config = config_file(config_path);
	free(config_path);

	if (config.logo == NULL) {
		warnx("No logo configured");
		close(sv[1]);
		return 1;
	}

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		warnx("Cannot open display");
		close(sv[1]);
		return 1;
	}
	screen = DefaultScreen(display);

	load_xresources(display, &config);
	create_window(display, &window, &gc, screen, &config);

	/*
	 * Reduce to the minimal steady‑state pledge set derived
	 * from the configuration.  This drops rpath unless
	 * show_bat is enabled (access to /dev/apm).  inet and
	 * dns are already absent; network is handled by the child.
	 */
	build_pledge(steadystr, sizeof(steadystr), &config);
	if (pledge(steadystr, NULL) == -1)
		err(EXIT_FAILURE, "pledge (steady)");
	/*
	 * Steady‑state pledge (REQUIRES RUNTIME VERIFICATION):
	 *   Always:    "stdio unix"
	 *   +sysctl    if show_cpu or show_mem
	 *   +rpath ioctl if show_bat
	 *   +route     if show_vpn or show_net
	 */

	printf("\e[?25l");

	int ip_update_counter = 0;

	while (1) {
		char buffer[1024];
		buffer[0] = '\0';

		if (config.logo != NULL && strlen(config.logo) > 0) {
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "%s", config.logo);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_hostname) {
			char *hostname = get_hostname();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " %s ", hostname);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_date) {
			update_datetime();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " %s ", datetime);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_cpu) {
			update_cpu_temp();
			update_cpu_avg_speed();
			update_cpu_base_speed();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " CPU: %s (%s) ",
			    cpu_avg_speed, cpu_temp);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_mem) {
			free_memory = update_mem();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " Mem: %.0llu MB ",
			    free_memory);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_load) {
			update_system_load(system_load);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " Load: %.2f ",
			    system_load[0]);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_bat) {
			update_battery();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " Bat: %s ",
			    battery_percent);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_vpn) {
			update_vpn();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " VPN: %s ",
			    vpn_status);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "|");
		}

		if (config.show_net) {
			if (ip_update_counter == 0) {
				if (net_fetch(sv[1], public_ip,
				    sizeof(public_ip), public_ipv6,
				    sizeof(public_ipv6)) == -1) {
					strlcpy(public_ip, "N/A",
					    sizeof(public_ip));
					strlcpy(public_ipv6, "N/A",
					    sizeof(public_ipv6));
				}
			}
			update_internal_ip(config);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer),
			    " IPs: %s | %s ~ %s ", public_ip,
			    public_ipv6, internal_ip);
		}

		draw_text(display, window, gc, buffer);
		XFlush(display);

		ip_update_counter = (ip_update_counter + 1) % 10;

		fflush(stdout);
		if (run_once)
			break;
		usleep(2000000);
	}

	close(sv[1]);
	free_config(&config);
	XCloseDisplay(display);
	return 0;
}
