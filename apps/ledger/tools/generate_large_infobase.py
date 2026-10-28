#!/usr/bin/env python3
"""Deterministic, synthetic Ledger 2026 workload (no third-party packages)."""
import argparse
import calendar
import hashlib
import json
from pathlib import Path
import random
import xml.etree.ElementTree as ET

CONFIGS = Path(__file__).resolve().parents[1] / 'share' / 'configurations'
SIZES = {'small': 1000, 'medium': 10000, 'large': 50000}


def generate(country, documents=10000, seed=2026):
    if country not in ('ru', 'de') or not 100 <= documents <= 1000000:
        raise ValueError('country must be ru/de and documents must be 100..1000000')
    cfg = ET.parse(CONFIGS / f'{country}-2026/configuration.xml').getroot()
    types = {r.get('code'): r.attrib for r in cfg.findall('./doc_types/doc_type')}
    accounts = {r.get('code'): r.get('id') for r in cfg.findall('./accounts/account')}
    setting = cfg.find('./settings/setting')
    ru = country == 'ru'
    rng = random.Random(seed)
    root = ET.Element('ib')
    tables = {}

    def row(table, tag, **fields):
        if table not in tables:
            tables[table] = ET.SubElement(root, table)
        parent = tables[table]
        return ET.SubElement(parent, tag, {k: str(v) for k, v in {'id': len(parent) + 1, **fields}.items()})

    def doc(code, date, party=0, **fields):
        typ = types[code]
        d = row('documents', 'document', doc_type_id=typ['id'], date=date,
                counterparty_id=party, posted=1, **fields)
        d.set('number', f"{typ['prefix']}{int(d.get('id')):07d}")
        return int(d.get('id'))

    def entry(document, debit, credit, amount, **fields):
        row('entries', 'entry', document_id=document, debit_id=debit, credit_id=credit,
            amount=amount, content='SYNTHETIC opening capital', **fields)

    row('organizations', 'organization', name='SYNTHETIC ООО Тестовый Ledger' if ru else 'SYNTHETIC Ledger Test GmbH',
        address='SYNTHETIC test address', director='SYNTHETIC Person 0001')
    parties = min(1000, max(20, documents // 20))
    items = min(2000, max(30, documents // 10))
    employees = min(50, max(5, documents // 200))
    for i in range(1, parties + 1):
        row('counterparties', 'counterparty', name=f'SYNTHETIC {"ООО Контрагент" if ru else "Handel GmbH"} {i:04d}',
            address=f'SYNTHETIC address {i}', tax_id=f'TEST-{country.upper()}-{i:06d}')
    for i in range(1, items + 1):
        row('items', 'item', sku=f'SYN-{i:06d}', name=f'SYNTHETIC {"Товар" if ru else "Artikel"} {i:04d}',
            unit_id=1, vat_rate_id=1 + i % 2, price=f'{(1000 + i * 17) / 100:.2f}')
    expense = accounts['26' if ru else '4120']
    for i in range(1, employees + 1):
        row('employees', 'employee', name=f'SYNTHETIC Employee {i:04d}', account_id=expense,
            position='SYNTHETIC staff', hired='2025-01-01', salary=60000 + i * 5000 if ru else 2200 + i * 150,
            allowance=0)
    row('assets', 'asset', name='SYNTHETIC office equipment', inventory='SYN-ASSET-1',
        account_id=accounts['01.01' if ru else '0490'],
        depreciation_id=accounts['02.01' if ru else '0490'], expense_id=accounts['26' if ru else '4830'],
        cost=180000 if ru else 3600, life=36, commissioned='2025-12-01')
    opening = doc('opening', '2026-01-01')
    auxiliary = setting.get('opening_account_id')
    bank = accounts['51' if ru else '1200']
    capital = accounts['80' if ru else '0800']
    entry(opening, bank, auxiliary, 100000000 if ru else 10000000)
    entry(opening, auxiliary, capital, 100000000 if ru else 10000000)
    entry(opening, accounts['01.01' if ru else '0490'], auxiliary, 180000 if ru else 3600, debit_subconto_id=1)
    entry(opening, auxiliary, capital, 180000 if ru else 3600)
    # Twelve accruals, payouts and closings. Remaining documents are complete
    # trade cycles, with at most four unpaid invoices at the end of the year.
    remaining = documents - 37
    for month in range(1, 13):
        budget = remaining // (13 - month)
        remaining -= budget
        cycle = 5 if ru else 4
        for n in range(budget):
            phase = n % cycle
            day = 2 + (n // cycle) % 24
            date = f'2026-{month:02d}-{day:02d}'
            if phase == 0:
                party = rng.randint(1, parties)
                item = rng.randint(1, items)
                rate_id = 1 + item % 2
                rate = int(cfg.find(f'./vat_rates/vat_rate[@id="{rate_id}"]').get('rate'))
                price = 1000 + item * 17  # integer cents
                tax = (price * 10 * rate + 50) // 100
                gross = (price * 10 + tax) / 100
            if phase in (0, 2 if not ru else 3):
                purchase = phase == 0
                code = ('receipt' if ru else 'incoming_invoice') if purchase else ('sale' if ru else 'outgoing_invoice')
                d = doc(code, date, party, comment=f'SYNTHETIC cycle {month}/{n // cycle}')
                row('doc_lines', 'doc_line', document_id=d, item_id=item, vat_rate_id=rate_id,
                    quantity=10 if purchase else 6, price=f'{price / 100:.2f}')
            else:
                code = 'bank_out' if phase == 1 else ('vat_deduction' if ru and phase == 2 else 'bank_in')
                amount = gross if phase == 1 else (tax / 100 if code == 'vat_deduction' else (price * 6 + (price * 6 * rate + 50) // 100) / 100)
                doc(code, date, party, total=f'{amount:.2f}')
        end = f'2026-{month:02d}-{calendar.monthrange(2026, month)[1]:02d}'
        accrual = doc('payroll', end)
        for employee in tables['employees']:
            row('payroll_lines', 'payroll_line', document_id=accrual,
                employee_id=employee.get('id'), amount=employee.get('salary'))
        # Filled using the real engine by the materializer, after the accrual posts.
        doc('payout', end, comment='SYNTHETIC fill net payroll before posting')
        doc('closing', end)
    # IDs must follow dates, including postings that use date/id ordering.
    docs = sorted(tables['documents'], key=lambda d: (d.get('date'), int(d.get('id'))))
    remap = {d.get('id'): str(i) for i, d in enumerate(docs, 1)}
    tables['documents'][:] = docs
    for d in docs:
        d.set('id', remap[d.get('id')])
    for table in ('entries', 'doc_lines', 'payroll_lines'):
        for r in tables.get(table, []):
            r.set('document_id', remap[r.get('document_id')])
    row('report_settings', 'report_setting', date_from='2026-01-01', date_to='2026-12-31', account_id=bank)
    ET.indent(root)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--country', choices=('ru', 'de'), required=True)
    parser.add_argument('--size', choices=SIZES, default='medium')
    parser.add_argument('--documents', type=int)
    parser.add_argument('--seed', type=int, default=2026)
    parser.add_argument('--output', type=Path, required=True, help='new infobase directory; refuses overwrite')
    args = parser.parse_args()
    data = generate(args.country, args.documents if args.documents is not None else SIZES[args.size], args.seed)
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / 'infobase.xml').open('xb') as stream:
        stream.write(data)
    manifest = {'generator_version': 1, 'configuration': f'{args.country}-2026', 'seed': args.seed,
                'documents': args.documents if args.documents is not None else SIZES[args.size],
                'sha256': hashlib.sha256(data).hexdigest(),
                'configuration_sha256': hashlib.sha256((CONFIGS / f'{args.country}-2026/configuration.xml').read_bytes()).hexdigest(),
                'state': 'requires materialization'}
    (args.output / 'synthetic.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest))


if __name__ == '__main__':
    main()
