/*
 * Benchmark 2.0 Application
 * -------------------------
 * A Geekbench-style, single-thread application mix.  Eight real workloads:
 * SHA-256, LZSS compression, JSON parsing, text search, image blur, sorting,
 * matrix multiplication and a memory copy.  One unified baseline (i7-9750H =
 * 1000); no per-architecture reference and no processor-specific intrinsics.
 *
 * Build macOS:  cc -O3 application_bench.c -o application-bench -lm
 * Build Windows: x86_64-w64-mingw32-gcc -O3 ... -luser32
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
#include <pthread.h>
#include <sched.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

#define APP_VERSION "2.0"
#define MAX_SAMPLES 15
#define MAX_THREADS 24

static volatile uint64_t g_sink;

/* ------------------------------------------------------------------ utils */
static uint64_t now_ns(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
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

static void *xalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "allocation failed: %zu bytes\n", n);
        exit(1);
    }
    return p;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n)
{
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    return (n & 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

static uint64_t splitmix64(uint64_t *s)
{
    uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* ------------------------------------------------------------------ SHA-256 */
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static void sha256_block(uint32_t h[8], const uint8_t p[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}
static void sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= len; i += 64) sha256_block(h, data + i);
    uint8_t tail[128];
    size_t rem = len - i;
    memcpy(tail, data + i, rem);
    tail[rem] = 0x80;
    size_t total = (rem < 56) ? 64 : 128;
    memset(tail + rem + 1, 0, total - rem - 1);
    uint64_t bits = (uint64_t)len * 8;
    for (int k = 0; k < 8; k++) tail[total - 1 - k] = (uint8_t)(bits >> (8*k));
    for (size_t off = 0; off < total; off += 64) sha256_block(h, tail + off);
    for (int k = 0; k < 8; k++) {
        out[k*4] = (uint8_t)(h[k] >> 24); out[k*4+1] = (uint8_t)(h[k] >> 16);
        out[k*4+2] = (uint8_t)(h[k] >> 8); out[k*4+3] = (uint8_t)h[k];
    }
}

/* ------------------------------------------------------------------ LZSS */
#define LZ_WINDOW 4096
#define LZ_MAX_MATCH 18
#define LZ_MIN_MATCH 3
#define LZ_HASH_SIZE 65536

typedef struct {
    int32_t head[LZ_HASH_SIZE];
    int32_t prev[LZ_WINDOW];
} lzss_state;

typedef struct worker_ctx {
    int tid;
    lzss_state *lz_st;
    uint8_t *lz_out, *lz_dec;
    uint8_t *img_out;
    uint32_t *sort_work;
    double *mat_c;
    uint8_t *copy_b;
    uint64_t result;
} worker_ctx;

static void lzss_init(lzss_state *st)
{
    for (int i = 0; i < LZ_HASH_SIZE; i++) st->head[i] = -1;
    for (int i = 0; i < LZ_WINDOW; i++) st->prev[i] = -1;
}
static unsigned lzss_hash(const uint8_t *p)
{
    return ((unsigned)p[0] << 10) ^ ((unsigned)p[1] << 5) ^ p[2];
}
static void lzss_insert(lzss_state *st, const uint8_t *in, size_t n, size_t pos)
{
    if (pos + 2 >= n) return;
    unsigned h = lzss_hash(in + pos) & (LZ_HASH_SIZE - 1);
    st->prev[pos & (LZ_WINDOW - 1)] = st->head[h];
    st->head[h] = (int32_t)pos;
}
static size_t lzss_compress(lzss_state *st, const uint8_t *in, size_t n,
                            uint8_t *out, size_t out_cap)
{
    lzss_init(st);
    size_t op = 0, flag_pos = 0;
    unsigned flag = 0;
    int bit = 0;
    for (size_t pos = 0; pos < n; ) {
        if (bit == 0) {
            if (op >= out_cap) return 0;
            flag_pos = op++;
            flag = 0;
        }
        size_t best_len = 0, best_dist = 0;
        if (pos + LZ_MIN_MATCH <= n) {
            unsigned h = lzss_hash(in + pos) & (LZ_HASH_SIZE - 1);
            int32_t cand = st->head[h];
            int tries = 0;
            while (cand >= 0 && pos - (size_t)cand <= LZ_WINDOW && tries < 64) {
                size_t max_len = n - pos;
                if (max_len > LZ_MAX_MATCH) max_len = LZ_MAX_MATCH;
                size_t len = 0;
                while (len < max_len && in[(size_t)cand + len] == in[pos + len]) len++;
                if (len > best_len && len >= LZ_MIN_MATCH) {
                    best_len = len;
                    best_dist = pos - (size_t)cand;
                }
                cand = st->prev[(size_t)cand & (LZ_WINDOW - 1)];
                tries++;
            }
        }
        if (best_len >= LZ_MIN_MATCH) {
            if (op + 2 > out_cap) return 0;
            unsigned code = (unsigned)(best_dist - 1) | ((unsigned)(best_len - LZ_MIN_MATCH) << 12);
            out[op++] = (uint8_t)(code & 0xff);
            out[op++] = (uint8_t)(code >> 8);
            flag |= 1u << bit;
            for (size_t k = 0; k < best_len; k++) lzss_insert(st, in, n, pos + k);
            pos += best_len;
        } else {
            if (op >= out_cap) return 0;
            out[op++] = in[pos];
            lzss_insert(st, in, n, pos);
            pos++;
        }
        bit++;
        if (bit == 8) { out[flag_pos] = (uint8_t)flag; bit = 0; }
    }
    if (bit) out[flag_pos] = (uint8_t)flag;
    return op;
}
static size_t lzss_decompress(const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_cap)
{
    size_t ip = 0, op = 0;
    while (ip < in_len) {
        uint8_t flag = in[ip++];
        for (int bit = 0; bit < 8 && ip < in_len; bit++) {
            if (flag & (1u << bit)) {
                if (ip + 1 >= in_len) return 0;
                unsigned code = (unsigned)in[ip] | ((unsigned)in[ip+1] << 8);
                ip += 2;
                size_t dist = (code & 0x0fff) + 1;
                size_t len = (code >> 12) + LZ_MIN_MATCH;
                if (dist > op || op + len > out_cap) return 0;
                for (size_t k = 0; k < len; k++) {
                    out[op] = out[op - dist];
                    op++;
                }
            } else {
                if (op >= out_cap) return 0;
                out[op++] = in[ip++];
            }
        }
    }
    return op;
}

/* ------------------------------------------------------------------ data */
static uint8_t *sha_data;
static size_t sha_size = 8 * 1024 * 1024;

static uint8_t *lz_in;
static size_t lz_size = 4 * 1024 * 1024;
static size_t lz_cap;

static char *json_data;
static size_t json_size;

static char *text_data;
static size_t text_size = 8 * 1024 * 1024;
static const char *patterns[] = {
    "application", "benchmark", "processor", "performance", "architecture",
    "instruction", "compiler", "memory", "cache", "integer", "floating",
    "portable", "algorithm", "workload", "baseline", "throughput"
};
#define PATTERN_COUNT (sizeof(patterns) / sizeof(patterns[0]))

static uint8_t *img_in;
static const int IMG_W = 1024, IMG_H = 1024;

static uint32_t *sort_orig;
static const size_t SORT_N = 1u << 20;

static double *mat_a, *mat_b;
static const int MAT_N = 256;

static uint8_t *copy_a;
static const size_t COPY_SIZE = 16u * 1024u * 1024u;

static worker_ctx *workers;

/* ------------------------------------------------------------------ prepare */
static void prepare_all(int nthreads)
{
    uint64_t seed = 0x123456789abcdef0ULL;
    sha_data = xalloc(sha_size);
    for (size_t i = 0; i < sha_size; i++) sha_data[i] = (uint8_t)splitmix64(&seed);

    lz_cap = lz_size + lz_size / 8 + 32;
    lz_in = xalloc(lz_size);
    for (size_t i = 0; i < lz_size; i++) {
        if ((i / 64) % 3 == 0) lz_in[i] = (uint8_t)("Benchmark application data "[i % 30]);
        else lz_in[i] = (uint8_t)splitmix64(&seed);
    }

    size_t cap = 4 * 1024 * 1024;
    json_data = xalloc(cap);
    size_t p = 0;
    p += (size_t)snprintf(json_data + p, cap - p, "{\"users\":[");
    for (int i = 0; i < 18000 && p + 400 < cap; i++) {
        p += (size_t)snprintf(json_data + p, cap - p,
            "%s{\"id\":%d,\"name\":\"user_%d\",\"active\":%s,"
            "\"score\":%.3f,\"tags\":[\"alpha\",\"beta\",\"gamma\"]}",
            i ? "," : "", i, i, (i & 1) ? "true" : "false", (double)(i % 1000) / 7.0);
    }
    p += (size_t)snprintf(json_data + p, cap - p, "],\"count\":18000,\"ok\":true}");
    json_size = p;

    text_data = xalloc(text_size + 1);
    const char *words[] = {"The quick brown fox jumps over the lazy dog. ",
                           "A benchmark application parses, compresses and sorts data. ",
                           "Processor architecture and memory hierarchy matter. "};
    size_t wp = 0;
    while (wp < text_size) {
        const char *w = words[splitmix64(&seed) % 3];
        size_t wl = strlen(w);
        if (wp + wl > text_size) break;
        memcpy(text_data + wp, w, wl);
        wp += wl;
    }
    text_data[wp] = '\0';
    text_size = wp;

    img_in = xalloc((size_t)IMG_W * IMG_H * 3);
    for (int y = 0; y < IMG_H; y++)
        for (int x = 0; x < IMG_W; x++) {
            size_t o = ((size_t)y * IMG_W + x) * 3;
            img_in[o] = (uint8_t)(x + y);
            img_in[o+1] = (uint8_t)(x * 3 + y);
            img_in[o+2] = (uint8_t)(x ^ y);
        }

    sort_orig = xalloc(SORT_N * sizeof(uint32_t));
    for (size_t i = 0; i < SORT_N; i++) sort_orig[i] = (uint32_t)splitmix64(&seed);

    mat_a = xalloc(MAT_N * MAT_N * sizeof(double));
    mat_b = xalloc(MAT_N * MAT_N * sizeof(double));
    for (int i = 0; i < MAT_N * MAT_N; i++) {
        mat_a[i] = (double)((i * 17 + 3) % 101) / 101.0;
        mat_b[i] = (double)((i * 29 + 7) % 97) / 97.0;
    }

    copy_a = xalloc(COPY_SIZE);
    for (size_t i = 0; i < COPY_SIZE; i++) copy_a[i] = (uint8_t)i;

    workers = calloc((size_t)nthreads, sizeof(worker_ctx));
    if (!workers) { fprintf(stderr, "worker alloc failed\n"); exit(1); }
    for (int t = 0; t < nthreads; t++) {
        workers[t].tid = t;
        workers[t].lz_st = xalloc(sizeof(lzss_state));
        workers[t].lz_out = xalloc(lz_cap);
        workers[t].lz_dec = xalloc(lz_cap);
        workers[t].img_out = xalloc((size_t)IMG_W * IMG_H * 3);
        workers[t].sort_work = xalloc(SORT_N * sizeof(uint32_t));
        workers[t].mat_c = xalloc(MAT_N * MAT_N * sizeof(double));
        workers[t].copy_b = xalloc(COPY_SIZE);
    }
}

/* ------------------------------------------------------------------ tasks */
static int same_bytes(const uint8_t *a, const uint8_t *b, size_t n);

static uint64_t run_sha(worker_ctx *w, int reps)
{
    (void)w;
    uint8_t digest[32];
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++) {
        sha256(sha_data, sha_size, digest);
        for (int i = 0; i < 32; i++) sum += digest[i];
    }
    return sum;
}
static uint64_t run_lzss(worker_ctx *w, int reps)
{
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++) {
        size_t clen = lzss_compress(w->lz_st, lz_in, lz_size, w->lz_out, lz_cap);
        size_t dlen = lzss_decompress(w->lz_out, clen, w->lz_dec, lz_cap);
        if (dlen != lz_size || !same_bytes(lz_in, w->lz_dec, lz_size)) {
            fprintf(stderr, "LZSS verification failed\n");
            exit(1);
        }
        sum += clen;
    }
    return sum;
}

/* JSON parser */
typedef struct {
    const char *s;
    size_t n, i;
    uint64_t hash, count;
    int depth;
} jparser;
static void jp_ws(jparser *p)
{
    while (p->i < p->n) {
        char c = p->s[p->i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') p->i++;
        else break;
    }
}
static int jp_value(jparser *p);
static int jp_string(jparser *p)
{
    if (p->i >= p->n || p->s[p->i] != '"') return -1;
    p->i++;
    while (p->i < p->n) {
        unsigned char c = (unsigned char)p->s[p->i++];
        if (c == '"') { p->count++; p->hash = p->hash * 131 + 7; return 0; }
        if (c == '\\') {
            if (p->i >= p->n) return -1;
            c = (unsigned char)p->s[p->i++];
        }
        p->hash = p->hash * 131 + c;
    }
    return -1;
}
static int jp_number(jparser *p)
{
    size_t st = p->i;
    if (p->i < p->n && (p->s[p->i] == '-' || p->s[p->i] == '+')) p->i++;
    while (p->i < p->n && ((p->s[p->i] >= '0' && p->s[p->i] <= '9') ||
                           p->s[p->i] == '.' || p->s[p->i] == 'e' || p->s[p->i] == 'E' ||
                           p->s[p->i] == '+' || p->s[p->i] == '-')) p->i++;
    if (p->i == st) return -1;
    p->count++;
    p->hash = p->hash * 131 + (unsigned char)p->s[st];
    return 0;
}
static int jp_object(jparser *p)
{
    if (++p->depth > 64) return -1;
    p->i++; jp_ws(p);
    if (p->i < p->n && p->s[p->i] == '}') { p->i++; p->count++; p->depth--; return 0; }
    for (;;) {
        jp_ws(p);
        if (jp_string(p) != 0) return -1;
        jp_ws(p);
        if (p->i >= p->n || p->s[p->i] != ':') return -1;
        p->i++; jp_ws(p);
        if (jp_value(p) != 0) return -1;
        jp_ws(p);
        if (p->i < p->n && p->s[p->i] == ',') { p->i++; continue; }
        if (p->i < p->n && p->s[p->i] == '}') { p->i++; p->depth--; return 0; }
        return -1;
    }
}
static int jp_array(jparser *p)
{
    if (++p->depth > 64) return -1;
    p->i++; jp_ws(p);
    if (p->i < p->n && p->s[p->i] == ']') { p->i++; p->count++; p->depth--; return 0; }
    for (;;) {
        jp_ws(p);
        if (jp_value(p) != 0) return -1;
        jp_ws(p);
        if (p->i < p->n && p->s[p->i] == ',') { p->i++; continue; }
        if (p->i < p->n && p->s[p->i] == ']') { p->i++; p->depth--; return 0; }
        return -1;
    }
}
static int jp_value(jparser *p)
{
    jp_ws(p);
    if (p->i >= p->n) return -1;
    char c = p->s[p->i];
    if (c == '{') return jp_object(p);
    if (c == '[') return jp_array(p);
    if (c == '"') return jp_string(p);
    if (c == 't' && p->i + 4 <= p->n && !memcmp(p->s + p->i, "true", 4)) {
        p->i += 4; p->count++; p->hash = p->hash * 131 + 1; return 0;
    }
    if (c == 'f' && p->i + 5 <= p->n && !memcmp(p->s + p->i, "false", 5)) {
        p->i += 5; p->count++; p->hash = p->hash * 131 + 2; return 0;
    }
    if (c == 'n' && p->i + 4 <= p->n && !memcmp(p->s + p->i, "null", 4)) {
        p->i += 4; p->count++; p->hash = p->hash * 131 + 3; return 0;
    }
    return jp_number(p);
}
static uint64_t run_json(worker_ctx *w, int reps)
{
    (void)w;
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++) {
        jparser p;
        p.s = json_data; p.n = json_size; p.i = 0;
        p.hash = 0; p.count = 0; p.depth = 0;
        if (jp_value(&p) != 0 || p.i != p.n) {
            fprintf(stderr, "JSON parse failed at %zu/%zu\n", p.i, p.n);
            exit(1);
        }
        sum += p.count + p.hash;
    }
    return sum;
}

static int same_bytes(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* Boyer-Moore-Horspool: same C code on every platform (no libc strstr). */
static uint64_t text_hits(const char *text, size_t n, const char *pat)
{
    size_t m = strlen(pat);
    if (m == 0 || m > n) return 0;
    size_t skip[256];
    for (size_t i = 0; i < 256; i++) skip[i] = m;
    for (size_t i = 0; i + 1 < m; i++) skip[(unsigned char)pat[i]] = m - 1 - i;
    size_t i = 0;
    uint64_t hits = 0;
    while (i <= n - m) {
        size_t j = m - 1;
        while (j > 0 && text[i + j] == pat[j]) j--;
        if (text[i + j] == pat[j]) {
            hits++;
            i += 1;
        } else {
            size_t s = skip[(unsigned char)text[i + m - 1]];
            i += (s ? s : 1);
        }
    }
    return hits;
}

/* In-place heapsort: identical algorithm on x86 and ARM, no libc qsort. */
static void sift_down(uint32_t *a, size_t start, size_t end)
{
    size_t root = start;
    while (root * 2 + 1 < end) {
        size_t child = root * 2 + 1;
        if (child + 1 < end && a[child] < a[child + 1]) child++;
        if (a[root] < a[child]) {
            uint32_t t = a[root]; a[root] = a[child]; a[child] = t;
            root = child;
        } else return;
    }
}
static void heap_sort(uint32_t *a, size_t n)
{
    for (size_t start = n / 2; start > 0; --start)
        sift_down(a, start - 1, n);
    for (size_t end = n - 1; end > 0; --end) {
        uint32_t t = a[0]; a[0] = a[end]; a[end] = t;
        sift_down(a, 0, end);
    }
}
static void copy_words(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    uint64_t *d = (uint64_t *)dst;
    const uint64_t *s = (const uint64_t *)src;
    size_t n = bytes / sizeof(uint64_t);
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static uint64_t run_text(worker_ctx *w, int reps)
{
    (void)w;
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++)
        for (size_t i = 0; i < PATTERN_COUNT; i++)
            sum += text_hits(text_data, text_size, patterns[i]);
    return sum;
}

static uint64_t run_blur(worker_ctx *w, int reps)
{
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++) {
        for (int y = 0; y < IMG_H; y++) {
            for (int x = 0; x < IMG_W; x++) {
                int sr = 0, sg = 0, sb = 0, cnt = 0;
                for (int dy = -2; dy <= 2; dy++) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= IMG_H) continue;
                    for (int dx = -2; dx <= 2; dx++) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= IMG_W) continue;
                        size_t o = ((size_t)yy * IMG_W + xx) * 3;
                        sr += img_in[o]; sg += img_in[o+1]; sb += img_in[o+2];
                        cnt++;
                    }
                }
                size_t o = ((size_t)y * IMG_W + x) * 3;
                w->img_out[o] = (uint8_t)(sr / cnt);
                w->img_out[o+1] = (uint8_t)(sg / cnt);
                w->img_out[o+2] = (uint8_t)(sb / cnt);
            }
        }
        for (size_t i = 0; i < (size_t)IMG_W * IMG_H * 3; i += 4096) sum += w->img_out[i];
    }
    return sum;
}

static uint64_t run_sort(worker_ctx *w, int reps)
{
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++) {
        copy_words((uint8_t *)w->sort_work, (const uint8_t *)sort_orig,
                   SORT_N * sizeof(uint32_t));
        heap_sort(w->sort_work, SORT_N);
        for (size_t i = 0; i < SORT_N; i += 4096) sum += w->sort_work[i];
    }
    return sum;
}

static uint64_t run_matmul(worker_ctx *w, int reps)
{
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < MAT_N; i++)
            for (int j = 0; j < MAT_N; j++)
                w->mat_c[i*MAT_N+j] = 0.0;
        for (int i = 0; i < MAT_N; i++) {
            for (int k = 0; k < MAT_N; k++) {
                double aik = mat_a[i*MAT_N+k];
                for (int j = 0; j < MAT_N; j++)
                    w->mat_c[i*MAT_N+j] += aik * mat_b[k*MAT_N+j];
            }
        }
        for (int i = 0; i < MAT_N * MAT_N; i += 4096) sum += (uint64_t)w->mat_c[i];
    }
    return sum;
}

static uint64_t run_copy(worker_ctx *w, int reps)
{
    uint64_t sum = 0;
    for (int r = 0; r < reps; r++) {
        copy_words(w->copy_b, copy_a, COPY_SIZE);
        for (size_t i = 0; i < COPY_SIZE; i += 4096) sum += w->copy_b[i];
    }
    return sum;
}

/* ------------------------------------------------------------------ table */
typedef struct {
    const char *id;
    const char *name;
    const char *category;
    const char *unit;
    double ops;
    double unit_scale;
    double ref;
    uint64_t (*run)(worker_ctx *, int);
} app_task;

static app_task tasks[] = {
    {"sha256",  "SHA-256",       "Crypto",      "MB/s",   8.0*1024*1024, 1e6, 242.0, run_sha},
    {"lzss",    "LZSS compress", "Compression", "MB/s",   4.0*1024*1024, 1e6, 75.9, run_lzss},
    {"json",    "JSON parse",    "Parsing",     "MB/s",   1.0, 1e6, 686.6, run_json},
    {"text",    "Text search",   "Text",        "MB/s",   1.0, 1e6, 1592.2, run_text},
    {"blur",    "Image blur 5x5","Imaging",     "MPix/s", 1024.0*1024.0, 1e6, 43.87, run_blur},
    {"sort",    "Sort 1M u32",   "Sorting",     "Melem/s",1.0*1024*1024, 1e6, 8.28, run_sort},
    {"matmul",  "Matmul 256^3",  "Numeric",     "GFLOP/s",2.0*256*256*256, 1e9, 7.96, run_matmul},
    {"copy",    "Memory copy",   "Memory",      "GB/s",   16.0*1024*1024*2, 1e9, 13.64, run_copy},
};
#define TASK_COUNT (sizeof(tasks) / sizeof(tasks[0]))

typedef struct {
    const app_task *task;
    int reps;
    int tid;
    volatile int *go;
} thread_arg;

#if defined(_WIN32)
static DWORD WINAPI worker_entry(LPVOID p)
#else
static void *worker_entry(void *p)
#endif
{
    thread_arg *a = (thread_arg *)p;
    while (!*a->go) {
#if defined(_WIN32)
        Sleep(0);
#else
        sched_yield();
#endif
    }
    workers[a->tid].result = a->task->run(&workers[a->tid], a->reps);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static double time_task(const app_task *t, int threads, int reps)
{
    if (threads <= 1) {
        uint64_t t0 = now_ns();
        uint64_t c = t->run(&workers[0], reps);
        uint64_t t1 = now_ns();
        g_sink ^= c;
        double sec = (double)(t1 - t0) / 1e9;
        return sec > 1e-9 ? sec : 1e-9;
    }

    thread_arg args[MAX_THREADS];
#if defined(_WIN32)
    HANDLE win_handles[MAX_THREADS];
#else
    pthread_t handles[MAX_THREADS];
#endif
    volatile int go = 0;
    for (int i = 0; i < threads; i++) {
        args[i].task = t;
        args[i].reps = reps;
        args[i].tid = i;
        args[i].go = &go;
    }
    for (int i = 0; i < threads; i++) {
#if defined(_WIN32)
        win_handles[i] = CreateThread(NULL, 0, worker_entry, &args[i], 0, NULL);
        if (!win_handles[i]) { fprintf(stderr, "CreateThread failed\n"); exit(1); }
#else
        if (pthread_create(&handles[i], NULL, worker_entry, &args[i]) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            exit(1);
        }
#endif
    }
    uint64_t t0 = now_ns();
    go = 1;
    uint64_t c = 0;
    for (int i = 0; i < threads; i++) {
#if defined(_WIN32)
        WaitForSingleObject(win_handles[i], INFINITE);
        CloseHandle(win_handles[i]);
#else
        pthread_join(handles[i], NULL);
#endif
        c ^= workers[i].result;
    }
    uint64_t t1 = now_ns();
    g_sink ^= c;
    double sec = (double)(t1 - t0) / 1e9;
    return sec > 1e-9 ? sec : 1e-9;
}

static void calibrate(const app_task *t, int threads, int quick, int *out_reps)
{
    int reps = 1;
    double target = quick ? 0.10 : 0.20;
    double sec = 0;
    for (int k = 0; k < 12; k++) {
        sec = time_task(t, threads, reps);
        if (sec >= target * 0.7) break;
        double f = target / (sec > 1e-6 ? sec : 1e-6) * 0.85;
        if (f < 1.5) f = 1.5;
        if (f > 16.0) f = 16.0;
        int next = (int)((double)reps * f + 0.5);
        if (next <= reps) next = reps + 1;
        if (next > 1000000) next = 1000000;
        reps = next;
    }
    *out_reps = reps;
}

static double measure_task(const app_task *t, int threads, int samples, int *out_reps)
{
    int reps = 1;
    calibrate(t, threads, 0, &reps);
    time_task(t, threads, reps);
    double v[MAX_SAMPLES];
    for (int s = 0; s < samples; s++) {
        double sec = time_task(t, threads, reps);
        double ops = t->ops * (double)reps * (double)threads;
        v[s] = ops / sec / t->unit_scale;
    }
    *out_reps = reps;
    return median(v, samples);
}

static void print_usage(const char *p)
{
    printf("Benchmark 2.0 Application " APP_VERSION "\n\n");
    printf("Usage: %s [options]\n\n", p);
    printf("  --samples N   samples per task (default 3)\n");
    printf("  --quick       one sample, shorter calibration\n");
    printf("  --json PATH   write JSON\n");
    printf("  -h, --help    help\n");
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v <= 0 || v > 100000) return -1;
    *out = (int)v;
    return 0;
}

int main(int argc, char **argv)
{
    int samples = 3;
    int quick = 0;
    const char *json_path = NULL;
#if defined(_WIN32)
    int threads = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int threads = (ncpu > 0) ? (int)ncpu : 4;
#endif
    if (threads < 1) threads = 1;
    if (threads > MAX_THREADS) threads = MAX_THREADS;

#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { print_usage(argv[0]); return 0; }
        else if (!strcmp(argv[i], "--quick")) quick = 1;
        else if (!strcmp(argv[i], "--samples") && i + 1 < argc) {
            if (parse_int(argv[++i], &samples) || samples > MAX_SAMPLES) {
                fprintf(stderr, "invalid --samples\n"); return 2;
            }
        } else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            if (parse_int(argv[++i], &threads) || threads > MAX_THREADS) {
                fprintf(stderr, "invalid --threads (1..%d)\n", MAX_THREADS); return 2;
            }
        } else if (!strcmp(argv[i], "--json") && i + 1 < argc) json_path = argv[++i];
        else { fprintf(stderr, "unknown option: %s\n", argv[i]); return 2; }
    }
    if (quick) samples = 1;
    if (threads < 1) threads = 1;
    if (threads > MAX_THREADS) threads = MAX_THREADS;

    prepare_all(threads);
    tasks[2].ops = (double)json_size;
    tasks[3].ops = (double)text_size * (double)PATTERN_COUNT;

    char cpu[256] = "Unknown CPU";
#if defined(__APPLE__)
    size_t len = sizeof(cpu);
    sysctlbyname("machdep.cpu.brand_string", cpu, &len, NULL, 0);
#elif defined(_WIN32)
    const char *id = getenv("PROCESSOR_IDENTIFIER");
    if (id) snprintf(cpu, sizeof(cpu), "%s", id);
#endif

    printf("Benchmark 2.0 Application " APP_VERSION "\n");
    printf("============================================================\n");
    printf("CPU        %s\n", cpu);
    printf("Mode       single + multi application mix\n");
    printf("Threads    %d\n", threads);
    printf("Samples    %d\n\n", samples);

    double single_scores[TASK_COUNT], multi_scores[TASK_COUNT];
    double single_rates[TASK_COUNT], multi_rates[TASK_COUNT];

    printf("Single-thread application\n");
    printf("%-18s %-13s %16s %14s %10s\n", "TEST", "CATEGORY", "THROUGHPUT", "REFERENCE", "SCORE");
    printf("--------------------------------------------------------------------------\n");
    for (size_t i = 0; i < TASK_COUNT; i++) {
        const app_task *t = &tasks[i];
        int reps = 1;
        double rate = measure_task(t, 1, samples, &reps);
        single_rates[i] = rate;
        single_scores[i] = 1000.0 * rate / t->ref;
        printf("%-18s %-13s %10.2f %-5s %14.2f %10.0f\n",
               t->name, t->category, rate, t->unit, t->ref, single_scores[i]);
    }
    double ss = 0.0;
    for (size_t i = 0; i < TASK_COUNT; i++) ss += log(single_scores[i]);
    double single_score = exp(ss / (double)TASK_COUNT);
    printf("--------------------------------------------------------------------------\n");
    printf("%-18s %56.0f\n", "SINGLE APP SCORE", single_score);
    printf("============================================================\n\n");

    if (threads > 1) {
        printf("Multi-core application (%d threads)\n", threads);
        printf("%-18s %-13s %16s %14s %10s\n", "TEST", "CATEGORY", "THROUGHPUT", "REFERENCE", "SCORE");
        printf("--------------------------------------------------------------------------\n");
        for (size_t i = 0; i < TASK_COUNT; i++) {
            const app_task *t = &tasks[i];
            int reps = 1;
            double rate = measure_task(t, threads, samples, &reps);
            multi_rates[i] = rate;
            multi_scores[i] = 1000.0 * rate / t->ref;
            printf("%-18s %-13s %10.2f %-5s %14.2f %10.0f\n",
                   t->name, t->category, rate, t->unit, t->ref, multi_scores[i]);
        }
        double ms = 0.0, sc = 0.0;
        for (size_t i = 0; i < TASK_COUNT; i++) {
            ms += log(multi_scores[i]);
            sc += log(multi_rates[i] / single_rates[i]);
        }
        double multi_score = exp(ms / (double)TASK_COUNT);
        double scale = exp(sc / (double)TASK_COUNT);
        double overall = sqrt(single_score * multi_score);
        printf("--------------------------------------------------------------------------\n");
        printf("%-18s %56.0f\n", "MULTI APP SCORE", multi_score);
        printf("%-18s %55.2fx\n", "MULTI SCALE", scale);
        printf("============================================================\n");
        printf("%-18s %56.0f\n", "OVERALL", overall);
        printf("============================================================\n");
        printf("Unified baseline: Intel Core i7-9750H = 1000 per task.\n");
        printf("MULTI SCORE = SINGLE x measured scaling; OVERALL = sqrt(SINGLE x MULTI).\n");

        if (json_path) {
            FILE *fp = fopen(json_path, "w");
            if (!fp) { fprintf(stderr, "cannot write %s: %s\n", json_path, strerror(errno)); return 1; }
            fprintf(fp, "{\n  \"version\": \"%s\",\n  \"type\": \"application\",\n", APP_VERSION);
            fprintf(fp, "  \"mode\": \"single+multi\",\n  \"threads\": %d,\n  \"samples\": %d,\n", threads, samples);
            fprintf(fp, "  \"cpu\": \"%s\",\n", cpu);
            fprintf(fp, "  \"scores\": {\"single\": %.2f, \"multi\": %.2f, \"overall\": %.2f, \"scale\": %.4f},\n",
                    single_score, multi_score, overall, scale);
            fprintf(fp, "  \"tasks\": [\n");
            for (size_t i = 0; i < TASK_COUNT; i++) {
                fprintf(fp, "    {\"id\":\"%s\",\"name\":\"%s\",\"category\":\"%s\","
                            "\"single_throughput\":%.4f,\"multi_throughput\":%.4f,"
                            "\"unit\":\"%s\",\"reference\":%.4f,"
                            "\"single_score\":%.2f,\"multi_score\":%.2f}%s\n",
                        tasks[i].id, tasks[i].name, tasks[i].category,
                        single_rates[i], multi_rates[i], tasks[i].unit, tasks[i].ref,
                        single_scores[i], multi_scores[i],
                        (i + 1 < TASK_COUNT) ? "," : "");
            }
            fprintf(fp, "  ]\n}\n");
            fclose(fp);
            printf("\nJSON written to %s\n", json_path);
        }
    } else {
        printf("Multi-core skipped (threads = 1).\n");
        printf("Unified baseline: Intel Core i7-9750H = 1000 per task.\n");
        if (json_path) {
            FILE *fp = fopen(json_path, "w");
            if (!fp) { fprintf(stderr, "cannot write %s: %s\n", json_path, strerror(errno)); return 1; }
            fprintf(fp, "{\n  \"version\": \"%s\",\n  \"type\": \"application\",\n", APP_VERSION);
            fprintf(fp, "  \"mode\": \"single\",\n  \"threads\": 1,\n  \"samples\": %d,\n", samples);
            fprintf(fp, "  \"cpu\": \"%s\",\n  \"scores\": {\"single\": %.2f, \"overall\": %.2f},\n",
                    cpu, single_score, single_score);
            fprintf(fp, "  \"tasks\": [\n");
            for (size_t i = 0; i < TASK_COUNT; i++) {
                fprintf(fp, "    {\"id\":\"%s\",\"name\":\"%s\",\"category\":\"%s\","
                            "\"single_throughput\":%.4f,\"unit\":\"%s\",\"reference\":%.4f,"
                            "\"single_score\":%.2f}%s\n",
                        tasks[i].id, tasks[i].name, tasks[i].category,
                        single_rates[i], tasks[i].unit, tasks[i].ref, single_scores[i],
                        (i + 1 < TASK_COUNT) ? "," : "");
            }
            fprintf(fp, "  ]\n}\n");
            fclose(fp);
            printf("\nJSON written to %s\n", json_path);
        }
    }
    return 0;
}
