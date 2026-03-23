// Seed Avalanche test for the 24-bit xorshift-multiply hash.
//
// Adapted from SMHasher3's SeedAvalancheTest.
//
// Flips each bit in the 64-bit meta seed that drives make_secret(),
// regenerates the full secret pipeline (make_secret -> derive_multiplier
// -> hash), and measures whether ~50% of output bits change.

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "hasher.h"

static const int BITS = 24;
static const int SEED_BITS = 64;
static const uint32_t N = UINT32_C(1) << BITS;

struct Multipliers {
  uint32_t m1, m2, m3;
};

static Multipliers muls_from_seed(uint64_t meta_seed) {
  uint64_t secrets[3];
  make_secret(meta_seed, secrets);
  return { derive_multiplier(secrets[0]),
           derive_multiplier(secrets[1]),
           derive_multiplier(secrets[2]) };
}

// Compute SAC bias across all 64 seed bits × 24 output bits.
// For each seed bit flip, iterates all 2^24 inputs exhaustively.
static double seed_bias(uint64_t meta_seed, bool three_round) {
  Multipliers orig = muls_from_seed(meta_seed);

  long long bins[SEED_BITS][BITS] = {{0}};

  // Precompute perturbed multipliers for all 64 seed bit flips.
  Multipliers perturbed[SEED_BITS];
  for (int j = 0; j < SEED_BITS; j++) {
    perturbed[j] = muls_from_seed(meta_seed ^ (UINT64_C(1) << j));
  }

  for (uint32_t x = 0; x < N; x++) {
    uint32_t h0 = three_round
        ? ArrayIndexHasher::xsr_mul_xsr_3(x, orig.m1, orig.m2, orig.m3)
        : ArrayIndexHasher::xsr_mul_xsr_2(x, orig.m1, orig.m2, orig.m3);
    for (int j = 0; j < SEED_BITS; j++) {
      const Multipliers& p = perturbed[j];
      uint32_t h1 = three_round
          ? ArrayIndexHasher::xsr_mul_xsr_3(x, p.m1, p.m2, p.m3)
          : ArrayIndexHasher::xsr_mul_xsr_2(x, p.m1, p.m2, p.m3);
      uint32_t set = h0 ^ h1;
      for (int k = 0; k < BITS; k++) bins[j][k] += (set >> k) & 1;
    }
  }

  double half = N / 2.0;
  double mean = 0.0;
  for (int j = 0; j < SEED_BITS; j++)
    for (int k = 0; k < BITS; k++) {
      double diff = (bins[j][k] - half) / half;
      mean += (diff * diff) / (SEED_BITS * BITS);
    }
  return sqrt(mean) * 1000.0;
}

int main(void) {
  printf("=== Seed Avalanche Test ===\n");
  printf("Exhaustive over 2^%d inputs x %d seed bits.\n", BITS, SEED_BITS);
  printf("Flips each bit in the 64-bit meta seed, regenerates secrets\n");
  printf("and measures output change.\n");
  printf("Ideal SAC bias = 0.0 (each output bit flips 50%% of the time).\n\n");

  const int NUM_SEEDS = 50;

  int64_t initial_seed = (int64_t)time(NULL) ^ ((int64_t)clock() << 32);
  RNG rng(initial_seed);

  uint64_t meta_seeds[NUM_SEEDS];
  for (int i = 0; i < NUM_SEEDS; i++) {
    meta_seeds[i] = (uint64_t)rng.next_int64();
  }

  printf("  initial RNG seed: 0x%016llx\n\n", (unsigned long long)initial_seed);

  double bias2_vals[NUM_SEEDS], bias3_vals[NUM_SEEDS];
  double bias2_min = 1e9, bias2_max = 0, bias2_sum = 0;
  double bias3_min = 1e9, bias3_max = 0, bias3_sum = 0;

  printf("  %4s  %-20s  %-18s %-18s %-18s  %8s  %8s\n",
         "#", "meta_seed", "m1", "m2", "m3", "2-round", "3-round");
  printf("  %4s  %-20s  %-18s %-18s %-18s  %8s  %8s\n",
         "----", "--------------------",
         "------------------", "------------------", "------------------",
         "--------", "--------");

  for (int i = 0; i < NUM_SEEDS; i++) {
    uint64_t ms = meta_seeds[i];
    Multipliers m = muls_from_seed(ms);

    double b2 = seed_bias(ms, false);
    double b3 = seed_bias(ms, true);

    bias2_vals[i] = b2; bias3_vals[i] = b3;
    bias2_sum += b2;    bias3_sum += b3;
    if (b2 < bias2_min) bias2_min = b2;
    if (b2 > bias2_max) bias2_max = b2;
    if (b3 < bias3_min) bias3_min = b3;
    if (b3 > bias3_max) bias3_max = b3;

    printf("  %4d  0x%016llx  0x%06x            0x%06x            0x%06x            %8.4f  %8.4f\n",
           i + 1, (unsigned long long)ms,
           m.m1, m.m2, m.m3,
           b2, b3);
  }

  double bias2_mean = bias2_sum / NUM_SEEDS;
  double bias3_mean = bias3_sum / NUM_SEEDS;
  double bias2_var = 0, bias3_var = 0;
  for (int i = 0; i < NUM_SEEDS; i++) {
    double d2 = bias2_vals[i] - bias2_mean;
    double d3 = bias3_vals[i] - bias3_mean;
    bias2_var += d2 * d2;
    bias3_var += d3 * d3;
  }

  printf("\n  Summary:\n");
  printf("  %-12s  %8s  %8s  %8s  %8s\n", "", "min", "mean", "max", "stddev");
  printf("  %-12s  %8.4f  %8.4f  %8.4f  %8.4f\n",
         "2-round:", bias2_min, bias2_mean, bias2_max, sqrt(bias2_var / NUM_SEEDS));
  printf("  %-12s  %8.4f  %8.4f  %8.4f  %8.4f\n",
         "3-round:", bias3_min, bias3_mean, bias3_max, sqrt(bias3_var / NUM_SEEDS));

  printf("\n  Scale: 0 = ideal, 1000 = identity\n");

  return 0;
}
