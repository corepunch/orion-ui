#ifndef LEDGER_MARKETPLACE_HOST_H
#define LEDGER_MARKETPLACE_HOST_H
#include <orion/script/script.h>
#include <lua.h>
#include <stdatomic.h>

typedef bool (*ledger_market_request_fn)(const char *method, const char *url, const char *client_id,
    const char *secret, const char *json, const atomic_bool *cancel, char **body, size_t *size, int *status);
void ledger_market_open(window_t *parent, const char *share);
void ledger_market_shutdown(void);
int ledger_market_start(lua_State *L, const char *provider, const char *options, const char *client);
int ledger_market_status(lua_State *L);
int ledger_market_preview(lua_State *L);
int ledger_market_dates(lua_State *L);
bool ledger_market_supported(void);
bool ledger_market_cancel(lua_State *L);
bool ledger_market_paste(lua_State *L);
const char *ledger_market_printable(lua_State *L);
// These explicit host entry points exercise the same view and worker offline.
script_page_t *ledger_market_test_open(const char *share, const char *json_module, ledger_market_request_fn request);
bool ledger_market_test_wait(script_page_t *page);
const char *ledger_market_test_html(script_page_t *page);
void ledger_market_test_close(script_page_t *page);
#endif
