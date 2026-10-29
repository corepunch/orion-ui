#ifndef __ORION_FORM_LOADER_H__
#define __ORION_FORM_LOADER_H__

// Runtime .orion forms: the form_def_t that orionc compiles into a header,
// built instead from the .orion file while the program runs, as WinAPI's
// CreateDialogIndirect builds a dialog from a template read at run time.
//
//   form_library_t *lib = form_library_load("penalties.orion");
//   const form_def_t *form = form_library_form(lib, "penalties");
//   show_dialog_from_form(form, NULL, parent, proc, state);
//   ...
//   form_library_free(lib);   // after every window built from it is gone
//
// Field bindings (field="ib.documents.number"), TableView and ComboBox
// sources resolve against the databases the document declares and those
// registered at load time, so load a library after register_database().
// Controls get numeric ids from FORM_LIBRARY_ID_BASE up; look them up by name.
//
// A form that uses what only compiled forms can express (toolbars, activity
// pages and context menus, whose commands are C identifiers) is rejected
// with a diagnostic on stderr; the library keeps every other form.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <orion/user/user.h>
#include <orion/user/database.h>

#define FORM_LIBRARY_ID_BASE 20000

typedef struct form_library_s form_library_t;

// Parses a .orion document from a file, or from memory (origin names it in
// diagnostics). Returns NULL when the document cannot be read or is not an
// <orion> document; individual forms that fail are reported and left out.
form_library_t *form_library_load(const char *path);
form_library_t *form_library_parse(const char *xml, size_t length, const char *origin);
// Frees the library and every definition in it. Windows keep pointers into
// their definitions, so destroy them first.
void form_library_free(form_library_t *lib);

const char *form_library_application_attr(const form_library_t *lib, const char *name);
const char *form_library_form_attr(const form_library_t *lib, const form_def_t *form, const char *name);

int form_library_form_count(const form_library_t *lib);
const form_def_t *form_library_form_at(const form_library_t *lib, int index);
const form_def_t *form_library_form(const form_library_t *lib, const char *name);

// The id of the control named `name` in `form`, or 0.
uint32_t form_library_control_id(const form_library_t *lib, const form_def_t *form, const char *name);
// The definition of a control, and any attribute it was declared with
// (action="calculate" on a Button, for a script to dispatch), or NULL.
const form_ctrl_def_t *form_library_control(const form_library_t *lib, const form_def_t *form, uint32_t id);
const char *form_library_control_attr(const form_library_t *lib, const form_def_t *form,
                                      uint32_t id, const char *attr);

// The <databases> the document declares, with schemas built at run time:
// record layouts follow the C structs orionc would declare. Create one with
// create_database_with_schema() and register it before showing a form that
// names it; the schema lives as long as the library.
int form_library_schema_count(const form_library_t *lib);
const db_schema_def_t *form_library_schema_at(const form_library_t *lib, int index);
const db_schema_def_t *form_library_schema(const form_library_t *lib, const char *name);

#endif
