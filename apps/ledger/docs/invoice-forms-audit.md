# Invoice forms: requirements, implementation and independent checks

Review date: 2026-10-28. This audit concerns printable invoices and the stated
UBL subset. It is not a claim that payroll, every tax regime, statutory
returns, bank submission or retention/audit controls are production-complete.

## Corrected invoice workflows

| Jurisdiction | Official requirement checked | Implementation |
|---|---|---|
| RU | FNS 2026 invoice update; shipment reference; advance invoice references; ordinal, description and goods-code columns; origin and traceability columns | Separate actual reference fields, row 5б in invoice and UPD, stored shipper/consignee, line origin/customs/traceability data. No invented shipment number. 2026 ordinary domestic paper invoice validation; not FNS electronic XML. |
| DE | §14(4) UStG; §35a GmbHG | Separate supply date, invoice-local tax breakdown, explicit supplier tax/VAT IDs, legal registration details and actual payment/discount terms. EN16931 core UBL is an additional output for positive-rate domestic invoices. |
| GB | VAT Regulations 1995 reg.14; HMRC VATREC5010 | Sequential identifier uniqueness, issue/supply/tax-point dates, party identities, seller VAT number, quantities, unit prices, line rates, net and VAT totals in GBP, recorded discount terms. |
| IE | Revenue full VAT-invoice particulars | Supplier VAT ID, parties, separate dates, itemised service extent, unit price, tax per rate and totals in EUR. Foreign currency requires additional national-currency disclosure and is rejected by this form. |
| NZ | IRD taxable supply information, including information for supplies over NZD1,000 | Supplier GST number, identified buyer, dates, service description/extent, GST-exclusive value, GST and payable total. The form retains the permitted “tax invoice” wording. |
| MT | VAT Act, Twelfth Schedule; MTCA full-invoice guidance | Supplier/customer identities and VAT IDs, Article 10 registration, recorded buyer verification date/reference, dates, unit price, rate and tax totals. Article 11 routes are rejected; a normal HTML receipt is not an approved fiscal receipt. |

For GB/IE/NZ/MT the implemented accounting subset remains ordinary domestic
standard-rated services on an invoice basis, with full input recovery. A
matching HTML layout does not establish eligibility for that regime. Reduced,
zero, exempt, reverse-charge, cash, margin and cross-border cases are not
silently enabled. The caller remains responsible for factual registration and
eligibility data; VIES is not contacted by this offline implementation.

The tax point is persisted separately and drives tax-book periods and rate
validity. General-ledger entries retain the accounting document date. The
application does not derive every country's time-of-supply rules automatically.

## Other existing RU/DE outputs reviewed

| Output | Audit finding / remaining work |
|---|---|
| RU invoice for payment | A company-defined payment request, not a substitute for a statutory VAT invoice. |
| RU KO-1 / KO-2 | Layouts exist, but individual identification, authorisation and complete source-document validation are not implemented. Signatures remain manual. |
| RU payment order 0401060 / ENP | Not bank-ready: document numbers contain internal alphabetic prefixes; recipient correspondent account and budget-specific fields are not fully represented. Do not infer VAT from a settlement payment's zero `vat` field. |
| RU KO-4 | Exported cash movements are not proof of a complete signed daily statutory cash-book workflow. |
| RU T-51 / DE payroll journal | Layouts do not remove the payroll calculation gaps documented in COMPLIANCE.md. |
| RU sales/purchase books | Still simplified working papers: full statutory columns, advance-VAT posting/corrections and the 2026 7а/11а workflow remain outstanding. Adding row 5б to an invoice does not implement those postings. |
| RU VAT summary / DE UStVA | Selected totals only; not full validated tax returns or electronic filing. |
| DE Quittung | Corrected to a payment acknowledgement under §368 BGB. It no longer claims to be a §33 UStDV small invoice or invents embedded VAT from a payment document. |
| DE cash book | Contains transaction references and balances; immutable record/closed-period controls and retention still need implementation. |
| DE DATEV | Existing automatic-account/tax-row import strategy remains a blocker; no compatible-importer acceptance claim is made. |
| RU/DE trial balance and general journal | Company-defined register layouts; correctness and auditability depend on the ledger, period controls and preserved source documents, not a government HTML pixel layout. |

## Validation and file generation

Every audited invoice form declares a configuration `validate_script`. The
shared Lua invoice primitives check calendar dates, required identities,
unique numbers, finite amounts, line calculations and document reconciliation.
National validators add their requirements. Print validators cannot call
`ledger.entry`. Historical 2003 forms keep their historical behaviour.

The UI selects one form at a time, so a payment request does not require a shipment document merely because the same document type also offers a VAT invoice. An incomplete selected mandatory form stops printing before any output is replaced.
Unresolved template fields and malformed blocks fail rendering. XML output is
also parsed for well-formedness. Files are staged and renamed after successful
rendering; invoice numbers are filename-escaped while preserving the original
business identifier inside the form. Optional UBL rejection is included in the
print result rather than silently reported as success. Previous files are not
an immutable issued-invoice archive and must not be confused with a newly
emitted output listed in the result.

HTML amounts use the form's national decimal convention even with an English
UI. Electronic decimals and ISO dates are independent of UI formatting.
Paper signatures, recipient consent for unstructured electronic transmission,
and legally sufficient storage are separate responsibilities/workflows.

## Reproduce the independent UBL checks

The DE demo invoice is checked against both:

- OASIS UBL 2.1 `xsd/maindoc/UBL-Invoice-2.1.xsd` from
  https://docs.oasis-open.org/ubl/os-UBL-2.1/UBL-2.1.zip
- Official CEN/TC434 EN16931 artefacts, release `validation-1.3.17`, repository
  commit `d0dea7300c2ad60d50af4a4f31e122fdbf7c0ed3`, using
  `ubl/xslt/EN16931-UBL-validation.xslt` from
  https://github.com/ConnectingEurope/eInvoicing-EN16931

The emitted demo was also checked against the previous `validation-1.3.16` compiled rules. A one-unit corruption of the payable total is rejected by BR-CO-16. These are the core rules, not XRechnung/Peppol CIUS rules. No XRechnung,
ZUGFeRD, submission-network or government approval is claimed.

```sh
python3 -m pip install lxml saxonche
python3 apps/ledger/tools/validate_einvoice.py \
  build/test-ledger/print/AR-0001-invoice-ubl.xml \
  --xsd /path/to/UBL-2.1/xsd/maindoc/UBL-Invoice-2.1.xsd \
  --rules /path/to/eInvoicing-EN16931/ubl/xslt/EN16931-UBL-validation.xslt
```

The checked fixture passes XSD and has zero failed EN16931 assertions. This is
message-instance validation, not verification of all company facts or all
future invoices. Test cases also reject missing identity, impossible dates,
unsupported schemes, inconsistent totals and forbidden print-time writes.
Rendered samples were visually inspected on A4/A4 landscape; the UPD signature
section continues onto a second page. Large business documents may paginate.

## Official sources

- RU 2026 recommended form and attachments (for January–March):
  https://www.nalog.gov.ru/rn77/about_fts/about_nalog/16594092/
- RU April 2026 changes:
  https://www.nalog.gov.ru/rn77/news/activities_fts/16614411/
- RU shipment references:
  https://www.nalog.gov.ru/rn26/news/activities_fts/14607694/
- DE invoice content: https://www.gesetze-im-internet.de/ustg_1980/__14.html
- DE company particulars: https://www.gesetze-im-internet.de/gmbhg/__35a.html
- DE receipts: https://www.gesetze-im-internet.de/bgb/__368.html
- DE electronic-invoice transition:
  https://www.bundesfinanzministerium.de/Content/DE/FAQ/e-rechnung.html
- GB: https://www.legislation.gov.uk/uksi/1995/2518/regulation/14
  and https://www.gov.uk/hmrc-internal-manuals/vat-trader-records/vatrec5010
- IE: https://www.revenue.ie/en/vat/vat-records-invoices-credit-notes/invoices/information-required-vat-invoice.aspx
- NZ: https://www.ird.govt.nz/gst/tax-invoices-for-gst/how-tax-invoices-for-gst-work
- MT: https://mtca.gov.mt/docs/default-source/documents/business-tax/vat/faqs/tax-invoice-and-fiscal-receipts---faqs.pdf
- EN16931 validation scope:
  https://ec.europa.eu/digital-building-blocks/sites/spaces/DIGITAL/pages/467108940/Validations
