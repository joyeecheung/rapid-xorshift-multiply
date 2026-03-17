#pragma once
#include <stdint.h>
#include <string.h>

#include "make_secret_standalone.h"

static const int     KSHIFT = 12;
static const uint32_t KMASK = 0xFFFFFF;

// Adapted from
// https://chromium.googlesource.com/v8/v8/+/0596ead5b04f5988d7742c2a4559637a4f81b849/src/base/utils/random-number-generator.h
class RNG {
 public:
  explicit RNG(int64_t seed) {
    uint64_t s;
    memcpy(&s, &seed, sizeof(s));
    state0_ = murmurhash3(s);
    state1_ = murmurhash3(~state0_);
  }

  uint64_t next_int64() {
    uint64_t s1 = state0_;
    uint64_t s0 = state1_;
    state0_ = s0;
    s1 ^= s1 << 23;
    s1 ^= s1 >> 17;
    s1 ^= s0;
    s1 ^= s0 >> 26;
    state1_ = s1;
    return state0_ + state1_;
  }

 private:
  uint64_t state0_, state1_;

  static uint64_t murmurhash3(uint64_t h) {
    h ^= h >> 33;
    h *= UINT64_C(0xFF51AFD7ED558CCD);
    h ^= h >> 33;
    h *= UINT64_C(0xC4CEB9FE1A85EC53);
    h ^= h >> 33;
    return h;
  }
};

// These served for analysis of the following implementations
// https://chromium.googlesource.com/v8/v8/+/aac14dd95e5be0d487eba6bcdaf9cef4f8bd806c/src/strings/string-hasher-inl.h
// https://chromium.googlesource.com/v8/v8/+/aac14dd95e5be0d487eba6bcdaf9cef4f8bd806c/src/numbers/hash-seed.cc
class ArrayIndexHasher {
 public:
  ArrayIndexHasher() { reseed_default(); }

  void reseed_default() {
    secrets_[0] = RAPIDHASH_DEFAULT_SECRET[0];
    secrets_[1] = RAPIDHASH_DEFAULT_SECRET[1];
    secrets_[2] = RAPIDHASH_DEFAULT_SECRET[2];
    apply_secrets();
  }

  void reseed(RNG& rng) {
    uint64_t seed = rng.next_int64();
    make_secret(seed, secrets_);
    apply_secrets();
  }

  uint32_t m1() const { return m1_; }
  uint32_t m2() const { return m2_; }
  uint32_t m3() const { return m3_; }
  const uint64_t* secrets() const { return secrets_; }

  uint32_t identity(uint32_t x)    const { return x; }
  uint32_t xor_only(uint32_t x)    const { return (x ^ m1_) & KMASK; }
  uint32_t mul_add(uint32_t x)     const { return (x * m1_ + m2_) & KMASK; }
  uint32_t xor_mul_xor(uint32_t x) const { return (((x ^ m1_) * m2_) ^ m3_) & KMASK; }
  uint32_t mul_add_xor(uint32_t x) const { return ((x * m1_ + m2_) ^ m3_) & KMASK; }

  uint32_t xsr_mul_xsr_2(uint32_t x) const {
    return xsr_mul_xsr_2(x, m1_, m2_, m3_);
  }

  static uint32_t xsr_mul_xsr_2(uint32_t x,
                                  uint32_t m1, uint32_t m2, uint32_t) {
    x ^= (x >> KSHIFT);
    x  = (x * m1) & KMASK;
    x ^= (x >> KSHIFT);
    x  = (x * m2) & KMASK;
    x ^= (x >> KSHIFT);
    return x & KMASK;
  }

  uint32_t xsr_mul_xsr_3(uint32_t x) const {
    return xsr_mul_xsr_3(x, m1_, m2_, m3_);
  }

  static uint32_t xsr_mul_xsr_3(uint32_t x,
                                  uint32_t m1, uint32_t m2, uint32_t m3) {
    x ^= (x >> KSHIFT);
    x  = (x * m1) & KMASK;
    x ^= (x >> KSHIFT);
    x  = (x * m2) & KMASK;
    x ^= (x >> KSHIFT);
    x  = (x * m3) & KMASK;
    x ^= (x >> KSHIFT);
    return x & KMASK;
  }

 private:
  uint32_t m1_, m2_, m3_;
  uint64_t secrets_[3];

  void apply_secrets() {
    m1_ = derive_multiplier(secrets_[0]);
    m2_ = derive_multiplier(secrets_[1]);
    m3_ = derive_multiplier(secrets_[2]);
  }
};
