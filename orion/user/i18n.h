#ifndef __UI_I18N_H__
#define __UI_I18N_H__

// i18n — localization: string catalogs in the spirit of Win32 MUI and Qt .ts files.
//
// Source text in .orion files and code is the neutral (English) language and
// the lookup key. Translations live in per-language XML catalogs beside the
// program, never in code:
//
//   <strings language="ru" decimal="," group=" ">
//     <string source="File">Файл</string>
//     <context name="document">                  <!-- a form name -->
//       <string source="Number:">Номер:</string>
//     </context>
//   </strings>
//
// Catalogs stack in layers: the framework's own, then the application's, then
// any later layer (e.g. an accounting configuration); a later layer overrides
// an earlier one. Each layer resolves the language like the MUI resource
// loader: "<dir>/ru.xml" (neutral) and then "<dir>/ru-RU.xml" (regional), so a
// regional catalog only lists what differs. A string without a translation
// shows its source text.
//
// Forms, menus, toolbars, table column titles and placeholders are translated
// when they are instantiated; switching language takes effect for windows
// created afterwards. Application code translates its own messages with
// ui_tr(). Whole sentences only: never build a message from translated
// fragments.
//
// "qps-ploc" (the Windows pseudo-locale) needs no catalog: every string is
// accented and lengthened by about 40% to expose truncation and hard-coded
// text.

#include <stdbool.h>
#include <stddef.h>

#define UI_PSEUDO_LANGUAGE "qps-ploc"

// Selects the UI language ("ru", "ru-RU", "en", "qps-ploc"), drops all loaded
// catalogs and loads the framework catalog from <exe>/../share/orion/locale.
bool ui_set_language(const char *language);
const char *ui_get_language(void);

// The user's preferred language from LC_ALL / LC_MESSAGES / LANG
// ("ru_RU.UTF-8" -> "ru-RU"); "en" when unset or "C"/"POSIX".
const char *ui_system_language(char *buf, size_t buf_sz);

// Adds a layer: loads the neutral then the regional catalog of the current
// language from dir. Returns true when at least one file was loaded.
bool ui_add_string_catalog_dir(const char *dir);
// Loads one catalog file into the current layer stack.
bool ui_load_string_catalog(const char *path);

// Translation of source in the current context, then without context.
// "Label\tCtrl+S" translates the label and keeps the shortcut. The result
// stays valid until the next ui_set_language().
const char *ui_tr(const char *source);
const char *ui_trc(const char *context, const char *source);

// Sets the context used by ui_tr (a form name while it is instantiated);
// returns the previous one so callers can restore it.
const char *ui_set_string_context(const char *context);
const char *ui_get_string_context(void);

// Formats a number with the catalog's decimal and grouping separators
// ("16 656,00" in Russian; "16656.00" without a catalog).
const char *ui_format_number(double value, int decimals, char *buf, size_t buf_sz);
// Formats an ISO date ("2003-03-10") with the catalog's date pattern, e.g.
// date="dd.MM.yyyy" -> "10.03.2003". Without a pattern the ISO text is kept.
const char *ui_format_date(const char *iso, char *buf, size_t buf_sz);
// Parses a date typed in the catalog's pattern or as ISO into "YYYY-MM-DD".
// Returns false (leaving iso empty) when the text is not a valid date.
bool ui_parse_date(const char *text, char *iso, size_t iso_sz);
// Parses a number typed in either the catalog's format or plain "1234.5";
// spaces and grouping separators are ignored.
double ui_parse_number(const char *text);

#endif // __UI_I18N_H__
