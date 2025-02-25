/*
 * Copyright (c) 2024 Gonzalo Rodriguez <gonzalo@x61.sh>
 * Copyright (c) 2024-2025 David Uhden Collado <david@uhden.dev>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <sys/sysctl.h>
#include <sys/sensors.h>
#include <machine/apmvar.h>

#include <wchar.h>
#include <time.h>
#include <locale.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#define INET_ADDRSTRLEN 16
#define MAX_IP_LENGTH 32
#define MAX_LINE_LENGTH 256
#define MAX_OUTPUT_LENGTH 16
#define HOSTNAME_MAX_LENGTH 256

// Define ANSI escape codes for color formatting
#define PURPLE      "\x1b[38;2;138;43;226m"
#define ORANGE      "\x1b[38;2;255;165;0m"
#define GREEN       "\x1b[38;2;0;128;0m"
#define YELLOW      "\x1b[38;2;255;255;0m"
#define RED         "\x1b[38;2;255;0;0m"
#define RESET       "\x1b[0m"

// Declare global variables
static char battery_percent[32];
static char cpu_temp[32];
static char cpu_base_speed[32];
static char cpu_avg_speed[32];
static char datetime[32];
static char public_ip[MAX_IP_LENGTH];
static char internal_ip[INET_ADDRSTRLEN];
char vpn_status[16];
char mem_info[32];
char window_id[MAX_OUTPUT_LENGTH];
double system_load[3];
unsigned long long free_memory;

// Define configuration structure
struct Config {
	char *logo;
	char *interface;
	int show_hostname;
	int show_date;
	int show_cpu;
	int show_mem;
	int show_bat;
	int show_load;
	int show_winid;
	int show_net;
	int show_vpn;
};

// Extract logo from configuration line
char *extract_logo(const char *line)
{
	if (strstr(line, "logo=")) {
		const char *logo_start = strchr(line, '=') + 1;
		const char *logo_end = NULL;

		// Find the end of the logo string
		const char *cursor = logo_start;
		while (*cursor && *cursor != ' ' && *cursor != '\n') {
			cursor++;
		}
		logo_end = cursor;

		// Calculate the length of the logo string
		size_t logo_length = logo_end - logo_start;

		// Allocate memory for the logo and copy it
		char *logo = (char *) malloc((logo_length + 1) * sizeof(char));
		if (logo == NULL) {
			perror("No logo.");
			exit(EXIT_FAILURE);
		}
		strncpy(logo, logo_start, logo_length);
		logo[logo_length] = '\0';
		return logo;
	}
	return NULL;
}

// Read configuration file
struct Config config_file()
{
	struct Config config = { NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0 };
	const char *home_dir = getenv("HOME");
	if (home_dir == NULL) {
		fprintf(stderr, "Error: HOME environment variable not set\n");
		exit(EXIT_FAILURE);
	}

	const char *config_file_names[] = { "termbar.conf", ".termbar.conf" };
	FILE *file = NULL;
	char config_file_path[256];

	for (int i = 0; i < 2; ++i) {
		snprintf(config_file_path, sizeof(config_file_path), "%s/%s",
				 home_dir, config_file_names[i]);
		file = fopen(config_file_path, "r");
		if (file != NULL) {
			break;  // File found, exit loop
		}
	}

	if (file == NULL) {
		fprintf(stderr, "Error: Unable to open config file\n");
		exit(EXIT_FAILURE);
	}

	char line[MAX_LINE_LENGTH];

	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\n")] = '\0';

		char *logo = extract_logo(line);
		if (logo != NULL) {
			config.logo = logo;
			continue;       // Move to the next line
		}
		// Extract interface option
		if (strstr(line, "interface=")) {
			const char *interface_start = strchr(line, '=') + 1;
			size_t interface_length = strlen(interface_start);
			config.interface = malloc(interface_length + 1);
			if (config.interface == NULL) {
				perror("No if.");
				exit(EXIT_FAILURE);
			}
			strncpy(config.interface, interface_start,
					interface_length);
			config.interface[interface_length] = '\0';
		}
		// Check confs
		if (strstr(line, "date=yes")) {
			config.show_date = 1;
		} else if (strstr(line, "cpu=yes")) {
			config.show_cpu = 1;
		} else if (strstr(line, "load=yes")) {
			config.show_load = 1;
		} else if (strstr(line, "bat=yes")) {
			config.show_bat = 1;
		} else if (strstr(line, "net=yes")) {
			config.show_net = 1;
		} else if (strstr(line, "mem=yes")) {
			config.show_mem = 1;
		} else if (strstr(line, "winid=yes")) {
			config.show_winid = 1;
		} else if (strstr(line, "hostname=yes")) {
			config.show_hostname = 1;
		} else if (strstr(line, "vpn=yes")) {
			config.show_vpn = 1;
		}
	}

	fclose(file);
	return config;
}

// Update public IP address
void update_public_ip()
{
	FILE *fp;
	char buffer[128];

	struct addrinfo hints, *res;
	int sockfd;
	char buffer[INET_ADDRSTRLEN];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo("ifconfig.me", "http", &hints, &res) != 0) {
		perror("getaddrinfo");
		exit(EXIT_FAILURE);
	}

	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd == -1) {
		perror("socket");
		freeaddrinfo(res);
		exit(EXIT_FAILURE);
	}

	if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
		perror("connect");
		close(sockfd);
		freeaddrinfo(res);
		exit(EXIT_FAILURE);
	}

	const char *request = "GET / HTTP/1.1\r\nHost: ifconfig.me\r\nConnection: close\r\n\r\n";
	if (send(sockfd, request, strlen(request), 0) == -1) {
		perror("send");
		close(sockfd);
		freeaddrinfo(res);
		exit(EXIT_FAILURE);
	}

	ssize_t bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
	if (bytes_received == -1) {
		perror("recv");
		close(sockfd);
		freeaddrinfo(res);
	if (config.interface == NULL || strlen(config.interface) == 0) {
		config.interface = strdup("lo0");

	buffer[bytes_received] = '\0';
	char *ip_start = strstr(buffer, "\r\n\r\n");
	if (ip_start != NULL) {
		ip_start += 4; // Skip the "\r\n\r\n"
		strncpy(public_ip, ip_start, MAX_IP_LENGTH);
		public_ip[strcspn(public_ip, "\n")] = '\0';
	} else {
		strncpy(public_ip, "N/A", MAX_IP_LENGTH);
	}

	close(sockfd);
	freeaddrinfo(res);
}

// Get hostname
char *get_hostname()
{
	static char hostname[HOSTNAME_MAX_LENGTH];

	if (gethostname(hostname, HOSTNAME_MAX_LENGTH) == -1) {
		perror("gethostname");
		exit(EXIT_FAILURE);
	}

	return hostname;
}

// Update internal IP address
void update_internal_ip(struct Config config)
{
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in *sa;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}
	// Search for the specified interface or fallback to lo0
	if (config.interface == NULL || strlen(config.interface) == 0) {
		strlcpy(internal_ip, "lo0", sizeof(internal_ip));
	} else {
		// Search for the specified interface
		for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
			if (strcmp(ifa->ifa_name, config.interface) == 0 &&
				ifa->ifa_addr != NULL
				&& ifa->ifa_addr->sa_family == AF_INET) {
				sa = (struct sockaddr_in *) ifa->ifa_addr;
				inet_ntop(AF_INET, &(sa->sin_addr), internal_ip,
						  sizeof(internal_ip));
				break;
			}
		}
	}

	freeifaddrs(ifap);
}

// Update VPN status
void update_vpn()
{
	struct ifaddrs *ifap, *ifa;
	int has_wg_interface = 0;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}
	// Check for wgX interfaces
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strncmp(ifa->ifa_name, "wg", 2) == 0
			&& ifa->ifa_flags & IFF_UP) {
			has_wg_interface = 1;
			break;
		}
	}

	freeifaddrs(ifap);

	if (has_wg_interface)
		snprintf(vpn_status, sizeof(vpn_status), "%sVPN%s", GREEN, RESET);
	else
		snprintf(vpn_status, sizeof(vpn_status), "%sNo VPN%s", RED, RESET);
}

// Update memory information
unsigned long long update_mem()
{
	int mib[2];
	size_t len;
	unsigned long long freemem;

	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;

	len = sizeof(struct uvmexp);

	struct uvmexp uvm_stats;

	if (sysctl(mib, 2, &uvm_stats, &len, NULL, 0) == -1) {
		perror("sysctl");
		exit(EXIT_FAILURE);
	}

	freemem =
		(unsigned long long) uvm_stats.free *
		(unsigned long long) uvm_stats.pagesize / (1024 * 1024);

	return freemem;
}

// Update CPU base speed
void update_cpu_base_speed()
{
	int temp = 0;
	size_t templen = sizeof(temp);

	int mib[5] = { CTL_HW, HW_CPUSPEED };

	if (sysctl(mib, 2, &temp, &templen, NULL, 0) == -1) {
		perror("sysctl HW_CPUSPEED");
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "error");
	} else {
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "%4dMhz", temp);
	}
}

// Update CPU average speed
void update_cpu_avg_speed()
{
	uint64_t freq = 0;
	size_t len = sizeof(freq);
	int mib[2] = { CTL_HW, HW_CPUSPEED };
	if (sysctl(mib, 2, &freq, &len, NULL, 0) == -1) {
		fprintf(stderr, "Error: Failed to get CPU average speed\n");
		return;
	}
	}
	snprintf(cpu_avg_speed, sizeof(cpu_avg_speed), "%4lluMhz", freq);
}

// Update system load
void update_system_load(double *load_avg)
{
	double load[3];         // Take 1, 5, and 15-minute load averages

	if (getloadavg(load, 3) == -1) {
		perror("getloadavg");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < 3; i++) {
		load_avg[i] = load[i];
	}
}

// Update CPU temperature
void update_cpu_temp()
{
	struct sensor sensor;
	size_t templen = sizeof(sensor);
	int temp = -1;

	static int temp_mib = -1;

	if (temp_mib == -1) {
		for (temp_mib = 0; temp_mib < 20; temp_mib++) {
			int mib[5] = { CTL_HW, HW_SENSORS, temp_mib, SENSOR_TEMP, 0 };  // acpitz0.temp0 (x395)
			if (sysctl(mib, 5, &sensor, &templen, NULL, 0) != -1)
				break;
		}
	}

	if (temp_mib != -1) {
		int mib[5] = { CTL_HW, HW_SENSORS, temp_mib, SENSOR_TEMP, 0 };
		if (sysctl(mib, 5, &sensor, &templen, NULL, 0) != -1) {
			temp = (sensor.value - 273150000) / 1000000.0;
			snprintf(cpu_temp, sizeof(cpu_temp), "%d\302\260C", temp);
			return;
		}
	}
	// If no valid temperature reading found, set to "x"
	// specially for VMs
	snprintf(cpu_temp, sizeof(cpu_temp), "x");
}

// Update battery information
void update_battery()
{
	int fd;
	struct apm_power_info pi;

	if ((fd = open("/dev/apm", O_RDONLY)) == -1 ||
		ioctl(fd, APM_IOC_GETPOWER, &pi) == -1 || close(fd) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
	if ((fd = open("/dev/apm", O_RDONLY)) == -1 ||
		ioctl(fd, APM_IOC_GETPOWER, &pi) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		if (fd != -1) {
			close(fd);
		}
		return;
	}

	if (close(fd) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}

	if (pi.ac_state == APM_AC_ON) {
		snprintf(battery_percent, sizeof(battery_percent), "%d%%",
				 pi.battery_life);
	} else {
		snprintf(battery_percent, sizeof(battery_percent), "%s%d%%%s",
				 RED, pi.battery_life, RESET);
	}
}

// Update date and time
void update_datetime()
{
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(datetime, sizeof(datetime), "%a %d %b %H:%M", timeinfo);
}

// Update window ID
void update_windowid(char *window_id)
{
	const char *command =
		"xprop -root 32c '\\t$0' _NET_CURRENT_DESKTOP | cut -f 2";

	FILE *pipe = popen(command, "r");
	if (pipe == NULL) {
		fprintf(stderr,
				"Error: Failed to open pipe for command execution");
		strlcpy(window_id, "N/A", MAX_OUTPUT_LENGTH);
		return;
	}

	char output[MAX_OUTPUT_LENGTH];
	if (fgets(output, MAX_OUTPUT_LENGTH, pipe) == NULL) {
		fprintf(stderr, "Error: Failed to read command output");
		strlcpy(window_id, "N/A", MAX_OUTPUT_LENGTH);
		pclose(pipe);
		return;
	}

	pclose(pipe);

	size_t len = strlen(output);
	if (len > 0 && output[len - 1] == '\n') {
		output[len - 1] = '\0';
	}

	strlcpy(window_id, output, MAX_OUTPUT_LENGTH);
}

// Create Xlib window
void create_window(Display **display, Window *window, GC *gc, int *screen) {
	*display = XOpenDisplay(NULL);
	if (*display == NULL) {
		fprintf(stderr, "Cannot open display\n");
		exit(1);
	*window = XCreateSimpleWindow(*display, RootWindow(*display, *screen), 0, 0, window_width, window_height, 1,
								  BlackPixel(*display, *screen), WhitePixel(*display, *screen));
	if (!*window) {
		fprintf(stderr, "Error: Failed to create window\n");
		XCloseDisplay(*display);
		exit(1);
	}

	if (!XSelectInput(*display, *window, ExposureMask | KeyPressMask)) {
		fprintf(stderr, "Error: Failed to select input\n");
		XDestroyWindow(*display, *window);
		XCloseDisplay(*display);
		exit(1);
	}

	if (!XMapWindow(*display, *window)) {
		fprintf(stderr, "Error: Failed to map window\n");
		XDestroyWindow(*display, *window);
		XCloseDisplay(*display);
		exit(1);
	}
	int window_width = screen_width;
	int window_height = 30; // Fixed height for the bar

	*window = XCreateSimpleWindow(*display, RootWindow(*display, *screen), 0, 0, window_width, window_height, 1,
								  BlackPixel(*display, *screen), WhitePixel(*display, *screen));

	XSelectInput(*display, *window, ExposureMask | KeyPressMask);
	XMapWindow(*display, *window);

	// Set window properties to make it unmanaged and always on top
	Atom wm_state = XInternAtom(*display, "_NET_WM_STATE", False);
	Atom wm_state_above = XInternAtom(*display, "_NET_WM_STATE_ABOVE", False);
	Atom wm_bypass_wm = XInternAtom(*display, "_NET_WM_BYPASS_COMPOSITOR", False);
	XChangeProperty(*display, *window, wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&wm_state_above, 1);
	XChangeProperty(*display, *window, wm_bypass_wm, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&wm_bypass_wm, 1);

	*gc = XCreateGC(*display, *window, 0, NULL);
	XSetForeground(*display, *gc, BlackPixel(*display, *screen));
	XSetBackground(*display, *gc, WhitePixel(*display, *screen));
	XClearWindow(*display, *window);
	XMapRaised(*display, *window);
}

	// Set locale to UTF-8
	setlocale(LC_ALL, "en_US.UTF-8");
	XDrawString(display, window, gc, 10, 20, text, strlen(text));
}

// Main function
int main(int argc, const char *argv[])
{
	Display *display;
	Window window;
	GC gc;
	int screen;

	create_window(&display, &window, &gc, &screen);

	// We all want UTF8
	setlocale(LC_CTYPE, "C");
	setlocale(LC_ALL, "en_US.UTF-8");

	// Read the config file
	struct Config config = config_file();
	if (config.logo == NULL) {
		fprintf(stderr, "Error: Unable to read name from config file\n");
		return 1;
	}

	// Hide cursor
	printf("\e[?25l");

	while (1) {
		char buffer[1024];
		snprintf(buffer, sizeof(buffer), "\r\e[K");

		if (config.show_winid) {
			update_windowid(window_id);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s[%s]%s", RESET, window_id, RESET);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.logo != NULL && strlen(config.logo) > 0) {
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s%s%s", GREEN, config.logo, RESET);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_hostname) {
			char *hostname = get_hostname();
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), " %s ", hostname);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_date) {
			update_datetime();
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), " %s ", datetime);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_cpu) {
			update_cpu_temp();
			update_cpu_avg_speed();
			update_cpu_base_speed();
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), " %sCPU:%s %s (%s) ", GREEN, RESET, cpu_avg_speed, cpu_temp);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_mem) {
			free_memory = update_mem();
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), " %sMem:%s %.0llu MB ", GREEN, RESET, free_memory);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_load) {
			update_system_load(system_load);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), " %sLoad:%s %.2f ", GREEN, RESET, system_load[0]);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_bat) {
			update_battery();
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), " %sBat:%s %s ", GREEN, RESET, battery_percent);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_vpn) {
			update_vpn();
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%s|%s", PURPLE, RESET);
		}
		if (config.show_net) {
			update_public_ip();
			update_internal_ip(config);
			snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), " %sIPs:%s %s ~ %s ", GREEN, RESET, public_ip, internal_ip);
		}

		draw_text(display, window, gc, buffer);

		fflush(stdout);
		if (argc == 2)
			if (strcmp("-1", argv[1]) >= 0)
			if (strcmp(argv[1], "-1") == 0)
		usleep(2000000);
	}

	XCloseDisplay(display);
	return 0;
}

