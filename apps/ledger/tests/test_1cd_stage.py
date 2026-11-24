import importlib.util
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
SCRIPT = HERE.parent / 'tools' / 'inspect_1cd.py'
spec = importlib.util.spec_from_file_location('fake_1cd', HERE / 'fixtures' / 'onecd' / 'fake_1cd.py')
fake = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = fake
spec.loader.exec_module(fake)
sys.path.insert(0, str(HERE.parent / 'tools'))
from onecd_stage import AttachmentManifest, Exporter, decode_value_storage, ref_uuid
from onecd_meta import LazyObject

F = fake.FixtureField


def G(n):
    return str(uuid.UUID(int=n))


def R(guid):
    """Stored 16-byte reference of a platform GUID (inverse of ref_uuid)."""
    h = guid.replace('-', '')
    return bytes.fromhex(h[16:20] + h[20:]) + bytes.fromhex(h[12:16]) + bytes.fromhex(h[8:12]) + bytes.fromhex(h[0:8])


def T(number):
    return number.to_bytes(4, 'big')


def hdr(guid, name):
    return '{1,{0,0,%s},"%s",{1,"ru","%s"},"",0,0}' % (guid, name, name)


def obj(guid, name, type_guids, body=''):
    return '{1,{20,%s,{0,%s}%s}}' % (','.join(type_guids), hdr(guid, name), body)


def fld(guid, name, *items):
    return ',{2,%s,{"Pattern",%s}}' % (hdr(guid, name), ','.join(items))


def ref_t(tg):
    return '{"#",%s}' % tg


# metadata GUIDs
CATALOG, BANKS, OTHER, ENUM, DOC, CHART, KINDS, REG, INFO = (G(n) for n in (100, 101, 102, 200, 300, 31, 400, 500, 900))
TYPEG = {n: G(10000 + n) for n in (100, 101, 102, 200, 300, 31, 400)}
ENUM_A, ENUM_B, ENUM_C = G(201), G(202), G(203)
CONST = G(901)


def deflate_files(table_fields, files):
    """CONFIG/PARAMS table with raw-deflated files in 256-byte blob chunks."""
    table = fake.FixtureTable('', table_fields)
    for index, (name, text) in enumerate(files.items(), 1):
        data = zlib.compress(('﻿' + text).encode('utf-8'))[2:-4]
        chunks = [data[i:i + 250] for i in range(0, len(data), 250)]
        first = index * 100
        assert len(chunks) < 100
        for i, chunk in enumerate(chunks):
            table.blobs[first + i] = (first + i + 1 if i + 1 < len(chunks) else 0, chunk)
        table.rows.append(fake.encode_row(table_fields, {'FILENAME': name, 'BINARYDATA': (first, len(data))}))
    return table


def build_tables():
    ent = []

    def add(guid, kind, number):
        ent.append('{%s,"%s",%d}' % (guid, kind, number))
    for guid, kind, number in ((CATALOG, 'Reference', 100), (BANKS, 'Reference', 101), (OTHER, 'Reference', 102),
                               (ENUM, 'Enum', 200), (DOC, 'Document', 300), (CHART, 'Acc', 31), (KINDS, 'Chrc', 400),
                               (REG, 'AccRg', 500), (REG, 'AccRgED', 501), (INFO, 'InfoRg', 900), (CONST, 'Const', 901)):
        add(guid, kind, number)
    flds = {1001: 'Название', 1002: 'Сальдо', 1003: 'Флаг', 1004: 'Контакт', 1005: 'Файл', 1006: 'Вид',
            1007: 'НеизвестныйТип', 3001: 'Контрагент', 3002: 'Вид', 3003: 'Прочее', 879: 'Валютный', 880: 'Количественный',
            884: 'Суммовой', 885: 'Валютный', 886: 'Количественный', 601: 'Организация', 602: 'Сумма',
            603: 'Количество', 604: 'Валюта', 1012: 'Тип', 3012: 'Сумма', 9011: 'Значение'}
    for number in flds:
        add(G(50000 + number), 'Fld', number)
    add(G(51010), 'VT', 1010)
    add(G(53010), 'VT', 3010)
    B, S, N = '{"B"}', '{"S",30,1}', '{"N",15,2,0}'
    files = {
        'root': '{2,%s,""}' % G(1),
        G(1): '{1,{0,%s,"1.2.3.4"}}' % hdr(G(2), 'Тестовая'),
        CONST: obj(CONST, 'ОкруглятьИтоги', [G(10901)], fld(G(59011), 'Значение', B)),
        CATALOG: obj(CATALOG, 'Контрагенты', [TYPEG[100]],
                     fld(G(51001), 'Название', S) + fld(G(50000 + 1002), 'Сальдо', N) + fld(G(50000 + 1003), 'Флаг', B)
                     + fld(G(50000 + 1004), 'Контакт', S, '{"N",10,2,0}', '{"D","D"}', B, ref_t(TYPEG[101]))
                     + fld(G(50000 + 1005), 'Файл', ref_t('e199ca70-93cf-46ce-a54b-6edc88c3a296'))
                     + fld(G(50000 + 1006), 'Вид', ref_t(TYPEG[200]))
                     + fld(G(50000 + 1007), 'НеизвестныйТип', ref_t(G(60000)))
                     + ',{0,%s}' % hdr(G(51010), 'Контакты') + fld(G(50000 + 1012), 'Тип', S)),
        BANKS: obj(BANKS, 'Банки', [TYPEG[101]]),
        OTHER: obj(OTHER, 'Прочее', [TYPEG[102]]),
        ENUM: obj(ENUM, 'Виды', [TYPEG[200]], ',{0,%s},{0,%s},{0,%s}' %
                  (hdr(ENUM_A, 'Первый'), hdr(ENUM_B, 'Второй'), hdr(ENUM_C, 'Третий'))),
        DOC: obj(DOC, 'Приход', [TYPEG[300]],
                 fld(G(50000 + 3001), 'Контрагент', ref_t(TYPEG[100])) + fld(G(50000 + 3002), 'Вид', ref_t(TYPEG[200]))
                 + fld(G(50000 + 3003), 'Прочее', ref_t(TYPEG[102]))
                 + ',{0,%s}' % hdr(G(53010), 'Товары') + fld(G(50000 + 3012), 'Сумма', N)),
        CHART: obj(CHART, 'Хозрасчетный', [TYPEG[31]],
                   fld(G(50879), 'Валютный', B) + fld(G(50880), 'Количественный', B) + fld(G(50884), 'Суммовой', B)
                   + fld(G(50885), 'Валютный', B) + fld(G(50886), 'Количественный', B)),
        G(31) + '.9': '{2,{"Pattern",{"B"}},{"#",x},{2,1,12,{"#",ae,{1,%s}},{"S","Касса"},{"S","50"}}}' % G(77),
        KINDS: obj(KINDS, 'ВидыСубконто', [TYPEG[400]]),
        REG: obj(REG, 'Журнал', [G(10500)], ',{9,%s}' % CHART + fld(G(50601), 'Организация', ref_t(TYPEG[100]))
                 + fld(G(50602), 'Сумма', N) + fld(G(50603), 'Количество', '{"N",15,3,0}') + fld(G(50604), 'Валюта', ref_t(TYPEG[101]))),
        INFO: obj(INFO, 'Сведения', [G(10900)]),
    }
    params = {'DBNames': '{1,%s}' % ','.join(ent)}
    cf = [F('FILENAME', 'NVC', length=128), F('BINARYDATA', 'I')]
    tables = [deflate_files(cf, files), deflate_files(cf, params)]
    tables[0].name, tables[1].name = 'CONFIG', 'PARAMS'
    return tables


class LazyObjectTests(unittest.TestCase):
    def test_blob_chain_rejects_premature_end_and_cycles(self):
        class Database:
            page_size = 256

            def __init__(self, pages):
                self.pages = pages

            def object_pages(self, _):
                return len(self.pages) * self.page_size, list(range(len(self.pages)))

            def read_page(self, page):
                return self.pages[page]

        def chunk(next_chunk, data):
            return (struct.pack('<Ih', next_chunk, len(data)) + data).ljust(256, b'\0')

        obj = LazyObject(Database([chunk(0, b'abc')]), 1)
        self.assertEqual(obj.read_chain(0, 3), b'abc')
        with self.assertRaisesRegex(ValueError, 'truncated blob chain'):
            obj.read_chain(0, 4)
        obj = LazyObject(Database([chunk(1, b'abc'), chunk(1, b'def')]), 1)
        with self.assertRaisesRegex(ValueError, 'invalid blob chain'):
            obj.read_chain(0, 7)


class StageFixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name) / '1Cv8.1CD'
        self.out = Path(self.tmp.name) / 'stage.xml'
        tables = build_tables()
        v = G
        const_fields = [F('_RECORDKEY', 'B', length=4), F('_FLD9011', 'L')]
        tables.append(fake.FixtureTable('_Const901', const_fields, [
            fake.encode_row(const_fields, {'_RECORDKEY': T(1), '_FLD9011': True})]))
        # catalogs
        cat_f = [F('_IDRREF', 'B', length=16), F('_VERSION', 'RV'), F('_MARKED', 'L'), F('_PREDEFINEDID', 'B', length=16),
                 F('_PARENTIDRREF', 'B', length=16), F('_FOLDER', 'L'), F('_CODE', 'NVC', length=9), F('_DESCRIPTION', 'NVC', length=50),
                 F('_FLD1001', 'NVC', length=30), F('_FLD1002', 'N', length=15, precision=2), F('_FLD1003', 'L'),
                 F('_FLD1004_TYPE', 'B', length=1), F('_FLD1004_L', 'L'), F('_FLD1004_N', 'N', length=10, precision=2),
                 F('_FLD1004_T', 'DT'), F('_FLD1004_S', 'NVC', length=20), F('_FLD1004_RTREF', 'B', length=4),
                 F('_FLD1004_RRREF', 'B', length=16), F('_FLD1005', 'I', null_exists=True), F('_FLD1006RREF', 'B', length=16),
                 F('_FLD1007RREF', 'B', length=16)]
        self.cat_f = cat_f
        k1, k2, k3, k4 = v(1001), v(1002), v(1003), v(1004)
        base = dict(_FOLDER=True)

        def cat(guid, code, name, **kw):
            return fake.encode_row(cat_f, {'_IDRREF': R(guid), '_CODE': code, '_DESCRIPTION': name, **base, **kw})
        self.bank_guid = v(2001)
        serialized = '\ufeff{"#",87126200-3e98-44e0-b931-ccb1d7edc497,{1,{0}}}'.encode('utf-8')
        storage = b'\x01\x01' + struct.pack('<Q', len(serialized)) + serialized + struct.pack('<IQ', 0, 3) + b'abc'
        catalog_rows = [
            cat(k1, '1', 'Альфа', _FLD1001='Название', _FLD1002='-1234.50', _FLD1003=True, _FLD1004_TYPE=b'\x05',
                _FLD1004_S='строка', _FLD1005=(1, len(storage)), _FLD1006RREF=R(ENUM_B)),
            cat(k2, '2', 'Бета', _FLD1002='0.00', _FLD1004_TYPE=b'\x08', _FLD1004_RTREF=T(101),
                _FLD1004_RRREF=R(self.bank_guid), _FLD1006RREF=R(ENUM_A)),
            cat(k3, '3', 'Гамма', _FLD1004_TYPE=b'\x03', _FLD1004_N='12.34', _FLD1007RREF=R(G(70000))),
            cat(v(1005), '5', 'Дельта', _FLD1004_TYPE=b'\x09', _FLD1004_S='x'),
            cat(k4, '4', 'Группа', _FOLDER=False, _MARKED=True, _FLD1004_TYPE=b'\x01', _PARENTIDRREF=R(k1)),
        ]
        vt_f = [F('_REFERENCE100_IDRREF', 'B', length=16), F('_KEYFIELD', 'B', length=4), F('_LINENO1011', 'N', length=5),
                F('_FLD1012', 'NVC', length=20)]
        vt_rows = [fake.encode_row(vt_f, {'_REFERENCE100_IDRREF': R(k1), '_LINENO1011': n, '_FLD1012': t})
                   for n, t in ((3, 'третья'), (1, 'первая'), (2, 'вторая'))]
        catalog = fake.FixtureTable('_Reference100', cat_f, catalog_rows)
        catalog.blobs[1] = (0, storage)
        tables += [catalog, fake.FixtureTable('_Reference100_VT1010', vt_f, vt_rows)]
        bank_f = [F('_IDRREF', 'B', length=16), F('_VERSION', 'RV'), F('_MARKED', 'L'), F('_FOLDER', 'L'),
                  F('_CODE', 'NVC', length=9), F('_DESCRIPTION', 'NVC', length=50)]
        tables.append(fake.FixtureTable('_Reference101', bank_f, [fake.encode_row(
            bank_f, {'_IDRREF': R(self.bank_guid), '_CODE': 'B1', '_DESCRIPTION': 'Банк', '_FOLDER': True})]))
        self.other_guid, self.missing_guid = v(3001), v(3999)
        tables.append(fake.FixtureTable('_Reference102', bank_f, [fake.encode_row(
            bank_f, {'_IDRREF': R(self.other_guid), '_CODE': 'P1', '_DESCRIPTION': 'Вытянутый', '_FOLDER': True})]))
        enum_f = [F('_IDRREF', 'B', length=16), F('_ENUMORDER', 'N', length=10)]
        tables.append(fake.FixtureTable('_Enum200', enum_f, [
            fake.encode_row(enum_f, {'_IDRREF': R(guid), '_ENUMORDER': order})
            for order, guid in enumerate((ENUM_A, ENUM_B, ENUM_C))]))
        # document with a tabular section
        doc_f = [F('_IDRREF', 'B', length=16), F('_VERSION', 'RV'), F('_MARKED', 'L'), F('_DATE_TIME', 'DT'), F('_NUMBER', 'NVC', length=11),
                 F('_POSTED', 'L'), F('_FLD3001RREF', 'B', length=16), F('_FLD3002RREF', 'B', length=16), F('_FLD3003RREF', 'B', length=16)]
        self.doc_guid = v(4001)
        tables.append(fake.FixtureTable('_Document300', doc_f, [fake.encode_row(doc_f, {
            '_IDRREF': R(self.doc_guid), '_DATE_TIME': '2026-01-02 03:04:05', '_NUMBER': '0001', '_POSTED': True,
            '_FLD3001RREF': R(k1), '_FLD3002RREF': R(ENUM_A), '_FLD3003RREF': R(self.other_guid)})]))
        dvt_f = [F('_DOCUMENT300_IDRREF', 'B', length=16), F('_KEYFIELD', 'B', length=4), F('_LINENO3011', 'N', length=5),
                 F('_FLD3012', 'N', length=15, precision=2)]
        tables.append(fake.FixtureTable('_Document300_VT3010', dvt_f, [fake.encode_row(dvt_f, {
            '_DOCUMENT300_IDRREF': R(self.doc_guid), '_LINENO3011': n, '_FLD3012': a}) for n, a in ((2, '-5.00'), (1, '7.25'))]))
        # chart of accounts with ext dimensions and subkonto kinds
        acc_f = [F('_IDRREF', 'B', length=16), F('_VERSION', 'RV'), F('_MARKED', 'L'), F('_PREDEFINEDID', 'B', length=16),
                 F('_PARENTIDRREF', 'B', length=16), F('_CODE', 'NVC', length=8), F('_DESCRIPTION', 'NVC', length=50),
                 F('_ORDERFIELD', 'NVC', length=8), F('_KIND', 'N', length=1), F('_OFFBALANCE', 'L'),
                 F('_FLD879', 'L'), F('_FLD880', 'L')]
        self.a_cash, self.a_goods, self.a_sales = v(5001), v(5002), v(5003)
        tables.append(fake.FixtureTable('_Acc31', acc_f, [
            fake.encode_row(acc_f, {'_IDRREF': R(self.a_cash), '_PREDEFINEDID': R(v(77)), '_CODE': '50', '_DESCRIPTION': 'Касса',
                                    '_ORDERFIELD': '50', '_KIND': 0, '_FLD879': True}),
            fake.encode_row(acc_f, {'_IDRREF': R(self.a_goods), '_CODE': '41', '_DESCRIPTION': 'Товары', '_ORDERFIELD': '41',
                                    '_KIND': 2, '_FLD880': True}),
            fake.encode_row(acc_f, {'_IDRREF': R(self.a_sales), '_CODE': '90', '_DESCRIPTION': 'Продажи', '_ORDERFIELD': '90',
                                    '_KIND': 1, '_OFFBALANCE': True})]))
        self.kind_a, self.kind_b = v(6001), v(6002)
        ext_f = [F('_ACC31_IDRREF', 'B', length=16), F('_KEYFIELD', 'B', length=4), F('_LINENO', 'N', length=5),
                 F('_DIMKINDRREF', 'B', length=16), F('_DIMISMETADATA', 'L'), F('_TURNOVERONLY', 'L'),
                 F('_FLD884', 'L'), F('_FLD885', 'L'), F('_FLD886', 'L')]
        tables.append(fake.FixtureTable('_Acc31_ExtDim883', ext_f, [
            fake.encode_row(ext_f, {'_ACC31_IDRREF': R(self.a_goods), '_LINENO': 2, '_DIMKINDRREF': R(self.kind_b), '_FLD884': True}),
            fake.encode_row(ext_f, {'_ACC31_IDRREF': R(self.a_goods), '_LINENO': 1, '_DIMKINDRREF': R(self.kind_a),
                                    '_TURNOVERONLY': True, '_FLD886': True})]))
        chrc_f = [F('_IDRREF', 'B', length=16), F('_VERSION', 'RV'), F('_MARKED', 'L'), F('_PREDEFINEDID', 'B', length=16),
                  F('_CODE', 'NVC', length=5), F('_DESCRIPTION', 'NVC', length=50), F('_TYPE', 'I', null_exists=True)]
        blob = fake.FixtureTable('_Chrc400', chrc_f)
        text = ('﻿{"#",f5c65050,{"Pattern",{"#",%s},{"S",25,1}}}' % TYPEG[100]).encode('utf-8')
        payload = b'\x01\x01' + len(text).to_bytes(8, 'little') + text
        blob.blobs[1] = (0, payload)
        for guid, code, name in ((self.kind_a, 'K1', 'Контрагенты'), (self.kind_b, 'K2', 'Прочее')):
            blob.rows.append(fake.encode_row(chrc_f, {'_IDRREF': R(guid), '_CODE': code, '_DESCRIPTION': name, '_TYPE': (1, len(payload))}))
        tables.append(blob)
        # accounting register: three movements, two sides, per-side NULL vs zero, negative amount
        reg_f = [F('_PERIOD', 'DT'), F('_RECORDERTREF', 'B', length=4), F('_RECORDERRREF', 'B', length=16), F('_LINENO', 'N', length=9),
                 F('_ACTIVE', 'L'), F('_ACCOUNTDTRREF', 'B', length=16), F('_ACCOUNTCTRREF', 'B', length=16),
                 F('_FLD601RREF', 'B', length=16), F('_FLD602', 'N', length=15, precision=2),
                 F('_FLD603DT', 'N', length=15, precision=3, null_exists=True), F('_FLD603CT', 'N', length=15, precision=3, null_exists=True),
                 F('_FLD604DTRREF', 'B', length=16, null_exists=True), F('_FLD604CTRREF', 'B', length=16, null_exists=True),
                 F('_EDHASHDT', 'N', length=10), F('_EDHASHCT', 'N', length=10)]

        def mv(line, dt, ct, amount, active=True, **kw):
            return fake.encode_row(reg_f, {'_PERIOD': '2026-01-0%d 00:00:00' % line, '_RECORDERTREF': T(300), '_RECORDERRREF': R(self.doc_guid),
                                           '_LINENO': line, '_ACTIVE': active, '_ACCOUNTDTRREF': R(dt), '_ACCOUNTCTRREF': R(ct),
                                           '_FLD601RREF': R(k2), '_FLD602': amount, **kw})
        tables.append(fake.FixtureTable('_AccRg500', reg_f, [
            mv(1, self.a_goods, self.a_cash, '-150.25', _FLD603DT='0.000', _FLD604DTRREF=R(self.bank_guid)),
            mv(2, self.a_cash, self.a_sales, '10.00', _FLD603CT='3.500', _FLD604CTRREF=R(self.missing_guid)),
            mv(3, self.a_cash, self.a_sales, '1.00', active=False)]))
        ed_f = [F('_PERIOD', 'DT'), F('_RECORDERTREF', 'B', length=4), F('_RECORDERRREF', 'B', length=16), F('_LINENO', 'N', length=9),
                F('_CORRESPOND', 'N', length=1), F('_KINDRREF', 'B', length=16), F('_VALUE_TYPE', 'B', length=1),
                F('_VALUE_RTREF', 'B', length=4), F('_VALUE_RRREF', 'B', length=16)]

        def ed(line, side, kind, vtype, rtref=b'\x00' * 4, rref=b'\x00' * 16):
            return fake.encode_row(ed_f, {'_PERIOD': '2026-01-0%d 00:00:00' % line, '_RECORDERTREF': T(300), '_RECORDERRREF': R(self.doc_guid),
                                          '_LINENO': line, '_CORRESPOND': side, '_KINDRREF': R(kind), '_VALUE_TYPE': vtype,
                                          '_VALUE_RTREF': rtref, '_VALUE_RRREF': rref})
        tables.append(fake.FixtureTable('_AccRgED501', ed_f, [
            ed(1, 0, self.kind_b, b'\x01'), ed(1, 0, self.kind_a, b'\x08', T(100), R(k2))]))
        info_f = [F('_IDRREF', 'B', length=16)]
        tables.append(fake.FixtureTable('_InfoRg900', info_f, [fake.encode_row(info_f, {'_IDRREF': R(v(9))})]))
        fake.write_fake_1cd(self.base, tables)
        self.k = (k1, k2, k3, k4)

    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), str(self.base), 'export-stage', '--output', str(self.out), *args],
                              text=True, capture_output=True)

    def export(self):
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.result = result
        return ET.parse(self.out).getroot()


class StageExportTest(StageFixture):
    def test_header_and_sections(self):
        root = self.export()
        self.assertEqual(root.get('format'), 'orion-1cd-stage-v1')
        source = root.find('header/source')
        self.assertEqual(int(source.get('size')), self.base.stat().st_size)
        self.assertEqual(len(source.get('sha256')), 64)
        self.assertEqual(root.find('header/configuration').attrib, {'name': 'Тестовая', 'version': '1.2.3.4'})
        self.assertEqual(len(root.find('chart-of-accounts')), 3)
        self.assertEqual(len(root.find('subkonto-kinds')), 2)

    def test_account_slots_flags_and_predefined(self):
        root = self.export()
        accounts = {a.get('code'): a for a in root.find('chart-of-accounts')}
        cash, goods, sales = accounts['50'], accounts['41'], accounts['90']
        self.assertEqual((cash.get('kind'), goods.get('kind'), sales.get('kind')), ('active', 'active-passive', 'passive'))
        self.assertEqual((cash.get('currency'), cash.get('quantity')), ('true', 'false'))
        self.assertEqual((goods.get('currency'), goods.get('quantity')), ('false', 'true'))
        self.assertEqual((sales.get('off-balance'), cash.get('off-balance')), ('true', 'false'))
        self.assertEqual((cash.get('predefined'), cash.get('predefined-id')), ('Касса', G(77)))
        slots = goods.findall('subkonto')
        self.assertEqual([s.get('n') for s in slots], ['1', '2'])
        self.assertEqual([s.get('kind') for s in slots], [self.kind_a, self.kind_b])
        self.assertEqual(slots[0].attrib['turnover-only'], 'true')
        self.assertEqual((slots[0].get('quantity'), slots[0].get('amount')), ('true', 'false'))
        self.assertEqual((slots[1].get('quantity'), slots[1].get('amount'), slots[1].get('turnover-only')), ('false', 'true', 'false'))
        kinds = root.find('subkonto-kinds')
        types = kinds[0].findall('types/type')
        self.assertEqual([t.attrib for t in types], [{'t': 'ref', 'type': 'Справочник.Контрагенты'}, {'t': 'string', 'length': '25'}])

    def test_movements_multi_subkonto_null_zero_negative(self):
        root = self.export()
        movements = root.find('registers/register')
        self.assertEqual(len(movements), 3)
        m1, m2, m3 = movements
        self.assertEqual((m1.get('recorder'), m1.get('recorder-type'), m1.get('line')), (self.doc_guid, 'Документ.Приход', '1'))
        self.assertEqual(m1.find('v[@n="Сумма"]').text, '-150.25')
        debit, credit = m1.find('debit'), m1.find('credit')
        self.assertEqual(debit.get('account'), self.a_goods)
        self.assertEqual(debit.find('v[@n="Количество"]').attrib, {'n': 'Количество', 't': 'decimal'})
        self.assertEqual(debit.find('v[@n="Количество"]').text, '0.000')
        self.assertEqual(credit.find('v[@n="Количество"]').get('t'), 'null')
        subs = debit.findall('subkonto')
        self.assertEqual([(s.get('n'), s.get('kind'), s.get('t')) for s in subs],
                         [('1', self.kind_a, 'ref'), ('2', self.kind_b, 'null')])
        self.assertEqual((subs[0].get('type'), subs[0].get('id')), ('Справочник.Контрагенты', self.k[1]))
        self.assertEqual(credit.findall('subkonto'), [])
        self.assertEqual(m2.find('credit/v[@n="Количество"]').text, '3.500')
        self.assertEqual(m2.find('debit/v[@n="Количество"]').get('t'), 'null')
        self.assertEqual(m3.get('active'), 'false')
        technical = [e for e in root.iter('v') if e.get('n', '').startswith('_EDHASH')]
        self.assertEqual(technical, [])

    def test_composite_enum_and_blob_attributes(self):
        root = self.export()
        records = {r.get('code'): r for r in root.find('records') if r.get('type') == 'Справочник.Контрагенты'}
        alpha, beta, gamma, group = records['1'], records['2'], records['3'], records['4']
        self.assertEqual(alpha.find('v[@n="Контакт"]').attrib, {'n': 'Контакт', 't': 'string'})
        self.assertEqual(alpha.find('v[@n="Контакт"]').text, 'строка')
        self.assertEqual(alpha.find('v[@n="Сальдо"]').text, '-1234.50')
        self.assertEqual(beta.find('v[@n="Сальдо"]').text, '0.00')
        self.assertEqual(beta.find('v[@n="Контакт"]').attrib,
                         {'n': 'Контакт', 't': 'ref', 'type': 'Справочник.Банки', 'id': self.bank_guid})
        self.assertEqual(gamma.find('v[@n="Контакт"]').text, '12.34')
        self.assertEqual(gamma.find('v[@n="Контакт"]').get('t'), 'decimal')
        self.assertEqual(group.find('v[@n="Контакт"]').attrib, {'n': 'Контакт', 't': 'null', 'undefined': 'true'})
        self.assertEqual((group.get('folder'), alpha.get('folder'), group.get('deleted'), group.get('parent')),
                         ('true', 'false', 'true', self.k[0]))
        blob = alpha.find('v[@n="Файл"]')
        self.assertEqual(blob.get('t'), 'value-storage')
        self.assertIn('87126200-3e98-44e0-b931-ccb1d7edc497', blob.findtext('serialized'))
        self.assertEqual(blob.find('segment').attrib, {'encoding': 'base64', 'bytes': '3'})
        self.assertEqual(blob.findtext('segment'), 'YWJj')
        self.assertEqual(beta.find('v[@n="Файл"]').get('t'), 'null')
        self.assertEqual(alpha.find('v[@n="НеизвестныйТип"]').attrib,
                         {'n': 'НеизвестныйТип', 't': 'ref', 'empty': 'true'})
        self.assertEqual(gamma.find('v[@n="НеизвестныйТип"]').get('t'), 'undecoded')
        enum = alpha.find('v[@n="Вид"]')
        self.assertEqual(enum.attrib, {'n': 'Вид', 't': 'enum', 'type': 'Перечисление.Виды', 'name': 'Второй', 'id': ENUM_B})
        listed = {(e.get('name'), e.get('order')) for e in root.find('enums')}
        self.assertEqual(listed, {('Первый', '0'), ('Второй', '1'), ('Третий', '2')})
        constant = root.find('constants/constant')
        self.assertEqual(constant.attrib, {'type': 'Константа.ОкруглятьИтоги', 'key': '00000001'})
        self.assertEqual(constant.findtext('v[@n="Значение"]'), 'true')

    def test_compressed_value_storage(self):
        source = b'\xef\xbb\xbf{}'
        body = struct.pack('<Q', len(source)) + source + struct.pack('<IQ', 0, 2) + b'xy'
        header = b'\x02\x01' + bytes.fromhex('534b6ff4888dc14ea0d5ebb6bda0a70d')
        compressed = zlib.compress(body)[2:-4]
        self.assertEqual(decode_value_storage(header + compressed), ('{}', [b'xy']))
        with self.assertRaises(ValueError):
            decode_value_storage(header + compressed[:-1])
        with self.assertRaises(ValueError):
            decode_value_storage(b'\x01\x01' + body[:-1])

    def test_dangling_pulled_and_missing(self):
        root = self.export()
        pulled = [r for r in root.find('records') if r.get('type') == 'Справочник.Прочее']
        self.assertEqual(len(pulled), 1)
        self.assertEqual((pulled[0].get('pulled'), pulled[0].get('id')), ('true', self.other_guid))
        doc = root.find('documents/document')
        self.assertEqual(doc.find('v[@n="Прочее"]').get('id'), self.other_guid)
        self.assertIsNone(doc.find('v[@n="Прочее"]').get('dangling'))
        missing = root.find('registers/register')[1].find('credit/v[@n="Валюта"]')
        self.assertEqual((missing.get('dangling'), missing.get('id')), ('true', self.missing_guid))
        self.assertEqual(root.find('summary/dangling').get('count'), '1')
        self.assertIn('dangling references: 1', self.result.stdout)

    def test_every_reference_resolves_or_is_flagged(self):
        root = self.export()
        ids = {e.get('id') for name in ('chart-of-accounts', 'subkonto-kinds', 'records', 'documents', 'enums') for e in root.find(name)}
        unresolved = [e for e in root.iter() if e.get('t') in ('ref', 'enum') and e.get('id') and e.get('id') not in ids]
        self.assertEqual([e.get('id') for e in unresolved], [self.missing_guid])
        self.assertEqual(unresolved[0].get('dangling'), 'true')

    def test_tabular_sections_in_line_order(self):
        root = self.export()
        doc = root.find('documents/document')
        self.assertEqual((doc.get('number'), doc.get('date'), doc.get('posted')), ('0001', '2026-01-02T03:04:05', 'true'))
        rows = doc.findall('table[@n="Товары"]/row')
        self.assertEqual([(r.get('line'), r.find('v').text) for r in rows], [('1', '7.25'), ('2', '-5.00')])
        alpha = next(r for r in root.find('records') if r.get('code') == '1' and r.get('type') == 'Справочник.Контрагенты')
        self.assertEqual([r.find('v').text for r in alpha.findall('table[@n="Контакты"]/row')], ['первая', 'вторая', 'третья'])

    def test_skipped_tables_listed(self):
        root = self.export()
        names = {t.get('name'): t for t in root.find('skipped')}
        self.assertIn('_InfoRg900', names)
        self.assertEqual(names['_InfoRg900'].get('rows'), '1')
        self.assertIn('CONFIG', names)
        self.assertNotIn('_Document300', names)

    def test_undecoded_marker_and_summary(self):
        root = self.export()
        marks = [e for e in root.iter('v') if e.get('t') == 'undecoded']
        self.assertEqual({(mark.get('n'), mark.get('reason')) for mark in marks}, {
            ('Контакт', 'unsupported value type code 9'),
            ('НеизвестныйТип', 'reference target type unknown')})
        self.assertEqual(root.find('summary/undecoded').get('count'), '2')
        self.assertIn('undecoded markers: 2', self.result.stdout)
        self.assertIn('unsupported value type code 9', self.result.stdout)


class StageCliTest(StageFixture):
    def test_deterministic_and_source_untouched(self):
        original = self.base.read_bytes()
        self.assertEqual(self.run_tool().returncode, 0)
        first = self.out.read_bytes()
        self.assertEqual(self.run_tool('--overwrite').returncode, 0)
        self.assertEqual(self.out.read_bytes(), first)
        self.assertEqual(self.base.read_bytes(), original)
        self.assertFalse(list(Path(self.tmp.name).glob('*.tmp')))

    def test_refuses_to_overwrite(self):
        self.out.write_text('precious')
        result = self.run_tool()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--overwrite', result.stderr)
        self.assertEqual(self.out.read_text(), 'precious')
        self.assertEqual(self.run_tool('--overwrite').returncode, 0)
        self.assertTrue(self.out.read_text().startswith('<?xml'))

    def test_refuses_source_as_output(self):
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.base), 'export-stage', '--output', str(self.base), '--overwrite'],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.base.read_bytes().startswith(b'1CDBMSV8'))

    def test_decimal_text_is_exact(self):
        root = self.export()
        text = self.out.read_text(encoding='utf-8')
        self.assertIn('>-1234.50<', text)
        self.assertNotIn('e+', text)
        self.assertEqual(ref_uuid(R(G(5))), G(5))




class AttachmentManifestTest(unittest.TestCase):
    def test_external_catalog_stages_manifest_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = b'abc'
            (root / 'file.bin').write_bytes(data)
            file_ref, volume_ref = b'\x11' * 16, b'\x22' * 16
            info = SimpleNamespace(number=1, name='Справочник.ПрисоединенныеФайлы', short_name='ПрисоединенныеФайлы')
            entry = {'type': info.name, 'id': ref_uuid(file_ref), 'volume_id': ref_uuid(volume_ref),
                     'path': 'file.bin', 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
            path = root / 'manifest.json'
            path.write_text(json.dumps({'format': 'orion-1cd-attachments-v1', 'source_sha256': 'source',
                                        'attachments': [entry]}), encoding='utf-8')
            table = SimpleNamespace(fields={'_FLD1': 'volume', '_DESCRIPTION': 'name'})
            exporter = Exporter.__new__(Exporter)
            exporter.meta = SimpleNamespace(objects={('Reference', 1): info}, object_table=lambda _: table)
            exporter.nonempty = lambda _: True
            exporter.field_name = lambda _info, _number: 'Том'
            exporter.index = lambda _: {file_ref: b'row'}
            exporter.primitive = lambda *_args: ({'t': 'string'}, 'file')
            exporter.cell = lambda *_args: (False, volume_ref)
            exporter.note = lambda *_args: None
            exporter.attachment_manifest = None
            missing, = exporter.export_attachments().kids
            self.assertIn('unavailable', missing.attrs)
            exporter.attachment_manifest = AttachmentManifest(path, 'source')
            attached, = exporter.export_attachments().kids
            self.assertEqual(attached.attrs['volume-id'], ref_uuid(volume_ref))
            self.assertEqual(attached.attrs['sha256'], entry['sha256'])
            self.assertEqual(attached.attrs['size'], '3')
            self.assertEqual(attached.text, 'YWJj')

    def test_verified_file_and_rejections(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'data').mkdir()
            (root / 'data' / 'file').write_bytes(b'abc')
            entry = {'type': 'Справочник.ПрисоединенныеФайлы', 'id': G(1), 'volume_id': G(2),
                     'path': 'data/file', 'size': 3, 'sha256': hashlib.sha256(b'abc').hexdigest()}
            manifest = root / 'manifest.json'
            def load():
                manifest.write_text(json.dumps({'format': 'orion-1cd-attachments-v1',
                    'source_sha256': 'source', 'attachments': [entry]}))
                return AttachmentManifest(manifest, 'source')
            attrs = {'type': entry['type'], 'id': G(1), 'volume-id': G(2), 'size': '3'}
            mapping = load()
            self.assertEqual(mapping.read(attrs), (b'abc', ''))
            mapping.check_output(root / 'stage.xml')
            with self.assertRaises(ValueError): mapping.check_output(manifest)
            with self.assertRaises(ValueError): mapping.check_output(root / 'data' / 'file')
            self.assertIsNone(mapping.read({**attrs, 'volume-id': G(3)})[0])
            (root / 'data' / 'file').write_bytes(b'abd')
            self.assertIsNone(mapping.read(attrs)[0])
            (root / 'data' / 'file').unlink()
            self.assertIsNone(mapping.read(attrs)[0])
            (root / 'target').write_bytes(b'abc')
            try:
                (root / 'data' / 'file').symlink_to(root / 'target')
            except OSError:
                pass  # Some Windows runners lack symlink creation privileges.
            else:
                self.assertIsNone(mapping.read(attrs)[0])
            entry['path'] = '../outside'
            with self.assertRaises(ValueError): load()
            entry['path'] = '/absolute'
            with self.assertRaises(ValueError): load()
            entry['path'] = 'C:/absolute'
            with self.assertRaises(ValueError): load()
            entry['path'] = 'data/file'
            with self.assertRaises(ValueError): AttachmentManifest(manifest, 'other-source')

if __name__ == '__main__':
    unittest.main()
