#ifndef CERTS_H
#define CERTS_H

#include <stdint.h>
#include <stdlib.h>

extern const uint8_t ca_cert[];
extern const size_t ca_cert_len;

extern const uint8_t server_cert[];
extern const size_t server_cert_len;

extern const uint8_t server_key[];
extern const size_t server_key_len;

#endif	/* CERTS_H */
