import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import stat
import tempfile
import unittest
import uuid
import xml.etree.ElementTree as ET
import zlib
from decimal import Decimal

HERE = Path(__file__).resolve().parent
SCRIPT = HERE.parent / 'tools' / 'inspect_1cd.py'
spec = importlib.util.spec_from_file_location('fake_1cd', HERE / 'fixtures' / 'onecd' / 'fake_1cd.py')
fake = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = fake
spec.loader.exec_module(fake)
sys.path.insert(0, str(HERE.parent / 'tools'))
from vendor.onecreader.raw1cd import Database1CD
from vendor.onecreader import bracket
from onecd_metadata import read_files
from inspect_1cd import decode_numeric_decimal


class ExtractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name) / '1Cv8.1CD'
        self.out = Path(self.tmp.name) / 'out.xml'
        self.fields = [fake.FixtureField('_IDRREF', 'B', length=16),
                       fake.FixtureField('_CODE', 'NVC', length=10),
                       fake.FixtureField('_DESCRIPTION', 'NVC', length=50),
                       fake.FixtureField('_MARKED', 'L'), fake.FixtureField('_FOLDER', 'L')]

    def row(self, name, code, **extra):
        return fake.encode_row(self.fields, {'_IDRREF': uuid.uuid4().bytes_le,
            '_CODE': code, '_DESCRIPTION': name, '_FOLDER': True, **extra})

    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), str(self.base), *args],
                              text=True, capture_output=True)

    def file_table(self, name, files):
        fields = [fake.FixtureField('FILENAME', 'NVC', length=128),
                  fake.FixtureField('BINARYDATA', 'I')]
        table = fake.FixtureTable(name, fields)
        for index, (filename, content) in enumerate(files.items(), 1):
            encoded = zlib.compress(content)[2:-4]
            chunks = [encoded[i:i + 250] for i in range(0, len(encoded), 250)]
            first = index * 10
            for i, chunk in enumerate(chunks):
                table.blobs[first + i] = (first + i + 1 if i + 1 < len(chunks) else 0, chunk)
            table.rows.append(fake.encode_row(fields, {'FILENAME': filename,
                'BINARYDATA': (first, len(encoded))}))
        return table

    def test_extract_and_refuse_overwrite(self):
        rows = [self.row('Товар & чай', '0001'), self.row('Группа', '0002', _FOLDER=False),
                self.row('Удалённый', '0003', _MARKED=True)]
        fake.write_fake_1cd(self.base, [fake.FixtureTable('_Reference42', self.fields, rows)])
        original = self.base.read_bytes()
        listing = self.run_tool('tables')
        self.assertEqual(listing.returncode, 0, listing.stderr)
        self.assertIn('_Reference42', listing.stdout)
        result = self.run_tool('extract', '--table', '_Reference42', '--kind', 'items', '--output', str(self.out))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Exported 1 records; skipped 2', result.stdout)
        body = ET.parse(self.out).getroot().find('Body')
        self.assertEqual(body[0].find('КлючевыеСвойства/Наименование').text, 'Товар & чай')
        self.assertEqual(body[0].find('КлючевыеСвойства/КодВПрограмме').text, '0001')
        self.assertNotEqual(self.run_tool('extract', '--table', '_Reference42', '--kind', 'items', '--output', str(self.out)).returncode, 0)
        self.assertEqual(self.base.read_bytes(), original)

    def test_duplicate_rejected_without_output(self):
        rows = [self.row('Одинаковые', '001'), self.row('Одинаковые', '002')]
        fake.write_fake_1cd(self.base, [fake.FixtureTable('_Reference42', self.fields, rows)])
        result = self.run_tool('extract', '--table', '_Reference42', '--kind', 'items', '--output', str(self.out))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.out.exists())

    def test_unsupported_version(self):
        fake.write_fake_1cd(self.base, [fake.FixtureTable('_Reference42', self.fields, [self.row('x', '1')])])
        data = bytearray(self.base.read_bytes()); data[10] = 9; self.base.write_bytes(data)
        self.assertNotEqual(self.run_tool('tables').returncode, 0)

    def test_registers_lists_schema_without_exporting_movements(self):
        fields = [fake.FixtureField('_RecorderRRef', 'B', length=16),
                  fake.FixtureField('_Period', 'DT'),
                  fake.FixtureField('_AccountDtRRef', 'B', length=16),
                  fake.FixtureField('_AccountCtRRef', 'B', length=16),
                  fake.FixtureField('_Fld100', 'N', length=9, precision=2)]
        row = fake.encode_row(fields, {'_Period': '2026-01-02 00:00:00', '_Fld100': 123.45})
        fake.write_fake_1cd(self.base, [fake.FixtureTable('_AccRg17', fields, [row]),
                                        fake.FixtureTable('_Reference42', self.fields, [self.row('Secret', '1')])])
        original = self.base.read_bytes()

        result = self.run_tool('registers')

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Physical register tables only', result.stdout)
        self.assertIn('_AccRg17\t_RecorderRRef:B, _Period:DT', result.stdout)
        self.assertIn('_AccountDtRRef:B', result.stdout)
        self.assertIn('_Fld100:N', result.stdout)
        self.assertNotIn('Secret', result.stdout)
        self.assertNotIn('123.45', result.stdout)
        self.assertEqual(self.base.read_bytes(), original)

    def test_registers_fails_when_no_accounting_register_schema_exists(self):
        fake.write_fake_1cd(self.base, [fake.FixtureTable('_Reference42', self.fields, [self.row('x', '1')])])
        result = self.run_tool('registers')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no physical accounting-register tables found', result.stderr)

    def test_root_object_is_read_once_when_loading_table_definitions(self):
        tables = [fake.FixtureTable(f'_Reference{n}', self.fields) for n in range(10, 15)]
        fake.write_fake_1cd(self.base, tables)
        db = Database1CD(str(self.base))
        self.addCleanup(db.close)
        read_object = db.read_object
        root_reads = []

        def count_root_reads(page):
            if page == 2:
                root_reads.append(page)
            return read_object(page)

        db.read_object = count_root_reads
        self.assertEqual(len(db.tables), len(tables))
        self.assertEqual(root_reads, [2])

    def test_metadata_blob_object_is_read_once_per_file_scan(self):
        self.metadata_base()
        db = Database1CD(str(self.base))
        self.addCleanup(db.close)
        table = db.tables['CONFIG']
        read_object = db.read_object
        blob_reads = []

        def count_blob_reads(page):
            if page == table.blob_page:
                blob_reads.append(page)
            return read_object(page)

        db.read_object = count_blob_reads
        files = read_files(db, 'CONFIG', {'root', 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa',
                                          'bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb'})
        self.assertEqual(set(files), {'root', 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa',
                                      'bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb'})
        self.assertEqual(blob_reads, [table.blob_page])

    def test_packed_decimal_is_exact_and_rejects_invalid_bcd(self):
        self.assertEqual(decode_numeric_decimal(bytes.fromhex('101234'), 5, 2), Decimal('12.34'))
        self.assertEqual(decode_numeric_decimal(bytes.fromhex('000201'), 5, 2), Decimal('-2.01'))
        self.assertEqual(decode_numeric_decimal(bytes.fromhex('100000'), 5, 2), Decimal('0.00'))
        for data, length in ((bytes.fromhex('201234'), 5), (bytes.fromhex('1A1234'), 5),
                             (bytes.fromhex('101234'), 4)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode_numeric_decimal(data, length, 2)

    def test_register_audit_maps_metadata_and_reports_aggregate_only(self):
        register_guid = '11111111-1111-4111-8111-111111111111'
        amount_guid = '22222222-2222-4222-8222-222222222222'
        account_guid = '33333333-3333-4333-8333-333333333333'
        document_guid = '44444444-4444-4444-8444-444444444444'
        metadata = bracket.dumps_file(['1',
            ['1', ['0', '0', register_guid], 'SyntheticRegister', ['1', 'ru', 'Register'], '', '0', '0'],
            ['1', ['0', '0', amount_guid], 'Amount', ['1', 'ru', 'Amount'], '', '0', '0'], account_guid])
        dbnames = bracket.dumps_file([[register_guid, 'AccRg', '17'],
                                      [register_guid, 'AccRgED', '18'],
                                      [amount_guid, 'Fld', '100'], [account_guid, 'Acc', '31'],
                                      [document_guid, 'Document', '23']])
        config = self.file_table('CONFIG', {register_guid: metadata})
        params = self.file_table('PARAMS', {'DBNames': dbnames})
        reg_fields = [fake.FixtureField('_PERIOD', 'DT'), fake.FixtureField('_RECORDERTREF', 'B', length=4),
                      fake.FixtureField('_RECORDERRREF', 'B', length=16), fake.FixtureField('_LINENO', 'N', length=4),
                      fake.FixtureField('_ACTIVE', 'L'), fake.FixtureField('_ACCOUNTDTRREF', 'B', length=16),
                      fake.FixtureField('_ACCOUNTCTRREF', 'B', length=16),
                      fake.FixtureField('_FLD100DT', 'N', length=5, precision=2),
                      fake.FixtureField('_FLD100CT', 'N', length=5, precision=2)]
        recorder_type, recorder, account1, account2 = b'\x00\x00\x00\x17', b'R' * 16, b'A' * 16, b'B' * 16
        def movement(line, active, debit, credit, dt, ct, type_ref=recorder_type, document_ref=recorder):
            return fake.encode_row(reg_fields, {'_PERIOD': '2026-01-02 00:00:00',
                '_RECORDERTREF': type_ref, '_RECORDERRREF': document_ref, '_LINENO': line,
                '_ACTIVE': active, '_ACCOUNTDTRREF': debit, '_ACCOUNTCTRREF': credit,
                '_FLD100DT': dt, '_FLD100CT': ct})
        deleted = bytearray(movement(4, True, account1, account2, 9.00, 9.00)); deleted[0] = 1
        register = fake.FixtureTable('_AccRg17', reg_fields, [
            movement(1, True, account1, b'\x00' * 16, 12.34, 0.00),
            movement(2, True, b'\x00' * 16, account2, -2.01, 0.00),
            movement(3, False, account1, account2, 9.00, 9.00),
            movement(1, False, account1, account2, 9.00, 9.00),
            movement(5, True, b'Z' * 16, b'M' * 16, 4.00, 0.00, document_ref=b'X' * 16),
            movement(6, True, account1, account2, 5.00, 0.00, type_ref=b'\x00\x00\x03\xe7'),
            bytes(deleted)])
        account_fields = [fake.FixtureField('_IDRREF', 'B', length=16), fake.FixtureField('_MARKED', 'L')]
        account_deleted = bytearray(fake.encode_row(account_fields, {'_IDRREF': b'Z' * 16}))
        account_deleted[0] = 1
        account_table = fake.FixtureTable('_Acc31', account_fields,
            [fake.encode_row(account_fields, {'_IDRREF': account1}),
             fake.encode_row(account_fields, {'_IDRREF': account2}), bytes(account_deleted)])
        document_fields = [fake.FixtureField('_IDRREF', 'B', length=16), fake.FixtureField('_MARKED', 'L')]
        document_deleted = bytearray(fake.encode_row(document_fields, {'_IDRREF': b'X' * 16}))
        document_deleted[0] = 1
        document_table = fake.FixtureTable('_Document23', document_fields,
            [fake.encode_row(document_fields, {'_IDRREF': recorder}), bytes(document_deleted)])
        ed_fields = [fake.FixtureField('_PERIOD', 'DT'), fake.FixtureField('_RECORDERTREF', 'B', length=4),
                     fake.FixtureField('_RECORDERRREF', 'B', length=16), fake.FixtureField('_LINENO', 'N', length=4),
                     fake.FixtureField('_CORRESPOND', 'L'), fake.FixtureField('_KINDRREF', 'B', length=16),
                     fake.FixtureField('_VALUE_TYPE', 'B', length=1), fake.FixtureField('_VALUE_RTREF', 'B', length=4),
                     fake.FixtureField('_VALUE_RRREF', 'B', length=16)]
        def subconto(line):
            return fake.encode_row(ed_fields, {'_PERIOD': '2026-01-02 00:00:00',
                '_RECORDERTREF': recorder_type, '_RECORDERRREF': recorder, '_LINENO': line,
                '_KINDRREF': account1, '_VALUE_TYPE': b'\x01', '_VALUE_RTREF': b'\x02\x00\x00\x00',
                '_VALUE_RRREF': account2})
        subkonto = fake.FixtureTable('_AccRgED18', ed_fields, [subconto(1), subconto(2), subconto(99)])
        fake.write_fake_1cd(self.base, [config, params, register, subkonto, account_table, document_table])

        result = self.run_tool('audit-register', '_AccRg17')

        self.assertEqual(result.returncode, 0, result.stderr)
        audit = __import__('json').loads(result.stdout)
        self.assertEqual(audit['physical_slots'], 7)
        self.assertEqual(audit['live_rows'], 6)
        self.assertEqual(audit['active_rows'], 4)
        self.assertEqual(audit['zero_debit_account_refs_live_rows'], 1)
        self.assertEqual(audit['zero_credit_account_refs_live_rows'], 1)
        self.assertEqual(audit['subkonto'], {'table': '_AccRgED18', 'physical_slots': 3,
                                             'live_rows': 3, 'matched_rows': 1, 'orphan_rows': 1,
                                             'ambiguous_rows': 1, 'duplicate_parent_keys': 1})
        refs = audit['source_references']
        self.assertEqual(refs['account_plan']['table'], '_Acc31')
        self.assertEqual(refs['account_plan']['debit'], {'live_rows': 6, 'unique_nonzero_refs': 2, 'zero_refs': 1,
            'resolved_refs': 4, 'deleted_only_refs': 1, 'missing_refs': 0, 'ambiguous_refs': 0})
        self.assertEqual(refs['account_plan']['credit'], {'live_rows': 6, 'unique_nonzero_refs': 2, 'zero_refs': 1,
            'resolved_refs': 4, 'deleted_only_refs': 0, 'missing_refs': 1, 'ambiguous_refs': 0})
        self.assertEqual(refs['recorders']['unique_document_types'], 2)
        self.assertEqual(refs['recorders']['unique_nonzero_recorder_refs'], 3)
        self.assertEqual(refs['recorders']['resolved_refs'], 4)
        self.assertEqual(refs['recorders']['deleted_only_refs'], 1)
        self.assertEqual(refs['recorders']['unknown_types'], 1)
        self.assertEqual(refs['recorders']['missing_refs'], 0)
        self.assertEqual(refs['recorders']['physical_document_tables'][0]['deleted_only_ids'], 1)
        self.assertEqual(audit['resource_side_comparisons']['Amount']['unequal_rows'], 4)
        self.assertEqual(audit['resource_side_comparisons']['Amount']['unequal_fraction'], 1.0)
        self.assertIn('_LINENO', audit['validated_bcd_fields'])
        self.assertNotIn('12.34', result.stdout)

        staged = self.run_tool('stage-register', '_AccRg17', '--output', str(self.out))
        self.assertEqual(staged.returncode, 0, staged.stderr)
        payload = json.loads(self.out.read_text())
        self.assertEqual(stat.S_IMODE(self.out.stat().st_mode), 0o600)
        self.assertIn('excludes blob/index objects', payload['scope'])
        self.assertEqual(payload['source']['sha256'], hashlib.sha256(self.base.read_bytes()).hexdigest())
        self.assertEqual(payload['register']['number'], 17)
        by_kind = {table['kind']: table for table in payload['tables']}
        self.assertEqual(bytes.fromhex(by_kind['AccRg']['data_object_hex']), b''.join(register.rows))
        self.assertEqual(bytes.fromhex(by_kind['AccRgED']['data_object_hex']), b''.join(subkonto.rows))
        self.assertEqual(by_kind['AccRg']['deleted_slots'], 1)
        self.assertIn('offset', by_kind['AccRg']['fields'][0])
        original_stage = self.out.read_bytes()
        refused = self.run_tool('stage-register', '_AccRg17', '--output', str(self.out))
        self.assertNotEqual(refused.returncode, 0)
        self.assertEqual(self.out.read_bytes(), original_stage)
        failed_path = Path(self.tmp.name) / 'missing' / 'stage.json'
        failed = self.run_tool('stage-register', '_AccRg17', '--output', str(failed_path))
        self.assertNotEqual(failed.returncode, 0)
        self.assertFalse(failed_path.exists())

        second_account_guid = '55555555-5555-4555-8555-555555555555'
        ambiguous_metadata = bracket.dumps_file(['1',
            ['1', ['0', '0', register_guid], 'SyntheticRegister', ['1', 'ru', 'Register'], '', '0', '0'],
            ['1', ['0', '0', amount_guid], 'Amount', ['1', 'ru', 'Amount'], '', '0', '0'],
            account_guid, second_account_guid])
        ambiguous_dbnames = bracket.dumps_file([[register_guid, 'AccRg', '17'],
            [register_guid, 'AccRgED', '18'], [amount_guid, 'Fld', '100'],
            [account_guid, 'Acc', '31'], [second_account_guid, 'Acc', '32'], [document_guid, 'Document', '23']])
        fake.write_fake_1cd(self.base, [self.file_table('CONFIG', {register_guid: ambiguous_metadata}),
            self.file_table('PARAMS', {'DBNames': ambiguous_dbnames}), register, subkonto, account_table, document_table])
        ambiguous = self.run_tool('audit-register', '_AccRg17')
        self.assertNotEqual(ambiguous.returncode, 0)
        self.assertIn('account plan reference is missing or ambiguous', ambiguous.stderr)

        malformed = bytearray(register.rows[0])
        malformed[28] = (malformed[28] & 0x0f) | 0x20  # invalid _LINENO sign nibble
        fake.write_fake_1cd(self.base, [config, params,
            fake.FixtureTable('_AccRg17', reg_fields, [bytes(malformed)]),
            fake.FixtureTable('_AccRgED18', ed_fields)])
        invalid = self.run_tool('audit-register', '_AccRg17')
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn('invalid packed-decimal sign nibble', invalid.stderr)

    def test_register_audit_validates_empty_subkonto_key_schema(self):
        register_guid = '11111111-1111-4111-8111-111111111111'
        amount_guid = '22222222-2222-4222-8222-222222222222'
        metadata = bracket.dumps_file(['1', ['1', ['0', '0', register_guid], 'Register', ['1', 'ru', 'Register']],
                                       ['1', ['0', '0', amount_guid], 'Amount', ['1', 'ru', 'Amount']]])
        dbnames = bracket.dumps_file([[register_guid, 'AccRg', '17'], [register_guid, 'AccRgED', '18'],
                                      [amount_guid, 'Fld', '100']])
        config, params = self.file_table('CONFIG', {register_guid: metadata}), self.file_table('PARAMS', {'DBNames': dbnames})
        reg_fields = [fake.FixtureField('_PERIOD', 'DT'), fake.FixtureField('_RECORDERTREF', 'B', length=4),
                      fake.FixtureField('_RECORDERRREF', 'B', length=16), fake.FixtureField('_LINENO', 'N', length=4),
                      fake.FixtureField('_ACTIVE', 'L'), fake.FixtureField('_ACCOUNTDTRREF', 'B', length=16),
                      fake.FixtureField('_ACCOUNTCTRREF', 'B', length=16), fake.FixtureField('_FLD100', 'N', length=5)]
        ed_fields = [fake.FixtureField('_PERIOD', 'DT'), fake.FixtureField('_RECORDERTREF', 'B', length=4),
                     fake.FixtureField('_RECORDERRREF', 'B', length=16), fake.FixtureField('_LINENO', 'N', length=5),
                     fake.FixtureField('_CORRESPOND', 'L'), fake.FixtureField('_KINDRREF', 'B', length=16),
                     fake.FixtureField('_VALUE_TYPE', 'B', length=1), fake.FixtureField('_VALUE_RTREF', 'B', length=4),
                     fake.FixtureField('_VALUE_RRREF', 'B', length=16)]
        fake.write_fake_1cd(self.base, [config, params, fake.FixtureTable('_AccRg17', reg_fields),
                                        fake.FixtureTable('_AccRgED18', ed_fields)])
        result = self.run_tool('audit-register', '_AccRg17')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('incompatible join-key schema for _LINENO', result.stderr)

    def test_fixed_width_code_from_real_schema(self):
        self.fields[1] = fake.FixtureField('_CODE', 'NC', length=10)
        fake.write_fake_1cd(self.base, [fake.FixtureTable('_Reference42', self.fields,
            [self.row('Товар', '0001')])])
        result = self.run_tool('extract', '--table', '_Reference42', '--kind', 'items', '--output', str(self.out))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(ET.parse(self.out).find('.//КодВПрограмме').text, '0001')

    def test_generated_fixture_matches_ledger_test(self):
        row = fake.encode_row(self.fields, {'_IDRREF': bytes.fromhex('01020304050607080910111213141516'),
            '_CODE': 'ICD001', '_DESCRIPTION': '1CD Sample Item', '_FOLDER': True})
        fake.write_fake_1cd(self.base, [fake.FixtureTable('_Reference42', self.fields, [row])])
        result = self.run_tool('extract', '--table', '_Reference42', '--kind', 'items', '--output', str(self.out))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.out.read_bytes(), (HERE / 'fixtures' / 'onecd' / 'catalog.xml').read_bytes())

    def metadata_base(self, duplicate=False, missing=False):
        main = 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa'
        obj = 'bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb'
        cls = 'cf4abea6-37b2-11d4-940f-008048da11f9'
        config_fields = [fake.FixtureField('FILENAME', 'NVC', length=128),
                         fake.FixtureField('BINARYDATA', 'I')]
        def file_table(name, files):
            table = fake.FixtureTable(name, config_fields)
            for index, (filename, content) in enumerate(files, 1):
                encoded = zlib.compress(content.encode('utf-8'))[2:-4]
                chunks = [encoded[i:i+250] for i in range(0, len(encoded), 250)]
                first = index * 10
                for i, chunk in enumerate(chunks):
                    table.blobs[first + i] = (first + i + 1 if i + 1 < len(chunks) else 0, chunk)
                table.rows.append(fake.encode_row(config_fields, {'FILENAME': filename,
                    'BINARYDATA': (first, len(encoded))}))
            return table
        config = file_table('CONFIG', [('root', f'{{2,{main}}}'),
            (main, f'{{1,{{{cls},1,{obj}}}}}'),
            (obj, f'{{1,{{0,0,{obj}}},"Номенклатура",{{}}}}')])
        entry = f'{{{obj},"Reference",42}}'
        if duplicate:
            entry += f',{{{obj},"Reference",43}}'
        params = file_table('PARAMS', [('DBNames', '{1,{' + entry + '}}')])
        # A saved, unapplied configuration must never replace CONFIG metadata.
        saved = file_table('CONFIGSAVE', [('root', '{2,00000000-0000-0000-0000-000000000000}')])
        tables = [config, saved, params]
        if not missing:
            tables.append(fake.FixtureTable('_Reference42', self.fields, [self.row('Товар', '100')]))
        fake.write_fake_1cd(self.base, tables)

    def test_metadata_resolves_and_extracts(self):
        self.metadata_base()
        listing = self.run_tool('catalogs')
        self.assertEqual(listing.returncode, 0, listing.stderr)
        self.assertIn('Номенклатура\tbbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb\t_Reference42', listing.stdout)
        result = self.run_tool('extract', '--catalog', 'Номенклатура', '--output', str(self.out))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(ET.parse(self.out).find('.//Наименование').text, 'Товар')

    def test_metadata_ambiguity_and_missing_table_fail_closed(self):
        for opts in ({'duplicate': True}, {'missing': True}):
            with self.subTest(opts=opts):
                self.metadata_base(**opts)
                result = self.run_tool('extract', '--catalog', 'Номенклатура', '--output', str(self.out))
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.out.exists())


if __name__ == '__main__':
    unittest.main()
