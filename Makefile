CC       ?= cc
MACHINE  := $(shell uname -m)

# Native code generation is what makes a local benchmark meaningful.  Apple
# Clang does not accept -march=native on arm64, so use the default ARMv8 code
# generation there and keep -march=native on x86.
ifeq ($(MACHINE),arm64)
ARCHFLAGS ?= -O3
else
ARCHFLAGS ?= -O3 -march=native
endif

CFLAGS  ?= $(ARCHFLAGS) -fno-math-errno -ffp-contract=fast
CFLAGS  += -std=c11 -Wall -Wextra -pthread

# Keep scalar integer kernels scalar; the FP kernels use explicit vector types.
ifeq ($(shell $(CC) --version 2>/dev/null | grep -ci clang),1)
VECFLAGS = -fno-vectorize -fno-slp-vectorize
else
VECFLAGS = -fno-tree-vectorize
endif

ALL_FLAGS = $(CFLAGS) $(VECFLAGS) -DBENCH_FLAGS='"$(CFLAGS) $(VECFLAGS)"'

.PHONY: all run clean

all: cpu-bench

cpu-bench: bench.c Makefile
	$(CC) $(ALL_FLAGS) bench.c -o $@ -lm

run: cpu-bench
	./cpu-bench

clean:
	rm -f cpu-bench
