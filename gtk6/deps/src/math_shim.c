/* CodeOS math shim — functions glib's bundled gnulib needs at link time
 * but the CodeOS minimal libc / qt6stubs does not yet provide.
 *
 * frexp/ldexp are implemented with IEEE-754 bit manipulation so they do not
 * recurse back into themselves (GCC's __builtin_* would emit a libm call).
 * The long-double variants delegate to the double ones: adequate until a real
 * libm lands.
 */
#include <stdint.h>

double frexp(double x, int *e)
{
    union { double d; uint64_t u; } v;
    int ee = 0;
    int expo;
    uint64_t sign, mant;

    v.d = x;
    if (x == 0.0 || x != x || x == 1.0 / 0.0 || x == -1.0 / 0.0)
    {
        *e = 0;
        return x;
    }

    sign = v.u & 0x8000000000000000ULL;
    expo = (int)((v.u >> 52) & 0x7ff);
    mant = v.u & 0x000fffffffffffffULL;

    if (expo == 0)
    {
        while (!(mant & 0x0010000000000000ULL))
        {
            mant <<= 1;
            ee--;
        }
        expo = 1;
    }

    ee += expo - 1022;
    mant &= 0x000fffffffffffffULL;
    v.u = sign | (1022ULL << 52) | mant;
    *e = ee;
    return v.d;
}

double ldexp(double x, int n)
{
    union { uint64_t u; double d; } b;

    while (n > 1023)  { x *= 0x1p1023;  n -= 1023; }
    while (n < -1022) { x *= 0x1p-1022; n += 1022; }

    b.u = (uint64_t)(n + 1023) << 52;
    return x * b.d;
}

long double frexpl(long double x, int *e)
{
    return (long double)frexp((double)x, e);
}

long double ldexpl(long double x, int n)
{
    return (long double)ldexp((double)x, n);
}
