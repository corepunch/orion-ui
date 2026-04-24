// tests/gitclient_test_helpers.h — portable file-system and git helpers
// shared by gitclient_backend_test.c and gitclient_ui_test.c.
//
// All helpers use only standard C (file I/O) and the platform's native shell
// command processor so they compile cleanly on Windows (cmd.exe) and POSIX.
//
// IMPORTANT: Do NOT include <windows.h> here.  Including it before Orion's
// ui.h / platform.h causes macro redefinition conflicts (OFN_FILEMUSTEXIST,
// SIF_ALL, CW_USEDEFAULT, etc.).  We use only standard-C or CRT headers on
// Windows: <direct.h> for _mkdir, <process.h> for _getpid().
//
// Constraints on directory paths used with these helpers:
//   • POSIX: paths must not contain single-quote characters.
//   • Windows: paths must not contain double-quote characters.
//   Temp directories created by gct_make_temp_dir() satisfy both constraints.

#ifndef __GITCLIENT_TEST_HELPERS_H__
#define __GITCLIENT_TEST_HELPERS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>  // stat() available on both POSIX and Windows CRT

#ifdef _WIN32
#  include <direct.h>   // _mkdir
#  include <process.h>  // _getpid
#else
#  include <unistd.h>   // mkdtemp
#endif

// ── Portable temporary directory creation ────────────────────────────────────
// On POSIX: creates /tmp/<prefix>_XXXXXX via mkdtemp().
// On Windows: creates %TEMP%\<prefix>_<PID> via _mkdir().
//   Uses getenv("TEMP") (standard C, no windows.h) to locate the system temp
//   directory, falling back to "C:\\Temp" if the variable is not set.
// Returns true and writes the path into buf (up to sz bytes) on success.
static inline bool gct_make_temp_dir(char *buf, size_t sz, const char *prefix) {
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp || !tmp[0]) tmp = getenv("TMP");
    if (!tmp || !tmp[0]) tmp = "C:\\Temp";
    snprintf(buf, sz, "%s\\%s_%d", tmp, prefix, (int)_getpid());
    return _mkdir(buf) == 0;
#else
    snprintf(buf, sz, "/tmp/%s_XXXXXX", prefix);
    return mkdtemp(buf) != NULL;
#endif
}

// ── Portable recursive directory removal ─────────────────────────────────────
// Uses `rmdir /s /q` on Windows and `rm -rf` on POSIX.
// Logs to stderr if the removal command fails.
static inline void gct_remove_dir(const char *path) {
    if (!path || !path[0]) return;
    char cmd[1024];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", path);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
#endif
    if (system(cmd) != 0)
        fprintf(stderr, "[gct] WARNING: cleanup failed: %s\n", cmd);
}

// ── Portable file write / append ─────────────────────────────────────────────
// Write text to path, overwriting any existing content.
static inline bool gct_write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs(text, f);
    fclose(f);
    return true;
}

// Append text to an existing file.
static inline bool gct_append_file(const char *path, const char *text) {
    FILE *f = fopen(path, "a");
    if (!f) return false;
    fputs(text, f);
    fclose(f);
    return true;
}

// ── Portable git command helper ───────────────────────────────────────────────
// Runs `git <subcmd>` with dir as the working directory, using the platform's
// native shell (sh on POSIX, cmd.exe on Windows).
// Returns true if the git process exits with code 0; logs the command to
// stderr on failure to aid debugging.
//
// subcmd is appended verbatim; use double-quoted strings for values that
// contain spaces — both sh and cmd.exe accept double-quoted arguments.
//
// NOTE: dir must not contain single quotes (POSIX) or double quotes (Windows).
// Temp directory paths created by gct_make_temp_dir() satisfy this constraint.
//
// Windows note: when the test binary is compiled with MinGW and spawned from
// an MSYS2 make, the inherited PATH is in Unix format (/usr/bin:...) which
// cmd.exe cannot parse.  We prepend the two well-known Git for Windows
// installation directories at the front of PATH inside the cmd.exe command so
// that git.exe is always found regardless of how the test was launched.
static inline bool gct_git(const char *dir, const char *subcmd) {
    char cmd[4096];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd),
        "set \"PATH=C:\\Program Files\\Git\\cmd;"
              "C:\\Program Files\\Git\\bin;"
              "%%PATH%%\" "
        "&& cd /d \"%s\" && git %s",
        dir, subcmd);
#else
    snprintf(cmd, sizeof(cmd), "cd '%s' && git %s", dir, subcmd);
#endif
    if (system(cmd) != 0) {
        fprintf(stderr, "[gct] git command failed: %s\n", cmd);
        return false;
    }
    return true;
}

#endif // __GITCLIENT_TEST_HELPERS_H__
