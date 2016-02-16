#ifndef REQUEST_H
#define REQUEST_H

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

int parse_request(struct request *r, char *req);
const char *find_header(struct request *r, const char *header);

#endif /* REQUEST_H */
