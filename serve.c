#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "base64.h"
#include "fsdata.h"

#define BIND_PORT 8080
#define MAXCLIENTS 4

static fd_set rfds;
static int cfd[MAXCLIENTS];
static size_t ccnt;

static char rbuf[1024];
static const char *res_200 = "HTTP/1.0 200 OK\r\n";
static const char *res_401 =
	"HTTP/1.0 401 Access Denied\r\n"
	"WWW-Authenticate: Basic realm=\"Server\"\r\n"
	"Content-Length: 0\r\n";

static const char *res_404 = "HTTP/1.0 404 Not Found\r\n";
static const char *res_500 = "HTTP/1.0 500 Server Error\r\n";

static const char *authres = "Authorization: Basic ";

static int server_listen(uint16_t port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		printf("socket call failed\n");
		return -1;
	}

	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		printf("unable to se SO_REUSEADDR\n");
		goto err_close;
	}

	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
	};

	if (bind(fd, (void *)&sa, sizeof(sa)) < 0) {
		printf("bind call failed\n");
		goto err_close;
	}

	if (listen(fd, 20) < 0) {
		printf("listen call failed");
		goto err_close;
	}

	return fd;

err_close:
	close(fd);
	return -1;
}

static void writeall(int fd, const void *data, size_t len)
{
	const uint8_t *buf = data;
	while (len > 0) {
		ssize_t r = write(fd, buf, len);
		if (r <= 0)
			break;

		buf += r;
		len -= r;
	}
}

static int check_basic_auth(const char *req)
{
	char *auth = strstr(req, authres);
	if (!auth)
		return -1;

	auth += strlen(authres);
	char *end = strchr(auth, ' ');
	if (end)
		*end = 0;

	char user[100];
	size_t required = base64_decode(user, sizeof(user), auth);
	if (required >= sizeof(user))
		return -1;

	char *pass = strchr(user, ':');
	if (!pass)
		return -1;

	*pass++ = 0;

	/* TODO: need to be badly improved */
	if (strcmp(user, "root") || strcmp(pass, "password"))
		return -1;

	return 0;
}

static int client_handler(int fd)
{
	ssize_t len = read(fd, rbuf, sizeof(rbuf));
	if (len <= 0)
		return -1;

	/* parse the request */
	rbuf[len] = 0;
	if (strncmp(rbuf, "GET /", 5) != 0) {
		writeall(fd, res_500, strlen(res_500));
		return -1;
	}

	char *url = rbuf + 4;
	char *end = strchr(url, ' ');
	if (end)
		*end = 0;

	if (url+1 == end)
		url = "/index.html";

	/* authentication */
	if (!end || check_basic_auth(end+1) < 0) {
		writeall(fd, res_401, strlen(res_401));
		return -1;
	}

	/* serve the resource */
	struct fsdata *e;
	for (e = rootfs; e->path; e++)
		if (strcmp(url, e->path) == 0)
			break;
	if (e->path) {
		printf("200 %s\n", url);
		writeall(fd, res_200, strlen(res_200));
		writeall(fd, e->data, e->size);
	} else {
		printf("404 %s\n", url);
		writeall(fd, res_404, strlen(res_404));
	}
	return -1;
}

static void webserver_task(void *params)
{
	for (;;) {
		ccnt = 0;
		int sfd = server_listen(BIND_PORT);
		if (sfd < 0) {
			printf("server listen failed\n");
			continue;
		}

		for (;;) {
			int max = sfd;
			FD_ZERO(&rfds);
			FD_SET(sfd, &rfds);
			for (size_t i = 0; i < ccnt; i++) {
				FD_SET(cfd[i], &rfds);
				if (max < cfd[i])
					max = cfd[i];
			}

			if (select(max+1, &rfds, NULL, NULL, NULL) < 0) {
				printf("select failed\n");
				break;
			}

			if (FD_ISSET(sfd, &rfds)) {
				int fd = accept(sfd, NULL, NULL);
				if (fd < 0)
					break;

				if (ccnt >= MAXCLIENTS) {
					printf("max clients reached\n");
					close(fd);
				} else {
					cfd[ccnt] = fd;
					ccnt++;
				}
			}
			for (size_t i = 0; i < ccnt; ) {
				if (FD_ISSET(cfd[i], &rfds) &&
				    client_handler(cfd[i]) < 0) {
					close(cfd[i]);
					ccnt--;
					memmove(cfd + i, cfd + i + 1,
						(ccnt - i)*sizeof(cfd[0]));
				} else {
					i++;
				}
			}
		}

		/* close all the sockets */
		for (size_t i = 0; i < ccnt; i++)
			close(cfd[i]);
		close(sfd);
	}
}

int main(int argc, char *argv[])
{
	webserver_task(NULL);
	return 0;
}
