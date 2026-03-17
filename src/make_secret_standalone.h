// Ported from
// https://chromium.googlesource.com/v8/v8/+/0a8b1cdcc8b243c62cf045fa8beb50600e11758a/third_party/rapidhash-v8/rapidhash.h
// https://chromium.googlesource.com/v8/v8/+/0a8b1cdcc8b243c62cf045fa8beb50600e11758a/third_party/rapidhash-v8/secret.h
#pragma once

#include <stdint.h>
#include <utility>
#include <tuple>

/*
 * rapidhash - Very fast, high quality, platform-independent hashing algorithm.
 * Copyright (C) 2024 Nicolas De Carli
 *
 * Based on 'wyhash', by Wang Yi <godspeed_china@yeah.net>
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at:
 *   - rapidhash source repository: https://github.com/Nicoshev/rapidhash
 */

/*
 *  64*64 -> 128bit multiply function.
 *
 *  @param A  Address of 64-bit number.
 *  @param B  Address of 64-bit number.
 *
 *  Calculates 128-bit C = *A * *B.
 */
inline std::pair<uint64_t, uint64_t> rapid_mul128(uint64_t A, uint64_t B) {
#if defined(__SIZEOF_INT128__)
  __uint128_t r = A;
  r *= B;
  return {static_cast<uint64_t>(r), static_cast<uint64_t>(r >> 64)};
#else
  // High and low 32 bits of A and B.
  uint64_t a_high = A >> 32, b_high = B >> 32, a_low = (uint32_t)A,
           b_low = (uint32_t)B;

  // Intermediate products.
  uint64_t result_high = a_high * b_high;
  uint64_t result_m0 = a_high * b_low;
  uint64_t result_m1 = b_high * a_low;
  uint64_t result_low = a_low * b_low;

  // Final sum. The lower 64-bit addition can overflow twice,
  // so accumulate carry as we go.
  uint64_t high = result_high + (result_m0 >> 32) + (result_m1 >> 32);
  uint64_t t = result_low + (result_m0 << 32);
  high += (t < result_low);  // Carry.
  uint64_t low = t + (result_m1 << 32);
  high += (low < t);  // Carry.

  return {low, high};
#endif
}

/*
 *  Multiply and xor mix function.
 *
 *  @param A  64-bit number.
 *  @param B  64-bit number.
 *
 *  Calculates 128-bit C = A * B.
 *  Returns 64-bit xor between high and low 64 bits of C.
 */
inline uint64_t rapid_mix(uint64_t A, uint64_t B) {
  std::tie(A, B) = rapid_mul128(A, B);
  return A ^ B;
}


/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <https://unlicense.org/>
 *
 * author: 王一 Wang Yi <godspeed_china@yeah.net>
 * contributors: Reini Urban, Dietrich Epp, Joshua Haberman, Tommy Ettinger,
 * Daniel Lemire, Otmar Ertl, cocowalla, leo-yuriev, Diego Barrios Romero,
 * paulie-g, dumblob, Yann Collet, ivte-ms, hyb, James Z.M. Gao, easyaspi314
 * (Devin), TheOneric
 */

// Default secret parameters. If we wanted to, we could generate our own
// versions of these at renderer startup in order to perturb the hash
// and make it more DoS-resistant (similar to what base/hash.h does),
// but generating new ones takes a little bit of time (~200 µs on a desktop
// machine as of 2024), and good-quality random numbers may not be copious
// from within the sandbox. The secret concept is inherited from wyhash,
// described by the wyhash author here:
//
//   https://github.com/wangyi-fudan/wyhash/issues/139
//
// The rules are:
//
//   1. Each byte must be “balanced”, i.e., have exactly 4 bits set.
//      (This is trivially done by just precompting a LUT with the
//      possible bytes and picking from those.)
//
//   2. Each 64-bit group should have a Hamming distance of 32 to
//      all the others (i.e., popcount(secret[i] ^ secret[j]) == 32).
//      This is just done by rejection sampling.
//
//   3. Each 64-bit group should be prime. It's not obvious that this
//      is really needed for the hash, as opposed to wyrand which also
//      uses the same secret, but according to the author, it is
//      “a feeling to be perfect”. This naturally means the last byte
//      cannot be divisible by 2, but apart from that, is easiest
//      checked by testing a few small factors and then the Miller-Rabin
//      test, which rejects nearly all bad candidates in the first iteration
//      and is fast as long as we have 64x64 -> 128 bit muls and modulos.

static constexpr uint64_t RAPIDHASH_DEFAULT_SECRET[3] = {
    0x2d358dccaa6c78a5ull, 0x8bb84b93962eacc9ull, 0x4b33a62ed433d4a3ull};

static inline uint64_t wyrand(uint64_t* seed) {
  *seed += 0x2d358dccaa6c78a5ull;
  return rapid_mix(*seed, *seed ^ 0x8bb84b93962eacc9ull);
}

static inline unsigned long long mul_mod(unsigned long long a,
                                         unsigned long long b,
                                         unsigned long long m) {
  unsigned long long r = 0;
  while (b) {
    if (b & 1) {
      unsigned long long r2 = r + a;
      if (r2 < r) r2 -= m;
      r = r2 % m;
    }
    b >>= 1;
    if (b) {
      unsigned long long a2 = a + a;
      if (a2 < a) a2 -= m;
      a = a2 % m;
    }
  }
  return r;
}

static inline unsigned long long pow_mod(unsigned long long a,
                                         unsigned long long b,
                                         unsigned long long m) {
  unsigned long long r = 1;
  while (b) {
    if (b & 1) r = mul_mod(r, a, m);
    b >>= 1;
    if (b) a = mul_mod(a, a, m);
  }
  return r;
}

static unsigned sprp(unsigned long long n, unsigned long long a) {
  unsigned long long d = n - 1;
  unsigned char s = 0;
  while (!(d & 0xff)) { d >>= 8; s += 8; }
  if (!(d & 0xf)) { d >>= 4; s += 4; }
  if (!(d & 0x3)) { d >>= 2; s += 2; }
  if (!(d & 0x1)) { d >>= 1; s += 1; }
  unsigned long long b = pow_mod(a, d, n);
  if ((b == 1) || (b == (n - 1))) return 1;
  for (unsigned char r = 1; r < s; r++) {
    b = mul_mod(b, b, n);
    if (b <= 1) return 0;
    if (b == (n - 1)) return 1;
  }
  return 0;
}

static unsigned is_prime(unsigned long long n) {
  if (n < 2 || !(n & 1)) return 0;
  if (n < 4) return 1;
  if (!sprp(n, 2)) return 0;
  if (n < 2047) return 1;
  if (!sprp(n, 3)) return 0;
  if (!sprp(n, 5)) return 0;
  if (!sprp(n, 7)) return 0;
  if (!sprp(n, 11)) return 0;
  if (!sprp(n, 13)) return 0;
  if (!sprp(n, 17)) return 0;
  if (!sprp(n, 19)) return 0;
  if (!sprp(n, 23)) return 0;
  if (!sprp(n, 29)) return 0;
  if (!sprp(n, 31)) return 0;
  if (!sprp(n, 37)) return 0;
  return 1;
}

static inline int popcount64(uint64_t x) {
  x = x - ((x >> 1) & 0x5555555555555555ull);
  x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
  return (int)(((x + (x >> 4)) & 0x0f0f0f0f0f0f0f0full) * 0x0101010101010101ull >> 56);
}

static inline void make_secret(uint64_t seed, uint64_t* secret) {
  uint8_t c[] = {15,  23,  27,  29,  30,  39,  43,  45,  46,  51,  53,  54,
                 57,  58,  60,  71,  75,  77,  78,  83,  85,  86,  89,  90,
                 92,  99,  101, 102, 105, 106, 108, 113, 114, 116, 120, 135,
                 139, 141, 142, 147, 149, 150, 153, 154, 156, 163, 165, 166,
                 169, 170, 172, 177, 178, 180, 184, 195, 197, 198, 201, 202,
                 204, 209, 210, 212, 216, 225, 226, 228, 232, 240};
  for (int i = 0; i < 3; i++) {
    uint8_t ok;
    do {
      ok = 1;
      secret[i] = 0;
      for (int j = 0; j < 64; j += 8)
        secret[i] |= (uint64_t)(c[wyrand(&seed) % sizeof(c)]) << j;
      if (secret[i] % 2 == 0) { ok = 0; continue; }
      for (int j = 0; j < i; j++) {
        if (popcount64(secret[j] ^ secret[i]) != 32) { ok = 0; break; }
      }
      if (ok && !is_prime(secret[i])) ok = 0;
    } while (!ok);
  }
}

// Derive a 24-bit odd multiplier from a secret, matching V8's derive_multiplier.
static inline uint32_t derive_multiplier(uint64_t secret) {
  return ((uint32_t)secret & 0xFFFFFF) | 1;
}