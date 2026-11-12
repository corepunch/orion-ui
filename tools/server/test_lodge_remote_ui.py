import argparse, hashlib, importlib.util, os, pathlib, subprocess, tempfile, threading

root = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('server', root / 'tools/orion_server.py')
server = importlib.util.module_from_spec(spec); spec.loader.exec_module(server)


def run_case(base, kind):
    state, profile = base / 'company', base / 'profile'
    profile.mkdir(mode=0o700)
    server.initialize(argparse.Namespace(state=str(state), application='lodge', country='DE', configuration=None,
                      executable=str(root / 'build/bin/orion-service'), resources=str(root / 'apps/lodge/share'), source=None))
    token = 'disposable-integration-test-token'
    (state / 'users.json').unlink()
    server.private_write(state / 'users.json', server.canonical([{'id': 'tester', 'role': 'admin', 'token_sha256': hashlib.sha256(token.encode()).hexdigest()}]))
    company = server.Company(state)
    http = server.BusinessServer(('127.0.0.1', 0), server.Handler); http.company = company
    thread = threading.Thread(target=http.serve_forever); thread.start()
    try:
        env = os.environ.copy()
        env.update(ORION_SERVER_URL='http://127.0.0.1:' + str(http.server_port), ORION_SERVER_TOKEN=token, ORION_REMOTE_PROFILE=str(profile))
        if kind:
            def perform(key, command, args):
                body = {'request_id': key, 'expected_revision': company.status()['revision'], 'command': command, 'args': args}
                result = company.execute('tester', 'admin', body)
                assert result['ok'], result
                return body, result['result']
            if kind == 'payment':
                _, room = perform('room', 'save_room', {'name': '101', 'capacity': 2, 'rate': 100})
                _, guest = perform('guest', 'save_guest', {'name': 'Guest'})
                _, stay = perform('stay', 'reserve', {'room_id': room['id'], 'guest_id': guest['id'], 'arrival': '2035-01-01',
                    'departure': '2035-01-02', 'persons': 1, 'rate': 100, 'extras': 0, 'notes': ''})
                body, _ = perform('lost-payment', 'payment', {'id': stay['id'], 'amount_minor': 10000, 'request_key': 'payment-key'})
            else:
                body, _ = perform('lost-guest', 'save_guest', {'name': 'Recovered guest'})
            env.update(ORION_TEST_RECOVERY_BODY=server.canonical(body), ORION_TEST_COMPANY=company.company_id, ORION_TEST_RECOVERY_KIND=kind)
        result = subprocess.run([str(root / 'build/bin/test_lodge_remote_ui_test')], cwd=root, env=env, timeout=240)
        if result.returncode: raise SystemExit(result.returncode)
        if kind == 'payment':
            _, view = perform('verify-recovery', 'snapshot', {})
            assert len(view['payment_events']) == 1 and view['payment_events'][0]['amount_minor'] == 10000
            assert view['reservations'][0]['balance'] == 0
    finally:
        http.shutdown(); http.server_close(); thread.join(); company.close()


with tempfile.TemporaryDirectory() as temp:
    for name, kind in (('normal', None), ('payment', 'payment'), ('guest', 'guest')):
        base = pathlib.Path(temp) / name; base.mkdir()
        run_case(base, kind)
