#define _XOPEN_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <arpa/inet.h>
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
#include "request.h"

#define MAXCLIENTS 4

static const char *seed = "this is a secret!!1";

/* username: root
 * password: password
 */
static const char *username = "root";
static const char *pwdhash =
	"$6$cleverboy$Y5dVMplYCby7rYFbzMSvsdCUJOM7qWMfqzf2LOZ"
	"vi/sITnuMzYTj0kd8Z/JWiR0Gmt/VmNQ7v1AcqtoA8XVV8.";

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
static struct server
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

static void tls_debug(void *ctx, int level, const char *file, int line, const char *str)
{
	printf("%s:%04d: %s", file, line, str);
}

static int tls_server_listen(const char *host, const char *port)
{
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

	ret = mbedtls_net_bind(&stls.fd, host, port, MBEDTLS_NET_PROTO_TCP);
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

	if (strcmp(user, username) || strcmp(crypt(pass, pwdhash), pwdhash))
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

static void tls_client_free(struct client *c)
{
	mbedtls_ssl_close_notify(&c->ssl);
	mbedtls_net_free(&c->fd);
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

static void tls_server_init(struct server *s)
{
	mbedtls_net_init(&s->fd);
	mbedtls_entropy_init(&s->entropy);
	mbedtls_ctr_drbg_init(&s->ctr_drbg);
	mbedtls_ssl_config_init(&s->conf);
	mbedtls_x509_crt_init(&s->cacert);
	mbedtls_x509_crt_init(&s->srvcert);
	mbedtls_pk_init(&s->pkey);
}

static void tls_server_free(struct server *s)
{
	mbedtls_net_free(&s->fd);
	mbedtls_pk_free(&s->pkey);
	mbedtls_x509_crt_free(&s->srvcert);
	mbedtls_x509_crt_free(&s->cacert);
	mbedtls_ssl_config_free(&s->conf);
	mbedtls_ctr_drbg_free(&s->ctr_drbg);
	mbedtls_entropy_free(&s->entropy);
}

static void webserver_task(void *params)
{
	for (;;) {
		ccnt = 0;
		tls_server_init(&stls);
		int sfd = tls_server_listen("0", "8443");
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
					tls_client_free(ctls + i);
					ccnt--;
					memmove(ctls + i, ctls + i + 1,
						(ccnt - i)*sizeof(ctls[0]));
				} else {
					i++;
				}
			}
		}

		/* close all the clients */
		for (size_t i = 0; i < ccnt; i++)
			tls_client_free(ctls + i);

		/* free the structures */
		tls_server_free(&stls);
	}
}

int main(int argc, char *argv[])
{
	webserver_task(NULL);
	return 0;
}
