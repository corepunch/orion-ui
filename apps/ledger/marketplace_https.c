#include "marketplace_https.h"

#include <stdlib.h>
#include <string.h>

#if (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && !defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && !defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__))
#if defined(__has_include)
#if __has_include(<curl/curl.h>)
#define LEDGER_MARKET_CURL 1
#endif
#endif
#endif

#ifdef LEDGER_MARKET_CURL

#include <curl/curl.h>
#include <dlfcn.h>
#include <pthread.h>

enum { BODY_LIMIT = 1024 * 1024, REQUEST_LIMIT = 16384, SECRET_LIMIT = 8192 };

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static struct {
  bool available;
  CURLcode (*global_init)(long);
  curl_version_info_data *(*version_info)(CURLversion);
  CURL *(*easy_init)(void);
  CURLcode (*easy_setopt)(CURL *, CURLoption, ...);
  CURLcode (*easy_getinfo)(CURL *, CURLINFO, ...);
  CURLcode (*easy_perform)(CURL *);
  void (*easy_cleanup)(CURL *);
  struct curl_slist *(*slist_append)(struct curl_slist *, const char *);
  void (*slist_free_all)(struct curl_slist *);
} api;

static void load_curl(void) {
#ifdef __APPLE__
  void *lib = dlopen("/usr/lib/libcurl.4.dylib", RTLD_NOW | RTLD_LOCAL);
#else
  void *lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
#endif
  if (!lib) return;
#define LOAD(field) do { \
    void *address = dlsym(lib, "curl_" #field); \
    if (!address) goto unavailable; \
    _Static_assert(sizeof(api.field) == sizeof(address), "dlsym pointer size"); \
    memcpy(&api.field, &address, sizeof(address)); \
  } while (0)
  LOAD(global_init);
  LOAD(version_info);
  LOAD(easy_init);
  LOAD(easy_setopt);
  LOAD(easy_getinfo);
  LOAD(easy_perform);
  LOAD(easy_cleanup);
  LOAD(slist_append);
  LOAD(slist_free_all);
#undef LOAD
  const curl_version_info_data *info = api.version_info(CURLVERSION_FIRST);
  if (info && (info->features & CURL_VERSION_SSL) &&
      (info->features & CURL_VERSION_ASYNCHDNS) &&
      api.global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
    api.available = true;
    return;  /* Keep the dynamically initialized curl library for process lifetime. */
  }
unavailable:
  dlclose(lib);
}

bool ledger_market_https_available(void) {
  pthread_once(&curl_once, load_curl);
  const char *key_log = getenv("SSLKEYLOGFILE");
  return api.available && (!key_log || !*key_log);
}

static bool ebay_query_ok(const char *tail) {
  if (!*tail) return true;
  if (*tail++ != '?' || !*tail) return false;
  for (; *tail; ++tail) {
    unsigned char c = (unsigned char)*tail;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || strchr("_.~%=&-", c))) return false;
  }
  return true;
}

static bool allowed(const char *method, const char *url, const char *client_id, const char *json) {
  if (!method || !url || strlen(url) > 1024) return false;
  if (client_id) {
    return strcmp(method, "POST") == 0 && json && *json && strlen(json) <= REQUEST_LIMIT &&
      (strcmp(url, "https://api-seller.ozon.ru/v4/posting/fbs/list") == 0 ||
       strcmp(url, "https://api-seller.ozon.ru/v3/posting/fbo/list") == 0 ||
       strcmp(url, "https://api-seller.ozon.ru/v4/product/info/stocks") == 0);
  }
  if (strcmp(method, "GET") != 0 || json) return false;
  static const char *const hosts[] = { "https://api.ebay.com", "https://api.sandbox.ebay.com" };
  static const char *const paths[] = { "/sell/fulfillment/v1/order", "/sell/inventory/v1/inventory_item" };
  for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i) {
    size_t host_len = strlen(hosts[i]);
    if (strncmp(url, hosts[i], host_len) != 0) continue;
    const char *path = url + host_len;
    for (size_t j = 0; j < sizeof(paths) / sizeof(paths[0]); ++j) {
      size_t path_len = strlen(paths[j]);
      if (strncmp(path, paths[j], path_len) == 0 && ebay_query_ok(path + path_len)) return true;
    }
  }
  return false;
}

static bool credential_ok(const char *text, size_t max, bool digits) {
  if (!text || !*text) return false;
  size_t n = 0;
  for (; *text; ++text) {
    unsigned char c = (unsigned char)*text;
    if (++n > max || (digits ? (c < '0' || c > '9') : (c < '!' || c > '~'))) return false;
  }
  return true;
}

/* Erasure must not be optimized out before releasing these temporary buffers. */
static void wipe(char *text, size_t n) {
  volatile unsigned char *p = (volatile unsigned char *)text;
  while (n--) *p++ = 0;
}

static bool add_header(struct curl_slist **headers, const char *name, const char *value) {
  char line[SECRET_LIMIT + 40];
  size_t a = strlen(name), b = strlen(value);
  if (a + b >= sizeof(line)) return false;
  memcpy(line, name, a);
  memcpy(line + a, value, b + 1);
  struct curl_slist *extended = api.slist_append(*headers, line);
  wipe(line, a + b + 1);
  if (!extended) return false;
  *headers = extended;
  return true;
}

typedef struct {
  char *body;
  size_t length, capacity;
  const atomic_bool *cancel;
} transfer_t;

static bool cancelled(const atomic_bool *cancel) {
  return cancel && atomic_load_explicit(cancel, memory_order_relaxed);
}

static size_t receive_body(char *bytes, size_t size, size_t count, void *opaque) {
  transfer_t *t = opaque;
  if (size && count > (BODY_LIMIT - t->length) / size) return 0;
  size_t n = size * count;
  if (!n || cancelled(t->cancel)) return 0;
  size_t wanted = t->length + n + 1;
  if (wanted > t->capacity) {
    size_t next = t->capacity ? t->capacity * 2 : 4096;
    if (next < wanted) next = wanted;
    if (next > BODY_LIMIT + 1) next = BODY_LIMIT + 1;
    char *allocation = realloc(t->body, next);
    if (!allocation) return 0;
    t->body = allocation;
    t->capacity = next;
  }
  memcpy(t->body + t->length, bytes, n);
  t->length += n;
  t->body[t->length] = 0;
  return n;
}

static int progress(void *opaque, curl_off_t dl_total, curl_off_t downloaded,
                    curl_off_t ul_total, curl_off_t uploaded) {
  (void)dl_total; (void)downloaded; (void)ul_total; (void)uploaded;
  const transfer_t *t = opaque;
  return cancelled(t->cancel);
}

bool ledger_market_https(const char *method, const char *url, const char *client_id,
                         const char *secret, const char *json, const atomic_bool *cancel,
                         char **body, size_t *len, int *status) {
  *body = NULL;
  *len = 0;
  *status = 0;
  if (cancelled(cancel) || !allowed(method, url, client_id, json) ||
      !credential_ok(secret, SECRET_LIMIT, false) ||
      (client_id && !credential_ok(client_id, 32, true)) ||
      !ledger_market_https_available()) return false;
  CURL *handle = api.easy_init();
  if (!handle) return false;
  transfer_t t = {.cancel = cancel};
  long code = 0;
  bool ok = false;
  struct curl_slist *headers = NULL;
  if (!add_header(&headers, "Accept: ", "application/json")) goto cleanup;
  if (client_id) {
    if (!add_header(&headers, "Client-Id: ", client_id) || !add_header(&headers, "Api-Key: ", secret) ||
        !add_header(&headers, "Content-Type: ", "application/json")) goto cleanup;
  } else if (!add_header(&headers, "Authorization: Bearer ", secret)) goto cleanup;
#define SET(option, value) do { if (api.easy_setopt(handle, CURLOPT_ ## option, (value)) != CURLE_OK) goto cleanup; } while (0)
  SET(URL, url);
  SET(NOSIGNAL, 1L);
  SET(FOLLOWLOCATION, 0L);
  SET(PROXY, "");
  SET(NOPROXY, "*");
  SET(SSL_VERIFYPEER, 1L);
  SET(SSL_VERIFYHOST, 2L);
  SET(SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
  SET(CONNECTTIMEOUT_MS, 7000L);
  SET(TIMEOUT_MS, 20000L);
  SET(MAXFILESIZE_LARGE, (curl_off_t)BODY_LIMIT);
  SET(HTTPHEADER, headers);
  SET(WRITEFUNCTION, receive_body);
  SET(WRITEDATA, &t);
  SET(XFERINFOFUNCTION, progress);
  SET(XFERINFODATA, &t);
  SET(NOPROGRESS, 0L);
  if (client_id) {
    SET(POST, 1L);
    SET(POSTFIELDS, json);
    SET(POSTFIELDSIZE, (long)strlen(json));
  }
#undef SET
  if (api.easy_perform(handle) != CURLE_OK || cancelled(cancel) ||
      api.easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code) != CURLE_OK ||
      code < 200 || code > 599) goto cleanup;
  if (code < 300) {
    if (!t.body) t.body = calloc(1, 1);
    if (!t.body) goto cleanup;
    *body = t.body;
    *len = t.length;
    t.body = NULL;
  }
  *status = (int)code;
  ok = true;
cleanup:
  api.easy_cleanup(handle);
  for (struct curl_slist *line = headers; line; line = line->next) wipe(line->data, strlen(line->data));
  api.slist_free_all(headers);
  if (t.body) wipe(t.body, t.length);
  free(t.body);
  return ok;
}

#else

/* No bundled desktop HTTPS library is supplied for other targets. */
bool ledger_market_https_available(void) { return false; }

bool ledger_market_https(const char *method, const char *url, const char *client_id,
                         const char *secret, const char *json, const atomic_bool *cancel,
                         char **body, size_t *len, int *status) {
  (void)method; (void)url; (void)client_id; (void)secret; (void)json; (void)cancel;
  *body = NULL;
  *len = 0;
  *status = 0;
  return false;
}

#endif
