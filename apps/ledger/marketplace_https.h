#ifndef LEDGER_MARKETPLACE_HTTPS_H
#define LEDGER_MARKETPLACE_HTTPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

/* Safe to call before launch, without doing network I/O. */
bool ledger_market_https_available(void);

/* Synchronous worker-thread-only HTTPS request. A non-null client_id
 * selects Ozon; otherwise the secret is an eBay bearer token.
 * True means an HTTP response was received. Only a 2xx response has a
 * malloc-owned, NUL-terminated body. False leaves all outputs zeroed. */
bool ledger_market_https(const char *method, const char *url, const char *client_id,
                         const char *secret, const char *json, const atomic_bool *cancel,
                         char **body, size_t *len, int *status);

#endif
