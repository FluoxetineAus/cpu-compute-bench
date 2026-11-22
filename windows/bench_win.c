/*
 * CPU Compute Benchmark
 * ---------------------
 * A small, self-contained benchmark for sustained scalar/vector compute
 * throughput.  The measured loops are register-resident: they do not stream
 * large arrays, touch the disk, use the GPU, or allocate memory while timed.
 *
 * Windows build (MinGW-w64 / llvm-mingw):
 *   x86_64-w64-mingw32-clang -O3 -march=haswell ... bench_win.c
 * Run:    Run-CPU-Bench.bat
 *
 * The score is deliberately relative to a documented reference machine.  Raw
 * throughput is always printed next to the score, so the numbers remain useful
 * even if the reference constants are changed.
 */

#define _CRT_SECURE_NO_WARNINGS

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#include <intrin.h>
#endif

#ifndef BENCH_FLAGS
#define BENCH_FLAGS "unknown"
#endif

#define BENCH_VERSION "2.0"
#define MAX_THREADS 256

#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
#define FP64_SQRT_BATCH 32
#else
#define FP64_SQRT_BATCH 8
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

/*
 * If the benchmark hits an access violation, illegal instruction or another
 * SEH exception, keep the window open and leave a small log next to the exe.
 * This turns a silent "flash and close" into an actionable error message.
 */
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *info)
{
    DWORD code = 0;
    void *address = NULL;
    if (info && info->ExceptionRecord) {
        code = info->ExceptionRecord->ExceptionCode;
        address = info->ExceptionRecord->ExceptionAddress;
    }

    char log_path[MAX_PATH] = "cpu-bench-crash.log";
    char module[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, module, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        char *slash = strrchr(module, '\\');
        if (slash) {
            strcpy(slash + 1, "cpu-bench-crash.log");
            strncpy(log_path, module, MAX_PATH - 1);
            log_path[MAX_PATH - 1] = '\0';
        }
    }

    FILE *fp = fopen(log_path, "a");
    if (!fp) {
        char tmp[MAX_PATH];
        DWORD t = GetTempPathA(MAX_PATH, tmp);
        if (t > 0 && t < MAX_PATH) {
            snprintf(log_path, sizeof(log_path), "%scpu-bench-crash.log", tmp);
            fp = fopen(log_path, "a");
        }
    }
    if (fp) {
        fprintf(fp, "CPU Compute Benchmark crash\n");
        fprintf(fp, "exception=0x%08lX address=%p\n",
                (unsigned long)code, address);
        fclose(fp);
    }

    char message[512];
    snprintf(message, sizeof(message),
             "Benchmark crashed.\n\nException: 0x%08lX\nAddress: %p\n\n"
             "A log was written to:\n%s",
             (unsigned long)code, address, log_path);

    fprintf(stderr, "\n*** CRASH  exception=0x%08lX address=%p ***\n",
            (unsigned long)code, address);
    fprintf(stderr, "*** log: %s ***\n", log_path);
    fflush(stdout);
    fflush(stderr);

    MessageBoxA(NULL, message, "CPU Compute Benchmark - Crash",
                MB_OK | MB_ICONERROR);

    printf("\nPress Enter to close...");
    fflush(stdout);
    (void)getchar();
    return EXCEPTION_EXECUTE_HANDLER;
}

/* ------------------------------------------------------------------------- */
/* Compiler barriers                                                         */
/*                                                                           */
/* These are empty asm statements.  They add no instructions to the hot loop,*/
/* but they force the compiler to materialise each accumulator at the end of */
/* every iteration.  Without them LLVM can turn the simple integer loops into */
/* closed-form expressions.                                                  */
/* ------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define KEEP8U(a, b, c, d, e, f, g, h)                                      \
    __asm__ __volatile__("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d),            \
                         "+r"(e), "+r"(f), "+r"(g), "+r"(h))
#else
#define KEEP8U(a, b, c, d, e, f, g, h) do { (void)(a); } while (0)
#endif

#if defined(__AVX__) && (defined(__x86_64__) || defined(__i386__))
#define KEEP8V(a, b, c, d, e, f, g, h)                                      \
    __asm__ __volatile__("" : "+x"(a), "+x"(b), "+x"(c), "+x"(d),            \
                         "+x"(e), "+x"(f), "+x"(g), "+x"(h))
#else
#define KEEP8V(a, b, c, d, e, f, g, h) do { (void)(a); } while (0)
#endif

#if defined(__x86_64__) || defined(__i386__)
#define KEEP8D(a, b, c, d, e, f, g, h)                                      \
    __asm__ __volatile__("" : "+x"(a), "+x"(b), "+x"(c), "+x"(d),            \
                         "+x"(e), "+x"(f), "+x"(g), "+x"(h))
#elif defined(__aarch64__)
#define KEEP8D(a, b, c, d, e, f, g, h)                                      \
    __asm__ __volatile__("" : "+w"(a), "+w"(b), "+w"(c), "+w"(d),            \
                         "+w"(e), "+w"(f), "+w"(g), "+w"(h))
#else
#define KEEP8D(a, b, c, d, e, f, g, h) do { (void)(a); } while (0)
#endif

/* ------------------------------------------------------------------------- */
/* Timing                                                                    */
/* ------------------------------------------------------------------------- */

static uint64_t now_ns(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;

    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);

    uint64_t f = (uint64_t)freq.QuadPart;
    uint64_t c = (uint64_t)counter.QuadPart;
    if (f == 0)
        return 0;
    return (c / f) * 1000000000ULL + ((c % f) * 1000000000ULL) / f;
}

static volatile uint64_t g_checksum_sink;

/* ------------------------------------------------------------------------- */
/* Workloads                                                                 */
/*                                                                           */
/* Each kernel executes a fixed number of architectural operations per       */
/* iteration and returns a checksum.  The checksum makes the result visible to*/
/* the compiler and to this program without adding memory traffic to the loop.*/
/* ------------------------------------------------------------------------- */

static NOINLINE uint64_t int_add_kernel(uint64_t iters)
{
    uint64_t a0 = 0x0123456789abcdefULL;
    uint64_t a1 = 0xfedcba9876543210ULL;
    uint64_t a2 = 0x1111111111111111ULL;
    uint64_t a3 = 0x2222222222222222ULL;
    uint64_t a4 = 0x3333333333333333ULL;
    uint64_t a5 = 0x4444444444444444ULL;
    uint64_t a6 = 0x5555555555555555ULL;
    uint64_t a7 = 0x6666666666666666ULL;

    for (uint64_t i = 0; i < iters; i++) {
        a0 += 0x1f123bb5U;
        a1 += 0x2a3b4c5dU;
        a2 += 0x13579bdfU;
        a3 += 0x2468ace1U;
        a4 += 0x0badf00dU;
        a5 += 0x31415927U;
        a6 += 0x27182819U;
        a7 += 0x11235813U;
        KEEP8U(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    return a0 ^ (a1 << 1) ^ (a2 << 2) ^ (a3 << 3) ^
           (a4 << 4) ^ (a5 << 5) ^ (a6 << 6) ^ (a7 << 7);
}

static NOINLINE uint64_t int_mul_kernel(uint64_t iters)
{
    uint64_t a0 = 0x0123456789abcdefULL;
    uint64_t a1 = 0xfedcba9876543210ULL;
    uint64_t a2 = 0x1111111111111111ULL;
    uint64_t a3 = 0x2222222222222222ULL;
    uint64_t a4 = 0x3333333333333333ULL;
    uint64_t a5 = 0x4444444444444444ULL;
    uint64_t a6 = 0x5555555555555555ULL;
    uint64_t a7 = 0x6666666666666666ULL;

    for (uint64_t i = 0; i < iters; i++) {
        a0 *= 0x9e3779b1U;
        a1 *= 0x85ebca6bU;
        a2 *= 0xc2b2ae35U;
        a3 *= 0x27d4eb2fU;
        a4 *= 0x165667b1U;
        a5 *= 0x9e3779b9U;
        a6 *= 0x7f4a7c15U;
        a7 *= 0x94d049bbU;
        KEEP8U(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    return a0 ^ (a1 << 1) ^ (a2 << 2) ^ (a3 << 3) ^
           (a4 << 4) ^ (a5 << 5) ^ (a6 << 6) ^ (a7 << 7);
}

/*
 * SplitMix64-style mixing.  This is a latency-bound dependent chain of
 * multiplies, shifts and xor operations; it is representative of hashing,
 * PRNGs and integer-heavy serial code.
 */
static NOINLINE uint64_t int_hash_kernel(uint64_t iters)
{
    uint64_t x = 0x243f6a8885a308d3ULL;
    uint64_t acc = 0;

    for (uint64_t i = 0; i < iters; i++) {
        x += 0x9e3779b97f4a7c15ULL;
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        acc ^= x;
    }

    return acc;
}

typedef float v8f __attribute__((vector_size(32)));
typedef double v4d __attribute__((vector_size(32)));

static float fp32_reduce(v8f v)
{
    float out = 0.0f;
    for (int i = 0; i < 8; i++)
        out += v[i];
    return out;
}

static double fp64_reduce(v4d v)
{
    double out = 0.0;
    for (int i = 0; i < 4; i++)
        out += v[i];
    return out;
}

/*
 * Eight independent vector accumulators, enough to cover FMA latency and
 * saturate both FMA ports.  With AVX2 one iteration is 8 vectors * 8 lanes =
 * 64 FMAs = 128 FLOPs.  The recurrence is stable (b < 1) and cannot be
 * folded away.
 */
static NOINLINE uint64_t fp32_fma_kernel(uint64_t iters)
{
    v8f a0 = {1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.7f};
    v8f a1 = {1.8f, 1.9f, 2.0f, 2.1f, 2.2f, 2.3f, 2.4f, 2.5f};
    v8f a2 = {2.6f, 2.7f, 2.8f, 2.9f, 3.0f, 3.1f, 3.2f, 3.3f};
    v8f a3 = {3.4f, 3.5f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.1f};
    v8f a4 = {4.2f, 4.3f, 4.4f, 4.5f, 4.6f, 4.7f, 4.8f, 4.9f};
    v8f a5 = {5.0f, 5.1f, 5.2f, 5.3f, 5.4f, 5.5f, 5.6f, 5.7f};
    v8f a6 = {5.8f, 5.9f, 6.0f, 6.1f, 6.2f, 6.3f, 6.4f, 6.5f};
    v8f a7 = {6.6f, 6.7f, 6.8f, 6.9f, 7.0f, 7.1f, 7.2f, 7.3f};
    const v8f b = {0.999999f, 0.999998f, 0.999997f, 0.999996f,
                   0.999995f, 0.999994f, 0.999993f, 0.999992f};
    const v8f c = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    for (uint64_t i = 0; i < iters; i++) {
        a0 = a0 * b + c;
        a1 = a1 * b + c;
        a2 = a2 * b + c;
        a3 = a3 * b + c;
        a4 = a4 * b + c;
        a5 = a5 * b + c;
        a6 = a6 * b + c;
        a7 = a7 * b + c;
        KEEP8V(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    v8f sum = (a0 + a1) + (a2 + a3) + (a4 + a5) + (a6 + a7);
    float out = fp32_reduce(sum);
    uint32_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}

/*
 * Eight 256-bit vectors of doubles = 32 FMAs = 64 FLOPs per iteration.
 */
static NOINLINE uint64_t fp64_fma_kernel(uint64_t iters)
{
    v4d a0 = {1.0, 1.1, 1.2, 1.3};
    v4d a1 = {1.4, 1.5, 1.6, 1.7};
    v4d a2 = {1.8, 1.9, 2.0, 2.1};
    v4d a3 = {2.2, 2.3, 2.4, 2.5};
    v4d a4 = {2.6, 2.7, 2.8, 2.9};
    v4d a5 = {3.0, 3.1, 3.2, 3.3};
    v4d a6 = {3.4, 3.5, 3.6, 3.7};
    v4d a7 = {3.8, 3.9, 4.0, 4.1};
    const v4d b = {0.999999999, 0.999999998, 0.999999997, 0.999999996};
    const v4d c = {1.0, 1.0, 1.0, 1.0};

    for (uint64_t i = 0; i < iters; i++) {
        a0 = a0 * b + c;
        a1 = a1 * b + c;
        a2 = a2 * b + c;
        a3 = a3 * b + c;
        a4 = a4 * b + c;
        a5 = a5 * b + c;
        a6 = a6 * b + c;
        a7 = a7 * b + c;
        KEEP8V(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    v4d sum = (a0 + a1) + (a2 + a3) + (a4 + a5) + (a6 + a7);
    double out = fp64_reduce(sum);
    uint64_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}

#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
/*
 * Ultimate x86 path: four doubles per vsqrtpd, eight independent chains.
 * This does 32 square roots per iteration, same total work as the AVX2 FMA
 * tests.  It is not a reciprocal-square-root approximation.
 */
static NOINLINE uint64_t fp64_sqrt_kernel(uint64_t iters)
{
    __m256d a0 = _mm256_set_pd(1.3, 1.2, 1.1, 1.0);
    __m256d a1 = _mm256_set_pd(1.7, 1.6, 1.5, 1.4);
    __m256d a2 = _mm256_set_pd(2.1, 2.0, 1.9, 1.8);
    __m256d a3 = _mm256_set_pd(2.5, 2.4, 2.3, 2.2);
    __m256d a4 = _mm256_set_pd(2.9, 2.8, 2.7, 2.6);
    __m256d a5 = _mm256_set_pd(3.3, 3.2, 3.1, 3.0);
    __m256d a6 = _mm256_set_pd(3.7, 3.6, 3.5, 3.4);
    __m256d a7 = _mm256_set_pd(4.1, 4.0, 3.9, 3.8);
    const __m256d one = _mm256_set1_pd(1.0);

    for (uint64_t i = 0; i < iters; i++) {
        a0 = _mm256_sqrt_pd(_mm256_add_pd(a0, one));
        a1 = _mm256_sqrt_pd(_mm256_add_pd(a1, one));
        a2 = _mm256_sqrt_pd(_mm256_add_pd(a2, one));
        a3 = _mm256_sqrt_pd(_mm256_add_pd(a3, one));
        a4 = _mm256_sqrt_pd(_mm256_add_pd(a4, one));
        a5 = _mm256_sqrt_pd(_mm256_add_pd(a5, one));
        a6 = _mm256_sqrt_pd(_mm256_add_pd(a6, one));
        a7 = _mm256_sqrt_pd(_mm256_add_pd(a7, one));
    }

    __m256d sum = _mm256_add_pd(
        _mm256_add_pd(_mm256_add_pd(a0, a1), _mm256_add_pd(a2, a3)),
        _mm256_add_pd(_mm256_add_pd(a4, a5), _mm256_add_pd(a6, a7)));
    double lanes[4];
    _mm256_storeu_pd(lanes, sum);
    double out = lanes[0] + lanes[1] + lanes[2] + lanes[3];
    uint64_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}
#else
/*
 * Eight independent scalar square roots for non-AVX2 targets.
 */
static NOINLINE uint64_t fp64_sqrt_kernel(uint64_t iters)
{
    double a0 = 0.1, a1 = 0.2, a2 = 0.3, a3 = 0.4;
    double a4 = 0.5, a5 = 0.6, a6 = 0.7, a7 = 0.8;

    for (uint64_t i = 0; i < iters; i++) {
        a0 = __builtin_sqrt(a0 + 1.0);
        a1 = __builtin_sqrt(a1 + 1.0);
        a2 = __builtin_sqrt(a2 + 1.0);
        a3 = __builtin_sqrt(a3 + 1.0);
        a4 = __builtin_sqrt(a4 + 1.0);
        a5 = __builtin_sqrt(a5 + 1.0);
        a6 = __builtin_sqrt(a6 + 1.0);
        a7 = __builtin_sqrt(a7 + 1.0);
        KEEP8D(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    double out = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
    uint64_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}
#endif

/* ------------------------------------------------------------------------- */
/* Scalar architecture mode (cross-ISA)                                      */
/* ------------------------------------------------------------------------- */

static NOINLINE uint64_t arch_fp64_fma_kernel(uint64_t iters)
{
    double a0 = 1.0, a1 = 1.1, a2 = 1.2, a3 = 1.3;
    double a4 = 1.4, a5 = 1.5, a6 = 1.6, a7 = 1.7;
    const double b = 0.999999999, c = 1.0;
    for (uint64_t i = 0; i < iters; i++) {
        a0 = a0 * b + c; a1 = a1 * b + c;
        a2 = a2 * b + c; a3 = a3 * b + c;
        a4 = a4 * b + c; a5 = a5 * b + c;
        a6 = a6 * b + c; a7 = a7 * b + c;
        KEEP8D(a0, a1, a2, a3, a4, a5, a6, a7);
    }
    double out = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
    uint64_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}

static NOINLINE uint64_t arch_fp64_sqrt_kernel(uint64_t iters)
{
    double a0 = 0.1, a1 = 0.2, a2 = 0.3, a3 = 0.4;
    double a4 = 0.5, a5 = 0.6, a6 = 0.7, a7 = 0.8;
    for (uint64_t i = 0; i < iters; i++) {
        a0 = __builtin_sqrt(a0 + 1.0); a1 = __builtin_sqrt(a1 + 1.0);
        a2 = __builtin_sqrt(a2 + 1.0); a3 = __builtin_sqrt(a3 + 1.0);
        a4 = __builtin_sqrt(a4 + 1.0); a5 = __builtin_sqrt(a5 + 1.0);
        a6 = __builtin_sqrt(a6 + 1.0); a7 = __builtin_sqrt(a7 + 1.0);
        KEEP8D(a0, a1, a2, a3, a4, a5, a6, a7);
    }
    double out = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
    uint64_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}

/* ------------------------------------------------------------------------- */
/* Workload table                                                            */
/*                                                                           */
/* Reference constants: throughput of the reference machine, in the same unit */
/* as the printed result.  See README.md for the full reference description.  */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *id;
    const char *name;
    const char *unit;
    double ops_per_iter;
    double unit_scale;
    double ref_single;
    uint64_t (*fn)(uint64_t);
} workload_t;

static const workload_t workloads[] = {
    {"int_add",   "INT64 add",  "Gop/s",    8.0,  1e9, 14.4, int_add_kernel},
    {"int_mul",   "INT64 mul",  "Gop/s",    8.0,  1e9, 4.28, int_mul_kernel},
    {"int_hash",  "INT64 hash", "Mhash/s",  1.0,  1e6, 327.0, int_hash_kernel},
    {"fp32_fma",  "FP32 FMA",   "GFLOP/s", 128.0, 1e9, 137.0, fp32_fma_kernel},
    {"fp64_fma",  "FP64 FMA",   "GFLOP/s", 64.0,  1e9, 67.0, fp64_fma_kernel},
    {"fp64_sqrt", "FP64 sqrt",  "Gsqrt/s",  (double)FP64_SQRT_BATCH, 1e9, 1.40, fp64_sqrt_kernel},
};

#define WORKLOAD_COUNT (sizeof(workloads) / sizeof(workloads[0]))

static const workload_t arch_workloads[] = {
    {"int_add",  "INT64 add", "Gop/s",    8.0, 1e9, 14.0, int_add_kernel},
    {"int_mul",  "INT64 mul", "Gop/s",    8.0, 1e9, 3.95, int_mul_kernel},
    {"int_hash", "INT64 hash","Mhash/s",  1.0, 1e6, 298.0, int_hash_kernel},
    {"fp64_fma", "FP64 FMA",  "GFLOP/s", 16.0, 1e9, 15.8, arch_fp64_fma_kernel},
    {"fp64_sqrt","FP64 sqrt", "Gsqrt/s",  8.0, 1e9, 0.66, arch_fp64_sqrt_kernel},
};
#define ARCH_COUNT (sizeof(arch_workloads) / sizeof(arch_workloads[0]))

/* ------------------------------------------------------------------------- */
/* Threaded execution                                                        */
/* ------------------------------------------------------------------------- */

typedef struct {
    HANDLE start;
    volatile LONG ready;
} gate_t;

typedef struct {
    gate_t *gate;
    uint64_t (*fn)(uint64_t);
    uint64_t iters;
    uint64_t checksum;
} worker_t;

static DWORD WINAPI worker_main(LPVOID arg)
{
    worker_t *w = (worker_t *)arg;

    InterlockedIncrement(&w->gate->ready);
    WaitForSingleObject(w->gate->start, INFINITE);

    w->checksum = w->fn(w->iters);
    return 0;
}

/*
 * Run one timed sample.  Returns wall-clock seconds.  Thread startup is
 * outside the timed region; release of the start gate is inside it, but the
 * wake-up cost is negligible compared with the sample duration.
 */
static double run_once(uint64_t (*fn)(uint64_t), uint64_t iters, int threads,
                       uint64_t *checksum)
{
    if (threads <= 1) {
        uint64_t t0 = now_ns();
        uint64_t result = fn(iters);
        uint64_t t1 = now_ns();
        g_checksum_sink ^= result;
        if (checksum)
            *checksum = result;
        return (double)(t1 - t0) / 1e9;
    }

    gate_t gate;
    worker_t workers[MAX_THREADS];
    HANDLE tids[MAX_THREADS];
    int created = 0;

    gate.start = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (gate.start == NULL) {
        fprintf(stderr, "CreateEvent failed: %lu\n", (unsigned long)GetLastError());
        exit(1);
    }
    gate.ready = 0;

    for (int i = 0; i < threads; i++) {
        workers[i].gate = &gate;
        workers[i].fn = fn;
        workers[i].iters = iters;
        workers[i].checksum = 0;
        tids[i] = CreateThread(NULL, 0, worker_main, &workers[i], 0, NULL);
        if (tids[i] == NULL) {
            fprintf(stderr, "CreateThread failed: %lu\n",
                    (unsigned long)GetLastError());
            exit(1);
        }
        created++;
    }

    /* Wait until every worker is parked on the gate. */
    while (InterlockedCompareExchange(&gate.ready, 0, 0) < created)
        Sleep(0);

    uint64_t t0 = now_ns();
    if (!SetEvent(gate.start)) {
        fprintf(stderr, "SetEvent failed: %lu\n", (unsigned long)GetLastError());
        exit(1);
    }

    for (int i = 0; i < created; i++) {
        WaitForSingleObject(tids[i], INFINITE);
        CloseHandle(tids[i]);
    }
    uint64_t t1 = now_ns();
    CloseHandle(gate.start);

    uint64_t sum = 0;
    for (int i = 0; i < created; i++)
        sum ^= workers[i].checksum;
    g_checksum_sink ^= sum;
    if (checksum)
        *checksum = sum;

    return (double)(t1 - t0) / 1e9;
}

/* Choose an iteration count that lands near the requested sample duration. */
static uint64_t calibrate(uint64_t (*fn)(uint64_t), int threads,
                          double target_sec)
{
    uint64_t iters = 1ULL << 20;
    double last = 0.0;

    for (int attempt = 0; attempt < 16; attempt++) {
        last = run_once(fn, iters, threads, NULL);
        if (last >= target_sec * 0.65)
            break;

        double factor = (target_sec / (last > 1e-9 ? last : 1e-9)) * 0.85;
        if (factor < 1.25)
            factor = 1.25;
        if (factor > 64.0)
            factor = 64.0;

        double next = (double)iters * factor;
        if (next >= 9.0e18) {
            iters = 9000000000000000000ULL;
            break;
        }
        iters = (uint64_t)next;
    }

    (void)last;
    return iters;
}

/* ------------------------------------------------------------------------- */
/* Statistics                                                                */
/* ------------------------------------------------------------------------- */

static int cmp_double(const void *pa, const void *pb)
{
    double a = *(const double *)pa;
    double b = *(const double *)pb;
    return (a > b) - (a < b);
}

static double median(double *values, int n)
{
    qsort(values, (size_t)n, sizeof(double), cmp_double);
    if (n & 1)
        return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) * 0.5;
}

/* ------------------------------------------------------------------------- */
/* System information                                                        */
/* ------------------------------------------------------------------------- */

static void cpu_brand(char *out, size_t out_size)
{
    out[0] = '\0';
    DWORD size = (DWORD)out_size;
    if (RegGetValueA(HKEY_LOCAL_MACHINE,
                     "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, NULL, out,
                     &size) != ERROR_SUCCESS)
        out[0] = '\0';
    if (out[0] == '\0')
        GetEnvironmentVariableA("PROCESSOR_IDENTIFIER", out, (DWORD)out_size);
    if (out[0] == '\0')
        snprintf(out, out_size, "unknown CPU");
}

static int physical_cores(void)
{
    DWORD len = 0;
    if (!GetLogicalProcessorInformation(NULL, &len) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return 0;
    if (len == 0)
        return 0;

    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info = malloc(len);
    if (!info)
        return 0;

    int count = 0;
    if (GetLogicalProcessorInformation(info, &len)) {
        DWORD n = len / (DWORD)sizeof(*info);
        for (DWORD i = 0; i < n; i++) {
            if (info[i].Relationship == RelationProcessorCore)
                count++;
        }
    }
    free(info);
    return count;
}

static int logical_cores(void)
{
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n > 0 ? (int)n : 1;
}

#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("xsave")))
static uint64_t read_xcr0(void)
{
    return (uint64_t)_xgetbv(0);
}
#endif

static int cpu_has_avx2(void)
{
#if defined(__x86_64__) || defined(__i386__)
    int regs[4];

    __cpuid(regs, 0);
    if (regs[0] < 7)
        return 0;

    __cpuid(regs, 1);
    if ((regs[2] & (1 << 27)) == 0) /* OSXSAVE */
        return 0;
    if ((regs[2] & (1 << 28)) == 0) /* AVX */
        return 0;

    uint64_t xcr0 = read_xcr0();
    if ((xcr0 & 0x6) != 0x6) /* XMM and YMM state enabled */
        return 0;

    if ((regs[2] & (1 << 12)) == 0) /* FMA */
        return 0;

    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0; /* AVX2 */
#else
    return 0;
#endif
}

static const char *compiler_name(void)
{
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown compiler";
#endif
}

static const char *simd_name(void)
{
#if defined(__AVX512F__)
    return "AVX-512";
#elif defined(__AVX2__)
    return "AVX2";
#elif defined(__AVX__)
    return "AVX";
#elif defined(__ARM_NEON)
    return "NEON";
#elif defined(__SSE2__)
    return "SSE2";
#else
    return "scalar";
#endif
}

/* ------------------------------------------------------------------------- */
/* JSON                                                                      */
/* ------------------------------------------------------------------------- */

static void json_escape(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", fp); break;
        case '\\': fputs("\\\\", fp); break;
        case '\b': fputs("\\b", fp);  break;
        case '\f': fputs("\\f", fp);  break;
        case '\n': fputs("\\n", fp);  break;
        case '\r': fputs("\\r", fp);  break;
        case '\t': fputs("\\t", fp);  break;
        default:
            if (*p < 0x20)
                fprintf(fp, "\\u%04x", (unsigned int)*p);
            else
                fputc(*p, fp);
        }
    }
    fputc('"', fp);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    double rate;
    double score;
    double sec;
    int threads;
} measurement_t;

static double g_arch_rates[ARCH_COUNT];
static double g_arch_scores[ARCH_COUNT];
static double g_arch_score;

static void run_arch_suite(int samples, double duration)
{
    printf("Cross-architecture scalar (1 thread)\n");
    printf("%-14s %16s %14s %10s\n", "TEST", "THROUGHPUT", "REFERENCE", "SCORE");
    printf("------------------------------------------------------------\n");
    for (size_t wi = 0; wi < ARCH_COUNT; wi++) {
        const workload_t *w = &arch_workloads[wi];
        uint64_t iters = calibrate(w->fn, 1, duration);
        double rates[31];
        uint64_t checksum = 0;
        for (int s = 0; s < samples; s++) {
            double sec = run_once(w->fn, iters, 1, &checksum);
            if (sec <= 0.0) {
                fprintf(stderr, "timer failure\n");
                exit(1);
            }
            double ops = (double)iters * w->ops_per_iter;
            rates[s] = ops / sec / w->unit_scale;
        }
        double rate = median(rates, samples);
        double score = 1000.0 * rate / w->ref_single;
        g_arch_rates[wi] = rate;
        g_arch_scores[wi] = score;
        printf("%-14s %10.2f %-5s %14.2f %10.0f\n",
               w->name, rate, w->unit, w->ref_single, score);
    }
    double sum = 0.0;
    for (size_t wi = 0; wi < ARCH_COUNT; wi++)
        sum += log(g_arch_scores[wi]);
    g_arch_score = exp(sum / (double)ARCH_COUNT);
    printf("------------------------------------------------------------\n");
    printf("%-14s %38.0f\n", "ARCH SCORE", g_arch_score);
    printf("============================================================\n");
    printf("ARCH: scalar 1T, same instruction mix across x86/ARM.\n");
    printf("Use NATIVE for AVX2/NEON throughput and SYSTEM for all-core.\n");
}

static void print_usage(const char *argv0)
{
    printf("CPU Compute Benchmark " BENCH_VERSION "\n\n");
    printf("Usage: %s [options]\n\n", argv0);
    printf("  --threads N      threads for the multi-core pass (default: logical cores)\n");
    printf("  --duration SEC   target seconds per sample (default: 0.35)\n");
    printf("  --samples N      samples per test, median is used (default: 3)\n");
    printf("  --quick          fast mode: 0.12 s per sample, 1 sample\n");
    printf("  --json PATH      also write the results as JSON\n");
    printf("  --check-avx2     exit 0 when AVX2 is usable (Windows launcher)\n");
    printf("  -h, --help       show this help\n");
    printf("\nOnly CPU compute is measured: no GPU, disk, network or large-array\n");
    printf("memory bandwidth tests are involved.\n");
}

static int parse_positive_int(const char *s, int *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v <= 0 || v > 100000)
        return -1;
    *out = (int)v;
    return 0;
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    SetUnhandledExceptionFilter(crash_handler);
#endif

    int threads = logical_cores();
    int samples = 3;
    double duration = 0.35;
    const char *json_path = NULL;
    int quick = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--check-avx2") == 0) {
            return cpu_has_avx2() ? 0 : 1;
        } else if (strcmp(argv[i], "--quick") == 0) {
            quick = 1;

        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], &threads) != 0 || threads > MAX_THREADS) {
                fprintf(stderr, "invalid --threads value (1..%d)\n", MAX_THREADS);
                return 2;
            }
        } else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], &samples) != 0 || samples > 31) {
                fprintf(stderr, "invalid --samples value (1..31)\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            char *end = NULL;
            errno = 0;
            duration = strtod(argv[++i], &end);
            if (errno != 0 || end == argv[i] || *end != '\0' ||
                duration < 0.02 || duration > 10.0) {
                fprintf(stderr, "invalid --duration value (0.02..10 seconds)\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (quick) {
        duration = 0.12;
        samples = 1;
    }
    if (threads > logical_cores())
        threads = logical_cores();
    if (threads > MAX_THREADS)
        threads = MAX_THREADS;

    char cpu[256];
    cpu_brand(cpu, sizeof(cpu));
    int phys = physical_cores();
    int logical = logical_cores();

    printf("CPU Compute Benchmark " BENCH_VERSION "\n");
    printf("============================================================\n");
    printf("CPU        %s\n", cpu);
    if (phys > 0)
        printf("Cores      %d physical / %d logical\n", phys, logical);
    else
        printf("Cores      %d logical\n", logical);
    printf("Compiler   %s (%s)\n", compiler_name(), simd_name());
    printf("Build      %s\n", BENCH_FLAGS);
    printf("Timer      CLOCK_MONOTONIC, median of %d, %.2f s/sample\n",
           samples, duration);
    printf("Mode       %s\n", quick ? "quick" : "standard");
    printf("\n");

    measurement_t single[WORKLOAD_COUNT];
    measurement_t multi[WORKLOAD_COUNT];
    memset(single, 0, sizeof(single));
    memset(multi, 0, sizeof(multi));

    /* ---- single core ---- */
    printf("Single-core\n");
    printf("%-14s %16s %14s %10s\n", "TEST", "THROUGHPUT", "REFERENCE", "SCORE");
    printf("------------------------------------------------------------\n");

    for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++) {
        const workload_t *w = &workloads[wi];
        uint64_t iters = calibrate(w->fn, 1, duration);
        double rates[31];
        uint64_t checksum = 0;

        for (int s = 0; s < samples; s++) {
            double sec = run_once(w->fn, iters, 1, &checksum);
            if (sec <= 0.0) {
                fprintf(stderr, "timer failure\n");
                return 1;
            }
            double ops = (double)iters * w->ops_per_iter;
            rates[s] = ops / sec / w->unit_scale;
        }

        double rate = median(rates, samples);
        double score = 1000.0 * rate / w->ref_single;
        single[wi].rate = rate;
        single[wi].score = score;
        single[wi].sec = duration;
        single[wi].threads = 1;

        printf("%-14s %10.2f %-5s %14.2f %10.0f\n",
               w->name, rate, w->unit, w->ref_single, score);
    }

    double st_score = 0.0;
    for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++)
        st_score += log(single[wi].score);
    st_score = exp(st_score / (double)WORKLOAD_COUNT);

    printf("------------------------------------------------------------\n");
    printf("%-14s %38.0f\n", "SINGLE SCORE", st_score);
    printf("\n");

    /* ---- multi core ---- */
    double mt_score = st_score;
    double parallel_speedup = 1.0;
    int mt_threads = threads;

    if (threads > 1) {
        printf("Multi-core (%d threads)\n", threads);
        printf("%-14s %16s %14s %10s\n", "TEST", "THROUGHPUT", "REFERENCE", "SCORE");
        printf("------------------------------------------------------------\n");

        for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++) {
            const workload_t *w = &workloads[wi];
            uint64_t iters = calibrate(w->fn, threads, duration);
            double rates[31];
            uint64_t checksum = 0;

            for (int s = 0; s < samples; s++) {
                double sec = run_once(w->fn, iters, threads, &checksum);
                if (sec <= 0.0) {
                    fprintf(stderr, "timer failure\n");
                    return 1;
                }
                double ops = (double)iters * (double)threads * w->ops_per_iter;
                rates[s] = ops / sec / w->unit_scale;
            }

            double rate = median(rates, samples);
            double score = 1000.0 * rate / w->ref_single;
            multi[wi].rate = rate;
            multi[wi].score = score;
            multi[wi].sec = duration;
            multi[wi].threads = threads;

            printf("%-14s %10.2f %-5s %14.2f %10.0f\n",
                   w->name, rate, w->unit, w->ref_single, score);
        }

        mt_score = 0.0;
        for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++)
            mt_score += log(multi[wi].score);
        mt_score = exp(mt_score / (double)WORKLOAD_COUNT);

        parallel_speedup = 0.0;
        for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++)
            parallel_speedup += log(multi[wi].rate / single[wi].rate);
        parallel_speedup = exp(parallel_speedup / (double)WORKLOAD_COUNT);
    }

    if (threads <= 1) {
        for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++)
            multi[wi] = single[wi];
    }

    double overall = sqrt(st_score * mt_score);

    if (threads > 1) {
        printf("------------------------------------------------------------\n");
        printf("%-14s %38.0f\n", "MULTI SCORE", mt_score);
        printf("%-14s %37.2fx\n", "MULTI SCALE", parallel_speedup);
        int use_phys = phys > 0 && threads >= phys;
        double eff_base = use_phys ? (double)phys : (double)threads;
        printf("%-14s %37.1f%%\n", use_phys ? "PHYS EFF" : "EFFICIENCY",
               100.0 * parallel_speedup / eff_base);
        printf("\n");
    }

    printf("============================================================\n");
    printf("%-14s %38.0f\n", "OVERALL SCORE", overall);
    printf("============================================================\n");
    printf("Reference machine: Intel Core i7-9750H, 6C/12T, see README.md\n");
    printf("Higher is better. Raw throughput is the primary result; the\n");
    printf("score is a geometric mean normalised to the reference machine.\n");
    printf("\n");
    run_arch_suite(samples, duration);

    /* ---- JSON ---- */
    if (json_path) {
        FILE *fp = fopen(json_path, "w");
        if (!fp) {
            fprintf(stderr, "cannot write %s: %s\n", json_path, strerror(errno));
            return 1;
        }

        time_t now = time(NULL);
        struct tm tmv;
        struct tm *tmp = localtime(&now);
        char stamp[64] = "";
        if (tmp)
            tmv = *tmp;
        else
            memset(&tmv, 0, sizeof(tmv));
        strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &tmv);

        fprintf(fp, "{\n");
        fprintf(fp, "  \"version\": \"%s\",\n", BENCH_VERSION);
        fprintf(fp, "  \"mode\": \"native+arch\",\n");
        fprintf(fp, "  \"timestamp\": ");
        json_escape(fp, stamp);
        fprintf(fp, ",\n  \"cpu\": ");
        json_escape(fp, cpu);
        fprintf(fp, ",\n  \"physical_cores\": %d,\n", phys);
        fprintf(fp, "  \"logical_cores\": %d,\n", logical);
        fprintf(fp, "  \"compiler\": ");
        json_escape(fp, compiler_name());
        fprintf(fp, ",\n  \"simd\": ");
        json_escape(fp, simd_name());
        fprintf(fp, ",\n  \"build_flags\": ");
        json_escape(fp, BENCH_FLAGS);
        fprintf(fp, ",\n  \"samples\": %d,\n  \"duration_sec\": %.6f,\n",
                samples, duration);
        fprintf(fp, "  \"threads\": %d,\n", mt_threads);
        fprintf(fp, "  \"scores\": {\"arch\": %.2f, \"single\": %.2f, "
                    "\"multi\": %.2f, \"overall\": %.2f, \"speedup\": %.4f},\n",
                g_arch_score, st_score, mt_score, overall, parallel_speedup);

        fprintf(fp, "  \"single_core\": [\n");
        for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++) {
            const workload_t *w = &workloads[wi];
            fprintf(fp, "    {\"id\": ");
            json_escape(fp, w->id);
            fprintf(fp, ", \"name\": ");
            json_escape(fp, w->name);
            fprintf(fp, ", \"throughput\": %.4f, \"unit\": ", single[wi].rate);
            json_escape(fp, w->unit);
            fprintf(fp, ", \"reference\": %.4f, \"score\": %.2f}%s\n",
                    w->ref_single, single[wi].score,
                    (wi + 1 < WORKLOAD_COUNT) ? "," : "");
        }
        fprintf(fp, "  ],\n  \"multi_core\": [\n");
        for (size_t wi = 0; wi < WORKLOAD_COUNT; wi++) {
            const workload_t *w = &workloads[wi];
            fprintf(fp, "    {\"id\": ");
            json_escape(fp, w->id);
            fprintf(fp, ", \"name\": ");
            json_escape(fp, w->name);
            fprintf(fp, ", \"throughput\": %.4f, \"unit\": ", multi[wi].rate);
            json_escape(fp, w->unit);
            fprintf(fp, ", \"reference\": %.4f, \"score\": %.2f}%s\n",
                    w->ref_single,
                    multi[wi].score,
                    (wi + 1 < WORKLOAD_COUNT) ? "," : "");
        }
        fprintf(fp, "  ],\n  \"arch_core\": [\n");
        for (size_t wi = 0; wi < ARCH_COUNT; wi++) {
            const workload_t *w = &arch_workloads[wi];
            fprintf(fp, "    {\"id\": "); json_escape(fp, w->id);
            fprintf(fp, ", \"name\": "); json_escape(fp, w->name);
            fprintf(fp, ", \"throughput\": %.4f, \"unit\": ", g_arch_rates[wi]);
            json_escape(fp, w->unit);
            fprintf(fp, ", \"reference\": %.4f, \"score\": %.2f}%s\n",
                    w->ref_single, g_arch_scores[wi],
                    (wi + 1 < ARCH_COUNT) ? "," : "");
        }
        fprintf(fp, "  ]\n}\n");
        fclose(fp);
        printf("\nJSON written to %s\n", json_path);
    }

#if defined(_WIN32)
    /* A direct double-click has no arguments.  Keep the console open so the
     * result remains readable; the .bat launcher always passes --json. */
    if (argc == 1) {
        printf("\nPress Enter to close...");
        fflush(stdout);
        (void)getchar();
    }
#endif
    return 0;
}
