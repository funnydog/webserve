#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t b64dir[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"0123456789+/";

static const uint8_t b64inv[] = {
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64,  0, 64, 64,
	64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
	64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
};

size_t base64_encode(char *buf, size_t buflen, const char *s)
{
	if (!buf)
		buflen = 0;

	size_t len = 0;
	const uint8_t *b = (uint8_t *)s;

	unsigned val;
	unsigned i;
	for (;;) {
		val = 0;
		for (i = 0; i < 3; i++) {
			if (!*b) break;
			val = val << 8 | *b++;
		}
		if (i != 3)
			break;

		len += 4;
		if (len < buflen) {
			*buf++ = b64dir[(val >> 18) & 0x3F];
			*buf++ = b64dir[(val >> 12) & 0x3F];
			*buf++ = b64dir[(val >> 6) & 0x3F];
			*buf++ = b64dir[val & 0x3F];
		}
	}

	if (i == 1) {
		/* 8 bits left => 2 bytes to gen + padding */
		len += 4;
		if (len < buflen) {
			*buf++ = b64dir[(val >> 2) & 0x3F];
			*buf++ = b64dir[(val << 4) & 0x3F];
			*buf++ = '=';
			*buf++ = '=';
		}
	} else if (i == 2) {
		/* 16 bits left => 3 bytes to gen + padding */
		len += 4;
		if (len < buflen) {
			*buf++ = b64dir[(val >> 10) & 0x3F];
			*buf++ = b64dir[(val >> 4) & 0x3F];
			*buf++ = b64dir[(val << 2) & 0x3F];
			*buf++ = '=';
		}
	}

	if (len < buflen)
		*buf = '\0';

	return len;
}

size_t base64_decode(char *buf, size_t buflen, const char *s)
{
	if (!buf)
		buflen = 0;

	size_t len = 0;
	unsigned val = 0;
	for (size_t pos = 0; *s; s++) {
		uint8_t c = b64inv[(uint8_t)*s & 0x7F];
		if (c > 63)
			continue;

		val = val << 6 | c;
		pos++;
		if (pos == 4) {
			len += 3;
			if (len < buflen) {
				*buf++ = val >> 16;
				*buf++ = val >> 8;
				*buf++ = val;
			}
			pos = 0;
			val = 0;
		}
	}
	if (len < buflen)
		*buf = '\0';

	return len;
}
