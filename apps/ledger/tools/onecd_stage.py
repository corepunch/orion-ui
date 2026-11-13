"""Read-only export of the business data of a 1CD file into one staging XML file.

Format `orion-1cd-stage-v1` is documented in docs/1cd-verification-without-1c.md.
Rules: source UUIDs are preserved (platform GUID form, equal to the GUIDs in CONFIG),
decimals are exact text, NULL stays distinct from zero and empty values, nothing is
matched by name, and whatever cannot be decoded reliably becomes an explicit
`undecoded` marker. Every table that is not (fully) exported is listed.
"""
import hashlib
import os
import re
import struct
from collections import Counter, defaultdict
from decimal import Decimal
from pathlib import Path

from vendor.onecreader.raw1cd import decode_datetime, decode_nvc
import onecd_meta as M
from onecd_meta import decode_numeric_decimal

FORMAT = 'orion-1cd-stage-v1'

BUSINESS_CATALOGS = (
    'Контрагенты', 'Номенклатура', 'ДоговорыКонтрагентов', 'БанковскиеСчета', 'Банки', 'Склады', 'Организации',
    'ПодразделенияОрганизаций', 'ФизическиеЛица', 'Валюты', 'СтатьиЗатрат', 'СтатьиДвиженияДенежныхСредств',
    'ПрочиеДоходыИРасходы', 'НоменклатурныеГруппы', 'РасходыБудущихПериодов', 'ВидыНоменклатуры',
    'КлассификаторЕдиницИзмерения', 'Сотрудники', 'ОсновныеСредства', 'НематериальныеАктивы')
ACCOUNT_KINDS = {0: 'active', 1: 'passive', 2: 'active-passive'}
COMPOSITE_PARTS = ('_TYPE', '_L', '_N', '_T', '_S', '_RTREF', '_RRREF')
# the common attribute that separates data areas: constant 0 in a non-separated database
SEPARATOR_NAME = 'ОбластьДанныхОсновныеДанные'
XML_BAD = re.compile('[\x00-\x08\x0b\x0c\x0e-\x1f\ud800-\udfff￾￿]')
FLD_RE = re.compile(r'^_FLD(\d+)(DT|CT)?(RREF)?$', re.I)
PART_RE = re.compile(r'^(.*?)(_TYPE|_L|_N|_T|_S|_RTREF|_RRREF)$', re.I)
ZERO16 = bytes(16)


def ref_uuid(raw):
    """Platform GUID of a 16-byte stored reference (the form CONFIG metadata uses)."""
    if len(raw) != 16:
        raise ValueError('reference must be 16 bytes')
    h = (raw[12:16] + raw[10:12] + raw[8:10] + raw[0:2] + raw[2:8]).hex()
    return f'{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:]}'


def escape(text, attr=False):
    text = text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('\r', '&#13;')
    if attr:
        text = text.replace('"', '&quot;').replace('\n', '&#10;').replace('\t', '&#9;')
    return text


class El:
    __slots__ = ('tag', 'attrs', 'kids', 'text')

    def __init__(self, tag, attrs=None, text=None):
        self.tag, self.attrs, self.kids, self.text = tag, dict(attrs or {}), [], text

    def add(self, child):
        self.kids.append(child)
        return child

    def render(self, out, depth=0):
        pad = '  ' * depth
        attrs = ''.join(f' {k}="{escape(str(v), True)}"' for k, v in self.attrs.items())
        if self.text is not None:
            out.append(f'{pad}<{self.tag}{attrs}>{escape(self.text)}</{self.tag}>\n')
        elif not self.kids:
            out.append(f'{pad}<{self.tag}{attrs}/>\n')
        else:
            out.append(f'{pad}<{self.tag}{attrs}>\n')
            for kid in self.kids:
                kid.render(out, depth + 1)
            out.append(f'{pad}</{self.tag}>\n')


def lower_map(mapping):
    return {k.casefold(): v for k, v in mapping.items()}


class Layout:
    """Physical columns of one table grouped into logical fields."""

    def __init__(self, table):
        self.table = table
        self.cols = lower_map(table.fields)
        self.groups = {}       # base -> {part: Field} for composite (typed) columns
        for name, field in table.fields.items():
            m = PART_RE.match(name)
            if m and not (m.group(2).upper() == '_TYPE' and field.type != 'B'):
                self.groups.setdefault(m.group(1).upper(), {})[m.group(2).upper()] = field
        self.composite = {b: p for b, p in self.groups.items() if '_TYPE' in p}
        self.composite_cols = {f.name for p in self.composite.values() for f in p.values()}

    def get(self, name):
        return self.cols.get(name.casefold())


class Stage:
    def __init__(self, db, source_sha256=None):
        self.db = db
        self.meta = M.Meta(db)
        self.sha256 = source_sha256
        self.layouts, self.blobs, self.indexes = {}, {}, {}
        self.records = {}              # (kind, number, id16) -> El (catalog-like and documents)
        self.pending = []
        self.pulled = set()
        self.enums = {}                # (number, id16) -> (name, order)
        self.enum_names = {}
        self.exported_rows = Counter()  # table name -> rows exported
        self.undecoded = Counter()      # (location, reason) -> count
        self.escaped = 0
        self.dangling = []
        self.unnamed = Counter()
        self.predefined = {}
        self.fld_cache = {}
        self.vt_cache = {}
        self.common_names = {}
        self.not_exported = []

    # -- tables -----------------------------------------------------------------

    def layout(self, table):
        if table.name not in self.layouts:
            self.layouts[table.name] = Layout(table)
        return self.layouts[table.name]

    def blob(self, table):
        if table.name not in self.blobs:
            self.blobs[table.name] = M.LazyObject(self.db, table.blob_page) if table.blob_page else None
        return self.blobs[table.name]

    def index(self, info):
        """id -> row of the object's live rows."""
        key = (info.kind, info.number)
        if key not in self.indexes:
            table = self.meta.object_table(info)
            result = {}
            if table is not None:
                idf = table.fields['_IDRREF']
                for row in M.live_rows(self.db, table):
                    ref = row[idf.offset:idf.offset + idf.size]
                    if ref in result:
                        raise ValueError(f'{table.name}: duplicate live _IDRREF {ref_uuid(ref)}')
                    result[ref] = row
            self.indexes[key] = result
        return self.indexes[key]

    def note(self, loc, reason):
        self.undecoded[(loc, reason)] += 1

    # -- primitive cells -------------------------------------------------------

    @staticmethod
    def cell(row, field):
        """(is_null, data) honouring the nullable flag byte."""
        data = row[field.offset:field.offset + field.size]
        if len(data) != field.size:
            raise ValueError('truncated row')
        if field.null_exists:
            if data[0] not in (0, 1):
                raise ValueError('invalid null flag')
            return data[0] == 0, data[1:]
        return False, data

    def undec(self, loc, reason, data=b''):
        self.note(loc, reason)
        attrs = {'t': 'undecoded', 'reason': reason}
        if data and len(data) <= 32:
            attrs['hex'] = data.hex()
        return attrs, None

    def primitive(self, table, row, field, loc, pattern=None):
        """Decode one non-reference physical column into (attrs, text)."""
        try:
            null, data = self.cell(row, field)
        except ValueError as exc:
            return self.undec(loc, str(exc))
        if null:
            return {'t': 'null'}, None
        kind = field.type
        try:
            if kind == 'L':
                if data[0] not in (0, 1):
                    return self.undec(loc, 'invalid boolean', data)
                return {'t': 'boolean'}, 'true' if data[0] else 'false'
            if kind == 'N':
                return {'t': 'decimal'}, format(decode_numeric_decimal(data, field.length, field.precision), 'f')
            if kind == 'NVC':
                if int.from_bytes(data[:2], 'little') > (len(data) - 2) // 2:
                    return self.undec(loc, 'invalid string length', data[:8])
                return self.string(decode_nvc(data), loc)
            if kind == 'NC':
                return self.string(data.decode('utf-16-le').rstrip(' '), loc)
            if kind == 'DT':
                return self.date(data, loc, pattern)
            if kind in ('I', 'NT'):
                start, size = struct.unpack('<II', data[:8])
                if kind == 'I':
                    return {'t': 'blob', 'bytes': size}, None
                blob = self.blob(table)
                raw = blob.read_chain(start, size) if size else b''
                return self.string(raw.decode('utf-16-le'), loc)
            if kind == 'B':
                if field.length == 16 and not any(data):
                    return {'t': 'uuid', 'empty': 'true'}, None
                return self.undec(loc, f'raw bytes B({field.length})', data)
        except (ValueError, OverflowError, UnicodeError, struct.error) as exc:
            return self.undec(loc, f'{kind}: {exc}', data)
        return self.undec(loc, f'unsupported column type {kind}', data)

    def string(self, text, loc):
        if XML_BAD.search(text):
            self.escaped += 1
            return {'t': 'string-hex'}, text.encode('utf-8', 'surrogatepass').hex()
        return {'t': 'string'}, text

    def date(self, data, loc, pattern=None):
        if not any(data):   # 1CD stores the empty date as seven zero bytes
            return {'t': 'datetime', 'empty': 'true'}, None
        value = decode_datetime(data)
        if value is None:
            return self.undec(loc, 'invalid date', data)
        flavour = None
        items = [i for i in (pattern or []) if str(i[0]) == 'D']
        if len(items) == 1 and len(items[0]) > 1:
            flavour = {'D': 'date', 'T': 'time'}.get(str(items[0][1]))
        attrs = {'t': flavour or 'datetime'}
        if flavour == 'date':
            text = value.strftime('%Y-%m-%d')
        elif flavour == 'time':
            text = value.strftime('%H:%M:%S')
        else:
            text = value.strftime('%Y-%m-%dT%H:%M:%S')
        if value.year == 1 and value.month == 1 and value.day == 1:
            attrs['empty'] = 'true'
        return attrs, text

    # -- references ------------------------------------------------------------

    def reference(self, info, raw, loc):
        """Typed reference value to `info` (an ObjectInfo) with the 16-byte id."""
        if info.kind == 'Enum':
            attrs = {'t': 'enum', 'type': info.name}
            if not any(raw):
                attrs['empty'] = 'true'
                return attrs, None
            found = self.enum_value(info, raw)
            if found is None:
                attrs.update(id=ref_uuid(raw), dangling='true')
                self.dangling.append((loc, info.name, ref_uuid(raw)))
                return attrs, None
            attrs.update(name=found[0], id=ref_uuid(raw))
            self.enums[(info.number, raw)] = found
            return attrs, None
        attrs = {'t': 'ref', 'type': info.name}
        if not any(raw):
            attrs['empty'] = 'true'
            return attrs, None
        attrs['id'] = ref_uuid(raw)
        if raw not in self.index(info):
            attrs['dangling'] = 'true'
            self.dangling.append((loc, info.name, ref_uuid(raw)))
        else:
            self.require(info, raw)
        return attrs, None

    def enum_value(self, info, raw):
        """(name, order) of an enum value: row in _EnumN matched by GUID to the metadata value header."""
        if info.number not in self.enum_names:
            table = self.meta.object_table(info)
            names = {}
            for guid, name in info.headers.items():
                if guid != info.guid:
                    names[guid] = name
            order_field = table.fields.get('_ENUMORDER')
            idf = table.fields['_IDRREF']
            values = {}
            for row in M.live_rows(self.db, table):
                ref = row[idf.offset:idf.offset + idf.size]
                order = int(decode_numeric_decimal(row[order_field.offset:order_field.offset + order_field.size],
                                                   order_field.length, order_field.precision))
                name = names.get(ref_uuid(ref))
                if name is not None:
                    values[ref] = (name, order)
            self.enum_names[info.number] = values
        return self.enum_names[info.number].get(raw)

    def require(self, info, raw):
        key = (info.kind, info.number, raw)
        if key not in self.records and key not in self.pulled:
            self.pulled.add(key)
            self.pending.append(key)

    def single_ref_target(self, info, number, side, loc):
        guid = self.meta.fld_guid.get(number)
        pattern = info.field_patterns.get(guid) if info and guid else None
        targets = {str(i[1]).lower() for i in (pattern or []) if str(i[0]) == '#'}
        found = {self.meta.type_by_guid[g].number: self.meta.type_by_guid[g] for g in targets if g in self.meta.type_by_guid}
        return next(iter(found.values())) if len(found) == 1 and len(targets) == 1 else None

    def decode_composite(self, table, row, parts, loc, pattern=None):
        try:
            null, tcode = self.cell(row, parts['_TYPE'])
        except ValueError as exc:
            return self.undec(loc, str(exc))
        if null:
            return {'t': 'null'}, None
        code = tcode[0]
        raw_all = b''.join(row[f.offset:f.offset + f.size] for f in parts.values())

        def part(name):
            f = parts.get(name)
            if f is None:
                return None
            n, d = self.cell(row, f)
            return None if n else (f, d)
        try:
            if code == 1:
                return {'t': 'null', 'undefined': 'true'}, None
            if code == 2 and '_L' in parts:
                f, d = part('_L')
                return self.primitive(table, row, f, loc)
            if code == 3 and '_N' in parts:
                return self.primitive(table, row, parts['_N'], loc)
            if code == 4 and '_T' in parts:
                return self.primitive(table, row, parts['_T'], loc, pattern)
            if code == 5 and '_S' in parts:
                return self.primitive(table, row, parts['_S'], loc)
            if code == 8 and '_RTREF' in parts and '_RRREF' in parts:
                rt, rr = part('_RTREF'), part('_RRREF')
                if rt is None or rr is None:
                    return self.undec(loc, 'NULL parts in a reference', raw_all)
                number = int.from_bytes(rt[1], 'big')
                if number == 0 and not any(rr[1]):
                    return {'t': 'ref', 'empty': 'true'}, None
                info = self.meta.type_by_number.get(number)
                if info is None:
                    return self.undec(loc, f'unknown reference type {number:#x}', rt[1] + rr[1])
                return self.reference(info, rr[1], loc)
        except (ValueError, struct.error, UnicodeError) as exc:
            return self.undec(loc, str(exc), raw_all)
        return self.undec(loc, f'unsupported value type code {code}', raw_all)

    # -- fields ----------------------------------------------------------------

    def field_name(self, info, number):
        guid = self.meta.fld_guid.get(number)
        if guid is None:
            return None
        name = info.headers.get(guid) if info else None
        if name is None:
            if guid not in self.common_names:
                self.common_names[guid] = None
                if self.meta.config.has('CONFIG', guid):
                    for node in M.walk(self.meta.config.tree('CONFIG', guid)):
                        hit = M.header_guid_name(node)
                        if hit and hit[0] == guid:
                            self.common_names[guid] = hit[1]
                            break
            name = self.common_names[guid]
        return name

    def fields(self, table, row, info, loc, skip=()):
        """Decode all _FLD columns of a row into [(El)]; yields side-aware results as (side, El)."""
        lay = self.layout(table)
        skip = {s.upper() for s in skip}
        seen, out = set(), []
        for name, field in table.fields.items():
            if name.upper() in skip:
                continue
            base = None
            m = FLD_RE.match(name)
            pm = PART_RE.match(name)
            if m:
                base = f'_FLD{m.group(1)}{(m.group(2) or "").upper()}'
                number, side, single_ref = int(m.group(1)), (m.group(2) or '').upper(), bool(m.group(3))
            elif pm and pm.group(1).upper() in lay.composite and FLD_RE.match(pm.group(1)):
                fm = FLD_RE.match(pm.group(1))
                base = pm.group(1).upper()
                number, side, single_ref = int(fm.group(1)), (fm.group(2) or '').upper(), False
            else:
                continue
            if base in seen:
                continue
            seen.add(base)
            guid = self.meta.fld_guid.get(number)
            pattern = info.field_patterns.get(guid) if info and guid else None
            fname = self.field_name(info, number)
            floc = f'{loc}.{fname or base}'
            if fname is None:
                self.unnamed[loc] += 1
                fname = base
            if base in lay.composite:
                attrs, text = self.decode_composite(table, row, lay.composite[base], floc, pattern)
            elif single_ref:
                target = self.single_ref_target(info, number, side, floc)
                null, data = self.cell(row, field)
                if null:
                    attrs, text = {'t': 'null'}, None
                elif target is None:
                    attrs, text = self.undec(floc, 'reference target type unknown', data)
                else:
                    attrs, text = self.reference(target, data, floc)
            else:
                attrs, text = self.primitive(table, row, field, floc, pattern)
            if fname == SEPARATOR_NAME and attrs.get('t') == 'decimal' and text is not None and Decimal(text) == 0:
                continue
            el = El('v', {'n': fname, **attrs}, text)
            out.append((side, number, el))
        return out

    # -- predefined names ------------------------------------------------------

    def predefined_names(self, info):
        """predefined GUID -> name, read from the object's predefined-data files in CONFIG."""
        if info.guid in self.predefined:
            return self.predefined[info.guid]
        names = {}
        prefix = info.guid + '.'
        for fname in sorted(self.meta.config.names('CONFIG')):
            if not fname.startswith(prefix) or fname.endswith('.0'):
                continue
            raw = self.meta.config.read('CONFIG', fname)
            if raw[:3] != b'\xef\xbb\xbf' or raw[3:4] != b'{' or b'"Pattern"' not in raw[:4000] or b'{"#",' not in raw[:6000]:
                continue
            try:
                tree = M.bracket.loads_file(raw)
            except ValueError:
                continue
            for node in M.walk(tree):
                if len(node) >= 5 and str(node[0]) == '2' and isinstance(node[3], list) and len(node[3]) == 3 \
                        and str(node[3][0]) == '#' and isinstance(node[3][2], list) and len(node[3][2]) == 2:
                    guid = str(node[3][2][1]).lower()
                    text = next((c for c in node[4:] if isinstance(c, list) and len(c) == 2 and str(c[0]) == 'S'), None)
                    if M.GUID.fullmatch(guid) and guid != '00000000-0000-0000-0000-000000000000' and text is not None:
                        names.setdefault(guid, str(text[1]))
        self.predefined[info.guid] = names
        return names

    # -- objects ---------------------------------------------------------------

    def object_el(self, info, row):
        """Record/document/account element with attributes and decoded fields."""
        table = self.meta.object_table(info)
        lay = self.layout(table)
        loc = info.name
        get = lay.get

        def ref_attr(name):
            f = get(name)
            if f is None:
                return None
            null, data = self.cell(row, f)
            return None if null or not any(data) else data
        idf = get('_IDRREF')
        raw_id = row[idf.offset:idf.offset + idf.size]
        tag = {'Document': 'document', 'Acc': 'account'}.get(info.kind, 'record')
        el = El(tag, {'type': info.name, 'id': ref_uuid(raw_id)})
        std = {'_IDRREF', '_VERSION', '_MARKED', '_PREDEFINEDID', '_PARENTIDRREF', '_FOLDER', '_OWNERIDRREF',
               '_CODE', '_DESCRIPTION', '_DATE_TIME', '_NUMBER', '_POSTED', '_ORDERFIELD', '_KIND', '_OFFBALANCE'}
        skip = set(std)
        if info.kind == 'Chrc':
            skip.add('_TYPE')
        for part in ('_OWNERID',):
            skip |= {f.name.upper() for f in lay.groups.get(part, {}).values()}
        a = el.attrs

        def value(name, prefer_attr=True):
            f = get(name)
            if f is None:
                return None
            attrs, text = self.primitive(table, row, f, f'{loc}.{name}')
            return attrs, text
        for col, attr in (('_CODE', 'code'), ('_DESCRIPTION', 'description'), ('_NUMBER', 'number')):
            got = value(col)
            if got is not None:
                attrs, text = got
                if attrs['t'] in ('string', 'decimal'):
                    a[attr] = text
                elif attrs['t'] == 'string-hex':
                    a[attr + '-hex'] = text
                elif attrs['t'] == 'null':
                    pass
                else:
                    el.add(El('v', {'n': col.lower(), **attrs}, text))
        got = value('_DATE_TIME')
        if got:
            attrs, text = got
            a['date'] = text if attrs['t'] == 'datetime' else ''
            if attrs['t'] != 'datetime':
                el.add(El('v', {'n': '_DATE_TIME', **attrs}, text))
        for col, attr in (('_POSTED', 'posted'), ('_MARKED', 'deleted'), ('_OFFBALANCE', 'off-balance')):
            got = value(col)
            if got:
                a[attr] = got[1] if got[0]['t'] == 'boolean' else 'undecoded'
        f = get('_FOLDER')
        if f is not None:
            attrs, text = self.primitive(table, row, f, f'{loc}._FOLDER')
            # 1CD stores _Folder inverted: 1 = element, 0 = group
            a['folder'] = {'true': 'false', 'false': 'true'}.get(text, 'undecoded')
        f = get('_KIND')
        if f is not None:
            attrs, text = self.primitive(table, row, f, f'{loc}._KIND')
            code = int(Decimal(text)) if attrs['t'] == 'decimal' else None
            if info.kind == 'Acc':
                a['kind'] = ACCOUNT_KINDS.get(code, 'undecoded')
                if code not in ACCOUNT_KINDS:
                    self.note(f'{loc}._KIND', f'unknown account kind {text}')
        f = get('_ORDERFIELD')
        if f is not None:
            attrs, text = self.primitive(table, row, f, f'{loc}._ORDERFIELD')
            if attrs['t'] == 'string':
                a['order'] = text
        pid = ref_attr('_PREDEFINEDID')
        if pid is not None:
            a['predefined-id'] = ref_uuid(pid)
            name = self.predefined_names(info).get(ref_uuid(pid))
            if name is not None:
                a['predefined'] = name
            else:
                self.note(f'{loc}._PREDEFINEDID', 'predefined name not found in metadata')
        parent = ref_attr('_PARENTIDRREF')
        if parent is not None:
            a['parent'] = ref_uuid(parent)
            if parent not in self.index(info):
                self.dangling.append((f'{loc}.parent', info.name, ref_uuid(parent)))
                a['parent-dangling'] = 'true'
            else:
                self.require(info, parent)
        self.owner(info, table, row, lay, el, loc)
        for side, number, v in self.fields(table, row, info, loc, skip):
            el.add(v)
        for col, field in table.fields.items():
            cu = col.upper()
            if cu in skip or cu in lay.composite_cols or FLD_RE.match(col) or cu in ('_VERSION',):
                continue
            if PART_RE.match(col) and PART_RE.match(col).group(1).upper() in lay.composite:
                continue
            attrs, text = self.primitive(table, row, field, f'{loc}.{col}')
            el.add(El('v', {'n': col, 'src': 'column', **attrs}, text))
        if info.kind == 'Chrc':
            self.value_types(info, table, row, el, loc)
        self.exported_rows[table.name] += 1
        return el

    def owner(self, info, table, row, lay, el, loc):
        """Owner of a subordinate catalog item: composite _OwnerID or a single-owner _OwnerIDRRef."""
        parts = lay.groups.get('_OWNERID')
        if parts and '_TYPE' in parts:
            attrs, text = self.decode_composite(table, row, parts, f'{loc}.owner')
            if attrs['t'] == 'ref' and 'id' in attrs:
                el.attrs['owner-type'], el.attrs['owner'] = attrs['type'], attrs['id']
            elif attrs['t'] != 'null' and attrs.get('empty') != 'true':
                el.add(El('v', {'n': 'owner', **attrs}, text))
            return
        f = lay.get('_OWNERIDRREF')
        if f is None:
            return
        null, data = self.cell(row, f)
        if null or not any(data):
            return
        target = self.single_owner(info, data)
        if target is None:
            el.attrs['owner'] = ref_uuid(data)
            attrs, _ = self.undec(f'{loc}.owner', 'owner catalog not determined', data)
            el.add(El('v', {'n': 'owner', **attrs}))
            return
        attrs, _ = self.reference(target, data, f'{loc}.owner')
        el.attrs['owner-type'], el.attrs['owner'] = target.name, attrs['id']
        if 'dangling' in attrs:
            el.attrs['owner-dangling'] = 'true'

    def single_owner(self, info, data):
        """The one catalog allowed as owner (from the owners list in metadata), verified against the data."""
        guids = set()
        for node in M.walk(info.tree):
            if len(node) == 3 and str(node[0]) == '#' and str(node[1]).lower() == '157fa490-4ce9-11d4-9415-008048da11f9' \
                    and isinstance(node[2], list) and len(node[2]) == 2:
                g = str(node[2][1]).lower()
                if (g, 'Reference') in self.meta.by_guid:
                    guids.add(g)
        hits = [self.meta.by_guid[(g, 'Reference')] for g in sorted(guids) if data in self.index(self.meta.by_guid[(g, 'Reference')])]
        return hits[0] if len(hits) == 1 else None

    def value_types(self, info, table, row, el, loc):
        """Characteristic value types: the _Type blob holds a bracket-file type description."""
        f = table.fields.get('_TYPE')
        container = El('types')
        try:
            null, data = self.cell(row, f)
            if null:
                return
            start, size = struct.unpack('<II', data[:8])
            raw = self.blob(table).read_chain(start, size) if size else b''
            if len(raw) < 10 or int.from_bytes(raw[2:10], 'little') != len(raw) - 10:
                raise ValueError('unexpected type blob header')
            tree = M.bracket.loads_file(raw[10:])
            pattern = next(n for n in M.walk(tree) if n and str(n[0]) == 'Pattern')
            for item in pattern[1:]:
                tag = str(item[0])
                if tag == '#':
                    target = self.meta.type_by_guid.get(str(item[1]).lower())
                    if target is None:
                        self.note(f'{loc}._TYPE', f'unknown type guid {item[1]}')
                        container.add(El('type', {'t': 'undecoded', 'guid': str(item[1]).lower()}))
                    else:
                        container.add(El('type', {'t': 'ref', 'type': target.name}))
                elif tag == 'S':
                    container.add(El('type', {'t': 'string', **({'length': str(item[1])} if len(item) > 1 else {}),
                                              **({'fixed': 'true'} if len(item) > 2 and str(item[2]) == '0' else {})}))
                elif tag == 'N':
                    container.add(El('type', {'t': 'decimal', 'length': str(item[1]), 'precision': str(item[2]),
                                              'non-negative': 'true' if len(item) > 3 and str(item[3]) == '1' else 'false'}))
                elif tag == 'D':
                    container.add(El('type', {'t': {'D': 'date', 'T': 'time'}.get(str(item[1]) if len(item) > 1 else '', 'datetime')}))
                elif tag == 'B':
                    container.add(El('type', {'t': 'boolean'}))
                else:
                    self.note(f'{loc}._TYPE', f'unknown type item {tag}')
                    container.add(El('type', {'t': 'undecoded', 'item': tag}))
        except (ValueError, StopIteration, struct.error, IndexError) as exc:
            attrs, _ = self.undec(f'{loc}._TYPE', f'type description: {exc}')
            container.add(El('type', attrs))
        el.add(container)

    def tabular_sections(self, info, raw_id, el):
        """Tabular sections of an object in line order."""
        for number in self.vt_numbers(info):
            table = self.meta.table(self.meta.vt_table_name(info, number))
            sec = self.vt_rows(info, number, table).get(raw_id)
            if not sec:
                continue
            vt_name = info.headers.get(self.meta.vt_guid.get(number), f'VT{number}')
            sec_el = El('table', {'n': vt_name})
            loc = f'{info.name}.{vt_name}'
            for line, row in sorted(sec, key=lambda x: x[0]):
                row_el = sec_el.add(El('row', {'line': str(line)}))
                for _, _, v in self.fields(table, row, info, loc, skip=('_KEYFIELD', f'_{info.kind}{info.number}_IDRREF')):
                    row_el.add(v)
                for col, field in table.fields.items():
                    if col.upper() == '_KEYFIELD' or re.fullmatch(r'_LINENO\d*', col, re.I) or col.upper() == f'_{info.kind}{info.number}_IDRREF'.upper() \
                            or FLD_RE.match(col) or PART_RE.match(col):
                        continue
                    attrs, text = self.primitive(table, row, field, f'{loc}.{col}')
                    row_el.add(El('v', {'n': col, 'src': 'column', **attrs}, text))
                self.exported_rows[table.name] += 1
            el.add(sec_el)

    def vt_numbers(self, info):
        if info.guid not in self.vt_cache:
            numbers = []
            prefix = f'_{info.kind}{info.number}_VT'.casefold()
            for name in self.db.tables:
                if name.casefold().startswith(prefix) and name[len(prefix):].isdigit():
                    numbers.append(int(name[len(prefix):]))
            self.vt_cache[info.guid] = sorted(numbers)
        return self.vt_cache[info.guid]

    def vt_rows(self, info, number, table):
        key = (info.kind, info.number, number)
        if key not in self.vt_cache:
            owner_f = self.layout(table).get(f'_{info.kind}{info.number}_IDRREF')
            line_f = next(f for n, f in table.fields.items() if re.fullmatch(r'_LINENO\d*', n, re.I))
            result = defaultdict(list)
            for row in M.live_rows(self.db, table):
                line = int(decode_numeric_decimal(row[line_f.offset:line_f.offset + line_f.size], line_f.length, line_f.precision))
                result[row[owner_f.offset:owner_f.offset + owner_f.size]].append((line, row))
            self.vt_cache[key] = result
        return self.vt_cache[key]

    def export_object(self, info, raw_id, row, pulled=False):
        el = self.object_el(info, row)
        if pulled:
            el.attrs['pulled'] = 'true'
        self.tabular_sections(info, raw_id, el)
        self.records[(info.kind, info.number, raw_id)] = el
        return el

    def drain(self):
        """Export every object referenced so far that is not exported yet (transitively)."""
        while self.pending:
            kind, number, raw = self.pending.pop()
            if (kind, number, raw) in self.records:
                continue
            info = self.meta.objects[(kind, number)]
            self.export_object(info, raw, self.index(info)[raw], pulled=True)


class Exporter(Stage):
    """Sections of the staging file on top of the generic record decoding."""

    SLOT_FLAGS = {'Суммовой': 'amount', 'Валютный': 'currency', 'Количественный': 'quantity'}
    ACCOUNT_FLAGS = {'Валютный': 'currency', 'Количественный': 'quantity'}
    TECHNICAL = ('_EDHASHDT', '_EDHASHCT')

    # -- chart of accounts ------------------------------------------------------

    def chart_slots(self, info):
        """account id -> ordered list of (subkonto kind id, slot El) from the _AccN_ExtDimM table."""
        names = [n for n in self.db.tables if re.fullmatch(rf'_Acc{info.number}_ExtDim\d+', n, re.I)]
        if len(names) > 1:
            raise ValueError(f'{info.name}: ambiguous ext-dimension tables')
        slots = defaultdict(list)
        if not names:
            return slots
        table = self.db.tables[names[0]]
        lay = self.layout(table)
        owner_f, line_f = lay.get(f'_ACC{info.number}_IDRREF'), table.fields['_LINENO']
        rows = []
        for row in M.live_rows(self.db, table):
            line = int(decode_numeric_decimal(row[line_f.offset:line_f.offset + line_f.size], line_f.length, line_f.precision))
            rows.append((row[owner_f.offset:owner_f.offset + owner_f.size], line, row))
        for owner, line, row in sorted(rows, key=lambda r: (r[0], r[1])):
            loc = f'{info.name}.Субконто'
            kind_f = lay.get('_DIMKINDRREF')
            kind_raw = row[kind_f.offset:kind_f.offset + kind_f.size]
            slot = El('subkonto', {'n': str(line), 'kind': ref_uuid(kind_raw)})
            for col, attr in (('_TURNOVERONLY', 'turnover-only'), ('_DIMISMETADATA', 'kind-is-metadata')):
                f = lay.get(col)
                if f is not None:
                    attrs, text = self.primitive(table, row, f, f'{loc}.{col}')
                    slot.attrs[attr] = text if attrs['t'] == 'boolean' else 'undecoded'
            for _, _, v in self.fields(table, row, info, loc):
                attr = self.SLOT_FLAGS.get(v.attrs['n'])
                if attr and v.attrs['t'] == 'boolean':
                    slot.attrs[attr] = v.text
                else:
                    slot.add(v)
            self.exported_rows[table.name] += 1
            slots[owner].append((kind_raw, slot))
        return slots

    def export_chart(self, info):
        table = self.meta.object_table(info)
        slots = self.chart_slots(info)
        index = self.index(info)
        if not index:
            return None
        for raw in index:
            for kind_raw, _ in slots.get(raw, ()):
                pass
        for raw, row in index.items():
            el = self.export_object(info, raw, row)
            for name, attr in self.ACCOUNT_FLAGS.items():
                for v in list(el.kids):
                    if v.tag == 'v' and v.attrs['n'] == name and v.attrs['t'] == 'boolean':
                        el.attrs[attr] = v.text
                        el.kids.remove(v)
            for _, slot in slots.get(raw, ()):
                el.add(slot)
        return slots

    def subkonto_info(self, charts_slots):
        """The characteristic-type plan holding all subkonto kinds, proven by the data."""
        kinds = {k for slots in charts_slots.values() for per in slots.values() for k, _ in per}
        if not kinds:
            return None
        hits = [i for (k, _), i in sorted(self.meta.objects.items()) if k == 'Chrc'
                and self.meta.object_table(i) is not None and kinds <= set(self.index(i))]
        if len(hits) != 1:
            raise ValueError(f'subkonto kinds match {len(hits)} characteristic-type plans, expected 1')
        return hits[0]

    def chart_for(self, register):
        guids = {str(t).lower() for n in M.walk(register.tree) for t in n if not isinstance(t, list)}
        hits = [i for (g, k), i in self.meta.by_guid.items() if k == 'Acc' and g in guids]
        if len(hits) != 1:
            raise ValueError(f'{register.name}: chart of accounts not determined ({len(hits)} candidates)')
        return hits[0]

    # -- accounting register ---------------------------------------------------

    def export_register(self, info, chart, slots):
        table = self.meta.object_table(info)
        lay = self.layout(table)
        ed_number = self.meta.sub_numbers.get((info.guid, 'AccRgED'))
        ed_table = self.meta.table(f'_AccRgED{ed_number}') if ed_number is not None else None
        ed_rows = defaultdict(list)
        if ed_table is not None:
            keys = ('_PERIOD', '_RECORDERTREF', '_RECORDERRREF', '_LINENO')
            fs = [ed_table.fields[k] for k in keys]
            for row in M.live_rows(self.db, ed_table):
                ed_rows[tuple(row[f.offset:f.offset + f.size] for f in fs)].append(row)
        keys_f = [table.fields[k] for k in ('_PERIOD', '_RECORDERTREF', '_RECORDERRREF', '_LINENO')]
        chart_index = self.index(chart)
        slot_kinds = {raw: [k for k, _ in per] for raw, per in slots.items()}
        out = El('register', {'type': info.name, 'chart': chart.name})
        used_ed = set()
        loc = info.name
        movements = []
        for row in M.live_rows(self.db, table):
            key = tuple(row[f.offset:f.offset + f.size] for f in keys_f)
            period_attrs, period = self.date(key[0], f'{loc}._PERIOD')
            line = decode_numeric_decimal(key[3], keys_f[3].length, keys_f[3].precision)
            mv = El('movement', {'line': str(int(line))})
            rec_n = int.from_bytes(key[1], 'big')
            rec_info = self.meta.type_by_number.get(rec_n)
            if rec_info is None or rec_info.kind != 'Document':
                attrs, _ = self.undec(f'{loc}.recorder', f'unknown recorder type {rec_n:#x}', key[1] + key[2])
                mv.attrs.update({'recorder-type': '', 'recorder': ref_uuid(key[2]), 'recorder-undecoded': 'true'})
            else:
                attrs, _ = self.reference(rec_info, key[2], f'{loc}.recorder')
                mv.attrs.update({'recorder-type': rec_info.name, 'recorder': attrs.get('id', ''),
                                 **({'recorder-dangling': 'true'} if 'dangling' in attrs else {})})
            mv.attrs['period'] = period if period_attrs['t'] == 'datetime' else ''
            active_attrs, active = self.primitive(table, row, lay.get('_ACTIVE'), f'{loc}._ACTIVE')
            mv.attrs['active'] = active if active_attrs['t'] == 'boolean' else 'undecoded'
            sides = {}
            for side, col in (('debit', '_ACCOUNTDTRREF'), ('credit', '_ACCOUNTCTRREF')):
                f = lay.get(col)
                raw = row[f.offset:f.offset + f.size]
                attrs, _ = self.reference(chart, raw, f'{loc}.{side}')
                el = El(side, {'account': attrs.get('id', '')})
                for flag in ('empty', 'dangling'):
                    if flag in attrs:
                        el.attrs[f'account-{flag}'] = 'true'
                sides[side] = (el, raw)
            skip = {'_PERIOD', '_RECORDERTREF', '_RECORDERRREF', '_LINENO', '_ACTIVE', '_ACCOUNTDTRREF',
                    '_ACCOUNTCTRREF', *self.TECHNICAL}
            for side, number, v in self.fields(table, row, info, loc):
                if side == 'DT':
                    sides['debit'][0].add(v)
                elif side == 'CT':
                    sides['credit'][0].add(v)
                else:
                    mv.add(v)
            for col, field in table.fields.items():
                if col.upper() in skip or FLD_RE.match(col):
                    continue
                attrs, text = self.primitive(table, row, field, f'{loc}.{col}')
                mv.add(El('v', {'n': col, 'src': 'column', **attrs}, text))
            mv_ed = ed_rows.get(key, [])
            for el_side, (el, raw) in zip((0, 1), (sides['debit'], sides['credit'])):
                self.add_subkonto(ed_table, mv_ed, el_side, el, slot_kinds.get(raw, []), loc, used_ed)
            mv.add(sides['debit'][0])
            mv.add(sides['credit'][0])
            movements.append((period or '', ref_uuid(key[2]), int(line), mv))
            self.exported_rows[table.name] += 1
        for _, _, _, mv in sorted(movements, key=lambda m: m[:3]):
            out.add(mv)
        for key, rows in ed_rows.items():
            for r in rows:
                if id(r) not in used_ed:
                    self.note(f'{loc}.subkonto', 'subkonto row not attached to a movement side')
        return out

    def add_subkonto(self, ed_table, rows, correspond, el, kinds, loc, used):
        """Attach the _AccRgED rows of one side as ordered subkonto 1..n of the account."""
        lay = self.layout(ed_table)
        corr_f, kind_f = ed_table.fields['_CORRESPOND'], ed_table.fields['_KINDRREF']
        mine = []
        for row in rows:
            n, d = self.cell(row, corr_f)
            if int(decode_numeric_decimal(d, corr_f.length, corr_f.precision)) == correspond:
                mine.append(row)
        by_kind = defaultdict(list)
        for row in mine:
            by_kind[row[kind_f.offset:kind_f.offset + kind_f.size]].append(row)
        order = list(kinds) + [k for k in by_kind if k not in kinds]
        for number, kind in enumerate(order, 1):
            hits = by_kind.get(kind, [])
            sub = El('subkonto', {'n': str(number), 'kind': ref_uuid(kind)})
            if kind not in kinds:
                sub.attrs['extra'] = 'true'
                self.note(f'{loc}.subkonto', 'subkonto kind is not a dimension of the account')
            if len(hits) != 1:
                reason = 'no value row for the account dimension' if not hits else 'several rows for one dimension'
                sub.attrs.update(t='undecoded', reason=reason)
                self.note(f'{loc}.subkonto', reason)
            else:
                row = hits[0]
                used.add(id(row))
                attrs, text = self.decode_composite(ed_table, row, lay.composite['_VALUE'], f'{loc}.subkonto')
                sub.attrs.update(attrs)
                sub.text = text
            for h in hits:
                used.add(id(h))
            el.add(sub)
        self.exported_rows[ed_table.name] += len(mine)

    # -- sections --------------------------------------------------------------

    def catalog_infos(self):
        by_name = defaultdict(list)
        for (kind, _), info in self.meta.objects.items():
            if kind == 'Reference':
                by_name[info.short_name].append(info)
        found, missing = [], []
        for name in BUSINESS_CATALOGS:
            infos = by_name.get(name, [])
            (found if len(infos) == 1 else missing).append(infos[0] if len(infos) == 1 else name)
        return found, missing

    def table_for(self, info):
        return self.meta.object_table(info)

    def nonempty(self, info):
        table = self.meta.object_table(info)
        return table is not None and M.count_live_rows(self.db, table) > 0

    def build(self):
        meta, root = self.meta, El('stage', {'format': FORMAT})
        header = root.add(El('header'))
        self.header(header)
        charts = {}
        for (kind, _), info in sorted(meta.objects.items()):
            if kind == 'Acc' and self.nonempty(info):
                charts[info.number] = (info, self.export_chart(info))
        sub_info = self.subkonto_info({n: s for n, (_, s) in charts.items()})
        if sub_info is not None:
            for raw, row in self.index(sub_info).items():
                self.export_object(sub_info, raw, row)
        registers = []
        for (kind, _), info in sorted(meta.objects.items()):
            if kind == 'AccRg' and self.nonempty(info):
                chart = self.chart_for(info)
                if chart.number not in charts:
                    raise ValueError(f'{info.name}: chart {chart.name} is empty')
                registers.append(self.export_register(info, chart, charts[chart.number][1]))
        catalogs, missing = self.catalog_infos()
        for info in catalogs:
            for raw, row in self.index(info).items():
                self.export_object(info, raw, row)
        for (kind, _), info in sorted(meta.objects.items()):
            if kind == 'Document' and self.nonempty(info):
                for raw, row in self.index(info).items():
                    self.export_object(info, raw, row)
        self.drain()
        header.add(El('missing-catalogs', {'names': ','.join(missing)}))
        sec = root.add(El('chart-of-accounts'))
        for number, (info, _) in sorted(charts.items()):
            sec.attrs['type'] = info.name
            for key in sorted((k for k in self.records if k[:2] == (info.kind, info.number)),
                              key=lambda k: (self.records[k].attrs.get('code', ''), self.records[k].attrs['id'])):
                sec.add(self.records[key])
        sk = root.add(El('subkonto-kinds', {'type': sub_info.name if sub_info else ''}))
        if sub_info is not None:
            for key in sorted((k for k in self.records if k[:2] == (sub_info.kind, sub_info.number)),
                              key=lambda k: (self.records[k].attrs.get('code', ''), self.records[k].attrs['id'])):
                sk.add(self.records[key])
        shown = {(i.kind, i.number) for _, (i, _) in charts.items()} | ({(sub_info.kind, sub_info.number)} if sub_info else set())
        recs = root.add(El('records'))
        for key in sorted((k for k in self.records if k[0] != 'Document' and k[:2] not in shown),
                          key=lambda k: (self.records[k].attrs['type'], self.records[k].attrs.get('code', ''),
                                         self.records[k].attrs['id'])):
            recs.add(self.records[key])
        docs = root.add(El('documents'))
        for key in sorted((k for k in self.records if k[0] == 'Document'),
                          key=lambda k: (self.records[k].attrs['type'], self.records[k].attrs.get('date', ''),
                                         self.records[k].attrs.get('number', ''), self.records[k].attrs['id'])):
            docs.add(self.records[key])
        regs = root.add(El('registers'))
        for reg in registers:
            regs.add(reg)
        enums = root.add(El('enums'))
        for (number, raw), (name, order) in sorted(self.enums.items(), key=lambda kv: (kv[0][0], kv[1][1], kv[0][1])):
            info = meta.type_by_number[number]
            enums.add(El('enum', {'type': info.name, 'name': name, 'id': ref_uuid(raw), 'order': str(order)}))
            self.exported_rows[meta.object_table(info).name] += 1
        skipped, partial = self.skipped()
        root.add(skipped)
        root.add(partial)
        return root

    def header(self, header):
        db = self.db
        header.add(El('source', {'file': Path(db.path).name, 'size': os.path.getsize(db.path),
                                 'sha256': self.sha256 or '', 'format-version': db.format_version,
                                 'locale': getattr(db, 'locale', '')}))
        name = version = ''
        try:
            tree = self.meta.config.tree('CONFIG', 'root')
            main = self.meta.config.tree('CONFIG', str(tree[1]).lower())
            parents = {}
            first = None
            for node, par in M.walk_parents(main):
                parents[id(node)] = par
                if first is None and M.header_guid_name(node):
                    first = node
            name = M.header_guid_name(first)[1]
            node = first
            for _ in range(5):
                node = parents.get(id(node))
                if node is None:
                    break
                hit = next((str(c) for c in node if not isinstance(c, list) and getattr(c, 'quoted', False)
                            and re.fullmatch(r'\d+(?:\.\d+){2,3}', str(c))), None)
                if hit:
                    version = hit
                    break
        except (KeyError, ValueError, IndexError, TypeError):
            self.note('configuration', 'name/version not read')
        header.add(El('configuration', {'name': name, 'version': version}))

    # -- skipped tables --------------------------------------------------------

    def describe(self, tname):
        meta = self.meta
        if not hasattr(self, '_table_entries'):
            self._table_entries = {}
            self._owners = {i.guid: i for i in meta.objects.values()}
            upper = {n.upper() for n in self.db.tables}
            for guid, kind, number in meta.entries:
                key = f'_{kind}{number}'.upper()
                if key in upper and kind not in ('Fld', 'VT'):
                    self._table_entries.setdefault(key, (guid, kind, number))
        up = tname.upper()

        def owner_name(guid):
            owner = self._owners.get(guid)
            try:
                return owner.name if owner else ''
            except (KeyError, ValueError):
                return ''
        ent = self._table_entries.get(up)
        if ent:
            return ent[1], owner_name(ent[0])
        m = re.fullmatch(r'(_.+?)_(VT|EXTDIM)(\d+)', tname, re.I)
        if m and m.group(1).upper() in self._table_entries:
            guid, kind, _ = self._table_entries[m.group(1).upper()]
            owner = self._owners.get(guid)
            if m.group(2).upper() == 'VT':
                sec = None
                if owner is not None:
                    sec = owner.headers.get(meta.vt_guid.get(int(m.group(3))))
                return f'{kind}.VT', f'{owner_name(guid)}.{sec}' if sec else owner_name(guid)
            return f'{kind}.ExtDim', owner_name(guid)
        return 'system', ''

    def skipped(self):
        skipped, partial = El('skipped'), El('partial-tables')
        rows = []
        for name, table in self.db.tables.items():
            count = M.count_live_rows(self.db, table)
            if not count:
                continue
            done = self.exported_rows.get(name, 0)
            if done >= count:
                continue
            kind, meta_name = self.describe(name)
            rows.append((name, kind, meta_name, count, done))
        for name, kind, meta_name, count, done in sorted(rows, key=lambda r: (-r[3], r[0])):
            (partial if done else skipped).add(El('table', {'name': name, 'kind': kind, 'metadata': meta_name,
                                                          'rows': count, **({'exported': done} if done else {})}))
        skipped.attrs['tables'] = len(skipped.kids)
        skipped.attrs['rows'] = sum(int(k.attrs['rows']) for k in skipped.kids)
        partial.attrs['tables'] = len(partial.kids)
        return skipped, partial


def source_sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        for chunk in iter(lambda: source.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def build_stage(db):
    """Build the staging element tree; returns (root, exporter)."""
    exporter = Exporter(db, source_sha256(db.path))
    root = exporter.build()
    summary = root.add(El('summary'))
    summary.add(El('undecoded', {'count': sum(exporter.undecoded.values())}))
    for (loc, reason), n in sorted(exporter.undecoded.items()):
        summary.kids[-1].add(El('item', {'where': loc, 'reason': reason, 'count': n}))
    summary.add(El('dangling', {'count': len(exporter.dangling)}))
    for loc, tname, uid in exporter.dangling:
        summary.kids[-1].add(El('item', {'where': loc, 'type': tname, 'id': uid}))
    return root, exporter


def write_stage(root, output, source, overwrite=False):
    """Write the XML without ever touching the source; refuse to replace a file unless asked."""
    output = Path(output)
    if output.resolve() == Path(source).resolve():
        raise ValueError('stage output must not be the 1CD source file')
    if output.exists() and not overwrite:
        raise ValueError(f'{output} exists; pass --overwrite to replace it')
    out = ['<?xml version="1.0" encoding="UTF-8"?>\n']
    root.render(out)
    data = ''.join(out).encode('utf-8')
    tmp = output.with_name(output.name + '.tmp')
    fd = os.open(tmp, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if overwrite:
            os.replace(tmp, output)
        else:
            os.link(tmp, output)   # fails if the file appeared meanwhile; never overwrites
            os.unlink(tmp)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    return len(data)


def summary_text(root, exporter, size):
    sections = {k.tag: k for k in root.kids}
    lines = [f'output_bytes={size}']
    for tag in ('chart-of-accounts', 'subkonto-kinds', 'records', 'documents'):
        lines.append(f'{tag}: {len(sections[tag].kids)}')
    for reg in sections['registers'].kids:
        lines.append(f'register {reg.attrs["type"]}: {len(reg.kids)} movements')
    lines.append(f'enums referenced: {len(sections["enums"].kids)}')
    sk = sections['skipped']
    lines.append(f'skipped: {sk.attrs["tables"]} tables / {sk.attrs["rows"]} rows; partial tables: {sections["partial-tables"].attrs["tables"]}')
    lines.append(f'dangling references: {len(exporter.dangling)}')
    lines.append(f'undecoded markers: {sum(exporter.undecoded.values())}')
    for (loc, reason), n in sorted(exporter.undecoded.items(), key=lambda kv: -kv[1])[:20]:
        lines.append(f'  {n} x {loc}: {reason}')
    return '\n'.join(lines)
