#!/usr/bin/env python3
"""Reproduce synthetic ru/de infobases and EnterpriseData fixtures; no external data."""
import argparse
from copy import deepcopy
from datetime import date, timedelta
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'share' / 'samples'
PARTIES, ITEMS, DOCUMENTS, ENTRIES = 500, 2000, 1000, 10000


def child(parent, tag, text=None, **attrs):
    node = ET.SubElement(parent, tag, {key: str(value) for key, value in attrs.items()})
    node.text = text
    return node


def serialize(root):
    ET.indent(root, space='  ')
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


def build(country):
    de = country == 'de'
    config = ET.parse(ROOT / 'share' / 'configurations' / f'{country}-2003' / 'configuration.xml').getroot()
    accounts = {a.get('code'): a.get('id') for a in config.findall('./accounts/account')}
    types = {a.get('code'): a.get('id') for a in config.findall('./doc_types/doc_type')}
    bank, suppliers, customers, stock, offset = [accounts[c] for c in (
        ('1200', '1600', '1400', '3980', '9000') if de else ('51', '60.01', '62.01', '41.01', '000'))]
    revenue = accounts['8400' if de else '90.01']
    local = 'EUR' if de else 'RUB'
    ib = ET.Element('ib')
    ib.append(ET.Comment('Synthetic large example; all names and identifiers are invented.'))
    demo = ET.parse(ROOT / 'share' / 'configurations' / f'{country}-2003' / 'demo.xml').getroot()
    for table in ('organizations', 'employees', 'assets'):
        ib.append(deepcopy(demo.find(table)))
    parties = child(ib, 'counterparties')
    items = child(ib, 'items')
    currencies = child(ib, 'currencies')
    child(currencies, 'currency', id=1, code='USD', name='US-Dollar' if de else 'Доллар США')
    rates = child(ib, 'rates')
    child(rates, 'rate', id=1, currency_id=1, date='2003-01-01', rate='2.0')
    docs = child(ib, 'documents')
    entries = child(ib, 'entries')
    msg = ET.Element('Message', {'xmlns:msg': 'http://www.1c.ru/SSL/Exchange/Message'})
    msg.append(ET.Comment('Synthetic test data for Ledger; not an actual 1C or German accounting export.'))
    header = child(msg, 'msg:Header')
    child(header, 'msg:Format', 'http://v8.1c.ru/edi/edi_stnd/EnterpriseData/1.24')
    child(header, 'msg:CreationDate', '2003-01-01T00:00:00')
    body = child(msg, 'Body', xmlns='http://v8.1c.ru/edi/edi_stnd/EnterpriseData/1.24')
    for code in (local, 'USD'):
        cur = child(body, 'Справочник.Валюты')
        child(cur, 'БуквенныйКод', code)
        child(cur, 'Наименование', code)
    for i in range(1, PARTIES + 1):
        name = f'Testpartner {i:04d} GmbH' if de else f'ООО «Тестовый контрагент {i:04d}»'
        tax = f'TEST-DE-{i:06d}' if de else f'TEST-RU-{i:06d}'
        child(parties, 'counterparty', id=i, name=name, tax_id=tax, address='Synthetic / Тестовые данные')
        obj = child(body, 'Справочник.Контрагенты')
        child(obj, 'Ссылка', f'{country}-party-{i:06d}')
        child(obj, 'Наименование', name)
        child(obj, 'ИНН', tax)
    for i in range(1, ITEMS + 1):
        name = f'Testartikel {i:04d}' if de else f'Тестовый товар {i:04d}'
        sku = f'{country.upper()}-{i:06d}'
        child(items, 'item', id=i, name=name, sku=sku, unit_id=1, vat_rate_id=1, price='10.00')
        obj = child(body, 'Справочник.Номенклатура')
        child(obj, 'Ссылка', f'{country}-item-{i:06d}')
        child(obj, 'Наименование', name)
        child(obj, 'КодВПрограмме', sku)
        child(obj, 'СтавкаНДС', '16%' if de else '20%')
        unit = child(obj, 'ЕдиницаИзмерения')
        classifier = child(unit, 'ДанныеКлассификатора')
        child(classifier, 'Код', '796')
    tables = []
    for kind, table in [('ОстаткиБезналичныхДенежныхСредств', 'ДенежныеСредства'),
                        ('ОстаткиРасчетовСПоставщикамиДолги', 'Расчеты'),
                        ('ОстаткиРасчетовСПокупателямиДолги', 'Расчеты'),
                        ('ОстаткиСобственныхТМЦ', 'ТМЦ')]:
        obj = child(body, 'Документ.' + kind)
        child(obj, 'Дата', '2003-01-01T00:00:00')
        tables.append(child(obj, table))
    for i in range(DOCUMENTS):
        day = (date(2003, 1, 1) + timedelta(days=i % 365)).isoformat()
        child(docs, 'document', id=i + 1, doc_type_id=types['journal' if de else 'operation'], number=f'TEST-{i+1:06d}',
              date=day, comment=f'Synthetische Buchung {i+1}' if de else f'Тестовая операция {i+1}',
              posted=1, amount='1000.00', total='1000.00')
        for j in range(10):
            n = i * 10 + j
            party, item = (n // 4) % PARTIES + 1, (n // 4) % ITEMS + 1
            # Purchase, supplier payment, sale and customer payment (manual, no VAT).
            pattern = n % 4
            dr, cr = [(stock, suppliers), (suppliers, bank), (customers, revenue), (bank, customers)][pattern]
            attrs = dict(id=n+1, document_id=i+1, date=day, debit_id=dr, credit_id=cr,
                         amount='100.00', content='Synthetic fixture / Тестовые данные')
            if pattern == 0: attrs.update(debit_subconto_id=item, quantity='10')
            if pattern == 0: attrs['credit_subconto_id'] = party
            if pattern == 1: attrs['debit_subconto_id'] = party
            if pattern == 2: attrs['debit_subconto_id'] = party
            if pattern == 3: attrs['credit_subconto_id'] = party
            child(entries, 'entry', **attrs)
    for n in range(ENTRIES):
        group, i = divmod(n, ENTRIES // 4)
        row = child(tables[group], 'Строка')
        cents = 10000 + n
        amount = f'{cents//100}.{cents%100:02d}'
        child(row, 'Сумма', amount)
        if group == 0 and i % 10 == 0:
            currency = child(row, 'Валюта')
            child(currency, 'БуквенныйКод', 'USD')
            child(row, 'СуммаРегл', f'{cents*2//100}.{cents*2%100:02d}')
        if group in (1, 2):
            party = i % PARTIES + 1
            ref = child(row, 'Контрагент')
            child(ref, 'Ссылка', f'{country}-party-{party:06d}')
        if group == 3:
            item = i % ITEMS + 1
            ref = child(row, 'Номенклатура')
            child(ref, 'Ссылка', f'{country}-item-{item:06d}')
            child(row, 'Количество', '10')
    return {f'{country}-2003-large.xml': serialize(ib),
            f'{country}-2003-enterprisedata-large.xml': serialize(msg)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='fail if committed samples differ from generated data')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for country in ('ru', 'de'):
        for name, content in build(country).items():
            path = OUT / name
            if args.check:
                if not path.exists() or path.read_bytes() != content:
                    raise SystemExit(f'Out of date: {path}')
            else:
                path.write_bytes(content)
            print(f'{name}: {len(content)} bytes')


if __name__ == '__main__':
    main()
