---
layout: default
title: Localization
nav_order: 14
---

# Localization

Orion applications are written in one neutral language (English) and
translated by XML **string catalogs** that sit beside the program. Nothing
about a language lives in code: adding Russian, German or Japanese means
adding catalog files.

The design follows the Win32 MUI resource model and Qt's `.ts` files:

| Win32 / MUI | Qt | Orion |
|---|---|---|
| Language-neutral binary + `app.exe.ru-RU.mui` satellite files | `.ts` / `.qm` per language | `<dir>/ru.xml`, `<dir>/ru-RU.xml` beside the app |
| Resource loader fallback `de-CH` → `de` → ultimate fallback | `QTranslator` fallback | `ru-RU.xml` over `ru.xml` over the source text |
| `LoadString(hInst, IDS_SAVE)` | `tr("Save")` | `ui_tr("Save")` |
| Dialog templates translated in the `.mui` | `.ui` strings extracted by `lupdate` | `.orion` texts translated when a form is instantiated |
| — | context (class name) disambiguates | `<context name="form">`, the form's name |
| `GetUserDefaultUILanguage` | `QLocale::system()` | `ui_system_language()` (`LC_ALL`/`LC_MESSAGES`/`LANG`) |
| `GetNumberFormatEx`, `GetDateFormatEx` | `QLocale::toString` | `ui_format_number`, `ui_format_date` |
| Pseudo-locale `qps-ploc` | — | `ui_set_language("qps-ploc")` |

## Catalog format

```xml
<strings language="ru" decimal="," group=" " date="dd.MM.yyyy">
  <string source="File">Файл</string>
  <string source="Save Infobase" where="menu">Сохранить информационную базу</string>
  <context name="document">
    <string source="Number:">Номер документа:</string>
  </context>
</strings>
```

- The `source` is the exact text in `.orion` or passed to `ui_tr()`.
- `where` is informational: where the generator found the text, or `code`
  for strings that only C code passes to `ui_tr()`.
- An empty translation is an untranslated template entry; the source text
  shows.
- `decimal`, `group` and `date` set the number and date format used by
  tables, bound text boxes and `ui_format_*`.
- A `<context>` names a form. Its entries win inside that form; global
  entries apply everywhere else.

## Layers and fallback

```c
ui_set_language("ru-RU");                    // framework: <exe>/../share/orion/locale
ui_add_string_catalog_dir("share/myapp/locale");   // the application
ui_add_string_catalog_dir("data/profile/locale");  // optional data layer
```

Each directory contributes `ru.xml` and then `ru-RU.xml`, so a regional file
lists only what differs. Later layers override earlier ones. Ledger uses a
third layer for its accounting configuration: the platform says "Tax ID",
the Russian configuration turns it into "ИНН" — in Russian *and* in English
("INN"), because the term belongs to the country, not the UI language.

## What is translated automatically

Everything declared in `.orion`, when it is instantiated:

- form titles (`<form title>`) and every control's `text`;
- `placeholder` cue text;
- `TableView` column titles;
- menu and context-menu labels (shortcuts after `\t` are kept);
- toolbar button text, tooltips and ActivityBar page tooltips;
- framework dialogs (message box buttons, file picker).

Generated forms carry their name as `form_def_t.id`; that is both the
catalog context and the marker that the form is a resource.
`show_dialog_from_form(def, title, ...)` treats `title` as source text too.
Windows made with plain `create_window()` keep the title they were given:
it may be user data.

Language changes apply to windows created afterwards. Like most Win32
applications, Ledger stores the choice and applies it on the next start.

## Strings in code

```c
message_box(win, ui_tr("Delete the selected document and its entries?"),
            ui_tr("Delete Document"), MB_OK);
snprintf(text, sizeof(text), ui_tr("This item is used by %d document line(s)."), refs);
```

- Translate **whole sentences**. Never assemble a message from translated
  fragments ("Delete the selected " + noun + "?"): word order and grammar
  differ between languages.
- A translated format string must keep the same conversions in the same
  order.
- Do not translate data: record names, file names and user input.

## Plural forms and numbers in words

Legal forms write amounts in words, in the language of their country rather
than the UI language, so these functions take the language explicitly
(`NULL` = UI language). Like ICU's spellout rules and CLDR plural categories:

```c
ui_plural_pick("ru", 22, "рубль|рубля|рублей", buf, sz);         // "рубля"
ui_spell_number("de", 5021, UI_GENDER_MASCULINE, buf, sz);       // "fünftausendeinundzwanzig"
ui_money_words_t rub = { "рубль|рубля|рублей", UI_GENDER_MASCULINE,
                         "копейка|копейки|копеек", UI_GENDER_FEMININE };
ui_spell_amount("ru", 1234.05, &rub, buf, sz);
// "Одна тысяча двести тридцать четыре рубля 05 копеек"
```

Russian numerals agree in gender with the counted noun ("одна тысяча", "две
копейки"). Rules exist for Russian, German and English; other languages get
digits.

## Names in grammatical cases

Russian documents address people in oblique cases: "Выдать Петрову Петру
Петровичу" (dative), "Принято от Сидоровой Анны Викторовны" (genitive).
`ui_inflect_name` declines a full name written surname first:

```c
ui_inflect_name("ru", "Петров Петр Петрович", UI_CASE_DATIVE, buf, sz);  // "Петрову Петру Петровичу"
ui_inflect_name("ru", "ИП Иванов Иван Иванович", UI_CASE_GENITIVE, buf, sz);  // "ИП Иванова Ивана Ивановича"
ui_inflect_name("ru", "Сидорова А.В.", UI_CASE_DATIVE, buf, sz);  // "Сидоровой А.В."
```

- Surnames in -ов/-ин and -ский/-ой decline like adjectives; women's
  surnames ending in a consonant (Шмидт) and surnames in -ко, -их/-ых do not.
- First names and patronymics follow the noun declensions, with the usual
  exceptions (Павел → Павла, Лев → Льва) and spelling rules (Маши, Машей).
- The gender comes from the patronymic, else the first name, else the
  surname. Initials and a leading abbreviation such as "ИП" stay as they are.
- Names with quotes — organizations — and languages other than Russian are
  returned unchanged.

## Extracting strings

`orionc` lists every translatable text in a `.orion` file, like `lupdate`:

```bash
build/bin/orionc --input apps/ledger/ledger.orion \
                 --strings apps/ledger/share/locale/ru.xml --language ru
```

Existing translations, `where="code"` entries and `<context>` sections are
kept; texts no longer used are marked `obsolete="true"` instead of being
dropped. Add strings used only from C by hand with `where="code"`.

## Testing layouts

Run with the pseudo-locale to find truncation and hard-coded text before
translating:

```bash
build/bin/ledger --lang qps-ploc
```

Every string becomes accented and about 40% longer — `[Šàvé ·]` — which is
the expansion Microsoft recommends planning for from English. Text that stays
plain English was never sent through a catalog. Auto-layout sizes labels and
buttons to their text; fixed widths are the usual source of truncation.
