#ifndef __GC_LAUNCH_H__
#define __GC_LAUNCH_H__

// launch.xml — how a checkout is started. The file sits at the repository root
// and is committed, so every worktree and branch carries its own targets.
//
//   <launch project="x-com">
//     <target name="City" default="true" run="swift run XComCity" />
//     <target name="Preset" run="swift run -c release XComCity">
//       <env name="XCOM_AUTOSTART" value="${preset}" />
//       <param name="preset" choices="preset1 preset2 demo" />
//     </target>
//     <target name="Tests" kind="test" run="swift test" />
//   </launch>

#include <stdbool.h>
#include <stddef.h>

#define GC_LAUNCH_FILE   "launch.xml"
#define GC_MAX_TARGETS   32
#define GC_MAX_PARAMS    4
#define GC_MAX_ENV       4

typedef enum { GC_TARGET_RUN, GC_TARGET_BUILD, GC_TARGET_TEST } gc_target_kind_t;

typedef struct { char name[32], choices[256], def[128]; } gc_launch_param_t;
typedef struct { char name[64], value[256]; } gc_launch_env_t;

typedef struct {
  char name[64], build[512], run[512], dir[256];
  gc_target_kind_t kind;
  bool is_default;
  gc_launch_param_t params[GC_MAX_PARAMS]; int param_count;
  gc_launch_env_t env[GC_MAX_ENV];         int env_count;
} gc_launch_target_t;

typedef struct {
  char project[96];
  gc_launch_target_t targets[GC_MAX_TARGETS];
  int count;
  int default_index;   // -1 without targets
} gc_launch_t;

// Reads <checkout>/launch.xml. Returns the number of targets, 0 when the file is
// absent, -1 when it is malformed (diagnosed on stderr; out is then empty).
int  gc_launch_load(const char *checkout, gc_launch_t *out);
int  gc_launch_parse(const char *xml_path, gc_launch_t *out);
// The shell command for a target: "cd dir && build && ENV=… run". values[i]
// overrides params[i] (NULL or a NULL entry keeps the param's default).
bool gc_launch_command(const gc_launch_target_t *t, const char *const *values, char *out, size_t n);
const char *gc_launch_kind_name(gc_target_kind_t kind);

#endif
