// Reed-Solomon FEC tests for reedsolomon/rs.c:
//  - parity bytes for fixed inputs must match the values produced by the original implementation
//    (CRC32 golden values), so the decoder stays compatible with the host's encoder;
//  - reed_solomon_reconstruct() must recover any pattern of up to p lost data shards, with the
//    missing shards in buffers full of stale bytes (as in RtpVideoQueue.c, which uses malloc()).
// Usage: tests/fec/fectest [--golden]   (--golden prints the table below instead of checking it)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rs.h"

#define BLOCK_SIZE 1408 // 1392-byte packetSize + MAX_RTP_HEADER_SIZE, the shard size used for video

static uint32_t rngState;
static uint32_t rnd(void) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

static uint32_t crc32(uint32_t crc, const unsigned char* p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

typedef struct { int k, p; uint32_t parityCrc; } SHAPE;

// Generated with the original rs.c (before the encode matrix change) using --golden. The same
// values come out of nanors, the encoder Helios uses, so these pin wire compatibility with the host.
static SHAPE shapes[] = {
    { 1, 1, 0x9c898895u },
    { 1, 2, 0x704407adu },
    { 2, 1, 0xdca33069u },
    { 4, 1, 0xd3cbf122u },
    { 10, 2, 0xbe74c7b4u },
    { 10, 10, 0xd9a42d45u },
    { 25, 5, 0x39f5fb16u },
    { 50, 10, 0xa1cf02b1u },
    { 100, 20, 0x0d8b9e31u },
    { 150, 30, 0xc4a43703u },
    { 200, 55, 0x26becc6cu },
    { 212, 43, 0x29005d9fu },
    { 240, 15, 0x1bb7e2deu },
};

static unsigned char** allocShards(int n) {
    unsigned char** s = malloc(n * sizeof(*s));
    for (int i = 0; i < n; i++)
        s[i] = malloc(BLOCK_SIZE);
    return s;
}

static void freeShards(unsigned char** s, int n) {
    for (int i = 0; i < n; i++)
        free(s[i]);
    free(s);
}

int main(int argc, char** argv) {
    int golden = argc > 1 && !strcmp(argv[1], "--golden");
    int failures = 0, recoveries = 0;

    reed_solomon_init();

    for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
        int k = shapes[s].k, p = shapes[s].p, n = k + p;
        unsigned char** orig = allocShards(n);

        rngState = 0x9E3779B9u ^ (uint32_t)(k * 131 + p);
        for (int i = 0; i < k; i++)
            for (int j = 0; j < BLOCK_SIZE; j++)
                orig[i][j] = (unsigned char)rnd();

        reed_solomon* rs = reed_solomon_new(k, p);
        if (rs == NULL) {
            printf("FAIL k=%d p=%d: reed_solomon_new() returned NULL\n", k, p);
            failures++;
            freeShards(orig, n);
            continue;
        }
        reed_solomon_encode(rs, orig, n, BLOCK_SIZE);

        uint32_t crc = 0;
        for (int i = k; i < n; i++)
            crc = crc32(crc, orig[i], BLOCK_SIZE);

        if (golden) {
            printf("    { %d, %d, 0x%08xu },\n", k, p, crc);
        }
        else if (crc != shapes[s].parityCrc) {
            printf("FAIL k=%d p=%d: parity CRC 0x%08x, expected 0x%08x\n", k, p, crc, shapes[s].parityCrc);
            failures++;
        }

        // Recover random patterns of 1..p lost data shards, plus "first p shards lost".
        for (int trial = 0; trial < 40 && !golden; trial++) {
            unsigned char** pk = calloc(n, sizeof(*pk));
            unsigned char* marks = calloc(n, 1);
            int lost = trial == 0 ? (p < k ? p : k) : 1 + (int)(rnd() % (uint32_t)p);
            if (lost > k)
                lost = k;

            for (int e = 0; e < lost; ) {
                int x = trial == 0 ? e : (int)(rnd() % (uint32_t)k);
                if (!marks[x]) {
                    marks[x] = 1;
                    e++;
                }
            }
            for (int i = 0; i < n; i++) {
                if (marks[i]) {
                    pk[i] = malloc(BLOCK_SIZE);
                    memset(pk[i], 0xA5, BLOCK_SIZE); // stale heap contents
                }
                else {
                    pk[i] = orig[i];
                }
            }

            int ret = reed_solomon_reconstruct(rs, pk, marks, n, BLOCK_SIZE);
            int bad = 0;
            for (int i = 0; i < k; i++)
                if (marks[i] && memcmp(pk[i], orig[i], BLOCK_SIZE))
                    bad++;
            if (ret != 0 || bad) {
                printf("FAIL k=%d p=%d lost=%d: ret=%d, %d corrupt shard(s)\n", k, p, lost, ret, bad);
                failures++;
            }
            recoveries++;

            for (int i = 0; i < n; i++)
                if (marks[i])
                    free(pk[i]);
            free(pk);
            free(marks);
        }

        reed_solomon_release(rs);
        freeShards(orig, n);
    }

    if (!golden)
        printf("%s: %zu shapes, %d recoveries, %d failure(s)\n", failures ? "FAILED" : "OK",
               sizeof(shapes) / sizeof(shapes[0]), recoveries, failures);
    return failures ? 1 : 0;
}
