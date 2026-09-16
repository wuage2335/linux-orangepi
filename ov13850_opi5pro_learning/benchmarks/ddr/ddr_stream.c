/* CPU可见的顺序内存带宽；不是DFI全通道物理字节计数。 */
#define _GNU_SOURCE
#include <arm_neon.h>
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile uint64_t sink;

static double now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) abort();
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

__attribute__((noinline)) static uint64_t read_all(const uint64_t *p, size_t n)
{
    uint64x2_t a = vdupq_n_u64(0), b = a, c = a, d = a;
    for (size_t i = 0; i < n; i += 8) {
        a = vaddq_u64(a, vld1q_u64(p + i));
        b = vaddq_u64(b, vld1q_u64(p + i + 2));
        c = vaddq_u64(c, vld1q_u64(p + i + 4));
        d = vaddq_u64(d, vld1q_u64(p + i + 6));
    }
    return vaddvq_u64(vaddq_u64(vaddq_u64(a,b), vaddq_u64(c,d)));
}

static void pass(const char *op, void *dst, const void *src, size_t size)
{
    if (!strcmp(op, "read")) sink = read_all(src, size / 8);
    else if (!strcmp(op, "write")) memset(dst, 0xa5, size);
    else memcpy(dst, src, size);
    /* 防止编译器删除重复访存，等待本次CPU访存指令完成。 */
    __asm__ volatile("dsb ish" ::: "memory");
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s <read|write|copy> <MiB:1..256> <seconds:0.1..5> <repeats:1..5>\n", argv[0]);
        return 2;
    }
    char *end;
    const char *op = argv[1];
    if (strcmp(op,"read") && strcmp(op,"write") && strcmp(op,"copy")) return 2;
    long mib = strtol(argv[2], &end, 10);
    if (*end || mib < 1 || mib > 256) return 2;
    double duration = strtod(argv[3], &end);
    if (*end || !(duration >= .1 && duration <= 5)) return 2;
    long repeats = strtol(argv[4], &end, 10);
    if (*end || repeats < 1 || repeats > 5) return 2;
    const size_t size = (size_t)mib * 1024 * 1024;
    void *src = NULL, *dst = NULL;
    if (posix_memalign(&src, 4096, size) || posix_memalign(&dst, 4096, size)) {
        free(src); free(dst); return 1;
    }
    memset(src, 0x3c, size);
    memset(dst, 0, size);
    const uint64_t expected = (uint64_t)(size / 8) * UINT64_C(0x3c3c3c3c3c3c3c3c);
    /* 预触页和预热不计入结果。 */
    double warm = now();
    do { pass(op, dst, src, size); } while (now() - warm < .5);
    for (long r = 0; r < repeats; ++r) {
        unsigned long iterations = 0;
        double start = now(), elapsed;
        do {
            pass(op, dst, src, size);
            ++iterations;
            elapsed = now() - start;
        } while (elapsed < duration);
        int valid = 1;
        if (!strcmp(op, "read")) valid = sink == expected;
        else if (!strcmp(op, "copy")) valid = !memcmp(src, dst, size);
        else {
            const unsigned char *p = dst;
            for (size_t i = 0; i < size; ++i) if (p[i] != 0xa5) { valid = 0; break; }
        }
        printf("{\"op\":\"%s\",\"cpu\":%d,\"bytes\":%zu,\"repeat\":%ld,\"iterations\":%lu,\"start_s\":%.9f,\"end_s\":%.9f,\"seconds\":%.9f,\"payload_MB_s\":%.3f,\"valid\":%s}\n",
               op, sched_getcpu(), size, r + 1, iterations, start, start + elapsed, elapsed,
               (double)size * iterations / elapsed / 1000000.0, valid ? "true" : "false");
        fflush(stdout);
        if (!valid) { free(src); free(dst); return 1; }
    }
    free(src); free(dst);
    return 0;
}
