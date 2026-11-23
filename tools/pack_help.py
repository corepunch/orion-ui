#!/usr/bin/env python3
"""Build a reproducible Orion Help book from its XML and referenced images."""

import argparse
from pathlib import Path
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_manifest(source):
    xml = source.read_bytes()
    require(len(xml) <= 512 * 1024, f"{source.name} exceeds 512 KiB")
    require(b"<!DOCTYPE" not in xml, "DTD declarations are unsupported")
    root = ET.fromstring(xml)
    require(root.tag == "help" and root.get("version") == "1" and root.get("title"),
            'expected a titled <help version="1"> book')
    require(1 <= len(root) <= 128, "a book needs 1 to 128 topics")
    images, ids = set(), []
    for topic in root:
        ident = topic.get("id", "")
        require(topic.tag == "topic" and re.fullmatch(r"[a-z0-9][a-z0-9-]*", ident)
                and ident not in ids and topic.get("title"), f"invalid topic {ident!r}")
        ids.append(ident)
        require(1 <= len(topic) <= 64, f"{ident} needs 1 to 64 content blocks")
        for block in topic:
            require(not len(block), f"{ident}: nested markup is unsupported")
            if block.tag in ("p", "heading"):
                require(0 < len((block.text or "").strip().encode()) <= 511,
                        f"{ident}: {block.tag} must fit in 511 UTF-8 bytes")
                continue
            name = block.get("src", "")
            require(block.tag == "image" and block.get("caption") and
                    re.fullmatch(r"[a-z0-9][a-z0-9-]*\.(jpg|png)", name),
                    f"{ident}: invalid content block or image name {name!r}")
            images.add(name)
    return xml, ids, images


def pack(source, output):
    xml, ids, names = load_manifest(source)
    manifests = {"book.xml": xml}
    for translation in sorted(source.parent.glob("book.*.xml")):
        require(re.fullmatch(r"book\.[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*\.xml", translation.name),
                f"invalid localized manifest name {translation.name!r}")
        localized, localized_ids, localized_names = load_manifest(translation)
        require(localized_ids == ids, f"{translation.name}: topic IDs and order must match book.xml")
        manifests[translation.name] = localized
        names.update(localized_names)
    images = {}
    for name in sorted(names):
        path = source.parent / name
        require(0 < path.stat().st_size <= 4 * 1024 * 1024, f"{name}: image size must be between 1 byte and 4 MiB")
        images[name] = path.read_bytes()

    with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".ohlp.tmp", delete=False) as handle:
        tmp = Path(handle.name)
    try:
        with zipfile.ZipFile(tmp, "w", allowZip64=False) as archive:
            for name, manifest in manifests.items():
                archive.writestr(zipfile.ZipInfo(name), manifest, zipfile.ZIP_DEFLATED)
            for name, image in images.items():
                archive.writestr(zipfile.ZipInfo(name), image)
        require(tmp.stat().st_size <= 16 * 1024 * 1024, "book exceeds 16 MiB")
        tmp.chmod(0o644)
        tmp.replace(output)
    finally:
        tmp.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        pack(args.source, args.output)
    except (OSError, ValueError, ET.ParseError) as error:
        sys.exit(f"{args.source}: {error}")


if __name__ == "__main__":
    main()
