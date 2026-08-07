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

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>

#include <stdint.h>
#include <sys/sensors.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <netinet/in.h>

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
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

#define INET_ADDRSTRLEN		16
#define INET6_ADDRSTRLEN	46
#define MAX_IP_LENGTH		64
#define MAX_LINE_LENGTH		256
#define MAX_OUTPUT_LENGTH	16
#define HOSTNAME_MAX_LENGTH	256

#define IPC_CMD_FETCH	0x01

struct net_response {
	uint8_t		status_v4;
	uint8_t		status_v6;
	char		addr_v4[MAX_IP_LENGTH];
	char		addr_v6[INET6_ADDRSTRLEN];
};

#define NET_OK		  0
#define NET_ERR_DNS	  1
#define NET_ERR_CONNECT	  2
#define NET_ERR_RECV	  3
#define NET_ERR_PARSE	  4
#define NET_ERR_SYS	  5

static char		battery_percent[32];
static char		cpu_temp[32];
static char		cpu_avg_speed[32];
static char		datetime[32];
static char		public_ip[MAX_IP_LENGTH];
static char		public_ipv6[INET6_ADDRSTRLEN];
static char		internal_ip[INET_ADDRSTRLEN];
static char		vpn_status[16];
double			system_load[3];
unsigned long long	free_memory;

enum color_slot {
	BAR_FG,
	BAR_BG,
	BAR_URGENT,
	COLOR_NITEMS
};

struct gap {
	int	 top;
	int	 bottom;
	int	 left;
	int	 right;
};

struct Config {
	char		*logo;
	char		*interface;
	char		*fontname;
	char		*color[COLOR_NITEMS];
	int		 show_hostname;
	int		 show_date;
	int		 show_cpu;
	int		 show_mem;
	int		 show_bat;
	int		 show_load;
	int		 show_net;
	int		 show_vpn;
	int		 barheight;
	struct gap	 gap;
};

static const char *color_defaults[] = {
	"#000000",		/* BAR_FG */
	"#CCCCCC",		/* BAR_BG */
	"#FC8814",		/* BAR_URGENT */
};

static void	*xmalloc(size_t);
static void	*xcalloc(size_t, size_t);

static void *
xcalloc(size_t no, size_t siz)
{
	void	*p;

	if (siz == 0 || no == 0)
		errx(1, "xcalloc: zero size");
	if (SIZE_MAX / no < siz)
		errx(1, "xcalloc: no * siz > SIZE_MAX");
	if ((p = calloc(no, siz)) == NULL)
		err(1, "calloc");
	return p;
}

static char	*xstrdup(const char *);
static void	 config_free(struct Config *);
static void	 config_setstr(char **, const char *);

static void *
xmalloc(size_t siz)
{
	void	*p;

	if (siz == 0)
		errx(1, "xmalloc: zero size");
	if ((p = malloc(siz)) == NULL)
		err(1, "malloc");
	return p;
}

static char *
xstrdup(const char *str)
{
	char	*p;

	if (str == NULL)
		errx(1, "xstrdup: NULL pointer");
	if ((p = strdup(str)) == NULL)
		err(1, "strdup");
	return p;
}

static void
config_setstr(char **dest, const char *value)
{
	free(*dest);
	*dest = xstrdup(value);
}

static void
config_free(struct Config *c)
{
	unsigned int	i;

	free(c->logo);
	c->logo = NULL;
	free(c->interface);
	c->interface = NULL;
	free(c->fontname);
	c->fontname = NULL;
	for (i = 0; i < COLOR_NITEMS; i++) {
		free(c->color[i]);
		c->color[i] = NULL;
	}
}

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
		if (r == 0)
			return -1;
		left -= (size_t)r;
		p += r;
	}
	return (ssize_t)n;
}

static char *
trim(char *value)
{
	char *end;

	while (*value == ' ' || *value == '\t')
		value++;
	end = value + strlen(value);
	while (end > value && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	return value;
}

static char *
resolve_config_path(const char *override_path)
{
	char		 buffer[PATH_MAX];
	const char	*home;
	int		 length;

	if (override_path != NULL)
		return xstrdup(override_path);

	home = getenv("HOME");
	if (home != NULL && home[0] != '\0') {
		length = snprintf(buffer, sizeof(buffer),
		    "%s/.openbarrc", home);
		if (length > 0 && (size_t)length < sizeof(buffer) &&
		    access(buffer, R_OK) == 0)
			return xstrdup(buffer);
	}

	return xstrdup("/etc/openbarrc");
}

struct Config
config_load(const char *config_file_path)
{
	struct Config	 config;
	FILE		*file = NULL;
	char		 line[MAX_LINE_LENGTH];
	unsigned int	 i;

	memset(&config, 0, sizeof(config));
	config.barheight = 24;
	config.gap.top = 0;
	config.gap.bottom = 0;
	config.gap.left = 0;
	config.gap.right = 0;

	config.fontname = xstrdup("sans-serif:pixelsize=14:bold");
	for (i = 0; i < COLOR_NITEMS; i++)
		config.color[i] = xstrdup(color_defaults[i]);

	file = fopen(config_file_path, "r");
	if (file == NULL) {
		warn("Unable to open config file at %s",
		    config_file_path);
		goto fail;
	}

	while (fgets(line, sizeof(line), file)) {
		char *keyword, *argument;

		if (strchr(line, '\n') == NULL && !feof(file)) {
			warnx("Configuration line is too long");
			goto fail;
		}
		line[strcspn(line, "\n")] = '\0';
		keyword = trim(line);
		if (*keyword == '\0' || *keyword == '#')
			continue;

		argument = strchr(keyword, ' ');
		if (argument != NULL) {
			*argument = '\0';
			argument = trim(argument + 1);
		}

		if (argument == NULL || *argument == '\0') {
			warnx("Missing argument for %s", keyword);
			continue;
		}

		if (argument[0] == '"' || argument[0] == '\'') {
			char  quote = argument[0];
			char *end = strrchr(argument + 1, quote);
			if (end != NULL) {
				argument++;
				*end = '\0';
			}
		}

		if (strcmp(keyword, "logo") == 0)
			config_setstr(&config.logo, argument);
		else if (strcmp(keyword, "interface") == 0)
			config_setstr(&config.interface, argument);
		else if (strcmp(keyword, "fontname") == 0)
			config_setstr(&config.fontname, argument);
		else if (strcmp(keyword, "barheight") == 0) {
			const char *errstr;
			int v = (int)strtonum(argument, 12, 60, &errstr);
			if (errstr != NULL) {
				warnx("Invalid barheight: %s", argument);
				goto fail;
			}
			config.barheight = v;
		} else if (strcmp(keyword, "gap") == 0) {
			const char *errstr;
			char *token, *saveptr;
			int gaps[4], n;
			n = 0;
			for (token = strtok_r(argument, " \t", &saveptr);
			    token != NULL && n < 4;
			    token = strtok_r(NULL, " \t", &saveptr)) {
				gaps[n] = (int)strtonum(token, 0, INT_MAX,
				    &errstr);
				if (errstr != NULL) {
					warnx("Invalid gap value");
					goto fail;
				}
				n++;
			}
			if (n == 4) {
				config.gap.top = gaps[0];
				config.gap.bottom = gaps[1];
				config.gap.left = gaps[2];
				config.gap.right = gaps[3];
			} else
				warnx("gap requires four values");
		} else if (strcmp(keyword, "color") == 0) {
			char *space, *slot, *val;

			space = strchr(argument, ' ');
			if (space == NULL)
				space = strchr(argument, '\t');
			if (space != NULL) {
				*space = '\0';
				slot = argument;
				val = trim(space + 1);
				if (strcmp(slot, "barfg") == 0)
					config_setstr(&config.color[BAR_FG],
					    val);
				else if (strcmp(slot, "barbg") == 0)
					config_setstr(&config.color[BAR_BG],
					    val);
				else if (strcmp(slot, "urgent") == 0)
					config_setstr(&config.color[BAR_URGENT],
					    val);
				else
					warnx("Unknown color slot: %s", slot);
			}
		} else if (strcmp(keyword, "show") == 0) {
			int *setting = NULL;
			if (strcmp(argument, "hostname") == 0)
				setting = &config.show_hostname;
			else if (strcmp(argument, "date") == 0)
				setting = &config.show_date;
			else if (strcmp(argument, "cpu") == 0)
				setting = &config.show_cpu;
			else if (strcmp(argument, "mem") == 0)
				setting = &config.show_mem;
			else if (strcmp(argument, "bat") == 0)
				setting = &config.show_bat;
			else if (strcmp(argument, "load") == 0)
				setting = &config.show_load;
			else if (strcmp(argument, "net") == 0)
				setting = &config.show_net;
			else if (strcmp(argument, "vpn") == 0)
				setting = &config.show_vpn;
			else {
				warnx("Unknown show keyword: %s", argument);
				continue;
			}
			*setting = 1;
		} else if (strcmp(keyword, "hide") == 0) {
			int *setting = NULL;
			if (strcmp(argument, "hostname") == 0)
				setting = &config.show_hostname;
			else if (strcmp(argument, "date") == 0)
				setting = &config.show_date;
			else if (strcmp(argument, "cpu") == 0)
				setting = &config.show_cpu;
			else if (strcmp(argument, "mem") == 0)
				setting = &config.show_mem;
			else if (strcmp(argument, "bat") == 0)
				setting = &config.show_bat;
			else if (strcmp(argument, "load") == 0)
				setting = &config.show_load;
			else if (strcmp(argument, "net") == 0)
				setting = &config.show_net;
			else if (strcmp(argument, "vpn") == 0)
				setting = &config.show_vpn;
			else {
				warnx("Unknown hide keyword: %s", argument);
				continue;
			}
			*setting = 0;
		} else
			warnx("Ignoring unknown configuration keyword: %s",
			    keyword);
	}

	fclose(file);
	return config;

fail:
	if (file != NULL)
		fclose(file);
	config_free(&config);
	exit(EXIT_FAILURE);
}

static int
set_socket_timeouts(int fd)
{
	struct timeval timeout = {10, 0};

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
	    sizeof(timeout)) == -1)
		return -1;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
	    sizeof(timeout)) == -1)
		return -1;
	return 0;
}

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
	if (set_socket_timeouts(sockfd) == -1) {
		close(sockfd);
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
			if (errno == EINTR)
				continue;
			close(sockfd);
			freeaddrinfo(res);
			return NET_ERR_SYS;
		}
		if (sent == 0) {
			close(sockfd);
			freeaddrinfo(res);
			return NET_ERR_SYS;
		}
		total_sent += sent;
	}

	ssize_t	bytes_received;
	size_t	total_bytes_received = 0;
	for (;;) {
		bytes_received = recv(sockfd, buffer + total_bytes_received,
		    sizeof(buffer) - 1 - total_bytes_received, 0);
		if (bytes_received > 0) {
			total_bytes_received += (size_t)bytes_received;
			continue;
		}
		if (bytes_received == 0)
			break;
		if (errno == EINTR)
			continue;
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
		close(sockfd);
		freeaddrinfo(res);
		return NET_ERR_PARSE;
	}

	close(sockfd);
	freeaddrinfo(res);
	return NET_OK;
}

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
	if (set_socket_timeouts(sockfd) == -1) {
		close(sockfd);
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
			if (errno == EINTR)
				continue;
			close(sockfd);
			freeaddrinfo(res);
			return NET_ERR_SYS;
		}
		if (sent == 0) {
			close(sockfd);
			freeaddrinfo(res);
			return NET_ERR_SYS;
		}
		total_sent += sent;
	}

	ssize_t	bytes_received;
	size_t	total_bytes_received = 0;
	for (;;) {
		bytes_received = recv(sockfd, buffer + total_bytes_received,
		    sizeof(buffer) - 1 - total_bytes_received, 0);
		if (bytes_received > 0) {
			total_bytes_received += (size_t)bytes_received;
			continue;
		}
		if (bytes_received == 0)
			break;
		if (errno == EINTR)
			continue;
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
		close(sockfd);
		freeaddrinfo(res);
		return NET_ERR_PARSE;
	}

	close(sockfd);
	freeaddrinfo(res);
	return NET_OK;
}

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
	hostname[sizeof(hostname) - 1] = '\0';

	return hostname;
}

void
update_internal_ip(const struct Config *config)
{
	struct ifaddrs		*ifap, *ifa;
	struct sockaddr_in	*sa;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	bool found_interface = false;
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (config->interface != NULL &&
		    strcmp(ifa->ifa_name, config->interface) == 0 &&
		    ifa->ifa_addr != NULL &&
		    ifa->ifa_addr->sa_family == AF_INET) {
			sa = (struct sockaddr_in *)ifa->ifa_addr;
			if (inet_ntop(AF_INET, &(sa->sin_addr), internal_ip,
			    sizeof(internal_ip)) != NULL) {
				found_interface = true;
				break;
			}
		}
	}

	if (!found_interface)
		strlcpy(internal_ip, "N/A", sizeof(internal_ip));

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
update_cpu_avg_speed(void)
{
	int		freq = 0;
	size_t		len = sizeof(freq);
	int		mib[2] = {CTL_HW, HW_CPUSPEED};

	if (sysctl(mib, 2, &freq, &len, NULL, 0) == -1) {
		strlcpy(cpu_avg_speed, "N/A", sizeof(cpu_avg_speed));
		return;
	}
	snprintf(cpu_avg_speed, sizeof(cpu_avg_speed), "%4dMHz", freq);
}

void
update_system_load(double *load_avg)
{
	double load[3];

	if (getloadavg(load, 3) != 3) {
		errx(EXIT_FAILURE, "getloadavg returned incomplete data");
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
		if (temp_mib == 20)
			temp_mib = -2;
	}

	if (temp_mib >= 0) {
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

	fd = open("/dev/apm", O_RDONLY);
	if (fd == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}
	if (ioctl(fd, APM_IOC_GETPOWER, &pi) == -1) {
		close(fd);
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}
	if (close(fd) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}

	if (pi.battery_life > 100) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}
	snprintf(battery_percent, sizeof(battery_percent), "%u%%",
	    (unsigned int)pi.battery_life);
}

void
update_datetime(void)
{
	time_t		 rawtime;
	struct tm	*timeinfo;

	if (time(&rawtime) == (time_t)-1) {
		strlcpy(datetime, "N/A", sizeof(datetime));
		return;
	}
	timeinfo = localtime(&rawtime);
	if (timeinfo == NULL ||
	    strftime(datetime, sizeof(datetime), "%a %d %b %H:%M",
	    timeinfo) == 0)
		strlcpy(datetime, "N/A", sizeof(datetime));
}

static XftColor *
xft_colors_alloc(Display *display, int screen, const struct Config *config)
{
	XftColor	*colors;
	unsigned int	 i;
	Visual		*visual;
	Colormap	 colormap;

	colors = xcalloc(COLOR_NITEMS, sizeof(XftColor));
	visual = DefaultVisual(display, screen);
	colormap = DefaultColormap(display, screen);

	for (i = 0; i < COLOR_NITEMS; i++) {
		if (!XftColorAllocName(display, visual, colormap,
		    config->color[i], &colors[i])) {
			warnx("Cannot allocate color: %s", config->color[i]);
			XftColorAllocName(display, visual, colormap,
			    color_defaults[i], &colors[i]);
		}
	}
	return colors;
}

static void
xft_colors_free(Display *display, int screen, XftColor *colors)
{
	unsigned int	 i;
	Visual		*visual;
	Colormap	 colormap;

	visual = DefaultVisual(display, screen);
	colormap = DefaultColormap(display, screen);
	for (i = 0; i < COLOR_NITEMS; i++)
		XftColorFree(display, visual, colormap, &colors[i]);
	free(colors);
}

static void
create_window(Display *display, Window *window, XftDraw **xftdraw, int screen,
    const struct Config *config, const XftColor *xftcolor)
{
	int		 screen_width = DisplayWidth(display, screen);
	int		 window_width = screen_width;
	int		 window_height = config->barheight;
	Colormap	 colormap;
	Visual		*visual;

	*window = XCreateSimpleWindow(display, RootWindow(display, screen),
	    config->gap.left, config->gap.top,
	    window_width - config->gap.left - config->gap.right,
	    window_height, 0,
	    BlackPixel(display, screen), WhitePixel(display, screen));

	XSelectInput(display, *window, ExposureMask | KeyPressMask);

	Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
	Atom wm_state_above = XInternAtom(display, "_NET_WM_STATE_ABOVE",
	    False);
	Atom wm_bypass_compositor = XInternAtom(display,
	    "_NET_WM_BYPASS_COMPOSITOR", False);
	Atom wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE",
	    False);
	Atom wm_window_type_dock = XInternAtom(display,
	    "_NET_WM_WINDOW_TYPE_DOCK", False);
	Atom wm_state_skip_taskbar =
	    XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
	Atom wm_state_skip_pager =
	    XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
	Atom wm_state_sticky =
	    XInternAtom(display, "_NET_WM_STATE_STICKY", False);

	Atom wm_state_atoms[] = {wm_state_above,
		wm_state_skip_taskbar, wm_state_skip_pager, wm_state_sticky};
	XChangeProperty(display, *window, wm_state, XA_ATOM, 32,
	    PropModeReplace, (unsigned char *)wm_state_atoms, 4);
	XChangeProperty(display, *window, wm_window_type, XA_ATOM, 32,
	    PropModeReplace, (unsigned char *)&wm_window_type_dock, 1);
	unsigned long bypass = 1;
	XChangeProperty(display, *window, wm_bypass_compositor, XA_CARDINAL, 32,
	    PropModeReplace, (unsigned char *)&bypass, 1);

	visual = DefaultVisual(display, screen);
	colormap = DefaultColormap(display, screen);

	XSetWindowBackground(display, *window, xftcolor[BAR_BG].pixel);

	*xftdraw = XftDrawCreate(display, *window, visual, colormap);
	if (*xftdraw == NULL) {
		fprintf(stderr, "Cannot create Xft draw\n");
		XDestroyWindow(display, *window);
		XCloseDisplay(display);
		exit(1);
	}

	XClearWindow(display, *window);
	XMapRaised(display, *window);
}

static void
draw_text(Display *display, Window win, XftDraw *xftdraw, XftFont *font,
    const XftColor *xftcolor, const char *text)
{
	XWindowAttributes	 window_attributes;

	XGetWindowAttributes(display, win, &window_attributes);
	int window_width = window_attributes.width;
	int window_height = window_attributes.height;

	XftDrawRect(xftdraw, &xftcolor[BAR_BG], 0, 0,
	    window_width, window_height);

	if (font == NULL || text == NULL)
		return;

	XGlyphInfo extents;
	XftTextExtentsUtf8(display, font, (const FcChar8 *)text,
	    (int)strlen(text), &extents);

	int x_position = (window_width - extents.xOff) / 2;
	int y_position = (window_height + font->ascent - font->descent) / 2;

	XftDrawStringUtf8(xftdraw, &xftcolor[BAR_FG], font,
	    x_position, y_position,
	    (const FcChar8 *)text, (int)strlen(text));
}

static int
validate_ip(const char *s, int family)
{
	struct in_addr	 address_v4;
	struct in6_addr	 address_v6;

	if (s == NULL || s[0] == '\0')
		return -1;
	if (family == AF_INET)
		return inet_pton(AF_INET, s, &address_v4) == 1 ? 0 : -1;
	if (family == AF_INET6)
		return inet_pton(AF_INET6, s, &address_v6) == 1 ? 0 : -1;
	return -1;
}

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

	if (resp.status_v4 == NET_OK &&
	    validate_ip(resp.addr_v4, AF_INET) == 0) {
		strlcpy(v4buf, resp.addr_v4, v4len);
	} else {
		strlcpy(v4buf, "N/A", v4len);
	}

	if (resp.status_v6 == NET_OK &&
	    validate_ip(resp.addr_v6, AF_INET6) == 0) {
		strlcpy(v6buf, resp.addr_v6, v6len);
	} else {
		strlcpy(v6buf, "N/A", v6len);
	}

	return 0;
}

static void
build_pledge(char *buf, size_t bufsz, const struct Config *cfg)
{
	snprintf(buf, bufsz, "stdio unix");
	if (cfg->show_mem)
		strlcat(buf, " vminfo", bufsz);
	if (cfg->show_vpn || cfg->show_net)
		strlcat(buf, " route", bufsz);
}

static char *
resolve_xauthority_path(void)
{
	char		 candidate[PATH_MAX], resolved[PATH_MAX];
	const char	*authority, *home;

	authority = getenv("XAUTHORITY");
	if (authority != NULL && authority[0] != '\0') {
		if (realpath(authority, resolved) != NULL)
			return xstrdup(resolved);
		return NULL;
	}
	home = getenv("HOME");
	if (home == NULL || home[0] == '\0' ||
	    snprintf(candidate, sizeof(candidate), "%s/.Xauthority", home) <= 0)
		return NULL;
	if (realpath(candidate, resolved) == NULL)
		return NULL;
	return xstrdup(resolved);
}

static int
unveil_parent(const char *authority_path, int show_bat)
{
	int has_apm = show_bat && access("/dev/apm", R_OK) == 0;

	if (unveil("/tmp/.X11-unix", "rw") == -1) {
		warn("unveil /tmp/.X11-unix");
		return -1;
	}
	if (authority_path != NULL && unveil(authority_path, "r") == -1) {
		warn("unveil %s", authority_path);
		return -1;
	}
	if (has_apm && unveil("/dev/apm", "r") == -1) {
		warn("unveil /dev/apm");
		return -1;
	}
	if (unveil(NULL, NULL) == -1) {
		warn("unveil lock");
		return -1;
	}
	return 0;
}

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
	if (unveil("/etc/protocols", "r") == -1) {
		warn("unveil /etc/protocols");
		return -1;
	}
	if (unveil(NULL, NULL) == -1) {
		warn("unveil lock");
		return -1;
	}
	return 0;
}

int
main(int argc, const char *argv[])
{
	Display		*display;
	Window		 window;
	struct Config	 config;
	XftDraw		*xftdraw;
	XftFont		*xftfont;
	XftColor	*xftcolor;
	int		 screen, opt, run_once = 0;
	const char	*config_override = NULL;
	char		*config_path, *authority_path;
	int		 sv[2] = {-1, -1};
	pid_t		 pid = -1;
	char		 steadystr[128];

	setlocale(LC_ALL, "");
	tzset();

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
	config = config_load(config_path);
	free(config_path);
	if (config.logo == NULL)
		errx(EXIT_FAILURE, "No logo configured");

	if (config.show_net) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
			err(EXIT_FAILURE, "socketpair");
		pid = fork();
		if (pid == -1)
			err(EXIT_FAILURE, "fork");
		if (pid == 0) {
			close(sv[1]);
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			if (unveil_child() == -1)
				_exit(1);
			if (pledge("stdio inet dns", NULL) == -1)
				err(EXIT_FAILURE, "pledge (network worker)");
			network_worker(sv[0]);
			_exit(0);
		}
		close(sv[0]);
		sv[0] = -1;
	}

	authority_path = resolve_xauthority_path();
	if (unveil_parent(authority_path, config.show_bat) == -1) {
		if (sv[1] != -1)
			close(sv[1]);
		free(authority_path);
		config_free(&config);
		return 1;
	}
	free(authority_path);

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		warnx("Cannot open display");
		if (sv[1] != -1)
			close(sv[1]);
		config_free(&config);
		return 1;
	}
	screen = DefaultScreen(display);

	xftcolor = xft_colors_alloc(display, screen, &config);

	xftfont = XftFontOpenName(display, screen, config.fontname);
	if (xftfont == NULL) {
		warnx("Cannot open font: %s", config.fontname);
		xftfont = XftFontOpenName(display, screen,
		    "sans-serif:pixelsize=14:bold");
		if (xftfont == NULL) {
			XCloseDisplay(display);
			xft_colors_free(display, screen, xftcolor);
			config_free(&config);
			errx(EXIT_FAILURE, "No font available");
		}
	}

	create_window(display, &window, &xftdraw, screen, &config, xftcolor);
	if (config.show_cpu)
		update_cpu_avg_speed();

	if (config.show_bat) {
		warnx("bar: parent pledge disabled; unveil remains active");
	} else {
		build_pledge(steadystr, sizeof(steadystr), &config);
		if (pledge(steadystr, NULL) == -1)
			err(EXIT_FAILURE, "pledge (display process)");
	}

	int ip_update_counter = 0;

	while (1) {
		char buffer[1024];
		buffer[0] = '\0';

		if (config.logo != NULL && strlen(config.logo) > 0) {
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), "%s",
			    config.logo);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
		}

		if (config.show_hostname) {
			char *hostname = get_hostname();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " %s",
			    hostname);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
		}

		if (config.show_date) {
			update_datetime();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " %s",
			    datetime);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
		}

		if (config.show_cpu) {
			update_cpu_temp();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer),
			    " CPU: %s (%s)", cpu_avg_speed, cpu_temp);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
		}

		if (config.show_mem) {
			free_memory = update_mem();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer),
			    " Mem: %llu MB", free_memory);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
		}

		if (config.show_load) {
			update_system_load(system_load);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer),
			    " Load: %.2f", system_load[0]);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
		}

		if (config.show_bat) {
			update_battery();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer),
			    " Bat: %s", battery_percent);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
		}

		if (config.show_vpn) {
			update_vpn();
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer),
			    " VPN: %s", vpn_status);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer), " |");
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
			update_internal_ip(&config);
			snprintf(buffer + strlen(buffer),
			    sizeof(buffer) - strlen(buffer),
			    " IPs: %s | %s ~ %s", public_ip,
			    public_ipv6, internal_ip);
		}

		draw_text(display, window, xftdraw, xftfont, xftcolor,
		    buffer);
		XFlush(display);

		ip_update_counter = (ip_update_counter + 1) % 10;

		fflush(stdout);
		if (run_once)
			break;
		usleep(2000000);
	}

	if (sv[1] != -1)
		close(sv[1]);
	XftDrawDestroy(xftdraw);
	XftFontClose(display, xftfont);
	xft_colors_free(display, screen, xftcolor);
	XDestroyWindow(display, window);
	XCloseDisplay(display);
	config_free(&config);
	return 0;
}
