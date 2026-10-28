# Ledger compliance record: Russia 2026 and Germany 2026

**Not production-complete.** See [the readiness audit](docs/compliance-readiness.md)
for correctness blockers, outstanding approximations and acceptance evidence.
The four new English-language configurations are development profiles; their
coverage is documented separately in their `COVERAGE.md` files.

This file is the compliance record of the `ru-2026` and `de-2026`
configurations: which rules they implement, where each figure comes from,
what is simplified, and how to bring them up to date when the authorities
change something. The `ru-2003` and `de-2003` configurations are historical
reference material and are not maintained for compliance.

All figures below were verified against the sources listed in October 2026.

## How rules are represented

The platform (`ledger.orion`, `*.c`) carries no national figure. Everything
that changes with the law is a row in `share/configurations/<code>/configuration.xml`:

| Rule | Where | How it is kept current |
|---|---|---|
| Chart of accounts | `accounts` | Add accounts; never renumber ids (documents refer to them) |
| Tax rates | `vat_rates` | Add a row for a new rate and keep the old one for documents of earlier years (`20% (до 2026)`). A rate may carry automatic accounts (`revenue_id`, `output_tax_id`, `expense_id`, `input_tax_id`) that document types with `auto_accounts="1"` use per line |
| Payroll taxes and contributions | `payroll_taxes` | **Effective-dated rows**: `valid_from` / `valid_to`. A document applies the rows in force on its date. A new year closes the old row with `valid_to` and adds a new one; reposting 2025 documents still uses 2025 figures |
| Progressive scales | `tax_scales` | Zones of a tax (`tax_id`): `base_from`, marginal `rate`, `fixed` tax at the zone start, `curve` for quadratic zones. Russian brackets have `curve="0"`; the German § 32a formula uses `curve` |
| Caps and thresholds | `payroll_taxes.cap_month`, `cap_year`, `rate_above`, `allowance_limit` | Beitragsbemessungsgrenzen (monthly), единая предельная база (annual), the rate above the cap, the income limit of the child deduction |
| Depreciation start | `closing_rules.start` | `next` (ФСБУ 6/2020 as typical 1C configurations apply it) or `same` (§ 7 Abs. 1 EStG, monatsgenau) |
| Print and report forms | `print_forms`, `print/*.html`, `print/*.csv` | Templates filled by `print.c`; `basis` names the act the layout follows |
| Legal basis | `regulations` | One row per act: area, rule, reference, in force from, where to re-check. Shown on the Configuration page, *Legal Basis* tab |
| Rules of another shape | `rules.lua`, `plugins/*.lua` | Lua functions named by `script=` on document types, payroll taxes and closing rules, and `ledger.on("post", fn)` subscriptions; see README, *Scripting rules and plugins*. A new levy, a special tariff or a document with its own posting is a script, not a C change |

`apps/ledger/tools/check_configuration.py` validates a configuration without
building Orion; `make test` runs `ledger_test.c`, which posts the demo
infobases of all four configurations and asserts the 2026 figures.

## Russia 2026 (`ru-2026`)

| Area | Rule implemented | Figures | Legal basis |
|---|---|---|---|
| Accounts | Chart of accounts with the subaccounts of 1C:Бухгалтерия 8.3 (19.04, 44.01, 68.90 ЕНС, 69.09, 69.11, 90.07/08/09, 99.01/02, МЦ.04) | 83 accounts | Приказ Минфина России от 31.10.2000 № 94н |
| VAT | Basic rate 22 % from 1 January 2026; 10 % and 0 % unchanged; 20 % kept for documents dated before 2026; 5 % / 7 % special rates of УСН payers available for items | 22, 10, 0, 20, 5, 7 | П. 3 ст. 164 НК РФ в ред. Федерального закона от 28.11.2025 № 425-ФЗ; ФНС, письмо от 29.12.2025 № СД-4-3/11802 (ставка по дате реализации) |
| VAT deduction | Input VAT deducted when goods are booked and an invoice is held (no payment condition since 2006) | — | Ст. 171, 172 НК РФ |
| VAT books and return | Sales and purchase books one row per document and rate; summary per rate for раздел 3 of the return | — | Постановление Правительства РФ от 26.12.2011 № 1137 (прил. 4, 5); приказ ФНС от 29.10.2014 № ММВ-7-3/558@ |
| Invoice forms | Счет-фактура with lines 1–8 and columns 1–14 (incl. 5а «документ об отгрузке» and traceability columns 12–14); УПД status 1 | — | Постановление № 1137 (ред. 2024); письмо ФНС от 21.10.2013 № ММВ-20-3/96@ |
| Personal income tax | Progressive scale on the year's income so far (cumulative), rounded to roubles; standard child deduction from the employee's `allowance` until the year's income passes the limit | 13 % to 2.4 M, 15 % to 5 M, 18 % to 20 M, 20 % to 50 M, 22 % above; limit 450 000 | Ст. 224, 218, 226 НК РФ (176-ФЗ); п. 6 ст. 52 НК РФ |
| Insurance contributions | Unified tariff on the year's pay up to the annual base, reduced rate above | 30 % up to 2 979 000; 15.1 % above (2025: 2 759 000) | Ст. 425, 421 НК РФ; постановление Правительства РФ от 31.10.2025 № 1705 (2025: № 1457) |
| Injury insurance | Class I rate | 0.2 % | 125-ФЗ, 179-ФЗ; закон о тарифах на 2026 г. |
| Minimum wage | Reference figure (regulations) | 27 093 ₽ | Федеральный закон от 28.11.2025 № 429-ФЗ |
| Fixed assets | Straight-line depreciation from the month after recognition; average cost of goods | — | ФСБУ 6/2020; ФСБУ 5/2019 |
| Cash | КО-1, КО-2, КО-4 forms, cash book per day | — | Указание Банка России от 11.03.2014 № 3210-У (ред. 09.01.2024); постановление Госкомстата от 18.08.1998 № 88 |
| Payments | Payment order 0401060; ЕНП payment type to 68.90 | — | Положение Банка России от 29.06.2021 № 762-П; ст. 11.3, 45 НК РФ |
| Payroll form | Расчетная ведомость Т-51 | — | Постановление Госкомстата от 05.01.2004 № 1 (optional form, ст. 9 402-ФЗ) |
| Inventory write-off | Списание товаров at average cost by script (`post_writeoff`) | — | ФСБУ 5/2019 п. 36, 41 |
| Registers | Оборотно-сальдовая ведомость, журнал операций | — | Ст. 10 402-ФЗ |

Simplifications and known gaps (ru-2026):

- The reduced contribution tariff for SMEs in priority industries (15 % above
  1.5 МРОТ, пп. 17 п. 1 ст. 427 НК РФ, распоряжение Правительства от
  27.12.2025 № 4125-р) is the `contributions_sme` function of `rules.lua`;
  such an organization sets `script="contributions_sme"` on its
  contributions row. The 100 000 ₽ cash limit (указание № 5348-У) is a
  warning plugin, not a block.
- The cumulative НДФЛ base deducts the monthly allowance once per earlier
  accrual document in the year; employees hired mid-year or paid twice a
  month are approximated. The 30 % non-resident rate and the 15 % rate on
  dividends are not modelled.
- Amounts in words, name declension and the number of the «документ об
  отгрузке» follow the demo; the electronic formats of счет-фактура and УПД
  (ФНС, format 5.03) are not produced, only printable forms.
- Books of purchases and sales list the row's rate instead of the separate
  columns 14–18; totals per rate follow below the rows.
- ФСБУ 9/2025 «Доходы» (mandatory from 2027) changes revenue recognition
  for construction contracts and deferred payments; the ledger recognizes
  revenue on shipment, which stays correct for goods.
- The ЕНС (single tax account) is a payment type; the offset of the single
  payment against НДС, НДФЛ and contributions is a manual operation.

## Germany 2026 (`de-2026`)

| Area | Rule implemented | Figures | Legal basis |
|---|---|---|---|
| Accounts | SKR 03 subset with automatic accounts per rate; 2650 for interest on late payment (§ 288 BGB) | 34 accounts | DATEV Standardkontenrahmen SKR 03, Stand 2026 |
| VAT | 19 % and 7 %; a line's rate routes revenue to 8400/8300, output tax to 1776/1771, purchases to 3400/3300 with input tax 1576/1571 (document types with `auto_accounts`); service invoices keep their expense account | 19, 7, 0 | § 12 UStG; Steueränderungsgesetz 2025 (Speisen 7 % ab 2026) |
| Invoices | Pflichtangaben incl. Leistungsdatum, Steuernummer and USt-IdNr., Entgelt nach Steuersätzen; GmbH footer | — | § 14 Abs. 4 UStG; § 35a GmbHG |
| VAT return | Kennzahlen 81, 86, 41/43, 66 from the per-rate summary | — | § 18 UStG; Vordruck USt 1 A |
| Wage tax | Steuerklasse I: tariff on twelve monthly bases less Vorsorgepauschale (employee KV, PV, RV, AV) and Pauschbeträge (employee `allowance` 105.50 = (1 230 + 36) / 12); annual tax floored to euros, one twelfth truncated to cents | Grundfreibetrag 12 348; zones to 17 799 / 69 878 / 277 825 | § 32a EStG (Steuerfortentwicklungsgesetz); § 39b Abs. 2 EStG; BMF-Schreiben vom 14.08.2025 (Vorsorgepauschale ab 2026) |
| Solidaritätszuschlag | 5.5 % of Lohnsteuer above the Freigrenze, Milderungszone (scripted row `soli`) | Freigrenze 20 350 € a year, 1 695.83 € a month; 11.9 % | §§ 3, 4 SolZG 1995 |
| Social insurance | Employee and employer halves, capped monthly | KV 7.3 % + 1.45 % Zusatzbeitrag, PV 1.8 %, RV 9.3 %, AV 1.3 % each side; Insolvenzgeldumlage 0.15 % employer; BBG 5 812.50 (KV/PV) and 8 450 (RV/AV) | Sozialversicherungs-Rechengrößenverordnung 2026 (BGBl. 2025 I Nr. 278); Bundesanzeiger 10.11.2025 (Ø Zusatzbeitrag 2.9 %); SGB III, IV, V, VI, XI |
| 2025 rows | Kept for documents dated 2025 | Zusatzbeitrag 2.5 %, BBG 5 512.50 / 8 050, tariff 2025 (12 096) | Rechengrößenverordnung 2025; § 32a EStG a. F. |
| Fixed assets | Linear AfA monatsgenau from the month of acquisition, booked against the asset account; GWG as 1-month life on 0480 | GWG 800 € netto | § 7 Abs. 1, § 6 Abs. 2 EStG |
| Cash | Kassenbuch per day | — | § 146 Abs. 1 AO; GoBD |
| Export | DATEV Buchungsstapel (EXTF 700, format 13) CSV of the period's entries | — | DATEV Formatbeschreibung |
| Registers | Summen- und Saldenliste, Buchungsjournal | — | §§ 238, 239 HGB |

Simplifications and known gaps (de-2026):

- Only Steuerklasse I. The Solidaritätszuschlag is a scripted row (`soli` in
  `rules.lua`: Freigrenze 20 350 €, Milderungszone 11.9 %); the childless
  surcharge in the Pflegeversicherung is an opt-in script (`pv_an_kinderlos`).
  Kirchensteuer, Kinderfreibeträge, the Sachsen split and the Krankengeld
  adjustment in the KV part of the Vorsorgepauschale are not modelled; the
  official Programmablaufplan (PAP) is the reference for payroll of record.
  The computed Lohnsteuer is within a few euro of the PAP for ordinary
  salaries.
- Each Krankenkasse sets its own Zusatzbeitrag; 2.9 % is the published average.
  Change the two KV rows for the actual Kasse.
- Minijobs (603 €), Gleitzone and the degressive AfA (30 %, acquisitions
  01.07.2025–31.12.2027) are documented in `regulations` but not computed.
- E-Rechnung (EN 16931 / XRechnung / ZUGFeRD) is not generated; the HTML
  Rechnung is a printable form. Issuing B2B e-invoices becomes mandatory
  from 2027 (turnover above 800 000 €) and 2028 for all.
- The DATEV export writes tax entries as separate rows. Automatic accounts in
  DATEV compute tax themselves; a Steuerberater importing the file should
  map 1776/1771/1576/1571 rows or disable the automatic function of the
  target accounts. Beraternummer and Mandantennummer in the header are 0.
- Invoice per-rate totals are calculated directly from its lines in document
  currency, independently of the report period and posting status.

## Refreshing the configurations

Rules change at year end in both countries; some change mid-year. Do the
review in **November** and **early January**, and whenever a source below
publishes.

### Where the figures come from

Russia

| What | Who publishes | When | Where |
|---|---|---|---|
| Tax Code amendments (НДС, НДФЛ, взносы) | Госдума / Президент, budget package | November–December | publication.pravo.gov.ru, nalog.gov.ru/rn77/taxation/ |
| Единая предельная база взносов | Правительство РФ (постановление) | late October–November | publication.pravo.gov.ru |
| МРОТ | Federal law | November–December | publication.pravo.gov.ru |
| Injury insurance tariffs | Federal law on tariffs for the year | December | sfr.gov.ru |
| Forms: счет-фактура, книги, декларация | Правительство (№ 1137), ФНС (приказы, форматы) | any time; often spring | nalog.gov.ru/rn77/about_fts/docs/ |
| Cash and payments | Банк России (указания, положения) | any time | cbr.ru |
| Accounting standards (ФСБУ) | Минфин России | any time | minfin.gov.ru → Бухгалтерский учет → ФСБУ |
| Chart of accounts amendments | Минфин России | rare | minfin.gov.ru |

Germany

| What | Who publishes | When | Where |
|---|---|---|---|
| Sozialversicherungs-Rechengrößenverordnung (BBG, Bezugsgröße) | Bundesregierung, BGBl. | October–November | recht.bund.de (Bundesgesetzblatt) |
| Durchschnittlicher Zusatzbeitrag | BMG, Bundesanzeiger | early November | bundesanzeiger.de, gkv-spitzenverband.de |
| Beitragssätze RV/AV/PV | SGB VI, III, XI; Verordnungen | November–December | gesetze-im-internet.de |
| Einkommensteuertarif, Pauschbeträge | EStG amendments (Jahressteuergesetz) | December | gesetze-im-internet.de/estg/__32a.html |
| Programmablaufplan Lohnsteuer | BMF | November (and mid-year corrections) | bundesfinanzministerium.de → Lohnsteuer → Programmablaufplan |
| Umsatzsteuer (§ 12, § 14, § 19 UStG), Vordrucke UStVA | BMF, BGBl. | December | gesetze-im-internet.de/ustg_1980/, elster.de |
| SKR 03 changes | DATEV eG | December–January | datev.de → Jahreswechsel → Kontenrahmenänderungen |
| Mindestlohn, Geringfügigkeitsgrenze | Mindestlohnkommission / BMAS, Minijob-Zentrale | announced a year ahead | minijob-zentrale.de |
| GoBD, Kassen | BMF-Schreiben | any time | bundesfinanzministerium.de |

### Procedure

1. **Collect the figures** of the coming year into a short table: rate, cap,
   threshold, act and date of publication. Keep the links.
2. **Never edit history.** Close every `payroll_taxes` row that changes with
   `valid_to="YYYY-12-31"` and add the new row with `valid_from="YYYY+1-01-01"`
   and a new `id` and `code` (suffix the old code with the year, as `kv_an_2025`).
   For a new tariff add new `tax_scales` rows under the new tax id. Rows
   that do not change keep their open `valid_to`.
3. **Tax rates**: add a new `vat_rates` row for a new rate and change the
   default `vat_rate_id` of items in the demo; mark the old rate in its name
   («20% (до 2026)») and keep it, so documents of earlier years repost
   unchanged. In Germany give the new rate its automatic accounts.
4. **Accounts**: add new accounts at the end of `accounts` with fresh ids;
   never reuse or renumber ids.
5. **Forms**: adjust templates in `print/` when a form changes; update the
   `basis` of the `print_forms` row and the regulation row. Template tags are
   listed in `README.md`; `{{#table field=value}}` filters rows, `|comma` and
   `|ymd` format exports.
6. **Regulations**: add a row for each act you relied on, with `valid_from`
   and a URL; update `settings.basis` and bump `settings.version`
   (`2.0.2027`). Record the change in the log at the end of this file.
7. **Check**: `python3 -I apps/ledger/tools/check_configuration.py --all apps/ledger/share/configurations`
   must report `ok`; it fails when no payroll row covers the current year.
8. **Test**: add a dated case to `apps/ledger/tests/ledger_test.c` (as the
   2025 document in `test_de_2026` and the high earner in `test_ru_2026`) with
   figures computed by hand from the act, then `make test`.
9. **Demo**: when the demo year moves, update dates, rates and expected
   totals in `demo.xml` and in the tests; `--screenshot` the main pages for
   the README.
10. **Strings**: new user-visible texts in `ledger.orion` need
    `orionc --strings` and translations in `share/locale/*.xml`; national
    terms go to `configurations/<code>/locale/`.

When the shape of a rule changes (a new levy, a tariff with its own
conditions, a document that posts differently), write it in the
configuration's `rules.lua` first and point the row at it with `script=`;
`check_configuration.py` confirms the function exists. Extend the schema in
`ledger.orion` and the engine in `posting.c` or `print.c` only when the
script API lacks a primitive (and then add the primitive generically to
`ledger_script.c`); never special-case a country in C.

### Signals that a refresh is due

- `check_configuration.py` reports that no payroll row covers 1 January.
- A document of the new year posts contributions at last year's cap.
- The authorities publish one of the acts in the tables above.
- A print form shows an act reference the authority has replaced.

## Change log

| Date | Configuration | Change |
|---|---|---|
| 2026-10 | ru-2026 | Created: НДС 22 % (425-ФЗ), прогрессивная шкала НДФЛ (176-ФЗ), единый тариф и база 2 979 000 (№ 1705), травматизм 0.2 %, ФСБУ 6/2020, формы по № 1137, № 762-П, № 3210-У, ЕНС |
| 2026-10 | ru-2026, de-2026 | Lua rule engine: `rules.lua` hooks for document posting, payroll taxes, closing steps; plugins with `ledger.on`. Examples: `post_writeoff`, `contributions_sme`, `soli`, `pv_an_kinderlos`, cash-limit and Kleinbetrag plugins |
| 2026-10 | de-2026 | Created: SKR 03 2026 with automatic accounts, 19 %/7 %, § 32a tariff 2026 and 2025, SV-Rechengrößen 2026 and 2025, Vorsorgepauschale 2026, § 14 UStG Rechnung, UStVA Kennzahlen, DATEV EXTF export |
