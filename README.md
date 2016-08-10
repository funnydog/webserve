## webserve

Webserve is a very small webserver written in C as a proof-of-concept.

It supports TLS using mbedtls. Rough edges everywhere.

## Motivation

The project explores how to write a low footprint webserver with TLS
support, that may be suitable for embedding in an RTOS.

## Build instructions

Put your static files in fs/ and type:

```
$ make
```

## Installation

There is no installation for now, run the executable in place.

## BUGS

Probably many. Use at your own risk, better in an isolated
environment.

Enjoy. :D
