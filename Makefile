CFLAGS = -Wall -Werror -Wshadow -Wstrict-aliasing=2 \
	-Wstrict-overflow -Wno-missing-field-initializers \
	-pedantic -march=native -D_DEFAULT_SOURCE -std=gnu99 \
	-O2 -march=native -I/usr/local/include
LIBS = -lmbedtls -lmbedcrypto -lmbedx509 -lcrypt -L/usr/local/lib
SRCS = serve.c fsdata.c base64.c certs.c request.c
OBJS = ${SRCS:.c=.o}
DESTDIR = /usr/local

.PHONY: all clean install fsdata.c

all: serve

fsdata.c: fs
	perl makefsdata.pl

serve: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LIBS)

clean:
	@rm -f *.o *~ serve fsdata.c
