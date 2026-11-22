/*
 * CPU Compute Benchmark
 * ---------------------
 * A small, self-contained benchmark for sustained scalar/vector compute
 * throughput.  The measured loops are register-resident: they do not stream
 * large arrays, touch the disk, use the GPU, or allocate memory while timed.
 *
 * Build:  make
 * Run:    ./cpu-bench
 *
 * The score is deliberately relative to a documented reference machine.  Raw
 * throughput is always printed next to the score, so the numbers remain useful
 * even if the reference constants are changed.
 */

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__)
#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

#ifndef BENCH_FLAGS
#define BENCH_FLAGS "unknown"
#endif

#define BENCH_VERSION "2.0"
#define MAX_THREADS 256

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

/* Ask macOS to run the benchmark threads at the highest QoS class.  On Apple
 * Silicon this makes the scheduler strongly prefer the performance cores. */
static void set_high_qos(void)
{
#if defined(__APPLE__)
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
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
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        abort();
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
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

/*
 * x86 AVX2 has 256-bit vectors.  Apple Silicon NEON is 128-bit, so using a
 * 32-byte vector there would only force the compiler to split every operation
 * into two halves.  Match the native width instead.
 */
#if defined(__aarch64__)
typedef float v8f __attribute__((vector_size(16)));
typedef double v4d __attribute__((vector_size(16)));
#else
typedef float v8f __attribute__((vector_size(32)));
typedef double v4d __attribute__((vector_size(32)));
#endif

#if defined(__aarch64__)
#define VEC8F(a, b, c, d, e, f, g, h) {a, b, c, d}
#define VEC4D(a, b, c, d) {a, b}
#else
#define VEC8F(a, b, c, d, e, f, g, h) {a, b, c, d, e, f, g, h}
#define VEC4D(a, b, c, d) {a, b, c, d}
#endif

#define FP32_LANES (sizeof(v8f) / sizeof(float))
#define FP64_LANES (sizeof(v4d) / sizeof(double))

#if defined(__aarch64__)
#define FP32_ACCUM 16
#define FP64_ACCUM 16
#else
#define FP32_ACCUM 8
#define FP64_ACCUM 8
#endif

#if (defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))) || defined(__aarch64__)
#define FP64_SQRT_BATCH 32
#else
#define FP64_SQRT_BATCH 8
#endif

static float fp32_reduce(v8f v)
{
    float out = 0.0f;
    for (size_t i = 0; i < FP32_LANES; i++)
        out += v[i];
    return out;
}

static double fp64_reduce(v4d v)
{
    double out = 0.0;
    for (size_t i = 0; i < FP64_LANES; i++)
        out += v[i];
    return out;
}

#if defined(__aarch64__)
/*
 * Apple Silicon has 128-bit NEON and up to four FMA pipelines.  Use sixteen
 * independent chains so latency is not the limit.  The recurrence is written
 * as a = a*b + a; AArch64 can encode this as one "fmla a, b, a" instruction,
 * avoiding the extra register copies needed for a*b + 1.
 * b = -0.5 keeps the values finite (they decay to zero).
 */
static NOINLINE uint64_t fp32_fma_kernel(uint64_t iters)
{
    v8f a0  = VEC8F(1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.7f);
    v8f a1  = VEC8F(1.8f, 1.9f, 2.0f, 2.1f, 2.2f, 2.3f, 2.4f, 2.5f);
    v8f a2  = VEC8F(2.6f, 2.7f, 2.8f, 2.9f, 3.0f, 3.1f, 3.2f, 3.3f);
    v8f a3  = VEC8F(3.4f, 3.5f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.1f);
    v8f a4  = VEC8F(4.2f, 4.3f, 4.4f, 4.5f, 4.6f, 4.7f, 4.8f, 4.9f);
    v8f a5  = VEC8F(5.0f, 5.1f, 5.2f, 5.3f, 5.4f, 5.5f, 5.6f, 5.7f);
    v8f a6  = VEC8F(5.8f, 5.9f, 6.0f, 6.1f, 6.2f, 6.3f, 6.4f, 6.5f);
    v8f a7  = VEC8F(6.6f, 6.7f, 6.8f, 6.9f, 7.0f, 7.1f, 7.2f, 7.3f);
    v8f a8  = VEC8F(7.4f, 7.5f, 7.6f, 7.7f, 7.8f, 7.9f, 8.0f, 8.1f);
    v8f a9  = VEC8F(8.2f, 8.3f, 8.4f, 8.5f, 8.6f, 8.7f, 8.8f, 8.9f);
    v8f a10 = VEC8F(9.0f, 9.1f, 9.2f, 9.3f, 9.4f, 9.5f, 9.6f, 9.7f);
    v8f a11 = VEC8F(9.8f, 9.9f, 10.0f, 10.1f, 10.2f, 10.3f, 10.4f, 10.5f);
    v8f a12 = VEC8F(10.6f, 10.7f, 10.8f, 10.9f, 11.0f, 11.1f, 11.2f, 11.3f);
    v8f a13 = VEC8F(11.4f, 11.5f, 11.6f, 11.7f, 11.8f, 11.9f, 12.0f, 12.1f);
    v8f a14 = VEC8F(12.2f, 12.3f, 12.4f, 12.5f, 12.6f, 12.7f, 12.8f, 12.9f);
    v8f a15 = VEC8F(13.0f, 13.1f, 13.2f, 13.3f, 13.4f, 13.5f, 13.6f, 13.7f);
    const v8f b = VEC8F(-0.5f, -0.5f, -0.5f, -0.5f,
                        -0.5f, -0.5f, -0.5f, -0.5f);

    for (uint64_t i = 0; i < iters; i++) {
        a0  = a0  * b + a0;
        a1  = a1  * b + a1;
        a2  = a2  * b + a2;
        a3  = a3  * b + a3;
        a4  = a4  * b + a4;
        a5  = a5  * b + a5;
        a6  = a6  * b + a6;
        a7  = a7  * b + a7;
        a8  = a8  * b + a8;
        a9  = a9  * b + a9;
        a10 = a10 * b + a10;
        a11 = a11 * b + a11;
        a12 = a12 * b + a12;
        a13 = a13 * b + a13;
        a14 = a14 * b + a14;
        a15 = a15 * b + a15;
    }

    v8f sum = (((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7))) +
              (((a8 + a9) + (a10 + a11)) + ((a12 + a13) + (a14 + a15)));
    float out = fp32_reduce(sum);
    uint32_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}
#else
/*
 * Eight independent vector accumulators, enough to cover FMA latency and
 * saturate the FMA ports.  One iteration is 8 vectors * FRAC_LANES lanes =
 * 64 FMAs on AVX2, or 32 FMAs on 128-bit NEON.  The recurrence is stable
 * (b < 1) and cannot be folded away.
 */
static NOINLINE uint64_t fp32_fma_kernel(uint64_t iters)
{
    v8f a0 = VEC8F(1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.6f, 1.7f);
    v8f a1 = VEC8F(1.8f, 1.9f, 2.0f, 2.1f, 2.2f, 2.3f, 2.4f, 2.5f);
    v8f a2 = VEC8F(2.6f, 2.7f, 2.8f, 2.9f, 3.0f, 3.1f, 3.2f, 3.3f);
    v8f a3 = VEC8F(3.4f, 3.5f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.1f);
    v8f a4 = VEC8F(4.2f, 4.3f, 4.4f, 4.5f, 4.6f, 4.7f, 4.8f, 4.9f);
    v8f a5 = VEC8F(5.0f, 5.1f, 5.2f, 5.3f, 5.4f, 5.5f, 5.6f, 5.7f);
    v8f a6 = VEC8F(5.8f, 5.9f, 6.0f, 6.1f, 6.2f, 6.3f, 6.4f, 6.5f);
    v8f a7 = VEC8F(6.6f, 6.7f, 6.8f, 6.9f, 7.0f, 7.1f, 7.2f, 7.3f);
    const v8f b = VEC8F(0.999999f, 0.999998f, 0.999997f, 0.999996f,
                        0.999995f, 0.999994f, 0.999993f, 0.999992f);
    const v8f c = VEC8F(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

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

#endif

#if defined(__aarch64__)
/* Same 16-chain / one-instruction-FMA approach for double precision. */
static NOINLINE uint64_t fp64_fma_kernel(uint64_t iters)
{
    v4d a0  = VEC4D(1.0, 1.1, 1.2, 1.3);
    v4d a1  = VEC4D(1.4, 1.5, 1.6, 1.7);
    v4d a2  = VEC4D(1.8, 1.9, 2.0, 2.1);
    v4d a3  = VEC4D(2.2, 2.3, 2.4, 2.5);
    v4d a4  = VEC4D(2.6, 2.7, 2.8, 2.9);
    v4d a5  = VEC4D(3.0, 3.1, 3.2, 3.3);
    v4d a6  = VEC4D(3.4, 3.5, 3.6, 3.7);
    v4d a7  = VEC4D(3.8, 3.9, 4.0, 4.1);
    v4d a8  = VEC4D(4.2, 4.3, 4.4, 4.5);
    v4d a9  = VEC4D(4.6, 4.7, 4.8, 4.9);
    v4d a10 = VEC4D(5.0, 5.1, 5.2, 5.3);
    v4d a11 = VEC4D(5.4, 5.5, 5.6, 5.7);
    v4d a12 = VEC4D(5.8, 5.9, 6.0, 6.1);
    v4d a13 = VEC4D(6.2, 6.3, 6.4, 6.5);
    v4d a14 = VEC4D(6.6, 6.7, 6.8, 6.9);
    v4d a15 = VEC4D(7.0, 7.1, 7.2, 7.3);
    const v4d b = VEC4D(-0.5, -0.5, -0.5, -0.5);

    for (uint64_t i = 0; i < iters; i++) {
        a0  = a0  * b + a0;
        a1  = a1  * b + a1;
        a2  = a2  * b + a2;
        a3  = a3  * b + a3;
        a4  = a4  * b + a4;
        a5  = a5  * b + a5;
        a6  = a6  * b + a6;
        a7  = a7  * b + a7;
        a8  = a8  * b + a8;
        a9  = a9  * b + a9;
        a10 = a10 * b + a10;
        a11 = a11 * b + a11;
        a12 = a12 * b + a12;
        a13 = a13 * b + a13;
        a14 = a14 * b + a14;
        a15 = a15 * b + a15;
    }

    v4d sum = (((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7))) +
              (((a8 + a9) + (a10 + a11)) + ((a12 + a13) + (a14 + a15)));
    double out = fp64_reduce(sum);
    uint64_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}
#else
/*
 * Eight vectors of doubles: 32 FMAs on AVX2, 16 FMAs on 128-bit NEON.
 */
static NOINLINE uint64_t fp64_fma_kernel(uint64_t iters)
{
    v4d a0 = VEC4D(1.0, 1.1, 1.2, 1.3);
    v4d a1 = VEC4D(1.4, 1.5, 1.6, 1.7);
    v4d a2 = VEC4D(1.8, 1.9, 2.0, 2.1);
    v4d a3 = VEC4D(2.2, 2.3, 2.4, 2.5);
    v4d a4 = VEC4D(2.6, 2.7, 2.8, 2.9);
    v4d a5 = VEC4D(3.0, 3.1, 3.2, 3.3);
    v4d a6 = VEC4D(3.4, 3.5, 3.6, 3.7);
    v4d a7 = VEC4D(3.8, 3.9, 4.0, 4.1);
    const v4d b = VEC4D(0.999999999, 0.999999998, 0.999999997, 0.999999996);
    const v4d c = VEC4D(1.0, 1.0, 1.0, 1.0);

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

#endif

#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
/*
 * AVX2 vector square root: four doubles per instruction, eight independent
 * chains.  This is a throughput test, so processing four roots per vsqrtpd is
 * the natural form on x86.  The scalar implementation below remains the
 * fallback for SSE-only CPUs and for ARM.
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
#elif defined(__aarch64__)
/*
 * NEON vector square root: two doubles per instruction, sixteen independent
 * chains, so this does the same 32 sqrt/iteration as the AVX2 path.  The
 * instruction count is higher because NEON is 128-bit, which is a real
 * architectural difference rather than an artificial handicap.
 */
static NOINLINE uint64_t fp64_sqrt_kernel(uint64_t iters)
{
    float64x2_t a0  = vdupq_n_f64(1.0);
    float64x2_t a1  = vdupq_n_f64(2.0);
    float64x2_t a2  = vdupq_n_f64(3.0);
    float64x2_t a3  = vdupq_n_f64(4.0);
    float64x2_t a4  = vdupq_n_f64(5.0);
    float64x2_t a5  = vdupq_n_f64(6.0);
    float64x2_t a6  = vdupq_n_f64(7.0);
    float64x2_t a7  = vdupq_n_f64(8.0);
    float64x2_t a8  = vdupq_n_f64(9.0);
    float64x2_t a9  = vdupq_n_f64(10.0);
    float64x2_t a10 = vdupq_n_f64(11.0);
    float64x2_t a11 = vdupq_n_f64(12.0);
    float64x2_t a12 = vdupq_n_f64(13.0);
    float64x2_t a13 = vdupq_n_f64(14.0);
    float64x2_t a14 = vdupq_n_f64(15.0);
    float64x2_t a15 = vdupq_n_f64(16.0);
    const float64x2_t one = vdupq_n_f64(1.0);

    for (uint64_t i = 0; i < iters; i++) {
        a0  = vsqrtq_f64(vaddq_f64(a0, one));
        a1  = vsqrtq_f64(vaddq_f64(a1, one));
        a2  = vsqrtq_f64(vaddq_f64(a2, one));
        a3  = vsqrtq_f64(vaddq_f64(a3, one));
        a4  = vsqrtq_f64(vaddq_f64(a4, one));
        a5  = vsqrtq_f64(vaddq_f64(a5, one));
        a6  = vsqrtq_f64(vaddq_f64(a6, one));
        a7  = vsqrtq_f64(vaddq_f64(a7, one));
        a8  = vsqrtq_f64(vaddq_f64(a8, one));
        a9  = vsqrtq_f64(vaddq_f64(a9, one));
        a10 = vsqrtq_f64(vaddq_f64(a10, one));
        a11 = vsqrtq_f64(vaddq_f64(a11, one));
        a12 = vsqrtq_f64(vaddq_f64(a12, one));
        a13 = vsqrtq_f64(vaddq_f64(a13, one));
        a14 = vsqrtq_f64(vaddq_f64(a14, one));
        a15 = vsqrtq_f64(vaddq_f64(a15, one));
    }

    float64x2_t sum = vaddq_f64(
        vaddq_f64(vaddq_f64(a0, a1), vaddq_f64(a2, a3)),
        vaddq_f64(vaddq_f64(a4, a5), vaddq_f64(a6, a7)));
    sum = vaddq_f64(sum, vaddq_f64(
        vaddq_f64(vaddq_f64(a8, a9), vaddq_f64(a10, a11)),
        vaddq_f64(vaddq_f64(a12, a13), vaddq_f64(a14, a15))));

    double out = vgetq_lane_f64(sum, 0) + vgetq_lane_f64(sum, 1);
    uint64_t bits;
    memcpy(&bits, &out, sizeof(bits));
    return bits;
}
#else
/*
 * Eight independent scalar square roots.  Each operation is a real hardware
 * square root, not a reciprocal-square-root approximation (no -ffast-math).
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
/* Scalar architecture mode (for cross-ISA comparison)                       */
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
    {"fp32_fma",  "FP32 FMA",   "GFLOP/s", (double)FP32_ACCUM * (double)FP32_LANES * 2.0, 1e9, 137.0, fp32_fma_kernel},
    {"fp64_fma",  "FP64 FMA",   "GFLOP/s", (double)FP64_ACCUM * (double)FP64_LANES * 2.0, 1e9, 67.0, fp64_fma_kernel},
    {"fp64_sqrt", "FP64 sqrt",  "Gsqrt/s",  (double)FP64_SQRT_BATCH, 1e9, 1.40, fp64_sqrt_kernel},
};

#define WORKLOAD_COUNT (sizeof(workloads) / sizeof(workloads[0]))

/* Separate scalar table for --arch; constants are calibrated to this i7. */
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
    pthread_mutex_t mu;
    pthread_cond_t ready_cv;
    pthread_cond_t go_cv;
    int ready;
    int go;
    int n;
} gate_t;

typedef struct {
    gate_t *gate;
    uint64_t (*fn)(uint64_t);
    uint64_t iters;
    uint64_t checksum;
} worker_t;

static void *worker_main(void *arg)
{
    worker_t *w = arg;

    set_high_qos();

    pthread_mutex_lock(&w->gate->mu);
    w->gate->ready++;
    pthread_cond_signal(&w->gate->ready_cv);
    while (!w->gate->go)
        pthread_cond_wait(&w->gate->go_cv, &w->gate->mu);
    pthread_mutex_unlock(&w->gate->mu);

    w->checksum = w->fn(w->iters);
    return NULL;
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
    pthread_t tids[MAX_THREADS];
    int created = 0;
    int rc;

    memset(&gate, 0, sizeof(gate));
    gate.n = threads;
    if (pthread_mutex_init(&gate.mu, NULL) != 0 ||
        pthread_cond_init(&gate.ready_cv, NULL) != 0 ||
        pthread_cond_init(&gate.go_cv, NULL) != 0) {
        fprintf(stderr, "failed to initialise thread gate\n");
        exit(1);
    }

    for (int i = 0; i < threads; i++) {
        workers[i].gate = &gate;
        workers[i].fn = fn;
        workers[i].iters = iters;
        workers[i].checksum = 0;
        rc = pthread_create(&tids[i], NULL, worker_main, &workers[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(rc));
            exit(1);
        }
        created++;
    }

    /* Wait until every worker is parked on the gate. */
    pthread_mutex_lock(&gate.mu);
    while (gate.ready < created)
        pthread_cond_wait(&gate.ready_cv, &gate.mu);
    pthread_mutex_unlock(&gate.mu);

    uint64_t t0 = now_ns();
    pthread_mutex_lock(&gate.mu);
    gate.go = 1;
    pthread_cond_broadcast(&gate.go_cv);
    pthread_mutex_unlock(&gate.mu);

    for (int i = 0; i < created; i++)
        pthread_join(tids[i], NULL);
    uint64_t t1 = now_ns();

    uint64_t sum = 0;
    for (int i = 0; i < created; i++)
        sum ^= workers[i].checksum;
    g_checksum_sink ^= sum;
    if (checksum)
        *checksum = sum;

    pthread_cond_destroy(&gate.go_cv);
    pthread_cond_destroy(&gate.ready_cv);
    pthread_mutex_destroy(&gate.mu);
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
#if defined(__APPLE__)
    size_t len = out_size;
    if (sysctlbyname("machdep.cpu.brand_string", out, &len, NULL, 0) != 0)
        out[0] = '\0';
#elif defined(__linux__)
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ' || *colon == '\t')
                        colon++;
                    size_t n = strcspn(colon, "\r\n");
                    if (n >= out_size)
                        n = out_size - 1;
                    memcpy(out, colon, n);
                    out[n] = '\0';
                    break;
                }
            }
        }
        fclose(fp);
    }
#endif
    if (out[0] == '\0')
        snprintf(out, out_size, "unknown CPU");
}

static int physical_cores(void)
{
#if defined(__APPLE__)
    int value = 0;
    size_t len = sizeof(value);
    if (sysctlbyname("hw.physicalcpu", &value, &len, NULL, 0) == 0)
        return value;
#endif
    return 0;
}

static int sysctl_int_value(const char *name)
{
#if defined(__APPLE__)
    int value = 0;
    size_t len = sizeof(value);
    if (sysctlbyname(name, &value, &len, NULL, 0) == 0)
        return value;
#else
    (void)name;
#endif
    return 0;
}

static int performance_cores(void)
{
    return sysctl_int_value("hw.perflevel0.physicalcpu");
}

static int efficiency_cores(void)
{
    return sysctl_int_value("hw.perflevel1.physicalcpu");
}

static int logical_cores(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
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
    printf("  --pcores         multi-core pass uses performance cores only (Apple Silicon)\n");
    printf("  --json PATH      also write the results as JSON\n");
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
    set_high_qos();

    int threads = logical_cores();
    int samples = 3;
    double duration = 0.35;
    const char *json_path = NULL;
    int quick = 0;
    int pcores_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--quick") == 0) {
            quick = 1;
        } else if (strcmp(argv[i], "--pcores") == 0) {
            pcores_only = 1;
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

    char cpu[256];
    cpu_brand(cpu, sizeof(cpu));
    int phys = physical_cores();
    int logical = logical_cores();
    int pcores = performance_cores();
    int ecores = efficiency_cores();
    int heterogeneous = pcores > 0 && ecores > 0;

    if (threads > logical)
        threads = logical;
    if (threads > MAX_THREADS)
        threads = MAX_THREADS;

    if (pcores_only) {
        if (!heterogeneous) {
            fprintf(stderr, "--pcores is only available on heterogeneous Apple Silicon CPUs\n");
            return 2;
        }
        threads = pcores;
    }

    printf("CPU Compute Benchmark " BENCH_VERSION "\n");
    printf("============================================================\n");
    printf("CPU        %s\n", cpu);
    if (heterogeneous)
        printf("Cores      %d performance + %d efficiency / %d logical\n",
               pcores, ecores, logical);
    else if (phys > 0)
        printf("Cores      %d physical / %d logical\n", phys, logical);
    else
        printf("Cores      %d logical\n", logical);
    printf("Compiler   %s (%s)\n", compiler_name(), simd_name());
    printf("Build      %s\n", BENCH_FLAGS);
    printf("Timer      CLOCK_MONOTONIC, median of %d, %.2f s/sample\n",
           samples, duration);
    printf("Mode       %s%s\n", quick ? "quick" : "standard",
           pcores_only ? ", P-cores only" : "");
#if defined(__APPLE__)
    printf("QoS        USER_INTERACTIVE\n");
#endif
#if defined(__APPLE__) && defined(__aarch64__)
    printf("Note       NEON 128-bit build; score reference is Intel AVX2\n");
#endif
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
            double mt_ref = w->ref_single;
            double score = 1000.0 * rate / mt_ref;
            multi[wi].rate = rate;
            multi[wi].score = score;
            multi[wi].sec = duration;
            multi[wi].threads = threads;

            printf("%-14s %10.2f %-5s %14.2f %10.0f\n",
                   w->name, rate, w->unit, mt_ref, score);
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
        int use_pcore = heterogeneous && threads >= pcores;
        int use_phys = !use_pcore && phys > 0 && threads >= phys;
        double eff_base = use_pcore ? (double)pcores
                        : use_phys ? (double)phys
                                   : (double)threads;
        const char *eff_label = use_pcore ? "P-CORE RATIO"
                              : use_phys ? "PHYS EFF"
                                         : "EFFICIENCY";
        printf("%-14s %37.1f%%\n", eff_label,
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
        char stamp[64] = "";
#if defined(_WIN32)
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S%z", &tmv);

        fprintf(fp, "{\n");
        fprintf(fp, "  \"version\": \"%s\",\n", BENCH_VERSION);
        fprintf(fp, "  \"mode\": \"native+arch\",\n");
        fprintf(fp, "  \"timestamp\": ");
        json_escape(fp, stamp);
        fprintf(fp, ",\n  \"cpu\": ");
        json_escape(fp, cpu);
        fprintf(fp, ",\n  \"physical_cores\": %d,\n", phys);
        fprintf(fp, "  \"logical_cores\": %d,\n", logical);
        fprintf(fp, "  \"performance_cores\": %d,\n", pcores);
        fprintf(fp, "  \"efficiency_cores\": %d,\n", ecores);
        fprintf(fp, "  \"compiler\": ");
        json_escape(fp, compiler_name());
        fprintf(fp, ",\n  \"simd\": ");
        json_escape(fp, simd_name());
        fprintf(fp, ",\n  \"build_flags\": ");
        json_escape(fp, BENCH_FLAGS);
        fprintf(fp, ",\n  \"samples\": %d,\n  \"duration_sec\": %.6f,\n",
                samples, duration);
        fprintf(fp, "  \"threads\": %d,\n", mt_threads);
        fprintf(fp, "  \"pcores_only\": %s,\n", pcores_only ? "true" : "false");
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

    return 0;
}
