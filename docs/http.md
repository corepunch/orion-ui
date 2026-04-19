# Async HTTP/HTTPS Client

Main project README quick-start section: [README.md](../README.md)

Orion provides a built-in async HTTP and HTTPS client that integrates with the
window message loop.  Applications issue requests with a single call and
receive the response as an ordinary Orion window message — no callbacks, no
polling, no curl.

## Architecture

```
Application                 Orion kernel layer             Platform layer
──────────                  ──────────────────             ──────────────
http_request_async()  ─────►  queue request
                              worker thread ──────────────► axNetSocket / axNetConnect
                                             ──────────────► axTlsConnect (HTTPS)
                                             ──────────────► axNet/TlsSend/Recv
                              post_message(evHttpDone)
window proc ◄─────────────── dispatch_message()
```

The worker is a single long-lived background thread.  The main thread is
never blocked.  HTTPS is handled transparently via the platform TLS layer
(Secure Transport on macOS, OpenSSL on Linux when `HAVE_OPENSSL` is defined,
Schannel on Windows).

## Quick Start

```c
#include "ui.h"   /* includes kernel/http.h transitively */

static result_t my_win_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam)
{
  switch (msg) {
    case evCreate:
      http_request_async(win, "https://api.example.com/data",
                         NULL, NULL);
      return true;

    case evHttpDone: {
      http_request_id_t id   = (http_request_id_t)wparam;
      http_response_t  *resp = (http_response_t *)lparam;
      if (resp->status == 200) {
        /* resp->body is a heap buffer of resp->body_len bytes */
        printf("Got %zu bytes\n", resp->body_len);
      } else if (resp->error) {
        fprintf(stderr, "Request %u failed: %s\n", id, resp->error);
      }
      http_response_free(resp);   /* transfer complete; must free */
      return true;
    }

    default:
      return false;
  }
}
```

## Complete Example (GET + Progress + Cancel)

```c
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  http_request_id_t active_request;
} net_demo_state_t;

static result_t net_demo_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam)
{
  net_demo_state_t *st = (net_demo_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      st = (net_demo_state_t *)calloc(1, sizeof(*st));
      if (!st) return false;
      win->userdata = st;
      st->active_request = http_request_async(win,
                                              "https://httpbin.org/bytes/200000",
                                              NULL, NULL);
      return true;

    case evHttpProgress: {
      http_progress_t *p = (http_progress_t *)lparam;
      printf("request %u progress: %zu/%zd\n",
             p->request_id, p->bytes_received, p->bytes_total);
      return true;
    }

    case evHttpDone: {
      http_request_id_t id = (http_request_id_t)wparam;
      http_response_t *resp = (http_response_t *)lparam;
      if (resp && resp->status == 200) {
        printf("request %u done: %zu bytes\n", id, resp->body_len);
      } else if (resp && resp->error) {
        printf("request %u failed: %s\n", id, resp->error);
      }
      http_response_free(resp);
      if (st && st->active_request == id)
        st->active_request = HTTP_INVALID_REQUEST;
      return true;
    }

    case evKeyDown:
      if (wparam == AX_KEY_ESCAPE && st &&
          st->active_request != HTTP_INVALID_REQUEST) {
        http_cancel(st->active_request);
        st->active_request = HTTP_INVALID_REQUEST;
        return true;
      }
      return false;

    case evDestroy:
      if (st) {
        if (st->active_request != HTTP_INVALID_REQUEST)
          http_cancel(st->active_request);
        free(st);
        win->userdata = NULL;
      }
      return true;

    default:
      return false;
  }
}
```

## Initialisation

`http_init()` / `http_shutdown()` must bracket usage.  They are called
automatically when the application uses `ui_init_graphics()` and
`ui_shutdown_graphics()` — standalone callers must call them explicitly.

```c
if (!http_init()) { /* handle error */ }
/* ... */
http_shutdown();
```

Both functions are idempotent.

## Issuing a Request

```c
http_request_id_t http_request_async(
    window_t            *notify_win,   /* window to receive the Done message */
    const char          *url,          /* "http://…" or "https://…" */
    const http_options_t *opts,        /* NULL = defaults (GET, no body) */
    void                *userdata);    /* reserved — pass NULL */
```

Returns `HTTP_INVALID_REQUEST` (0) on immediate failure (bad URL, OOM,
subsystem not initialised).

### Options

```c
typedef struct {
  http_method_t method;      /* HTTP_GET (default), HTTP_POST, HTTP_PUT, … */
  const char   *body;        /* request body bytes (NULL = none) */
  size_t        body_len;    /* 0 = treat body as null-terminated string */
  const char   *headers;     /* extra headers, each ending with \r\n */
  uint32_t      timeout_ms;  /* reserved; currently not enforced */
} http_options_t;
```

Example — POST with JSON body:

```c
http_options_t opts = {
  .method   = HTTP_POST,
  .body     = "{\"name\":\"Orion\"}",
  .headers  = "Content-Type: application/json\r\n",
};
http_request_id_t id = http_request_async(win, "https://api.example.com/items",
                                           &opts, NULL);
```

Example — PUT with explicit body length:

```c
static const char kPayload[] = "hello from Orion";

http_options_t opts = {
  .method   = HTTP_PUT,
  .body     = kPayload,
  .body_len = sizeof(kPayload) - 1,
  .headers  = "Content-Type: text/plain\r\n",
};

http_request_async(win, "https://api.example.com/blob/42", &opts, NULL);
```

## Receiving the Response

### `evHttpDone`

Posted to `notify_win` when the request finishes (success **or** failure).

| Parameter | Value |
|-----------|-------|
| `wparam`  | `http_request_id_t` — the handle returned by `http_request_async()` |
| `lparam`  | `http_response_t*`  — **caller owns**; call `http_response_free()` |

```c
typedef struct {
  int               status;      /* HTTP status code; 0 = transport error */
  char             *body;        /* response body (not null-terminated) */
  size_t            body_len;    /* body length in bytes */
  char             *headers;     /* response header block (null-terminated) */
  const char       *error;       /* static error string, or NULL on success */
  http_request_id_t request_id;
} http_response_t;
```

Always call `http_response_free(resp)` after processing — the framework
transfers ownership to the window proc.

### `evHttpProgress`

Posted periodically during large downloads **only** when the server sends a
`Content-Length` header.

| Parameter | Value |
|-----------|-------|
| `wparam`  | `http_request_id_t` |
| `lparam`  | `http_progress_t*` — framework-owned and valid **only during the message handler**; do NOT retain or free |

```c
typedef struct {
  size_t            bytes_received;
  ssize_t           bytes_total;     /* -1 if Content-Length was not provided */
  http_request_id_t request_id;
} http_progress_t;
```

## Cancellation

```c
void http_cancel(http_request_id_t id);
```

Marks a pending request as cancelled.  If the worker has not yet started it,
no `evHttpDone` is posted.  If the worker is already executing the
request the cancellation is noted but the network I/O continues until the
current read/write completes; the response is then discarded silently.

Safe to call after the request has already completed (no-op).

## Current Limitations

- `timeout_ms` is currently reserved and not enforced by the worker.
- Chunked transfer decoding is not implemented yet; responses using
  `Transfer-Encoding: chunked` are returned as raw payload bytes.

## Thread Safety

`http_request_async()` and `http_cancel()` are safe to call from the main
thread at any time after `http_init()`.  `http_shutdown()` must be called from
the main thread and must not be called concurrently with
`http_request_async()`.

## Redirects

Up to 8 HTTP redirects (301, 302, 303, 307, 308) are followed automatically.
For 303 responses the method is changed to GET regardless of the original
request method.

## HTTPS

HTTPS is selected automatically when the URL scheme is `https://`.  The
platform TLS backend performs full certificate verification.  There is no
public API surface for TLS — it is entirely internal.
