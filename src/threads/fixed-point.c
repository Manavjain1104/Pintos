#include "fixed-point.h"
#include <stdint.h>

int32_t f = (1<<14);

/* Convert an interger n to fixed point */
int32_t
convert_int_to_fp (int32_t n)
{
     return n * f;
}

/* Convert an fixed point x to integer rounding towards zero */
int32_t
convert_to_int_towards_zero (int32_t x)
{
     return x / f;
}

/* Convert an fixed point x to integer rounding to nearest */
int32_t
convert_to_nearest_int (int32_t x)
{
    if (x >= 0)
    {
        return (x + f) / f;
    }
    else 
    {
        return (x - f) / f;
    }
}

/* Add fixed point to fixed point */
int32_t
add_fp_to_fp (int32_t x, int32_t y)
{
     return (x + y);
}

/* Subtract fixed point from fixed point */
int32_t
subtract_fp_from_fp (int32_t x, int32_t y)
{
     return (x - y);
}

/* Add integer to fixed point */
int32_t
add_int_to_fp (int32_t n, int32_t x)
{
     return (x + convert_int_to_fp(n));
}


/* Subtract integer from fixed point */
int32_t
subtract_int_from_fp (int32_t n, int32_t x)
{
     return (x - convert_int_to_fp(n));
}

/* Multiply fixed point to fixed point */
int32_t
multiply_fp_to_fp (int32_t x, int32_t y)
{
     return ((int64_t) x * convert_to_int_towards_zero(y));
}

/* Multiply integer to fixed point */
int32_t
multiply_fp_to_int (int32_t x, int32_t n)
{
    return x * n;
}

/* Divide fixed point by fixed point */
int32_t
divide_fp_by_fp (int32_t x, int32_t y)
{
    return ((int64_t) x * (f / y));
}

/* Divide fixed point by fixed point */
int32_t
divide_fp_by_int (int32_t n, int32_t x)
{
    return x / n;
}
