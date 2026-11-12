"""End-to-end server tests against compiled Orion and Ledger business hosts."""
import argparse
import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('orion_server_e2e', ROOT / 'tools/orion_server.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


@unittest.skipUnless((ROOT / 'build/bin/orion-service').is_file(), 'build orion-service before integration tests')
class LodgeHostTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'company'
        server.initialize(argparse.Namespace(state=str(self.path), application='lodge', country='DE', configuration=None,
                          executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
        self.company = server.Company(self.path)
        self.addCleanup(lambda: self.company.close())
        self.seq = 0

    def call(self, command, args=None, role='reception', revision=None):
        self.seq += 1
        request = {'request_id': 'e2e-' + str(self.seq), 'expected_revision': self.company.status()['revision'] if revision is None else revision,
                   'command': command, 'args': args or {}}
        return self.company.execute('tester-' + role, role, request)

    def test_booking_payment_permissions_replay_and_restart(self):
        upgrade = self.call('maintenance.upgrade', role='admin')
        self.assertTrue(upgrade['ok'], upgrade)
        self.assertTrue(upgrade['result']['validated'])
        room = self.call('save_room', {'name': '101', 'capacity': 2, 'rate': 80, 'category': 'Double'})
        self.assertTrue(room['ok'], room)
        room_id = room['result']['id']
        guest = self.call('save_guest', {'name': 'Test guest', 'phone': '+49 test'})
        self.assertTrue(guest['ok'], guest)
        guest_id = guest['result']['id']
        result = self.call('set_room_cleaning', {'id': room_id, 'state': 'clean'}, 'housekeeping')
        self.assertTrue(result['ok'], result)
        booking = self.call('reserve', {'room_id': room_id, 'guest_id': guest_id, 'arrival': '2026-12-01', 'departure': '2026-12-03', 'persons': 2, 'rate': 80, 'extras': 0, 'notes': ''})
        self.assertTrue(booking['ok'], booking)
        booking_id = booking['result']['id']
        conflict = self.call('reserve', {'room_id': room_id, 'guest_id': guest_id, 'arrival': '2026-12-02', 'departure': '2026-12-04', 'persons': 1, 'rate': 80, 'extras': 0, 'notes': ''})
        self.assertFalse(conflict['ok'], conflict)
        self.assertEqual(conflict['revision'], booking['revision'])
        checked_in = self.call('check_in', {'id': booking_id})
        self.assertTrue(checked_in['ok'], checked_in)
        request = {'request_id': 'stable-payment', 'expected_revision': checked_in['revision'], 'command': 'payment',
                   'args': {'id': booking_id, 'amount_minor': 16000, 'request_key': 'stable-payment'}}
        paid = self.company.execute('tester-reception', 'reception', request)
        self.assertTrue(paid['ok'], paid)
        self.assertEqual(self.company.execute('tester-reception', 'reception', request), paid)
        self.company.close(); self.company = server.Company(self.path)
        self.assertEqual(self.company.execute('tester-reception', 'reception', request), paid)
        checkout = self.call('check_out', {'id': booking_id})
        self.assertTrue(checkout['ok'], checkout)
        view = self.call('snapshot', role='auditor')['result']
        self.assertEqual(len(view['payment_events']), 1)
        self.assertEqual(view['payment_events'][0]['amount_minor'], 16000)
        self.assertEqual(view['payment_events'][0]['actor'], 'tester-reception')
        self.assertEqual(view['rooms'][0]['cleaning_state'], 'dirty')
        cleaner = self.call('snapshot', role='housekeeping')['result']
        self.assertEqual(set(cleaner), {'rooms', 'today'})
        self.assertEqual(cleaner['today'], view['today'])
        self.assertNotIn('rate', cleaner['rooms'][0])
        with self.assertRaises(server.Refusal) as failure:
            self.call('payment', {'id': booking_id, 'amount_minor': 1, 'request_key': 'bad'}, 'auditor')
        self.assertEqual(failure.exception.status, 403)


@unittest.skipUnless((ROOT / 'build/bin/ledger-run').is_file(), 'build ledger-run before integration tests')
class LedgerHostTests(unittest.TestCase):
    def test_retained_history_of_deleted_document(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / 'source'; source.mkdir()
            (source / 'infobase.xml').write_text(
                '<ib orion-version="3"><posting_revisions><posting_revision id="1" document_id="17" action="post"/>'
                '</posting_revisions><revision_entries><revision_entry id="1" revision_id="1" debit_id="1" credit_id="2" amount="12.34"/>'
                '</revision_entries></ib>', encoding='utf-8')
            path = root / 'company'
            server.initialize(argparse.Namespace(state=str(path), application='ledger', country='DE', configuration=None,
                              executable=str(ROOT / 'build/bin/ledger-run'), resources=str(ROOT / 'apps/ledger/share'), source=str(source)))
            company = server.Company(path)
            try:
                for index, (command, args) in enumerate((('documents.history', {'document_id': 17}),
                                                         ('documents.history_entries', {'revision_id': 1}))):
                    result = company.execute('auditor', 'auditor', {'request_id': f'old-history-{index}',
                        'expected_revision': company.status()['revision'], 'command': command, 'args': args})
                    self.assertTrue(result['ok'], result)
                    self.assertEqual(len(result['result']), 1)
                self.assertEqual(result['result'][0]['amount'], 12.34)
            finally:
                company.close()

    def test_russian_and_german_small_trade_period(self):
        for country in ('RU', 'DE'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='ledger', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/ledger-run'), resources=str(ROOT / 'apps/ledger/share'), source=None))
                company = server.Company(path)
                try:
                    sequence = 0
                    def call(command, args=None, role='accountant'):
                        nonlocal sequence
                        sequence += 1
                        result = company.execute('trade-' + role, role, {'request_id': f'trade-{sequence}', 'expected_revision': company.status()['revision'], 'command': command, 'args': args or {}})
                        self.assertTrue(result['ok'], result)
                        return result['result']
                    self.assertTrue(call('maintenance.upgrade', role='admin')['validated'])
                    party = call('counterparties.save', {'fields': {'name': 'Small trade test', 'country_code': country}})
                    vat_rates = call('catalogs.list', {'table': 'vat_rates'})
                    types = call('catalogs.list', {'table': 'doc_types'})
                    # The app returns typed records; select its supported sales executor.
                    self.assertTrue(vat_rates)
                    self.assertTrue(types)
                    self.assertTrue(party)
                    item = call('items.save', {'fields': {'name': 'Test product', 'sku': 'TEST-1', 'unit_id': 1, 'vat_rate_id': vat_rates[0]['id']}})
                    call('counterparties.save', {'id': party['id'], 'fields': {'name': 'Updated party', 'tax_id': 'TEST'}})
                    call('items.save', {'id': item['id'], 'fields': {'name': 'Updated product', 'sku': 'TEST-2', 'unit_id': 1, 'vat_rate_id': vat_rates[0]['id']}})
                    updated_party = next(row for row in call('catalogs.list', {'table': 'counterparties'}) if row['id'] == party['id'])
                    updated_item = next(row for row in call('catalogs.list', {'table': 'items'}) if row['id'] == item['id'])
                    self.assertEqual((updated_party['name'], updated_party['tax_id'], updated_party['country_code']), ('Updated party', 'TEST', country))
                    self.assertEqual((updated_item['name'], updated_item['sku']), ('Updated product', 'TEST-2'))
                    with self.assertRaises(server.Refusal) as failure:
                        call('items.save', {'id': item['id'], 'fields': {'name': 'Denied', 'sku': 'X'}}, 'auditor')
                    self.assertEqual(failure.exception.status, 403)
                    codes = {row['code']: row['id'] for row in types}
                    purchase_code, sale_code = ('receipt', 'sale') if country == 'RU' else ('incoming_invoice', 'outgoing_invoice')
                    purchase = call('documents.create', {'doc_type_id': codes[purchase_code], 'counterparty_id': party['id'], 'date': '2026-11-10',
                        'lines': [{'item_id': item['id'], 'vat_rate_id': vat_rates[0]['id'], 'quantity': '10.000', 'price': '10.00'}]})
                    call('documents.post', purchase)
                    sale = call('documents.create', {'doc_type_id': codes[sale_code], 'counterparty_id': party['id'], 'date': '2026-11-11',
                        'lines': [{'item_id': item['id'], 'vat_rate_id': vat_rates[0]['id'], 'quantity': '2.000', 'price': '19.00'}]})
                    call('documents.update', {**sale, 'counterparty_id': party['id'], 'date': '2026-11-12'})
                    updated_sale = next(row for row in call('documents.list') if row['id'] == sale['document_id'])
                    self.assertEqual(updated_sale['date'], '2026-11-12')
                    replacement = {**sale, 'lines': [{'item_id': item['id'], 'vat_rate_id': vat_rates[0]['id'], 'quantity': '2.000', 'price': '20.00'}]}
                    call('documents.replace_lines', replacement)
                    call('documents.post', sale)
                    snapshot = company.db.execute('SELECT snapshot FROM current').fetchone()[0]
                    stored = ET.parse(path / 'snapshots' / snapshot / 'infobase.xml').getroot()
                    revisions = stored.find('posting_revisions')
                    self.assertIsNotNone(revisions)
                    sale_revision = next(row for row in revisions if int(row.get('document_id', '0')) == sale['document_id'])
                    self.assertEqual(sale_revision.get('actor'), 'trade-accountant')
                    before_rejection = company.status()['revision']; sequence += 1
                    rejected = company.execute('trade-accountant', 'accountant', {'request_id': f'trade-{sequence}',
                        'expected_revision': before_rejection, 'command': 'documents.replace_lines', 'args': replacement})
                    self.assertFalse(rejected['ok'], rejected)
                    self.assertEqual(company.status()['revision'], before_rejection)
                    docs = call('documents.list')
                    sale_row = next(row for row in docs if row['id'] == sale['document_id'])
                    self.assertTrue(sale_row['exact_money'])
                    self.assertEqual(sale_row['amount_minor'], '4000')
                    self.assertEqual(sale_row['vat_minor'], '880' if country == 'RU' else '760')
                    journal = call('journal.list')
                    self.assertTrue(journal)
                    self.assertTrue(all(row['exact_money'] for row in journal))
                    self.assertTrue(all(isinstance(row['amount_minor'], str) for row in journal))
                    for role in ('admin', 'accountant', 'auditor'):
                        lines = call('documents.lines', {**sale, 'offset': 0, 'limit': 1}, role)
                        self.assertEqual(len(lines), 1)
                        self.assertEqual(lines[0]['document_id'], sale['document_id'])
                        self.assertTrue(lines[0]['exact_money'])
                        self.assertEqual(lines[0]['quantity_milli'], '2000')
                        self.assertEqual(lines[0]['price_minor'], '2000')
                        self.assertEqual(call('documents.lines', {**sale, 'offset': 1, 'limit': 1}, role), [])
                        self.assertEqual(call('documents.lines', {'document_id': 2147483647}, role), [])
                        history = call('documents.history', {**sale, 'offset': 0, 'limit': 1}, role)
                        self.assertEqual(len(history), 1)
                        self.assertEqual(history[0]['document_id'], sale['document_id'])
                        self.assertEqual(history[0]['actor'], 'trade-accountant')
                        self.assertEqual(history[0]['action'], 'post')
                        entries = call('documents.history_entries', {'revision_id': history[0]['id'], 'offset': 0, 'limit': 1}, role)
                        self.assertEqual(len(entries), 1)
                        self.assertEqual(entries[0]['revision_id'], history[0]['id'])
                        self.assertIsInstance(entries[0]['amount_minor'], str)
                        self.assertEqual(call('documents.history_entries', {'revision_id': 2147483647}, role), [])
                        self.assertEqual(history[0]['total_minor'], '4880' if country == 'RU' else '4760')
                        self.assertEqual(call('documents.history', {**sale, 'offset': 1, 'limit': 1}, role), [])
                        self.assertEqual(call('documents.history', {'document_id': 2147483647}, role), [])
                    sequence += 1
                    invalid = company.execute('trade-auditor', 'auditor', {'request_id': f'trade-{sequence}',
                        'expected_revision': company.status()['revision'], 'command': 'documents.history', 'args': {'document_id': -1}})
                    self.assertFalse(invalid['ok'], invalid)
                    with self.assertRaises(server.Refusal) as failure:
                        call('documents.history', {**sale}, 'reception')
                    self.assertEqual(failure.exception.status, 403)
                    with self.assertRaises(server.Refusal) as failure:
                        call('documents.lines', {**sale}, 'reception')
                    self.assertEqual(failure.exception.status, 403)
                    sequence += 1
                    invalid = company.execute('trade-auditor', 'auditor', {'request_id': f'trade-{sequence}',
                        'expected_revision': company.status()['revision'], 'command': 'documents.history_entries', 'args': {'revision_id': -1}})
                    self.assertFalse(invalid['ok'], invalid)
                    with self.assertRaises(server.Refusal) as failure:
                        call('documents.history_entries', {'revision_id': 1}, 'reception')
                    self.assertEqual(failure.exception.status, 403)
                    reversal = call('documents.reverse', {**sale, 'date': '2026-11-12', 'reason': 'Accounting correction in integration test'})
                    self.assertNotEqual(reversal['document_id'], sale['document_id'])
                    self.assertEqual(call('documents.list', {'offset': 500, 'limit': 10}), [])
                    call('period.close', {'date': '2026-11-30'}, 'admin')
                    for role in ('admin', 'accountant', 'auditor'):
                        history = call('period.history', {'offset': 0, 'limit': 1}, role)
                        self.assertEqual(len(history), 1)
                        self.assertEqual(history[0]['closed_through'], '2026-11-30')
                        self.assertEqual(history[0]['actor'], 'trade-admin')
                        self.assertTrue(history[0]['recorded_utc'])
                        self.assertEqual(call('period.history', {'offset': 1, 'limit': 1}, role), [])
                    sequence += 1
                    denied = company.execute('trade-accountant', 'accountant', {'request_id': f'trade-{sequence}', 'expected_revision': company.status()['revision'],
                        'command': 'documents.update', 'args': {**sale, 'counterparty_id': party['id'], 'date': '2026-11-11'}})
                    self.assertFalse(denied['ok'], denied)
                    with self.assertRaises(server.Refusal) as failure:
                        call('counterparties.save', {'fields': {'name': 'Denied'}}, 'auditor')
                    self.assertEqual(failure.exception.status, 403)
                finally:
                    company.close()


if __name__ == '__main__':
    unittest.main()
