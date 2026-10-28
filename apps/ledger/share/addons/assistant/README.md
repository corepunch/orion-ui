# Ledger local assistant — prototype

Clippy appears automatically at the bottom-right of Ledger's application workspace with a greeting in
a speech bubble. Type a command and press **Enter** or **Send**. Click Clippy
or **Close** to fold the bubble away; click him again to continue. The
conversation, results and unfinished input are retained while it is collapsed.
Double-click Clippy to open the conversation and focus its input, even when the bubble is already open.
Closing the assistant window hides the character too, preserving the conversation;
**Help → Show Office Assistant** or
**Ctrl+Shift+A** brings him back and focuses the input. Transparent space beside
the character passes clicks through to Ledger.

The greeting is the Office 97 question balloon: **What would you like to do?**,
the command field, and **Send / Examples / Close**. The yellow bubble sizes itself
to its content. Below the field is a command palette: the answer's message,
then rows in groups.

| Group | Rows | Choosing a row |
|---|---|---|
| Counterparties / Documents / Items | up to six records with amounts, then a count of further matches | opens the record, or answers the assistant's question, as typing its number does |
| Advances received | when no one owes the company: who paid in advance | opens the counterparty |
| Commands | the commands that belong with the result, or every menu command whose caption the request names ("нов док" finds New Document) | runs the command; the balloon steps aside |
| Suggestions | the books' own checks, the first three of the Home page's **Needs attention** list | settles the check as the Home page does: posts the drafts, closes the month, opens or creates the document, reposts the books |
| Needs attention / Money / Settlements / Movements | the answer to a question about the books (below) | settles the check, opens the counterparty or the document |
| Examples | **Examples** | runs the example |

Up and Down move through the rows while typing stays in the field; Enter opens
the selected row until something new is typed, and then Enter sends the text.
**Help → Find a Command** (Ctrl+Shift+K) opens the balloon with the field focused.
Saving, posting or deleting a document refreshes the suggestions; when there
is a new one while the balloon is closed, Clippy asks for attention. Narrow
the request or use **open first** to open a result by typing; the full result
context remains available.

## Questions about the books

The command grammar wants every word to be known. A request it does not
take is read again for a topic word, the way Office 97's Answer Wizard took
a question in plain words, so the accountant can ask what they would ask a
colleague or a 1C programmer:

| Question | Answer |
|---|---|
| проверь учет · что не так · требует внимания · check the books | every check of the books (Home's **Needs attention**), each settled by choosing it |
| почему красное сальдо · где минус · отрицательные остатки | the days cash or stock went below zero, with the document that took it there |
| закрыт ли месяц · закрытие месяца | a finished month not closed, or a closing that later documents made stale |
| сколько денег · деньги в кассе · how much money | money accounts as of the period's end and their total |
| кому мы должны · кредиторы · who do we owe | counterparties the company owes, largest first |
| кто нам должен · дебиторы | counterparties who owe the company |
| налоги к уплате · НДС · taxes | taxes and wages payable, input tax to deduct |
| акт сверки с Альфой · сверка с "Бета" | the counterparty's movements on the receivables and payables groups and who owes whom |

Topic words are `<topic>` entries in `vocabulary/*.xml`, inflected like every
other word; `topics.lua` finds them and the counterparty a request names
("альфой" finds ООО "Торговый дом Альфа"). The checks themselves are
Ledger's (`home.c`, `ib.home_rows`); the balloon and the Home page show the
same rows, and `ledger.settle(id)` does what a row offers.

The parts have one job each. `engine.lua` parses and answers, as before.
`commands.lua` searches the menu tree that Ledger exposes as
`ledger.commands()`; `ledger.command(name)` runs a command after the
add-on's action returns. `tips.lua` turns the books' checks into suggestions,
`topics.lua` reads questions about the books, and
`assistant.lua` turns an answer into palette rows. The balloon is plain Orion:
a TextBox with `list=`, and a TableView with `group-field`, `fit-rows` and
`action=`.

The layout follows Office 97's built-in question balloon, documented in
[Microsoft KB 173766 (archived)](https://www.betaarchive.com/wiki/index.php?title=Microsoft_KB_Archive/173766)
and the [Office Assistant Help API](https://learn.microsoft.com/en-us/dotnet/api/microsoft.office.core.assistant.help).
The tail proportions follow the [clippy.js 10×16 tail atlas](https://github.com/clippyjs/clippy.js/blob/master/build/clippy.css):
one upright edge and one diagonal edge, with a continuous outline where it joins
the balloon. Orion renders this geometry as a single vector silhouette.
The tip points about 30% across the character frame, just left of its head,
matching the offset visible in the
[Word 97 Letter Wizard screenshot](https://winworldpc.com/screenshot/c390c3ab-6334-2472-11c3-a4e284a2c3a5)
and the [Word 97 welcome screenshot](https://s3.amazonaws.com/s3.timetoast.com/public/uploads/photos/10436249/97.png).
The proportion follows the character frame when the layout changes.

System dictation works in the command field. Clicking Clippy opens the bubble;
it does not start microphone recording. Offline dictation availability depends
on the operating system.

The parser and execution run locally, with no LLM, network or API key.
`build/bin/ledger --assistant --lang ru` also focuses the input on startup.
`--no-assistant` starts without the companion; the menu can still show him.

Try a contextual dialogue:

1. `покажи должников`
2. `больше пятидесяти тысяч`
3. `отсортируй по долгу по убыванию`
4. `открой первого`

Other commands: `покажи клиентов`, `найди клиента "Альфа"`, `покажи документы`,
`только проведенные`, `покажи товары`, `помощь`, `сброс`. English, German and Russian commands share one typed grammar. For example: `show customers`, `find customer
"Intertrade"`, `open first`. The interface and clarifications use the selected UI language (`--lang en`, `de`, or `ru`); command vocabulary is accepted in all three languages.
Unknown words reject the entire command; user input is never evaluated as Lua.

## The Office 97 balloon model

Office 97 did not draw balloons from dialog templates. Each balloon was a
record that Office laid out and sized when it was shown, so the same window
showed tips, questions, alerts and search results. VBA programs built one with
`Assistant.NewBalloon`, set its properties and called `Show`. The surviving
Microsoft references describe the Office XP–2010 versions of this object model,
which carry the Office 97 properties forward
([Balloon object](https://learn.microsoft.com/en-us/dotnet/api/microsoft.office.core.balloon),
[Creating and Modifying Balloons](https://learn.microsoft.com/en-us/previous-versions/office/developer/office-2003/aa170979(v=office.11))):

| Balloon property | Office 97 | Ledger |
|---|---|---|
| `Heading` | Title line | `heading` Label, “What would you like to do?” |
| `Text` | Body text | `summary` Label: the engine's reply |
| `Labels(1..5)`, `BalloonType` | Up to five labels as blue-bullet buttons, bullets or numbers; a label appears when it has text | `choice1`…`choice5`, `BUTTON_COMMANDLINK` with `LABEL_COLLAPSE_EMPTY` |
| `Show` return value | Number of the label clicked, or the button | The `choose` action receives the clicked control's name |
| `Checkboxes(1..5)` | Up to five check boxes, read after closing | Not used |
| `Button` | A button set: OK, Yes/No, Back/Next, Search/Close, Tips/Options/Close… | Fixed Send / Examples / Close row |
| `Icon`, `Mode`, `Callback` | Alert or tip icon; modal, auto-down or modeless with a callback | Modeless; no icon |
| `{cf n}`, `{ul}`, `{bmp …}`, `{wmf …}` in text | Inline colour, underline and pictures | Not supported; text is plain |

The built-in help balloon also had a question field and **Search**, which
the VBA object model did not expose. Ledger's balloon combines this field with
the label list. The `.orion` form declares the fixed slots, as Office did with
its five labels. Lua's `self.balloon = {text, labels, more, examples}` is the
record that `data()` turns into control values. There is no app-specific
drawing or hit-testing.

### Character files and reverse engineering

No Office Assistant source code is publicly available. The public work is on
the character file formats; the details are in
[Office Assistant character formats](../../../docs/office-assistant-formats.md):

- Office 97 assistants are Microsoft Actor `.ACT` files (descended from
  Microsoft Bob). The reader in
  [agentviewer `actor.rs`](https://github.com/andrewfader/agentviewer/blob/main/crates/acs/src/actor.rs)
  finds vector metafile frames, sounds and animation sequences, but no balloon
  data. In Office 97, the balloon belonged entirely to Office.
- Office 2000 and later use Microsoft Agent `.ACS` files. The character header
  has a `BALLOONINFO` block: number of lines, characters per line, text,
  fill and border colours, and the font
  ([agentviewer `types.rs`](https://github.com/andrewfader/agentviewer/blob/main/crates/acs/src/types.rs),
  [MSAgentUtils](https://github.com/tkfoss/MSAgentUtils)). The bundled
  Clippit `CLIPPIT.ACS` declares 2 lines × 28 characters, black text on
  `#FFFFE1` with a black border, and MS Sans Serif at 13 px. The colour is
  `#FFFFE1` when read as a `COLORREF`, the Windows tooltip yellow. Agent's own
  balloon ([IAgentBalloon](https://learn.microsoft.com/en-us/windows/win32/lwef/iagentballoon))
  only shows spoken text; Office's interactive balloon stayed Office's.
- [clippy.js](https://github.com/clippyjs/clippy.js) and
  [Double Agent](https://doubleagent.sourceforge.net/) reimplement the character
  and a text-only speech balloon. Lebeau Software's MSAgent Decompiler covers
  Agent 2.0 `.ACS` files only, not Office 97 `.ACT`.

## Implementation

- `vocabulary/en.xml`, `de.xml`, `ru.xml`: command words keyed by English source words, inflection tables, number stems, spelling substitutions and character maps, one language per file.
- `vocabulary.lua`: generated data packaged by `make share`, loaded directly by the parser and language helpers.
- `dictionary.lua`: load-time compilation to interned word records and a phrase trie.
- `normalize.lua`: shared Unicode normalization for compilation, input and matching.
- `german_numbers.lua`: number composition using stems and particles from the German XML.
- `parser.lua`: UTF-8 normalization, number phrases, grammar and contextual refinements.
- `engine.lua`: typed commands, filters, sorting, disambiguation, host callbacks
  and `choose(index)` for a clicked balloon label.
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
| Noun forms → entity | `customer/customers`, generated from `customer` | `Kunde/Kunden`, from `Kunde` | `клиент/клиента/клиентов`, from `клиент` |
| Posted adjective → boolean | `posted/draft` | `gebucht/gebuchte/gebuchten`, corresponding `ungebucht` forms | `проведенные/проведенных`, corresponding `непроведенные` forms |
| Comparison → `ge` | `at least` | `mindestens` | `не меньше/не менее` |
| Number composition | `one hundred and twenty five` → 125 | `einhundertfünfundzwanzig` → 125 | `ста двадцати пяти` → 125 |
| Thousands | `fifty thousand` → 50000 | `fünfzig tausend/fünfzigtausend` → 50000 | `пятьдесят тысяч/пятидесяти тысяч` → 50000 |
| ASCII spelling | unchanged | every form also as `ae/oe/ue/ss`: `fuer`, `oeffne`; `fuenf/zwoelf/dreissig` → numbers | not transliterated |

Inflected forms come from the paradigm tables in each vocabulary file, applied
to known headwords when `make share` packages the vocabulary (see below). Input
is matched against those forms exactly: there is no suffix stripping, so
unknown words are still rejected. Irregular forms, such as Russian number words
after comparisons (`пятидесяти`), are listed with the headword. German adds a
bounded cardinal-number grammar in
`german_numbers.lua`: unit + `und` + tens; unit + `hundert` + remainder;
under-thousand + `tausend` + remainder. The number stems, particles, prefix
restrictions and ASCII spelling substitutions are declared in `vocabulary/de.xml`;
Lua contains the composition rules. Language-specific character mappings also
come from the vocabulary XML. Compound forms cover 0–999999;
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
The Russian `<morphology>` uses the layout of its `dictionary/paradigms.txt` (cut
count, one ending per slot, `=` and `-`); the tables are written for this
vocabulary. No translator code or dictionaries are copied or loaded at runtime.

Run from the repository root (Lua 5.4+):

```sh
make share
lua apps/ledger/tests/assistant_test.lua
python3 apps/ledger/tests/assistant_vocabulary_test.py
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

Command vocabulary lives in `vocabulary/*.xml`, separate from the `locale/*.xml`
catalogs that translate replies and UI text. It is collected the way Zork's
compiler collected its vocabulary: nobody keeps a list of every word or form.
English is the source language, as in the string catalogs. An entry with
`means` defines a source word and its meaning, like Zork's `SYNTAX`; any file
translates it with an entry naming the same `source`:

```xml
<vocabulary language="en">
  <verb source="show" means="list" synonyms="list"/>
  <noun source="customer" means="counterparties" paradigm="noun" synonyms="client"/>
  <comparison source="greater than" means="gt" synonyms="over"/>
  <comparison source="greater than">more than</comparison>
</vocabulary>

<vocabulary language="ru">
  <morphology>
    <class name="noun" slots="nom gen dat acc inst prep  pl.nom pl.gen pl.dat pl.acc pl.inst pl.prep"/>
    <paradigm name="noun-m-animate" class="noun" cut="0">= а у а ом е  ы ов ам ов ами ах</paradigm>
  </morphology>
  <verb source="show" forms="показать" synonyms="выведи список">покажи</verb>
  <noun source="customer" paradigm="noun-m-animate" synonyms="контрагент">клиент</noun>
  <number value="50" forms="пятидесяти">пятьдесят</number>
</vocabulary>
```

| Entry | Meaning | Keyed by |
|---|---|---|
| `verb`, `noun`, `adjective`, `field`, `comparison`, `relation`, `direction` | What the source word means (`means`, once per source) | `source` |
| `ordinal`, `number`, `scale`, `multiplier` | The number it stands for | `value` |
| `pronoun`, `noise`, `unit`, `conjunction` | The role alone | — |

The text is the headword; a definition without text uses its source word.
`synonyms` lists more headwords separated by spaces, like `class=` in HTML. A
phrase (`more than`) or a word with another paradigm (`покупатель`) gets its
own entry. `paradigm` expands each headword through the file's `<morphology>`:
cut `cut` letters, then add one ending per slot of the paradigm's class, where
`=` is the headword, `0` the bare stem and `-` no form. `forms` lists irregular
forms (`имя` → `имени`). A `<spelling from="ü" to="ue"/>` adds a variant of
every form. Animate and inanimate nouns differ only in their accusative, which
is always a nominative or genitive form, so both recognize the same words. The
slot names say which case each form is; the parser does not check case
agreement, so a command with a mistyped case still works.

Add words, inflections or a language in these XML files. `make share` discovers
all of them and packages their merged data as `vocabulary.lua` in the build's
assistant resource directory, using `tools/generate_assistant_vocabulary.py`.
The generated module stays out of the source tree. The sandbox loads ordinary
Lua data without file access; all command languages remain available regardless
of the selected UI language. A word whose source is not defined, a source
defined twice, an unknown paradigm, a paradigm whose endings do not match its
class, or a word with two meanings in one role stops the build with the file
and the reason. `assistant_vocabulary_test.py` fails when a language does not
translate every defined source word.

The German `<compound-numbers conjunction="und" multiplier="hundert" scale="tausend">`
section names its particles and contains ordered `<replace source="ue" target="ü"/>`
spelling substitutions. Its multiplier and scale refer to aliases in the same
file's `multipliers` and `scales` roles. Number entries marked `compound="ones"`,
`compound="small"` or `compound="tens"` provide the recognizer's stem tables;
`prefix="false"` keeps a standalone form such as `eins` out of compound prefixes.
`<normalization><map source="ÄÖÜẞ" target="äöüß"/></normalization>` declares
character-by-character substitutions shared by dictionary, tokenizer and record
matching. These language data stay separate from grammar roles in the generated
module: the parser loads `.lexicon`, while the recognizer and normalizer use
`.languages` and `.normalization`.

At runtime, “compile” means transforming that vocabulary data into indexed Lua
tables once when `parser` is required. There is no bytecode output or binary
dictionary. All behavior stays in Lua functions. Lua's `require` cache retains
the loaded parser and its compiled dictionary; each isolated Ledger Lua state
owns its own instance.

```lua
local dictionary = require('dictionary').compile(require('vocabulary').lexicon)
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
records by numeric ID. IDs are assigned in sorted order, independent of XML
file order or Lua source-table iteration order. They identify vocabulary within
a compiled instance, not Ledger records, and are not a persistence contract across lexicon
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
runners execute these checks. `assistant_vocabulary_test.py` checks XML
validation, preservation of every authored word, paradigm expansion, translation
coverage and adding a language without changing the grammar or loader. It
changes XML stems, particles, spelling and character maps to verify that the
Lua rules use that data, and tests reference parsing without an English `open`.

XML expresses the authored vocabulary; Lua tables hold its loaded data and
compiled indexes, and functions express the grammar and productive rules.
This compilation model does not create a reason to move the assistant into C.
The native data access/UI boundary stays in C; a future move of a specific
operation should follow measured need.
