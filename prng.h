#pragma once
#include "base.h"

// The core PRNG (architecture spine AD-8): PCG32, the XSH RR output function
// over a 64-bit LCG, as in the PCG reference implementation (pcg-basic,
// pcg32_random_r, pcg32_srandom_r and pcg32_boundedrand_r). Lowercase r and
// any later stateful randomness draw from it. R keeps its upstream hash and
// the arpeggiator's random pattern keeps its seed+step hash; neither uses
// this file.
//
// The generator works in U64 and U32 only, never Usz, and uses no float, so a
// 32-bit Usz (armhf) gives the same sequence as a 64-bit one. It holds no
// writable data: the caller owns each Prng, which for r lives in the op-state
// store (opstate.h).

typedef struct {
  U64 state;
  U64 inc; // stream selector; always odd
} Prng;

// Next 32-bit output.
static inline U32 prng_next_u32(Prng *rng) {
  U64 old = rng->state;
  rng->state = old * UINT64_C(6364136223846793005) + rng->inc;
  U32 xorshifted = (U32)(((old >> 18) ^ old) >> 27);
  U32 rot = (U32)(old >> 59);
  return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

// Seeds with an initial state and a stream, exactly as pcg32_srandom_r does,
// so the reference outputs hold (see the unit test).
static inline void prng_seed_raw(Prng *rng, U64 initstate, U64 initseq) {
  rng->state = 0;
  rng->inc = (initseq << 1) | 1u;
  prng_next_u32(rng);
  rng->state += initstate;
  prng_next_u32(rng);
}

// SplitMix64's finalizer: spreads each input bit over the whole word.
static inline U64 prng_mix64(U64 z) {
  z += UINT64_C(0x9e3779b97f4a7c15);
  z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
  return z ^ (z >> 31);
}

// Seeds the generator of the cell (y, x) under random_seed. Each cell gets
// its own stream, and its start state depends on the seed and the cell, so
// two cells never share a sequence and a different seed changes every cell.
// The arguments are widened to U64 before any arithmetic.
static inline void prng_seed(Prng *rng, Usz seed, Usz y, Usz x) {
  U64 cell = ((U64)y << 32) ^ (U64)x;
  prng_seed_raw(rng, prng_mix64((U64)seed ^ prng_mix64(cell)), cell);
}

// A uniform value in [0, bound), without modulo bias: outputs below
// 2^32 mod bound are rejected and redrawn. A bound of 0 returns 0 and draws
// nothing.
static inline U32 prng_bounded(Prng *rng, U32 bound) {
  if (bound == 0)
    return 0;
  U32 threshold = (U32)(0u - bound) % bound;
  for (;;) {
    U32 r = prng_next_u32(rng);
    if (r >= threshold)
      return r % bound;
  }
}
