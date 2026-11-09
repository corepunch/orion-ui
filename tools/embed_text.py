#!/usr/bin/env python3
"""Embed a text file in C as a NUL-terminated string constant.

usage: embed_text.py INPUT SYMBOL > OUTPUT.h
"""
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())
    path, symbol = sys.argv[1], sys.argv[2]
    with open(path, encoding="utf-8") as f:
        text = f.read()
    guard = "__" + symbol.upper() + "_H__"
    out = [f"// Generated from {path} by tools/embed_text.py. Do not edit.",
           f"#ifndef {guard}", f"#define {guard}",
           f"static const char {symbol}[] ="]
    lines = text.splitlines(keepends=True) or [""]
    for line in lines:
        escaped = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
        out.append(f'  "{escaped}"')
    out[-1] += ";"
    out.append("#endif")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
