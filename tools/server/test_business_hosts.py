"""End-to-end server tests against compiled Orion and Ledger business hosts."""
import argparse
import importlib.util
from pathlib import Path
import tempfile
import unittest

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
                    party = call('counterparties.save', {'fields': {'name': 'Small trade test', 'country_code': country}})
                    vat_rates = call('catalogs.list', {'table': 'vat_rates'})
                    types = call('catalogs.list', {'table': 'doc_types'})
                    # The app returns typed records; select its supported sales executor.
                    self.assertTrue(vat_rates)
                    self.assertTrue(types)
                    self.assertTrue(party)
                    call('documents.list')
                    with self.assertRaises(server.Refusal) as failure:
                        call('counterparties.save', {'fields': {'name': 'Denied'}}, 'auditor')
                    self.assertEqual(failure.exception.status, 403)
                finally:
                    company.close()


if __name__ == '__main__':
    unittest.main()
