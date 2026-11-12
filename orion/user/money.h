#ifndef __UI_MONEY_H__
#define __UI_MONEY_H__

#include <stdbool.h>
#include <stdint.h>

// Parse an ASCII decimal into scaled integer units, without rounding.
// Excess fractional digits are accepted only if they are zero. Scale is 0..9.
bool money_parse_decimal(const char *text, unsigned scale, int64_t *out);

// Compute a*b/divisor in integer units, rounding halves away from zero.
// divisor must be positive. Overflow or invalid input leaves out unchanged.
bool money_mul_div(int64_t a, int64_t b, int64_t divisor, int64_t *out);
bool money_add(int64_t a, int64_t b, int64_t *out);

#endif
