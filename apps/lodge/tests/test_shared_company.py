"""Exercise Lodge conflicts and retries against a disposable shared company."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
from pathlib import Path
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('orion_server_lodge', ROOT / 'tools/orion_server.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


@unittest.skipUnless((ROOT / 'build/bin/orion-service').is_file(), 'build orion-service first')
class SharedCompanyTests(unittest.TestCase):
    def test_complete_shift_with_recovery_and_roles(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(actor, role, key, command, args, revision=None):
                        body = {'request_id': key, 'expected_revision': company.status()['revision'] if revision is None else revision,
                                'command': command, 'args': args}
                        return body, company.execute(actor, role, body)
                    def ok(actor, role, key, command, args):
                        body, result = request(actor, role, key, command, args)
                        self.assertTrue(result['ok'], result)
                        return body, result
                    room = ok('alice', 'reception', 'room', 'save_room', {'name': '101', 'capacity': 2, 'rate': 100})[1]['result']['id']
                    guest = ok('alice', 'reception', 'guest', 'save_guest', {'name': 'Guest'})[1]['result']['id']
                    stay = {'room_id': room, 'guest_id': guest, 'arrival': '2035-01-01', 'departure': '2035-01-02',
                            'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''}
                    booking = ok('alice', 'reception', 'stay', 'reserve', stay)[1]['result']['id']
                    self.assertFalse(request('bob', 'reception', 'premature', 'check_in', {'id': booking})[1]['ok'])
                    with self.assertRaises(server.Refusal) as denied:
                        request('cleaner', 'housekeeping', 'unauthorized-pay', 'payment',
                                {'id': booking, 'amount_minor': 10000, 'request_key': 'denied'})
                    self.assertEqual(denied.exception.status, 403)
                    stale = company.status()['revision']
                    ok('cleaner', 'housekeeping', 'inspect', 'set_room_cleaning', {'id': room, 'state': 'inspection_needed'})
                    with self.assertRaises(server.Refusal) as conflict:
                        request('bob', 'reception', 'stale-edit', 'reserve', {'id': booking, 'notes': 'Late arrival'}, stale)
                    self.assertEqual(conflict.exception.status, 409)
                    ok('bob', 'reception', 'edit', 'reserve', {'id': booking, 'notes': 'Late arrival'})
                    ok('cleaner', 'housekeeping', 'clean', 'set_room_cleaning', {'id': room, 'state': 'clean'})
                    ok('bob', 'reception', 'check-in', 'check_in', {'id': booking})
                    payment_body, payment_result = ok('alice', 'reception', 'payment', 'payment',
                                                       {'id': booking, 'amount_minor': 12000, 'request_key': 'payment'})
                    company.close(); company = server.Company(path)
                    self.assertEqual(company.execute('alice', 'reception', payment_body), payment_result)
                    ok('bob', 'reception', 'refund', 'payment', {'id': booking, 'amount_minor': -2000, 'request_key': 'refund'})
                    ok('bob', 'reception', 'checkout', 'check_out', {'id': booking})
                    view = ok('alice', 'reception', 'view', 'snapshot', {})[1]['result']
                    self.assertEqual(view['reservations'][0]['status'], 'checked_out')
                    self.assertEqual(view['reservations'][0]['notes'], 'Late arrival')
                    self.assertEqual(view['reservations'][0]['balance'], 0)
                    self.assertEqual(view['rooms'][0]['cleaning_state'], 'dirty')
                    self.assertEqual([event['amount_minor'] for event in view['payment_events']], [12000, -2000])
                    preview = ok('alice', 'reception', 'preview', 'day_preview', {})[1]['result']
                    self.assertEqual((preview['net_minor'], preview['outstanding_minor'], preview['pending']), (10000, 0, 0))
                    with self.assertRaises(server.Refusal) as denied_close:
                        request('alice', 'reception', 'denied-close', 'close_day', {})
                    self.assertEqual(denied_close.exception.status, 403)
                    close_body, close_result = ok('manager', 'admin', 'close', 'close_day', {})
                    company.close(); company = server.Company(path)
                    self.assertEqual(company.execute('manager', 'admin', close_body), close_result)
                    closed = ok('bob', 'reception', 'closed', 'snapshot', {})[1]['result']
                    self.assertEqual(len(closed['day_closes']), 1)
                    self.assertEqual(len(closed['payment_events']), 2)
                    self.assertFalse(request('bob', 'reception', 'after-close', 'payment',
                                             {'id': booking, 'amount_minor': 100, 'request_key': 'after-close'})[1]['ok'])
                finally:
                    company.close()

    def test_cancelled_payments_in_day_preview(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(key, command, args, role='reception'):
                        result = company.execute('alice', role, {'request_id': key, 'expected_revision': company.status()['revision'],
                                                                        'command': command, 'args': args})
                        self.assertTrue(result['ok'], result)
                        return result['result']
                    room = request('room', 'save_room', {'name': '101', 'capacity': 2, 'rate': 100})['id']
                    guest = request('guest', 'save_guest', {'name': 'Guest'})['id']
                    booking = request('stay', 'reserve', {'room_id': room, 'guest_id': guest, 'arrival': '2035-01-01',
                                      'departure': '2035-01-02', 'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''})['id']
                    request('pay', 'payment', {'id': booking, 'amount_minor': 7000, 'request_key': 'deposit'})
                    request('cancel', 'cancel', {'id': booking})
                    request('refund', 'payment', {'id': booking, 'amount_minor': -2000, 'request_key': 'refund'})
                    company.close(); company = server.Company(path)
                    report = request('preview', 'day_preview', {})
                    self.assertEqual((report['cancelled_paid_count'], report['cancelled_paid_minor']), (1, 5000))
                    request('refund-rest', 'payment', {'id': booking, 'amount_minor': -5000, 'request_key': 'refund-rest'})
                    report = request('preview-again', 'day_preview', {})
                    self.assertEqual((report['cancelled_paid_count'], report['cancelled_paid_minor']), (0, 0))
                    request('repay', 'payment', {'id': booking, 'amount_minor': 5000, 'request_key': 'repay'})
                    request('close', 'close_day', {}, 'admin')
                    company.close(); company = server.Company(path)
                    close = request('closed-view', 'snapshot', {})['day_closes'][0]
                    self.assertEqual((close['cancelled_paid_count'], close['cancelled_paid_minor']), (1, '5000'))
                finally:
                    company.close()

    def test_two_clients_conflicts_and_lost_payment_reply(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(actor, key, command, args, revision=None):
                        return company.execute(actor, 'reception', {'request_id': key, 'expected_revision': company.status()['revision'] if revision is None else revision,
                                               'command': command, 'args': args})
                    room = request('alice', 'room', 'save_room', {'name': '101', 'capacity': 2, 'rate': 100})['result']['id']
                    guest = request('alice', 'guest', 'save_guest', {'name': 'Guest'})['result']['id']
                    stay = {'room_id': room, 'guest_id': guest, 'arrival': '2035-01-01', 'departure': '2035-01-02',
                            'persons': 2, 'rate': 100, 'extras': 0, 'notes': ''}

                    def race(command, args):
                        revision = company.status()['revision']
                        barrier = threading.Barrier(2)
                        def submit(actor):
                            body = {'request_id': command + '-' + actor, 'expected_revision': revision, 'command': command, 'args': args(actor)}
                            barrier.wait()
                            try: return actor, body, company.execute(actor, 'reception', body)
                            except server.Refusal as error: return actor, body, error
                        with ThreadPoolExecutor(max_workers=2) as pool:
                            results = list(pool.map(submit, ('alice', 'bob')))
                        winners = [item for item in results if isinstance(item[2], dict)]
                        losers = [item for item in results if isinstance(item[2], server.Refusal)]
                        self.assertEqual(len(winners), 1, results)
                        self.assertTrue(winners[0][2]['ok'])
                        self.assertEqual(len(losers), 1, results)
                        self.assertEqual(losers[0][2].status, 409)
                        return winners[0], losers[0]

                    winner, loser = race('reserve', lambda actor: dict(stay))
                    booking = winner[2]['result']['id']
                    self.assertFalse(request('alice', 'direct-paid', 'reserve', {'id': booking, 'paid': 100})['ok'])
                    self.assertFalse(request('alice', 'reduce-capacity', 'save_room', {'id': room, 'capacity': 1})['ok'])
                    self.assertEqual(request('alice', 'room-view', 'snapshot', {})['result']['rooms'][0]['capacity'], 2)
                    overlap = request(loser[0], 'reserve-after-refresh', 'reserve', stay)
                    self.assertFalse(overlap['ok'])
                    self.assertEqual(len(request('alice', 'reservation-view', 'snapshot', {})['result']['reservations']), 1)

                    paid, rejected = race('payment', lambda actor: {'id': booking, 'amount_minor': 10000, 'request_key': 'payment-' + actor})
                    # Treat the first response as lost: replay the identical request, even after restart.
                    self.assertEqual(company.execute(paid[0], 'reception', paid[1]), paid[2])
                    with self.assertRaises(server.Refusal) as stale:
                        company.execute(rejected[0], 'reception', rejected[1])
                    self.assertEqual(stale.exception.status, 409)
                    company.close(); company = server.Company(path)
                    self.assertEqual(company.execute(paid[0], 'reception', paid[1]), paid[2])
                    view = request('alice', 'payment-view', 'snapshot', {})['result']
                    self.assertEqual(len(view['payment_events']), 1)
                    self.assertEqual(view['payment_events'][0]['amount_minor'], 10000)
                    self.assertEqual(view['reservations'][0]['balance'], 0)
                finally:
                    company.close()


    def test_group_settlement_allocations_and_retry(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(key, command, args, actor='alice', role='reception'):
                        body = {'request_id': key, 'expected_revision': company.status()['revision'], 'command': command, 'args': args}
                        return body, company.execute(actor, role, body)
                    rooms = [request('room-' + str(i), 'save_room', {'name': str(i), 'capacity': 2, 'rate': 100})[1]['result']['id'] for i in (101, 102)]
                    guest = request('guest', 'save_guest', {'name': 'Guest'})[1]['result']['id']
                    def stay(room):
                        return {'room_id': room, 'guest_id': guest, 'arrival': '2035-01-01', 'departure': '2035-01-02',
                                'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''}
                    group = request('group', 'group_reserve', {'name': 'Group', 'stays': [stay(r) for r in rooms]})[1]['result']
                    first, second = group['reservations']
                    allocation = [{'id': first, 'amount_minor': 10000}, {'id': second, 'amount_minor': 10000}]
                    stale_revision = company.status()['revision']
                    stale_body = {'request_id': 'stale-client', 'expected_revision': stale_revision, 'command': 'group_settle',
                                  'args': {'id': group['id'], 'allocation': allocation, 'request_key': 'stale-client'}}
                    request('deposit', 'payment', {'id': first, 'amount_minor': 2500, 'request_key': 'deposit'})
                    with self.assertRaises(server.Refusal) as conflict:
                        company.execute('bob', 'reception', stale_body)
                    self.assertEqual(conflict.exception.status, 409)
                    args = {'id': group['id'], 'allocation': allocation, 'request_key': 'settlement'}
                    self.assertFalse(request('stale', 'group_settle', args)[1]['ok'])
                    view = request('after-stale', 'snapshot', {})[1]['result']
                    self.assertEqual(len(view['payment_events']), 1)
                    allocation[0]['amount_minor'] = 7500
                    with self.assertRaises(server.Refusal) as denied:
                        request('denied', 'group_settle', args, 'cleaner', 'housekeeping')
                    self.assertEqual(denied.exception.status, 403)
                    body, result = request('settle', 'group_settle', args)
                    self.assertTrue(result['ok'], result)
                    company.close(); company = server.Company(path)
                    self.assertEqual(company.execute('alice', 'reception', body), result)
                    replay = request('new-envelope', 'group_settle', args)[1]
                    self.assertTrue(replay['ok'], replay)
                    self.assertFalse(request('other-actor-same-key', 'group_settle', args, 'bob')[1]['ok'])
                    bob_group = request('bob-group', 'group_reserve', {'name': 'Bob', 'stays': [dict(stay(r), arrival='2035-03-01', departure='2035-03-02') for r in rooms]}, 'bob')[1]['result']
                    bob_args = {'id': bob_group['id'], 'allocation': [{'id': i, 'amount_minor': 10000} for i in bob_group['reservations']], 'request_key': 'settlement'}
                    self.assertTrue(request('bob-settle', 'group_settle', bob_args, 'bob')[1]['ok'])
                    altered = {'id': group['id'], 'allocation': [allocation[0]], 'request_key': 'settlement'}
                    self.assertFalse(request('altered', 'group_settle', altered)[1]['ok'])
                    view = request('final', 'snapshot', {}, 'bob')[1]['result']
                    self.assertEqual([r['balance'] for r in view['reservations']], [0, 0, 0, 0])
                    self.assertEqual([e['amount_minor'] for e in view['payment_events']], [2500, 7500, 10000, 10000, 10000])
                    self.assertEqual([e['actor'] for e in view['payment_events']], ['alice', 'alice', 'alice', 'bob', 'bob'])
                    request('refund', 'payment', {'id': first, 'amount_minor': -2500, 'request_key': 'refund'})
                    request('cancel-second', 'cancel', {'id': second})
                    cancelled = {'id': group['id'], 'allocation': [{'id': first, 'amount_minor': 2500}], 'request_key': 'cancelled-group'}
                    self.assertFalse(request('cancelled-group', 'group_settle', cancelled)[1]['ok'])
                    other = request('other-group', 'group_reserve', {'name': 'Credit', 'stays': [dict(stay(r), arrival='2035-02-01', departure='2035-02-02') for r in rooms]})[1]['result']
                    a, b = other['reservations']
                    request('overpay', 'payment', {'id': a, 'amount_minor': 11000, 'request_key': 'overpay'})
                    credit = {'id': other['id'], 'allocation': [{'id': b, 'amount_minor': 10000}], 'request_key': 'credit-group'}
                    self.assertFalse(request('credit-group', 'group_settle', credit)[1]['ok'])
                    view = request('unchanged', 'snapshot', {})[1]['result']
                    self.assertEqual([r['balance'] for r in view['reservations']], [25, 0, 0, 0, -10, 100])
                finally:
                    company.close()

    def test_group_cancellation_is_atomic_and_replayable(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(key, command, args, actor='alice', role='reception'):
                        body = {'request_id': key, 'expected_revision': company.status()['revision'], 'command': command, 'args': args}
                        return body, company.execute(actor, role, body)
                    rooms = [request('room-' + str(i), 'save_room', {'name': str(i), 'capacity': 2, 'rate': 100})[1]['result']['id'] for i in (101, 102)]
                    guest = request('guest', 'save_guest', {'name': 'Guest'})[1]['result']['id']
                    def stay(room):
                        return {'room_id': room, 'guest_id': guest, 'arrival': '2035-01-01', 'departure': '2035-01-02',
                                'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''}
                    group = request('group', 'group_reserve', {'name': 'Group', 'stays': [stay(r) for r in rooms]})[1]['result']
                    first, second = group['reservations']
                    request('clean', 'set_room_cleaning', {'id': rooms[1], 'state': 'clean'}, 'cleaner', 'housekeeping')
                    request('in', 'check_in', {'id': second})
                    self.assertFalse(request('bad-cancel', 'group_cancel', {'id': group['id']})[1]['ok'])
                    view = request('view', 'snapshot', {})[1]['result']
                    self.assertEqual([r['status'] for r in view['reservations']], ['reserved', 'checked_in'])
                    request('pay-second', 'payment', {'id': second, 'amount_minor': 10000, 'request_key': 'pay-second'})
                    request('checkout', 'check_out', {'id': second})
                    self.assertFalse(request('bad-closed', 'group_cancel', {'id': group['id']})[1]['ok'])
                    group2 = request('group2', 'group_reserve', {'name': 'Future', 'stays': [dict(stay(r), arrival='2035-02-01', departure='2035-02-02') for r in rooms]})[1]['result']
                    request('deposit', 'payment', {'id': group2['reservations'][0], 'amount_minor': 2500, 'request_key': 'deposit'})
                    with self.assertRaises(server.Refusal) as denied:
                        request('denied', 'group_cancel', {'id': group2['id']}, 'cleaner', 'housekeeping')
                    self.assertEqual(denied.exception.status, 403)
                    body, result = request('cancel', 'group_cancel', {'id': group2['id']})
                    self.assertTrue(result['ok'])
                    company.close(); company = server.Company(path)
                    self.assertEqual(company.execute('alice', 'reception', body), result)
                    view = request('after', 'snapshot', {}, 'bob')[1]['result']
                    members = [r for r in view['reservations'] if r['group_id'] == group2['id']]
                    self.assertEqual([r['status'] for r in members], ['cancelled', 'cancelled'])
                    self.assertEqual(members[0]['paid'], 25)
                    self.assertEqual(len(view['payment_events']), 2)
                    self.assertFalse(request('again', 'group_cancel', {'id': group2['id']})[1]['ok'])
                finally:
                    company.close()

    def test_group_atomicity_and_retry_after_restart(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(actor, key, command, args):
                        body = {'request_id': key, 'expected_revision': company.status()['revision'], 'command': command, 'args': args}
                        return body, company.execute(actor, 'reception', body)
                    rooms = [request('alice', 'room-' + str(i), 'save_room',
                                     {'name': str(i), 'capacity': 2, 'rate': 100})[1]['result']['id'] for i in (101, 102, 103)]
                    guest = request('alice', 'guest', 'save_guest', {'name': 'Guest'})[1]['result']['id']
                    def stay(room):
                        return {'room_id': room, 'guest_id': guest, 'arrival': '2035-01-01', 'departure': '2035-01-02',
                                'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''}
                    occupied = request('alice', 'occupied', 'reserve', dict(stay(rooms[2]), persons=2))[1]['result']['id']
                    revision = company.status()['revision']
                    _, failed = request('alice', 'failed-group', 'group_reserve',
                                        {'name': 'Rejected group', 'stays': [stay(rooms[0]), stay(rooms[2])]})
                    self.assertFalse(failed['ok'])
                    self.assertEqual(company.status()['revision'], revision)
                    view = request('alice', 'after-failure', 'snapshot', {})[1]['result']
                    self.assertEqual(len(view['reservations']), 1)
                    self.assertEqual(len(view['reservation_groups']), 0)

                    body, result = request('alice', 'group', 'group_reserve',
                                           {'name': 'Group', 'stays': [stay(rooms[0]), stay(rooms[1])]})
                    self.assertTrue(result['ok'])
                    company.close(); company = server.Company(path)
                    self.assertEqual(company.execute('alice', 'reception', body), result)
                    altered = dict(body, args={'name': 'Other', 'stays': body['args']['stays']})
                    with self.assertRaises(server.Refusal) as reuse:
                        company.execute('alice', 'reception', altered)
                    self.assertEqual(reuse.exception.status, 409)
                    view = request('bob', 'after-restart', 'snapshot', {})[1]['result']
                    self.assertEqual(len(view['reservation_groups']), 1)
                    self.assertEqual(len(view['reservations']), 3)
                    self.assertEqual(len([r for r in view['reservations'] if r['group_id'] == result['result']['id']]), 2)
                    _, overlap = request('bob', 'overlap', 'reserve', stay(rooms[0]))
                    self.assertFalse(overlap['ok'])
                    self.assertTrue(request('alice', 'cancel-occupied', 'cancel', {'id': occupied})[1]['ok'])
                    self.assertTrue(request('alice', 'reduce-cancelled', 'save_room', {'id': rooms[2], 'capacity': 1})[1]['ok'])
                finally:
                    company.close()


    def test_checked_in_room_transfer(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(key, command, args, role='reception'):
                        return company.execute('alice', role, {'request_id': key, 'expected_revision': company.status()['revision'],
                                                               'command': command, 'args': args})
                    rooms = [request('room-' + str(i), 'save_room', {'name': str(i), 'capacity': 2, 'rate': 100})['result']['id'] for i in (101, 102)]
                    guest = request('guest', 'save_guest', {'name': 'Guest'})['result']['id']
                    booking = request('reserve', 'reserve', {'room_id': rooms[0], 'guest_id': guest, 'arrival': '2035-01-01',
                                      'departure': '2035-01-02', 'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''})['result']['id']
                    self.assertTrue(request('clean-first', 'set_room_cleaning', {'id': rooms[0], 'state': 'clean'}, 'housekeeping')['ok'])
                    self.assertTrue(request('check-in', 'check_in', {'id': booking})['ok'])
                    hs = request('occupied-view', 'snapshot', {}, 'housekeeping')['result']
                    self.assertTrue(hs['rooms'][0]['occupied']); self.assertNotIn('reservations', hs); self.assertNotIn('guests', hs)
                    self.assertFalse(request('bad-transfer', 'reserve', {'id': booking, 'room_id': rooms[1]})['ok'])
                    before = request('before-transfer', 'snapshot', {})['result']
                    self.assertEqual(before['reservations'][0]['room_id'], rooms[0])
                    self.assertEqual(before['rooms'][0]['cleaning_state'], 'clean')
                    self.assertTrue(request('clean-second', 'set_room_cleaning', {'id': rooms[1], 'state': 'clean'}, 'housekeeping')['ok'])
                    self.assertTrue(request('transfer', 'reserve', {'id': booking, 'room_id': rooms[1]})['ok'])
                    company.close(); company = server.Company(path)
                    after = request('after-transfer', 'snapshot', {})['result']
                    self.assertEqual(after['reservations'][0]['room_id'], rooms[1])
                    self.assertEqual(after['rooms'][0]['cleaning_state'], 'dirty')
                    self.assertEqual(after['rooms'][1]['cleaning_state'], 'clean')
                    hs = request('transfer-view', 'snapshot', {}, 'housekeeping')['result']
                    self.assertEqual([room['occupied'] for room in hs['rooms']], [False, True])
                finally:
                    company.close()

    def test_adjacent_stays_require_actual_checkout(self):
        for country in ('DE', 'RU'):
            with self.subTest(country=country), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'company'
                server.initialize(argparse.Namespace(state=str(path), application='lodge', country=country, configuration=None,
                                  executable=str(ROOT / 'build/bin/orion-service'), resources=str(ROOT / 'apps/lodge/share'), source=None))
                company = server.Company(path)
                try:
                    def request(actor, key, command, args, role='reception'):
                        return company.execute(actor, role, {'request_id': key, 'expected_revision': company.status()['revision'],
                                                             'command': command, 'args': args})
                    rooms = [request('alice', 'room-' + str(i), 'save_room', {'name': str(i), 'capacity': 2, 'rate': 100})['result']['id'] for i in (101, 102)]
                    guest = request('alice', 'guest', 'save_guest', {'name': 'Guest'})['result']['id']
                    def reserve(key, room, arrival, departure):
                        return request('alice', key, 'reserve', {'room_id': room, 'guest_id': guest, 'arrival': arrival,
                                       'departure': departure, 'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''})['result']['id']
                    first = reserve('first', rooms[0], '2035-01-01', '2035-01-02')
                    adjacent = reserve('adjacent', rooms[0], '2035-01-02', '2035-01-03')
                    transfer = reserve('transfer-stay', rooms[1], '2035-01-02', '2035-01-03')
                    for index, room in enumerate(rooms):
                        self.assertTrue(request('housekeeper', 'clean-' + str(index), 'set_room_cleaning',
                                                {'id': room, 'state': 'clean'}, 'housekeeping')['ok'])
                    self.assertTrue(request('alice', 'first-in', 'check_in', {'id': first})['ok'])
                    self.assertFalse(request('bob', 'adjacent-in', 'check_in', {'id': adjacent})['ok'])
                    self.assertTrue(request('bob', 'transfer-in', 'check_in', {'id': transfer})['ok'])
                    self.assertFalse(request('bob', 'transfer-occupied', 'reserve', {'id': transfer, 'room_id': rooms[0]})['ok'])
                    self.assertTrue(request('alice', 'pay', 'payment', {'id': first, 'amount_minor': 10000, 'request_key': 'pay-first'})['ok'])
                    self.assertTrue(request('alice', 'first-out', 'check_out', {'id': first})['ok'])
                    self.assertFalse(request('bob', 'unclean-in', 'check_in', {'id': adjacent})['ok'])
                    self.assertTrue(request('housekeeper', 'reclean', 'set_room_cleaning', {'id': rooms[0], 'state': 'clean'}, 'housekeeping')['ok'])
                    self.assertTrue(request('bob', 'adjacent-after-checkout', 'check_in', {'id': adjacent})['ok'])
                    company.close(); company = server.Company(path)
                    view = request('alice', 'after-restart', 'snapshot', {})['result']
                    self.assertEqual([r['status'] for r in view['reservations']], ['checked_out', 'checked_in', 'checked_in'])
                    self.assertEqual([r['room_id'] for r in view['reservations']], [rooms[0], rooms[0], rooms[1]])
                finally:
                    company.close()


if __name__ == '__main__':
    unittest.main()
