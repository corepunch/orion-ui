#define _POSIX_C_SOURCE 200809L
#include "test_framework.h"
#include "test_env.h"
#include <orion/script/application.h>
#include <orion/script/remote.h>
#include <orion/kernel/http.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int start_server(pid_t *child) {
  int listener = socket(AF_INET, SOCK_STREAM, 0); if (listener < 0) return -1;
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000001u), .sin_port = 0};
  if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 2)) { close(listener); return -1; }
  socklen_t size = sizeof(address); if (getsockname(listener, (struct sockaddr *)&address, &size)) { close(listener); return -1; }
  *child = fork();
  if (*child == 0) {
    signal(SIGPIPE, SIG_IGN); alarm(8);
    for (int i = 0; i < 2; i++) {
      int fd = accept(listener, NULL, NULL); if (fd < 0) _exit(2);
      char request[8192] = {0}; size_t used = 0;
      while (used < sizeof(request) - 1 && !strstr(request, "\r\n\r\n")) {
        ssize_t got = read(fd, request + used, sizeof(request) - 1 - used);
        if (got <= 0) _exit(3); used += (size_t)got;
      }
      if (!strstr(request, "Authorization: Bearer test-token")) _exit(3);
      if (i == 0) {
        const char *body = "{\"protocol\":1,\"application\":\"lodge\",\"company_id\":\"0123456789abcdef0123456789abcdef\",\"actor\":\"alice\",\"role\":\"admin\",\"revision\":1}";
        char response[1024]; int n = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(body), body);
        if (write(fd, response, (size_t)n) != n) _exit(4);
      } else { struct timespec delay = {.tv_sec = 3}; nanosleep(&delay, NULL); }
      close(fd);
    }
    close(listener); _exit(0);
  }
  close(listener); return *child < 0 ? -1 : ntohs(address.sin_port);
}
static void test_bridge(void) {
  TEST("native RPC invokes callback on page and cancels on teardown");
  pid_t child; int port = start_server(&child); ASSERT_TRUE(port > 0);
  char url[128], error[2048], directory[128];
  snprintf(url, sizeof(url), "http://127.0.0.1:%d", port);
  setenv("ORION_SERVER_URL", url, 1); setenv("ORION_SERVER_TOKEN", "test-token", 1);
  ASSERT_TRUE(script_remote_configure_from_environment(error, sizeof(error)));
  snprintf(directory, sizeof(directory), "/tmp/orion-remote-bridge-%ld", (long)getpid()); mkdir(directory, 0700);
  test_env_init();
  script_application_t *app = script_application_open("tests/fixtures/remote_bridge", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app); script_page_t *page = script_application_main(app); ASSERT_NOT_NULL(page);
  ASSERT_TRUE(script_page_perform(page, "connect"));
  for (int i = 0; i < 500 && strcmp(script_page_control(page, "result")->title, "200"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_STR_EQUAL(script_page_control(page, "result")->title, "200");
  ASSERT_TRUE(script_page_perform(page, "connect"));
  script_application_close(app); repost_messages();
  ASSERT_FALSE(http_has_pending()); test_env_shutdown();
  kill(child, SIGTERM); int status; waitpid(child, &status, 0); rmdir(directory);
  PASS();
}
#endif
int main(void) {
  TEST_START("remote bridge");
#ifndef _WIN32
  test_bridge();
#endif
  TEST_END();
}
