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
// A role="host" form's <ActivityBar><Page form="..."/> destinations get
// page_bindings whose commands the library numbers like control ids; pass the
// form to page_host_init(). A form that uses what only compiled forms can
// express (toolbars and context menus, whose commands are C identifiers) is
// rejected with a diagnostic on stderr; the library keeps every other form.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <orion/user/user.h>
#include <orion/user/database.h>
#include <orion/commctl/commctl.h>

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

// The application's <ribbon>, read exactly as orionc reads it for a compiled
// application, after the Windows Ribbon framework: <menus> is the command
// registry (Application.Commands) and the ribbon only places its commands.
//
//   <menus>
//     <menu name="desk" label="Front Desk">
//       <item name="check_in" label="Check In" shortcut="Ctrl+I" action="check_in" />
//       <item name="new" label="New Reservation..." action="new_booking" page="reception" />
//       <item name="exit" label="Exit" target="orion.application" action="quit" />
//     </menu>
//   </menus>
//   <ribbon>
//     <ApplicationMenu menu="file" />
//     <QuickAccessToolbar><Button command="desk.new" icon="..." /></QuickAccessToolbar>
//     <Tab label="Home"><Group label="Arrive and Leave">
//       <Button command="desk.check_in" icon="..." size="large" />
//     </Group></Tab>
//     <Tab label="Reservation" context="reception">...</Tab>
//   </ribbon>
//
// Where a compiled application handles a command in C, a declarative one names
// it on the menu <item>: action= runs on the selected page, or on page= after
// selecting it; target="orion.application" action= argument= is an application
// action. A <Tab context="form"> is a contextual tab, shown while that page is
// selected. NULL when the document declares no ribbon, or a malformed one.
typedef struct {
  const char *action, *page, *target, *argument;
} form_command_t;
typedef struct {
  ribbon_def_t          def;
  const menu_def_t     *menus;           // the declared <menus>, for the operating system's menu bar
  const char *const    *menu_names;      // name= of each menu
  int                   menu_count;
  const form_command_t *commands;        // commands[ident - first_ident]
  const char *const    *command_names;   // "menu.item"
  int                   command_count;
  uint16_t              first_ident;
  const accel_t        *accelerators;    // from the items' shortcut=
  int                   accelerator_count;
  const char *const    *contexts;        // a tab's context bit i is the page contexts[i]
  int                   context_count;
} form_ribbon_t;
const form_ribbon_t *form_library_ribbon(const form_library_t *lib);
// The command a ribbon or menu ident runs, or NULL; the ident of "menu.item", or 0.
const form_command_t *form_ribbon_command(const form_ribbon_t *ribbon, uint32_t ident);
uint32_t form_ribbon_command_id(const form_ribbon_t *ribbon, const char *name);
// The context bit of the page named `form` (0 when no tab names it).
uint32_t form_ribbon_context(const form_ribbon_t *ribbon, const char *form);

#endif
