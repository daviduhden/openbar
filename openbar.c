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

#include <sys/types.h>
#include <sys/param.h>
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

// Definitions for various lengths and sizes used in the program
#define SLEEP_DURATION 2
#define INET_ADDRSTRLEN 16
#define MAX_IP_LENGTH 32
#define MAX_LINE_LENGTH 256
#define MAX_OUTPUT_LENGTH 16
#define HOSTNAME_MAX_LENGTH 256

// Define color values for readability
#define BLACK_COLOR 0x000000
#define WHITE_COLOR 0xFFFFFF

// Define a named constant for bar height
#define BAR_HEIGHT 20

// Define IFF_UP if not defined
#ifndef IFF_UP
#define IFF_UP 0x1
#endif

// Global variables for storing various system information
static char battery_percent[32];
static char cpu_temp[32];
static char cpu_base_speed[32];
static char cpu_avg_speed[32];
static char datetime[32];
static char public_ip[MAX_IP_LENGTH];
static char internal_ip[INET_ADDRSTRLEN];
static char vpn_status[16];
static char mem_info[32];
static char window_id[MAX_OUTPUT_LENGTH];
double system_load[3];
unsigned long long free_memory;

// Struct for configuration settings
struct Config {
	char *logo_path;
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
	int background_color;
};

// Function to extract the logo from a configuration line
// This function searches for the "logo=" keyword in the line and extracts
// the logo string.
char *extract_logo(const char *line) {
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
		char *logo_path = (char *) calloc((logo_length + 1), sizeof(*logo_path));
		if (logo_path == NULL) {
			perror("Failed to allocate memory for logo_path");
			exit(EXIT_FAILURE);
		}
		strlcpy(logo_path, logo_start, logo_length + 1);
		return logo_path;
	}
	return NULL;
}

// Function to read configuration settings from a file
// This function reads the configuration file and populates the Config
// struct with the settings.
struct Config read_config_file() {
	struct Config config = {
		.logo_path = NULL,
		.interface = NULL,
		.show_hostname = 0,
		.show_date = 0,
		.show_cpu = 0,
		.show_mem = 0,
		.show_bat = 0,
		.show_load = 0,
		.show_winid = 0,
		.show_net = 0,
		.show_vpn = 0,
		.background_color = 0
	};
	const char *home_dir = getenv("HOME");
	if (home_dir == NULL) {
		fprintf(stderr, "Error: HOME environment variable not set\n");
		exit(EXIT_FAILURE);
	}

	const char *config_file_names[] = { "openbar.conf", ".openbar.conf" };
	FILE *file = NULL;
	char config_file_path[256];

	// Search for the config file in the home directory
	for (int i = 0; i < 2; ++i) {
		int ret = snprintf(config_file_path, sizeof(config_file_path), "%s/%s", home_dir, config_file_names[i]);
		if (ret < 0 || ret >= sizeof(config_file_path)) {
			fprintf(stderr, "Error: Config file path is too long\n");
			exit(EXIT_FAILURE);
		}
		file = fopen(config_file_path, "r");
		if (file != NULL) {
			break;  // File found, exit loop
		}
	}
	// If config file not found in home directory, search in etc directory
	if (file == NULL) {
		snprintf(config_file_path, sizeof(config_file_path), "/etc/%s", config_file_names[0]);
		file = fopen(config_file_path, "r");
	}
	if (file == NULL) {
		fprintf(stderr, "Error: Unable to open config file: %s\n", config_file_path);
		exit(EXIT_FAILURE);
	}

	char line[MAX_LINE_LENGTH];

	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\n")] = '\0';

		char *logo_path = extract_logo(line);
		if (logo_path != NULL) {
			config.logo_path = logo_path;
			continue;  // Move to the next line
		}
		// Extract interface option
		if (strstr(line, "interface=")) {
			const char *interface_start = strchr(line, '=') + 1;
			size_t interface_length = strlen(interface_start);
			config.interface = calloc((interface_length + 1), sizeof(*config.interface));
			if (config.interface == NULL) {
				perror("Failed to allocate memory for interface");
				exit(EXIT_FAILURE);
			}
			strlcpy(config.interface, interface_start, interface_length + 1);
		}
		// Check other configuration options
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
		} else if (strstr(line, "background=black")) {
			config.background_color = 1;
		} else if (strstr(line, "background=white")) {
			config.background_color = 0;
		}
	}

	fclose(file);
	return config;
}

// Function to update the public IP address
// This function uses the curl command to fetch the public IP address and
// stores it in the global variable.
void update_public_ip() {
	FILE *fp;
	char buffer[32];

	// Using curl to get the public IP address
	fp = popen("curl -s ifconfig.me", "r");
	if (fp == NULL) {
		perror("Failed to open pipe for curl command");
		exit(EXIT_FAILURE);
	} else {
		if (fgets(buffer, sizeof(buffer), fp) != NULL) {
			buffer[strcspn(buffer, "\n")] = '\0';
		}
		strlcpy(public_ip, buffer, sizeof(public_ip));
		pclose(fp);
	}
}

// Function to get the hostname of the machine
// This function retrieves the hostname of the machine using the gethostname
// system call.
char *get_hostname() {
	static char hostname[HOSTNAME_MAX_LENGTH];

	if (gethostname(hostname, HOSTNAME_MAX_LENGTH) == -1) {
		perror("gethostname");
		exit(EXIT_FAILURE);
	}

	return hostname;
}

// Function to update the internal IP address
// This function retrieves the internal IP address of the specified network
// interface.
void update_internal_ip(struct Config config) {
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in *sa;
	bool found = false;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(ifa->ifa_name, config.interface) == 0 &&
			ifa->ifa_addr != NULL &&
			ifa->ifa_addr->sa_family == AF_INET) {
			sa = (struct sockaddr_in *) ifa->ifa_addr;
			inet_ntop(AF_INET, &(sa->sin_addr), internal_ip, sizeof(internal_ip));
			found = true;
			break;
		}
	}
	if (!found) {
		strlcpy(internal_ip, "127.0.0.1", sizeof(internal_ip));
	}
	freeifaddrs(ifap);
}

// Function to update VPN status
// This function checks for the presence of WireGuard (wg) interfaces to
// determine VPN status.
void update_vpn() {
	struct ifaddrs *ifap, *ifa;
	int has_wg_interface = 0;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		return;
	}
	// Check for wgX interfaces
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strncmp(ifa->ifa_name, "wg", 2) == 0 &&
			ifa->ifa_flags & IFF_UP) {
			has_wg_interface = 1;
			break;
		}
	}

	freeifaddrs(ifap);

	if (has_wg_interface)
		strlcpy(vpn_status, "VPN", sizeof(vpn_status));
	else
		strlcpy(vpn_status, "No VPN", sizeof(vpn_status));
}

// Function to update memory usage
// This function retrieves the amount of free memory in the system.
unsigned long long update_mem() {
	int mib[2];
	size_t len;
	unsigned long long freemem;

	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;

	len = sizeof(struct uvmexp);

	struct uvmexp uvm_stats;

	if (sysctl(mib, 2, &uvm_stats, &len, NULL, 0) == -1) {
		perror("Failed to get memory statistics");
		exit(EXIT_FAILURE);
	} else {
		freemem =
			(unsigned long long) uvm_stats.free *
			(unsigned long long) uvm_stats.pagesize / (1024 * 1024);
	}

	return freemem;
}

// Function to update the CPU base speed
// This function retrieves the base speed of the CPU.
void update_cpu_base_speed() {
	int temp = 0;
	size_t templen = sizeof(temp);

	int mib[2] = { CTL_HW, HW_CPUSPEED };

	if (sysctl(mib, 2, &temp, &templen, NULL, 0) == -1) {
		perror("sysctl");
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "no_freq");
	} else {
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "%4dMHz", temp);
	}
}

// Function to update the average CPU speed
// This function retrieves the average speed of the CPU.
void update_cpu_avg_speed() {
	uint64_t freq = 0;
	size_t len = sizeof(freq);
	int mib[2] = { CTL_HW, HW_CPUSPEED };

	if (sysctl(mib, 2, &freq, &len, NULL, 0) == -1) {
		perror("sysctl");
		return;
	} else {
		snprintf(cpu_avg_speed, sizeof(cpu_avg_speed), "%4lluMHz", freq);
	}
}

// Function to update the system load
// This function retrieves the system load averages for the past 1, 5, and
// 15 minutes.
void update_system_load(double load_avg[3]) {
	double load[3];

	if (getloadavg(load, 3) == -1) {
		perror("getloadavg");
		exit(EXIT_FAILURE);
	} else {
		for (int i = 0; i < 3; i++) {
			load_avg[i] = load[i];
		}
	}
}

// Function to update the CPU temperature
// This function retrieves the temperature of the CPU.
void update_cpu_temp() {
	struct sensor sensor;
	size_t templen = sizeof(sensor);
	int temp = -1;

	static int temp_mib = -1;
	if (temp_mib == -1) {
		const int max_sensors = 20;  // Define an upper limit for the loop
		for (temp_mib = 0; temp_mib < max_sensors; temp_mib++) {
			int mib[5] = { CTL_HW, HW_SENSORS, temp_mib, SENSOR_TEMP, 0 };  // acpitz0.temp0 (x395)
			if (sysctl(mib, 5, &sensor, &templen, NULL, 0) != -1)
				break;
		}
		if (temp_mib == max_sensors) {
			temp_mib = -1;  // Reset temp_mib if no valid sensor is found
		}
	}

	if (temp_mib != -1) {
		int mib[5] = { CTL_HW, HW_SENSORS, temp_mib, SENSOR_TEMP, 0 };
		if (sysctl(mib, 5, &sensor, &templen, NULL, 0) == -1) {
			perror("sysctl");
			snprintf(cpu_temp, sizeof(cpu_temp), "x");
			return;
		}
		temp = (sensor.value - 273150000) / 1000000.0;
		if (temp >= 0 && temp <= 100) {
			snprintf(cpu_temp, sizeof(cpu_temp), "%d ºC", temp);
			return;
		}
	}
	// If no valid temperature reading found, set to "x"
	// specially for VMs
	snprintf(cpu_temp, sizeof(cpu_temp), "x");
}

// Function to update battery status
// This function retrieves the battery status and percentage.
void update_battery() {
	int fd;
	struct apm_power_info pi;

	if ((fd = open("/dev/apm", O_RDONLY)) == -1) {
		perror("Failed to open /dev/apm");
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}

	if (ioctl(fd, APM_IOC_GETPOWER, &pi) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		close(fd);
		return;
	}

	if (close(fd) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}

	if (pi.battery_state == (unsigned char)APM_BATT_UNKNOWN ||
		pi.battery_state == (unsigned char)APM_BATTERY_ABSENT) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}
	snprintf(battery_percent, sizeof(battery_percent), "%d%%", pi.battery_life);
}

// Function to update date and time
// This function retrieves the current date and time.
void update_datetime() {
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(datetime, sizeof(datetime), "%a %d %b %H:%M", timeinfo);
}

// Function to update the window ID
// This function retrieves the ID of the currently focused window.
void update_windowid(char *window_id) {
	const char *command =
		"xprop -root 32c '\\t$0' _NET_CURRENT_DESKTOP | cut -f 2";

	FILE *pipe = popen(command, "r");
	if (pipe == NULL) {
		fprintf(stderr, "Error: Failed to open pipe for xprop command");
		strlcpy(window_id, "N/A", MAX_OUTPUT_LENGTH);
		return;
	}
	char output[MAX_OUTPUT_LENGTH];
	if (fgets(output, MAX_OUTPUT_LENGTH, pipe) == NULL) {
		fprintf(stderr, "Error: Failed to read xprop command output");
		strlcpy(window_id, "N/A", MAX_OUTPUT_LENGTH);
		pclose(pipe);
		return;
	}
	size_t len = strlen(output);
	if (len > 0 && output[len - 1] == '\n') {
		output[len - 1] = '\0';
	}
	strlcpy(window_id, output, MAX_OUTPUT_LENGTH);
	pclose(pipe);
}

// Function to draw text on the X11 window
int get_text_width(Display *display, GC gc, const char *text) {
	return XTextWidth(XQueryFont(display, XGContextFromGC(gc)), text, strlen(text));
}

// Function to calculate the width of the text
int calculate_text_width(Display *display, GC gc, const char *text) {
	return XTextWidth(XQueryFont(display, XGContextFromGC(gc)), text, strlen(text));
}

// Function to draw truncated text on the X11 window
// This function draws the text truncated to fit within the specified
// maximum width.
void draw_text_with_max_width(Display *display, Window window, GC gc, int x,
	int y, const char *text, int max_width) {
	int len = strlen(text);
	int text_width = calculate_text_width(display, gc, text);
	while (text_width > max_width && len > 0) {
		len--;
		text_width = calculate_text_width(display, gc, text);
	}
	char truncated_text[len + 1];
	strlcpy(truncated_text, text, len + 1);
	truncated_text[len] = '\0';
	XDrawString(display, window, gc, x, y, truncated_text, len);
}

// Function to draw text on the X11 window
// This function draws the text, truncating it if necessary to fit within
// the specified maximum width.
void draw_text(Display *display, Window window, GC gc, int x, int y,
	const char *text, int max_width) {
	int text_width = calculate_text_width(display, gc, text);
	if (text_width > max_width) {
		draw_text_with_max_width(display, window, gc, x, y, text, max_width);
	} else {
		XDrawString(display, window, gc, x, y, text, strlen(text));
	}
}

// Function to calculate the total width of text tokens
// This function calculates the total width of the text tokens separated
// by the '|' character.
int get_total_text_width(Display *display, GC gc, const char *text) {
	char buffer[1024];
	strlcpy(buffer, text, sizeof(buffer));
	buffer[sizeof(buffer) - 1] = '\0';

	char *saveptr;
	char *token = strtok_r(buffer, "|", &saveptr);
	int total_text_width = 0;
	while (token != NULL) {
		total_text_width += XTextWidth(XQueryFont(display, XGContextFromGC(gc)), token, strlen(token)) + 5;
		token = strtok_r(NULL, "|", &saveptr);
	}
	return total_text_width;
}

// Function to draw text tokens on the X11 window
// This function draws the text tokens separated by the '|' character.
void draw_text_segments(Display *display, Window window, GC gc, int x, int y,
	const char *text) {
	char buffer[1024];
	strlcpy(buffer, text, sizeof(buffer));
	buffer[sizeof(buffer) - 1] = '\0';

	char *saveptr;
	char *token = strtok_r(buffer, "|", &saveptr);
	while (token != NULL) {
		XDrawString(display, window, gc, x, y, token, strlen(token));
		x += XTextWidth(XQueryFont(display, XGContextFromGC(gc)), token, strlen(token)) + 5;
		token = strtok_r(NULL, "|", &saveptr);
	}
}

// Function to draw centered text on the X11 window
// This function draws the text centered within the specified maximum
// width.
void draw_text_centered(Display *display, Window window, GC gc, int x, int y, const char *text, int max_width) {
	int total_text_width = get_total_text_width(display, gc, text);
	int text_x = (max_width - total_text_width) / 2;
	draw_text_segments(display, window, gc, text_x, y, text);
}

// Main function
// This function initializes the X11 display, reads the configuration file,
// and enters the main event loop to update and draw the status bar.
int main(int argc, const char *argv[]) {
	// Set locale for UTF-8
	setlocale(LC_ALL, "C.UTF-8");

	// Read the config file
	struct Config config = read_config_file();
	if (config.logo_path == NULL) {
		fprintf(stderr, "Error: Unable to read logo path from config file\n");
		return 1;
	}

	// Initialize X11
	Display *display = XOpenDisplay(NULL);
	if (display == NULL) {
		fprintf(stderr, "Unable to open X display\n");
		return 1;
	}

	int screen = DefaultScreen(display);

	unsigned long fg_color = config.background_color == 0 ? BLACK_COLOR : WHITE_COLOR;
	unsigned long bg_color = config.background_color == 0 ? WHITE_COLOR : BLACK_COLOR;
	
	// Get screen dimensions
	int screen_width = DisplayWidth(display, screen);
	int bar_width = screen_width;  // Width of the bar set to screen width
	int bar_height = BAR_HEIGHT;  // Height of the bar
	int x = 0;  // Start position of the bar
	int y = 0;  // Position the bar at the top
	Window window = XCreateSimpleWindow(display, RootWindow(display, screen), x, y, bar_width, bar_height, 1, fg_color, bg_color);
	XSetWindowAttributes attrs;
	attrs.override_redirect = True;
	XChangeWindowAttributes(display, window, CWOverrideRedirect, &attrs);

	// Create GC (Graphics Context)
	GC gc = XCreateGC(display, window, 0, NULL);
	XSetForeground(display, gc, fg_color);

	// Load non-Unicode font
	XFontStruct *font_info = XLoadQueryFont(display, "fixed");
	if (font_info == NULL) {
		fprintf(stderr, "Failed to load X11 font\n");
		return 1;
	}
	XSetFont(display, gc, font_info->fid);

	// Show the window
	XMapWindow(display, window);

	struct timespec req = { .tv_sec = SLEEP_DURATION, .tv_nsec = 0 };

	// Main event loop
	while (1) {
		XClearWindow(display, window);

		// Collect and calculate width of each part of the text
		char text_buffer[1024];
		text_buffer[0] = '\0';

		// Collect and concatenate text parts with separators
		if (config.show_winid) {
			update_windowid(window_id);
			strlcat(text_buffer, window_id, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));

		}
		if (config.logo_path != NULL && strlen(config.logo_path) > 0) {
			strlcat(text_buffer, config.logo_path, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_hostname) {
			char *hostname = get_hostname();
			strlcat(text_buffer, hostname, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_date) {
			update_datetime();
			strlcat(text_buffer, datetime, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_cpu) {
			update_cpu_temp();
			update_cpu_avg_speed();
			update_cpu_base_speed();
			char cpu_info[MAX_OUTPUT_LENGTH * 2];
			snprintf(cpu_info, sizeof(cpu_info), "CPU: %s (%s)", cpu_avg_speed, cpu_temp);
			strlcat(text_buffer, cpu_info, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_mem) {
			free_memory = update_mem();
			snprintf(mem_info, sizeof(mem_info), "Mem: %.0llu MB", free_memory);
			strlcat(text_buffer, mem_info, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_load) {
			update_system_load(system_load);
			char load_info[MAX_OUTPUT_LENGTH];
			snprintf(load_info, sizeof(load_info), "Load: %.2f", system_load[0]);
			strlcat(text_buffer, load_info, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_bat) {
			update_battery();
			strlcat(text_buffer, battery_percent, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_vpn) {
			update_vpn();
			strlcat(text_buffer, "VPN ", sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		if (config.show_net) {
			update_public_ip();
			update_internal_ip(config);
			char net_info[MAX_OUTPUT_LENGTH];
			snprintf(net_info, sizeof(net_info), "IPs: %s ~ %s", public_ip, internal_ip);
			strlcat(text_buffer, net_info, sizeof(text_buffer));
			strlcat(text_buffer, " | ", sizeof(text_buffer));
		}

		// Center the text horizontally
		const int text_y = 15;  // Y position for text, adjusted for smaller height

		// Draw the centered text with separators
		draw_text_centered(display, window, gc, 0, text_y, text_buffer, bar_width);

		if (nanosleep(&req, NULL) == -1) {
			perror("nanosleep");
			exit(EXIT_FAILURE);
		}
		nanosleep(&req, NULL);  // Sleep for 2 seconds before the next update
	}

	// Close the X11 display
	XCloseDisplay(display);
	return 0;
}
