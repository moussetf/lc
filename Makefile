.PHONY: all clean check

all: lc

CFLAGS = -g -Wall

lc: lc.c
	$(CC) $(CFLAGS) -o $@ $< -lreadline

check: lc
	./tests/tests.sh

clean:
	rm -f lc
