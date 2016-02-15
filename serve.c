#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <mbedtls/config.h>
#include <mbedtls/certs.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net.h>
#include <mbedtls/ssl.h>

#include "certs.h"
#include "base64.h"
#include "fsdata.h"

#define MAXCLIENTS 4

static const char *seed = "this is a secret!!1";

static struct client
{
	mbedtls_net_context fd;
	mbedtls_ssl_context ssl;
	int (*handler)(struct client *);
} ctls[MAXCLIENTS];
static size_t ccnt;

static fd_set rfds;
static char rbuf[1024];

/* TLS variables */
static struct
{
	mbedtls_net_context fd;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cacert;
	mbedtls_x509_crt srvcert;
	mbedtls_pk_context pkey;
} stls;

static const char *res_200 = "HTTP/1.0 200 OK\r\n";
static const char *res_401 =
	"HTTP/1.0 401 Access Denied\r\n"
	"WWW-Authenticate: Basic realm=\"Server\"\r\n"
	"Content-Length: 0\r\n";

static const char *res_404 = "HTTP/1.0 404 Not Found\r\n";
static const char *res_500 = "HTTP/1.0 500 Server Error\r\n";
static const char *authres = "Authorization: Basic ";

const char *reqtype[] = {
	"GET",
	"HEAD",
	"POST",
	"PUT",
	"DELETE",
	"CONNECT",
	"OPTIONS",
	"TRACE",
};

enum {
	REQ_GET = 0,
	REQ_HEAD,
	REQ_POST,
	REQ_PUT,
	REQ_DELETE,
	REQ_CONNECT,
	REQ_OPTIONS,
	REQ_TRACE,
	REQ_MAX,
};

struct request
{
	int type;
	const char *uri;
	const char *headers[10];
	const char *message;
};

static char *chomp(char **start)
{
	char *line = *start;
	char *end = strchr(line, '\r');
	if (!end) {
		end = strchr(line, '\n');
		if (!end)
			return NULL;
	}
	while (*end == '\r' || *end == '\n')
		*end++ = 0;

	*start = end;
	return line;
}

static int parse_request(struct request *r, char *req)
{
	/* type & uri */
	char *line = chomp(&req);
	if (!line)
		return -1;

	int i;
	for (i = 0; i < REQ_MAX; i++) {
		if (strncmp(line, reqtype[i], strlen(reqtype[i])) == 0)
			break;
	}
	if (i == REQ_MAX)
		return -1;

	r->type = i;
	line += strlen(reqtype[i]);
	while (isspace(*line))
		line++;

	char *end = line;
	while (*end && !isspace(*end))
		end++;
	if (*end == 0)
		return -1;

	*end = 0;
	r->uri = line;

	/* headers */
	i = 0;
	while ((line = chomp(&req))) {
		if (line[0] == 0)
			break;

		if (i < 10) {
			r->headers[i] = line;
			i++;
		}
	}
	for (; i < 10; i++)
		r->headers[i] = NULL;

	/* message */
	r->message = line ? req : NULL;

#if 0
	printf("type: %s\nuri: %s\n", reqtype[r->type], r->uri);
	for (i = 0; i < 10; i++) {
		if (r->headers[i])
			printf("header: %s\n", r->headers[i]);
	}
	printf("message: %s\n", r->message);
#endif

	return 0;
}

static void tls_debug(void *ctx, int level, const char *file, int line, const char *str)
{
	printf("%s:%04d: %s\n", file, line, str);
}

static int tls_server_listen(const char *port)
{
	mbedtls_net_init(&stls.fd);
	mbedtls_entropy_init(&stls.entropy);
	mbedtls_ctr_drbg_init(&stls.ctr_drbg);
	mbedtls_ssl_config_init(&stls.conf);
	mbedtls_x509_crt_init(&stls.cacert);
	mbedtls_x509_crt_init(&stls.srvcert);
	mbedtls_pk_init(&stls.pkey);

	int ret = mbedtls_ctr_drbg_seed(
		&stls.ctr_drbg,
		mbedtls_entropy_func,
		&stls.entropy,
		(uint8_t*)seed, strlen(seed));
	if (ret)
		return -1;

	ret = mbedtls_x509_crt_parse(&stls.cacert, ca_cert, ca_cert_len);
	if (ret) {
		printf("cacert parse failed\n");
		return -1;
	}

	ret = mbedtls_x509_crt_parse(&stls.srvcert, server_cert, server_cert_len);
	if (ret) {
		printf("servercert parse failed\n");
		return -1;
	}

	ret = mbedtls_pk_parse_key(&stls.pkey, server_key, server_key_len, NULL, 0);
	if (ret) {
		printf("key parse failed\n");
		return -1;
	}

	ret = mbedtls_ssl_config_defaults(
		&stls.conf,
		MBEDTLS_SSL_IS_SERVER,
		MBEDTLS_SSL_TRANSPORT_STREAM,
		MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret) {
		printf("defaults failed\n");
		return -1;
	}
	mbedtls_ssl_conf_rng(&stls.conf, mbedtls_ctr_drbg_random, &stls.ctr_drbg);
	mbedtls_ssl_conf_dbg(&stls.conf, tls_debug, NULL);
	mbedtls_debug_set_threshold(1);

	mbedtls_ssl_conf_ca_chain(&stls.conf, stls.cacert.next, NULL);
	ret = mbedtls_ssl_conf_own_cert(&stls.conf, &stls.srvcert, &stls.pkey);
	if (ret) {
		printf("setting own cert failed\n");
		return -1;
	}

	ret = mbedtls_net_bind(&stls.fd, NULL, port, MBEDTLS_NET_PROTO_TCP);
	if (ret) {
		printf("bind failed\n");
		return -1;
	}

	return stls.fd.fd;
}

static int tls_client_write(struct client *c, const void *data, size_t len)
{
	const uint8_t *buf = data;
	while (len > 0) {
		ssize_t r = mbedtls_ssl_write(&c->ssl, buf, len);
		if (r <= 0)
			return -1;

		buf += r;
		len -= r;
	}

	return 0;
}

static int check_basic_auth(struct request *r)
{
	char *auth = NULL;
	for (int i = 0; i < 10; i++) {
		if (r->headers[i]) {
			auth = strstr(r->headers[i], authres);
			if (auth)
				break;
		}
	}
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

static int tls_client_read(struct client *c)
{
	ssize_t len = mbedtls_ssl_read(&c->ssl, (uint8_t *)rbuf, sizeof(rbuf));
	if (len <= 0)
		return -1;

	struct request req;
	if (parse_request(&req, rbuf) < 0)
		goto err_500;

	if (req.type != REQ_GET)
		goto err_500;

	if (strcmp(req.uri, "/") == 0)
		req.uri = "/index.html";

	if (check_basic_auth(&req) < 0) {
		tls_client_write(c, res_401, strlen(res_401));
		return -1;
	}

	/* serve the resource */
	struct fsdata *e;
	for (e = rootfs; e->path; e++)
		if (strcmp(req.uri, e->path) == 0)
			break;
	if (e->path) {
		printf("200 %s\n", req.uri);
		tls_client_write(c, res_200, strlen(res_200));
		tls_client_write(c, e->data, e->size);
	} else {
		printf("404 %s\n", req.uri);
		tls_client_write(c, res_404, strlen(res_404));
	}
	return -1;

err_500:
	printf("500\n");
	tls_client_write(c, res_500, strlen(res_500));
	return -1;
}

static int tls_client_handshake(struct client *c)
{
	int ret = mbedtls_ssl_handshake(&c->ssl);
	if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
	    ret == MBEDTLS_ERR_SSL_WANT_WRITE)
		return 0;

	if (ret) {
		printf("handshake failed\n");
		return -1;
	}

	c->handler = tls_client_read;
	return 0;
}

static int tls_server_accept(void)
{
	int ret;
	mbedtls_net_context client;

	mbedtls_net_init(&client);
	ret = mbedtls_net_accept(&stls.fd, &client, NULL, 0, NULL);
	if (ret || ccnt >= MAXCLIENTS) {
		printf("max clients\n");
		goto err;
	}

	struct client *c = ctls + ccnt;
	c->fd = client;
	mbedtls_ssl_init(&c->ssl);

	ret = mbedtls_ssl_setup(&c->ssl, &stls.conf);
	if (ret) {
		printf("cannot setup the ssl session\n");
		goto err;
	}

	mbedtls_ssl_set_bio(&c->ssl, &c->fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	c->handler = tls_client_handshake;
	ccnt++;
	return 0;

err:
	mbedtls_net_free(&client);
	return -1;
}

static void webserver_task(void *params)
{
	for (;;) {
		ccnt = 0;
		int sfd = tls_server_listen("8443");
		if (sfd < 0) {
			printf("server listen failed\n");
			continue;
		}

		for (;;) {
			int max = sfd;
			FD_ZERO(&rfds);
			FD_SET(sfd, &rfds);
			for (size_t i = 0; i < ccnt; i++) {
				FD_SET(ctls[i].fd.fd, &rfds);
				if (max < ctls[i].fd.fd)
					max = ctls[i].fd.fd;
			}

			if (select(max+1, &rfds, NULL, NULL, NULL) < 0) {
				printf("select failed\n");
				break;
			}

			if (FD_ISSET(sfd, &rfds)) {
				if (tls_server_accept() < 0)
					break;
			}
			for (size_t i = 0; i < ccnt; ) {
				if (FD_ISSET(ctls[i].fd.fd, &rfds) &&
				    ctls[i].handler(ctls+i) < 0) {
					mbedtls_ssl_close_notify(&ctls[i].ssl);
					mbedtls_net_free(&ctls[i].fd);
					ccnt--;
					memmove(ctls + i, ctls + i + 1,
						(ccnt - i)*sizeof(ctls[0]));
				} else {
					i++;
				}
			}
		}

		/* close all the sockets */
		for (size_t i = 0; i < ccnt; i++) {
			mbedtls_ssl_close_notify(&ctls[i].ssl);
			mbedtls_net_free(&ctls[i].fd);
		}

		/* free the structures */
		mbedtls_net_free(&stls.fd);
		mbedtls_pk_free(&stls.pkey);
		mbedtls_x509_crt_free(&stls.srvcert);
		mbedtls_x509_crt_free(&stls.cacert);
		mbedtls_ssl_config_free(&stls.conf);
		mbedtls_ctr_drbg_free(&stls.ctr_drbg);
		mbedtls_entropy_free(&stls.entropy);
	}
}

int main(int argc, char *argv[])
{
	webserver_task(NULL);
	return 0;
}
