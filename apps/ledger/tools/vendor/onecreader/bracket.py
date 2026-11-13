"""Парсер и сериализатор «скобкофайлов» 1С.

Формат: вложенные структуры ``{a,b,{c,d},"строка"}``. Строки в кавычках,
кавычка внутри строки экранируется удвоением. Значения вне кавычек (числа,
GUID, base64) могут содержать переносы строк, которые игнорируются.
"""

from __future__ import annotations


class Tok(str):
    """Лист дерева. Атрибут quoted различает "строку" и голое значение."""

    quoted: bool

    def __new__(cls, value: str, quoted: bool = False):
        obj = super().__new__(cls, value)
        obj.quoted = quoted
        return obj


Node = list  # элементы: Node | Tok


def parse(text: str) -> Node:
    """Разбирает скобкофайл (без BOM) и возвращает корневой список."""
    pos = 0
    n = len(text)

    def skip_ws(p: int) -> int:
        while p < n and text[p] in ' \t\r\n':
            p += 1
        return p

    def parse_value(p: int):
        p = skip_ws(p)
        if p >= n:
            raise ValueError('unexpected end of bracket data')
        ch = text[p]
        if ch == '{':
            return parse_list(p)
        if ch == '"':
            # строка с экранированием ""
            p += 1
            chunks = []
            while True:
                q = text.find('"', p)
                if q == -1:
                    raise ValueError('unterminated string')
                if q + 1 < n and text[q + 1] == '"':
                    chunks.append(text[p:q] + '"')
                    p = q + 2
                else:
                    chunks.append(text[p:q])
                    p = q + 1
                    break
            return Tok(''.join(chunks), quoted=True), p
        # голое значение: до , } { — пробелы/переносы внутри отбрасываются
        chunks = []
        while p < n and text[p] not in ',}{':
            if text[p] not in ' \t\r\n':
                chunks.append(text[p])
            p += 1
        return Tok(''.join(chunks), quoted=False), p

    def parse_list(p: int):
        assert text[p] == '{'
        p += 1
        items: Node = []
        p = skip_ws(p)
        if p < n and text[p] == '}':
            return items, p + 1
        while True:
            value, p = parse_value(p)
            items.append(value)
            p = skip_ws(p)
            if p >= n:
                raise ValueError('unexpected end of bracket data')
            if text[p] == ',':
                p += 1
                continue
            if text[p] == '}':
                return items, p + 1
            raise ValueError(f'unexpected char {text[p]!r} at {p}')

    pos = skip_ws(pos)
    root, pos = parse_value(pos)
    return root


def dumps(node) -> str:
    """Сериализует дерево в стиле платформы: \r\n перед каждым вложенным списком."""
    out: list[str] = []

    def emit(item, first_in_parent: bool):
        if isinstance(item, list):
            if out:
                if not first_in_parent or out[-1] == '{':
                    out.append('\r\n')
            out.append('{')
            for i, child in enumerate(item):
                if i:
                    out.append(',')
                emit(child, first_in_parent=(i == 0))
            if item and isinstance(item[-1], list):
                out.append('\r\n')
            out.append('}')
        else:
            if getattr(item, 'quoted', False):
                out.append('"' + str(item).replace('"', '""') + '"')
            else:
                out.append(str(item))

    emit(node, first_in_parent=True)
    return ''.join(out)


def loads_file(data: bytes) -> Node:
    """Разбирает содержимое файла (UTF-8, опциональный BOM)."""
    return parse(data.decode('utf-8-sig'))


def dumps_file(node) -> bytes:
    """Сериализует дерево в байты файла с BOM (как пишет платформа)."""
    return b'\xef\xbb\xbf' + dumps(node).encode('utf-8')
