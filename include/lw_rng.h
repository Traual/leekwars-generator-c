/*
 * lw_rng.h -- LCG matching the Leek Wars engine's RandomGenerator.
 *
 * IMPORTANT: this is NOT java.util.Random. The Leek Wars engine uses
 * its own LCG with the classic glibc rand() constants, kept identical
 * across the Java/Python/C ports:
 *   n  = (n * 1103515245 + 12345) in signed 64-bit (overflow wraps)
 *   r  = (n / 65536) % 32768 + 32768           // truncating div
 *   getDouble() = r / 65536.0
 *
 * Matched in the Python port at:
 *   leekwars/state/state.py::_DefaultRandom
 *   leekwars/util/java_math.py::{java_long, java_div, java_mod}
 *
 * In C99+, ``int64_t`` arithmetic wraps modulo 2^64 with two's
 * complement, identical to Java ``long`` overflow. Truncating
 * division and ``%`` with-sign-of-dividend match Java semantics.
 */
#ifndef LW_RNG_H
#define LW_RNG_H

#include <stdint.h>

#define LW_RNG_MULT  1103515245LL
#define LW_RNG_ADD   12345LL
#define LW_RNG_HALF  65536LL
#define LW_RNG_MOD   32768LL

static inline void lw_rng_advance(uint64_t *state) {
    int64_t n = (int64_t)(*state);
    n = n * LW_RNG_MULT + LW_RNG_ADD;
    *state = (uint64_t)n;
}

static inline double lw_rng_double(uint64_t *state) {
    lw_rng_advance(state);
    int64_t n = (int64_t)(*state);
    int64_t r = ((n / LW_RNG_HALF) % LW_RNG_MOD) + LW_RNG_MOD;
    return ((double)r) / 65536.0;
}

/*
 * Seed: ``self.n = java_long(seed)`` -- direct signed cast.
 */
static inline void lw_rng_seed(uint64_t *state, int64_t seed) {
    *state = (uint64_t)seed;
}

#endif /* LW_RNG_H */
