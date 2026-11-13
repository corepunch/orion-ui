"""Resolve 1CD catalogs via CONFIG metadata GUIDs and PARAMS/DBNames."""
import re
import struct
import zlib

from vendor.onecreader import bracket
from vendor.onecreader.raw1cd import decode_nvc

CATALOG_CLASS = 'cf4abea6-37b2-11d4-940f-008048da11f9'
GUID = re.compile(r'^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$', re.I)


def walk(root):
    stack = [root]
    while stack:
        node = stack.pop()
        if isinstance(node, list):
            yield node
            stack.extend(reversed(node))


def read_files(db, name, wanted):
    table = db.tables[name]
    fields = table.fields
    for key, kind in (('FILENAME', 'NVC'), ('BINARYDATA', 'I')):
        if key not in fields or fields[key].type != kind or fields[key].null_exists:
            raise ValueError(f'{name}: unsupported {key} field')
    records = {}
    data = db.read_object(table.data_page)
    blob_data = None
    if len(data) % table.row_length:
        raise ValueError(f'{name}: truncated rows')
    for off in range(0, len(data), table.row_length):
        row = data[off:off + table.row_length]
        if row[0] == 1:
            continue
        if row[0] != 0:
            raise ValueError(f'{name}: unknown row state')
        f = fields['FILENAME']
        field = row[f.offset:f.offset + f.size]
        if int.from_bytes(field[:2], 'little') > (len(field) - 2) // 2:
            raise ValueError(f'{name}: invalid filename')
        filename = decode_nvc(field).strip()
        if not filename:
            raise ValueError(f'{name}: empty file name')
        if filename not in wanted:
            continue
        if filename in records:
            raise ValueError(f'{name}: duplicate file name {filename!r}')
        # Split files require explicit assembly; do not silently use one part.
        part = fields.get('PARTNO')
        if part:
            from vendor.onecreader.raw1cd import decode_numeric
            if part.type != 'N' or part.null_exists or decode_numeric(row[part.offset:part.offset + part.size], part.length, part.precision) != 0:
                raise ValueError(f'{name}: multipart file {filename!r} is unsupported')
        f = fields['BINARYDATA']
        start, size = struct.unpack('<II', row[f.offset:f.offset + f.size])
        if blob_data is None:
            blob_data = db.read_object(table.blob_page)
        blob = db.read_blob(table, start, size, object_data=blob_data)
        if len(blob) != size:
            raise ValueError(f'{name}: truncated file {filename!r}')
        try:
            blob = zlib.decompress(blob, -15)
        except zlib.error:
            pass
        records[filename] = blob
    return records


def catalog_map(db):
    files = read_files(db, 'CONFIG', {'root'})
    params = read_files(db, 'PARAMS', {'DBNames'})
    root = bracket.loads_file(files['root'])
    if len(root) < 2 or not GUID.fullmatch(str(root[1])):
        raise ValueError('CONFIG: invalid root metadata')
    main_guid = str(root[1])
    main = bracket.loads_file(read_files(db, 'CONFIG', {main_guid})[main_guid])
    collections = [n for n in walk(main) if len(n) >= 2 and str(n[0]).lower() == CATALOG_CLASS]
    if len(collections) != 1:
        raise ValueError('CONFIG: missing or ambiguous catalog collection')
    col = collections[0]
    if not str(col[1]).isdigit() or int(col[1]) != len(col) - 2:
        raise ValueError('CONFIG: invalid catalog collection size')
    files = read_files(db, 'CONFIG', {str(value).lower() for value in col[2:]})
    by_guid = {}
    for value in col[2:]:
        guid = str(value).lower()
        if not GUID.fullmatch(guid) or guid in by_guid:
            raise ValueError('CONFIG: duplicate or invalid catalog GUID')
        tree = bracket.loads_file(files[guid])
        headers = [n for n in walk(tree) if len(n) >= 4 and isinstance(n[1], list)
                   and len(n[1]) == 3 and str(n[1][0]) in ('0', '1')
                   and str(n[1][1]) == '0' and str(n[1][2]).lower() == guid
                   and isinstance(n[2], str)]
        if len(headers) != 1 or not str(headers[0][2]):
            raise ValueError(f'CONFIG: ambiguous catalog metadata for {guid}')
        by_guid[guid] = str(headers[0][2])
    dbnames = bracket.loads_file(params['DBNames'])
    refs = {}
    numbers = set()
    for n in walk(dbnames):
        if len(n) != 3 or isinstance(n[0], list) or str(n[1]) != 'Reference':
            continue
        guid = str(n[0]).lower()
        if not GUID.fullmatch(guid) or not str(n[2]).isdigit():
            raise ValueError('DBNames: invalid Reference entry')
        number = int(n[2])
        if guid in refs or number in numbers:
            raise ValueError('DBNames: duplicate Reference mapping')
        refs[guid] = number
        numbers.add(number)
    result = {}
    for guid, name in by_guid.items():
        if guid not in refs:
            raise ValueError(f'DBNames: no Reference mapping for {name} ({guid})')
        table = f'_Reference{refs[guid]}'
        if not any(t.lower() == table.lower() for t in db.tables):
            raise ValueError(f'DBNames: missing table {table} for {name}')
        if name.casefold() in result:
            raise ValueError(f'CONFIG: duplicate catalog name {name}')
        result[name.casefold()] = (name, guid, table)
    return result


def accounting_register_map(db, table_name):
    """Resolve a physical _AccRgN table to its metadata field GUIDs and names."""
    match = re.fullmatch(r'_AccRg([0-9]+)', table_name, re.IGNORECASE)
    if not match:
        raise ValueError('table must be named _AccRgN')
    number = str(int(match.group(1)))
    names = bracket.loads_file(read_files(db, 'PARAMS', {'DBNames'})['DBNames'])
    register_guids = [str(n[0]).lower() for n in walk(names)
                      if len(n) == 3 and str(n[1]) == 'AccRg' and str(n[2]) == number]
    if len(register_guids) != 1:
        raise ValueError(f'DBNames: expected one AccRg mapping for table {table_name}')
    register_guid = register_guids[0]
    if not GUID.fullmatch(register_guid):
        raise ValueError('DBNames: invalid accounting register GUID')
    config = read_files(db, 'CONFIG', {register_guid})
    if register_guid not in config:
        raise ValueError(f'CONFIG: missing accounting register {register_guid}')
    metadata = bracket.loads_file(config[register_guid])
    fields = []
    for node in walk(metadata):
        if len(node) < 4 or not isinstance(node[1], list) or len(node[1]) != 3:
            continue
        if str(node[1][0]) not in ('0', '1') or str(node[1][1]) != '0':
            continue
        guid, name = str(node[1][2]).lower(), str(node[2])
        if GUID.fullmatch(guid) and name and guid != register_guid:
            fields.append((guid, name))
    # The same field may appear in several inherited/configuration variants.
    fields = list(dict.fromkeys(fields))
    fld_numbers = {}
    for node in walk(names):
        if len(node) != 3 or str(node[1]) != 'Fld':
            continue
        guid, raw_number = str(node[0]).lower(), str(node[2])
        if guid in {g for g, _ in fields}:
            if not raw_number.isdigit() or (guid in fld_numbers and fld_numbers[guid] != int(raw_number)):
                raise ValueError(f'DBNames: ambiguous Fld mapping for {guid}')
            fld_numbers[guid] = int(raw_number)
    table = next((t for n, t in db.tables.items() if n.casefold() == table_name.casefold()), None)
    if table is None:
        raise ValueError(f'missing physical register table {table_name}')
    result = []
    for guid, name in fields:
        if guid not in fld_numbers:
            continue
        number = fld_numbers[guid]
        prefix = f'_FLD{number}'.casefold()
        columns = [field.name for field in table.fields.values()
                   if field.name.casefold() == prefix or field.name.casefold().startswith(prefix + 'dt')
                   or field.name.casefold().startswith(prefix + 'ct') or field.name.casefold().startswith(prefix + 'rref')]
        result.append({'guid': guid, 'name': name, 'number': number, 'columns': columns})
    if not result:
        raise ValueError(f'CONFIG: no DBNames Fld mappings for {table_name}')
    return register_guid, result
