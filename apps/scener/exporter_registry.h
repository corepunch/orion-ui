#ifndef __EXPORTER_REGISTRY_H__
#define __EXPORTER_REGISTRY_H__

#include "exporter.h"

void exporter_registry_load(void);
int exporter_registry_count(void);
const scener_exporter_t *exporter_registry_at(int index, const char **path);
const scener_exporter_t *exporter_registry_find(const char *id);
const scener_exporter_t *exporter_registry_for_file(const char *file);

#endif
