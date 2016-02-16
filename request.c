#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "request.h"

static const char *reqtype[] = {
	"GET",
	"HEAD",
	"POST",
	"PUT",
	"DELETE",
	"CONNECT",
	"OPTIONS",
	"TRACE",
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

int parse_request(struct request *r, char *req)
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

const char *find_header(struct request *r, const char *header)
{
	for (int i = 0; i < 10; i++) {
		if (r->headers[i] && strstr(r->headers[i], header))
			return r->headers[i];
	}
	return NULL;
}
