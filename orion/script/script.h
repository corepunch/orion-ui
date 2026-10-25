#ifndef __ORION_SCRIPT_H__
#define __ORION_SCRIPT_H__

// Lua add-ons: a .orion file declares an add-on's forms and scratch tables,
// a .lua file beside it supplies the behaviour. No C per add-on.
//
//   penalties.orion   <form name="penalties">...<Button text="Post" action="post" />
//   penalties.lua     local Penalties = {form = "penalties"}
//                     function Penalties:data() return {values = {...}, lists = {...}} end
//                     function Penalties:post(values) ... end
//                     return Penalties
//
// A page is a request, as in lua-objc: the host asks the script for data(),
// shows it, runs a button's action= as a method of the page, then asks for
// data() again. There are no bindings or observers.
//
// Scripts run in a sandbox: no io, no os beyond the clock and dates, no
// loading of native code; require() reaches the add-on's folder, Orion's own
// Lua modules (orion, orion.model) and modules a host registers. Data goes
// through datasources only, as C code's does: see docs/scripting.md.

#include <stdbool.h>
#include <lua.h>

#include <orion/user/user.h>

typedef struct script_page_s script_page_t;

// Modules every script can require, added by a host before opening pages
// (Ledger registers "ledger"). `open` is a lua_CFunction returning the module.
bool script_register_module(const char *name, lua_CFunction open);

// Opens an add-on from its .orion or .lua path (or the shared stem): loads the
// forms, creates and registers the databases the .orion declares, runs the
// script and calls its init(). Returns NULL, with the reason on stderr and in
// *error when given, if any step fails.
script_page_t *script_page_open(const char *path, char *error, size_t error_sz);
// Shows the page as a modal dialog over `parent`; returns when it closes.
void script_page_run(script_page_t *page, window_t *parent);
// Closes the add-on: destroys its databases and frees the script and forms.
void script_page_close(script_page_t *page);

// The page's title: its title field, else its form's.
const char *script_page_title(const script_page_t *page);

// For tests and hosts embedding a page in a window of their own: builds the
// page's window without a modal loop, runs an action as a button would, and
// reports the last refusal or script error ("" when the last action succeeded).
window_t *script_page_create(script_page_t *page, window_t *parent);
bool script_page_perform(script_page_t *page, const char *action);
const char *script_page_message(const script_page_t *page);
// The control named `name` in the page's window, or NULL.
window_t *script_page_control(const script_page_t *page, const char *name);

#endif
