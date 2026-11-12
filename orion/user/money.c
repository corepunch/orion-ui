#include "money.h"
#include <limits.h>
#include <stdio.h>

static bool money_reject(const char *operation) {
  fprintf(stderr, "[money] %s rejected: invalid value or overflow\n", operation);
  fflush(stderr);
  return false;
}

bool money_parse_decimal(const char *text, unsigned scale, int64_t *out) {
  if (!text || !out || scale > 9) return money_reject("parse");
  const char *p = text;
  bool negative = *p == '-';
  if (*p == '-' || *p == '+') p++;
  if (*p < '0' || *p > '9') return money_reject("parse");
  uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX, value = 0;
  while (*p >= '0' && *p <= '9') {
    unsigned digit = (unsigned)(*p++ - '0');
    if (value > (limit - digit) / 10) return money_reject("parse");
    value = value * 10 + digit;
  }
  unsigned fractional = 0;
  if (*p == '.') {
    p++;
    if (*p < '0' || *p > '9') return money_reject("parse");
    while (*p >= '0' && *p <= '9') {
      unsigned digit = (unsigned)(*p++ - '0');
      if (fractional < scale) {
        if (value > (limit - digit) / 10) return money_reject("parse");
        value = value * 10 + digit;
        fractional++;
      } else if (digit) return money_reject("parse");
    }
  }
  if (*p) return money_reject("parse");
  while (fractional++ < scale) {
    if (value > limit / 10) return money_reject("parse");
    value *= 10;
  }
  *out = negative ? value == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)value : (int64_t)value;
  return true;
}

bool money_mul_div(int64_t a, int64_t b, int64_t divisor, int64_t *out) {
  if (!out || divisor <= 0) return money_reject("multiply/divide");
#if defined(__SIZEOF_INT128__)
  __int128 product = (__int128)a * b;
  __int128 quotient = product / divisor, remainder = product % divisor;
  if (remainder < 0) remainder = -remainder;
  if (remainder * 2 >= divisor) quotient += product < 0 ? -1 : 1;
  if (quotient < INT64_MIN || quotient > INT64_MAX) return money_reject("multiply/divide");
  *out = (int64_t)quotient;
  return true;
#else
  bool negative = (a < 0) != (b < 0);
  uint64_t ua = a < 0 ? (uint64_t)(-(a + 1)) + 1 : (uint64_t)a;
  uint64_t ub = b < 0 ? (uint64_t)(-(b + 1)) + 1 : (uint64_t)b;
  if (ub && ua > UINT64_MAX / ub) return money_reject("multiply/divide");
  uint64_t product = ua * ub, d = (uint64_t)divisor, quotient = product / d, remainder = product % d;
  if (remainder >= d - remainder) quotient++;
  uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
  if (quotient > limit) return money_reject("multiply/divide");
  *out = negative ? quotient == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)quotient : (int64_t)quotient;
  return true;
#endif
}

bool money_add(int64_t a, int64_t b, int64_t *out) {
  if (!out || (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return money_reject("add");
  *out = a + b; return true;
}
