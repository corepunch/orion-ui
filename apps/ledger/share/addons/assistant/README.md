# Ledger local assistant — prototype

Open **File → Assistant…** or **Ctrl+Shift+A**. The modeless window stays alongside
Ledger. Click Clippy to focus the command field, type or use system dictation,
then press **Выполнить**. Clicking Clippy does not start microphone recording;
offline dictation availability depends on the operating system.

The parser and execution run locally, with no LLM, network or API key.
`build/bin/ledger --assistant --lang ru` opens the assistant on startup.

Try a contextual dialogue:

1. `покажи должников`
2. `больше пятидесяти тысяч`
3. `отсортируй по долгу по убыванию`
4. `открой первого`

Other commands: `покажи клиентов`, `найди клиента "Альфа"`, `покажи документы`,
`только проведенные`, `покажи товары`, `помощь`, `сброс`. Basic English and German
vocabulary is also included. For example: `show customers`, `find customer
"Intertrade"`, `open first`. The prototype interface and clarifications are Russian.
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
