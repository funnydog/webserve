CFLAGS = -Wall -Werror -Wshadow -Wstrict-aliasing=2 \
	-Wstrict-overflow -Wno-missing-field-initializers \
	-pedantic -march=native -D_DEFAULT_SOURCE -std=gnu99 -O2 -march=native
LIBS =
SRCS =
OBJS = ${SRCS:.c=.o}
DESTDIR = /usr/local

.PHONY: all clean install fsdata.c

all: serve

fsdata.c: fs
	perl makefsdata.pl

serve: serve.o fsdata.o base64.o
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	@rm -f *.o *~ serve fsdata.c
