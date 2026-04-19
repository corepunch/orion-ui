#ifndef __UI_HTTP_H__
#define __UI_HTTP_H__

/**
 * @file kernel/http.h
 * @brief Orion async HTTP/HTTPS client API.
 *
 * Provides a Win32-style message-based interface for non-blocking HTTP and
 * HTTPS requests.  Callers issue a request with http_request_async() and
 * receive kWindowMessageHttpDone (and optionally kWindowMessageHttpProgress)
 * Orion messages on the registered window when the request completes.
 *
 * The underlying transport (TCP sockets + TLS via the platform layer) is
 * entirely hidden from the caller; curl is not required.
 *
 * ### Typical usage
 * @code
 *   // In your window proc:
 *   case kWindowMessageCreate:
 *     http_request_async(win, "https://api.example.com/data",
 *                        NULL, NULL);
 *     return true;
 *
 *   case kWindowMessageHttpDone: {
 *     http_request_id_t id   = (http_request_id_t)wparam;
 *     http_response_t  *resp = (http_response_t *)lparam;
 *     if (resp->status == 200) {
 *       // use resp->body / resp->body_len
 *     }
 *     http_response_free(resp);
 *     return true;
 *   }
 * @endcode
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>   /* ssize_t */

/* Forward declaration — defined in user/user.h (included transitively). */
typedef struct window_s window_t;

/* -------------------------------------------------------------------------
 * Request handles
 * ---------------------------------------------------------------------- */

/** @brief Opaque request identifier returned by http_request_async(). */
typedef uint32_t http_request_id_t;

/**
 * @brief Sentinel value indicating an invalid / failed request handle.
 *
 * http_request_async() returns this value when the request cannot be queued
 * (e.g. malformed URL or out-of-memory).
 */
#define HTTP_INVALID_REQUEST ((http_request_id_t)0)

/* -------------------------------------------------------------------------
 * Request methods
 * ---------------------------------------------------------------------- */

/** @brief HTTP method constants for the @p method parameter. */
typedef enum {
  HTTP_GET    = 0, /**< HTTP GET  (default when opts is NULL). */
  HTTP_POST   = 1, /**< HTTP POST. */
  HTTP_PUT    = 2, /**< HTTP PUT. */
  HTTP_DELETE = 3, /**< HTTP DELETE. */
  HTTP_HEAD   = 4, /**< HTTP HEAD. */
} http_method_t;

/* -------------------------------------------------------------------------
 * Request options
 * ---------------------------------------------------------------------- */

/**
 * @brief Optional per-request configuration.
 *
 * Pass NULL to use all defaults.
 */
typedef struct {
  /** HTTP method (default: HTTP_GET). */
  http_method_t method;

  /** Request body for POST/PUT (may be NULL). */
  const char   *body;

  /** Length of @p body in bytes (0 = treat @p body as a null-terminated
   *  string; ignored when @p body is NULL). */
  size_t        body_len;

  /**
   * @brief Null-terminated string of extra request headers.
   *
   * Each header must be terminated with @c \\r\\n, e.g.:
   * @code "Content-Type: application/json\r\nAccept: *&#47;*\r\n" @endcode
   * Pass NULL for no extra headers.
   */
  const char   *headers;

  /**
   * @brief Reserved timeout field in milliseconds.
   *
   * Current implementation stores this value but does not enforce request
   * timeouts yet.
   */
  uint32_t      timeout_ms;
} http_options_t;

/* -------------------------------------------------------------------------
 * Response
 * ---------------------------------------------------------------------- */

/**
 * @brief HTTP response delivered via kWindowMessageHttpDone.
 *
 * Ownership is transferred to the window proc; call http_response_free()
 * exactly once after processing the message.
 */
typedef struct {
  /** HTTP status code (e.g. 200, 404).  0 indicates a transport error. */
  int           status;

  /** Response body bytes (not null-terminated; length is @p body_len). */
  char         *body;

  /** Number of bytes in @p body. */
  size_t        body_len;

  /**
   * @brief Null-terminated response headers as a single string.
   *
   * Individual headers are separated by @c \\r\\n.  The string is
   * null-terminated after the final header.  May be NULL if headers were
   * not retained.
   */
  char         *headers;

  /**
   * @brief Error description (NULL on success).
   *
   * Points to a static string; do not free.
   */
  const char   *error;

  /** The request ID that produced this response. */
  http_request_id_t request_id;
} http_response_t;

/* -------------------------------------------------------------------------
 * Progress notification
 * ---------------------------------------------------------------------- */

/**
 * @brief Download progress snapshot delivered via kWindowMessageHttpProgress.
 *
 * The pointer is framework-owned and only valid for the duration of the
 * message handler — do NOT retain or free it.
 */
typedef struct {
  /** Bytes received so far. */
  size_t bytes_received;

  /**
   * @brief Total expected bytes, or -1 if Content-Length was not provided.
   *
   * Declared as `ssize_t` so that the sentinel value -1 is unambiguous.
   */
  ssize_t bytes_total;

  /** The request ID for this progress update. */
  http_request_id_t request_id;
} http_progress_t;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Initialise the async HTTP subsystem.
 *
 * Must be called once before http_request_async().  Idempotent: safe to call
 * multiple times.
 *
 * @return true on success, false on failure.
 */
bool http_init(void);

/**
 * @brief Shut down the async HTTP subsystem.
 *
 * Cancels all in-flight requests and joins the worker thread.  After this
 * call no further kWindowMessageHttpDone messages will be posted.
 */
void http_shutdown(void);

/**
 * @brief Issue an asynchronous HTTP or HTTPS request.
 *
 * The request is executed on a background thread.  On completion (or error)
 * a kWindowMessageHttpDone message is posted to @p notify_win:
 *   - wparam = request handle (http_request_id_t)
 *   - lparam = heap-allocated http_response_t*
 *
 * Ownership of the http_response_t* is transferred to the window proc; the
 * caller must free it with http_response_free() when done.
 *
 * If the response body is large a kWindowMessageHttpProgress message is
 * also posted periodically (only when Content-Length is known).
 *
 * @param notify_win  Window that receives the completion message.
 * @param url         Full URL string, e.g. "https://example.com/api/v1/foo".
 *                    Both "http://" and "https://" schemes are supported.
 * @param opts        Optional per-request options (NULL = defaults).
 * @param userdata    Opaque pointer stored in http_response_t (not used by
 *                    the framework; for caller bookkeeping only).
 *                    NOTE: currently reserved; pass NULL.
 * @return Non-zero request handle on success, HTTP_INVALID_REQUEST on
 *         immediate failure (malformed URL, OOM, subsystem not initialised).
 */
http_request_id_t http_request_async(window_t       *notify_win,
                                     const char     *url,
                                     const http_options_t *opts,
                                     void           *userdata);

/**
 * @brief Cancel a pending request.
 *
 * If the request is still running it is abandoned; no
 * kWindowMessageHttpDone will be posted for it.  Safe to call after the
 * request has already completed (no-op in that case).
 *
 * @param id  Handle returned by http_request_async().
 */
void http_cancel(http_request_id_t id);

/**
 * @brief Free a response object.
 *
 * Must be called exactly once for every http_response_t* received via
 * kWindowMessageHttpDone.  Passing NULL is safe.
 *
 * @param resp  Response to free.
 */
void http_response_free(http_response_t *resp);

#endif /* __UI_HTTP_H__ */
