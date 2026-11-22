CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra

.PHONY: all clean
all: memory-bench

memory-bench: memory_bench.c
	$(CC) $(CFLAGS) memory_bench.c -o $@ -lm

clean:
	rm -f memory-bench memory-bench-x86_64 memory-bench-arm64
