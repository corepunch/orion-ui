#!/usr/bin/env python3
"""Fetch bank and EDI evidence for review only. Does not write Ledger data."""

import argparse
from collections import Counter
from datetime import datetime
from decimal import Decimal
import getpass
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import warnings
from urllib import error, parse, request


BANK = {
    "production": "https://business.tbank.ru/openapi/api/v1/statement",
    "sandbox": "https://business.tbank.ru/openapi/sandbox/api/v1/statement",
}
EDI_PREFIX = "https://diadoc-api.kontur.ru"
EDI_LIST = EDI_PREFIX + "/V3/GetDocuments"
EDI_CONTENT = EDI_PREFIX + "/V4/GetEntityContent"
MAX_JSON = 2 * 1024 * 1024
MAX_DOCUMENT = 16 * 1024 * 1024
MAX_PAGES = 30
UUID = r"[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}"


class ExchangeError(ValueError):
    """A bounded diagnostic safe to show to the operator."""


def require(value, message):
    if not value:
        raise ExchangeError(message)


def json_data(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON field")
            result[key] = value
        return result

    def constant(_):
        raise ExchangeError("non-finite JSON number")

    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                          parse_float=Decimal, parse_constant=constant)
    except (UnicodeError, ValueError, RecursionError):
        raise ExchangeError("invalid UTF-8 JSON response or review file") from None


def text_field(value, limit, name, *, required=False):
    if value is None and not required:
        return ""
    require(isinstance(value, str) and len(value) <= limit and
            all(ord(ch) >= 32 and ord(ch) != 127 and not 0xD800 <= ord(ch) <= 0xDFFF
                for ch in value) and (value.strip() or not required),
            f"invalid {name}")
    return value


def identity(value, pattern, name):
    require(isinstance(value, str) and re.fullmatch(pattern, value, re.ASCII), f"invalid {name}")
    return value


def utc(value):
    identity(value, r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,6})?Z", "UTC timestamp")
    try:
        return datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError:
        raise ExchangeError("invalid UTC timestamp") from None


def minor(value):
    require(type(value) in (int, Decimal) and Decimal("0.01") <= value <= Decimal("999999999999.99"),
            "invalid account amount")
    exact = Decimal(value)
    rounded = exact.quantize(Decimal("0.01"))
    require(exact == rounded, "sub-kopeck account amount")
    return int(rounded * 100)


def get_json(fetch, endpoint, fields):
    result = json_data(fetch(endpoint, fields, MAX_JSON, "application/json"))
    require(isinstance(result, dict), "provider returned a non-object response")
    return result


def add_row(rows, key, row):
    if key in rows:
        require(rows[key] == row, "conflicting duplicate provider identity; no report written")
    else:
        rows[key] = row


def bank_report(fetch, environment, account, start, end):
    identity(account, r"[0-9]{20}", "bank account")
    start_dt, end_dt = utc(start), utc(end)
    require(start_dt < end_dt and environment in BANK, "invalid statement interval or environment")
    fields = dict(accountNumber=account, operationStatus="Transaction", limit=200,
                  **{"from": start, "to": end})
    rows, cursors = {}, set()
    for _ in range(MAX_PAGES):
        page = get_json(fetch, BANK[environment], fields)
        ops = page.get("operations")
        require(isinstance(ops, list) and len(ops) <= 200, "invalid operations page")
        for op in ops:
            require(isinstance(op, dict) and op.get("accountNumber") == account,
                    "operation has an unexpected account")
            require(op.get("operationStatus") == "Transaction", "operation is not booked")
            require(op.get("accountCurrencyDigitalCode") == "643", "only RUB account amounts are supported")
            when = utc(op.get("operationDate"))
            require(start_dt <= when < end_dt, "operation is outside the requested period")
            op_id = identity(op.get("operationId"), UUID, "operation ID").lower()
            kind = op.get("typeOfOperation")
            require(kind in ("Credit", "Debit"), "unknown operation direction")
            direction = "incoming" if kind == "Credit" else "outgoing"
            party = op.get("counterParty")
            if party is None:
                party = op.get("payer" if kind == "Credit" else "receiver")
            party = {} if party is None else party
            require(isinstance(party, dict), "invalid operation counterparty")
            inn = text_field(party.get("inn"), 12, "counterparty INN")
            require(not inn or re.fullmatch(r"[0-9]{10}(?:[0-9]{2})?", inn), "invalid counterparty INN")
            add_row(rows, op_id, dict(
                source_id=op_id, direction=direction, operation_at=when.isoformat(),
                amount_minor=minor(op.get("accountAmount")), counterparty_inn=inn,
                number=text_field(op.get("documentNumber"), 128, "document number"),
                purpose=text_field(op.get("payPurpose") or op.get("description"), 4096, "payment purpose"),
                counterparty=text_field(party.get("name"), 512, "counterparty name"),
            ))
        cursor = text_field(page.get("nextCursor"), 100, "statement cursor")
        if not cursor:
            return dict(format="ledger-bank-review-v1", provider="tbank", environment=environment,
                        account=account, currency="RUB", period_start=start, period_end_exclusive=end,
                        rows=sorted(rows.values(), key=lambda row: (row["operation_at"], row["source_id"])))
        require(cursor not in cursors and ops, "statement cursor repeated or has an empty page")
        cursors.add(cursor)
        fields["cursor"] = cursor
    raise ExchangeError("statement exceeded page limit; shorten the period")


def invoice_matches(report, raw):
    """Review-only exact candidates, never accounting commands or a claim of settlement."""
    invoices = json_data(raw)
    require(isinstance(invoices, list) and len(invoices) <= 10000, "invalid invoice review list")
    by_key, ids = {}, set()
    for item in invoices:
        require(isinstance(item, dict), "invalid invoice review record")
        row_id = text_field(item.get("ledger_id"), 64, "Ledger invoice ID", required=True)
        require(row_id not in ids, "duplicate Ledger invoice ID")
        ids.add(row_id)
        inn = identity(item.get("counterparty_inn"), r"[0-9]{10}(?:[0-9]{2})?", "invoice INN")
        number = text_field(item.get("payment_reference"), 128, "payment reference", required=True)
        identity(number, r"[\w](?:[\w./-]*[\w])?", "invoice reference")
        value = item.get("outstanding_minor")
        require(type(value) is int and 0 < value <= 99999999999999 and item.get("currency") == "RUB",
                "invalid outstanding invoice amount or currency")
        by_key.setdefault((inn, number, value), []).append(row_id)
    candidates = []
    for movement in report["rows"]:
        # Bank document number is NOT an invoice number; require a standalone
        # reference in the payment purpose, and an exact matching INN/amount.
        ids = []
        if movement["direction"] == "incoming" and movement["counterparty_inn"]:
            seen = set(re.findall(r"(?<![\w./-])\w(?:[\w./-]*\w)?(?![\w/-]|\.\w)", movement["purpose"]))
            for code in seen:
                ids.extend(by_key.get((movement["counterparty_inn"], code, movement["amount_minor"]), []))
        candidates.append((movement["source_id"], sorted(ids)))
    counts = Counter(candidate for _, ids in candidates for candidate in ids)
    return [dict(source_id=op, candidate_ledger_ids=ids,
                 status=("unique_candidate" if len(ids) == 1 and counts[ids[0]] == 1 else
                         "ambiguous" if ids else "unmatched")) for op, ids in candidates]


def edi_box(value):
    return identity(value, rf"(?:{UUID}|[0-9A-Fa-f]{{32}}@diadoc\.ru)", "Diadoc box ID")


def edi_report(fetch, box):
    fields = dict(boxId=edi_box(box), filterCategory="Any.Inbound")
    rows, cursors = {}, set()
    for _ in range(MAX_PAGES):
        page = get_json(fetch, EDI_LIST, fields)
        documents = page.get("Documents")
        require(isinstance(documents, list) and len(documents) <= 100, "invalid Diadoc page")
        require(type(page.get("TotalCount")) is int and page["TotalCount"] >= 0,
                "invalid Diadoc document count")
        more = page.get("HasMoreResults")
        require(more is None or type(more) is bool, "invalid Diadoc continuation flag")
        for doc in documents:
            require(isinstance(doc, dict) and doc.get("DocumentDirection") == "Inbound",
                    "Diadoc returned a document outside the inbox")
            msg = identity(doc.get("MessageId"), UUID, "message ID").lower()
            entity = identity(doc.get("EntityId"), UUID, "entity ID").lower()
            flow = doc.get("DocflowStatus") or {}
            require(isinstance(flow, dict), "invalid Diadoc document flow")
            primary = flow.get("PrimaryStatus") or {}
            require(isinstance(primary, dict), "invalid Diadoc primary status")
            add_row(rows, (msg, entity), dict(
                message_id=msg, entity_id=entity,
                status=text_field(primary.get("StatusText"), 4096, "document status"),
                signature_status=text_field(doc.get("SenderSignatureStatus"), 256, "signature status"),
                document_type=text_field(doc.get("DocumentType"), 256, "document type"),
                document_date=text_field(doc.get("DocumentDate"), 64, "document date"),
                number=text_field(doc.get("DocumentNumber"), 128, "document number"),
                title=text_field(doc.get("Title"), 1024, "document title"),
            ))
        if more is False or (more is None and len(rows) >= page["TotalCount"]):
            return dict(format="ledger-edi-inbox-review-v1", provider="diadoc", box_id=box,
                        rows=list(rows.values()))
        require(documents, "Diadoc reports unreturned documents but returned an empty page")
        cursor = text_field(documents[-1].get("IndexKey"), 2048, "Diadoc cursor", required=True)
        require(cursor not in cursors, "Diadoc cursor repeated")
        cursors.add(cursor)
        fields["afterIndexKey"] = cursor
    raise ExchangeError("Diadoc inbox exceeded page limit")


def edi_document(fetch, box, message, entity):
    return fetch(EDI_CONTENT, dict(boxId=edi_box(box),
                                   messageId=identity(message, UUID, "message ID"),
                                   entityId=identity(entity, UUID, "entity ID")),
                 MAX_DOCUMENT, "application/octet-stream")


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def transport(token, endpoints, opener=None):
    require(isinstance(token, str) and re.fullmatch(r"[!-~]{1,4096}", token), "invalid bearer token")
    endpoints = frozenset(endpoints)
    require(endpoints and endpoints <= {*BANK.values(), EDI_LIST, EDI_CONTENT}, "unknown provider")
    require(not os.environ.get("SSLKEYLOGFILE"), "provider requests disabled while TLS key logging is enabled")
    client = opener or request.build_opener(request.ProxyHandler({}), NoRedirect())

    def fetch(endpoint, fields, limit, accept):
        require(endpoint in endpoints, "provider endpoint refused")
        req = request.Request(endpoint + "?" + parse.urlencode(fields), method="GET",
                              headers={"Accept": accept, "Authorization": "Bearer " + token})
        try:
            with client.open(req, timeout=20) as response:
                require(response.status == 200, "unexpected provider HTTP response")
                raw = response.read(limit + 1)
                require(0 < len(raw) <= limit, "empty or oversized provider response")
                return raw
        except error.HTTPError as exc:
            code = exc.code
            exc.close()
            raise ExchangeError(f"provider HTTP {code}; no output written") from None
        except (error.URLError, OSError):
            raise ExchangeError("provider connection failed; no output written") from None

    return fetch


def private_output(filename, data):
    """Write a complete owner-only report; atomically refuse existing destinations."""
    path = Path(filename)
    fd, temp = tempfile.mkstemp(prefix=".ledger-exchange-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temp, path)
    finally:
        os.unlink(temp)


def main(argv=None, *, connect=transport, read_token=getpass.getpass):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    b = commands.add_parser("bank", help="T-Bank booked RUB statement review")
    b.add_argument("--environment", choices=BANK, default="sandbox")
    b.add_argument("--account", required=True)
    b.add_argument("--from", dest="start", required=True)
    b.add_argument("--to", dest="end", required=True)
    b.add_argument("--invoices", type=Path)
    i = commands.add_parser("edi-inbox", help="Diadoc inbound document status review")
    d = commands.add_parser("edi-document", help="download one Diadoc document as bytes")
    for command in (i, d):
        command.add_argument("--box", required=True)
    d.add_argument("--message", required=True)
    d.add_argument("--entity", required=True)
    for command in (b, i, d):
        command.add_argument("--output", required=True, help="new owner-only output file")
    args = parser.parse_args(argv)
    try:
        require(not os.path.lexists(args.output), "output file already exists")
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            token = read_token("Provider bearer token (input hidden): ")
        if args.command == "bank":
            data = bank_report(connect(token, {BANK[args.environment]}),
                               args.environment, args.account, args.start, args.end)
            if args.invoices:
                with args.invoices.open("rb") as stream:
                    invoices = stream.read(MAX_JSON + 1)
                require(len(invoices) <= MAX_JSON, "invoice review list exceeds size limit")
                data["match_review"] = invoice_matches(data, invoices)
        elif args.command == "edi-inbox":
            data = edi_report(connect(token, {EDI_LIST}), args.box)
        else:
            data = edi_document(connect(token, {EDI_CONTENT}), args.box, args.message, args.entity)
        raw = data if isinstance(data, bytes) else (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode()
        private_output(args.output, raw)
    except (ExchangeError, OSError, EOFError, getpass.GetPassWarning) as exc:
        message = str(exc) if isinstance(exc, ExchangeError) else "cannot read credential, review file, or write output"
        print(f"provider exchange: {message}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
