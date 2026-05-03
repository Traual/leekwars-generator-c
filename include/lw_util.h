/*
 * lw_util.h -- 1:1 port of util/Util.java + java semantic helpers.
 *
 * Java's Math.round(double) returns "half-up" (toward +inf for positive
 * values).  C's lround() is "half away from zero".  These agree for
 * positive values (where damage / heal land), but the safe path is to
 * always go through java_round.
 *
 * Reference: leekwars/util/java_math.py (Python upstream's mirror)
 */
#ifndef LW_UTIL_H
#define LW_UTIL_H

#include <math.h>
#include <stdint.h>


/* Java: Math.round(double d) -> (long) floor(d + 0.5) */
static inline int lw_java_round(double d) {
    return (int)floor(d + 0.5);
}


/* Java: int / int truncates toward zero (Python // floors toward -inf).
 *       Replicated for parity with java_math.java_div in Python upstream. */
static inline int64_t lw_java_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    /* C99 truncates toward zero, same as Java -- so q is already correct.
     * But: when a/b is the exact same as Java's, no fixup needed.  We keep
     * the wrapper for documentation purity. */
    return q;
}


/* Java: a % b takes sign of dividend.  C99 same. */
static inline int64_t lw_java_mod(int64_t a, int64_t b) {
    return a % b;
}


/* Java: long arithmetic wraps to signed 64-bit two's complement.
 *       int64_t in C99 already does this. */
static inline int64_t lw_java_long_overflow(int64_t n) {
    return n;
}


/* Java: Math.signum(double) -> -1, 0 or 1 (as double). */
static inline double lw_java_signum(double d) {
    if (d > 0.0) return 1.0;
    if (d < 0.0) return -1.0;
    return 0.0;
}


/* Java: Math.signum(int) variant.  Often used as `int s = sign(v) ? 1 : -1`. */
static inline int lw_java_signum_int(int v) {
    if (v > 0) return 1;
    if (v < 0) return -1;
    return 0;
}


/* Java: Math.abs (we already have abs/labs in libc; here for symmetry). */
static inline int lw_abs_int(int v) { return v < 0 ? -v : v; }


/* Java: Math.max/min for int. */
static inline int lw_max_int(int a, int b) { return a > b ? a : b; }
static inline int lw_min_int(int a, int b) { return a < b ? a : b; }
static inline double lw_max_d(double a, double b) { return a > b ? a : b; }
static inline double lw_min_d(double a, double b) { return a < b ? a : b; }


#endif /* LW_UTIL_H */
