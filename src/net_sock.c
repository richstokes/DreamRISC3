/* BSD sockets for the guest, the same code over KOS and the Mac. Sockets are always
 * non-blocking so a guest call cannot stall the machine. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "net.h"

/* RISC OS numbers these the BSD way, unlike newlib */
#define GUEST_EWOULDBLOCK 35
#define GUEST_EINPROGRESS 36
#define GUEST_EALREADY 37
#define GUEST_EISCONN 56
#define GUEST_ENOTCONN 57
#define GUEST_ENOBUFS 55

static int fail(void)
{
	if (errno == EAGAIN || errno == EWOULDBLOCK)
		return -GUEST_EWOULDBLOCK;
	if (errno == EINPROGRESS)
		return -GUEST_EINPROGRESS;
	if (errno == EALREADY)
		return -GUEST_EALREADY;
	if (errno == EISCONN)
		return -GUEST_EISCONN;
	if (errno == ENOTCONN)
		return -GUEST_ENOTCONN;
	if (errno == ENOBUFS || errno == ENOMEM)
		return -GUEST_ENOBUFS;
	return -errno;
}

static int result(int value)
{
	return value < 0 ? fail() : value;
}

static void fill_addr(struct sockaddr_in *a, const uint8_t ip[4], int port)
{
	memset(a, 0, sizeof(*a));
	a->sin_family = AF_INET;
	a->sin_port = htons((uint16_t)port);
	if (ip)
		memcpy(&a->sin_addr, ip, 4);
}

int net_sock_open(int kind)
{
	int fd = socket(AF_INET, kind == SOCK_KIND_UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
	if (fd < 0)
		return fail();
	if (kind == SOCK_KIND_TCP)
		plat_sock_tune(fd);
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		int code = fail();
		close(fd);
		return code;
	}
	return fd;
}

int net_sock_bind(int fd, int port)
{
	struct sockaddr_in a;
	fill_addr(&a, 0, port);
	return result(bind(fd, (struct sockaddr *)&a, sizeof(a)));
}

int net_sock_connect(int fd, const uint8_t ip[4], int port)
{
	struct sockaddr_in a;
	fill_addr(&a, ip, port);
	return result(connect(fd, (struct sockaddr *)&a, sizeof(a)));
}

int net_sock_send(int fd, const void *data, int len)
{
	return result((int)send(fd, data, (size_t)len, 0));
}

int net_sock_recv(int fd, void *data, int len)
{
	return result((int)recv(fd, data, (size_t)len, 0));
}

int net_sock_sendto(int fd, const void *data, int len, const uint8_t ip[4], int port)
{
	struct sockaddr_in a;
	fill_addr(&a, ip, port);
	return result((int)sendto(fd, data, (size_t)len, 0, (struct sockaddr *)&a, sizeof(a)));
}

int net_sock_recvfrom(int fd, void *data, int len, uint8_t ip[4], int *port)
{
	struct sockaddr_in a;
	socklen_t size = sizeof(a);
	int got = (int)recvfrom(fd, data, (size_t)len, 0, (struct sockaddr *)&a, &size);
	if (got < 0)
		return fail();
	memcpy(ip, &a.sin_addr, 4);
	*port = ntohs(a.sin_port);
	return got;
}

int net_sock_close(int fd)
{
	return result(close(fd));
}
