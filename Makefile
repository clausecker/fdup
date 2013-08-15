LDLIBS=-lcrypto
CFLAGS=-O3 -Wall -Wextra -pedantic -std=c1x -g

all: fdup

.PHONY: all clean

clean:
	$(RM) *.o *.s fdup
