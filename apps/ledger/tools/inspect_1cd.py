#!/usr/bin/env python3
"""Experimental, read-only 1CD catalog extraction for Ledger."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import uuid
import xml.etree.ElementTree as ET
from decimal import Decimal

from vendor.onecreader.raw1cd import Database1CD, decode_datetime, decode_nvc
from vendor.onecreader import bracket
from onecd_metadata import accounting_register_map, catalog_map, read_files, walk


def get_field(table, row, name, kind):
    f = table.fields.get(name)
    if not f or f.type != kind or f.null_exists:
        raise ValueError(f'{table.name}: expected non-null {name} of type {kind}')
    data = row[f.offset:f.offset + f.size]
    if len(data) != f.size:
        raise ValueError(f'{table.name}: truncated {name}')
    return data


def text_field(table, row, name):
    field = table.fields.get(name)
    if field and field.type == 'NC':
        return get_field(table, row, name, 'NC').decode('utf-16-le').strip(' \x00')
    data = get_field(table, row, name, 'NVC')
    size = int.from_bytes(data[:2], 'little')
    if size > (len(data) - 2) // 2:
        raise ValueError(f'{table.name}: invalid {name} length')
    return decode_nvc(data).strip()


def flag(table, row, name):
    data = get_field(table, row, name, 'L')
    if data[0] not in (0, 1):
        raise ValueError(f'{table.name}: invalid {name} flag')
    return bool(data[0])


def export_catalog(db, table_name, kind):
    if not re.fullmatch(r'_Reference[0-9]+', table_name, re.IGNORECASE):
        raise ValueError('table must be named _ReferenceN')
    table = next((t for n, t in db.tables.items() if n.lower() == table_name.lower()), None)
    if not table or not table.data_page:
        raise ValueError(f'table missing or has no data object: {table_name}')
    raw = db.read_object(table.data_page)
    if len(raw) % table.row_length:
        raise ValueError('truncated table data')
    rows, skipped, seen_ids, seen_names, seen_codes = [], 0, set(), set(), set()
    for offset in range(0, len(raw), table.row_length):
        row = raw[offset:offset + table.row_length]
        if row[0] == 1:  # free/deleted physical record
            skipped += 1
            continue
        if row[0] != 0:
            raise ValueError(f'unknown row state at byte {offset}: {row[0]}')
        ref = get_field(table, row, '_IDRREF', 'B')
        if len(ref) != 16 or not any(ref):
            raise ValueError(f'invalid object reference at byte {offset}')
        if flag(table, row, '_MARKED') or not flag(table, row, '_FOLDER'):
            skipped += 1
            continue
        name = text_field(table, row, '_DESCRIPTION')
        code = text_field(table, row, '_CODE')
        if not name or len(name.encode()) > 159 or (kind == 'items' and len(code.encode()) > 15):
            raise ValueError(f'empty or too-long name/code at byte {offset}')
        if ref in seen_ids or name in seen_names or (kind == 'items' and code and code in seen_codes):
            raise ValueError(f'duplicate identity, name or item code at byte {offset}')
        seen_ids.add(ref); seen_names.add(name); seen_codes.add(code)
        rows.append((str(uuid.UUID(bytes_le=ref)), name, code))
    if not rows:
        raise ValueError('no importable records')
    root = ET.Element('Message')
    header = ET.SubElement(root, 'Header')
    ET.SubElement(header, 'Format').text = 'EnterpriseData (Ledger experimental 1CD catalog extraction)'
    body = ET.SubElement(root, 'Body')
    for ref, name, code in rows:
        obj = ET.SubElement(body, 'Справочник.Номенклатура' if kind == 'items' else 'Справочник.Контрагенты')
        key = ET.SubElement(obj, 'КлючевыеСвойства')
        ET.SubElement(key, 'Ссылка').text = ref
        ET.SubElement(key, 'Наименование').text = name
        if kind == 'items' and code:
            ET.SubElement(key, 'КодВПрограмме').text = code
    return root, len(rows), skipped


def accounting_register_tables(db):
    """Return physical accounting-register schemas, without reading their rows."""
    return [(name, table) for name, table in sorted(db.tables.items(), key=lambda item: item[0].casefold())
            if re.fullmatch(r'_AccRg[0-9]+', name, re.IGNORECASE)]


def decode_numeric_decimal(data, length, precision):
    """Decode the reader's packed decimal format without binary float conversion."""
    if len(data) != length // 2 + 1 or precision < 0 or precision > length:
        raise ValueError('invalid packed-decimal field dimensions')
    digits = data.hex()
    if not digits or digits[0] not in '01':
        raise ValueError('invalid packed-decimal sign nibble')
    value, padding = digits[1:1 + length], digits[1 + length:]
    if len(value) != length or any(ch not in '0123456789' for ch in value):
        raise ValueError('invalid packed-decimal digit')
    if any(ch != '0' for ch in padding):
        raise ValueError('invalid packed-decimal padding')
    number = Decimal(int(value)).scaleb(-precision)
    return -number if digits[0] == '0' and number else number


def validate_field_schema(table, name, kind, length=None, size=None, nullable=False):
    field = table.fields.get(name)
    if not field or field.type != kind or (field.null_exists and not nullable):
        raise ValueError(f'{table.name}: expected {"nullable " if nullable else "non-null "}{name} of type {kind}')
    if length is not None and field.length != length:
        raise ValueError(f'{table.name}: unexpected length for {name}: {field.length}, expected {length}')
    if size is not None and field.size != size:
        raise ValueError(f'{table.name}: unexpected storage size for {name}: {field.size}, expected {size}')
    return field


def required_field(table, row, name, kind):
    field = validate_field_schema(table, name, kind)
    data = row[field.offset:field.offset + field.size]
    if len(data) != field.size:
        raise ValueError(f'{table.name}: truncated {name}')
    return data


def numeric_field_value(table, row, name):
    field = validate_field_schema(table, name, 'N', nullable=True)
    data = row[field.offset:field.offset + field.size]
    if len(data) != field.size:
        raise ValueError(f'{table.name}: truncated {name}')
    if field.null_exists:
        if data[0] not in (0, 1):
            raise ValueError(f'{table.name}: invalid null flag for {name}')
        if not data[0]:
            return None
        data = data[1:]
    return decode_numeric_decimal(data, field.length, field.precision)


def register_rows(db, table):
    data = db.read_object(table.data_page) if table.data_page else b''
    if len(data) % table.row_length:
        raise ValueError(f'{table.name}: truncated rows')
    rows = []
    for offset in range(0, len(data), table.row_length):
        row = data[offset:offset + table.row_length]
        if row[0] == 1:
            continue
        if row[0] != 0:
            raise ValueError(f'{table.name}: invalid row state at byte {offset}')
        rows.append(row)
    return len(data) // table.row_length, rows


def reference_table_index(db, table_name):
    table = next((t for n, t in db.tables.items() if n.casefold() == table_name.casefold()), None)
    if table is None:
        raise ValueError(f'missing reference table {table_name}')
    field = validate_field_schema(table, '_IDRREF', 'B', length=16)
    data = db.read_object(table.data_page) if table.data_page else b''
    if len(data) % table.row_length:
        raise ValueError(f'{table.name}: truncated rows')
    live, deleted = Counter(), Counter()
    slots = 0
    for offset in range(0, len(data), table.row_length):
        row = data[offset:offset + table.row_length]
        state = row[0]
        if state not in (0, 1):
            raise ValueError(f'{table.name}: invalid physical row state at byte {offset}')
        slots += 1
        ref = row[field.offset:field.offset + field.size]
        if len(ref) != 16:
            raise ValueError(f'{table.name}: truncated _IDRREF at byte {offset}')
        if not any(ref):
            continue
        (live if state == 0 else deleted)[ref] += 1
    return table, live, deleted, slots


def validate_source_references(db, table, rows, register_guid):
    params = bracket.loads_file(read_files(db, 'PARAMS', {'DBNames'})['DBNames'])
    entries = [node for node in walk(params) if len(node) == 3]
    metadata = bracket.loads_file(read_files(db, 'CONFIG', {register_guid})[register_guid])
    metadata_guids = {str(value).lower() for node in walk(metadata) for value in node
                      if isinstance(node, list)}
    account_entries = [(str(node[0]).lower(), int(node[2])) for node in entries
                       if str(node[1]) == 'Acc' and str(node[2]).isdigit()]
    account_matches = [(guid, number) for guid, number in account_entries if guid in metadata_guids]
    if len(account_matches) != 1:
        raise ValueError('CONFIG/DBNames: account plan reference is missing or ambiguous')
    account_guid, account_number = account_matches[0]
    account_table_name = f'_Acc{account_number}'
    account_table, account_live, account_deleted, account_slots = reference_table_index(db, account_table_name)
    account_sides = {}
    for name, label in (('_ACCOUNTDTRREF', 'debit'), ('_ACCOUNTCTRREF', 'credit')):
        validate_field_schema(table, name, 'B', length=16)
        zero = resolved = deleted_only = missing = ambiguous = 0
        unique_refs = set()
        for row in rows:
            ref = required_field(table, row, name, 'B')
            if not any(ref):
                zero += 1
            else:
                unique_refs.add(ref)
                if account_live[ref] > 1:
                    ambiguous += 1
                elif account_live[ref] == 1:
                    resolved += 1
                elif account_deleted[ref]:
                    deleted_only += 1
                else:
                    missing += 1
        account_sides[label] = {'live_rows': len(rows), 'unique_nonzero_refs': len(unique_refs),
                                'zero_refs': zero, 'resolved_refs': resolved,
                                'deleted_only_refs': deleted_only, 'missing_refs': missing,
                                'ambiguous_refs': ambiguous}
    recorder_field = validate_field_schema(table, '_RECORDERTREF', 'B', length=4)
    validate_field_schema(table, '_RECORDERRREF', 'B', length=16)
    document_numbers = {}
    for node in entries:
        if str(node[1]) != 'Document' or not str(node[2]).isdigit():
            continue
        document_numbers.setdefault(int(node[2]), []).append(str(node[0]).lower())
    recorder_codes = Counter()
    recorder_pairs = set()
    for row in rows:
        code = int.from_bytes(required_field(table, row, '_RECORDERTREF', 'B'), 'big')
        ref = required_field(table, row, '_RECORDERRREF', 'B')
        recorder_codes[code] += 1
        if code and any(ref): recorder_pairs.add((code, ref))
    recorder_counts = {'live_rows': len(rows), 'unique_document_types': len([code for code in recorder_codes if code]),
                       'unique_nonzero_recorder_refs': len(recorder_pairs), 'resolved_refs': 0, 'zero_refs': 0,
                       'unknown_types': 0, 'ambiguous_types': 0, 'missing_tables': 0,
                       'missing_refs': 0, 'deleted_only_refs': 0, 'ambiguous_refs': 0,
                       'physical_document_tables': []}
    checked_tables = {}
    for code, occurrences in recorder_codes.items():
        if code == 0:
            recorder_counts['zero_refs'] += occurrences
            continue
        mappings = document_numbers.get(code, [])
        if not mappings:
            recorder_counts['unknown_types'] += occurrences
            continue
        if len(mappings) != 1:
            recorder_counts['ambiguous_types'] += occurrences
            continue
        doc_table_name = f'_Document{code}'
        try:
            doc_table, live, deleted, slots = checked_tables.get(code) or reference_table_index(db, doc_table_name)
        except ValueError as exc:
            if str(exc).startswith('missing reference table'):
                recorder_counts['missing_tables'] += occurrences
                continue
            raise
        checked_tables[code] = (doc_table, live, deleted, slots)
        recorder_counts['physical_document_tables'].append({'table': doc_table_name, 'type_number': code,
            'slots': slots, 'live_ids': len(live), 'deleted_only_ids': len(deleted),
            'duplicate_live_ids': sum(count > 1 for count in live.values())})
    for row in rows:
        code = int.from_bytes(required_field(table, row, '_RECORDERTREF', 'B'), 'big')
        ref = required_field(table, row, '_RECORDERRREF', 'B')
        if code == 0:
            continue
        mappings = document_numbers.get(code, [])
        if not mappings or len(mappings) != 1:
            continue
        target = checked_tables.get(code)
        if target is None:
            continue
        _, live, deleted, _ = target
        if not any(ref):
            recorder_counts['zero_refs'] += 1
        elif live[ref] > 1:
            recorder_counts['ambiguous_refs'] += 1
        elif live[ref] == 1:
            recorder_counts['resolved_refs'] += 1
        elif deleted[ref]:
            recorder_counts['deleted_only_refs'] += 1
        else:
            recorder_counts['missing_refs'] += 1
    recorder_counts['physical_document_tables'].sort(key=lambda item: item['type_number'])
    return {'account_plan': {'guid': account_guid, 'table': account_table_name,
                             'physical_slots': account_slots, 'live_ids': len(account_live),
                             'deleted_only_ids': len(account_deleted),
                             'duplicate_live_ids': sum(count > 1 for count in account_live.values()),
                             'debit': account_sides['debit'], 'credit': account_sides['credit']},
            'recorders': recorder_counts}


def audit_accounting_register(db, table_name):
    table = next((t for n, t in db.tables.items() if n.casefold() == table_name.casefold()), None)
    if table is None:
        raise ValueError(f'missing physical register table {table_name}')
    register_guid, metadata_fields = accounting_register_map(db, table_name)
    total_slots, rows = register_rows(db, table)
    validate_field_schema(table, '_ACTIVE', 'L', length=0)
    validate_field_schema(table, '_PERIOD', 'DT', size=7)
    validate_field_schema(table, '_LINENO', 'N', nullable=False)
    validate_field_schema(table, '_ACCOUNTDTRREF', 'B', length=16)
    validate_field_schema(table, '_ACCOUNTCTRREF', 'B', length=16)
    validate_field_schema(table, '_RECORDERTREF', 'B')
    validate_field_schema(table, '_RECORDERRREF', 'B')
    active = []
    zero_debit_refs = zero_credit_refs = invalid_periods = 0
    for row in rows:
        value = required_field(table, row, '_ACTIVE', 'L')[0]
        if value not in (0, 1):
            raise ValueError(f'{table.name}: invalid _ACTIVE value')
        if value:
            active.append(row)
        for name, side in (('_ACCOUNTDTRREF', 'debit'), ('_ACCOUNTCTRREF', 'credit')):
            ref = required_field(table, row, name, 'B')
            if not any(ref):
                if side == 'debit': zero_debit_refs += 1
                else: zero_credit_refs += 1
        period = required_field(table, row, '_PERIOD', 'DT')
        try:
            decoded = decode_datetime(period)
        except (ValueError, OverflowError):
            raise ValueError(f'{table.name}: invalid _PERIOD value')
        invalid_periods += decoded is None
        line_field = table.fields['_LINENO']
        decode_numeric_decimal(required_field(table, row, '_LINENO', 'N'), line_field.length, line_field.precision)
    resource_comparisons = {}
    validated_bcd_fields = ['_LINENO']
    for metadata_field in metadata_fields:
        if not metadata_field['columns']:
            raise ValueError(f"{table.name}: no physical columns for metadata field {metadata_field['name']}")
        by_suffix = {col.casefold()[len(f"_fld{metadata_field['number']}"):] : col
                     for col in metadata_field['columns']}
        debit_name = by_suffix.get('dt')
        credit_name = by_suffix.get('ct')
        if not debit_name or not credit_name:
            for col in metadata_field['columns']:
                field = table.fields.get(col)
                if field and field.type == 'N':
                    validated_bcd_fields.append(col)
                    for row in rows:
                        numeric_field_value(table, row, col)
            continue
        debit_field, credit_field = table.fields[debit_name], table.fields[credit_name]
        if debit_field.type != 'N' or credit_field.type != 'N':
            raise ValueError(f'{table.name}: unsupported resource columns {debit_name}/{credit_name}')
        unequal = 0
        null_mismatch = 0
        for row in rows:
            debit = numeric_field_value(table, row, debit_name)
            credit = numeric_field_value(table, row, credit_name)
        validated_bcd_fields.extend((debit_name, credit_name))
        for row in active:
            debit = numeric_field_value(table, row, debit_name)
            credit = numeric_field_value(table, row, credit_name)
            if debit != credit:
                unequal += 1
                if (debit is None) != (credit is None): null_mismatch += 1
        resource_comparisons[metadata_field['name']] = {
            'active_rows': len(active), 'unequal_rows': unequal,
            'unequal_fraction': (unequal / len(active)) if active else 0.0,
            'one_sided_null_rows': null_mismatch}
    params = bracket.loads_file(read_files(db, 'PARAMS', {'DBNames'})['DBNames'])
    ed_numbers = [str(node[2]) for node in walk(params)
                  if len(node) == 3 and str(node[0]).lower() == register_guid and str(node[1]) == 'AccRgED']
    subkonto = {'table': None, 'physical_slots': 0, 'live_rows': 0,
                'matched_rows': 0, 'orphan_rows': 0, 'ambiguous_rows': 0,
                'duplicate_parent_keys': 0}
    if len(ed_numbers) > 1:
        raise ValueError(f'DBNames: ambiguous AccRgED mapping for {table_name}')
    if ed_numbers:
        ed_name = f'_AccRgED{ed_numbers[0]}'
        ed_table = next((t for n, t in db.tables.items() if n.casefold() == ed_name.casefold()), None)
        if ed_table is None:
            raise ValueError(f'DBNames: missing physical subkonto table {ed_name}')
        subkonto['table'] = ed_name
        ed_slots, ed_rows = register_rows(db, ed_table)
        parent_fields = ('_PERIOD', '_RECORDERTREF', '_RECORDERRREF', '_LINENO')
        for name, kind, length, size in (('_PERIOD', 'DT', None, 7), ('_RECORDERTREF', 'B', None, None),
                                   ('_RECORDERRREF', 'B', 16, None), ('_LINENO', 'N', None, None)):
            parent_field = validate_field_schema(table, name, kind, length=length, size=size)
            ed_field = validate_field_schema(ed_table, name, kind, length=length, size=size)
            if (parent_field.type, parent_field.length, parent_field.precision, parent_field.size) != \
                    (ed_field.type, ed_field.length, ed_field.precision, ed_field.size):
                raise ValueError(f'{ed_name}: incompatible join-key schema for {name}')
        parent_counts = Counter()
        for row in rows:
            parent_counts[tuple(required_field(table, row, name, table.fields[name].type) for name in parent_fields)] += 1
        matched = orphan = ambiguous = 0
        for row in ed_rows:
            key = tuple(required_field(ed_table, row, name, ed_table.fields[name].type) for name in parent_fields)
            line_field = ed_table.fields['_LINENO']
            decode_numeric_decimal(key[-1], line_field.length, line_field.precision)
            owners = parent_counts[key]
            if owners == 0: orphan += 1
            elif owners == 1: matched += 1
            else: ambiguous += 1
        subkonto.update(physical_slots=ed_slots, live_rows=len(ed_rows), matched_rows=matched,
                        orphan_rows=orphan, ambiguous_rows=ambiguous,
                        duplicate_parent_keys=sum(count > 1 for count in parent_counts.values()))
    source_references = validate_source_references(db, table, rows, register_guid)
    return {'table': table_name, 'register_guid': register_guid, 'metadata_fields': metadata_fields,
            'physical_slots': total_slots, 'live_rows': len(rows), 'active_rows': len(active),
            'zero_debit_account_refs_live_rows': zero_debit_refs,
            'zero_credit_account_refs_live_rows': zero_credit_refs,
            'invalid_periods': invalid_periods, 'validated_bcd_fields': validated_bcd_fields,
            'subkonto': subkonto, 'source_references': source_references,
            'resource_side_comparisons': resource_comparisons}


def source_sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def stage_table(db, table, kind):
    data = db.read_object(table.data_page) if table.data_page else b''
    if len(data) % table.row_length:
        raise ValueError(f'{table.name}: truncated rows')
    states = [data[offset] for offset in range(0, len(data), table.row_length)]
    if any(state not in (0, 1) for state in states):
        raise ValueError(f'{table.name}: invalid physical row state')
    return {'name': table.name, 'kind': kind, 'row_length': table.row_length,
            'physical_slots': len(states), 'live_slots': states.count(0), 'deleted_slots': states.count(1),
            'fields': [{'name': f.name, 'type': f.type, 'offset': f.offset, 'size': f.size,
                        'length': f.length, 'precision': f.precision, 'nullable': f.null_exists}
                       for f in table.fields.values()],
            'data_object_bytes': len(data), 'data_object_hex': data.hex()}


def write_register_stage(db, table_name, output_path):
    output = Path(output_path)
    if output.resolve() == Path(db.path).resolve():
        raise ValueError('stage output must not be the 1CD source file')
    audit = audit_accounting_register(db, table_name)
    params = bracket.loads_file(read_files(db, 'PARAMS', {'DBNames'})['DBNames'])
    register_guid = audit['register_guid']
    ed_numbers = [str(node[2]) for node in walk(params)
                  if len(node) == 3 and str(node[0]).lower() == register_guid and str(node[1]) == 'AccRgED']
    primary = next(t for n, t in db.tables.items() if n.casefold() == table_name.casefold())
    tables = [stage_table(db, primary, 'AccRg')]
    if ed_numbers:
        ed_name = f'_AccRgED{ed_numbers[0]}'
        ed_table = next((t for n, t in db.tables.items() if n.casefold() == ed_name.casefold()), None)
        if ed_table is None:
            raise ValueError(f'DBNames: missing physical subkonto table {ed_name}')
        tables.append(stage_table(db, ed_table, 'AccRgED'))
    payload = {'format': 'orion-1cd-register-row-stage-v1',
               'scope': 'Raw _AccRg and _AccRgED data objects only; excludes blob/index objects, related tables, and Ledger projection.',
               'source': {'path': str(Path(db.path).resolve()), 'format_version': db.format_version,
                          'sha256': source_sha256(db.path)},
               'register': {'guid': register_guid, 'table': table_name,
                            'number': int(re.fullmatch(r'_AccRg([0-9]+)', table_name, re.IGNORECASE).group(1)),
                            'metadata_fields': audit['metadata_fields']},
               'tables': tables}
    encoded = (json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n').encode('utf-8')
    created = False
    fd = -1
    try:
        fd = os.open(output, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        created = True
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            fd = -1
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
    except Exception:
        if fd >= 0:
            os.close(fd)
        if created:
            try:
                output.unlink()
            except OSError:
                pass
        raise
    return len(encoded), payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', help='consistent offline copy of 1Cv8.1CD')
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('tables', help='list physical catalog tables; does not identify their meaning')
    sub.add_parser('catalogs', help='resolve catalog names and tables from CONFIG and DBNames')
    sub.add_parser('registers', help='list physical accounting-register schemas; does not export movements')
    audit = sub.add_parser('audit-register', help='inspect mapped register metadata and aggregate movement counts')
    audit.add_argument('table', help='physical accounting-register table, e.g. _AccRg891')
    stage = sub.add_parser('stage-register', help='stage exact physical register and subkonto row bytes for review')
    stage.add_argument('table', help='physical accounting-register table, e.g. _AccRg891')
    stage.add_argument('--output', required=True, help='new JSON staging file; existing files are never overwritten')
    extract = sub.add_parser('extract', help='extract an explicitly identified catalog for review')
    source = extract.add_mutually_exclusive_group(required=True)
    source.add_argument('--table', help='physical _ReferenceN table, independently verified')
    source.add_argument('--catalog', help='metadata name: Номенклатура or Контрагенты')
    extract.add_argument('--kind', choices=('items', 'counterparties'), help='required with --table')
    extract.add_argument('--output', required=True, help='new EnterpriseData-shaped XML file')
    args = parser.parse_args()
    db = None
    try:
        db = Database1CD(args.source)
        if args.command == 'tables':
            for name, table in sorted(db.tables.items()):
                if re.fullmatch(r'_Reference[0-9]+', name, re.IGNORECASE):
                    print(name, ', '.join(table.fields))
        elif args.command == 'catalogs':
            for name, guid, table in sorted(catalog_map(db).values()):
                print(f'{name}\t{guid}\t{table}')
        elif args.command == 'registers':
            registers = accounting_register_tables(db)
            if not registers:
                raise ValueError('no physical accounting-register tables found')
            print('Physical register tables only; field meanings require CONFIG metadata.')
            for name, table in registers:
                fields = ', '.join(f'{field.name}:{field.type}' for field in table.fields.values())
                print(f'{name}\t{fields}')
        elif args.command == 'audit-register':
            print(json.dumps(audit_accounting_register(db, args.table), ensure_ascii=False, indent=2))
        elif args.command == 'stage-register':
            size, payload = write_register_stage(db, args.table, args.output)
            table_count = len(payload['tables'])
            slot_count = sum(table['physical_slots'] for table in payload['tables'])
            print(f'Staged {table_count} physical tables / {slot_count} slots; '
                  f'output_bytes={size}; source_sha256={payload["source"]["sha256"]}')
        else:
            if args.catalog:
                if args.kind:
                    raise ValueError('--kind is only permitted with --table')
                key = args.catalog.casefold().removeprefix('справочник.')
                kinds = {'номенклатура': 'items', 'контрагенты': 'counterparties'}
                if key not in kinds:
                    raise ValueError('supported catalogs: Номенклатура, Контрагенты')
                name, guid, table = catalog_map(db)[key]
                print(f'CONFIG/DBNames: {name} ({guid}) → {table}', file=sys.stderr)
                kind = kinds[key]
            else:
                if not args.kind:
                    raise ValueError('--kind is required with --table')
                table, kind = args.table, args.kind
            root, count, skipped = export_catalog(db, table, kind)
            data = ET.tostring(root, encoding='utf-8', xml_declaration=True)
            # Exclusive creation avoids overwriting a source or a previous export.
            with open(args.output, 'xb') as output:
                output.write(data)
            print(f'Exported {count} records; skipped {skipped} deleted records/groups. Review {args.output} before importing.')
    except (OSError, ValueError, KeyError, IndexError, OverflowError, NotImplementedError, UnicodeError, struct.error) as exc:
        print(f'1CD extraction failed: {exc}', file=sys.stderr)
        return 1
    finally:
        if db:
            db.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
