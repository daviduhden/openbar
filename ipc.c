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
 * Worker IPC codec and small descriptor helpers.
 *
 * Pure ISO C17: usable by the display process, the network worker and
 * the host test suite.  The wire format is described in openbar.h.
 */

#include "openbar.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

_Static_assert(sizeof(struct net_response) == 65,
    "net_response layout changed");

/*
 * Read exactly n bytes (or until EOF).  Returns the number of bytes
 * read, which is less than n only at EOF, or -1 on error.  Retries on
 * EINTR.
 */
ssize_t
read_full(int fd, void *buf, size_t n)
{
	size_t	 left = n;
	char	*p = buf;

	while (left > 0) {
		ssize_t	r = read(fd, p, left);

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

/* Write n bytes.  Returns n or -1. */
ssize_t
write_full(int fd, const void *buf, size_t n)
{
	size_t		 left = n;
	const char	*p = buf;

	while (left > 0) {
		ssize_t	r = write(fd, p, left);

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

/* Strict IPv4/IPv6 syntax check; 1 valid, 0 otherwise. */
int
valid_ip(const char *s, int family)
{
	unsigned char	buf[sizeof(struct in6_addr)];

	if (s == NULL || s[0] == '\0')
		return 0;
	return inet_pton(family, s, buf) == 1;
}

int
ipc_send_fetch(int fd)
{
	uint8_t	cmd = IPC_CMD_FETCH;

	return write_full(fd, &cmd, sizeof(cmd)) == (ssize_t)sizeof(cmd) ?
	    0 : -1;
}

static void
copybounded(char *dst, const char *src, size_t dstsz)
{
	size_t	len;

	if (dstsz == 0)
		return;
	len = strlen(src);
	if (len >= dstsz)
		len = dstsz - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
}

/*
 * Fill a response frame.  Address strings are copied only when the
 * corresponding status is NET_OK; addresses are always validated
 * before display, but encoding only trusted content keeps invalid data
 * from ever entering the IPC channel.
 */
void
ipc_encode(struct net_response *resp, int status_v4, const char *addr_v4,
    int status_v6, const char *addr_v6)
{
	memset(resp, 0, sizeof(*resp));
	resp->magic = IPC_MAGIC;
	resp->status_v4 = (uint8_t)status_v4;
	resp->status_v6 = (uint8_t)status_v6;
	if (status_v4 == NET_OK && addr_v4 != NULL)
		copybounded(resp->addr_v4, addr_v4, sizeof(resp->addr_v4));
	if (status_v6 == NET_OK && addr_v6 != NULL)
		copybounded(resp->addr_v6, addr_v6, sizeof(resp->addr_v6));
}

/*
 * Validate a response frame.  The magic byte, the status values and
 * both addresses (when their status is NET_OK) are checked; the
 * display process never trusts the worker blindly.  Returns 0 and
 * fills out on success, -1 otherwise.
 */
int
ipc_decode(const unsigned char *buf, size_t len, struct net_response *out)
{
	struct net_response	r;

	if (buf == NULL || len != sizeof(r))
		return -1;
	memcpy(&r, buf, sizeof(r));
	if (r.magic != IPC_MAGIC)
		return -1;
	if (r.status_v4 >= NET_STATUS_NITEMS ||
	    r.status_v6 >= NET_STATUS_NITEMS)
		return -1;
	r.addr_v4[sizeof(r.addr_v4) - 1] = '\0';
	r.addr_v6[sizeof(r.addr_v6) - 1] = '\0';
	if (r.status_v4 == NET_OK && !valid_ip(r.addr_v4, AF_INET))
		return -1;
	if (r.status_v6 == NET_OK && !valid_ip(r.addr_v6, AF_INET6))
		return -1;
	memcpy(out, &r, sizeof(r));
	return 0;
}
