// Exhaustive test for key-independent LSB collisions in the
// xorshift-multiply hash (16-bit analogue, shift = 8):
//
//   h(x, m1, m2, m3) = f(g(f(g(f(g(f(x), m1)), m2)), m3))
//   where f(x) = x ^ (x >> 8), g(x, m) = x * m  (mod 2^16)
//
// For every distinct input pair (x1, x2), checks whether the low k
// bits of the output agree for ALL valid key triples (m1, m2, m3).
// Keys are constrained to match the secret generation rules: each
// byte has exactly 4 bits set (balanced), and the lowest byte is odd.
//
// If any pair survives all key triples, it would indicate a structural
// weakness: those inputs always land in the same bucket regardless of
// key choice, making the collision unavoidable. The k=1 case is the
// strongest test. Agreement on more bits implies agreement on fewer,
// so zero survivors at k=1 rules out all higher k as well.

#include <stdio.h>
#include <stdint.h>
#include <time.h>

// f(x) = (x ^ (x >> 8)) & 0xFFFF  — operates on 16-bit value
static inline uint16_t f16(uint16_t x) { return x ^ (x >> 8); }

int main() {
    const int N = 65536;

    //   low byte: popcount=4 AND odd  (C(7,3) = 35 values)
    //   high byte: popcount=4         (C(8,4) = 70 values)
    //   Total: 35 * 70 = 2450
    uint16_t keys[2450];
    int nkeys = 0;
    for (int hi = 0; hi < 256; hi++) {
        if (__builtin_popcount(hi) != 4) continue;
        for (int lo = 0; lo < 256; lo++) {
            if (__builtin_popcount(lo) != 4 || !(lo & 1)) continue;
            keys[nkeys++] = (uint16_t)((hi << 8) | lo);
        }
    }

    const long long total_triples = (long long)nkeys * nkeys * nkeys;
    const long long total_pairs = (long long)N * (N - 1) / 2;
    const long long theoretical_max = total_pairs * total_triples * 2;

    printf("16-bit exhaustive LSB collision analysis\n");
    printf("Hash: h(x,m1,m2,m3) = f(g(f(g(f(g(f(x),m1)),m2)),m3))\n");
    printf("  f(x) = (x ^ (x >> 8)) & 0xFFFF\n");
    printf("  g(x,m) = (x * m) & 0xFFFF\n");
    printf("Domain: 0..65535, Keys: balanced-odd 16-bit (%d values)\n", nkeys);
    printf("Total pairs: %lld, Total key triples: %lld\n", total_pairs, total_triples);
    printf("Theoretical max hash evaluations: %lld\n\n", theoretical_max);

    for (int k = 1; k <= 4; k++) {
        uint16_t mask = (1 << k) - 1;
        printf("============================================\n");
        printf("  k = %d  (low %d bit%s, mask = 0x%X)\n",
               k, k, k > 1 ? "s" : "", mask);
        printf("============================================\n");

        long long bucket[8] = {};
        long long best_survived = -1;
        int best_x1 = -1, best_x2 = -1;
        long long total_hash_evals = 0;

        time_t start = time(NULL);

        for (int x1 = 0; x1 < N; x1++) {
            if (x1 % 1000 == 0) {
                fprintf(stderr, "\r  k=%d: x1 = %d / %d (%.1f%%)  [%.0fs elapsed]   ",
                        k, x1, N, 100.0 * x1 / N,
                        difftime(time(NULL), start));
                fflush(stderr);
            }

            uint16_t a1 = f16((uint16_t)x1);

            for (int x2 = x1 + 1; x2 < N; x2++) {
                uint16_t a2 = f16((uint16_t)x2);

                long long survived = 0;
                bool failed = false;

                for (int i1 = 0; i1 < nkeys && !failed; i1++) {
                    uint16_t c1 = f16((uint16_t)(a1 * keys[i1]));
                    uint16_t c2 = f16((uint16_t)(a2 * keys[i1]));
                    for (int i2 = 0; i2 < nkeys && !failed; i2++) {
                        uint16_t e1 = f16((uint16_t)(c1 * keys[i2]));
                        uint16_t e2 = f16((uint16_t)(c2 * keys[i2]));
                        for (int i3 = 0; i3 < nkeys; i3++) {
                            uint16_t r1 = f16((uint16_t)(e1 * keys[i3]));
                            uint16_t r2 = f16((uint16_t)(e2 * keys[i3]));
                            if ((r1 & mask) != (r2 & mask)) {
                                survived = (long long)i1 * nkeys * nkeys
                                         + (long long)i2 * nkeys + i3;
                                total_hash_evals += (survived + 1) * 2;
                                failed = true;
                                break;
                            }
                        }
                    }
                }

                if (!failed) {
                    survived = total_triples;
                    total_hash_evals += total_triples * 2;
                }

                if (survived == total_triples)  bucket[7]++;
                else if (survived == 0)         bucket[0]++;
                else if (survived == 1)         bucket[1]++;
                else if (survived <= 9)         bucket[2]++;
                else if (survived <= 99)        bucket[3]++;
                else if (survived <= 999)       bucket[4]++;
                else if (survived <= 9999)      bucket[5]++;
                else                            bucket[6]++;

                if (survived > best_survived) {
                    best_survived = survived;
                    best_x1 = x1;
                    best_x2 = x2;
                }
            }
        }

        double elapsed = difftime(time(NULL), start);
        fprintf(stderr, "\r  k=%d: done in %.0f seconds                              \n",
                k, elapsed);

        printf("Survival distribution:\n");
        printf("  0 triples:          %lld pairs\n", bucket[0]);
        printf("  1 triple:           %lld pairs\n", bucket[1]);
        printf("  2-9 triples:        %lld pairs\n", bucket[2]);
        printf("  10-99 triples:      %lld pairs\n", bucket[3]);
        printf("  100-999 triples:    %lld pairs\n", bucket[4]);
        printf("  1000-9999 triples:  %lld pairs\n", bucket[5]);
        printf("  10000+ triples:     %lld pairs\n", bucket[6]);
        printf("  ALL (%lld):   %lld pairs\n", total_triples, bucket[7]);
        printf("\n");
        printf("Best-surviving pair: (%d, %d) survived %lld / %lld triples\n",
               best_x1, best_x2, best_survived, total_triples);
        printf("Total hash evaluations:  %lld\n", total_hash_evals);
        printf("Theoretical max:         %lld\n", theoretical_max);
        printf("Coverage:                %.9f%%\n",
               100.0 * total_hash_evals / (double)theoretical_max);
        printf("\n>>> Pairs surviving ALL key triples for k=%d: %lld <<<\n\n",
               k, bucket[7]);
    }

    return 0;
}
