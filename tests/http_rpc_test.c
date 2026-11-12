#include "test_framework.h"
#include <platform/platform.h>

#ifdef _WIN32
static void test_unavailable(void) {
  TEST("bounded RPC fails closed on Windows until its backend is available");
  char *body = NULL; size_t length = 0; int status = 0;
  ASSERT_EQUAL(axHttpRpc("GET", "http://127.0.0.1/", "secret", NULL, 1000, NULL, NULL, &body, &length, &status), AX_HTTP_RPC_UNAVAILABLE);
  ASSERT_NULL(body); PASS();
}
int main(void) { TEST_START("Bounded HTTP RPC"); test_unavailable(); TEST_END(); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/sysctl.h>
#endif

static bool contains_bytes(const char *data, size_t n, const char *needle) {
  size_t length = strlen(needle);
  for (size_t i = 0; i + length <= n; i++) if (!memcmp(data + i, needle, length)) return true;
  return false;
}
static int check_child_arguments(void) {
#ifdef __APPLE__
  pid_t children[64]; int count = proc_listchildpids(getppid(), children, sizeof(children));
  for (int i = 0; i < count; i++) {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(children[i], path, sizeof(path)) <= 0 || !strstr(path, "/curl")) continue;
    int query[] = {CTL_KERN, KERN_PROCARGS2, children[i]};
    char args[65536]; size_t size = sizeof(args);
    if (sysctl(query, 3, args, &size, NULL, 0) != 0) return -1;
    return !contains_bytes(args, size, "secret\"\\nX-Injected:evil") && !contains_bytes(args, size, "@payload");
  }
#elif defined(__linux__)
  char list[128]; snprintf(list, sizeof(list), "/proc/%d/task/%d/children", getppid(), getppid());
  FILE *children = fopen(list, "r"); if (!children) return -1;
  int pid;
  while (fscanf(children, "%d", &pid) == 1) {
    char name[128], args[65536]; snprintf(name, sizeof(name), "/proc/%d/cmdline", pid);
    FILE *command = fopen(name, "rb"); if (!command) continue;
    size_t size = fread(args, 1, sizeof(args), command); fclose(command);
    if (size && !strcmp(args, "curl")) {
      fclose(children);
      return !contains_bytes(args, size, "secret\"\\nX-Injected:evil") && !contains_bytes(args, size, "@payload");
    }
  }
  fclose(children);
#endif
  return -1;
}

static int64_t test_now(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static bool_t cancel_after(void *context) { return test_now() >= *(int64_t *)context; }
static int make_server(unsigned short *port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  struct sockaddr_in address = {0}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 1)) { close(fd); return -1; }
  socklen_t length = sizeof(address);
  if (getsockname(fd, (struct sockaddr *)&address, &length)) { close(fd); return -1; }
  *port = ntohs(address.sin_port); return fd;
}
static pid_t serve_one(int listener, int mode) {
  pid_t child = fork();
  if (child) { close(listener); return child; }
  signal(SIGPIPE, SIG_IGN);
  int peer = accept(listener, NULL, NULL); close(listener);
  if (peer < 0) _exit(2);
  if (mode == 6) {
    const char plaintext[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    write(peer, plaintext, sizeof(plaintext) - 1); close(peer); _exit(0);
  }
  char input[8192] = {0}; size_t used = 0;
  while (used + 1 < sizeof(input) && !strstr(input, "\r\n\r\n")) {
    ssize_t n = read(peer, input + used, sizeof(input) - used - 1);
    if (n <= 0) _exit(3);
    used += (size_t)n; input[used] = 0;
  }
  if (mode == 0) {
    if (!strstr(input, "Authorization: Bearer secret\"\\nX-Injected:evil") ||
        strstr(input, "\r\nX-Injected:") || !strstr(input, "POST /rpc ")) _exit(4);
#if defined(__APPLE__) || defined(__linux__)
    if (check_child_arguments() != 1) _exit(6);
#endif
    char *body = strstr(input, "\r\n\r\n") + 4;
    while (!strstr(body, "payload")) {
      ssize_t n = read(peer, input + used, sizeof(input) - used - 1);
      if (n <= 0) _exit(5);
      used += (size_t)n; input[used] = 0;
    }
    const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}";
    write(peer, reply, sizeof(reply) - 1);
  } else if (mode == 1) {
    sleep(2);
  } else if (mode == 2) {
    const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\n{";
    write(peer, reply, sizeof(reply) - 1); sleep(2);
  } else if (mode == 3) {
    const char reply[] = "HTTP/1.1 302 Found\r\nLocation: /follow\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    write(peer, reply, sizeof(reply) - 1);
  } else {
    const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 4194305\r\n\r\n";
    write(peer, reply, sizeof(reply) - 1);
    char block[4096]; memset(block, 'x', sizeof(block));
    for (int i = 0; i < 1025; i++) if (write(peer, block, sizeof(block)) <= 0) break;
  }
  close(peer); _exit(0);
}
static void stop_server(pid_t child) {
  int status; kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}
static void test_arguments(void) {
  TEST("bounded RPC rejects cleartext remote URL and supports early cancellation");
  char *body = NULL; size_t length = 0; int status = 0; int64_t now = test_now();
  ASSERT_EQUAL(axHttpRpc("GET", "http://example.com/", "secret", NULL, 1000, NULL, NULL, &body, &length, &status), AX_HTTP_RPC_ERROR);
  ASSERT_EQUAL(axHttpRpc("GET", "http://localhost.evil/", "secret", NULL, 1000, NULL, NULL, &body, &length, &status), AX_HTTP_RPC_ERROR);
  ASSERT_EQUAL(axHttpRpc("GET", "http://127.0.0.1/\r\nX-Injected:", "secret", NULL, 1000, NULL, NULL, &body, &length, &status), AX_HTTP_RPC_ERROR);
  ASSERT_EQUAL(axHttpRpc("GET", "http://127.0.0.1/", "secret\nInjected", NULL, 1000, NULL, NULL, &body, &length, &status), AX_HTTP_RPC_ERROR);
  ASSERT_EQUAL(axHttpRpc("GET", "http://127.0.0.1/", "secret", NULL, 1000, cancel_after, &now, &body, &length, &status), AX_HTTP_RPC_CANCELLED);
  ASSERT_NULL(body); PASS();
}
static void test_loopback(void) {
  TEST("bounded RPC handles success, redirects, deadlines, cancellation and response limits");
  if (access("/usr/bin/curl", X_OK)) {
    if (getenv("ORION_REQUIRE_LOOPBACK_TESTS")) { FAIL("system curl unavailable"); return; }
    SKIP("system curl unavailable");
  }
  for (int mode = 0; mode < 7; mode++) {
    unsigned short port; int listener = make_server(&port);
    if (listener < 0) {
      if (getenv("ORION_REQUIRE_LOOPBACK_TESTS")) { FAIL("loopback sockets unavailable"); return; }
      SKIP("loopback sockets unavailable");
    }
    pid_t child = serve_one(listener, mode == 5 ? 1 : mode);
    ASSERT(child > 0, "fork succeeds");
    char url[128]; snprintf(url, sizeof(url), "%s://127.0.0.1:%u/rpc", mode == 6 ? "https" : "http", port);
    char *body = NULL; size_t length = 0; int status = 0; int64_t stop = test_now() + 100;
    int result = axHttpRpc(mode == 0 ? "POST" : "GET", url, mode == 0 ? "secret\"\\nX-Injected:evil" : "secret-token", mode == 0 ? "@payload" : NULL,
                           mode == 1 || mode == 2 ? 150 : 3000, mode == 5 ? cancel_after : NULL, &stop, &body, &length, &status);
    stop_server(child);
    if (mode == 0) { ASSERT_EQUAL(result, AX_HTTP_RPC_OK); ASSERT_EQUAL(status, 200); ASSERT_STR_EQUAL(body, "{\"ok\":true}"); }
    if (mode == 1 || mode == 2) ASSERT_EQUAL(result, AX_HTTP_RPC_TIMEOUT);
    if (mode == 3) { ASSERT_EQUAL(result, AX_HTTP_RPC_OK); ASSERT_EQUAL(status, 302); }
    if (mode == 4) ASSERT_EQUAL(result, AX_HTTP_RPC_TOO_LARGE);
    if (mode == 5) ASSERT_EQUAL(result, AX_HTTP_RPC_CANCELLED);
    if (mode == 6) ASSERT_EQUAL(result, AX_HTTP_RPC_ERROR);
    free(body);
  }
  PASS();
}
static void test_self_signed(void) {
  TEST("bounded RPC rejects a self-signed TLS peer");
  if (access("/usr/bin/curl", X_OK) || access("/usr/bin/openssl", X_OK) || access("/usr/bin/python3", X_OK)) {
    if (getenv("ORION_REQUIRE_LOOPBACK_TESTS")) { FAIL("curl, openssl or python3 unavailable"); return; }
    SKIP("system curl, openssl or python3 unavailable");
  }
  unsigned short port; int listener = make_server(&port);
  if (listener < 0) {
    if (getenv("ORION_REQUIRE_LOOPBACK_TESTS")) { FAIL("loopback sockets unavailable"); return; }
    SKIP("loopback sockets unavailable");
  }
  char directory[] = "/tmp/orion-rpc-cert-XXXXXX", key[256], certificate[256];
  if (!mkdtemp(directory)) { close(listener); FAIL("temporary certificate directory"); return; }
  snprintf(key, sizeof(key), "%s/key.pem", directory);
  snprintf(certificate, sizeof(certificate), "%s/cert.pem", directory);
  pid_t generator = fork();
  if (!generator) {
    FILE *discard = freopen("/dev/null", "w", stdout); (void)discard;
    discard = freopen("/dev/null", "w", stderr); (void)discard;
    execl("/usr/bin/openssl", "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
          "-keyout", key, "-out", certificate, "-days", "1", "-subj", "/CN=localhost", (char *)NULL);
    _exit(127);
  }
  int child_status = 0;
  if (generator < 0 || waitpid(generator, &child_status, 0) != generator || !WIFEXITED(child_status) || WEXITSTATUS(child_status)) {
    close(listener); unlink(key); unlink(certificate); rmdir(directory); FAIL("generate self-signed test certificate"); return;
  }
  pid_t child = fork();
  if (!child) {
    alarm(8);
    char descriptor[16]; snprintf(descriptor, sizeof(descriptor), "%d", listener);
    const char *script = "import socket,ssl,sys\n"
      "s=socket.socket(fileno=int(sys.argv[1]))\n"
      "ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)\n"
      "ctx.load_cert_chain(sys.argv[2],sys.argv[3])\n"
      "c,_=s.accept()\n"
      "try:\n ctx.wrap_socket(c,server_side=True)\nexcept ssl.SSLError:\n sys.exit(0)\nsys.exit(3)\n";
    execl("/usr/bin/python3", "python3", "-c", script, descriptor, certificate, key, (char *)NULL);
    _exit(127);
  }
  close(listener);
  char url[128]; snprintf(url, sizeof(url), "https://localhost:%u/rpc", port);
  char *body = NULL; size_t length = 0; int status = 0;
  int result = axHttpRpc("GET", url, "secret-token", NULL, 5000, NULL, NULL, &body, &length, &status);
  if (child > 0) { if (waitpid(child, &child_status, 0) != child) child_status = -1; }
  unlink(key); unlink(certificate); rmdir(directory); free(body);
  ASSERT(child > 0, "TLS server started");
  ASSERT_EQUAL(result, AX_HTTP_RPC_ERROR);
  ASSERT_TRUE(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
  PASS();
}
int main(void) { TEST_START("Bounded HTTP RPC"); test_arguments(); test_loopback(); test_self_signed(); TEST_END(); }
#endif
