"""Чтение заголовка, объектов БД, таблиц и blob-цепочек 1Cv8.1CD.

Формат изучен по onec_dtools (Infactum) и tool1cd (awa, e8tools) — MIT/MPL.
"""

from __future__ import annotations

import datetime as dt
import math
import re
import struct
import os
from collections import OrderedDict
from dataclasses import dataclass
from urllib.error import URLError
from urllib.request import HTTPRedirectHandler, Request, build_opener

PAGE_HEADER_FMT = '<2sH3IQ'
PAGE_HEADER_SIZE = struct.calcsize(PAGE_HEADER_FMT)  # 24
OBJ_SIG = b'\x1c\xfd'
BLOB_CHUNK = 256
END32 = 0xFFFFFFFF

# ---------------------------------------------------------------------------
# Описание таблиц
# ---------------------------------------------------------------------------

_TABLE_RE = re.compile(
    r'\{"(\S+)".*\n\{"Fields",\n([\s\S]*)\n\},\n\{"Indexes"(?:,|)([\s\S]*)\},'
    r'\n\{"Recordlock","(\d+)"\},\n\{"Files",(\S+)\}\n\}'
)
_FIELD_RE = re.compile(r'\{"(\w+)","(\w+)",(\d+),(\d+),(\d+),"(\w+)"\}(?:,|)')


def _field_size(ftype: str, length: int) -> int:
    return {
        'B': length, 'L': 1, 'N': length // 2 + 1, 'NC': length * 2,
        'NVC': length * 2 + 2, 'RV': 16, 'NT': 8, 'I': 8, 'DT': 7,
    }[ftype]


@dataclass
class Field:
    name: str
    type: str
    null_exists: bool
    length: int
    precision: int
    case_sensitive: bool
    offset: int
    size: int


class TableDef:
    def __init__(self, description: str):
        m = _TABLE_RE.match(description)
        if m is None:
            raise ValueError('bad table description')
        self.name = m.group(1)
        self.data_page, self.blob_page, self.index_page = (
            int(x) for x in m.group(5).split(','))
        self.fields: dict[str, Field] = {}
        offset = 17 if '"RV"' in m.group(2) else 1
        for line in m.group(2).splitlines():
            fm = _FIELD_RE.match(line)
            if fm is None:
                raise ValueError('bad field description')
            ftype = fm.group(2)
            null_exists = fm.group(3) == '1'
            length = int(fm.group(4))
            size = (1 if null_exists else 0) + _field_size(ftype, length)
            if ftype == 'RV':
                foffset = 1
            else:
                foffset = offset
                offset += size
            self.fields[fm.group(1)] = Field(
                fm.group(1), ftype, null_exists, length, int(fm.group(5)),
                fm.group(6) == 'CS', foffset, size)
        if m.group(4) == '1' and not any(field.type == 'RV' for field in self.fields.values()):
            raise NotImplementedError(f'{self.name}: hidden record version is unsupported')
        self.row_length = max(offset, 5)


# ---------------------------------------------------------------------------
# Кодирование значений полей
# ---------------------------------------------------------------------------

def decode_nvc(buf: bytes) -> str:
    n = struct.unpack('<H', buf[:2])[0]
    return buf[2:2 + n * 2].decode('utf-16-le')


def encode_nvc(value: str, length: int) -> bytes:
    raw = value.encode('utf-16-le')
    if len(raw) > length * 2:
        raise ValueError(f'string too long for NVC({length}): {value!r}')
    return struct.pack('<H', len(value)) + raw.ljust(length * 2, b'\x00')


def decode_numeric(buf: bytes, length: int, precision: int):
    hex_str = buf.hex()
    sign = '-' if hex_str[0] == '0' else ''
    if precision:
        return float(sign + hex_str[1:length + 1 - precision] + '.'
                     + hex_str[length + 1 - precision:length + 1])
    return int(sign + hex_str[1:length + 1])


def encode_numeric(value: int, length: int) -> bytes:
    if value < 0:
        raise ValueError('negative numerics are not needed here')
    digits = str(value).rjust(length, '0')
    if len(digits) > length:
        raise ValueError(f'numeric overflow for N({length}): {value}')
    nibbles = '1' + digits
    nibbles = nibbles.ljust((length // 2 + 1) * 2, '0')
    return bytes.fromhex(nibbles)


def decode_datetime(buf: bytes):
    if buf[:2] == b'\x00\x00':
        return None
    s = buf.hex()
    return dt.datetime(int(s[:4]), int(s[4:6]), int(s[6:8]),
                       int(s[8:10]), int(s[10:12]), int(s[12:14]))


def encode_datetime(value: dt.datetime | None) -> bytes:
    if value is None:
        return b'\x00' * 7
    return bytes.fromhex(value.strftime('%Y%m%d%H%M%S'))


# ---------------------------------------------------------------------------
# База
# ---------------------------------------------------------------------------

class SnapshotRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.startswith('https://'):
            raise ValueError('snapshot redirect requires HTTPS')
        return super().redirect_request(req, fp, code, msg, headers, newurl)


class RangeSnapshot:
    """Read an immutable HTTPS object with a bounded in-memory block cache."""

    block_size = 256 * 1024
    max_blocks = 32

    def __init__(self, url):
        self.url = url
        self.size = None
        self.etag = None
        self.cache = OrderedDict()
        self.opener = build_opener(SnapshotRedirect())

    def request(self, start, end):
        for attempt in range(3):
            try:
                return self.fetch(start, end)
            except (TimeoutError, URLError) as exc:
                transient = isinstance(exc, TimeoutError) or isinstance(getattr(exc, 'reason', None), TimeoutError)
                if attempt == 2 or not transient:
                    raise

    def fetch(self, start, end):
        headers = {'Range': f'bytes={start}-{end}', 'Accept-Encoding': 'identity'}
        if self.etag is not None:
            headers['If-Match'] = self.etag
        with self.opener.open(Request(self.url, headers=headers), timeout=20) as reply:
            if reply.status != 206:
                raise ValueError(f'expected HTTP 206 from snapshot, got {reply.status}')
            match = re.fullmatch(r'bytes (\d+)-(\d+)/(\d+)', reply.headers.get('Content-Range', ''))
            if not match:
                raise ValueError('invalid snapshot Content-Range')
            first, last, size = map(int, match.groups())
            etag = reply.headers.get('ETag', '')
            if (first, last) != (start, end) or size <= last or (self.size is not None and size != self.size):
                raise ValueError('snapshot byte range or size changed')
            if not re.fullmatch(r'"[^"]+"', etag) or (self.etag is not None and etag != self.etag):
                raise ValueError('snapshot needs an unchanged strong ETag')
            if reply.headers.get('Content-Encoding', 'identity').lower() != 'identity':
                raise ValueError('snapshot ranges must use identity encoding')
            length = end - start + 1
            data = reply.read(length + 1)
            if len(data) != length:
                raise ValueError('snapshot byte range has the wrong length')
            self.size, self.etag = size, etag
            return data

    def read_at(self, offset, size):
        if self.size is None:
            return self.request(offset, offset + size - 1)
        end = offset + size
        if offset < 0 or end > self.size:
            raise ValueError('read outside snapshot')
        pieces = []
        while offset < end:
            block_number, inner = divmod(offset, self.block_size)
            block = self.cache.get(block_number)
            if block is None:
                start = block_number * self.block_size
                block = self.request(start, min(start + self.block_size, self.size) - 1)
                self.cache[block_number] = block
                if len(self.cache) > self.max_blocks:
                    self.cache.popitem(last=False)
            else:
                self.cache.move_to_end(block_number)
            part = block[inner:inner + end - offset]
            pieces.append(part)
            offset += len(part)
        return b''.join(pieces)


class Database1CD:
    """Файл 1CD: страницы, объекты, таблицы (8.2.14.0 и 8.3.8.0)."""

    def __init__(self, path: str, writable: bool = False):
        if writable:
            raise ValueError('1creader read-only: запись в 1CD отключена')
        self.path = path
        self.remote = RangeSnapshot(str(path)) if str(path).startswith('https://') else None
        self.file = None if self.remote else open(path, 'rb')
        try:
            self._open_header()
        except BaseException:
            self.close()
            raise

    def _open_header(self):
        head = self._read_at(0, 24)
        sig, v1, v2, v3, v4, total_pages, _unk = struct.unpack('<8s4bIi', head[:20])
        if sig != b'1CDBMSV8':
            raise ValueError('not a 1CD file')
        self.format_version = f'{v1}.{v2}.{v3}.{v4}'
        if self.format_version not in ('8.2.14.0', '8.3.8.0'):
            raise NotImplementedError(
                f'unsupported 1CD format {self.format_version}')
        self.total_pages = total_pages
        self.legacy = self.format_version == '8.2.14.0'
        self.page_size = 4096 if self.legacy else struct.unpack('<I', head[20:24])[0]
        if self.page_size not in (4096, 8192, 16384, 32768, 65536) or total_pages < 3:
            raise ValueError('invalid page size or page count')
        size = self.remote.size if self.remote else os.fstat(self.file.fileno()).st_size
        if size < total_pages * self.page_size:
            raise ValueError('truncated database')
        self._tables: dict[str, TableDef] | None = None
        self._root_data: bytes | None = None

    def close(self):
        if self.file:
            self.file.close()

    def _read_at(self, offset, size):
        if self.remote:
            return self.remote.read_at(offset, size)
        self.file.seek(offset)
        return self.file.read(size)

    # --- страницы ---

    def read_page(self, num: int) -> bytes:
        if num < 0 or num >= self.total_pages:
            raise ValueError(f'page {num} outside database')
        data = self._read_at(num * self.page_size, self.page_size)
        if len(data) != self.page_size:
            raise ValueError(f'truncated page {num}')
        return data

    # --- объекты ---

    def object_pages(self, header_page: int):
        buf = self.read_page(header_page)
        if self.legacy:
            if buf[:8] != b'1CDBOBV8':
                raise ValueError(f'bad legacy object signature at page {header_page}')
            length = struct.unpack_from('<I', buf, 8)[0]
        else:
            if buf[:2] != OBJ_SIG:
                raise ValueError(f'bad object signature at page {header_page}: {buf[:2].hex()}')
            length = struct.unpack_from('<Q', buf, 16)[0]
        if length > self.total_pages * self.page_size:
            raise ValueError('object larger than database')
        n_entries = (self.page_size - PAGE_HEADER_SIZE) // 4
        fat = struct.unpack(f'<{n_entries}I', buf[PAGE_HEADER_SIZE:])
        need = math.ceil(length / self.page_size)
        pages: list[int] = []
        if self.legacy:
            for index in fat[:math.ceil(need / 1023)]:
                if index == 0:
                    raise ValueError('missing legacy index page')
                directory = struct.unpack('<1024I', self.read_page(index))
                count, expected = directory[0], min(1023, need - len(pages))
                if count != expected:
                    raise ValueError('invalid legacy index page count')
                pages.extend(directory[1:1 + count])
        else:
            fat_level = struct.unpack_from('<H', buf, 2)[0]
            if fat_level == 0:
                pages = list(fat[:need])
            elif fat_level == 1:
                for index in fat:
                    if not index or len(pages) >= need:
                        break
                    for page in struct.unpack(f'<{self.page_size // 4}I', self.read_page(index)):
                        if not page or len(pages) == need:
                            break
                        pages.append(page)
            else:
                raise NotImplementedError(f'fat_level {fat_level}')
        if len(pages) != need or any(not 0 < page < self.total_pages for page in pages):
            raise ValueError('incomplete object page list')
        return length, pages

    def read_object(self, header_page: int) -> bytes:
        length, pages = self.object_pages(header_page)
        data = b''.join(self.read_page(p) for p in pages)
        return data[:length]

    # --- корневой объект и таблицы ---

    def _read_root_blob(self, chunk_offset: int) -> bytes:
        """Корневой объект (стр. 2) хранит данные blob-цепочками по 256 байт."""
        if self._root_data is None:
            self._root_data = self.read_object(2)
        data = self._root_data
        out = []
        pos = chunk_offset
        seen = set()
        while True:
            if pos in seen or pos * BLOB_CHUNK + 6 > len(data):
                raise ValueError('invalid root blob chain')
            seen.add(pos)
            block = data[pos * BLOB_CHUNK:(pos + 1) * BLOB_CHUNK]
            nxt, size = struct.unpack('<Ih', block[:6])
            if size < 0 or size > len(block) - 6:
                raise ValueError('invalid root blob size')
            out.append(block[6:6 + size])
            if nxt == 0:
                break
            pos = nxt
        return b''.join(out)

    @property
    def tables(self) -> dict[str, TableDef]:
        if self._tables is None:
            root = self.read_object(2) if self.legacy else self._read_root_blob(1)
            locale, count = struct.unpack('<32si', root[:36])
            if not 0 <= count <= (len(root) - 36) // 4:
                raise ValueError('invalid table directory count')
            self.locale = locale.decode('utf-8').rstrip('\x00')
            offsets = struct.unpack(f'<{count}i', root[36:36 + 4 * count])
            self._tables = {}
            for off in offsets:
                text = self.read_object(off).decode('utf-16').lstrip('\ufeff') if self.legacy else self._read_root_blob(off).decode('utf-8')
                t = TableDef(text)
                self._tables[t.name] = t
        return self._tables

    # --- строки и blob таблиц ---

    def table_rows(self, table: TableDef) -> list[bytes]:
        data = self.read_object(table.data_page)
        rl = table.row_length
        return [data[i:i + rl] for i in range(0, len(data) - len(data) % rl, rl)]

    def read_blob(self, table: TableDef, first_chunk: int, size: int,
                  object_data: bytes | None = None) -> bytes:
        if size == 0:
            return b''
        data = object_data if object_data is not None else self.read_object(table.blob_page)
        out = []
        pos = first_chunk
        remaining = size
        seen = set()
        while remaining > 0:
            if pos in seen or pos * BLOB_CHUNK + 6 > len(data):
                raise ValueError('invalid blob chain')
            seen.add(pos)
            block = data[pos * BLOB_CHUNK:(pos + 1) * BLOB_CHUNK]
            nxt, chunk_size = struct.unpack('<Ih', block[:6])
            if chunk_size <= 0 or chunk_size > len(block) - 6:
                raise ValueError('invalid blob size')
            out.append(block[6:6 + chunk_size])
            remaining -= chunk_size
            if nxt == 0:
                break
            pos = nxt
        return b''.join(out)[:size]
