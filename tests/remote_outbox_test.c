#include "test_framework.h"
#include <orion/script/remote_outbox.h>
#ifdef _WIN32
static void test_unavailable(void) {
  TEST("remote outbox fails closed on Windows"); char *request = NULL;
  ASSERT_EQUAL(remote_outbox_load("lodge", "https://hotel", "company", "alice", &request), REMOTE_OUTBOX_ERROR);
  ASSERT_NULL(request); PASS();
}
int main(void) { TEST_START("Remote outbox"); test_unavailable(); TEST_END(); }
#else
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void test_outbox(void) {
  TEST("remote outbox persists exact requests, isolates identity and rejects conflicting changes");
  char base[] = "/tmp/orion-outbox-XXXXXX"; ASSERT_NOT_NULL(mkdtemp(base));
  const char *app = "lodge", *origin = "https://hotel.example", *company = "0123456789abcdef0123456789abcdef", *actor = "alice";
  const char *body = "{\"request_id\":\"one\",\"notes\":\"Гость \\\"A\\\"\"}";
  char *loaded = NULL;
  ASSERT_EQUAL(remote_outbox_load_at(base, app, origin, company, actor, &loaded), REMOTE_OUTBOX_EMPTY);
  ASSERT_NULL(loaded);
  ASSERT_EQUAL(remote_outbox_save_at(base, app, origin, company, actor, body), REMOTE_OUTBOX_OK);
  ASSERT_EQUAL(remote_outbox_save_at(base, app, origin, company, actor, body), REMOTE_OUTBOX_OK);
  ASSERT_EQUAL(remote_outbox_save_at(base, app, origin, company, actor, "{\"different\":true}"), REMOTE_OUTBOX_CONFLICT);
  ASSERT_EQUAL(remote_outbox_load_at(base, app, origin, company, "bob", &loaded), REMOTE_OUTBOX_EMPTY);
  ASSERT_EQUAL(remote_outbox_load_at(base, app, origin, "other-company", actor, &loaded), REMOTE_OUTBOX_EMPTY);
  ASSERT_EQUAL(remote_outbox_load_at(base, app, "https://other.example", company, actor, &loaded), REMOTE_OUTBOX_EMPTY);
  ASSERT_EQUAL(remote_outbox_load_at(base, app, origin, company, actor, &loaded), REMOTE_OUTBOX_OK);
  ASSERT_STR_EQUAL(loaded, body); free(loaded); loaded = NULL;
  ASSERT_EQUAL(remote_outbox_clear_at(base, app, origin, company, actor, "wrong"), REMOTE_OUTBOX_CONFLICT);
  ASSERT_EQUAL(remote_outbox_clear_at(base, app, origin, company, actor, body), REMOTE_OUTBOX_OK);
  ASSERT_EQUAL(remote_outbox_load_at(base, app, origin, company, actor, &loaded), REMOTE_OUTBOX_EMPTY);
  char directory[256]; snprintf(directory, sizeof(directory), "%s/orion-remote-outbox", base);
  struct stat st; ASSERT_EQUAL(stat(directory, &st), 0); ASSERT_EQUAL(st.st_mode & 077, 0);
  DIR *dir = opendir(directory); ASSERT_NOT_NULL(dir); struct dirent *entry;
  while ((entry = readdir(dir))) if (entry->d_name[0] != '.') {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
    ASSERT_EQUAL(stat(path, &st), 0); ASSERT_EQUAL(st.st_mode & 077, 0); unlink(path);
  }
  closedir(dir); rmdir(directory); rmdir(base); PASS();
}
static void test_restart(void) {
  TEST("remote outbox survives process exit and refuses malformed bytes");
  char base[] = "/tmp/orion-outbox-XXXXXX"; ASSERT_NOT_NULL(mkdtemp(base));
  const char *body = "{\"request_id\":\"durable\",\"args\":{}}";
  int ready[2]; ASSERT_EQUAL(pipe(ready), 0);
  pid_t child = fork(); ASSERT(child >= 0, "fork succeeds");
  if (!child) {
    close(ready[0]);
    if (remote_outbox_save_at(base, "ledger", "http://127.0.0.1:8790", "company", "alice", body) != REMOTE_OUTBOX_OK) _exit(1);
    if (write(ready[1], "x", 1) != 1) _exit(2);
    for (;;) pause();
  }
  close(ready[1]); char marker; ASSERT_EQUAL(read(ready[0], &marker, 1), 1); close(ready[0]);
  ASSERT_EQUAL(kill(child, SIGKILL), 0);
  int status; ASSERT_EQUAL(waitpid(child, &status, 0), child); ASSERT_TRUE(WIFSIGNALED(status));
  char *loaded = NULL;
  ASSERT_EQUAL(remote_outbox_load_at(base, "ledger", "http://127.0.0.1:8790", "company", "alice", &loaded), REMOTE_OUTBOX_OK);
  ASSERT_STR_EQUAL(loaded, body); free(loaded);
  ASSERT_EQUAL(remote_outbox_save_at(base, "ledger", "http://127.0.0.1:8790", "company", "other", "\xff"), REMOTE_OUTBOX_ERROR);
  char *oversized = malloc(1024 * 1024 + 1); ASSERT_NOT_NULL(oversized);
  memset(oversized, 'a', 1024 * 1024); oversized[1024 * 1024] = 0;
  ASSERT_EQUAL(remote_outbox_save_at(base, "ledger", "http://127.0.0.1:8790", "company", "other", oversized), REMOTE_OUTBOX_ERROR);
  free(oversized); loaded = NULL;
  ASSERT_EQUAL(remote_outbox_load_at(base, "ledger", "http://127.0.0.1:8790", "company", "other", &loaded), REMOTE_OUTBOX_EMPTY);
  ASSERT_EQUAL(remote_outbox_clear_at(base, "ledger", "http://127.0.0.1:8790", "company", "alice", body), REMOTE_OUTBOX_OK);
  char directory[256]; snprintf(directory, sizeof(directory), "%s/orion-remote-outbox", base);
  DIR *dir = opendir(directory); ASSERT_NOT_NULL(dir); struct dirent *entry;
  while ((entry = readdir(dir))) if (entry->d_name[0] != '.') {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name); unlink(path);
  }
  closedir(dir); rmdir(directory); rmdir(base); PASS();
}
static void test_symlink_and_concurrency(void) {
  TEST("remote outbox prevents link substitution and competing request overwrite");
  char base[] = "/tmp/orion-outbox-XXXXXX"; ASSERT_NOT_NULL(mkdtemp(base));
  const char *app = "lodge", *origin = "https://hotel", *company = "company", *actor = "alice";
  pid_t child = fork(); ASSERT(child >= 0, "fork succeeds");
  if (!child) _exit(remote_outbox_save_at(base, app, origin, company, actor, "{\"child\":true}"));
  int parent = remote_outbox_save_at(base, app, origin, company, actor, "{\"parent\":true}");
  int status; ASSERT_EQUAL(waitpid(child, &status, 0), child); ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQUAL((parent == REMOTE_OUTBOX_OK) + (WEXITSTATUS(status) == REMOTE_OUTBOX_OK), 1);
  char *loaded = NULL; ASSERT_EQUAL(remote_outbox_load_at(base, app, origin, company, actor, &loaded), REMOTE_OUTBOX_OK);
  ASSERT_TRUE(!strcmp(loaded, "{\"child\":true}") || !strcmp(loaded, "{\"parent\":true}")); free(loaded);
  char directory[256], item[512] = {0}; snprintf(directory, sizeof(directory), "%s/orion-remote-outbox", base);
  DIR *dir = opendir(directory); ASSERT_NOT_NULL(dir); struct dirent *entry;
  while ((entry = readdir(dir))) {
    size_t n = strlen(entry->d_name);
    if (n > 5 && !strcmp(entry->d_name + n - 5, ".json")) snprintf(item, sizeof(item), "%s/%s", directory, entry->d_name);
  }
  closedir(dir); ASSERT_TRUE(item[0] != 0); ASSERT_EQUAL(unlink(item), 0);
  ASSERT_EQUAL(symlink("/dev/null", item), 0);
  ASSERT_EQUAL(remote_outbox_load_at(base, app, origin, company, actor, &loaded), REMOTE_OUTBOX_ERROR);
  unlink(item); dir = opendir(directory); ASSERT_NOT_NULL(dir);
  while ((entry = readdir(dir))) if (entry->d_name[0] != '.') {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name); unlink(path);
  }
  closedir(dir); rmdir(directory); rmdir(base); PASS();
}
static void test_profile_override(void) {
  TEST("remote outbox profile override requires an owned private directory");
  char base[] = "/tmp/orion-outbox-profile-XXXXXX", alias[256]; ASSERT_NOT_NULL(mkdtemp(base));
  ASSERT_EQUAL(setenv("ORION_REMOTE_PROFILE", base, 1), 0);
  const char *body = "{\"request_id\":\"profile\"}";
  ASSERT_EQUAL(remote_outbox_save("lodge", "https://hotel", "company", "alice", body), REMOTE_OUTBOX_OK);
  ASSERT_EQUAL(remote_outbox_clear("lodge", "https://hotel", "company", "alice", body), REMOTE_OUTBOX_OK);
  ASSERT_EQUAL(chmod(base, 0755), 0);
  char *loaded = NULL;
  ASSERT_EQUAL(remote_outbox_load("lodge", "https://hotel", "company", "alice", &loaded), REMOTE_OUTBOX_ERROR);
  ASSERT_EQUAL(chmod(base, 0700), 0);
  snprintf(alias, sizeof(alias), "%s-link", base); ASSERT_EQUAL(symlink(base, alias), 0);
  ASSERT_EQUAL(setenv("ORION_REMOTE_PROFILE", alias, 1), 0);
  ASSERT_EQUAL(remote_outbox_load("lodge", "https://hotel", "company", "alice", &loaded), REMOTE_OUTBOX_ERROR);
  unsetenv("ORION_REMOTE_PROFILE"); unlink(alias);
  char directory[256]; snprintf(directory, sizeof(directory), "%s/orion-remote-outbox", base);
  DIR *dir = opendir(directory); ASSERT_NOT_NULL(dir); struct dirent *entry;
  while ((entry = readdir(dir))) if (entry->d_name[0] != '.') {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name); unlink(path);
  }
  closedir(dir); rmdir(directory); rmdir(base); PASS();
}
int main(void) { TEST_START("Remote outbox"); test_outbox(); test_restart(); test_symlink_and_concurrency(); test_profile_override(); TEST_END(); }
#endif
