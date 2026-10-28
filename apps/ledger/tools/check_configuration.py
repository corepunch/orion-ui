#!/usr/bin/env python3
"""Checks a Ledger configuration directory without building Orion.

    python3 -I apps/ledger/tools/check_configuration.py apps/ledger/share/configurations/ru-2026
    python3 -I apps/ledger/tools/check_configuration.py --all apps/ledger/share/configurations

Run it after a compliance refresh (see apps/ledger/COMPLIANCE.md). It reports:
  - account ids referenced by settings, document types, tax rates, payroll
    taxes, closing and balance rules that name no account;
  - print forms whose template file is missing, or whose report names no
    report table of ledger.orion;
  - print templates with unbalanced {{#block}} ... {{/block}} tags;
  - payroll taxes whose validity range is inverted, whose scale has no zones
    (or zones without a scale), zones out of order, and rows of the same code
    whose validity ranges overlap;
  - the current year not covered by any payroll tax row (a refresh is due);
  - regulations without a reference or date, and a configuration whose
    version does not name the year it covers;
  - script= names (document types, payroll taxes, closing rules) that
    rules.lua and plugins/*.lua do not define as functions.
Exit status 1 when anything is wrong.
"""
import datetime
import os
import re
import sys
import xml.etree.ElementTree as ET

REPORT_TABLES = {"balances", "card", "analysis_rows", "vat_book", "vat_summary", "journal_rows",
                 "cash_book", "asset_rows", "payroll_rows"}
ACCOUNT_FIELDS = {
    "setting": ["cash_account_id", "payroll_account_id", "opening_account_id"],
    "doc_type": ["debit_id", "credit_id", "vat_debit_id", "vat_credit_id", "cost_debit_id", "cost_credit_id"],
    "vat_rate": ["revenue_id", "expense_id", "output_tax_id", "input_tax_id"],
    "payroll_tax": ["debit_id", "credit_id"],
    "closing_rule": ["account_id", "target_id", "via_id", "gain_id", "loss_id"],
    "balance_rule": ["account_id", "currency_account_id"],
}


def check(directory, today=None):
    today = today or datetime.date.today()
    problems = []
    path = os.path.join(directory, "configuration.xml")
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as e:
        return [f"{path}: {e}"]
    accounts = {a.get("id") for a in root.iter("account")}
    codes = [a.get("code") for a in root.iter("account")]
    for code in {c for c in codes if codes.count(c) > 1}:
        problems.append(f"account code {code} appears more than once")
    for tag, fields in ACCOUNT_FIELDS.items():
        for row in root.iter(tag):
            for f in fields:
                v = row.get(f, "0")
                if v not in ("", "0") and v not in accounts:
                    problems.append(f"{tag} id={row.get('id')}: {f}={v} names no account")
    for form in root.iter("print_form"):
        template = os.path.join(directory, "print", form.get("template", ""))
        if not os.path.isfile(template):
            problems.append(f"print_form id={form.get('id')}: template {form.get('template')} missing")
        else:
            problems.extend(check_template(template))
        report = form.get("report", "")
        if report and report not in REPORT_TABLES:
            problems.append(f"print_form id={form.get('id')}: report table {report} unknown")
        if form.get("doc_type_id", "0") not in ("", "0") and form.get("doc_type_id") not in {d.get("id") for d in root.iter("doc_type")}:
            problems.append(f"print_form id={form.get('id')}: doc_type_id names no document type")
    for rate in root.iter("vat_rate"):
        start, end = rate.get("valid_from", ""), rate.get("valid_to", "")
        if start and end and start > end:
            problems.append(f"vat_rate id={rate.get('id')}: inverted validity range")
        if rate.get("treatment", "") not in ("", "standard", "reduced", "zero", "exempt", "outside_scope", "reverse_charge"):
            problems.append(f"vat_rate id={rate.get('id')}: unknown treatment")
    zones = {}
    for z in root.iter("tax_scale"):
        zones.setdefault(z.get("tax_id"), []).append(float(z.get("base_from", "0")))
    by_code = {}
    covered = False
    for tax in root.iter("payroll_tax"):
        tid, code = tax.get("id"), tax.get("code", "")
        valid_from, valid_to = tax.get("valid_from", ""), tax.get("valid_to", "")
        if valid_from and valid_to and valid_from > valid_to:
            problems.append(f"payroll_tax {code}: valid_from after valid_to")
        scaled, n = bool(tax.get("scale", "")), len(zones.get(tid, []))
        if scaled != (n > 0):
            problems.append(f"payroll_tax {code}: scale='{tax.get('scale', '')}' with {n} zones")
        if n and zones[tid] != sorted(zones[tid]):
            problems.append(f"payroll_tax {code}: scale zones are not in ascending order")
        if n and zones[tid][0] != 0:
            problems.append(f"payroll_tax {code}: the first zone must start at 0")
        by_code.setdefault(re.sub(r"_\d{4}$", "", code), []).append((valid_from or "0000-00-00", valid_to or "9999-12-31", code))
        year = f"{today.year}-01-01"
        if (valid_from or "0000") <= year and (valid_to or "9999") >= year:
            covered = True
    for base, rows in by_code.items():
        rows.sort()
        for (f1, t1, c1), (f2, t2, c2) in zip(rows, rows[1:]):
            if f2 <= t1 and f1 != f2:
                problems.append(f"payroll taxes {c1} and {c2}: validity ranges overlap ({f2} <= {t1})")
    if root.find("payroll_taxes") is not None and list(root.iter("payroll_tax")) and not covered:
        problems.append(f"no payroll tax row is in force on {today.year}-01-01: the yearly refresh is due")
    for rule in root.iter("regulation"):
        if not rule.get("reference") or not rule.get("valid_from"):
            problems.append(f"regulation id={rule.get('id')}: needs a reference and valid_from")
    functions = lua_functions(directory)
    for tag in ("doc_type", "payroll_tax", "closing_rule"):
        for row in root.iter(tag):
            validator = row.get("validate_script", "")
            if validator and validator not in functions:
                problems.append(f"{tag} id={row.get('id')}: validate_script={validator} is not defined")
            name = row.get("script", "")
            if name and name not in functions:
                problems.append(f"{tag} id={row.get('id')}: script={name} is not a function of rules.lua or plugins/*.lua")
            if tag == "closing_rule" and row.get("kind") == "script" and not name:
                problems.append(f"closing_rule id={row.get('id')}: kind=script needs script=")
    setting = root.find("settings/setting")
    if setting is not None:
        version, code = setting.get("version", ""), setting.get("code", "")
        year = code[-4:]
        if year.isdigit() and year not in version:
            problems.append(f"settings: version '{version}' does not name the year {year} of '{code}'")
        if not setting.get("currency_code"):
            problems.append("settings: currency_code (ISO 4217) missing")
    return problems


def lua_functions(directory):
    """Names defined as `function name(` in rules.lua and plugins/*.lua."""
    names = set()
    files = [os.path.join(directory, "rules.lua")]
    plugins = os.path.join(directory, "plugins")
    if os.path.isdir(plugins):
        files += [os.path.join(plugins, f) for f in sorted(os.listdir(plugins)) if f.endswith(".lua")]
    for path in files:
        if os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                names.update(re.findall(r"^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", f.read(), re.M))
    return names


def check_template(path):
    problems = []
    with open(path, encoding="utf-8") as f:
        text = f.read()
    opened = re.findall(r"\{\{#([a-z_]+)", text)
    closed = re.findall(r"\{\{/([a-z_]+)\}\}", text)
    if sorted(opened) != sorted(closed):
        problems.append(f"{os.path.basename(path)}: blocks opened {opened} but closed {closed}")
    for name in opened:
        if name not in REPORT_TABLES and name not in {"lines", "doc_lines", "payroll_lines", "asset_lines", "entries", "tax_totals"}:
            problems.append(f"{os.path.basename(path)}: unknown block table {name}")
    return problems


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    dirs = [args[0]]
    if "--all" in argv:
        dirs = sorted(os.path.join(args[0], d) for d in os.listdir(args[0]) if os.path.isdir(os.path.join(args[0], d)))
    status = 0
    for d in dirs:
        problems = check(d)
        print(f"{d}: {'ok' if not problems else str(len(problems)) + ' problem(s)'}")
        for p in problems:
            print("  " + p)
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
