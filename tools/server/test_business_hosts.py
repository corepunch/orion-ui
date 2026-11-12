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
        self.assertEqual(set(cleaner), {'rooms'})
        self.assertNotIn('rate', cleaner['rooms'][0])
        with self.assertRaises(server.Refusal) as failure:
            self.call('payment', {'id': booking_id, 'amount_minor': 1, 'request_key': 'bad'}, 'auditor')
        self.assertEqual(failure.exception.status, 403)


@unittest.skipUnless((ROOT / 'build/bin/ledger-run').is_file(), 'build ledger-run before integration tests')
class LedgerHostTests(unittest.TestCase):
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
                    codes = {row['code']: row['id'] for row in types}
                    purchase_code, sale_code = ('receipt', 'sale') if country == 'RU' else ('incoming_invoice', 'outgoing_invoice')
                    purchase = call('documents.create', {'doc_type_id': codes[purchase_code], 'counterparty_id': party['id'], 'date': '2026-11-10',
                        'lines': [{'item_id': item['id'], 'vat_rate_id': vat_rates[0]['id'], 'quantity': '10.000', 'price': '10.00'}]})
                    call('documents.post', purchase)
                    sale = call('documents.create', {'doc_type_id': codes[sale_code], 'counterparty_id': party['id'], 'date': '2026-11-11',
                        'lines': [{'item_id': item['id'], 'vat_rate_id': vat_rates[0]['id'], 'quantity': '2.000', 'price': '19.00'}]})
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
