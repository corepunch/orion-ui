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

- `lexicon.lua`: authored vocabulary, number words, grammatical roles and aliases.
- `dictionary.lua`: load-time compilation to interned word records and a phrase trie.
- `normalize.lua`: shared Unicode normalization for compilation, input and matching.
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
verbs and noun phrases, qualifiers, remembered queries, stable object pronouns, scoped indirect objects and orphan
completion (`show documents for` → a customer). Ambiguous names produce a numbered result list;
`2` completes that pending open. It is not a full Infocom parser: there is no
world-object scope tree, general indirect-object grammar, `ALL EXCEPT`, `AGAIN`, `OOPS`,
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

## More from Zork: object memory, missing arguments and inference

The reference routines were inspected with `gh` in
[`parser.zil`](https://github.com/corepunch/zilscript/blob/main/infocom/zork1/parser.zil)
(`SYNTAX-CHECK`, `ORPHAN-MERGE`, `GWIM`, `GET-OBJECT`) and
[`verbs.zil`](https://github.com/corepunch/zilscript/blob/main/infocom/zork1/verbs.zil)
(`THIS-IS-IT`). The behavior is reimplemented in Ledger's own Lua; no ZIL code
is embedded.

| Zork idea | Accounting behavior |
|---|---|
| `THIS-IS-IT` / `P-IT-OBJECT` | Remember `{entity, id}` after opening or uniquely identifying a record. `it`/`this` does not mean “the row at the old index.” |
| Accessibility checks | Resolve the ID against current host data before opening. Deleted/unavailable records fail; no fallback to another row. |
| Missing indirect object | `show posted documents for` asks which customer; the reply fills the saved customer slot without losing `posted`. |
| `ORPHAN-MERGE` | Reply with `"Alpha AG"`, `customer "Alpha AG"`, a displayed row number or a pronoun. Ambiguous replies narrow the candidate list. Invalid replies retain the pending question. |
| Typed `GWIM` | Infer an omitted customer from the remembered customer, a unique customer in scope, or the sole eligible customer. Infer an omitted opening target only when unambiguous. |
| Scope restrictions | Ordinals refer to the displayed list; answers to a question stay inside its offered candidates. Explicit new commands can start a different search. |

The accounting equivalent of choosing a suitable tool is choosing a compatible
record for a relationship. Documents have a real `counterparty_id`; commands
use that ID, never a name-based join. This supports `show documents for customer
"Alpha AG"` and `open document for customer "Alpha GmbH"`. An ambiguous document
or customer produces a question. Inference is visible in the result message
(the customer name, or “inferred from context” for an opening).

`this_is_it(entity, row)` is the engine's explicit object-memory operation.
A unique query result becomes the remembered object. A list with multiple or
zero matches preserves it; a resolved customer operand becomes the remembered
object when its document query has no unique result. `open it` may therefore
refer to a customer even after showing several documents. `open first` always
uses the currently displayed list. Both revalidate record availability.

| Real request/dialogue | Result |
|---|---|
| `find customer "Alpha AG"` → `show documents for it` → `only posted` → `open first` | Review that customer's posted documents and open the first displayed record. |
| `zeige gebuchte Belege für` → `"Alpha"` → `den zweiten` | Ask for a customer, narrow matching names, use the second offered customer, retaining the posted filter. |
| `найди клиента "Alpha AG"` → `покажи документы для этого клиента` → `только проведенные` | The same relationship and filter in Russian. |
| `open customer "Alpha"` → `"Missing"` → `2` | Keep the ambiguity question after a failed answer, then open candidate two. |
| `show items` → `open` | Open the only item if the result is unique; otherwise ask. |
| `open customer "Alpha AG"` → `sort customers by debt descending` → `open it` | Reopen the same customer ID despite reordered rows. |
| `show documents for` → `cancel` | Abandon the pending question. `abbrechen` and `отмена` also work. |
| `show documents for` → `show items` | A complete new command replaces the pending question. |

English `for`, German `für/fuer` and Russian `для` introduce the customer slot.
Put document qualifiers before that slot: `show posted documents for customer
"Alpha AG"`. The relationship grammar currently covers documents → counterparty;
it does not claim invoice-specific, due-date, payment allocation or bank
reconciliation semantics. Posting, paying and deleting remain unsupported.

`apps/ledger/tests/assistant_dialogue_test.lua` contains the dialogue tables,
host-failure/deletion/reassignment cases and a synthetic stress ledger with 120
customers and 2,400 documents. Each customer-review cycle filters posted
records, sorts them, opens one, changes scope and verifies that `it` still opens
the same document ID. The native integration test also checks the relationship
against the actual RU/DE demo document foreign keys and posted flags.

## Compiled dictionary, using Lua tables and functions

The dictionary design was checked against `corepunch/zilscript` at `0edc03f`:
[`compiler/forms.lua`](https://github.com/corepunch/zilscript/blob/0edc03f/zilscript/compiler/forms.lua)
lowers vocabulary declarations and synonym forms to Lua;
[`bootstrap.lua`](https://github.com/corepunch/zilscript/blob/0edc03f/zilscript/bootstrap.lua)
implements `learn`, `BUZZ`, `SYNONYM` and `merge_dictionary_word`, building
`cache.words` and preserving grammatical roles. Its packed seven-byte records
serve the Z-machine parser API. Ledger needs the vocabulary model, without
that memory representation.

Here, “compile” means transforming authored Lua tables into indexed Lua tables
once when `parser` is required. There is no bytecode output, binary dictionary,
code generation, executable generation or extra build step. All behavior stays
in Lua functions. Lua's `require` cache retains the loaded parser and its compiled
dictionary; each isolated Ledger Lua state owns its own instance.

```lua
local dictionary = require('dictionary').compile(require('lexicon'))
assert(dictionary.words.show.roles.verbs == 'list')
assert(dictionary.words.zeige.roles.verbs == 'list')
assert(dictionary.words['покажи'].roles.verbs == 'list')
-- Separate surface forms, one canonical command meaning.
```

| Authored input | Compiled representation |
|---|---|
| Aliases for a verb, noun, qualifier or field | One interned record per normalized spelling, with its canonical role values |
| A word used in multiple grammatical roles | A `roles` table retaining every role; the grammar explicitly chooses role precedence |
| Duplicate aliases with the same meaning | The same interned record |
| Conflicting meanings in the same role | A compilation error naming the word and role |
| `at least`, `не меньше`, `mehr als` | Trie paths with terminal word IDs, supporting longest complete phrase matching |
| Pronouns, ordinals, noise, currency units and number words | Roles in the same dictionary, rather than unrelated parser-local maps |

`words` indexes records by normalized spelling; `entries` indexes the same
records by numeric ID. IDs are assigned in sorted order, independent of Lua
source-table iteration order. They identify vocabulary within a compiled
instance, not Ledger records, and are not a persistence contract across lexicon
edits. Exact word lookup uses Lua hash tables; phrase lookup follows only the
input phrase's trie path rather than scanning all aliases. A failed longer
phrase falls back to the last complete match. Unknown words remain errors,
and quoted names bypass vocabulary matching.

The compiler and tokenizer share the same Unicode normalizer. Full word
spellings are retained: there is no six-character truncation or arbitrary
stemming. A normalized collision such as two incompatible meanings for
`трёх`/`трех` is diagnosed. German productive compound numbers remain a small
recognizer function; there is no need to enumerate every possible cardinal
number. Customer names and accounting records remain runtime host data and
are never compiled into the language dictionary.

The dictionary compiler validates role names, alias-list shape, UTF-8,
empty/unreachable aliases, numeric meanings and same-role conflicts.
`apps/ledger/tests/assistant_dictionary_test.lua` audits every authored alias,
canonical meanings across languages, ID reproducibility, multi-role records,
longest-match fallback, quotation boundaries and isolation. It also checks that
100 commands compile the dictionary only once, and that adding 10,000 aliases
does not alter existing matches. Both standalone and native sandbox test
runners execute these checks.

Lua fits this work directly: tables express both the authored vocabulary and
its compiled indexes, and functions express the grammar and productive rules.
This compilation model does not create a reason to move the assistant into C.
The native data access/UI boundary stays in C; a future move of a specific
operation should follow measured need.
