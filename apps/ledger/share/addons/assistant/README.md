# Ledger local assistant — prototype

Open **File → Assistant…** or **Ctrl+Shift+A**. The modeless window stays alongside
Ledger. Click Clippy to focus the command field, type or use system dictation,
then press **Run**. Clicking Clippy does not start microphone recording;
offline dictation availability depends on the operating system.

The parser and execution run locally, with no LLM, network or API key.
`build/bin/ledger --assistant --lang ru` opens the assistant on startup.

Try a contextual dialogue:

1. `покажи должников`
2. `больше пятидесяти тысяч`
3. `отсортируй по долгу по убыванию`
4. `открой первого`

Other commands: `покажи клиентов`, `найди клиента "Альфа"`, `покажи документы`,
`только проведенные`, `покажи товары`, `помощь`, `сброс`. English, German and Russian commands share one typed grammar. For example: `show customers`, `find customer
"Intertrade"`, `open first`. The interface and clarifications use the selected UI language (`--lang en`, `de`, or `ru`); command vocabulary is accepted in all three languages.
Unknown words reject the entire command; user input is never evaluated as Lua.

## Implementation

- `lexicon.lua`: verbs, nouns, adjectives, fields and synonyms.
- `parser.lua`: UTF-8 normalization, number phrases, grammar and contextual refinements.
- `engine.lua`: typed commands, filters, sorting, disambiguation and host callbacks.
- `assistant.lua`: real Ledger models, receivable balances, native record opening
  and selection of character animations.
- `animations.lua`: generated metadata for the bundled Clippy atlases.

This is a new Lua parser inspired by the verb/noun/qualifier/orphan architecture
in <https://github.com/corepunch/zilscript/tree/main/infocom/zork1>. It does not
embed Infocom code or depend on the ZIL VM.

Customer debt means the positive net receivable balance on the sales document
type's configured debit account and its chart group, through the report period's closing date when set. The account is resolved at runtime; no country-specific account map is embedded. Amounts are in
the accounting currency. This is not invoice ageing; foreign currency amounts
are rejected rather than implicitly converted. Overdue filtering, bank
reconciliation and automatic data writes are outside this prototype. Opening
an existing record uses Ledger's normal form, where saving remains explicit.

## Prototype character

Clippy is the bundled temporary character. The pack contains 43 clips and 1,233
timeline poses, with the original 124×93 frame canvas. The renderer scales the
original images on high-DPI displays. No custom character artwork is included.
Each clip uses one atlas with at most eight columns; only the active atlas is
loaded. Timing uses an average interval per clip, with a 16 ms minimum. Original
branching and individual frame delays are not reproduced yet.

Artwork attribution and source are in `assets/clippy/NOTICE.md`. To rebuild from
an existing local extraction (requires Pillow):

```sh
python tools/import_assistant_clippy.py /path/to/codex-clippy
make share
lua apps/ledger/tests/assistant_test.lua
```

## Host API

Lua `data()` can return `sprites = {controlName = {path, columns, rows, count,
interval, loop, revision}}` for a `SpriteButton`. Paths are relative to the add-on.
The host validates grids and owns bitmap/frame-timer lifetimes. Increment
`revision` to replay a clip. `self.app.focus(name)` focuses an existing control.

Ledger's trusted add-on module adds `ledger.open(table, id)`, limited to existing
counterparties, items and documents. The assistant queries and opens records;
it does not post, delete or modify accounting data through a command.

## Grammar review and executable examples

This is Zork-inspired, with a deliberately small accounting vocabulary. It has
longest-phrase lexical matching (`не меньше`, `greater than`, `mehr als`), typed
verbs and noun phrases, qualifiers, remembered queries, row pronouns and orphan
completion (`open` → `first`). Ambiguous names produce a numbered result list;
`2` completes that pending open. It is not a full Infocom parser: there is no
world-object scope tree, indirect object grammar, `ALL EXCEPT`, `AGAIN`, `OOPS`,
command chaining, or general natural-language understanding. Unknown words
reject the complete input. Names must be quoted. Only listing, filtering,
sorting and opening existing records are supported.

The table in `apps/ledger/tests/assistant_examples.lua` is executable: every
English/German/Russian row must produce an identical AST, including filter
operators and amounts. Other tables cover numbers, multi-turn dialogues,
ambiguous matches, invalid inputs and unchanged query/results after rejection.

| Intent | English | German | Russian |
|---|---|---|---|
| List | `show debtors` | `zeige Schuldner` | `покажи должников` |
| Refine current list | `over fifty thousand` | `über fünfzigtausend` | `больше пятидесяти тысяч` |
| Sort current list | `sort by debt descending` | `sortiere nach Schulden absteigend` | `отсортируй по долгу по убыванию` |
| Open result | `open first` | `öffne den ersten` | `открой первого` |
| Repeat selected opening | `open it` | `öffne ihn` | `открой его` |
| Find name | `find customer "Alpha"` | `suche Kunde "Alpha"` | `найди клиента "Alpha"` |
| Posted documents | `show only posted documents` | `zeige nur gebuchte Belege` | `покажи только проведенные документы` |
| Refine documents | `only posted` | `nur gebuchte` | `только проведенные` |

## Languages and transformations

Input language is separate from UI language. All supported aliases map directly
to language-neutral operations such as `list`, `counterparties`, `debt`, `ge`.
Switching the accounting configuration does not switch the command language.
Output uses English source sentences through `orion.tr`, with whole-sentence
German/Russian XML catalogs in `locale/`; user data stays untouched. The portable
Lua tests fall back to English when the Orion host is absent.

| Layer | English | German | Russian |
|---|---|---|---|
| Case folding | `SHOW` → `show` | `ÖFFNE` → `öffne`, `ẞ` → `ß` | `ПОКАЖИ` → `покажи`, `ё` → `е` |
| Noun forms → entity | `customer/customers` | `Kunde/Kunden` | `клиент/клиента/клиентов` |
| Posted adjective → boolean | `posted/draft` | `gebucht/gebuchte/gebuchten`, corresponding `ungebucht` forms | `проведенные/проведенных`, corresponding `непроведенные` forms |
| Comparison → `ge` | `at least` | `mindestens` | `не меньше/не менее` |
| Number composition | `one hundred and twenty five` → 125 | `einhundertfünfundzwanzig` → 125 | `ста двадцати пяти` → 125 |
| Thousands | `fifty thousand` → 50000 | `fünfzig tausend/fünfzigtausend` → 50000 | `пятьдесят тысяч/пятидесяти тысяч` → 50000 |
| ASCII spelling | unchanged | `fuenf/zwoelf/dreissig` → recognized numeric forms | not transliterated |

Russian uses explicit surface-form tables, including genitives after comparisons;
there is no broad suffix stripping. German uses the same alias-table approach
for commands and inflections, plus a bounded cardinal-number grammar in
`german_numbers.lua`: unit + `und` + tens; unit + `hundert` + remainder;
under-thousand + `tausend` + remainder. Compound forms cover 0–999999;
separate `Million/Millionen` scale tokens support larger values. This is not a
German compound-noun splitter or a general morphological analyzer.

Digits accept decimal dot or comma and space/NBSP/narrow-NBSP grouping:
`50000`, `50 000`, `50,5 тысяч`. Since commands can mix languages, `50.000`
and `50,000` are rejected as ambiguous; use `50000` or `50 000` instead.
Explicit currency units remain unsupported. Broad date expressions, arbitrary
inflections, German polite verb-final sentences and full number-language
validation remain outside this grammar.

[`en-ru-translator`](https://github.com/corepunch/en-ru-translator) was reviewed
as a reference, especially `ARCHITECTURE.md` and `core/grammar_values.lua`.
Its staged lexical/grammar processing and explicit case/imperative features are
useful design references. Its English-to-Russian generation tables are not a
Russian command analyzer. Do not translate commands through it: map recognized
surface forms directly to the AST, preserving names and unknown-word rejection.
No translator code or dictionaries are copied or loaded at runtime.

Run from the repository root (Lua 5.3+; `texlua` also works):

```sh
lua apps/ledger/tests/assistant_test.lua
python3 apps/ledger/tests/assistant_locale_test.py
make build/bin/test_ledger_assistant_test
build/bin/test_ledger_assistant_test
```

The existing C integration suite runs the Lua example tables as well as real
RU/DE configuration queries. The Python check verifies catalog coverage,
placeholder preservation, translated parser/engine replies and untouched names.
