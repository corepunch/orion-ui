/*
 * kernel/http.c — Orion async HTTP/HTTPS client.
 *
 * Architecture
 * ============
 * Requests are queued onto an internal linked list protected by a mutex.  A
 * single persistent worker thread drains the queue, performs the blocking
 * connect/TLS-handshake/send/recv cycle using the platform axNet* / axTls*
 * primitives, then posts a kWindowMessageHttpDone Orion message back to the
 * caller's window via post_message().
 *
 * The worker thread signals the Orion event loop via axPostMessageW so that
 * axWaitEvent() wakes up and the message is dispatched without polling delay.
 *
 * Threading model
 * ---------------
 * - Main thread:   calls http_request_async(), http_cancel(),
 *                  http_shutdown(), and receives window messages.
 * - Worker thread: one long-lived thread processes requests serially.
 *
 * Public API: see kernel/http.h
 */

/* Enable POSIX extensions (strdup, strncasecmp) without pulling in the full
 * GNU namespace.  Must come before any system header. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp */
#include <stdint.h>
#include <stdbool.h>

/* POSIX threads — available on macOS, Linux, and QNX. */
#include <pthread.h>

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

/* Progress notification interval (bytes between kWindowMessageHttpProgress). */
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
 * Global state
 * ====================================================================== */

static bool              g_initialized  = false;
static pthread_t         g_worker;
static pthread_mutex_t   g_mutex        = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t    g_cond         = PTHREAD_COND_INITIALIZER;
static http_pending_t   *g_queue_head   = NULL;
static http_pending_t   *g_queue_tail   = NULL;
static bool              g_worker_quit  = false;
static http_request_id_t g_next_id      = 1; /* starts at 1; 0 = invalid */

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
 * kWindowMessageHttpProgress messages periodically during the body download.
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
        http_progress_t prog;
        prog.bytes_received = body_received;
        prog.bytes_total    = content_length;
        prog.request_id     = req_id;
        post_message(notify_win, kWindowMessageHttpProgress,
                     (uint32_t)req_id, &prog);
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
                    char **location_out)
{
  *status_out         = 0;
  if (content_length_out) *content_length_out = (size_t)-1;
  if (location_out) *location_out = NULL;

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

    int hdr_len = snprintf(req_buf, sizeof(req_buf),
      "%s %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "Connection: close\r\n"
      "User-Agent: Orion/1.0\r\n",
      method_str, pu.path, pu.host);

    if (req->headers && req->headers[0]) {
      int rem = (int)sizeof(req_buf) - hdr_len;
      int n   = snprintf(req_buf + hdr_len, (size_t)rem,
                         "%s", req->headers);
      if (n > 0) hdr_len += n;
    }

    if (req->body && req->body_len > 0) {
      int rem = (int)sizeof(req_buf) - hdr_len;
      int n   = snprintf(req_buf + hdr_len, (size_t)rem,
                         "Content-Length: %zu\r\n", req->body_len);
      if (n > 0) hdr_len += n;
    }

    /* Terminate header block. */
    if (hdr_len + 2 < (int)sizeof(req_buf)) {
      req_buf[hdr_len++] = '\r';
      req_buf[hdr_len++] = '\n';
    }

    /* Send headers. */
    bool ok = send_all(sock, tls, req_buf, hdr_len);

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
    int    body_offset = parse_http_response(raw, raw_len, &status,
                                              NULL, &location);

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
    char  *body_copy = (char *)malloc(body_len + 1);
    if (body_copy) {
      memcpy(body_copy, raw + body_offset, body_len);
      body_copy[body_len] = '\0';
    }
    free(raw);
    free(current_url);

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

static void *
worker_thread(void *arg)
{
  (void)arg;

  for (;;) {
    pthread_mutex_lock(&g_mutex);
    while (!g_queue_head && !g_worker_quit)
      pthread_cond_wait(&g_cond, &g_mutex);

    if (g_worker_quit && !g_queue_head) {
      pthread_mutex_unlock(&g_mutex);
      break;
    }

    /* Dequeue the oldest pending (non-cancelled) request. */
    http_pending_t *req = NULL;
    for (http_pending_t *it = g_queue_head; it; it = it->next) {
      if (it->state == HTTP_STATE_PENDING) {
        req = it;
        break;
      }
    }

    if (!req) {
      /* All remaining items are cancelled / done — drain them. */
      while (g_queue_head) {
        http_pending_t *next = g_queue_head->next;
        free(g_queue_head->url);
        free(g_queue_head->body);
        free(g_queue_head->headers);
        free(g_queue_head);
        g_queue_head = next;
      }
      g_queue_tail = NULL;
      pthread_mutex_unlock(&g_mutex);
      continue;
    }

    req->state = HTTP_STATE_RUNNING;
    pthread_mutex_unlock(&g_mutex);

    /* Execute outside the lock so the main thread can cancel / enqueue. */
    http_response_t *resp = execute_request(req);

    pthread_mutex_lock(&g_mutex);
    bool cancelled = (req->state == HTTP_STATE_CANCELLED);
    req->state = HTTP_STATE_DONE;
    pthread_mutex_unlock(&g_mutex);

    if (cancelled) {
      /* Discard result; don't post a message. */
      http_response_free(resp);
    } else if (req->notify_win) {
      /* Transfer ownership of resp to the window proc via post_message. */
      post_message(req->notify_win, kWindowMessageHttpDone,
                   (uint32_t)req->id, resp);
    } else {
      http_response_free(resp);
    }

    /* Remove the completed request from the queue. */
    pthread_mutex_lock(&g_mutex);
    http_pending_t *cur = g_queue_head;
    http_pending_t *p   = NULL;
    while (cur && cur != req) { p = cur; cur = cur->next; }
    if (cur) {
      if (p) p->next = cur->next;
      else   g_queue_head = cur->next;
      if (g_queue_tail == cur) g_queue_tail = p;
    }
    pthread_mutex_unlock(&g_mutex);

    free(req->url);
    free(req->body);
    free(req->headers);
    free(req);
  }

  return NULL;
}

/* =========================================================================
 * Public API
 * ====================================================================== */

bool
http_init(void)
{
  if (g_initialized) return true;

  if (!axNetInit()) return false;

  g_worker_quit = false;
  g_queue_head  = NULL;
  g_queue_tail  = NULL;

  if (pthread_create(&g_worker, NULL, worker_thread, NULL) != 0) {
    axNetShutdown();
    return false;
  }

  g_initialized = true;
  return true;
}

void
http_shutdown(void)
{
  if (!g_initialized) return;

  pthread_mutex_lock(&g_mutex);
  g_worker_quit = true;
  /* Cancel all pending requests so the worker stops quickly. */
  for (http_pending_t *it = g_queue_head; it; it = it->next)
    if (it->state == HTTP_STATE_PENDING)
      it->state = HTTP_STATE_CANCELLED;
  pthread_cond_signal(&g_cond);
  pthread_mutex_unlock(&g_mutex);

  pthread_join(g_worker, NULL);

  /* Free anything left in the queue (worker drained its own items). */
  pthread_mutex_lock(&g_mutex);
  while (g_queue_head) {
    http_pending_t *next = g_queue_head->next;
    free(g_queue_head->url);
    free(g_queue_head->body);
    free(g_queue_head->headers);
    free(g_queue_head);
    g_queue_head = next;
  }
  g_queue_tail = NULL;
  pthread_mutex_unlock(&g_mutex);

  axNetShutdown();
  g_initialized = false;
}

http_request_id_t
http_request_async(window_t           *notify_win,
                   const char         *url,
                   const http_options_t *opts,
                   void               *userdata)
{
  (void)userdata; /* reserved for future use */

  if (!g_initialized || !url) return HTTP_INVALID_REQUEST;

  /* Validate the URL before queueing. */
  parsed_url_t pu;
  if (!parse_url(url, &pu)) return HTTP_INVALID_REQUEST;

  http_pending_t *req = (http_pending_t *)calloc(1, sizeof(*req));
  if (!req) return HTTP_INVALID_REQUEST;

  req->url = strdup(url);
  if (!req->url) { free(req); return HTTP_INVALID_REQUEST; }

  if (opts) {
    req->method     = opts->method;
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

  pthread_mutex_lock(&g_mutex);
  req->id = g_next_id++;
  if (g_next_id == HTTP_INVALID_REQUEST) g_next_id = 1; /* wrap, skip 0 */

  /* Append to the tail of the queue. */
  if (g_queue_tail) {
    g_queue_tail->next = req;
    g_queue_tail       = req;
  } else {
    g_queue_head = g_queue_tail = req;
  }
  pthread_cond_signal(&g_cond);
  pthread_mutex_unlock(&g_mutex);

  return req->id;
}

void
http_cancel(http_request_id_t id)
{
  if (id == HTTP_INVALID_REQUEST) return;

  pthread_mutex_lock(&g_mutex);
  for (http_pending_t *it = g_queue_head; it; it = it->next) {
    if (it->id == id) {
      if (it->state == HTTP_STATE_PENDING)
        it->state = HTTP_STATE_CANCELLED;
      break;
    }
  }
  pthread_mutex_unlock(&g_mutex);
}

void
http_response_free(http_response_t *resp)
{
  if (!resp) return;
  free(resp->body);
  free(resp->headers);
  free(resp);
}
