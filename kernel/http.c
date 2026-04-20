/*
 * kernel/http.c — Orion async HTTP/HTTPS client.
 *
 * Architecture
 * ============
 * Requests are queued onto an internal linked list protected by a mutex.  A
 * single persistent worker thread drains the queue, performs the blocking
 * connect/TLS-handshake/send/recv cycle using the platform axNet* / axTls*
 * primitives, then posts a evHttpDone Orion message back to the
 * caller's window via post_message().
 *
 * The worker thread signals the Orion event loop via axPostMessageW so that
 * axWaitMessage() wakes up and the message is dispatched without polling delay.
 *
 * Threading model
 * ---------------
 * - Main thread:   calls http_request_async(), http_cancel(),
 *                  http_shutdown(), and receives window messages.
 * - Worker thread: one long-lived thread processes requests serially.
 *
 * Public API: see kernel/http.h
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp, strcasecmp */
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

/* Threading backend.
 * - POSIX: pthreads on macOS/Linux/QNX
 * - Windows: Win32 synchronization/thread primitives */
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "../platform/platform.h"
#include "../user/user.h"
#include "../user/messages.h"
#include "kernel.h"
#include "http.h"

/* =========================================================================
 * Internal constants
 * ====================================================================== */

/* Maximum URL length accepted by the parser. */
#define HTTP_MAX_URL  2048

/* Initial receive-buffer size; grown dynamically if needed. */
#define HTTP_RECV_INITIAL  (16 * 1024)

/* Chunk size for axNetRecv / axTlsRecv calls. */
#define HTTP_CHUNK_SIZE    4096

/* Progress notification interval (bytes between evHttpProgress). */
#define HTTP_PROGRESS_INTERVAL  (64 * 1024)

/* Maximum number of HTTP redirects followed before giving up. */
#define HTTP_MAX_REDIRECTS  8

/* =========================================================================
 * Internal types
 * ====================================================================== */

/* State of a pending request. */
typedef enum {
  HTTP_STATE_PENDING   = 0,
  HTTP_STATE_RUNNING   = 1,
  HTTP_STATE_CANCELLED = 2,
  HTTP_STATE_DONE      = 3,
} http_req_state_t;

/* One entry in the request queue. */
typedef struct http_pending_s {
  http_request_id_t   id;
  http_req_state_t    state;
  window_t           *notify_win;

  /* URL copy (heap-allocated). */
  char               *url;

  /* Options copy. */
  http_method_t       method;
  char               *body;        /* heap copy, or NULL */
  size_t              body_len;
  char               *headers;     /* heap copy, or NULL */
  uint32_t            timeout_ms;

  struct http_pending_s *next;
} http_pending_t;

/* =========================================================================
 * Internal threading abstraction
 * ====================================================================== */

#ifdef _WIN32
typedef HANDLE             http_thread_t;
typedef CRITICAL_SECTION   http_mutex_t;
typedef CONDITION_VARIABLE http_cond_t;

static void http_mutex_init(http_mutex_t *m)   { InitializeCriticalSection(m); }
static void http_mutex_destroy(http_mutex_t *m){ DeleteCriticalSection(m); }
static void http_mutex_lock(http_mutex_t *m)   { EnterCriticalSection(m); }
static void http_mutex_unlock(http_mutex_t *m) { LeaveCriticalSection(m); }

static void http_cond_init(http_cond_t *c)     { InitializeConditionVariable(c); }
static void http_cond_destroy(http_cond_t *c)  { (void)c; }
static void http_cond_wait(http_cond_t *c, http_mutex_t *m) {
  SleepConditionVariableCS(c, m, INFINITE);
}
static void http_cond_signal(http_cond_t *c)   { WakeConditionVariable(c); }

#define HTTP_THREAD_RET DWORD WINAPI
static bool http_thread_create(http_thread_t *t, HTTP_THREAD_RET (*fn)(void *), void *arg) {
  *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
  return *t != NULL;
}
static void http_thread_join(http_thread_t t) {
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}
#else
typedef pthread_t       http_thread_t;
typedef pthread_mutex_t http_mutex_t;
typedef pthread_cond_t  http_cond_t;

static void http_mutex_init(http_mutex_t *m)   { pthread_mutex_init(m, NULL); }
static void http_mutex_lock(http_mutex_t *m)   { pthread_mutex_lock(m); }
static void http_mutex_unlock(http_mutex_t *m) { pthread_mutex_unlock(m); }

static void http_cond_init(http_cond_t *c)     { pthread_cond_init(c, NULL); }
static void http_cond_wait(http_cond_t *c, http_mutex_t *m) {
  pthread_cond_wait(c, m);
}
static void http_cond_signal(http_cond_t *c)   { pthread_cond_signal(c); }

#define HTTP_THREAD_RET void *
static bool http_thread_create(http_thread_t *t, HTTP_THREAD_RET (*fn)(void *), void *arg) {
  return pthread_create(t, NULL, fn, arg) == 0;
}
static void http_thread_join(http_thread_t t) {
  pthread_join(t, NULL);
}
#endif

/* =========================================================================
 * Global state
 * ====================================================================== */

typedef struct {
  bool              initialized;
  bool              sync_ready;
  bool              net_ready;
  http_thread_t     worker;
  http_mutex_t      mutex;
  http_cond_t       cond;
  http_pending_t   *queue_head;
  http_pending_t   *queue_tail;
  bool              worker_quit;
  http_request_id_t next_id;   /* starts at 1; 0 = invalid */
} http_state_t;

static http_state_t g_http = {
  .next_id = 1,
};

static bool
http_sync_init(void)
{
  if (g_http.sync_ready) return true;
  http_mutex_init(&g_http.mutex);
  http_cond_init(&g_http.cond);
  g_http.sync_ready = true;
  return true;
}

static bool
http_net_init_once(void)
{
  if (g_http.net_ready) return true;
  if (!axNetInit()) return false;
  g_http.net_ready = true;
  return true;
}

/* =========================================================================
 * URL parsing
 * ====================================================================== */

typedef struct {
  bool        is_https;
  char        host[256];
  uint16_t    port;
  char        path[HTTP_MAX_URL];
} parsed_url_t;

static bool
parse_url(const char *url, parsed_url_t *out)
{
  if (!url || !out) return false;

  memset(out, 0, sizeof(*out));

  const char *p = url;

  if (strncmp(p, "https://", 8) == 0) {
    out->is_https = true;
    p += 8;
  } else if (strncmp(p, "http://", 7) == 0) {
    out->is_https = false;
    p += 7;
  } else {
    return false; /* unsupported scheme */
  }

  /* Extract host (and optional port). */
  const char *slash = strchr(p, '/');
  const char *colon = strchr(p, ':');

  size_t host_len;
  if (colon && (!slash || colon < slash)) {
    host_len = (size_t)(colon - p);
    if (host_len == 0 || host_len >= sizeof(out->host)) return false;
    memcpy(out->host, p, host_len);
    out->host[host_len] = '\0';
    out->port = (uint16_t)atoi(colon + 1);
    if (out->port == 0) return false;
    p = colon + 1;
    while (*p && *p != '/') p++;
  } else {
    host_len = slash ? (size_t)(slash - p) : strlen(p);
    if (host_len == 0 || host_len >= sizeof(out->host)) return false;
    memcpy(out->host, p, host_len);
    out->host[host_len] = '\0';
    out->port = out->is_https ? 443 : 80;
    p = slash ? slash : p + host_len;
  }

  /* Path (everything from the first slash onwards, or "/" if absent). */
  if (*p == '/') {
    size_t path_len = strlen(p);
    if (path_len >= sizeof(out->path)) return false;
    memcpy(out->path, p, path_len + 1);
  } else {
    out->path[0] = '/';
    out->path[1] = '\0';
  }

  return true;
}

/* =========================================================================
 * HTTP request / response helpers
 * ====================================================================== */

static const char *
method_string(http_method_t m)
{
  switch (m) {
    case HTTP_GET:    return "GET";
    case HTTP_POST:   return "POST";
    case HTTP_PUT:    return "PUT";
    case HTTP_DELETE: return "DELETE";
    case HTTP_HEAD:   return "HEAD";
    default:          return "GET";
  }
}

/* Append formatted text into a fixed buffer and detect truncation. */
static bool
append_fmt(char *buf, size_t cap, int *len, const char *fmt, ...)
{
  va_list ap;
  int n;
  int rem;

  if (!buf || !len || !fmt || *len < 0) return false;
  if ((size_t)*len >= cap) return false;

  rem = (int)(cap - (size_t)*len);
  va_start(ap, fmt);
  n = vsnprintf(buf + *len, (size_t)rem, fmt, ap);
  va_end(ap);

  if (n < 0 || n >= rem) return false;
  *len += n;
  return true;
}

/* Send all bytes through either a plain socket or a TLS session. */
static bool
send_all(int sock, AXtlsctx *tls, const void *buf, int len)
{
  const char *p   = (const char *)buf;
  int         rem = len;
  while (rem > 0) {
    int n = tls ? axTlsSend(tls, p, rem) : axNetSend(sock, p, rem);
    if (n <= 0) return false;
    p   += n;
    rem -= n;
  }
  return true;
}

/* Receive bytes, growing the output buffer as needed.
 * When notify_win is non-NULL and content_length is known (>= 0), posts
 * evHttpProgress messages periodically during the body download.
 * Returns a heap-allocated buffer (caller frees) and sets *out_len.
 * On error returns NULL. */
static char *
recv_response(int sock, AXtlsctx *tls, size_t *out_len,
              window_t *notify_win, http_request_id_t req_id)
{
  size_t  cap  = HTTP_RECV_INITIAL;
  size_t  used = 0;
  char   *buf  = (char *)malloc(cap);
  if (!buf) return NULL;

  /* Whether we have already located the header/body boundary and parsed
   * Content-Length from the response headers. */
  bool    headers_done   = false;
  ssize_t content_length = -1; /* -1 = unknown */
  size_t  body_start     = 0;
  size_t  last_progress  = 0;

  for (;;) {
    if (used + HTTP_CHUNK_SIZE + 1 > cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) { free(buf); return NULL; }
      buf = nb;
    }

    int n = tls
      ? axTlsRecv(tls, buf + used, HTTP_CHUNK_SIZE)
      : axNetRecv(sock, buf + used, HTTP_CHUNK_SIZE);

    if (n < 0) { free(buf); return NULL; }
    if (n == 0) break; /* connection closed */
    used += (size_t)n;

    /* Once headers are complete, extract Content-Length for progress. */
    if (!headers_done) {
      buf[used] = '\0';
      const char *sep = strstr(buf, "\r\n\r\n");
      if (sep) {
        headers_done = true;
        body_start   = (size_t)(sep + 4 - buf);

        /* Scan for Content-Length header. */
        const char *p = buf;
        while (p < sep) {
          const char *eol = strstr(p, "\r\n");
          if (!eol) break;
          if (strncasecmp(p, "Content-Length:", 15) == 0) {
            content_length = (ssize_t)atol(p + 15);
            break;
          }
          p = eol + 2;
        }
      }
    }

    /* Post progress notification when Content-Length is known. */
    if (headers_done && notify_win && content_length >= 0) {
      size_t body_received = used > body_start ? used - body_start : 0;
      if (body_received - last_progress >= HTTP_PROGRESS_INTERVAL) {
        last_progress = body_received;
        http_progress_t *prog = (http_progress_t *)malloc(sizeof(*prog));
        if (prog) {
          prog->bytes_received = body_received;
          prog->bytes_total    = content_length;
          prog->request_id     = req_id;
          post_message(notify_win, evHttpProgress,
                       (uint32_t)req_id, prog);
        }
      }
    }
  }

  buf[used] = '\0';
  *out_len  = used;
  return buf;
}

/* Parse the status line and headers from a raw HTTP response buffer.
 * Returns the offset of the response body start (after the blank line),
 * or -1 on parse error. */
static int
parse_http_response(const char *raw, size_t raw_len,
                    int *status_out,
                    size_t *content_length_out,  /* may be NULL */
                    char **location_out,
                    bool *chunked_out)
{
  *status_out         = 0;
  if (content_length_out) *content_length_out = (size_t)-1;
  if (location_out) *location_out = NULL;
  if (chunked_out) *chunked_out = false;

  /* Status line: "HTTP/1.x NNN ..." */
  if (raw_len < 12) return -1;
  if (strncmp(raw, "HTTP/", 5) != 0) return -1;

  const char *sp = strchr(raw, ' ');
  if (!sp) return -1;
  *status_out = atoi(sp + 1);

  /* Find the blank line separating headers from body. */
  const char *body = strstr(raw, "\r\n\r\n");
  if (!body) return -1;

  /* Parse Content-Length and Location headers (within the header block). */
  const char *hdr_start = raw;
  const char *hdr_end   = body + 2; /* points to the second \r\n */

  const char *p = hdr_start;
  while (p < hdr_end) {
    const char *eol = strstr(p, "\r\n");
    if (!eol) break;

    if (content_length_out && strncasecmp(p, "Content-Length:", 15) == 0) {
      *content_length_out = (size_t)atol(p + 15);
    } else if (chunked_out && strncasecmp(p, "Transfer-Encoding:", 18) == 0) {
      const char *v = p + 18;
      while (v < eol && isspace((unsigned char)*v)) v++;
      /* Bounded token search within the header value [v, eol) only.
       * Per RFC 7230 §4, transfer-codings are comma-separated tokens;
       * verify "chunked" appears as a complete token (not a substring). */
      size_t val_len = (size_t)(eol - v);
      if (val_len >= 7) {
        for (size_t off = 0; off <= val_len - 7; off++) {
          if (strncasecmp(v + off, "chunked", 7) == 0) {
            bool at_start = (off == 0 || v[off - 1] == ','
                             || isspace((unsigned char)v[off - 1]));
            bool at_end   = (off + 7 == val_len || v[off + 7] == ','
                             || v[off + 7] == ';'
                             || isspace((unsigned char)v[off + 7]));
            if (at_start && at_end) {
              *chunked_out = true;
              break;
            }
          }
        }
      }
    } else if (location_out && strncasecmp(p, "Location:", 9) == 0) {
      const char *loc = p + 9;
      while (*loc == ' ') loc++;
      size_t loc_len = (size_t)(eol - loc);
      *location_out = (char *)malloc(loc_len + 1);
      if (*location_out) {
        memcpy(*location_out, loc, loc_len);
        (*location_out)[loc_len] = '\0';
      }
    }

    p = eol + 2;
  }

  return (int)((body + 4) - raw);
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static char *decode_chunked_body(const char *in, size_t in_len, size_t *out_len) {
  if (!in || !out_len) return NULL;

  size_t i = 0;
  size_t cap = in_len + 1;
  size_t used = 0;
  bool decoded_any = false;
  char *out = (char *)malloc(cap);
  if (!out) return NULL;

  while (i < in_len) {
    size_t line_start = i;
    while (i + 1 < in_len && !(in[i] == '\r' && in[i + 1] == '\n')) i++;
    if (i + 1 >= in_len) break;

    size_t line_end = i;
    i += 2; /* skip CRLF */

    size_t chunk_sz = 0;
    bool have_hex = false;
    size_t p = line_start;
    while (p < line_end && isspace((unsigned char)in[p])) p++;
    for (; p < line_end; p++) {
      if (in[p] == ';') break; /* chunk extension */
      if (isspace((unsigned char)in[p])) {
        while (p < line_end && isspace((unsigned char)in[p])) p++;
        if (p < line_end && in[p] != ';') {
          goto partial;
        }
        break;
      }
      int hv = hex_val(in[p]);
      if (hv < 0) goto partial;
      have_hex = true;
      /* Guard against chunk_sz overflow when accumulating hex digits. */
      if (chunk_sz > (SIZE_MAX >> 4)) goto partial;
      chunk_sz = (chunk_sz << 4) | (size_t)hv;
    }
    if (!have_hex) break;

    if (chunk_sz == 0) {
      while (i + 1 < in_len) {
        if (in[i] == '\r' && in[i + 1] == '\n') {
          i += 2;
          break;
        }
        while (i + 1 < in_len && !(in[i] == '\r' && in[i + 1] == '\n')) i++;
        if (i + 1 >= in_len) break;
        i += 2;
      }
      out[used] = '\0';
      *out_len = used;
      return out;
    }

    /* Overflow-safe bounds check: verify chunk_sz bytes + trailing CRLF fit in [i, in_len).
     * Loop invariant guarantees i <= in_len; the explicit check makes this self-documenting. */
    if (chunk_sz > in_len - i || in_len - i - chunk_sz < 2) break;
    /* Overflow-safe growth: verify used + chunk_sz + 1 does not wrap size_t. */
    if (chunk_sz > SIZE_MAX - used - 1) { free(out); return NULL; }
    if (used + chunk_sz + 1 > cap) {
      while (used + chunk_sz + 1 > cap) cap *= 2;
      char *nb = (char *)realloc(out, cap);
      if (!nb) { free(out); return NULL; }
      out = nb;
    }

    memcpy(out + used, in + i, chunk_sz);
    used += chunk_sz;
    decoded_any = true;
    i += chunk_sz;

    if (!(in[i] == '\r' && in[i + 1] == '\n')) break;
    i += 2;
  }

partial:
  if (decoded_any) {
    out[used] = '\0';
    *out_len = used;
    return out;
  }

  free(out);
  return NULL;
}

/* =========================================================================
 * Core request execution (runs on worker thread)
 * ====================================================================== */

static http_response_t *
execute_request(http_pending_t *req)
{
  http_response_t *resp = (http_response_t *)calloc(1, sizeof(*resp));
  if (!resp) return NULL;
  resp->request_id = req->id;
  resp->error      = NULL;

  char  *current_url       = strdup(req->url);
  /* Method may change on 303 redirects; use a local copy. */
  http_method_t current_method = req->method;
  if (!current_url) {
    resp->status = 0;
    resp->error  = "out of memory";
    return resp;
  }

  int redirect_count = 0;

  do {
    parsed_url_t pu;
    if (!parse_url(current_url, &pu)) {
      free(current_url);
      resp->status = 0;
      resp->error  = "malformed URL";
      return resp;
    }

    /* Open socket and connect. */
    int sock = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
    if (sock < 0) {
      free(current_url);
      resp->status = 0;
      resp->error  = "socket creation failed";
      return resp;
    }

    if (!axNetConnect(sock, pu.host, pu.port)) {
      axNetClose(sock);
      free(current_url);
      resp->status = 0;
      resp->error  = "connection failed";
      return resp;
    }

    /* TLS upgrade for HTTPS. */
    AXtlsctx *tls = NULL;
    if (pu.is_https) {
      tls = axTlsConnect(sock, pu.host);
      if (!tls) {
        axNetClose(sock);
        free(current_url);
        resp->status = 0;
        resp->error  = "TLS handshake failed";
        return resp;
      }
    }

    /* Build request line and headers. */
    char req_buf[HTTP_MAX_URL + 512];
    const char *method_str = method_string(current_method);

    int hdr_len = 0;
    bool ok = append_fmt(req_buf, sizeof(req_buf), &hdr_len,
      "%s %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Connection: close\r\n"
      "User-Agent: Orion/1.0\r\n",
      method_str, pu.path, pu.host);

    if (ok && req->headers && req->headers[0])
      ok = append_fmt(req_buf, sizeof(req_buf), &hdr_len, "%s", req->headers);

    if (ok && req->body && req->body_len > 0)
      ok = append_fmt(req_buf, sizeof(req_buf), &hdr_len,
                      "Content-Length: %zu\r\n", req->body_len);

    if (ok)
      ok = append_fmt(req_buf, sizeof(req_buf), &hdr_len, "\r\n");

    if (!ok) {
      if (tls) axTlsClose(tls);
      axNetClose(sock);
      free(current_url);
      resp->status = 0;
      resp->error  = "request headers too large";
      return resp;
    }

    /* Send headers. */
    ok = send_all(sock, tls, req_buf, hdr_len);

    /* Send body if present. */
    if (ok && req->body && req->body_len > 0)
      ok = send_all(sock, tls, req->body, (int)req->body_len);

    if (!ok) {
      if (tls) axTlsClose(tls);
      axNetClose(sock);
      free(current_url);
      resp->status = 0;
      resp->error  = "send failed";
      return resp;
    }

    /* Receive full response. */
    size_t raw_len = 0;
    char  *raw     = recv_response(sock, tls, &raw_len,
                                   req->notify_win, req->id);

    if (tls) axTlsClose(tls);
    axNetClose(sock);

    if (!raw) {
      free(current_url);
      resp->status = 0;
      resp->error  = "receive failed";
      return resp;
    }

    int    status = 0;
    char  *location = NULL;
    bool   chunked = false;
    int    body_offset = parse_http_response(raw, raw_len, &status,
                          NULL, &location, &chunked);

    if (body_offset < 0) {
      free(raw);
      free(location);
      free(current_url);
      resp->status = 0;
      resp->error  = "malformed HTTP response";
      return resp;
    }

    /* Handle redirects (301, 302, 303, 307, 308). */
    if ((status == 301 || status == 302 || status == 303 ||
         status == 307 || status == 308) &&
        location && redirect_count < HTTP_MAX_REDIRECTS)
    {
      free(raw);
      free(current_url);
      current_url = location; /* location is already heap-allocated */
      location    = NULL;
      /* For 303 responses, switch to GET regardless of original method. */
      if (status == 303) current_method = HTTP_GET;
      redirect_count++;
      continue;
    }

    free(location);

    /* Extract header block as a separate string. */
    size_t header_block_len = (size_t)(body_offset - 4); /* exclude \r\n\r\n */
    char  *headers_copy = (char *)malloc(header_block_len + 1);
    if (headers_copy) {
      memcpy(headers_copy, raw, header_block_len);
      headers_copy[header_block_len] = '\0';
    }

    /* Extract body. */
    size_t body_len  = raw_len - (size_t)body_offset;
    char  *body_copy = NULL;
    if (chunked) {
      body_copy = decode_chunked_body(raw + body_offset, body_len, &body_len);
      if (!body_copy) {
        body_len = raw_len - (size_t)body_offset;
        body_copy = (char *)malloc(body_len + 1);
        if (body_copy) {
          memcpy(body_copy, raw + body_offset, body_len);
          body_copy[body_len] = '\0';
        }
      }
    } else {
      body_copy = (char *)malloc(body_len + 1);
      if (body_copy) {
        memcpy(body_copy, raw + body_offset, body_len);
        body_copy[body_len] = '\0';
      }
    }
    free(raw);
    free(current_url);

    if (!body_copy) {
      resp->status = 0;
      resp->error  = "out of memory while storing response body";
      free(headers_copy);
      return resp;
    }

    resp->status   = status;
    resp->body     = body_copy;
    resp->body_len = body_len;
    resp->headers  = headers_copy;
    return resp;

  } while (true);
}

/* =========================================================================
 * Worker thread
 * ====================================================================== */

static HTTP_THREAD_RET
worker_thread(void *arg)
{
  (void)arg;

  for (;;) {
    http_mutex_lock(&g_http.mutex);
    while (!g_http.queue_head && !g_http.worker_quit)
      http_cond_wait(&g_http.cond, &g_http.mutex);

    if (g_http.worker_quit && !g_http.queue_head) {
      http_mutex_unlock(&g_http.mutex);
      break;
    }

    /* Dequeue the oldest pending (non-cancelled) request. */
    http_pending_t *req = NULL;
    for (http_pending_t *it = g_http.queue_head; it; it = it->next) {
      if (it->state == HTTP_STATE_PENDING) {
        req = it;
        break;
      }
    }

    if (!req) {
      /* All remaining items are cancelled / done — drain them. */
      while (g_http.queue_head) {
        http_pending_t *next = g_http.queue_head->next;
        free(g_http.queue_head->url);
        free(g_http.queue_head->body);
        free(g_http.queue_head->headers);
        free(g_http.queue_head);
        g_http.queue_head = next;
      }
      g_http.queue_tail = NULL;
      http_mutex_unlock(&g_http.mutex);
      continue;
    }

    req->state = HTTP_STATE_RUNNING;
    http_mutex_unlock(&g_http.mutex);

    /* Execute outside the lock so the main thread can cancel / enqueue. */
    http_response_t *resp = execute_request(req);

    http_mutex_lock(&g_http.mutex);
    bool cancelled = (req->state == HTTP_STATE_CANCELLED);
    bool shutting_down = g_http.worker_quit;
    req->state = HTTP_STATE_DONE;
    http_mutex_unlock(&g_http.mutex);

    if (cancelled || shutting_down) {
      /* Discard result; don't post a message. */
      http_response_free(resp);
    } else if (req->notify_win) {
      /* Transfer ownership of resp to the window proc via post_message. */
      post_message(req->notify_win, evHttpDone,
                   (uint32_t)req->id, resp);
    } else {
      http_response_free(resp);
    }

    /* Remove the completed request from the queue. */
    http_mutex_lock(&g_http.mutex);
    http_pending_t *cur = g_http.queue_head;
    http_pending_t *p   = NULL;
    while (cur && cur != req) { p = cur; cur = cur->next; }
    if (cur) {
      if (p) p->next = cur->next;
      else   g_http.queue_head = cur->next;
      if (g_http.queue_tail == cur) g_http.queue_tail = p;
    }
    http_mutex_unlock(&g_http.mutex);

    free(req->url);
    free(req->body);
    free(req->headers);
    free(req);
  }

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

/* =========================================================================
 * Public API
 * ====================================================================== */

bool
http_init(void)
{
  if (g_http.initialized) return true;
  if (!http_sync_init()) return false;
  if (!http_net_init_once()) return false;

  g_http.worker_quit = false;
  g_http.queue_head  = NULL;
  g_http.queue_tail  = NULL;

  if (!http_thread_create(&g_http.worker, worker_thread, NULL)) {
    return false;
  }

  g_http.initialized = true;
  return true;
}

void
http_shutdown(void)
{
  if (!g_http.initialized) return;

  http_mutex_lock(&g_http.mutex);
  g_http.worker_quit = true;
  /* Cancel all pending requests so the worker stops quickly. */
  for (http_pending_t *it = g_http.queue_head; it; it = it->next)
    if (it->state == HTTP_STATE_PENDING || it->state == HTTP_STATE_RUNNING)
      it->state = HTTP_STATE_CANCELLED;
  http_cond_signal(&g_http.cond);
  http_mutex_unlock(&g_http.mutex);

  http_thread_join(g_http.worker);

  /* Free anything left in the queue (worker drained its own items). */
  http_mutex_lock(&g_http.mutex);
  while (g_http.queue_head) {
    http_pending_t *next = g_http.queue_head->next;
    free(g_http.queue_head->url);
    free(g_http.queue_head->body);
    free(g_http.queue_head->headers);
    free(g_http.queue_head);
    g_http.queue_head = next;
  }
  g_http.queue_tail = NULL;
  http_mutex_unlock(&g_http.mutex);

  // Keep axNet initialized for process lifetime. Repeated init/shutdown cycles
  // can be unstable on some backends; http_init/http_shutdown therefore only
  // manage the worker thread and request queue lifecycle.
  g_http.initialized = false;
}

http_request_id_t
http_request_async(window_t           *notify_win,
                   const char         *url,
                   const http_options_t *opts,
                   void               *userdata)
{
  (void)userdata; /* reserved for future use */

  if (!g_http.initialized || !url) return HTTP_INVALID_REQUEST;

  /* Validate the URL before queueing. */
  parsed_url_t pu;
  if (!parse_url(url, &pu)) return HTTP_INVALID_REQUEST;

  http_pending_t *req = (http_pending_t *)calloc(1, sizeof(*req));
  if (!req) return HTTP_INVALID_REQUEST;

  req->url = strdup(url);
  if (!req->url) { free(req); return HTTP_INVALID_REQUEST; }

  if (opts) {
    req->method     = opts->method;
    /* timeout_ms is accepted and preserved for forward compatibility.
     * Current worker implementation does not enforce request timeouts. */
    req->timeout_ms = opts->timeout_ms;

    if (opts->body) {
      size_t blen   = opts->body_len ? opts->body_len : strlen(opts->body);
      req->body     = (char *)malloc(blen);
      if (!req->body) {
        free(req->url); free(req);
        return HTTP_INVALID_REQUEST;
      }
      memcpy(req->body, opts->body, blen);
      req->body_len = blen;
    }
    if (opts->headers) {
      req->headers = strdup(opts->headers);
      if (!req->headers) {
        free(req->body); free(req->url); free(req);
        return HTTP_INVALID_REQUEST;
      }
    }
  }

  req->notify_win = notify_win;
  req->state      = HTTP_STATE_PENDING;

  http_mutex_lock(&g_http.mutex);
  req->id = g_http.next_id++;
  if (g_http.next_id == HTTP_INVALID_REQUEST) g_http.next_id = 1; /* wrap, skip 0 */

  /* Append to the tail of the queue. */
  if (g_http.queue_tail) {
    g_http.queue_tail->next = req;
    g_http.queue_tail       = req;
  } else {
    g_http.queue_head = g_http.queue_tail = req;
  }
  http_cond_signal(&g_http.cond);
  http_mutex_unlock(&g_http.mutex);

  return req->id;
}

void
http_cancel(http_request_id_t id)
{
  if (id == HTTP_INVALID_REQUEST) return;

  http_mutex_lock(&g_http.mutex);
  for (http_pending_t *it = g_http.queue_head; it; it = it->next) {
    if (it->id == id) {
      if (it->state == HTTP_STATE_PENDING || it->state == HTTP_STATE_RUNNING)
        it->state = HTTP_STATE_CANCELLED;
      break;
    }
  }
  http_mutex_unlock(&g_http.mutex);
}

void
http_response_free(http_response_t *resp)
{
  if (!resp) return;
  free(resp->body);
  free(resp->headers);
  free(resp);
}
