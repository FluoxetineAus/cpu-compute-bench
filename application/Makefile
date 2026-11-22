CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -pthread

.PHONY: all clean
all: application-bench

application-bench: application_bench.c
	$(CC) $(CFLAGS) application_bench.c -o $@ -lm

clean:
	rm -f application-bench application-bench-x86_64 application-bench-arm64
