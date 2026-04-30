/*
 * lw_rng.h -- Linear-congruential RNG matching the Java engine.
 *
 * The Java side uses java.util.Random, which is a 48-bit LCG:
 *   n = (n * 0x5DEECE66DL + 0xBL) & 0xFFFFFFFFFFFFL
 *   getDouble() returns ((n >> 16) % 32768 + 32768) / 65536
 *
 * Matched in the Python port at
 * leekwars/state/state.py:_DefaultRandom.
 *
 * We replicate the exact same arithmetic in C so identical seed +
 * call sequence yields identical bit pattern.
 */
#ifndef LW_RNG_H
#define LW_RNG_H

#include <stdint.h>

#define LW_RNG_MULT  0x5DEECE66DULL
#define LW_RNG_INC   0xBULL
#define LW_RNG_MASK  0xFFFFFFFFFFFFULL

static inline uint64_t lw_rng_advance(uint64_t *n) {
    *n = ((*n) * LW_RNG_MULT + LW_RNG_INC) & LW_RNG_MASK;
    return *n;
}

/*
 * Match the Python ``getDouble()``:
 *   r = ((n >> 16) % 32768) + 32768
 *   return r / 65536.0
 *
 * Note this differs from Java's standard nextDouble (which uses two
 * advances and 53 bits) -- the engine intentionally replicates the
 * legacy LeekWars-Java behaviour.
 */
static inline double lw_rng_double(uint64_t *n) {
    lw_rng_advance(n);
    int r = (int)(((*n) >> 16) % 32768) + 32768;
    return ((double)r) / 65536.0;
}

/*
 * Match the Python seed routine:
 *   _set_seed(seed):
 *       self.n = (seed ^ 0x5DEECE66DL) & ((1 << 48) - 1)
 */
static inline void lw_rng_seed(uint64_t *n, int64_t seed) {
    *n = ((uint64_t)seed ^ LW_RNG_MULT) & LW_RNG_MASK;
}

#endif /* LW_RNG_H */
