/*
 * IPC codec and descriptor helper tests.  Pure C17, host-runnable.
 */

#include "test.h"

#include "../openbar.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void
test_valid_ip(void)
{
	CHECK(valid_ip("203.0.113.7", AF_INET) == 1);
	CHECK(valid_ip("255.255.255.255", AF_INET) == 1);
	CHECK(valid_ip("0.0.0.0", AF_INET) == 1);
	CHECK(valid_ip("2001:db8::1", AF_INET6) == 1);
	CHECK(valid_ip("::1", AF_INET6) == 1);
	CHECK(valid_ip("::", AF_INET6) == 1);

	CHECK(valid_ip("256.0.0.1", AF_INET) == 0);
	CHECK(valid_ip("1.2.3", AF_INET) == 0);
	CHECK(valid_ip("1.2.3.4.5", AF_INET) == 0);
	CHECK(valid_ip("1.2.3.4x", AF_INET) == 0);
	CHECK(valid_ip("1.2.3.4\r", AF_INET) == 0);
	CHECK(valid_ip(" 1.2.3.4", AF_INET) == 0);
	CHECK(valid_ip("", AF_INET) == 0);
	CHECK(valid_ip(NULL, AF_INET) == 0);
	CHECK(valid_ip("203.0.113.7", AF_INET6) == 0);
	CHECK(valid_ip("2001:db8::1", AF_INET) == 0);
	CHECK(valid_ip("::1 extra", AF_INET6) == 0);
	CHECK(valid_ip("2001:db8:::1", AF_INET6) == 0);
	CHECK(valid_ip("gggg::1", AF_INET6) == 0);
}

static void
test_encode_decode_roundtrip(void)
{
	struct net_response	resp;
	unsigned char		buf[sizeof(resp)];

	ipc_encode(&resp, NET_OK, "203.0.113.7", NET_OK, "2001:db8::1");
	CHECK(resp.magic == IPC_MAGIC);
	CHECK(resp.status_v4 == NET_OK);
	CHECK(resp.status_v6 == NET_OK);
	CHECK_STR(resp.addr_v4, "203.0.113.7");
	CHECK_STR(resp.addr_v6, "2001:db8::1");

	memcpy(buf, &resp, sizeof(buf));
	memset(&resp, 0, sizeof(resp));
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == 0);
	CHECK_STR(resp.addr_v4, "203.0.113.7");
	CHECK_STR(resp.addr_v6, "2001:db8::1");
}

static void
test_encode_failures(void)
{
	struct net_response	resp;

	ipc_encode(&resp, NET_ERR_DNS, "1.2.3.4", NET_ERR_CONNECT, "::1");
	CHECK(resp.status_v4 == NET_ERR_DNS);
	CHECK(resp.status_v6 == NET_ERR_CONNECT);
	CHECK_STR(resp.addr_v4, "");
	CHECK_STR(resp.addr_v6, "");
}

static void
test_decode_validation(void)
{
	struct net_response	resp, bad;
	unsigned char		buf[sizeof(resp)];

	ipc_encode(&bad, NET_OK, "1.2.3.4", NET_OK, "::1");
	memcpy(buf, &bad, sizeof(buf));
	memset(&resp, 0, sizeof(resp));
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == 0);

	/* truncated frame */
	CHECK(ipc_decode(buf, sizeof(buf) - 1, &resp) == -1);
	CHECK(ipc_decode(buf, 0, &resp) == -1);
	CHECK(ipc_decode(NULL, sizeof(buf), &resp) == -1);

	/* bad magic */
	memcpy(buf, &bad, sizeof(buf));
	buf[0] = IPC_MAGIC ^ 0xff;
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == -1);

	/* status out of range */
	memcpy(buf, &bad, sizeof(buf));
	buf[0] = IPC_MAGIC;
	buf[1] = NET_STATUS_NITEMS + 1;
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == -1);
	buf[1] = NET_OK;
	buf[2] = 0xff;
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == -1);

	/* invalid address with NET_OK status */
	memcpy(buf, &bad, sizeof(buf));
	buf[0] = IPC_MAGIC;
	buf[1] = NET_OK;
	strcpy((char *)buf + 3, "not an ip");
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == -1);

	/* valid status with an empty address also fails */
	memcpy(buf, &bad, sizeof(buf));
	buf[0] = IPC_MAGIC;
	buf[1] = NET_OK;
	buf[3] = '\0';
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == -1);

	/* address forced to NUL at the last byte is still invalid */
	memcpy(buf, &bad, sizeof(buf));
	buf[0] = IPC_MAGIC;
	buf[1] = NET_OK;
	memset(buf + 3, '1', ADDR4_STRLEN - 1);
	buf[3 + ADDR4_STRLEN - 1] = '\0';
	CHECK(ipc_decode(buf, sizeof(buf), &resp) == -1);
}

static void
test_read_write_full(void)
{
	int	fds[2];
	char	in[8], out[8] = "abcdefg";
	ssize_t	n;

	signal(SIGPIPE, SIG_IGN);
	CHECK(pipe(fds) == 0);
	for (size_t i = 0; i < strlen(out); i++) {
		/* one byte at a time exercises the retry loops */
		CHECK(write_full(fds[1], out + i, 1) == 1);
	}
	close(fds[1]);
	n = read_full(fds[0], in, sizeof(in));
	CHECK(n == 7);
	CHECK(memcmp(in, out, 7) == 0);
	CHECK(read_full(fds[0], in, 1) == 0);	/* EOF */
	close(fds[0]);

	/* write to a closed reader must fail with EPIPE, not hang */
	CHECK(pipe(fds) == 0);
	close(fds[0]);
	CHECK(write_full(fds[1], out, 1) == -1);
	close(fds[1]);
}

static void
test_ipc_send_fetch(void)
{
	int	fds[2];
	uint8_t	cmd = 0;

	CHECK(pipe(fds) == 0);
	CHECK(ipc_send_fetch(fds[1]) == 0);
	CHECK(read(fds[0], &cmd, 1) == 1);
	CHECK(cmd == IPC_CMD_FETCH);
	close(fds[0]);
	close(fds[1]);
}

static void
run_tests(void)
{
	test_valid_ip();
	test_encode_decode_roundtrip();
	test_encode_failures();
	test_decode_validation();
	test_read_write_full();
	test_ipc_send_fetch();
}

TEST_MAIN()
