#include <math.h>
#include <stdio.h>
#include <time.h>

#include "hasher.h"

// Modified from https://github.com/skeeto/hash-prospector/blob/master/prospector.c
// This is free and unencumbered software released into the public domain.

// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.

// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// For more information, please refer to <http://unlicense.org/>
template<typename HashFn>
static double exact_bias(HashFn fn) {
  static const int BITS = 24;
  static const uint32_t N = UINT32_C(1) << BITS;

  long long bins[24][24] = {{0}};

  for (uint32_t x = 0; x < N; x++) {
    uint32_t h0 = fn(x);
    for (int j = 0; j < BITS; j++) {
      uint32_t h1 = fn(x ^ (UINT32_C(1) << j));
      uint32_t set = h0 ^ h1;
      for (int k = 0; k < BITS; k++) bins[j][k] += (set >> k) & 1;
    }
  }

  double half = N / 2.0;
  double mean = 0.0;
  for (int j = 0; j < BITS; j++)
    for (int k = 0; k < BITS; k++) {
      double diff = (bins[j][k] - half) / half;
      mean += (diff * diff) / (BITS * BITS);
    }
  return sqrt(mean) * 1000.0;
}

int main(void) {
  ArrayIndexHasher h;

  printf("=== Reference (rapidhash default constants) ===\n");
  printf("  m1=0x%06x  m2=0x%06x  m3=0x%06x\n", h.m1(), h.m2(), h.m3());
  printf("  identity:       %.4f\n", exact_bias([&](uint32_t x){ return h.identity(x); }));
  printf("  xor only:       %.4f\n", exact_bias([&](uint32_t x){ return h.xor_only(x); }));
  printf("  mul+add:        %.4f\n", exact_bias([&](uint32_t x){ return h.mul_add(x); }));
  printf("  xsr*mul*xsr 2r: %.4f\n", exact_bias([&](uint32_t x){ return h.xsr_mul_xsr_2(x); }));
  printf("  xsr*mul*xsr 3r: %.4f\n", exact_bias([&](uint32_t x){ return h.xsr_mul_xsr_3(x); }));

  const int NUM_SEEDS = 50;
  double bias2_vals[NUM_SEEDS], bias3_vals[NUM_SEEDS];
  double bias2_min = 1e9, bias2_max = 0, bias2_sum = 0;
  double bias3_min = 1e9, bias3_max = 0, bias3_sum = 0;

  int64_t initial_seed = (int64_t)time(NULL) ^ ((int64_t)clock() << 32);
  RNG rng(initial_seed);

  printf("\n=== Statistical analysis over %d random seeds ===\n", NUM_SEEDS);
  printf("  initial seed: 0x%016llx\n", (unsigned long long)initial_seed);
  printf("\n  %4s  %-18s %-18s %-18s  %8s  %8s\n",
         "#", "m1", "m2", "m3", "2-round", "3-round");
  printf("  %4s  %-18s %-18s %-18s  %8s  %8s\n",
         "----", "------------------", "------------------",
         "------------------", "--------", "--------");

  for (int i = 0; i < NUM_SEEDS; i++) {
    h.reseed(rng);

    double b2 = exact_bias([&](uint32_t x){ return h.xsr_mul_xsr_2(x); });
    double b3 = exact_bias([&](uint32_t x){ return h.xsr_mul_xsr_3(x); });

    bias2_vals[i] = b2; bias3_vals[i] = b3;
    bias2_sum += b2;    bias3_sum += b3;
    if (b2 < bias2_min) bias2_min = b2;
    if (b2 > bias2_max) bias2_max = b2;
    if (b3 < bias3_min) bias3_min = b3;
    if (b3 > bias3_max) bias3_max = b3;

    printf("  %4d  0x%06x (%010llx)  0x%06x (%010llx)  0x%06x (%010llx)  %8.4f  %8.4f\n",
           i + 1,
           h.m1(), (unsigned long long)(h.secrets()[0] & 0xFFFFFFFFFFull),
           h.m2(), (unsigned long long)(h.secrets()[1] & 0xFFFFFFFFFFull),
           h.m3(), (unsigned long long)(h.secrets()[2] & 0xFFFFFFFFFFull),
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

  printf("\n  For reference:  identity = 1000.0,  ideal = 0.0\n");
  printf("  2-round achieves %.2f%% of ideal avalanche on average\n",
         100.0 * (1.0 - bias2_mean / 1000.0));
  printf("  3-round achieves %.2f%% of ideal avalanche on average\n",
         100.0 * (1.0 - bias3_mean / 1000.0));

  return 0;
}