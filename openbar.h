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

#ifndef OPENBAR_H
#define OPENBAR_H

// Definitions for various lengths and sizes used in the program
#define INET_ADDRSTRLEN 16
#define MAX_IP_LENGTH 32
#define MAX_LINE_LENGTH 256
#define MAX_OUTPUT_LENGTH 16
#define HOSTNAME_MAX_LENGTH 256

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
char vpn_status[16];
char mem_info[32];
char window_id[MAX_OUTPUT_LENGTH];
double system_load[3];
unsigned long long free_memory;

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
	int background_color;
};

// Function declarations

// Extracts the logo from a configuration line
char *extract_logo(const char *line);

// Reads configuration settings from a file
struct Config config_file();

// Updates the public IP address
void update_public_ip();

// Retrieves the hostname of the machine
char *get_hostname();

// Updates the internal IP address of the specified network interface
void update_internal_ip(struct Config config);

// Updates the VPN status
void update_vpn();

// Retrieves the amount of free memory in the system
unsigned long long update_mem();

// Retrieves the base speed of the CPU
void update_cpu_base_speed();

// Retrieves the average speed of the CPU
void update_cpu_avg_speed();

// Retrieves the system load averages for the past 1, 5, and 15 minutes
void update_system_load(double load_avg[3]);

// Retrieves the temperature of the CPU
void update_cpu_temp();

// Retrieves the battery status and percentage
void update_battery();

// Retrieves the current date and time
void update_datetime();

// Retrieves the ID of the currently focused window
void update_windowid(char *window_id);

// Calculates the width of the given text
int calculate_text_width(Display *display, GC gc, const char *text);

// Draws the text truncated to fit within the specified maximum width
void draw_truncated_text(Display *display, Window window, GC gc, int x, int y, const char *text, int max_width);

// Draws the text, truncating it if necessary to fit within the specified maximum width
void draw_text(Display *display, Window window, GC gc, int x, int y, const char *text, int max_width);

// Calculates the total width of the text tokens separated by the '|' character
int calculate_total_text_width(Display *display, GC gc, const char *text);

// Draws the text tokens separated by the '|' character
void draw_text_tokens(Display *display, Window window, GC gc, int x, int y, const char *text);

// Draws the text centered within the specified maximum width
void draw_centered_text(Display *display, Window window, GC gc, int x, int y, const char *text, int max_width);

// Declaration for the getloadavg function
int getloadavg(double loadavg[], int nelem);

#endif // OPENBAR_H
