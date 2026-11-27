#!/usr/bin/env python3
"""Compare independently normalized 1C reports and exported Ledger controls."""
import argparse
import csv
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import sys
import uuid

PROFILES = {
    "trial-balance": (("account", "subaccount"), (
        "opening_debit", "opening_credit", "debit", "credit", "closing_debit", "closing_credit")),
    "payroll": (("month", "person"), ("accrued", "paid", "withheld")),
    "contributions": (("month", "person", "contribution", "source_document"), ("amount",)),
    "depreciation": (("month", "asset", "expense_account"), (
        "opening", "additions", "depreciation", "disposals", "closing")),
    "vat-book": (("quarter", "book", "invoice", "event", "sheet", "source_line", "rate"),
                 ("net", "vat", "gross")),
    "entities": (("kind", "source_id"), ("target_id", "organization_tax_id", "owner_id")),
    "attachments": (("file_guid",), ("owner_guid", "owner_type", "volume_guid", "bytes", "sha256")),
}

# Monetary columns contain exact, signed minor-unit integers, never decimals.
TEXT_VALUES = {"target_id", "organization_tax_id", "owner_id", "owner_guid", "owner_type",
               "volume_guid", "sha256"}
OPTIONAL = {"subaccount", "expense_account", "owner_id"}
HEX = re.compile(r"[0-9a-f]{64}")
INTEGER = re.compile(r"(?:0|-?[1-9][0-9]{0,21})")


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def guid(value):
    parsed = uuid.UUID(value)
    if value != str(parsed) or not parsed.int:
        raise ValueError("expected canonical non-empty GUID")
    return value


def iso_date(value):
    parsed = dt.date.fromisoformat(value)
    if value != parsed.isoformat():
        raise ValueError("expected ISO calendar date")
    return parsed


def exact_integer(value):
    if not INTEGER.fullmatch(value):
        raise ValueError("expected exact integer without separator or scientific notation")
    return int(value)


def require_sha(value):
    if not isinstance(value, str) or not HEX.fullmatch(value):
        raise ValueError("expected lowercase SHA-256")
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate manifest field: " + key)
        result[key] = value
    return result


def validate_manifest(path, kind, source, reference, ledger):
    m = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if not isinstance(m, dict) or m.get("format") != "ledger-1c-reconciliation-v1" or m.get("profile") != kind:
        raise ValueError("manifest format or report profile does not match")
    for field in ("organization", "source_report", "source_version", "ledger_version", "settings"):
        if not isinstance(m.get(field), str) or not m[field].strip():
            raise ValueError("manifest requires " + field)
    start, end = (iso_date(m[k]) for k in ("period_start", "period_end"))
    if end < start:
        raise ValueError("manifest period is reversed")
    for field, file in (("snapshot_sha256", source), ("reference_sha256", reference), ("ledger_sha256", ledger)):
        if require_sha(m[field]) != sha256(file):
            raise ValueError("content does not match " + field)
    if m["reference_sha256"] == m["ledger_sha256"] and reference.samefile(ledger):
        raise ValueError("reference and Ledger inputs must be separate files")
    return m, start, end


def valid_period(field, value, period):
    start, end = period
    if field == "month":
        if len(value) != 7 or not start.replace(day=1) <= iso_date(value + "-01") <= end:
            raise ValueError("month is outside the manifest period")
    if field == "quarter":
        match = re.fullmatch(r"([0-9]{4})-Q([1-4])", value)
        if not match:
            raise ValueError("expected quarter YYYY-QN")
        first = dt.date(int(match[1]), (int(match[2]) - 1) * 3 + 1, 1)
        start_q = start.replace(month=(start.month - 1) // 3 * 3 + 1, day=1)
        if not start_q <= first <= end:
            raise ValueError("quarter is outside the manifest period")


def payload_check(root, row):
    if root is None:
        raise ValueError("attachment profile requires both payload directories")
    parts = row["file_guid"] + ".bin"
    file = (root / parts).resolve(strict=True)
    if file.parent != root or file.name != parts or not file.is_file():
        raise ValueError("payload must be a regular GUID.bin inside its supplied directory")
    if file.stat().st_size != exact_integer(row["bytes"]) or sha256(file) != row["sha256"]:
        raise ValueError("payload length or SHA-256 does not match its CSV row")


def read_rows(path, profile, period, payload_dir=None):
    keys, values = PROFILES[profile]
    rows = {}
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        if reader.fieldnames is None or tuple(reader.fieldnames) != keys + values:
            raise ValueError(f"{path}: expected columns {','.join(keys + values)}")
        for row in reader:
            try:
                if None in row or any(value is None for value in row.values()):
                    raise ValueError("extra or missing cells")
                for field, value in row.items():
                    if value != value.strip() or any(ord(c) < 32 for c in value) or (
                            not value and field not in OPTIONAL):
                        raise ValueError("invalid or empty " + field)
                    if field.endswith("_guid"):
                        guid(value)
                    if field == "sha256":
                        require_sha(value)
                    valid_period(field, value, period)
                if profile == "vat-book" and row["book"] not in ("purchase", "sales"):
                    raise ValueError("book must be purchase or sales")
                if profile == "attachments":
                    payload_check(payload_dir, row)
                key = tuple(row[k] for k in keys)
                if key in rows:
                    raise ValueError("duplicate identity " + repr(key))
                rows[key] = tuple(row[k] if k in TEXT_VALUES else exact_integer(row[k]) for k in values)
            except (ValueError, OSError) as exc:
                raise ValueError(f"{path}:{reader.line_num}: {exc}") from exc
    if not rows:
        raise ValueError(f"{path}: no rows to reconcile")
    return rows


def differences(keys, values, reference, ledger):
    for identity in sorted(reference.keys() | ledger.keys()):
        row = dict(zip(keys, identity))
        if identity not in reference or identity not in ledger:
            where = "missing_in_reference" if identity not in reference else "missing_in_ledger"
            yield {"key": row, "problem": where}
        else:
            for column, expected, actual in zip(values, reference[identity], ledger[identity]):
                if expected != actual:
                    yield {"key": row, "problem": "different_value", "field": column,
                           "reference": str(expected), "ledger": str(actual)}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("profile", choices=PROFILES)
    p.add_argument("--manifest", required=True, type=Path)
    p.add_argument("--snapshot", required=True, type=Path, help="untouched 1C snapshot (.1CD)")
    p.add_argument("--reference", required=True, type=Path, help="CSV normalized from native 1C report")
    p.add_argument("--ledger", required=True, type=Path, help="CSV normalized from Ledger result")
    p.add_argument("--reference-payloads", type=Path)
    p.add_argument("--ledger-payloads", type=Path)
    args = p.parse_args(argv)
    try:
        m, start, end = validate_manifest(args.manifest, args.profile, args.snapshot, args.reference, args.ledger)
        paths = [args.reference_payloads, args.ledger_payloads]
        if args.profile == "attachments":
            if any(path is None for path in paths):
                raise ValueError("attachment profile requires both payload directories")
            paths = [path.resolve(strict=True) for path in paths]
            if paths[0].samefile(paths[1]):
                raise ValueError("reference and Ledger payload directories must be separate")
        elif any(paths):
            raise ValueError("payload directories apply only to attachments")
        left = read_rows(args.reference, args.profile, (start, end), paths[0])
        right = read_rows(args.ledger, args.profile, (start, end), paths[1])
        delta = list(differences(*PROFILES[args.profile], left, right))
        print(json.dumps({"profile": args.profile, "snapshot_sha256": m["snapshot_sha256"],
                          "organization": m["organization"], "period_start": m["period_start"],
                          "period_end": m["period_end"], "reference_rows": len(left),
                          "ledger_rows": len(right), "differences": delta}, ensure_ascii=False))
        return 1 if delta else 0
    except (KeyError, TypeError, ValueError, OSError, csv.Error) as exc:
        print("Reconciliation failed: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
