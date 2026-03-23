// Bit Independence Criterion (BIC) test for the 24-bit xorshift-multiply hash.
//
// Adapted from SMHasher3's BitIndependenceTest.
//
// For each input bit flip, tests whether pairs of output bits change
// independently using a chi-square test on 2x2 contingency tables.
// If output bits are independent, knowing whether bit i flipped tells
// you nothing about whether bit j flipped.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <time.h>
#include <vector>

#include "hasher.h"

static const int BITS = 24;
static const uint32_t N = UINT32_C(1) << BITS;

// Chi-square test of independence on a 2x2 contingency table.
// Returns the chi-square statistic (1 DOF).
// a = both changed, b = only bit_i changed,
// c = only bit_k changed, d = neither changed.
static double chi_sq_2x2(double a, double b, double c, double d) {
  double n = a + b + c + d;
  if (n == 0) return 0;
  // Expected values under independence.
  double row0 = a + b, row1 = c + d;
  double col0 = a + c, col1 = b + d;
  // If any marginal is 0, independence is trivially satisfied.
  if (row0 == 0 || row1 == 0 || col0 == 0 || col1 == 0) return 0;
  double e_a = row0 * col0 / n;
  double e_b = row0 * col1 / n;
  double e_c = row1 * col0 / n;
  double e_d = row1 * col1 / n;
  // Require sufficient expected counts.
  if (e_a < 5 || e_b < 5 || e_c < 5 || e_d < 5) return 0;
  return ((a - e_a) * (a - e_a) / e_a +
          (b - e_b) * (b - e_b) / e_b +
          (c - e_c) * (c - e_c) / e_c +
          (d - e_d) * (d - e_d) / e_d);
}

// Cramer's V from chi-square (for 2x2 table, phi coefficient).
static double cramers_v(double chi_sq, double n) {
  if (n == 0) return 0;
  return sqrt(chi_sq / n);
}

struct BICResult {
  double worst_chi_sq;
  double worst_v;
  int worst_in_bit;
  int worst_out_bit_i;
  int worst_out_bit_k;
  double mean_v;
};

// Exhaustive BIC test over the full 24-bit input space.
template<typename HashFn>
static BICResult exact_bic(HashFn fn) {
  // popcount[j][i]: how many times output bit i changed when input bit j flipped.
  // andcount[j][i][k]: how many times output bits i AND k both changed.
  // Since we enumerate all 2^24 inputs, these are exact.

  unsigned num_threads = std::thread::hardware_concurrency();
  if (num_threads == 0) num_threads = 4;

  struct Accum {
    long long popcount[BITS][BITS];
    long long andcount[BITS][BITS][BITS];
  };

  std::vector<Accum> accums(num_threads);
  for (auto& a : accums) memset(&a, 0, sizeof(a));

  std::vector<std::thread> threads;
  uint32_t chunk = N / num_threads;

  for (unsigned t = 0; t < num_threads; t++) {
    uint32_t x_start = t * chunk;
    uint32_t x_end = (t == num_threads - 1) ? N : (t + 1) * chunk;
    threads.emplace_back([&fn, &accums, t, x_start, x_end]() {
      auto& acc = accums[t];
      for (uint32_t x = x_start; x < x_end; x++) {
        uint32_t h0 = fn(x);
        for (int j = 0; j < BITS; j++) {
          uint32_t h1 = fn(x ^ (UINT32_C(1) << j));
          uint32_t diff = h0 ^ h1;
          for (int i = 0; i < BITS; i++) {
            int bi = (diff >> i) & 1;
            acc.popcount[j][i] += bi;
            if (bi) {
              for (int k = i + 1; k < BITS; k++) {
                acc.andcount[j][i][k] += (diff >> k) & 1;
              }
            }
          }
        }
      }
    });
  }

  for (auto& t : threads) t.join();

  // Merge per-thread accumulators.
  long long popcount[BITS][BITS] = {{0}};
  long long andcount[BITS][BITS][BITS] = {{{0}}};
  for (const auto& acc : accums) {
    for (int j = 0; j < BITS; j++) {
      for (int i = 0; i < BITS; i++) {
        popcount[j][i] += acc.popcount[j][i];
        for (int k = i + 1; k < BITS; k++)
          andcount[j][i][k] += acc.andcount[j][i][k];
      }
    }
  }

  // Analyze: find worst chi-square across all (input_bit, output_bit_pair).
  BICResult result = {};
  result.worst_chi_sq = 0;
  int num_tuples = 0;
  double sum_v = 0;

  for (int j = 0; j < BITS; j++) {
    for (int i = 0; i < BITS; i++) {
      for (int k = i + 1; k < BITS; k++) {
        double a = andcount[j][i][k];           // both i and k changed
        double b = popcount[j][i] - a;          // only i changed
        double c = popcount[j][k] - a;          // only k changed
        double d = (double)N - a - b - c;       // neither changed

        double chi = chi_sq_2x2(a, b, c, d);
        double v = cramers_v(chi, (double)N);
        sum_v += v;
        num_tuples++;

        if (chi > result.worst_chi_sq) {
          result.worst_chi_sq = chi;
          result.worst_v = v;
          result.worst_in_bit = j;
          result.worst_out_bit_i = i;
          result.worst_out_bit_k = k;
        }
      }
    }
  }

  result.mean_v = sum_v / num_tuples;
  return result;
}

int main(void) {
  ArrayIndexHasher h;

  printf("=== Bit Independence Criterion (BIC) Test ===\n");
  printf("Exhaustive over 2^%d inputs.\n", BITS);
  printf("Tests whether output bit pairs change independently\n");
  printf("when each input bit is flipped (chi-square, 1 DOF).\n\n");

  // --- Reference (default constants) ---
  printf("--- Reference (rapidhash default constants) ---\n");
  printf("  m1=0x%06x  m2=0x%06x  m3=0x%06x\n\n", h.m1(), h.m2(), h.m3());

  auto run_bic = [&](const char* label, auto fn) {
    BICResult r = exact_bic(fn);
    printf("  %-18s  worst V=%.6f  (inbit=%2d, outbits=%2d,%2d)  mean_V=%.6f\n",
           label, r.worst_v, r.worst_in_bit,
           r.worst_out_bit_i, r.worst_out_bit_k, r.mean_v);
  };

  run_bic("identity:",   [&](uint32_t x){ return h.identity(x); });
  run_bic("xor only:",   [&](uint32_t x){ return h.xor_only(x); });
  run_bic("xsr*mul 2r:", [&](uint32_t x){ return h.xsr_mul_xsr_2(x); });
  run_bic("xsr*mul 3r:", [&](uint32_t x){ return h.xsr_mul_xsr_3(x); });

  // --- Statistical analysis over random seeds ---
  const int NUM_SEEDS = 50;
  double v2_vals[NUM_SEEDS], v3_vals[NUM_SEEDS];
  double v2_min = 1e9, v2_max = 0, v2_sum = 0;
  double v3_min = 1e9, v3_max = 0, v3_sum = 0;

  int64_t initial_seed = (int64_t)time(NULL) ^ ((int64_t)clock() << 32);
  RNG rng(initial_seed);

  printf("\n=== Statistical analysis over %d random seeds ===\n", NUM_SEEDS);
  printf("  initial seed: 0x%016llx\n\n", (unsigned long long)initial_seed);
  printf("  %4s  %-18s %-18s %-18s  %10s  %10s\n",
         "#", "m1", "m2", "m3", "V_2r", "V_3r");
  printf("  %4s  %-18s %-18s %-18s  %10s  %10s\n",
         "----", "------------------", "------------------",
         "------------------", "----------", "----------");

  for (int i = 0; i < NUM_SEEDS; i++) {
    h.reseed(rng);

    BICResult r2 = exact_bic([&](uint32_t x){ return h.xsr_mul_xsr_2(x); });
    BICResult r3 = exact_bic([&](uint32_t x){ return h.xsr_mul_xsr_3(x); });

    double w2 = r2.worst_v, w3 = r3.worst_v;
    v2_vals[i] = w2; v3_vals[i] = w3;
    v2_sum += w2;    v3_sum += w3;
    if (w2 < v2_min) v2_min = w2;
    if (w2 > v2_max) v2_max = w2;
    if (w3 < v3_min) v3_min = w3;
    if (w3 > v3_max) v3_max = w3;

    printf("  %4d  0x%06x (%010llx)  0x%06x (%010llx)  0x%06x (%010llx)  %10.6f  %10.6f\n",
           i + 1,
           h.m1(), (unsigned long long)(h.secrets()[0] & 0xFFFFFFFFFFull),
           h.m2(), (unsigned long long)(h.secrets()[1] & 0xFFFFFFFFFFull),
           h.m3(), (unsigned long long)(h.secrets()[2] & 0xFFFFFFFFFFull),
           w2, w3);
  }

  double v2_mean = v2_sum / NUM_SEEDS;
  double v3_mean = v3_sum / NUM_SEEDS;

  double v2_var = 0, v3_var = 0;
  for (int i = 0; i < NUM_SEEDS; i++) {
    double d2 = v2_vals[i] - v2_mean;
    double d3 = v3_vals[i] - v3_mean;
    v2_var += d2 * d2;
    v3_var += d3 * d3;
  }

  printf("\n  Summary (worst Cramer's V per seed):\n");
  printf("  %-12s  %10s  %10s  %10s  %10s\n",
         "", "min", "mean", "max", "stddev");
  printf("  %-12s  %10.6f  %10.6f  %10.6f  %10.6f\n",
         "2-round:", v2_min, v2_mean, v2_max, sqrt(v2_var / NUM_SEEDS));
  printf("  %-12s  %10.6f  %10.6f  %10.6f  %10.6f\n",
         "3-round:", v3_min, v3_mean, v3_max, sqrt(v3_var / NUM_SEEDS));

  printf("\n  Cramer's V: 0 = perfectly independent, 1 = fully dependent.\n");

  return 0;
}
