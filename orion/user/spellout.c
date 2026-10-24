// Spelled-out numbers and plural forms, as ICU's spellout rules and CLDR
// plural categories provide them. Each language has its own grammar here;
// languages without rules fall back to digits.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <orion/user/i18n.h>

typedef struct { char *buf; size_t sz, len; } spell_out_t;

static void spell_put(spell_out_t *o, const char *word, const char *sep) {
  if (!word || !*word) return;
  int n = snprintf(o->buf + o->len, o->sz - o->len, "%s%s", o->len && sep ? sep : "", word);
  if (n > 0) o->len = o->len + (size_t)n < o->sz ? o->len + (size_t)n : o->sz - 1;
}

static bool spell_lang(const char *language, const char *code) {
  if (!language) language = ui_get_language();
  size_t n = strlen(code);
  return !strncmp(language, code, n) && (language[n] == '\0' || language[n] == '-' || language[n] == '_');
}

int ui_plural_index(const char *language, long long n) {
  if (n < 0) n = -n;
  if (spell_lang(language, "ru")) {
    if (n % 10 == 1 && n % 100 != 11) return 0;
    if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14)) return 1;
    return 2;
  }
  return n == 1 ? 0 : 1;
}

const char *ui_plural_pick(const char *language, long long n, const char *forms, char *buf, size_t buf_sz) {
  if (!buf || !buf_sz) return "";
  buf[0] = '\0';
  if (!forms) return buf;
  int index = ui_plural_index(language, n);
  const char *s = forms;
  for (int i = 0; i < index; i++) {
    const char *bar = strchr(s, '|');
    if (!bar) break;  // fewer forms than categories: the last one serves
    s = bar + 1;
  }
  const char *end = strchr(s, '|');
  snprintf(buf, buf_sz, "%.*s", (int)(end ? (size_t)(end - s) : strlen(s)), s);
  return buf;
}

// ── Russian ────────────────────────────────────────────────────────────────

static void spell_ru_triad(spell_out_t *o, int n, ui_gender_t gender) {
  static const char *hundreds[] = { "", "сто", "двести", "триста", "четыреста", "пятьсот", "шестьсот", "семьсот", "восемьсот", "девятьсот" };
  static const char *tens[]     = { "", "", "двадцать", "тридцать", "сорок", "пятьдесят", "шестьдесят", "семьдесят", "восемьдесят", "девяносто" };
  static const char *teens[]    = { "десять", "одиннадцать", "двенадцать", "тринадцать", "четырнадцать", "пятнадцать", "шестнадцать", "семнадцать", "восемнадцать", "девятнадцать" };
  static const char *units[]    = { "", "один", "два", "три", "четыре", "пять", "шесть", "семь", "восемь", "девять" };
  spell_put(o, hundreds[n / 100], " ");
  int rest = n % 100;
  if (rest >= 10 && rest < 20) { spell_put(o, teens[rest - 10], " "); return; }
  spell_put(o, tens[rest / 10], " ");
  int u = rest % 10;
  if (u == 1 && gender == UI_GENDER_FEMININE)     spell_put(o, "одна", " ");
  else if (u == 1 && gender == UI_GENDER_NEUTER)  spell_put(o, "одно", " ");
  else if (u == 2 && gender == UI_GENDER_FEMININE) spell_put(o, "две", " ");
  else spell_put(o, units[u], " ");
}

static void spell_ru(spell_out_t *o, long long n, ui_gender_t gender) {
  static const struct { long long scale; const char *forms; ui_gender_t gender; } scales[] = {
    { 1000000000LL, "миллиард|миллиарда|миллиардов", UI_GENDER_MASCULINE },
    { 1000000LL,    "миллион|миллиона|миллионов",    UI_GENDER_MASCULINE },
    { 1000LL,       "тысяча|тысячи|тысяч",           UI_GENDER_FEMININE },
  };
  if (n == 0) { spell_put(o, "ноль", " "); return; }
  char form[64];
  for (size_t i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
    long long count = n / scales[i].scale;
    if (!count) continue;
    spell_ru(o, count, scales[i].gender);
    spell_put(o, ui_plural_pick("ru", count, scales[i].forms, form, sizeof(form)), " ");
    n %= scales[i].scale;
  }
  if (n) spell_ru_triad(o, (int)n, gender);
}

// ── German: numbers below a million are one word ───────────────────────────

static void spell_de_below_million(spell_out_t *o, int n) {
  static const char *units[] = { "", "ein", "zwei", "drei", "vier", "fünf", "sechs", "sieben", "acht", "neun",
                                 "zehn", "elf", "zwölf", "dreizehn", "vierzehn", "fünfzehn", "sechzehn", "siebzehn", "achtzehn", "neunzehn" };
  static const char *tens[]  = { "", "", "zwanzig", "dreißig", "vierzig", "fünfzig", "sechzig", "siebzig", "achtzig", "neunzig" };
  if (n >= 1000) { spell_de_below_million(o, n / 1000); spell_put(o, "tausend", ""); n %= 1000; }
  if (n >= 100)  { spell_put(o, units[n / 100], ""); spell_put(o, "hundert", ""); n %= 100; }
  if (n >= 20 && n % 10) { spell_put(o, units[n % 10], ""); spell_put(o, "und", ""); }
  if (n >= 20) spell_put(o, tens[n / 10], "");
  else spell_put(o, units[n], "");
}

static void spell_de(spell_out_t *o, long long n, ui_gender_t gender) {
  if (n == 0) { spell_put(o, "null", ""); return; }
  if (n == 1) { spell_put(o, gender == UI_GENDER_FEMININE ? "eine" : "ein", ""); return; }  // before a noun
  static const struct { long long scale; const char *one, *many; } scales[] = {
    { 1000000000LL, "eine Milliarde", "Milliarden" }, { 1000000LL, "eine Million", "Millionen" },
  };
  for (size_t i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
    long long count = n / scales[i].scale;
    if (!count) continue;
    if (count == 1) spell_put(o, scales[i].one, " ");
    else {
      char part[256];
      spell_out_t p = { part, sizeof(part), 0 };
      spell_de_below_million(&p, (int)count);
      spell_put(o, part, " ");
      spell_put(o, scales[i].many, " ");
    }
    n %= scales[i].scale;
  }
  if (!n) return;
  char part[256];
  spell_out_t p = { part, sizeof(part), 0 };
  spell_de_below_million(&p, (int)n);
  spell_put(o, part, " ");
  if (n % 100 == 1) spell_put(o, "s", "");  // "einhunderteins"
}

// ── English ────────────────────────────────────────────────────────────────

static void spell_en_triad(spell_out_t *o, int n) {
  static const char *units[] = { "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                 "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };
  static const char *tens[]  = { "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };
  if (n >= 100) { spell_put(o, units[n / 100], " "); spell_put(o, "hundred", " "); n %= 100; }
  if (n >= 20) { spell_put(o, tens[n / 10], " "); if (n % 10) { spell_put(o, "-", ""); spell_put(o, units[n % 10], ""); } }
  else spell_put(o, units[n], " ");
}

static void spell_en(spell_out_t *o, long long n) {
  static const struct { long long scale; const char *name; } scales[] = {
    { 1000000000LL, "billion" }, { 1000000LL, "million" }, { 1000LL, "thousand" },
  };
  if (n == 0) { spell_put(o, "zero", " "); return; }
  for (size_t i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
    long long count = n / scales[i].scale;
    if (!count) continue;
    spell_en(o, count);
    spell_put(o, scales[i].name, " ");
    n %= scales[i].scale;
  }
  if (n) spell_en_triad(o, (int)n);
}

const char *ui_spell_number(const char *language, long long n, ui_gender_t gender, char *buf, size_t buf_sz) {
  if (!buf || !buf_sz) return "";
  buf[0] = '\0';
  spell_out_t o = { buf, buf_sz, 0 };
  if (n < 0) {
    spell_put(&o, spell_lang(language, "ru") ? "минус" : "minus", " ");
    spell_put(&o, " ", "");
    o.buf += o.len; o.sz -= o.len; o.len = 0;  // the number follows as a fresh phrase
    n = -n;
  }
  if (spell_lang(language, "ru"))      spell_ru(&o, n, gender);
  else if (spell_lang(language, "de")) spell_de(&o, n, gender);
  else if (spell_lang(language, "en")) spell_en(&o, n);
  else snprintf(buf + o.len, buf_sz - o.len, "%lld", n);
  return buf;
}

// Capitalizes the first letter: ASCII and Cyrillic (UTF-8).
static void spell_capitalize(char *s) {
  unsigned char *u = (unsigned char *)s;
  if (u[0] >= 'a' && u[0] <= 'z') u[0] = (unsigned char)(u[0] - 32);
  else if (u[0] == 0xD0 && u[1] >= 0xB0 && u[1] <= 0xBF) u[1] = (unsigned char)(u[1] - 0x20);
  else if (u[0] == 0xD1 && u[1] >= 0x80 && u[1] <= 0x8F) { u[0] = 0xD0; u[1] = (unsigned char)(u[1] + 0x20); }
}

const char *ui_spell_amount(const char *language, double amount, const ui_money_words_t *words, char *buf, size_t buf_sz) {
  if (!buf || !buf_sz) return "";
  long long cents_total = llround(fabs(amount) * 100.0);
  long long units = cents_total / 100, cents = cents_total % 100;
  char number[512], unit[64], cent[64];
  ui_spell_number(language, amount < 0 ? -units : units, words ? words->unit_gender : UI_GENDER_MASCULINE, number, sizeof(number));
  ui_plural_pick(language, units, words ? words->units : NULL, unit, sizeof(unit));
  ui_plural_pick(language, cents, words ? words->cents : NULL, cent, sizeof(cent));
  snprintf(buf, buf_sz, "%s%s%s %02lld%s%s", number, unit[0] ? " " : "", unit, cents, cent[0] ? " " : "", cent);
  spell_capitalize(buf);
  return buf;
}
