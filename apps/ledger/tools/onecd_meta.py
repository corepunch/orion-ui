"""Read-only access to the 1CD metadata needed by the staging export.

Everything here resolves names from CONFIG and PARAMS/DBNames by GUID. Nothing
is matched by position or by similar names. Blobs are read lazily page by page,
so the 0.5 GB CONFIG blob object is never loaded into memory.
"""
import math
import re
import struct
import zlib
from collections import OrderedDict

from vendor.onecreader import bracket
from vendor.onecreader.raw1cd import BLOB_CHUNK, OBJ_SIG, PAGE_HEADER_SIZE, decode_nvc, decode_numeric

GUID = re.compile(r'^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$', re.I)

# DBNames kind -> (prefix of the metadata name, Russian kind name)
OBJECT_KINDS = {'Reference': 'Справочник', 'Document': 'Документ', 'Enum': 'Перечисление',
                'Chrc': 'ПланВидовХарактеристик', 'Acc': 'ПланСчетов', 'AccRg': 'РегистрБухгалтерии',
                'InfoRg': 'РегистрСведений', 'AccumRg': 'РегистрНакопления', 'Const': 'Константа',
                'Node': 'ПланОбмена', 'CKinds': 'ПланВидовРасчета', 'DocumentJournal': 'ЖурналДокументов',
                'Seq': 'Последовательность'}
# kinds that can be the target of a reference value
REFERENCE_KINDS = ('Reference', 'Document', 'Enum', 'Chrc', 'Acc', 'Node', 'CKinds')


class LazyObject:
    """A 1CD object (page chain) read page by page on demand."""

    def __init__(self, db, header_page, cache_pages=256):
        self.db = db
        buf = db.read_page(header_page)
        if buf[:2] != OBJ_SIG:
            raise ValueError(f'bad object signature at page {header_page}')
        fat_level = struct.unpack('<H', buf[2:4])[0]
        self.length = struct.unpack('<Q', buf[16:24])[0]
        if self.length > db.total_pages * db.page_size:
            raise ValueError('object larger than database')
        count = (db.page_size - PAGE_HEADER_SIZE) // 4
        fat = struct.unpack(f'<{count}I', buf[PAGE_HEADER_SIZE:])
        pages = []
        if fat_level == 0:
            pages = list(fat[:math.ceil(self.length / db.page_size)])
        elif fat_level == 1:
            for index_page in fat:
                if index_page == 0:
                    break
                for value in struct.unpack(f'<{db.page_size // 4}I', db.read_page(index_page)):
                    if value == 0:
                        break
                    pages.append(value)
        else:
            raise NotImplementedError(f'fat_level {fat_level}')
        if len(pages) * db.page_size < self.length or any(p == 0 for p in pages):
            raise ValueError('incomplete object page list')
        self.pages = pages
        self.cache = OrderedDict()
        self.cache_pages = cache_pages

    def _page(self, index):
        page = self.cache.get(index)
        if page is None:
            page = self.db.read_page(self.pages[index])
            self.cache[index] = page
            if len(self.cache) > self.cache_pages:
                self.cache.popitem(last=False)
        else:
            self.cache.move_to_end(index)
        return page

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.length:
            raise ValueError('read outside object')
        size_page, out = self.db.page_size, []
        while size > 0:
            index, inner = divmod(offset, size_page)
            part = self._page(index)[inner:inner + size]
            out.append(part)
            offset += len(part)
            size -= len(part)
        return b''.join(out)

    def read_all(self):
        return self.read(0, self.length)

    def read_chain(self, first_chunk, size):
        """Blob chunk chain (256-byte chunks), as db.read_blob but without the whole object."""
        if size == 0:
            return b''
        out, pos, remaining, seen = [], first_chunk, size, set()
        while remaining > 0:
            if pos in seen or pos * BLOB_CHUNK + 6 > self.length:
                raise ValueError('invalid blob chain')
            seen.add(pos)
            block = self.read(pos * BLOB_CHUNK, min(BLOB_CHUNK, self.length - pos * BLOB_CHUNK))
            nxt, chunk_size = struct.unpack('<Ih', block[:6])
            if chunk_size <= 0 or chunk_size > len(block) - 6:
                raise ValueError('invalid blob size')
            out.append(block[6:6 + chunk_size])
            remaining -= chunk_size
            if nxt == 0:
                break
            pos = nxt
        return b''.join(out)[:size]


def live_rows(db, table):
    """Yield row bytes of physically live rows; refuses unknown row states."""
    if not table.data_page:
        return
    data = db.read_object(table.data_page)
    if len(data) % table.row_length:
        raise ValueError(f'{table.name}: truncated rows')
    for off in range(0, len(data), table.row_length):
        row = data[off:off + table.row_length]
        if row[0] == 1:
            continue
        if row[0] != 0:
            raise ValueError(f'{table.name}: invalid row state at byte {off}')
        yield row


def count_live_rows(db, table):
    """Live row count; reads only what the table owns."""
    if not table.data_page:
        return 0
    data = db.read_object(table.data_page)
    if len(data) % table.row_length:
        raise ValueError(f'{table.name}: truncated rows')
    states = data[0::table.row_length]
    if any(s not in (0, 1) for s in set(states)):
        raise ValueError(f'{table.name}: invalid row state')
    return states.count(0)


def walk(root):
    stack = [root]
    while stack:
        node = stack.pop()
        if isinstance(node, list):
            yield node
            stack.extend(reversed(node))


def walk_parents(root, parent=None):
    """Yield (node, parent) for every list node."""
    stack = [(root, None)]
    while stack:
        node, par = stack.pop()
        if isinstance(node, list):
            yield node, par
            stack.extend((child, node) for child in reversed(node))


def header_guid_name(node):
    """A metadata header node is [version, [0|1, 0, guid], "Name", synonym, ...]."""
    if (len(node) >= 4 and isinstance(node[1], list) and len(node[1]) == 3
            and str(node[1][0]) in ('0', '1') and str(node[1][1]) == '0'
            and GUID.fullmatch(str(node[1][2])) and isinstance(node[2], str) and node[2].quoted):
        return str(node[1][2]).lower(), str(node[2])
    return None


class Config:
    """Index of CONFIG/PARAMS file records and lazy readers of their content."""

    def __init__(self, db):
        self.db = db
        self.stores = {}
        self.index = {}
        for name in ('CONFIG', 'PARAMS'):
            table = db.tables[name]
            fields = table.fields
            for key, kind in (('FILENAME', 'NVC'), ('BINARYDATA', 'I')):
                if key not in fields or fields[key].type != kind or fields[key].null_exists:
                    raise ValueError(f'{name}: unsupported {key} field')
            self.stores[name] = LazyObject(db, table.blob_page)
            index = self.index[name] = {}
            for row in live_rows(db, table):
                f = fields['FILENAME']
                filename = decode_nvc(row[f.offset:f.offset + f.size]).strip()
                p = fields.get('PARTNO')
                part = int(decode_numeric(row[p.offset:p.offset + p.size], p.length, p.precision)) if p else 0
                b = fields['BINARYDATA']
                start, size = struct.unpack('<II', row[b.offset:b.offset + 8])
                index.setdefault(filename.lower(), []).append((part, start, size))
        self.cache = {}

    def has(self, table, name):
        return name.lower() in self.index[table]

    def names(self, table):
        return self.index[table].keys()

    def read(self, table, name):
        key = (table, name.lower())
        if key not in self.cache:
            parts = sorted(self.index[table].get(name.lower(), ()))
            if not parts:
                raise KeyError(f'{table}: no file {name}')
            raw = b''.join(self.stores[table].read_chain(start, size) for _, start, size in parts)
            try:
                raw = zlib.decompress(raw, -15)
            except zlib.error:
                pass
            if len(self.cache) > 64:
                self.cache.clear()
            self.cache[key] = raw
        return self.cache[key]

    def tree(self, table, name):
        return bracket.loads_file(self.read(table, name))


def decode_numeric_decimal(data, length, precision):
    """Decode the reader's packed decimal format without binary float conversion."""
    from decimal import Decimal
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


_LEAD_GUIDS = re.compile(r'\A\s*\{1,\s*\{\d+,\s*((?:[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12},\s*)+)')


class ObjectInfo:
    """One DBNames object (catalog, document, enum, ...) with its CONFIG metadata."""

    def __init__(self, meta, kind, number, guid):
        self.meta, self.kind, self.number, self.guid = meta, kind, number, guid
        self.table_name = None
        self._tree = None
        self._fields = None
        self._name = None

    @property
    def tree(self):
        if self._tree is None:
            self._tree = self.meta.config.tree('CONFIG', self.guid)
        return self._tree

    @property
    def short_name(self):
        if self._name is None:
            names = self.headers
            if self.guid not in names:
                raise ValueError(f'CONFIG: no header for {self.guid}')
            self._name = names[self.guid]
        return self._name

    @property
    def name(self):
        return f'{OBJECT_KINDS.get(self.kind, self.kind)}.{self.short_name}'

    def type_guids(self):
        """Generated type GUIDs (the leading list of the object file); Pattern '#' items use them."""
        text = self.meta.config.read('CONFIG', self.guid)[:4096].decode('utf-8-sig', 'replace')
        m = _LEAD_GUIDS.match(text)
        return [g.lower() for g in re.findall(r'[0-9a-fA-F-]{36}', m.group(1))] if m else []

    @property
    def field_patterns(self):
        """guid -> type items of its Pattern (list of nodes) for every header followed by a Pattern."""
        if getattr(self, '_patterns', None) is None:
            result = {}
            for node, parent in walk_parents(self.tree):
                hit = header_guid_name(node)
                if not hit or parent is None:
                    continue
                at = next((i for i, c in enumerate(parent) if c is node), None)
                for sib in parent[at + 1:at + 2]:
                    if isinstance(sib, list) and sib and str(sib[0]) == 'Pattern':
                        result.setdefault(hit[0], sib[1:])
            self._patterns = result
        return self._patterns

    @property
    def headers(self):
        """guid -> first header name found in this object's metadata tree."""
        if self._fields is None:
            result = {}
            for node in walk(self.tree):
                hit = header_guid_name(node)
                if hit:
                    result.setdefault(hit[0], hit[1])
            self._fields = result
        return self._fields


class Meta:
    """DBNames + CONFIG: object kinds, table numbers, field and tabular-section names."""

    def __init__(self, db):
        self.db = db
        self.config = Config(db)
        self.objects = {}       # (kind, number) -> ObjectInfo
        self.by_guid = {}       # (guid, kind) -> ObjectInfo
        self.entries = []       # every DBNames (guid, kind, number)
        self.sub_numbers = {}   # (owner guid, kind) -> number for service tables (AccRgED, AccRgAT0, ...)
        self.fld_guid = {}      # field number -> guid
        self.vt_guid = {}       # tabular section number -> guid
        seen = set()
        for node in walk(self.config.tree('PARAMS', 'DBNames')):
            if len(node) != 3 or isinstance(node[0], list) or not str(node[2]).isdigit():
                continue
            guid, kind, number = str(node[0]).lower(), str(node[1]), int(node[2])
            if not GUID.fullmatch(guid):
                continue
            self.entries.append((guid, kind, number))
            if kind in ('Fld', 'VT'):
                target = self.fld_guid if kind == 'Fld' else self.vt_guid
                if number in target and target[number] != guid:
                    raise ValueError(f'DBNames: ambiguous {kind} number {number}')
                target[number] = guid
            elif kind not in OBJECT_KINDS:
                self.sub_numbers.setdefault((guid, kind), number)
            elif kind in OBJECT_KINDS:
                if (kind, number) in self.objects or (guid, kind) in self.by_guid:
                    raise ValueError(f'DBNames: duplicate {kind} mapping {guid}/{number}')
                info = ObjectInfo(self, kind, number, guid)
                self.objects[(kind, number)] = info
                self.by_guid[(guid, kind)] = info
                if kind in REFERENCE_KINDS:
                    if number in seen:
                        raise ValueError(f'DBNames: type number {number} used twice')
                    seen.add(number)
        self.type_by_number = {i.number: i for (k, _), i in self.objects.items() if k in REFERENCE_KINDS}
        self._type_by_guid = None
        self._table_names = {name.casefold(): name for name in db.tables}

    @property
    def type_by_guid(self):
        """Pattern '#' GUID -> referenced object (a GUID listed by exactly one object)."""
        if self._type_by_guid is None:
            result = {}
            for (kind, _), info in self.objects.items():
                if kind not in REFERENCE_KINDS:
                    continue
                for guid in info.type_guids():
                    if guid in result and result[guid] is not info:
                        raise ValueError(f'CONFIG: type {guid} listed by {result[guid].name} and {info.name}')
                    result[guid] = info
            self._type_by_guid = result
        return self._type_by_guid

    def table(self, name):
        real = self._table_names.get(name.casefold())
        return self.db.tables[real] if real else None

    def object_table(self, info):
        return self.table(f'_{info.kind}{info.number}')

    def vt_table_name(self, info, vt_number):
        return f'_{info.kind}{info.number}_VT{vt_number}'
