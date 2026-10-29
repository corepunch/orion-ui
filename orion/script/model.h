#ifndef __ORION_SCRIPT_MODEL_H__
#define __ORION_SCRIPT_MODEL_H__
#include "runtime.h"
#include <orion/user/database.h>
bool script_model_attach(database_t *database, script_runtime_t *runtime, int controller);
void script_model_detach(database_t *database);
#endif
