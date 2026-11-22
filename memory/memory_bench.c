/*
 * Memory / Cache Benchmark
 * ------------------------
 * Independent from the CPU Compute score.  Measures:
 *   - pointer-chase latency at L1, L2, L3/LLC and DRAM working-set sizes
 *   - sequential read, write, copy and triad bandwidth at a large working set
 *
 * Build (macOS): cc -O3 -march=native memory_bench.c -o memory-bench
 * Build (Windows): x86_64-w64-mingw32-gcc -O3 ... -luser32
 */

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

#define MEM_VERSION "1.0"
#define MAX_SAMPLES 15

static volatile uint64_t g_sink;

static uint64_t now_ns(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    uint64_t f = (uint64_t)freq.QuadPart;
    uint64_t v = (uint64_t)c.QuadPart;
    return (v / f) * 1000000000ULL + ((v % f) * 1000000000ULL) / f;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        abort();
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static void *xalloc(size_t bytes)
{
    void *p = NULL;
#if defined(_WIN32)
    p = _aligned_malloc(bytes, 64);
#else
    if (posix_memalign(&p, 64, bytes) != 0)
        p = NULL;
#endif
    if (!p) {
        fprintf(stderr, "allocation failed: %zu bytes\n", bytes);
        exit(1);
    }
    return p;
}

static void xfree(void *p)
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static uint64_t rnd_below(uint64_t *state, uint64_t n)
{
    return n ? splitmix64(state) % n : 0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n)
{
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    return (n & 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

static size_t floor_pow2(size_t x)
{
    size_t p = 1;
    while (p <= x / 2)
        p <<= 1;
    return p;
}

/* Sattolo's algorithm: one random cycle, so the pointer chase never gets
 * trapped in a short cycle. */
static uint64_t *make_chase(size_t bytes, uint64_t seed)
{
    size_t n = bytes / sizeof(uint64_t);
    if (n < 2)
        n = 2;
    uint64_t *a = xalloc(n * sizeof(uint64_t));
    for (size_t i = 0; i < n; i++)
        a[i] = (uint64_t)i;

    uint64_t state = seed;
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)rnd_below(&state, (uint64_t)i);
        uint64_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
    return a;
}

static double chase_once(uint64_t *a, size_t n, uint64_t iters)
{
    uint64_t idx = 0;
    uint64_t t0 = now_ns();
    for (uint64_t i = 0; i < iters; i++) {
        idx = a[idx & (n - 1)];
    }
    uint64_t t1 = now_ns();
    g_sink ^= idx;
    return (double)(t1 - t0) / (double)iters;
}

static double chase_latency(size_t bytes, int samples, double target_sec)
{
    uint64_t *a = make_chase(bytes, 0x123456789abcdefULL ^ (uint64_t)bytes);
    size_t n = bytes / sizeof(uint64_t);
    if (n < 2)
        n = 2;

    uint64_t iters = 1ULL << 18;
    double ns = chase_once(a, n, iters);
    double target_ns = target_sec * 1e9;
    for (int k = 0; k < 8; k++) {
        if (ns * (double)iters >= target_ns * 0.7)
            break;
        double factor = target_ns / (ns * (double)iters);
        if (factor < 1.5) factor = 1.5;
        if (factor > 32.0) factor = 32.0;
        uint64_t next = (uint64_t)((double)iters * factor);
        if (next < 1ULL << 18) next = 1ULL << 18;
        if (next > 200000000ULL) next = 200000000ULL;
        iters = next;
        ns = chase_once(a, n, iters);
    }

    double v[MAX_SAMPLES];
    for (int s = 0; s < samples; s++) {
        double t = chase_once(a, n, iters);
        v[s] = t;
    }
    xfree(a);
    return median(v, samples);
}

static double read_once(uint64_t *a, size_t n, int repeat)
{
    uint64_t sum = 0;
    uint64_t t0 = now_ns();
    for (int r = 0; r < repeat; r++)
        for (size_t i = 0; i < n; i++)
            sum += a[i];
    uint64_t t1 = now_ns();
    g_sink ^= sum;
    return (double)(t1 - t0) / 1e9;
}

static double write_once(uint64_t *a, size_t n, int repeat)
{
    uint64_t t0 = now_ns();
    for (int r = 0; r < repeat; r++)
        for (size_t i = 0; i < n; i++)
            a[i] = (uint64_t)i + (uint64_t)r;
    uint64_t t1 = now_ns();
    g_sink ^= a[n / 2];
    return (double)(t1 - t0) / 1e9;
}

static double copy_once(uint64_t *a, uint64_t *b, size_t n, int repeat)
{
    uint64_t t0 = now_ns();
    for (int r = 0; r < repeat; r++)
        memcpy(b, a, n * sizeof(uint64_t));
    uint64_t t1 = now_ns();
    g_sink ^= b[n / 2];
    return (double)(t1 - t0) / 1e9;
}

static double triad_once(uint64_t *a, uint64_t *b, uint64_t *c, size_t n, int repeat)
{
    uint64_t t0 = now_ns();
    for (int r = 0; r < repeat; r++)
        for (size_t i = 0; i < n; i++)
            c[i] = a[i] + b[i] + (uint64_t)r;
    uint64_t t1 = now_ns();
    g_sink ^= c[n / 2];
    return (double)(t1 - t0) / 1e9;
}

static void cache_sizes(size_t *l1, size_t *l2, size_t *l3)
{
    *l1 = 32 * 1024;
    *l2 = 256 * 1024;
    *l3 = 8 * 1024 * 1024;
#if defined(__APPLE__)
    uint64_t v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname("hw.l1dcachesize", &v, &len, NULL, 0) == 0 && v > 0)
        *l1 = (size_t)v;
    len = sizeof(v);
    if (sysctlbyname("hw.l2cachesize", &v, &len, NULL, 0) == 0 && v > 0)
        *l2 = (size_t)v;
    len = sizeof(v);
    if (sysctlbyname("hw.l3cachesize", &v, &len, NULL, 0) == 0 && v > 0)
        *l3 = (size_t)v;
#elif defined(_WIN32)
    DWORD len = 0;
    if (!GetLogicalProcessorInformation(NULL, &len) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION *info = malloc(len);
        if (info && GetLogicalProcessorInformation(info, &len)) {
            DWORD count = len / (DWORD)sizeof(*info);
            size_t best[4] = {0, 0, 0, 0};
            for (DWORD i = 0; i < count; i++) {
                if (info[i].Relationship == RelationCache) {
                    DWORD level = info[i].Cache.Level;
                    if (level >= 1 && level <= 3 && info[i].Cache.Size > best[level])
                        best[level] = info[i].Cache.Size;
                }
            }
            if (best[1]) *l1 = best[1];
            if (best[2]) *l2 = best[2];
            if (best[3]) *l3 = best[3];
        }
        free(info);
    }
#endif
}

static void fmt_size(char *buf, size_t n, size_t bytes)
{
    if (bytes >= 1024 * 1024)
        snprintf(buf, n, "%zu MB", bytes / (1024 * 1024));
    else
        snprintf(buf, n, "%zu KB", bytes / 1024);
}

static void print_usage(const char *p)
{
    printf("Memory / Cache Benchmark " MEM_VERSION "\n\n");
    printf("Usage: %s [options]\n\n", p);
    printf("  --samples N   samples per measurement (default 3)\n");
    printf("  --size MB     working set for bandwidth (default 256)\n");
    printf("  --quick       one sample, 128 MB bandwidth set\n");
    printf("  --json PATH   write JSON\n");
    printf("  -h, --help    help\n");
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v <= 0 || v > 100000)
        return -1;
    *out = (int)v;
    return 0;
}

int main(int argc, char **argv)
{
    int samples = 3;
    size_t bw_mb = 256;
    const char *json_path = NULL;
    int quick = 0;

#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--quick")) {
            quick = 1;
        } else if (!strcmp(argv[i], "--samples") && i + 1 < argc) {
            if (parse_int(argv[++i], &samples) || samples > MAX_SAMPLES) {
                fprintf(stderr, "invalid --samples\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            int mb = 0;
            if (parse_int(argv[++i], &mb) || mb < 32) {
                fprintf(stderr, "invalid --size (>=32 MB)\n");
                return 2;
            }
            bw_mb = (size_t)mb;
        } else if (!strcmp(argv[i], "--json") && i + 1 < argc) {
            json_path = argv[++i];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    if (quick) {
        samples = 1;
        bw_mb = 128;
    }

    size_t l1, l2, l3;
    cache_sizes(&l1, &l2, &l3);
    size_t l1_set = l1 / 2;
    size_t l2_set = l2 * 3 / 4;
    size_t l3_set = l3 ? l3 * 3 / 4 : 8 * 1024 * 1024;
    if (l1_set < 8 * 1024) l1_set = 8 * 1024;
    if (l1_set > 64 * 1024) l1_set = 64 * 1024;
    if (l2_set < 64 * 1024) l2_set = 64 * 1024;
    if (l2_set > 4 * 1024 * 1024) l2_set = 4 * 1024 * 1024;
    if (l3_set < 2 * 1024 * 1024) l3_set = 2 * 1024 * 1024;
    if (l3_set > 128 * 1024 * 1024) l3_set = 128 * 1024 * 1024;
    l1_set = floor_pow2(l1_set);
    l2_set = floor_pow2(l2_set);
    l3_set = floor_pow2(l3_set);
    size_t dram_set = 256 * 1024 * 1024;

    double target = quick ? 0.10 : 0.20;
    double lat[4];
    lat[0] = chase_latency(l1_set, samples, target);
    lat[1] = chase_latency(l2_set, samples, target);
    lat[2] = chase_latency(l3_set, samples, target);
    lat[3] = chase_latency(dram_set, samples, target);

    size_t bytes = bw_mb * 1024 * 1024;
    size_t n = bytes / sizeof(uint64_t);
    uint64_t *a = xalloc(bytes);
    uint64_t *b = xalloc(bytes);
    uint64_t *c = xalloc(bytes);
    for (size_t i = 0; i < n; i++) {
        a[i] = i + 1;
        b[i] = i + 2;
        c[i] = 0;
    }

    int repeat = quick ? 2 : 4;
    double read_gbs = 0, write_gbs = 0, copy_gbs = 0, triad_gbs = 0;
    double rv[MAX_SAMPLES], wv[MAX_SAMPLES], cv[MAX_SAMPLES], tv[MAX_SAMPLES];
    for (int s = 0; s < samples; s++) {
        double sec = read_once(a, n, repeat);
        rv[s] = (double)bytes * repeat / sec / 1e9;
        sec = write_once(a, n, repeat);
        wv[s] = (double)bytes * repeat / sec / 1e9;
        sec = copy_once(a, b, n, repeat);
        cv[s] = (double)bytes * 2 * repeat / sec / 1e9;
        sec = triad_once(a, b, c, n, repeat);
        tv[s] = (double)bytes * 3 * repeat / sec / 1e9;
    }
    read_gbs = median(rv, samples);
    write_gbs = median(wv, samples);
    copy_gbs = median(cv, samples);
    triad_gbs = median(tv, samples);
    xfree(a); xfree(b); xfree(c);

    char s1[32], s2[32], s3[32], s4[32];
    fmt_size(s1, sizeof(s1), l1_set);
    fmt_size(s2, sizeof(s2), l2_set);
    fmt_size(s3, sizeof(s3), l3_set);
    fmt_size(s4, sizeof(s4), dram_set);

    printf("Memory / Cache Benchmark " MEM_VERSION "\n");
    printf("============================================================\n");
    printf("Working sets: L1 %s, L2 %s, L3/LLC %s, DRAM %s\n", s1, s2, s3, s4);
    printf("Samples: %d, bandwidth set: %zu MB\n\n", samples, bw_mb);

    printf("Latency (pointer chase, dependent load)\n");
    printf("------------------------------------------------------------\n");
    printf("%-12s %12s\n", "LEVEL", "ns/access");
    printf("%-12s %12.3f  (%s)\n", "L1", lat[0], s1);
    printf("%-12s %12.3f  (%s)\n", "L2", lat[1], s2);
    printf("%-12s %12.3f  (%s)\n", "L3/LLC", lat[2], s3);
    printf("%-12s %12.3f  (%s)\n", "DRAM", lat[3], s4);

    printf("\nBandwidth (sequential, %zu MB)\n", bw_mb);
    printf("------------------------------------------------------------\n");
    printf("%-12s %12s\n", "TEST", "GB/s");
    printf("%-12s %12.2f\n", "Read", read_gbs);
    printf("%-12s %12.2f\n", "Write", write_gbs);
    printf("%-12s %12.2f  (read+write)\n", "Copy", copy_gbs);
    printf("%-12s %12.2f  (2 read + 1 write)\n", "Triad", triad_gbs);
    printf("============================================================\n");
    printf("Independent Memory module; not included in CPU Compute score.\n");

    if (json_path) {
        FILE *fp = fopen(json_path, "w");
        if (!fp) {
            fprintf(stderr, "cannot write %s: %s\n", json_path, strerror(errno));
            return 1;
        }
        fprintf(fp, "{\n  \"version\": \"%s\",\n  \"type\": \"memory\",\n", MEM_VERSION);
        fprintf(fp, "  \"working_sets\": {\"l1\": %zu, \"l2\": %zu, \"l3\": %zu, \"dram\": %zu},\n",
                l1_set, l2_set, l3_set, dram_set);
        fprintf(fp, "  \"bandwidth_mb\": %zu,\n  \"samples\": %d,\n", bw_mb, samples);
        fprintf(fp, "  \"latency_ns\": {\"l1\": %.4f, \"l2\": %.4f, \"l3\": %.4f, \"dram\": %.4f},\n",
                lat[0], lat[1], lat[2], lat[3]);
        fprintf(fp, "  \"bandwidth_gbs\": {\"read\": %.3f, \"write\": %.3f, \"copy\": %.3f, \"triad\": %.3f}\n",
                read_gbs, write_gbs, copy_gbs, triad_gbs);
        fprintf(fp, "}\n");
        fclose(fp);
        printf("\nJSON written to %s\n", json_path);
    }
    return 0;
}
