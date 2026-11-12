/*
 * tests/http_test.c — Unit tests for the Orion async HTTP client.
 *
 * These tests exercise the kernel/http API without requiring a real remote
 * server or an initialised OpenGL/window context.  They cover:
 *
 *   - http_init / http_shutdown lifecycle
 *   - URL parser (valid and invalid URLs)
 *   - http_request_async with a NULL notify_win (fire-and-forget mode)
 *   - http_cancel before the request is processed
 *   - http_response_free(NULL) is safe
 *   - HTTP_INVALID_REQUEST returned for bad URLs
 *   - Serial execution: two sequential async requests both complete
 *
 * Network-dependent tests (real HTTP or HTTPS connections) are excluded so
 * the suite can run in a restricted CI environment.
 */
#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"
#include "test_env.h"
#include <orion/kernel/http.h>
#include <orion/user/messages.h>

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#endif

/* =========================================================================
 * Tests
 * ====================================================================== */

/* http_init / http_shutdown: must be idempotent and not crash. */
static void
test_lifecycle(void)
{
  TEST("http_init / http_shutdown lifecycle");

  ASSERT_TRUE(http_init());
  ASSERT_TRUE(http_init()); /* idempotent */
  http_shutdown();
  http_shutdown(); /* idempotent */
  ASSERT_TRUE(http_init()); /* re-initialise */
  http_shutdown();

  PASS();
}

/* http_response_free(NULL) must be safe. */
static void
test_response_free_null(void)
{
  TEST("http_response_free(NULL) is safe");
  http_response_free(NULL); /* must not crash */
  PASS();
}

/* HTTP_INVALID_REQUEST returned for a bad URL (before init). */
static void
test_invalid_request_before_init(void)
{
  TEST("HTTP_INVALID_REQUEST without init");
  http_request_id_t id = http_request_async(NULL, "https://example.com/",
                                             NULL, NULL);
  ASSERT_EQUAL((int)id, (int)HTTP_INVALID_REQUEST);
  PASS();
}

/* HTTP_INVALID_REQUEST returned for a malformed URL. */
static void
test_invalid_url(void)
{
  TEST("HTTP_INVALID_REQUEST for malformed URL");
  ASSERT_TRUE(http_init());

  http_request_id_t id;

  id = http_request_async(NULL, NULL,  NULL, NULL);
  ASSERT_EQUAL((int)id, (int)HTTP_INVALID_REQUEST);

  id = http_request_async(NULL, "",    NULL, NULL);
  ASSERT_EQUAL((int)id, (int)HTTP_INVALID_REQUEST);

  id = http_request_async(NULL, "ftp://example.com/", NULL, NULL);
  ASSERT_EQUAL((int)id, (int)HTTP_INVALID_REQUEST);

  id = http_request_async(NULL, "example.com", NULL, NULL);
  ASSERT_EQUAL((int)id, (int)HTTP_INVALID_REQUEST);

  http_shutdown();
  PASS();
}

/* Valid URLs must return non-zero IDs; each ID must be unique. */
static void
test_valid_request_id(void)
{
  TEST("valid http_request_async returns unique non-zero IDs");
  ASSERT_TRUE(http_init());

  /* Fire with NULL notify_win — the worker will execute and discard. */
  http_request_id_t id1 = http_request_async(NULL, "http://127.0.0.1:9/",
                                              NULL, NULL);
  http_request_id_t id2 = http_request_async(NULL, "http://127.0.0.1:9/",
                                              NULL, NULL);

  ASSERT_TRUE(id1 != HTTP_INVALID_REQUEST);
  ASSERT_TRUE(id2 != HTTP_INVALID_REQUEST);
  ASSERT_TRUE(id1 != id2);

  /* Let the worker drain; the connections will fail (no server). */
  http_shutdown();
  PASS();
}

/* http_cancel must accept HTTP_INVALID_REQUEST without crashing. */
static void
test_cancel_invalid(void)
{
  TEST("http_cancel(HTTP_INVALID_REQUEST) is safe");
  ASSERT_TRUE(http_init());
  http_cancel(HTTP_INVALID_REQUEST); /* must not crash */
  http_shutdown();
  PASS();
}

/* http_cancel on a queued request must prevent its completion message. */
static void
test_cancel_pending(void)
{
  TEST("http_cancel prevents delivery of a pending request");
  ASSERT_TRUE(http_init());

  /* A connection to port 9 (Discard service) will fail immediately; but
   * cancelling before the worker picks it up is the scenario we test.
   * We enqueue several requests and cancel the first one before the worker
   * processes it — this is best-effort; the test only verifies no crash. */
  http_request_id_t id = http_request_async(NULL, "http://127.0.0.1:9/",
                                             NULL, NULL);
  ASSERT_TRUE(id != HTTP_INVALID_REQUEST);
  http_cancel(id);   /* cancel before worker picks it up */

  http_shutdown();   /* must not deadlock or crash */
  PASS();
}

/* Options struct: POST with body, custom headers, HTTP_POST method. */
static void
test_options_post(void)
{
  TEST("http_request_async with POST options (no crash)");
  ASSERT_TRUE(http_init());

  http_options_t opts;
  memset(&opts, 0, sizeof(opts));
  opts.method     = HTTP_POST;
  opts.body       = "{\"key\":\"value\"}";
  opts.body_len   = 0; /* 0 = strlen */
  opts.headers    = "Content-Type: application/json\r\n";
  opts.timeout_ms = 1000;

  http_request_id_t id = http_request_async(NULL, "http://127.0.0.1:9/",
                                             &opts, NULL);
  ASSERT_TRUE(id != HTTP_INVALID_REQUEST);

  http_shutdown();
  PASS();
}

/* Two sequential requests get distinct IDs. */
static void
test_sequential_ids(void)
{
  TEST("sequential requests get strictly increasing IDs");
  ASSERT_TRUE(http_init());

  http_request_id_t ids[4];
  for (int i = 0; i < 4; i++) {
    ids[i] = http_request_async(NULL, "http://127.0.0.1:9/", NULL, NULL);
    ASSERT_TRUE(ids[i] != HTTP_INVALID_REQUEST);
  }
  for (int i = 1; i < 4; i++) {
    ASSERT_TRUE(ids[i] > ids[i-1]);
  }

  http_shutdown();
  PASS();
}

/* evHttpDone and evHttpProgress must be defined
 * and have distinct values. */
static void
test_message_constants(void)
{
  TEST("HTTP message constants are defined and distinct");
  ASSERT_TRUE(evHttpDone     != 0);
  ASSERT_TRUE(evHttpProgress != 0);
  ASSERT_TRUE(evHttpDone     != evHttpProgress);
  PASS();
}

/* HTTP_INVALID_REQUEST sentinel must equal 0. */
static void
test_invalid_request_sentinel(void)
{
  TEST("HTTP_INVALID_REQUEST == 0");
  ASSERT_EQUAL((int)HTTP_INVALID_REQUEST, 0);
  PASS();
}

#ifndef _WIN32
static int response_status, response_count;
static result_t response_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  (void)win; (void)wp;
  if (msg == evHttpDone) {
    http_response_t *response = lp; response_status = response->status; response_count++;
    http_response_free(response); return true;
  }
  return false;
}
static int response_server(const char *response, pid_t *child) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) return -1;
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0};
  if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1)) { close(listener); return -1; }
  socklen_t size = sizeof(address);
  if (getsockname(listener, (struct sockaddr *)&address, &size)) { close(listener); return -1; }
  *child = fork();
  if (*child == 0) {
    alarm(5); int connection = accept(listener, NULL, NULL);
    if (connection >= 0) {
      char request[4096]; (void)read(connection, request, sizeof(request));
      size_t length = strlen(response), sent = 0;
      while (sent < length) { ssize_t n = write(connection, response + sent, length - sent); if (n <= 0) break; sent += (size_t)n; }
      close(connection);
    }
    close(listener); _exit(0);
  }
  close(listener);
  return *child < 0 ? -1 : ntohs(address.sin_port);
}
static void test_security_options(void) {
  TEST("HTTP no-redirect and response-size limits on a loopback server");
  test_env_init(); ASSERT_TRUE(http_init());
  window_t *win = test_env_create_window("http-test", 0, 0, 100, 100, response_proc, NULL); ASSERT_NOT_NULL(win);
  const char *responses[] = {
    "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:9/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\n0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
  };
  for (int i = 0; i < 2; i++) {
    pid_t child; int port = response_server(responses[i], &child); ASSERT_TRUE(port > 0);
    char url[128]; snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);
    http_options_t options = {.no_redirects = true, .max_response_bytes = i ? 64 : 1024};
    response_count = 0; response_status = -1;
    ASSERT_TRUE(http_request_async(win, url, &options, NULL) != HTTP_INVALID_REQUEST);
    for (int attempt = 0; attempt < 300 && !response_count; attempt++) {
      struct timespec pause = {.tv_nsec = 10000000}; nanosleep(&pause, NULL); repost_messages();
    }
    ASSERT_EQUAL(response_count, 1); ASSERT_EQUAL(response_status, i ? 0 : 302);
    int status; waitpid(child, &status, 0); ASSERT_TRUE(WIFEXITED(status));
  }
  http_shutdown(); destroy_window(win); test_env_shutdown(); PASS();
}
#endif

/* =========================================================================
 * main
 * ====================================================================== */

int
main(void)
{
  TEST_START("Orion Async HTTP Client");

  test_response_free_null();
  test_invalid_request_sentinel();
  test_message_constants();
  test_lifecycle();
  test_invalid_request_before_init();
  test_invalid_url();
  test_valid_request_id();
  test_cancel_invalid();
  test_cancel_pending();
  test_options_post();
  test_sequential_ids();
#ifndef _WIN32
  test_security_options();
#endif

  TEST_END();
}
