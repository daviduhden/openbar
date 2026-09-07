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
 * Public-address network worker.
 *
 * This process is forked before the display process sandboxes itself
 * and is the only process that keeps 'inet' and 'dns'.  It fetches the
 * public IPv4 and IPv6 addresses over HTTPS using libtls, validates
 * the responses with inet_pton(3) and reports through the IPC channel
 * described in openbar.h.
 *
 * Sandbox: filesystem visibility is locked to the resolver and TLS
 * certificate files; pledge(2) restricts the process to
 * "stdio inet dns".  libtls/libcrypto are pure userland code and need
 * no additional promises.
 *
 * Threat model: TLS authenticates ifconfig.me, so an on-path attacker
 * cannot substitute another address for the real response without a
 * valid certificate.  The service itself (and its DNS records) are
 * still trusted for correctness.
 */

#include "openbar.h"

#include <sys/socket.h>

#include <netdb.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <tls.h>
#include <unistd.h>

#define PUBLIC_HOST		"ifconfig.me"
#define PUBLIC_PORT		"443"
#define PUBLIC_PATH		"/ip"
#define REQUEST_TIMEOUT		5	/* seconds, per socket operation */
#define RESPONSE_MAX		1024	/* bound on the whole HTTP reply */

static int	http_fetch(int, char *, size_t);
static int	tcp_connect(struct addrinfo *);
static int	set_timeouts(int);
static int	parse_response(const char *, int, char *, size_t);
static int	unveil_worker(void);

/*
 * Open a TCP connection to one address of the chain, bounding the
 * connection establishment with poll(2) so that unreachable hosts
 * cannot stall the worker indefinitely.
 */
static int
tcp_connect(struct addrinfo *ai)
{
	struct pollfd	pfd;
	socklen_t	errlen;
	int		fd, flags, err;

	fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (fd == -1)
		return -1;
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1 ||
	    (flags = fcntl(fd, F_GETFL)) == -1 ||
	    fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		goto fail;
	if (connect(fd, ai->ai_addr, ai->ai_addrlen) == -1 &&
	    errno != EINPROGRESS)
		goto fail;

	pfd.fd = fd;
	pfd.events = POLLOUT;
	pfd.revents = 0;
	if (poll(&pfd, 1, REQUEST_TIMEOUT * 1000) != 1)
		goto fail;
	errlen = sizeof(err);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1 ||
	    err != 0)
		goto fail;
	if (fcntl(fd, F_SETFL, flags) == -1)
		goto fail;
	return fd;
fail:
	close(fd);
	return -1;
}

static int
set_timeouts(int fd)
{
	struct timeval	tv = { .tv_sec = REQUEST_TIMEOUT };

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
		return -1;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1)
		return -1;
	return 0;
}

/*
 * Fetch PUBLIC_PATH for the given address family.  Returns a net
 * status code; on NET_OK, out holds the validated address.
 */
static int
http_fetch(int family, char *out, size_t outlen)
{
	struct addrinfo	 hints, *res, *ai;
	struct tls_config *tls_cfg;
	struct tls	*ctx;
	char		 buf[RESPONSE_MAX];
	const char	 req[] =
	    "GET /ip HTTP/1.1\r\nHost: ifconfig.me\r\nConnection: close\r\n\r\n";
	size_t		 total;
	int		 sockfd, rc;
	ssize_t		 n;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(PUBLIC_HOST, PUBLIC_PORT, &hints, &res) != 0)
		return NET_ERR_DNS;

	sockfd = -1;
	for (ai = res; ai != NULL; ai = ai->ai_next) {
		sockfd = tcp_connect(ai);
		if (sockfd == -1)
			continue;
		if (set_timeouts(sockfd) == -1) {
			close(sockfd);
			sockfd = -1;
			continue;
		}
		break;
	}
	freeaddrinfo(res);
	if (sockfd == -1)
		return NET_ERR_CONNECT;

	tls_cfg = tls_config_new();
	ctx = tls_client();
	if (tls_cfg == NULL || ctx == NULL) {
		tls_config_free(tls_cfg);
		tls_free(ctx);
		close(sockfd);
		return NET_ERR_SYS;
	}
	if (tls_configure(ctx, tls_cfg) == -1 ||
	    tls_connect_socket(ctx, sockfd, PUBLIC_HOST) == -1) {
		tls_config_free(tls_cfg);
		tls_free(ctx);
		close(sockfd);
		return NET_ERR_CONNECT;
	}
	tls_config_free(tls_cfg);

	total = 0;
	while (total < sizeof(req) - 1) {
		n = tls_write(ctx, req + total, sizeof(req) - 1 - total);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			rc = NET_ERR_SYS;
			goto done;
		}
		if (n == 0) {
			rc = NET_ERR_SYS;
			goto done;
		}
		total += (size_t)n;
	}

	total = 0;
	for (;;) {
		if (total == sizeof(buf) - 1) {
			rc = NET_ERR_PARSE;	/* oversized response */
			goto done;
		}
		n = tls_read(ctx, buf + total, sizeof(buf) - 1 - total);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			rc = NET_ERR_RECV;	/* includes timeouts */
			goto done;
		}
		if (n == 0)
			break;
		total += (size_t)n;
	}
	buf[total] = '\0';
	rc = parse_response(buf, family, out, outlen);
done:
	tls_close(ctx);
	tls_free(ctx);
	close(sockfd);
	return rc;
}

/*
 * Extract the body of a 200 response, trim surrounding whitespace,
 * bound its length and require it to be exactly one address of the
 * requested family.  Never displays unvalidated remote data.
 */
static int
parse_response(const char *buf, int family, char *out, size_t outlen)
{
	const char	*body, *end;
	size_t		 len;

	if (strncmp(buf, "HTTP/1.1 200", 12) != 0 &&
	    strncmp(buf, "HTTP/1.0 200", 12) != 0)
		return NET_ERR_PARSE;
	body = strstr(buf, "\r\n\r\n");
	if (body == NULL)
		return NET_ERR_PARSE;
	body += 4;
	end = body + strlen(body);
	while (end > body && (end[-1] == '\r' || end[-1] == '\n' ||
	    end[-1] == ' ' || end[-1] == '\t'))
		end--;
	len = (size_t)(end - body);
	if (len == 0 || len >= outlen)
		return NET_ERR_PARSE;
	memcpy(out, body, len);
	out[len] = '\0';
	return valid_ip(out, family) ? NET_OK : NET_ERR_PARSE;
}

void
net_worker(int fd)
{
	char		 a4[ADDR4_STRLEN], a6[ADDR6_STRLEN];
	struct net_response resp;
	uint8_t		 cmd;
	int		 s4, s6;

	signal(SIGPIPE, SIG_IGN);
	if (tls_init() == -1)
		err(1, "tls_init");

	for (;;) {
		if (read_full(fd, &cmd, sizeof(cmd)) != (ssize_t)sizeof(cmd))
			_exit(0);
		if (cmd != IPC_CMD_FETCH)
			continue;
		s4 = http_fetch(AF_INET, a4, sizeof(a4));
		s6 = http_fetch(AF_INET6, a6, sizeof(a6));
		ipc_encode(&resp, s4, s4 == NET_OK ? a4 : NULL,
		    s6, s6 == NET_OK ? a6 : NULL);
		if (write_full(fd, &resp, sizeof(resp)) !=
		    (ssize_t)sizeof(resp))
			_exit(0);
	}
}

/*
 * Worker filesystem policy: the resolver configuration, /etc/hosts and
 * the TLS CA bundle.  The service port is numeric ("443"), so
 * /etc/services is not consulted.
 */
static int
unveil_worker(void)
{
	if (unveil("/etc/resolv.conf", "r") == -1)
		return -1;
	if (unveil("/etc/hosts", "r") == -1)
		return -1;
	if (unveil("/etc/ssl/cert.pem", "r") == -1)
		return -1;
	if (unveil(NULL, NULL) == -1)
		return -1;
	return 0;
}

/*
 * Create the worker.  Called by the display process before it pledges.
 * The child closes the parent's IPC end and the standard input and
 * output (stderr stays for diagnostics), locks its filesystem view and
 * drops to "stdio inet dns" before serving requests.
 */
int
net_worker_start(struct openbar *app)
{
	int	sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
		return -1;
	app->ipc_pid = fork();
	if (app->ipc_pid == -1) {
		close(sv[0]);
		close(sv[1]);
		return -1;
	}
	if (app->ipc_pid == 0) {
		struct sigaction	sa;

		/* drop the parent's signal handlers so the worker can
		 * be terminated normally */
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);

		close(sv[1]);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		if (unveil_worker() == -1)
			_exit(1);
		if (pledge("stdio inet dns", NULL) == -1)
			err(1, "pledge: network worker");
		net_worker(sv[0]);
		_exit(0);
	}
	close(sv[0]);
	app->ipc_fd = sv[1];
	return 0;
}
