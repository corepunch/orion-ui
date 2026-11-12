#ifndef __ORION_SCRIPT_RUNTIME_H__
#define __ORION_SCRIPT_RUNTIME_H__
#include <stdbool.h>
#include <stddef.h>
#include <lua.h>

#define ORION_RUNTIME_API 1
bool script_runtime_check_api(const char *required, char *error, size_t size);

typedef struct script_runtime_s script_runtime_t;
// One runtime per application/trust domain; UI-thread confined. Controllers
// are registry references, not VMs. The runtime outlives all attached pages.
script_runtime_t *script_runtime_open(const char *directory, const char *name);
bool script_runtime_close(script_runtime_t *runtime);
lua_State *script_runtime_state(script_runtime_t *runtime);
// Loads a fresh controller object with package-local require/cache. Shared
// Orion/native modules retain application-wide identity.
int script_runtime_controller(script_runtime_t *runtime, const char *path, char *error, size_t size);
// On failure `error` holds the message. Script faults (a missing method, a raised
// error) are also logged; a refusal (orion.assert, orion.fail) is only returned,
// for the caller to show to the user.
bool script_runtime_invoke(script_runtime_t *runtime, int controller, const char *method, const char *argument,
                           char *error, size_t size);
bool script_runtime_action(script_runtime_t *runtime, const char *module, const char *method, const char *argument, char *error, size_t size);
// Data-only request to a host-selected module; caller frees the returned string.
char *script_runtime_request(script_runtime_t *runtime, const char *module, const char *argument, char *error, size_t size);
void script_runtime_release(script_runtime_t *runtime, int controller);
#endif
