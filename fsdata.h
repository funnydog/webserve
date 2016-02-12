#ifndef FSDATA_H
#define FSDATA_H

#include <stdlib.h>

struct fsdata {
	const char *path;
	const void *data;
	size_t size;
};

extern struct fsdata rootfs[];

#endif	/* FSDATA_H */
