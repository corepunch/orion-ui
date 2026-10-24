// i18n_test.c — string catalogs: layers, MUI-style fallback, contexts,
// pseudo-locale, number/date formats, spelled-out numbers, and translation of
// declarative forms.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/menubar.h>

#define DIR "build/test-i18n"

static result_t test_form_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

static void write_file(const char *path, const char *text) {
  FILE *f = fopen(path, "wb");
  if (f) { fputs(text, f); fclose(f); }
}

static void setup(void) {
  axMkDir("build");
  axMkDir(DIR);
  axMkDir(DIR "/app");
  axMkDir(DIR "/cfg");
  write_file(DIR "/app/ru.xml",
    "<strings language=\"ru\" decimal=\",\" group=\" \" date=\"dd.MM.yyyy\">\n"
    "  <string source=\"File\">Файл</string>\n"
    "  <string source=\"Save\">Сохранить</string>\n"
    "  <string source=\"Tax ID:\">Налоговый номер:</string>\n"
    "  <string source=\"Untranslated\"></string>\n"
    "  <context name=\"invoice\"><string source=\"Number:\">Номер счета:</string></context>\n"
    "  <string source=\"Number:\">Номер:</string>\n"
    "</strings>\n");
  write_file(DIR "/app/ru-RU.xml",
    "<strings language=\"ru-RU\"><string source=\"Save\">Записать</string></strings>\n");
  write_file(DIR "/cfg/ru.xml",
    "<strings language=\"ru\"><string source=\"Tax ID:\">ИНН:</string></strings>\n");
}

static void test_layers_and_fallback(void) {
  TEST("catalogs: neutral then regional file, later layers override, missing text shows source");
  ASSERT_TRUE(ui_set_language("ru-RU"));
  ASSERT_TRUE(ui_add_string_catalog_dir(DIR "/app"));
  ASSERT_STR_EQUAL(ui_tr("File"), "Файл");
  ASSERT_STR_EQUAL(ui_tr("Save"), "Записать");             // ru-RU overrides ru
  ASSERT_STR_EQUAL(ui_tr("Tax ID:"), "Налоговый номер:");
  ASSERT_TRUE(ui_add_string_catalog_dir(DIR "/cfg"));
  ASSERT_STR_EQUAL(ui_tr("Tax ID:"), "ИНН:");               // configuration layer wins
  ASSERT_STR_EQUAL(ui_tr("Untranslated"), "Untranslated");  // empty translation = template entry
  ASSERT_STR_EQUAL(ui_tr("Unknown"), "Unknown");
  ASSERT_STR_EQUAL(ui_tr("Save\tCtrl+S"), "Записать\tCtrl+S");
  ASSERT_FALSE(ui_add_string_catalog_dir(DIR "/missing"));
  PASS();
}

static void test_contexts(void) {
  TEST("catalogs: a context entry beats the global one only inside that context");
  ASSERT_STR_EQUAL(ui_trc("invoice", "Number:"), "Номер счета:");
  ASSERT_STR_EQUAL(ui_trc("other", "Number:"), "Номер:");
  const char *outer = ui_set_string_context("invoice");
  ASSERT_STR_EQUAL(ui_tr("Number:"), "Номер счета:");
  ui_set_string_context(outer);
  ASSERT_STR_EQUAL(ui_tr("Number:"), "Номер:");
  PASS();
}

static void test_formats(void) {
  TEST("formats: catalog decimal, grouping and date pattern; parsing accepts both forms");
  char buf[64], iso[16];
  ASSERT_STR_EQUAL(ui_format_number(1234567.891, 2, buf, sizeof(buf)), "1 234 567,89");
  ASSERT_STR_EQUAL(ui_format_number(-950, 2, buf, sizeof(buf)), "-950,00");
  ASSERT_TRUE(ui_parse_number("1 234,50") == 1234.5);
  ASSERT_TRUE(ui_parse_number("1234.50") == 1234.5);
  ASSERT_STR_EQUAL(ui_format_date("2003-03-10", buf, sizeof(buf)), "10.03.2003");
  ASSERT_TRUE(ui_parse_date("14.03.2003", iso, sizeof(iso)));
  ASSERT_STR_EQUAL(iso, "2003-03-14");
  ASSERT_TRUE(ui_parse_date("2003-03-14", iso, sizeof(iso)));
  ASSERT_FALSE(ui_parse_date("31.13.2003", iso, sizeof(iso)));
  ui_set_language("en");
  ASSERT_STR_EQUAL(ui_format_number(1234.5, 2, buf, sizeof(buf)), "1234.50");
  ASSERT_STR_EQUAL(ui_format_date("2003-03-10", buf, sizeof(buf)), "2003-03-10");
  PASS();
}

static void test_pseudo_locale(void) {
  TEST("pseudo-locale: qps-ploc accents and lengthens every string, keeps shortcuts");
  ui_set_language(UI_PSEUDO_LANGUAGE);
  const char *s = ui_tr("Save\tCtrl+S");
  ASSERT(s[0] == '[' && strstr(s, "Šàvé") && strstr(s, "]\tCtrl+S"), "pseudo text");
  ui_set_language("en");
  ASSERT_STR_EQUAL(ui_tr("Save"), "Save");
  PASS();
}

static void test_system_language(void) {
  TEST("system language: LANG=ru_RU.UTF-8 maps to ru-RU");
  char buf[32];
  setenv("LC_ALL", "", 1);
  setenv("LC_MESSAGES", "", 1);
  setenv("LANG", "ru_RU.UTF-8", 1);
  ASSERT_STR_EQUAL(ui_system_language(buf, sizeof(buf)), "ru-RU");
  setenv("LANG", "C", 1);
  ASSERT_STR_EQUAL(ui_system_language(buf, sizeof(buf)), "en");
  PASS();
}

static void test_spellout(void) {
  TEST("spellout: numbers in words agree in gender; plural forms; amounts as legal forms write them");
  char buf[256];
  ASSERT_STR_EQUAL(ui_spell_number("ru", 21000, UI_GENDER_MASCULINE, buf, sizeof(buf)), "двадцать одна тысяча");
  ASSERT_STR_EQUAL(ui_spell_number("ru", 2, UI_GENDER_FEMININE, buf, sizeof(buf)), "две");
  ASSERT_STR_EQUAL(ui_spell_number("de", 5021, UI_GENDER_MASCULINE, buf, sizeof(buf)), "fünftausendeinundzwanzig");
  ASSERT_STR_EQUAL(ui_spell_number("de", 2000000, UI_GENDER_MASCULINE, buf, sizeof(buf)), "zwei Millionen");
  ASSERT_STR_EQUAL(ui_spell_number("en", 342, UI_GENDER_MASCULINE, buf, sizeof(buf)), "three hundred forty-two");
  ASSERT_STR_EQUAL(ui_spell_number("fr", 342, UI_GENDER_MASCULINE, buf, sizeof(buf)), "342");
  ASSERT_STR_EQUAL(ui_plural_pick("ru", 12, "рубль|рубля|рублей", buf, sizeof(buf)), "рублей");
  ASSERT_STR_EQUAL(ui_plural_pick("ru", 22, "рубль|рубля|рублей", buf, sizeof(buf)), "рубля");
  ASSERT_STR_EQUAL(ui_plural_pick("de", 2, "Euro", buf, sizeof(buf)), "Euro");
  ui_money_words_t rub = { "рубль|рубля|рублей", UI_GENDER_MASCULINE, "копейка|копейки|копеек", UI_GENDER_FEMININE };
  ASSERT_STR_EQUAL(ui_spell_amount("ru", 1234.05, &rub, buf, sizeof(buf)), "Одна тысяча двести тридцать четыре рубля 05 копеек");
  ui_money_words_t eur = { "Euro", UI_GENDER_MASCULINE, "Cent", UI_GENDER_MASCULINE };
  ASSERT_STR_EQUAL(ui_spell_amount("de", 699.48, &eur, buf, sizeof(buf)), "Sechshundertneunundneunzig Euro 48 Cent");
  PASS();
}

static void test_forms_translate(void) {
  TEST("instantiation: generated form texts translate in their context; plain windows keep their text");
  test_env_init();
  ui_set_language("ru");
  ui_add_string_catalog_dir(DIR "/app");
  form_ctrl_def_t label = { .class_name = "Label", .id = 1, .text = "Number:", .name = "lbl" };
  form_def_t def = { .name = "File", .id = "invoice", .width = 200, .height = 60,
                     .flags = WINDOW_AUTO_LAYOUT, .children = &label, .child_count = 1 };
  window_t *win = create_window_from_form(&def, 0, 0, NULL, test_form_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_STR_EQUAL(win->title, "Файл");
  ASSERT_STR_EQUAL(win->children->title, "Номер счета:");  // form context
  window_t *plain = create_window("File", 0, MAKERECT(0, 0, 50, 20), NULL, test_form_proc, 0, NULL);
  ASSERT_STR_EQUAL(plain->title, "File");
  ASSERT(ui_get_string_context() == NULL, "context restored after instantiation");
  destroy_window(plain);
  destroy_window(win);
  ui_set_language("en");
  test_env_shutdown();
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("i18n");
  setup();
  test_layers_and_fallback();
  test_contexts();
  test_formats();
  test_pseudo_locale();
  test_system_language();
  test_spellout();
  test_forms_translate();
  TEST_END();
}
