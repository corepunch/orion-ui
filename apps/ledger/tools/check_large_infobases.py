#!/usr/bin/env python3
"""Run the real Ledger engine in disposable directories; print wall/CPU timings."""
import argparse
from decimal import Decimal, ROUND_HALF_UP
import hashlib
from pathlib import Path
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from generate_large_infobase import CONFIGS, generate


def snapshot(path, movement_ids=True):
    root = ET.parse(path).getroot()
    # Ignore serialization whitespace; compare every persisted field and record.
    return {table.tag: sorted(tuple(sorted((k, v) for k, v in row.attrib.items()
                                       if movement_ids or table.tag != 'entries' or k != 'id')) for row in table) for table in root}


def check_invoices(path, country):
    root = ET.parse(path).getroot()
    cfg = ET.parse(CONFIGS / f'{country}-2026/configuration.xml')
    rates = {r.get('id'): Decimal(r.get('rate')) for r in cfg.findall('./vat_rates/vat_rate')}
    docs = {d.get('id'): d for d in root.findall('./documents/document')}
    cent = Decimal('.01')
    for line in root.findall('./doc_lines/doc_line'):
        net = (Decimal(line.get('quantity')) * Decimal(line.get('price'))).quantize(cent, rounding=ROUND_HALF_UP)
        vat = (net * rates[line.get('vat_rate_id')] / 100).quantize(cent, rounding=ROUND_HALF_UP)
        doc = docs[line.get('document_id')]
        assert Decimal(doc.get('amount')) == net, 'incorrect invoice net'
        assert Decimal(doc.get('vat')) == vat, 'incorrect invoice VAT'
        assert Decimal(doc.get('total')) == net + vat, 'incorrect invoice gross'
    # Payroll must include actual withholding calculated by the engine.
    gross = sum(Decimal(r.get('amount')) for r in root.findall('./payroll_lines/payroll_line')
                if Decimal(r.get('withheld', '0')) > 0)
    assert gross > 0, 'payroll deductions missing'


HISTORY = ('posting_revisions', 'revision_entries')


def check_repost(before, after):
    strip = lambda snap: {t: rows for t, rows in snap.items() if t not in HISTORY}
    assert strip(before) == strip(after), 'reposting changed persisted records'
    for table in HISTORY:
        assert set(before.get(table, [])) <= set(after.get(table, [])), f'reposting rewrote {table}'
    added = [dict(r) for r in set(after.get('posting_revisions', [])) - set(before.get('posting_revisions', []))]
    posted = [dict(r) for r in after.get('entries', [])]
    assert all(r['action'] == 'post' for r in added), 'reposting recorded a non-post revision'
    assert len({r['document_id'] for r in added}) == len(added), 'reposting recorded a document twice'
    new_ids = {r['id'] for r in added}
    snapshot_rows = [dict(r) for r in set(after.get('revision_entries', [])) - set(before.get('revision_entries', []))]
    assert all(r['revision_id'] in new_ids for r in snapshot_rows), 'reposting added entries to an old revision'
    assert len(snapshot_rows) == len(posted), 'posting history does not snapshot every entry'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checker', type=Path, default=Path('build/bin/ledger-fixture-check'))
    parser.add_argument('--documents', type=int, default=10000)
    parser.add_argument('--timeout', type=int, default=600, help='wall seconds per engine invocation')
    args = parser.parse_args()
    for country in ('ru', 'de'):
        with tempfile.TemporaryDirectory(prefix=f'ledger-synthetic-{country}-') as directory:
            path = Path(directory) / 'infobase.xml'
            data = generate(country, args.documents)
            path.write_bytes(data)
            print(f'{country} raw_bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}', flush=True)
            command = [str(args.checker.resolve()), f'{country}-2026', directory, str(args.documents)]
            start = time.perf_counter()
            subprocess.run(command + ['--materialize'], check=True, timeout=args.timeout)
            check_invoices(path, country)
            first = snapshot(path)
            assert 'balances' not in first, 'transient reports must not be persisted'
            subprocess.run(command, check=True, timeout=args.timeout)
            assert first == snapshot(path), 'save/reopen changed persisted records'
            # Reposting must not duplicate movements or change totals/net payroll; it only
            # appends one posting-history revision per document.
            before_repost = snapshot(path, movement_ids=False)
            subprocess.run(command + ['--materialize'], check=True, timeout=args.timeout)
            check_repost(before_repost, snapshot(path, movement_ids=False))
            print(f'{country} saved_bytes={path.stat().st_size} wall_seconds={time.perf_counter() - start:.3f}', flush=True)


if __name__ == '__main__':
    main()
