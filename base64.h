#ifndef BASE64_H
#define BASE64_H

#include <stdlib.h>

size_t base64_encode(char *buf, size_t buflen, const char *s);
size_t base64_decode(char *buf, size_t buflen, const char *s);

#endif	/* BASE64_H */
