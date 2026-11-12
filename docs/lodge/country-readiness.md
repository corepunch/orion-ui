# Country configuration and tax-document boundaries

Checked against the sources below on 12 November 2026. This records implementation
boundaries, not legal or tax advice or a compliance certification. Obtain local
professional review for the hotel's actual tax regime and municipality.

Lodge can identify a hotel as Germany/EUR or Russia/RUB. This configures one
currency for the database; it does not determine a tax rate, calculate tax,
produce a statutory invoice or fiscal receipt, transmit data to a tax authority,
or support currency conversion. Its booking confirmation is not an invoice.
Existing data must be explicitly adopted in the chosen currency without
conversion; see [migration](migration-v2.md).

Germany's [UStG § 12](https://www.gesetze-im-internet.de/ustg_1980/__12.html)
provides a reduced rate for qualifying short-term accommodation, while not all
related services necessarily receive the same treatment. [UStG § 14](https://www.gesetze-im-internet.de/ustg_1980/__14.html)
sets invoice requirements, including tax breakdown and, in relevant cases,
electronic invoice format. Lodge does not yet classify services or satisfy those
invoice workflows.

Russia's [FNS guidance on accommodation and VAT](https://www.nalog.gov.ru/rn03/news/activities_fts/16594326/)
describes hotel VAT treatment that depends on the taxpayer's regime. [FNS
cash-register guidance](https://www.nalog.gov.ru/rn59/news/activities_fts/16605068/)
describes cash-register and receipt obligations subject to statutory exceptions.
[Tourist tax can vary by municipality](https://www.nalog.gov.ru/rn39/news/activities_fts/16619107/).
Lodge does not yet configure those choices, operate a fiscal register, generate
required tax documents, or calculate tourist tax.

Before enabling statutory output, a country module needs service-level tax
classification, effective dates, rounding and exemption policies, seller and
buyer details, numbering, corrections, required document formats, fiscal-device
integration where applicable, and independently verified end-to-end examples.
The applicable requirements also depend on local law, the transaction and the
hotel's tax status; a country code alone is insufficient to choose them.
