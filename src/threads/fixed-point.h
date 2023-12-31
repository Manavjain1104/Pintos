#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>
#define ft 1<<(14)

#define mul_fp_fp(x, y) ((int64_t)(x)) * (y) / (ft)
#define div_fp_fp(x, y) ((int64_t)(x)) * (ft) / (y)

/* Convert an interger n to fixed point */
int32_t
convert_int_to_fp (int32_t n);

/* Convert an fixed point x to integer rounding towards zero */
int32_t
convert_to_int_towards_zero (int32_t x);

/* Convert an fixed point x to integer rounding to nearest */
int32_t
convert_to_nearest_int (int32_t x);

/* Add fixed point to fixed point */
int32_t
add_fp_to_fp (int32_t x, int32_t y);

/* Subtract fixed point from fixed point */
int32_t
subtract_fp_from_fp (int32_t x, int32_t y);


/* Add integer to fixed point */
int32_t
add_int_to_fp (int32_t n, int32_t x);


/* Subtract integer from fixed point */
int32_t
subtract_int_from_fp (int32_t n, int32_t x);

/* Multiply integer to fixed point */
int32_t
multiply_fp_to_int (int32_t x, int32_t n);

/* Divide fixed point by fixed point */
int32_t
divide_fp_by_int (int32_t n, int32_t x);

#endif // threads/fixed-point.h