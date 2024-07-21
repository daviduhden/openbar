/*
 * Copyright (c) 2024 Gonzalo Rodriguez <gonzalo@x61.sh>
 * Copyright (c) 2024 David Uhden Collado <david@uhden.dev>
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

#include "openbar.h"

#define MAX_LINE_LENGTH 256
#define MAX_IP_LENGTH 128
#define HOSTNAME_MAX_LENGTH 256
#define MAX_OUTPUT_LENGTH 256

// Struct for configuration settings
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

// Function to extract the logo from a configuration line
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

// Function to read configuration settings from a file
struct Config config_file() {
	struct Config config = { NULL, 0, 0, 0, 0, 0, 0, 0, 0 };
	const char *home_dir = getenv("HOME");
	if (home_dir == NULL) {
		fprintf(stderr, "Error: HOME environment variable not set\n");
		exit(EXIT_FAILURE);
	}

	const char *config_file_names[] = { "openbar.conf", ".openbar.conf" };
	FILE *file = NULL;
	char config_file_path[256];

	for (int i = 0; i < 2; ++i) {
		snprintf(config_file_path, sizeof(config_file_path), "%s/%s", home_dir, config_file_names[i]);
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
			continue;  // Move to the next line
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
			strncpy(config.interface, interface_start, interface_length);
			config.interface[interface_length] = '\0';
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
		}
	}

	fclose(file);
	return config;
}

// Function to update the public IP address
void update_public_ip() {
	FILE *fp;
	char buffer[128];

	// Using curl to get the public IP address
	fp = popen("/usr/local/bin/curl -s ifconfig.me", "r");
	if (fp == NULL) {
		perror("popen");
		exit(EXIT_FAILURE);
	}

	if (fgets(buffer, sizeof(buffer), fp) != NULL) {
		// Copy the public IP address to the global variable
		strncpy(public_ip, buffer, MAX_IP_LENGTH);
		// Remove trailing newline character, if any
		public_ip[strcspn(public_ip, "\n")] = '\0';
	}
	pclose(fp);
}

// Function to get the hostname of the machine
char *get_hostname() {
	static char hostname[HOSTNAME_MAX_LENGTH];

	if (gethostname(hostname, HOSTNAME_MAX_LENGTH) == -1) {
		perror("gethostname");
		exit(EXIT_FAILURE);
	}

	return hostname;
}

// Function to update the internal IP address
void update_internal_ip(struct Config config) {
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
				ifa->ifa_addr != NULL &&
				ifa->ifa_addr->sa_family == AF_INET) {
				sa = (struct sockaddr_in *) ifa->ifa_addr;
				inet_ntop(AF_INET, &(sa->sin_addr), internal_ip, sizeof(internal_ip));
				break;
			}
		}
	}

	freeifaddrs(ifap);
}

// Function to update VPN status
void update_vpn() {
	struct ifaddrs *ifap, *ifa;
	int has_wg_interface = 0;

	if (getifaddrs(&ifap) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
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
		printf(" %sVPN%s ", "\033[32m", "\033[0m");
	else
		printf(" %sNo VPN%s ", "\033[31m", "\033[0m");
}

// Function to update memory usage
unsigned long long update_mem() {
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

// Function to update the CPU base speed
void update_cpu_base_speed() {
	int temp = 0;
	size_t templen = sizeof(temp);

	int mib[2] = { CTL_HW, HW_CPUSPEED };

	if (sysctl(mib, 2, &temp, &templen, NULL, 0) == -1)
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "no_freq");
	else
		snprintf(cpu_base_speed, sizeof(cpu_base_speed), "%4dMhz", temp);
}

// Function to update the average CPU speed
void update_cpu_avg_speed() {
	uint64_t freq = 0;
	size_t len = sizeof(freq);
	int mib[2] = { CTL_HW, HW_CPUSPEED };

	if (sysctl(mib, 2, &freq, &len, NULL, 0) == -1) {
		perror("sysctl");
		return;
	}
	snprintf(cpu_avg_speed, sizeof(cpu_avg_speed), "%4lluMhz", freq);
}

// Function to update the system load
void update_system_load(double *load_avg) {
	double load[3];  // Take 1, 5, and 15-minute load averages

	if (getloadavg(load, 3) == -1) {
		perror("getloadavg");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < 3; i++) {
		load_avg[i] = load[i];
	}
}

// Function to update the CPU temperature
void update_cpu_temp() {
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
			if (temp >= 0 && temp <= 100) {  // hmmm could be more than 100?
				snprintf(cpu_temp, sizeof(cpu_temp), "%d\302\260C", temp);
				return;
			}
		}
	}
	// If no valid temperature reading found, set to "x"
	// specially for VMs
	snprintf(cpu_temp, sizeof(cpu_temp), "x");
}

// Function to update battery status
void update_battery() {
	int fd;
	struct apm_power_info pi;

	if ((fd = open("/dev/apm", O_RDONLY)) == -1 ||
		ioctl(fd, APM_IOC_GETPOWER, &pi) == -1 || close(fd) == -1) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}

	if (pi.battery_state == APM_BATT_UNKNOWN ||
		pi.battery_state == APM_BATTERY_ABSENT) {
		strlcpy(battery_percent, "N/A", sizeof(battery_percent));
		return;
	}

	if (pi.ac_state == APM_AC_ON) {
		snprintf(battery_percent, sizeof(battery_percent), "%d%%", pi.battery_life);
	} else {
		snprintf(battery_percent, sizeof(battery_percent), "%s%d%%%s", "\033[31m", pi.battery_life, "\033[0m");
	}
}

// Function to update date and time
void update_datetime() {
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(datetime, sizeof(datetime), "%a %d %b %H:%M", timeinfo);
}

// Function to update the window ID
void update_windowid(char *window_id) {
	const char *command =
		"xprop -root 32c '\\t$0' _NET_CURRENT_DESKTOP | cut -f 2";

	FILE *pipe = popen(command, "r");
	if (pipe == NULL) {
		fprintf(stderr, "Error: Failed to open pipe for command execution");
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

// Function to draw text on the X11 window
void draw_text(Display *display, Window window, GC gc, int x, int y, const char *text) {
	XDrawString(display, window, gc, x, y, text, strlen(text));
}

int main(int argc, const char *argv[]) {
	// Set locale for UTF-8
	setlocale(LC_CTYPE, "C");
	setlocale(LC_ALL, "en_US.UTF-8");

	// Read the config file
	struct Config config = config_file();
	if (config.logo == NULL) {
		fprintf(stderr, "Error: Unable to read name from config file\n");
		return 1;
	}

	// Initialize X11
	Display *display = XOpenDisplay(NULL);
	if (display == NULL) {
		fprintf(stderr, "Unable to open X display\n");
		return 1;
	}

	int screen = DefaultScreen(display);
	unsigned long black = BlackPixel(display, screen);
	unsigned long white = WhitePixel(display, screen);

	// Create window
	Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 800, 30, 1, black, white);
	XStoreName(display, window, "Status Bar");

	// Create GC (Graphics Context)
	GC gc = XCreateGC(display, window, 0, NULL);
	XSetForeground(display, gc, black);

	// Show the window
	XMapWindow(display, window);

	// Main event loop
	while (1) {
		XClearWindow(display, window);
		int x = 10;  // Starting X position for text

		// Update and display window ID if enabled
		if (config.show_winid) {
			update_windowid(window_id);
			draw_text(display, window, gc, x, 20, window_id);
			x += 100;  // Adjust X position for next text
		}

		// Display logo if available
		if (config.logo != NULL && strlen(config.logo) > 0) {
			draw_text(display, window, gc, x, 20, config.logo);
			x += 100;  // Adjust X position for next text
		}

		// Update and display hostname if enabled
		if (config.show_hostname) {
			char *hostname = get_hostname();
			draw_text(display, window, gc, x, 20, hostname);
			x += 100;  // Adjust X position for next text
		}

		// Update and display date/time if enabled
		if (config.show_date) {
			update_datetime();
			draw_text(display, window, gc, x, 20, datetime);
			x += 150;  // Adjust X position for next text
		}

		// Update and display CPU information if enabled
		if (config.show_cpu) {
			update_cpu_temp();
			update_cpu_avg_speed();
			update_cpu_base_speed();
			char cpu_info[MAX_OUTPUT_LENGTH];
			snprintf(cpu_info, sizeof(cpu_info), "CPU: %s (%s)", cpu_avg_speed, cpu_temp);
			draw_text(display, window, gc, x, 20, cpu_info);
			x += 200;  // Adjust X position for next text
		}

		// Update and display memory usage if enabled
		if (config.show_mem) {
			free_memory = update_mem();
			snprintf(mem_info, sizeof(mem_info), "Mem: %.0llu MB", free_memory);
			draw_text(display, window, gc, x, 20, mem_info);
			x += 150;  // Adjust X position for next text
		}

		// Update and display system load if enabled
		if (config.show_load) {
			update_system_load(system_load);
			char load_info[MAX_OUTPUT_LENGTH];
			snprintf(load_info, sizeof(load_info), "Load: %.2f", system_load[0]);
			draw_text(display, window, gc, x, 20, load_info);
			x += 150;  // Adjust X position for next text
		}

		// Update and display battery status if enabled
		if (config.show_bat) {
			update_battery();
			draw_text(display, window, gc, x, 20, battery_percent);
			x += 100;  // Adjust X position for next text
		}

		// Update and display VPN status if enabled
		if (config.show_vpn) {
			update_vpn();
			x += 100;  // Adjust X position for next text
		}

		// Update and display network information if enabled
		if (config.show_net) {
			update_public_ip();
			update_internal_ip(config);
			char net_info[MAX_OUTPUT_LENGTH];
			snprintf(net_info, sizeof(net_info), "IPs: %s ~ %s", public_ip, internal_ip);
			draw_text(display, window, gc, x, 20, net_info);
		}

		XFlush(display);  // Flush the X11 buffer to update the window
		usleep(2000000);  // Sleep for 2 seconds before the next update
	}

	// Close the X11 display
	XCloseDisplay(display);
	return 0;
}
