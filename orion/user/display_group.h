#ifndef __ORION_DISPLAY_GROUP_H__
#define __ORION_DISPLAY_GROUP_H__
#include "database.h"
#define dbSelectRecord (dbUser + 210)
lresult_t db_display_group(database_t *db, uint32_t msg, uint32_t wparam, void *lparam);
#endif
