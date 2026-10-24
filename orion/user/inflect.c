// Declension of personal names, for documents that address a person in an
// oblique case: "Выдать Петрову Петру Петровичу", "Принято от Сидоровой Анны
// Викторовны". The rules are Russian grammar's own: surnames in -ов/-ин and
// -ский decline like adjectives, women's surnames ending in a consonant do
// not decline, first names and patronymics follow the noun declensions.
// Other languages return the name unchanged.

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <orion/user/i18n.h>

// One ending pattern: a word ending in suffix drops `remove` and takes
// forms[case] (forms[UI_CASE_NOMINATIVE] restores the word). NULL forms mean
// the word does not decline.
typedef struct {
  const char *suffix, *remove;
  const char *forms[6];
} inflect_rule_t;

#define NO_CHANGE { NULL, NULL, NULL, NULL, NULL, NULL }

static const inflect_rule_t kMaleSurname[] = {
  { "ых", "", NO_CHANGE }, { "их", "", NO_CHANGE }, { "ко", "", NO_CHANGE },
  { "ой", "ой", { "ой", "ого", "ому", "ого", "ым", "ом" } },
  { "кий", "ий", { "ий", "ого", "ому", "ого", "им", "ом" } },
  { "гий", "ий", { "ий", "ого", "ому", "ого", "им", "ом" } },
  { "хий", "ий", { "ий", "ого", "ому", "ого", "им", "ом" } },
  { "ий", "ий", { "ий", "его", "ему", "его", "им", "ем" } },
  { "ый", "ый", { "ый", "ого", "ому", "ого", "ым", "ом" } },
  { "ов", "", { "", "а", "у", "а", "ым", "е" } }, { "ев", "", { "", "а", "у", "а", "ым", "е" } },
  { "ёв", "", { "", "а", "у", "а", "ым", "е" } }, { "ин", "", { "", "а", "у", "а", "ым", "е" } },
  { "ын", "", { "", "а", "у", "а", "ым", "е" } },
  { "о", "", NO_CHANGE }, { "е", "", NO_CHANGE }, { "и", "", NO_CHANGE }, { "у", "", NO_CHANGE },
  { "ю", "", NO_CHANGE }, { "э", "", NO_CHANGE },
  { "ь", "ь", { "ь", "я", "ю", "я", "ем", "е" } },
  { "й", "й", { "й", "я", "ю", "я", "ем", "е" } },
  { "я", "я", { "я", "и", "е", "ю", "ей", "е" } },
  { "а", "а", { "а", "ы", "е", "у", "ой", "е" } },
  { "", "", { "", "а", "у", "а", "ом", "е" } },  // a consonant
};

static const inflect_rule_t kFemaleSurname[] = {
  { "ая", "ая", { "ая", "ой", "ой", "ую", "ой", "ой" } },
  { "яя", "яя", { "яя", "ей", "ей", "юю", "ей", "ей" } },
  { "ова", "а", { "а", "ой", "ой", "у", "ой", "ой" } }, { "ева", "а", { "а", "ой", "ой", "у", "ой", "ой" } },
  { "ёва", "а", { "а", "ой", "ой", "у", "ой", "ой" } }, { "ина", "а", { "а", "ой", "ой", "у", "ой", "ой" } },
  { "ына", "а", { "а", "ой", "ой", "у", "ой", "ой" } },
  { "ия", "ия", { "ия", "ии", "ии", "ию", "ией", "ии" } },
  { "я", "я", { "я", "и", "е", "ю", "ей", "е" } },
  { "а", "а", { "а", "ы", "е", "у", "ой", "е" } },
  { "", "", NO_CHANGE },  // Шмидт, Кузнец: women's consonant surnames keep their form
};

static const inflect_rule_t kMaleName[] = {
  { "Павел", "ел", { "ел", "ла", "лу", "ла", "лом", "ле" } },
  { "Лев", "ев", { "ев", "ьва", "ьву", "ьва", "ьвом", "ьве" } },
  { "Пётр", "ётр", { "ётр", "етра", "етру", "етра", "етром", "етре" } },
  { "ий", "ий", { "ий", "ия", "ию", "ия", "ием", "ии" } },
  { "й", "й", { "й", "я", "ю", "я", "ем", "е" } },
  { "ья", "ья", { "ья", "ьи", "ье", "ью", "ьёй", "ье" } },
  { "я", "я", { "я", "и", "е", "ю", "ей", "е" } },
  { "а", "а", { "а", "ы", "е", "у", "ой", "е" } },
  { "ь", "ь", { "ь", "я", "ю", "я", "ем", "е" } },
  { "о", "", NO_CHANGE },
  { "", "", { "", "а", "у", "а", "ом", "е" } },
};

static const inflect_rule_t kFemaleName[] = {
  { "ия", "ия", { "ия", "ии", "ии", "ию", "ией", "ии" } },
  { "я", "я", { "я", "и", "е", "ю", "ей", "е" } },
  { "а", "а", { "а", "ы", "е", "у", "ой", "е" } },
  { "ь", "ь", { "ь", "и", "и", "ь", "ью", "и" } },
  { "", "", NO_CHANGE },
};

static const inflect_rule_t kMalePatronymic[] = {
  { "ич", "", { "", "а", "у", "а", "ем", "е" } },
  { "", "", NO_CHANGE },
};

static const inflect_rule_t kFemalePatronymic[] = {
  { "на", "а", { "а", "ы", "е", "у", "ой", "е" } },
  { "", "", NO_CHANGE },
};

static bool inflect_ends(const char *word, size_t len, const char *suffix) {
  size_t n = strlen(suffix);
  return n <= len && !memcmp(word + len - n, suffix, n);
}

// Spelling rules: ы becomes и after г к х ж ш ч щ; unstressed о becomes е
// after ж ш ч щ ц.
static const char *inflect_spell(const char *stem, size_t stem_len, const char *ending) {
  static const char *velar[] = { "г", "к", "х", "ж", "ш", "ч", "щ" }, *hush[] = { "ж", "ш", "ч", "щ", "ц" };
  for (size_t i = 0; i < sizeof(velar) / sizeof(velar[0]); i++)
    if (inflect_ends(stem, stem_len, velar[i]) && !strcmp(ending, "ы")) return "и";
  for (size_t i = 0; i < sizeof(hush) / sizeof(hush[0]); i++)
    if (inflect_ends(stem, stem_len, hush[i])) {
      if (!strcmp(ending, "ой")) return "ей";
      if (!strcmp(ending, "ом") && strcmp(hush[i], "ц")) return "ем";
    }
  return ending;
}

static bool inflect_upper(const unsigned char *p) {
  return (p[0] >= 'A' && p[0] <= 'Z') || (p[0] == 0xD0 && ((p[1] >= 0x90 && p[1] <= 0xAF) || p[1] == 0x81));
}

static void inflect_word(const char *word, size_t len, const inflect_rule_t *rules, size_t count,
                         ui_case_t gcase, char *out, size_t sz) {
  const inflect_rule_t *rule = NULL;
  for (size_t i = 0; i < count && !rule; i++) {
    bool exact = inflect_upper((const unsigned char *)rules[i].suffix);  // "Павел": that name only
    if (exact ? strlen(rules[i].suffix) == len && !memcmp(word, rules[i].suffix, len)
              : inflect_ends(word, len, rules[i].suffix)) rule = &rules[i];
  }
  const char *ending = rule ? rule->forms[gcase] : NULL;
  if (!ending || gcase == UI_CASE_NOMINATIVE) { snprintf(out, sz, "%.*s", (int)len, word); return; }
  size_t stem = len - strlen(rule->remove);
  snprintf(out, sz, "%.*s%s", (int)stem, word, inflect_spell(word, stem, ending));
}

// Hyphenated surnames decline in each part: Римский-Корсаков.
static void inflect_surname(const char *word, size_t len, bool female, ui_case_t gcase, char *out, size_t sz) {
  const char *dash = memchr(word, '-', len);
  const inflect_rule_t *rules = female ? kFemaleSurname : kMaleSurname;
  size_t count = female ? sizeof(kFemaleSurname) / sizeof(kFemaleSurname[0]) : sizeof(kMaleSurname) / sizeof(kMaleSurname[0]);
  if (!dash) { inflect_word(word, len, rules, count, gcase, out, sz); return; }
  char first[128];
  inflect_word(word, (size_t)(dash - word), rules, count, gcase, first, sizeof(first));
  size_t used = (size_t)snprintf(out, sz, "%s-", first);
  if (used < sz) inflect_surname(dash + 1, len - (size_t)(dash + 1 - word), female, gcase, out + used, sz - used);
}

// "ИП", "ЧП": an abbreviation before the name stays as it is.
static bool inflect_abbreviation(const char *word, size_t len) {
  if (len < 2) return false;
  for (size_t i = 0; i < len; i += ((unsigned char)word[i] >= 0x80 ? 2 : 1))
    if (!inflect_upper((const unsigned char *)word + i)) return false;
  return true;
}

static bool inflect_female_name(const char *word, size_t len) {
  static const char *male[] = { "Никита", "Илья", "Фома", "Кузьма", "Лука", "Савва", "Фока", "Данила", "Гаврила" };
  static const char *female_soft[] = { "Любовь", "Нинель", "Адель", "Рахиль", "Юдифь", "Эсфирь" };
  for (size_t i = 0; i < sizeof(male) / sizeof(male[0]); i++)
    if (strlen(male[i]) == len && !memcmp(word, male[i], len)) return false;
  for (size_t i = 0; i < sizeof(female_soft) / sizeof(female_soft[0]); i++)
    if (strlen(female_soft[i]) == len && !memcmp(word, female_soft[i], len)) return true;
  return inflect_ends(word, len, "а") || inflect_ends(word, len, "я");
}

static bool inflect_female_surname(const char *word, size_t len) {
  static const char *endings[] = { "ова", "ева", "ёва", "ина", "ына", "ая", "яя" };
  for (size_t i = 0; i < sizeof(endings) / sizeof(endings[0]); i++)
    if (inflect_ends(word, len, endings[i])) return true;
  return false;
}

const char *ui_inflect_name(const char *language, const char *name, ui_case_t gcase, char *buf, size_t buf_sz) {
  if (!buf || !buf_sz) return "";
  buf[0] = '\0';
  if (!name) return buf;
  if (!language) language = ui_get_language();
  bool russian = !strncmp(language, "ru", 2) && (language[2] == '\0' || language[2] == '-' || language[2] == '_');
  // Organizations ("ООО \"Ромашка\"") and other languages keep their name.
  if (!russian || gcase == UI_CASE_NOMINATIVE || (int)gcase < 0 || gcase > UI_CASE_PREPOSITIONAL ||
      strchr(name, '"') || strstr(name, "«")) {
    snprintf(buf, buf_sz, "%s", name);
    return buf;
  }
  struct { const char *p; size_t len; } words[8];
  int count = 0;
  for (const char *p = name; *p && count < 8; ) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *end = strchr(p, ' ');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    words[count].p = p; words[count].len = len; count++;
    p += len;
  }
  int first = 0;
  while (first < count && inflect_abbreviation(words[first].p, words[first].len)) first++;
  int names[3], n = 0;  // surname, first name, patronymic; initials stay
  for (int i = first; i < count && n < 3; i++)
    if (!memchr(words[i].p, '.', words[i].len)) names[n++] = i;
  bool female;
  if (n >= 3) female = inflect_ends(words[names[2]].p, words[names[2]].len, "на");
  else if (n == 2) female = inflect_female_name(words[names[1]].p, words[names[1]].len);
  else female = n == 1 && inflect_female_surname(words[names[0]].p, words[names[0]].len);
  size_t used = 0;
  for (int i = 0; i < count && used < buf_sz; i++) {
    char word[192];
    int role = -1;
    for (int k = 0; k < n; k++) if (names[k] == i) role = k;
    if (role == 0) inflect_surname(words[i].p, words[i].len, female, gcase, word, sizeof(word));
    else if (role == 1)
      inflect_word(words[i].p, words[i].len, female ? kFemaleName : kMaleName,
                   female ? sizeof(kFemaleName) / sizeof(kFemaleName[0]) : sizeof(kMaleName) / sizeof(kMaleName[0]), gcase, word, sizeof(word));
    else if (role == 2)
      inflect_word(words[i].p, words[i].len, female ? kFemalePatronymic : kMalePatronymic,
                   female ? sizeof(kFemalePatronymic) / sizeof(kFemalePatronymic[0]) : sizeof(kMalePatronymic) / sizeof(kMalePatronymic[0]), gcase, word, sizeof(word));
    else snprintf(word, sizeof(word), "%.*s", (int)words[i].len, words[i].p);
    used += (size_t)snprintf(buf + used, buf_sz - used, "%s%s", i ? " " : "", word);
  }
  return buf;
}
