"""Публичный генератор синтетического `1Cv8.1CD` (, идея dt-demo-configuration):

    создаёт валидную маленькую файловую базу для unit-тестов без реальных
    баз (реальные — 2.5 ГБ). Используется также тестами парсера.

Собирает минимальный, но структурно корректный файл:
- заголовок (1CDBMSV8, 8.3.8.0, страницы 8192);
- root-объект (стр. 2) с каталогом таблиц в blob-цепочках (чанки 256 б);
- объекты данных таблиц (fat_level 0, сигнатура `1c fd`);
- blob-объекты (цепочки чанков) для BINARYDATA/SERIALIZEDDATA.

Раскладка: {Files,data_page,blob_page,index_page} в описании таблицы указывает
реальные номера страниц (двухпроходный расчёт, фиксированная ширина чисел).
Формат — по docs/format-8x.md (раздел «Спайк 5.1»).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

PAGE = 8192
PAGE_HEADER_SIZE = 24
BLOB_CHUNK = 256


@dataclass
class FixtureField:
    name: str
    type: str
    null_exists: bool = False
    length: int = 0
    precision: int = 0
    case_sensitive: str = 'CS'


@dataclass
class FixtureTable:
    name: str
    fields: list[FixtureField] = field(default_factory=list)
    rows: list[bytes] = field(default_factory=list)
    # blob-чанки: first_chunk -> (next_chunk, payload)
    blobs: dict[int, tuple[int, bytes]] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Кодирование значений
# ---------------------------------------------------------------------------


def enc_nvc(value: str | None, length: int, null_exists: bool = False) -> bytes:
    raw = b''
    if value is not None:
        raw = value.encode('utf-16-le')
        assert len(raw) <= length * 2
    payload = struct.pack('<H', len(value or '')) + raw
    pad = b'\x20\x00' * ((length * 2 - len(raw)) // 2)  # пробелы, utf-16le
    if null_exists:
        flag = b'\x01' if value is not None else b'\x00'
        return flag + payload + pad if value is not None else flag + b'\x00' * (2 + length * 2)
    return payload + pad


def enc_nc(value: str, length: int) -> bytes:
    raw = value.encode('utf-16-le')
    assert len(raw) <= length * 2
    return raw + b'\x20\x00' * ((length * 2 - len(raw)) // 2)


def enc_numeric(value: float, length: int, precision: int = 0) -> bytes:
    sign = '1' if value >= 0 else '0'
    scaled = abs(round(float(value) * (10 ** precision)))
    digits = str(scaled).rjust(length, '0')
    nibbles = (sign + digits).ljust((length // 2 + 1) * 2, '0')
    return bytes.fromhex(nibbles)


def enc_datetime(value: str | None) -> bytes:
    if value is None:
        return b'\x00' * 7
    v = str(value)
    if '-' in v and ':' in v:  # ISO 'YYYY-MM-DD HH:MM:SS' / 'YYYY-MM-DDTHH:MM:SS'
        v = v.replace('T', ' ').split('.')[0]
        parts = v.replace('-', ' ').replace(':', ' ').split()
        v = ''.join(p.zfill(2) if i > 0 else p for i, p in enumerate(parts[:6]))
    return bytes.fromhex(v)  # 'YYYYMMDDHHMMSS'


def field_size(ftype: str, length: int) -> int:
    return {
        'B': length, 'L': 1, 'N': length // 2 + 1, 'NC': length * 2,
        'NVC': length * 2 + 2, 'RV': 16, 'NT': 8, 'I': 8, 'DT': 7,
    }.get(ftype, 0)


def encode_row(fields: list[FixtureField], values: dict[str, Any]) -> bytes:
    """Собирает строку по раскладке: RV на offset 1, остальные последовательно."""
    offset = 17 if any(f.type == 'RV' for f in fields) else 1
    slots: dict[str, tuple[int, bytes]] = {}
    for f in fields:
        v = values.get(f.name)
        size = (1 if f.null_exists else 0) + field_size(f.type, f.length)
        if f.type == 'RV':
            off = 1
        else:
            off = offset
            offset += size
        if f.type == 'RV':
            raw = v if isinstance(v, bytes) else b'\x00' * 16
        elif f.type == 'NVC':
            raw = enc_nvc(v, f.length, f.null_exists)
        elif f.type == 'NC':
            raw = enc_nc(v or '', f.length)
        elif f.type == 'N':
            raw = enc_numeric(v or 0, f.length, f.precision)
        elif f.type == 'L':
            raw = b'\x01' if v else b'\x00'
        elif f.type == 'DT':
            raw = enc_datetime(v)
        elif f.type == 'B':
            raw = (v if isinstance(v, bytes) else b'\x00' * 16)[:f.length].ljust(f.length, b'\x00')
        elif f.type == 'I' and isinstance(v, (tuple, list)) and len(v) == 2:
            raw = struct.pack('<2I', *v)
        else:
            raw = b'\x00' * size
        slots[f.name] = (off, raw)
    end = max((o + len(r) for o, r in slots.values()), default=1)
    row = bytearray(max(end, 5))
    for o, r in slots.values():
        row[o:o + len(r)] = r
    return bytes(row)


def _table_desc(table: FixtureTable, files: tuple[int, int, int]) -> str:
    lines = [f'{{"{table.name}",0,', '{"Fields",']
    for f in table.fields:
        n = 1 if f.null_exists else 0
        lines.append(f'{{"{f.name}","{f.type}",{n},{f.length},{f.precision},"{f.case_sensitive}"}},')
    lines.append('},')
    lines.append('{"Indexes"},')
    lines.append('{"Recordlock","0"},')
    d, b, i = files
    lines.append(f'{{"Files",{d:06d},{b:06d},{i:06d}}}')
    lines.append('}')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# Сборка файла (двухпроходная)
# ---------------------------------------------------------------------------


def build_fake_1cd(tables: list[FixtureTable], locale: str = 'ru_RU') -> bytes:
    # ---- проход 1: раскладка root-объекта (описания с заглушками Files) ----
    desc_texts: list[str] = [_table_desc(t, (0, 0, 0)) for t in tables]
    desc_offsets: list[int] = []
    chunk_num = 2
    for desc in desc_texts:
        desc_offsets.append(chunk_num)
        chunk_num += (len(desc.encode('utf-8')) + (BLOB_CHUNK - 6) - 1) // (BLOB_CHUNK - 6)
    head = locale.encode('utf-8').ljust(32, b'\x00') + struct.pack('<i', len(tables))
    head += struct.pack(f'<{len(tables)}i', *desc_offsets)
    head_chunks = (len(head) + (BLOB_CHUNK - 6) - 1) // (BLOB_CHUNK - 6)
    root_n_chunks = max(desc_offsets[-1] + (len(desc_texts[-1].encode('utf-8'))
                                            + BLOB_CHUNK - 7) // (BLOB_CHUNK - 6),
                        head_chunks) if tables else head_chunks
    root_n_pages = (root_n_chunks * BLOB_CHUNK + PAGE - 1) // PAGE
    root_data_pages = list(range(3, 3 + root_n_pages))
    next_page = 3 + root_n_pages

    # ---- проход 2: страницы данных и blob-объектов таблиц ----
    data_pages: dict[int, int] = {}
    row_data: dict[int, bytes] = {}
    blob_pages: dict[int, int] = {}
    blob_bufs: dict[int, bytes] = {}
    for i, t in enumerate(tables):
        raw_rows = b''.join(t.rows)
        if raw_rows:
            n = (len(raw_rows) + PAGE - 1) // PAGE
            data_pages[i] = next_page
            row_data[i] = raw_rows
            next_page += 1 + n
        if t.blobs:
            max_ch = max(t.blobs)
            buf = bytearray((max_ch + 1) * BLOB_CHUNK)
            for num, (nxt, payload) in t.blobs.items():
                assert len(payload) <= BLOB_CHUNK - 6
                part = struct.pack('<Ih', nxt, len(payload)) + payload
                buf[num * BLOB_CHUNK:(num + 1) * BLOB_CHUNK] = (
                    part + b'\x00' * (BLOB_CHUNK - len(part)))
            n = (len(buf) + PAGE - 1) // PAGE
            blob_pages[i] = next_page
            blob_bufs[i] = bytes(buf)
            next_page += 1 + n

    # ---- проход 3: описания с реальными Files + цепочки чанков ----
    chunks: dict[int, bytes] = {}
    pos = 2
    for i, t in enumerate(tables):
        desc = _table_desc(t, (data_pages.get(i, 0), blob_pages.get(i, 0), 0))
        assert len(desc) == len(desc_texts[i]), 'ширина описания изменилась'
        remaining = desc.encode('utf-8')
        while True:
            part = remaining[:BLOB_CHUNK - 6]
            remaining = remaining[BLOB_CHUNK - 6:]
            nxt = pos + 1 if remaining else 0
            chunks[pos] = struct.pack('<Ih', nxt, len(part)) + part
            if not remaining:
                break
            pos = nxt
        pos += 1
    remaining = head
    while True:
        part = remaining[:BLOB_CHUNK - 6]
        remaining = remaining[BLOB_CHUNK - 6:]
        nxt = 1 + 1 if False else 0  # каталог — одна цепочка от чанка 1
        chunks[1] = struct.pack('<Ih', 0, len(part)) + part
        if not remaining:
            break
        # каталог > 250 б — не поддерживаем в фикстурах (хватает на 20+ таблиц)
        raise AssertionError('каталог таблиц слишком велик для фикстуры')
    max_chunk = max(chunks)

    root_buf = bytearray((max_chunk + 1) * BLOB_CHUNK)
    for num, payload in chunks.items():
        pad = payload + b'\x00' * (BLOB_CHUNK - len(payload))
        root_buf[num * BLOB_CHUNK:(num + 1) * BLOB_CHUNK] = pad
    root_len = len(root_buf)

    # ---- объекты ----
    def object_pages(header_page: int, pages: list[int], length: int) -> bytes:
        header = bytearray(PAGE)
        header[0:2] = b'\x1c\xfd'
        struct.pack_into('<H', header, 2, 0)
        struct.pack_into('<Q', header, 16, length)
        for j, p in enumerate(pages):
            struct.pack_into('<I', header, PAGE_HEADER_SIZE + 4 * j, p)
        return bytes(header)

    pages: dict[int, bytes] = {1: b'\x00' * PAGE}
    pages[2] = object_pages(2, root_data_pages, root_len)
    for j, p in enumerate(root_data_pages):
        pages[p] = bytes(root_buf[j * PAGE:(j + 1) * PAGE])
    for i, t in enumerate(tables):
        if i in data_pages:
            dp = data_pages[i]
            n = (len(row_data[i]) + PAGE - 1) // PAGE
            plist = list(range(dp + 1, dp + 1 + n))
            pages[dp] = object_pages(dp, plist, len(row_data[i]))
            for j, p in enumerate(plist):
                pages[p] = row_data[i][j * PAGE:(j + 1) * PAGE]
        if i in blob_pages:
            bp = blob_pages[i]
            n = (len(blob_bufs[i]) + PAGE - 1) // PAGE
            plist = list(range(bp + 1, bp + 1 + n))
            pages[bp] = object_pages(bp, plist, len(blob_bufs[i]))
            for j, p in enumerate(plist):
                pages[p] = blob_bufs[i][j * PAGE:(j + 1) * PAGE]

    total = max(pages) + 1
    header = bytearray(PAGE)
    header[0:8] = b'1CDBMSV8'
    header[8:12] = bytes([8, 3, 8, 0])
    struct.pack_into('<I', header, 12, total)
    struct.pack_into('<I', header, 16, 1)
    struct.pack_into('<I', header, 20, PAGE)
    pages[0] = bytes(header)

    out = bytearray()
    for p in range(total):
        content = pages.get(p, b'')
        out += content
        out += b'\x00' * (PAGE - len(content))
    return bytes(out)


def write_fake_1cd(path: str | Path, tables: list[FixtureTable],
                   locale: str = 'ru_RU') -> bytes:
    data = build_fake_1cd(tables, locale)
    with open(path, 'wb') as f:
        f.write(data)
    return data
