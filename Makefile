CC       ?= cc
MACHINE  := $(shell uname -m)

ifeq ($(MACHINE),arm64)
ARCHFLAGS ?= -O3
else
ARCHFLAGS ?= -O3 -march=native
endif

CFLAGS  ?= $(ARCHFLAGS) -fno-math-errno -ffp-contract=fast
CFLAGS  += -std=c11 -Wall -Wextra -pthread

# Keep integer kernels scalar; FP kernels use explicit vector types.
ifeq ($(shell $(CC) --version 2>/dev/null | grep -ci clang),1)
VECFLAGS = -fno-vectorize -fno-slp-vectorize
else
VECFLAGS = -fno-tree-vectorize
endif

ALL_FLAGS = $(CFLAGS) $(VECFLAGS) -DBENCH_FLAGS='"$(CFLAGS) $(VECFLAGS)"'

.PHONY: all cpu application memory run clean

all: cpu application memory

cpu: cpu-bench

application: application/application-bench

memory: memory/memory-bench

cpu-bench: bench.c Makefile
	$(CC) $(ALL_FLAGS) bench.c -o $@ -lm

application/application-bench: application/application_bench.c application/Makefile
	$(MAKE) -C application

memory/memory-bench: memory/memory_bench.c memory/Makefile
	$(MAKE) -C memory

run: cpu
	./cpu-bench

clean:
	rm -f cpu-bench
	$(MAKE) -C application clean
	$(MAKE) -C memory clean
