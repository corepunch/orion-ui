"""Offline contract tests for Ledger provider review tools."""

import contextlib
from decimal import Decimal
import io
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
from unittest import TestCase, main
from urllib import error, parse

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import provider_exchange as exchange


BANK_ACCOUNT = "40702810110011000000"
START = "2026-11-01T00:00:00Z"
END = "2026-12-01T00:00:00Z"
BOX = "12691026-0c40-42b8-9969-23155c739c91"
MESSAGE = "f9ac00f1-0d30-418d-b30c-533942512f74"
ENTITY = "934462af-7816-4471-8a68-0ec61d727d19"
OP1 = "29291d83-d22e-4c38-886f-098d83c5ff93"
OP2 = "87b75135-6abf-4b1c-8194-944194811d92"
INN = "1234567890"


def pages(*bodies):
    pending = iter(bodies)
    calls = []

    def fetch(url, fields, cap, accept):
        calls.append((url, dict(fields), cap, accept))
        value = next(pending)
        return value if isinstance(value, bytes) else json.dumps(value).encode()

    return fetch, calls


def operation(id=OP1, **extra):
    return dict(dict(operationId=id, operationDate="2026-11-11T09:12:00Z",
                     operationStatus="Transaction", accountNumber=BANK_ACCOUNT, typeOfOperation="Credit",
                     accountAmount=144.55, accountCurrencyDigitalCode="643", operationCurrencyDigitalCode="840",
                     documentNumber="BANK-123", payPurpose="Оплата по счету INV-25.",
                     counterParty=dict(inn=INN, name="Example Vendor")), **extra)


def doc(**extra):
    return dict(dict(MessageId=MESSAGE, EntityId=ENTITY, DocumentDirection="Inbound",
                     DocumentType="UniversalTransferDocument", DocumentNumber="1", Title="Example",
                     IndexKey="cursor/+=", DocflowStatus={"PrimaryStatus": {"StatusText": "Ожидается подпись"}}),
                **extra)


def invoice(reference="INV-25", ledger_id="1", amount=14455, counterparty=INN):
    return dict(ledger_id=ledger_id, counterparty_inn=counterparty, payment_reference=reference,
                outstanding_minor=amount, currency="RUB")


class ProviderExchangeTest(TestCase):
    def test_bank_pages_exact_amount_and_conservative_reference_match(self):
        fetch, calls = pages(dict(operations=[operation()], nextCursor="cursor+/="),
                             dict(operations=[operation(), operation(OP2, typeOfOperation="Debit")]))
        review = exchange.bank_report(fetch, "sandbox", BANK_ACCOUNT, START, END)
        self.assertEqual(calls[0][0], exchange.BANK["sandbox"])
        self.assertEqual(calls[0][1]["operationStatus"], "Transaction")
        self.assertEqual(calls[0][1]["to"], END)
        self.assertEqual(calls[1][1]["cursor"], "cursor+/=")
        self.assertEqual([(r["direction"], r["amount_minor"]) for r in review["rows"]],
                         [("incoming", 14455), ("outgoing", 14455)])
        fixtures = json.dumps([invoice(), invoice("BANK-123", "2"),
                               invoice("INV-25", "3", counterparty="0123456789")]).encode()
        matches = exchange.invoice_matches(review, fixtures)
        self.assertEqual(matches[0]["candidate_ledger_ids"], ["1"])
        self.assertEqual(matches[0]["status"], "unique_candidate")
        self.assertEqual(matches[1]["status"], "unmatched")

    def test_ambiguous_candidates_and_multiple_payments(self):
        fetch, _ = pages(dict(operations=[operation(), operation(OP2, payPurpose="INV-25")]))
        review = exchange.bank_report(fetch, "production", BANK_ACCOUNT, START, END)
        for candidate in exchange.invoice_matches(review, json.dumps([invoice()]).encode()):
            self.assertEqual(candidate["status"], "ambiguous")
        review["rows"].pop()
        choices = exchange.invoice_matches(review, json.dumps([invoice(), invoice(ledger_id="2")]).encode())
        self.assertEqual(choices[0]["status"], "ambiguous")

    def test_bank_rejects_conflicts_wrong_account_pending_currencies_and_bad_cursors(self):
        bad = ([dict(operations=[operation(), operation(accountAmount=5)])],
               [dict(operations=[operation(accountNumber="1" * 20)])],
               [dict(operations=[operation(operationStatus="Authorization")])],
               [dict(operations=[operation(accountCurrencyDigitalCode="840")])],
               [dict(operations=[operation(accountAmount=1.001)])],
               [dict(operations=[operation(operationDate=END)])],
               [dict(operations=[operation()], nextCursor="same")] * 2)
        for fixture in bad:
            fetch, _ = pages(*fixture)
            with self.assertRaises(exchange.ExchangeError):
                exchange.bank_report(fetch, "sandbox", BANK_ACCOUNT, START, END)
        self.assertEqual(exchange.minor(Decimal("999999999999.99")), 99999999999999)
        with self.assertRaises(exchange.ExchangeError):
            exchange.minor(Decimal("1e-999999999"))

    def test_edi_pages_status_and_binary_content(self):
        second_entity = "81295799-c0e4-411a-8328-361027d86252"
        fetch, calls = pages(dict(Documents=[doc()], HasMoreResults=True, TotalCount=2),
                             dict(Documents=[doc(), doc(EntityId=second_entity)],
                                  HasMoreResults=False, TotalCount=2),
                             b"<?xml version='1.0'?>\n<Sample/>\x00")
        result = exchange.edi_report(fetch, BOX)
        self.assertEqual(calls[0][1]["filterCategory"], "Any.Inbound")
        self.assertEqual(calls[1][1]["afterIndexKey"], "cursor/+=")
        self.assertEqual(len(result["rows"]), 2)
        self.assertIn("подпись", result["rows"][0]["status"])
        raw = exchange.edi_document(fetch, BOX, MESSAGE, ENTITY)
        self.assertEqual(calls[2][:2], (exchange.EDI_CONTENT, dict(boxId=BOX, messageId=MESSAGE, entityId=ENTITY)))
        self.assertTrue(raw.endswith(b"\x00"))

    def test_edi_rejects_ambiguous_identity(self):
        fetch, _ = pages(dict(Documents=[doc(), doc(Title="Changed")], TotalCount=1))
        with self.assertRaises(exchange.ExchangeError):
            exchange.edi_report(fetch, BOX)

    def test_transport_fixed_get_encoded_cursor_and_no_redirect(self):
        class FakeResponse(io.BytesIO):
            status = 200

        class FakeOpener:
            def open(self, req, timeout):
                self.req = req
                return FakeResponse(b"{}")

        opener = FakeOpener()
        fetch = exchange.transport("fixture-token", {exchange.EDI_LIST}, opener)
        self.assertEqual(fetch(exchange.EDI_LIST, {"afterIndexKey": "cursor+/="}, 2, "application/json"), b"{}")
        self.assertEqual(opener.req.get_method(), "GET")
        self.assertEqual(opener.req.get_header("Authorization"), "Bearer fixture-token")
        self.assertEqual(parse.urlsplit(opener.req.full_url).query, "afterIndexKey=cursor%2B%2F%3D")
        self.assertIsNone(exchange.NoRedirect().redirect_request(opener.req, None, 307, "", {}, "https://invalid.test/"))
        with self.assertRaises(exchange.ExchangeError):
            fetch("https://invalid.test/", {}, 2, "application/json")
        with self.assertRaises(exchange.ExchangeError):
            fetch(exchange.EDI_LIST, {}, 1, "application/json")

    def test_cli_writes_private_complete_output_without_replacing_existing_files(self):
        def fake_connect(token, endpoints):
            self.assertEqual(token, "fixture-secret")
            self.assertEqual(endpoints, {exchange.BANK["sandbox"]})
            return pages(dict(operations=[operation()]))[0]

        cmd = ["bank", "--account", BANK_ACCOUNT, "--from", START, "--to", END]
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / "report.json"
            args = cmd + ["--output", str(out)]
            self.assertEqual(exchange.main(args, connect=fake_connect, read_token=lambda _: "fixture-secret"), 0)
            self.assertEqual(json.loads(out.read_text())["rows"][0]["amount_minor"], 14455)
            self.assertNotIn("fixture-secret", out.read_text())
            if os.name == "posix":
                self.assertEqual(stat.S_IMODE(out.stat().st_mode), 0o600)
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(exchange.main(args, read_token=lambda _: self.fail("prompted unexpectedly")), 1)
            new_out = Path(folder) / "broken.json"
            bad = lambda _t, _e: pages(b'{"operations":[],"operations":[]}')[0]
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(exchange.main(cmd + ["--output", str(new_out)], connect=bad,
                                               read_token=lambda _: "fixture-secret"), 1)
            self.assertFalse(new_out.exists())
            self.assertEqual([p.name for p in Path(folder).iterdir()], ["report.json"])

    def test_http_error_does_not_publish_provider_body(self):
        class FakeOpener:
            def open(self, req, timeout):
                raise error.HTTPError(req.full_url, 401, "sensitive-message", {}, io.BytesIO(b"sensitive-body"))

        fetch = exchange.transport("fixture-token", {exchange.EDI_LIST}, FakeOpener())
        with self.assertRaisesRegex(exchange.ExchangeError, r"^provider HTTP 401; no output written$"):
            fetch(exchange.EDI_LIST, {}, 2, "application/json")


if __name__ == "__main__":
    main()
