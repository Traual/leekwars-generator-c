/*
 * lw_rng.h -- 1:1 port of state/State._DefaultRandom (Python upstream's
 *             Random class -- itself a port of Java's deterministic LCG).
 *
 * Reference: java_reference/TestRng.java
 *            leekwars_generator_python/leekwars/state/state.py
 *
 * Algorithm:
 *   n  = (n * 1103515245 + 12345) in signed 64-bit (overflow wraps)
 *   r  = (n / 65536) % 32768 + 32768   // Java truncating division
 *   getDouble() = r / 65536.0
 *
 * In C99+, int64_t arithmetic wraps modulo 2^64 with two's complement,
 * identical to Java long overflow. Truncating division and signed % match
 * Java semantics.
 */
#ifndef LW_RNG_H
#define LW_RNG_H

#include <stdint.h>

#define LW_RNG_MULT  1103515245LL
#define LW_RNG_ADD   12345LL
#define LW_RNG_HALF  65536LL
#define LW_RNG_MOD   32768LL


/* public void seed(long seed) { this.n = seed; } */
static inline void lw_rng_seed(uint64_t *state, int64_t seed) {
    *state = (uint64_t)seed;
}


/* public double getDouble() {
 *     n = n * 1103515245 + 12345;
 *     long r = (n / 65536) % 32768 + 32768;
 *     return (double) r / 65536;
 * }
 */
static inline double lw_rng_get_double(uint64_t *state) {
    int64_t n = (int64_t)(*state);
    n = n * LW_RNG_MULT + LW_RNG_ADD;
    *state = (uint64_t)n;
    int64_t r = ((n / LW_RNG_HALF) % LW_RNG_MOD) + LW_RNG_MOD;
    return ((double)r) / 65536.0;
}


/* public int getInt(int min, int max) {
 *     if (max - min + 1 <= 0) return 0;
 *     return min + (int)(getDouble() * (max - min + 1));
 * }
 *
 * Java's Random.nextInt(min, max) inclusive on both ends.
 */
static inline int lw_rng_get_int(uint64_t *state, int min_v, int max_v) {
    int range = max_v - min_v + 1;
    if (range <= 0) return 0;
    double d = lw_rng_get_double(state);
    return min_v + (int)(d * (double)range);
}


/* getLong is identical to getInt in Java for the values we use; alias. */
static inline int64_t lw_rng_get_long(uint64_t *state, int64_t min_v, int64_t max_v) {
    int64_t range = max_v - min_v + 1;
    if (range <= 0) return 0;
    double d = lw_rng_get_double(state);
    return min_v + (int64_t)(d * (double)range);
}


/* Backward-compat alias used by older code -- DO NOT use in new ports. */
static inline double lw_rng_double(uint64_t *state) {
    return lw_rng_get_double(state);
}


#endif /* LW_RNG_H */
