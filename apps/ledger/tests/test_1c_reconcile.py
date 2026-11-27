#!/usr/bin/env python3
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools" / "reconcile_1c.py"
FILE = "12345678-1234-5678-9abc-012345678901"
OWNER = "23456789-1234-5678-9abc-012345678901"
VOLUME = "34567890-1234-5678-9abc-012345678901"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ReconcileTest(unittest.TestCase):
    def setUp(self):
        folder = TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.base = Path(folder.name)
        self.source = self.base / "source.1CD"
        self.source.write_bytes(b"immutable sample")
        self.left = self.base / "1c.csv"
        self.right = self.base / "ledger.csv"
        self.manifest = self.base / "manifest.json"

    def export(self, header, left, right):
        for path, rows in ((self.left, left), (self.right, right)):
            with path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream)
                writer.writerow(header.split(","))
                writer.writerows(rows)

    def command(self, profile, *extra, changes=None):
        m = {"format": "ledger-1c-reconciliation-v1", "profile": profile,
             "organization": "Тест · ИНН 001", "source_report": "1C native report",
             "source_version": "3.0-test", "ledger_version": "review-sha",
             "settings": "RUB, all accounts", "period_start": "2025-01-01", "period_end": "2025-12-31",
             "snapshot_sha256": sha(self.source), "reference_sha256": sha(self.left),
             "ledger_sha256": sha(self.right)}
        m.update(changes or {})
        self.manifest.write_text(json.dumps(m), encoding="utf-8")
        return subprocess.run([sys.executable, str(TOOL), profile, "--manifest", str(self.manifest),
                               "--snapshot", str(self.source), "--reference", str(self.left),
                               "--ledger", str(self.right), *extra], text=True, capture_output=True)

    def test_precise_values_and_missing_rows(self):
        header = "account,subaccount,opening_debit,opening_credit,debit,credit,closing_debit,closing_credit"
        one = ["001", "", 0, 0, 9007199254740993, 0, 9007199254740993, 0]
        two = ["002", "Первый", 0, 0, 0, 1, 0, 1]
        self.export(header, [one, two], [two, one])
        clean = self.command("trial-balance")
        self.assertEqual(clean.returncode, 0, clean.stderr)
        self.assertEqual(json.loads(clean.stdout)["differences"], [])
        self.export(header, [one, two], [["001", "", 0, 0, 9007199254740992, 0, 9007199254740993, 0]])
        diff = self.command("trial-balance")
        self.assertEqual(diff.returncode, 1)
        findings = json.loads(diff.stdout)["differences"]
        self.assertEqual(findings[0]["reference"], "9007199254740993")
        self.assertEqual(findings[1]["problem"], "missing_in_ledger")
        self.assertIn("002", findings[1]["key"].values())

    def test_bad_inputs_never_mark_a_match(self):
        header = "month,person,accrued,paid,withheld"
        valid = ["2025-02", "000007", 301, 270, 31]
        self.export(header, [valid, valid], [valid])
        self.assertIn("duplicate identity", self.command("payroll").stderr)
        self.export(header, [valid], [["2026-02", "000007", 301, 270, 31]])
        self.assertIn("outside the manifest period", self.command("payroll").stderr)
        self.export(header, [valid], [["2025-02", "000007", "3.01", 270, 31]])
        self.assertIn("expected exact integer", self.command("payroll").stderr)
        self.export(header, [], [])
        self.assertEqual(self.command("payroll").returncode, 2)
        bad_hash = self.command("payroll", changes={"snapshot_sha256": "1" * 64})
        self.assertEqual(bad_hash.returncode, 2)
        self.assertIn("snapshot_sha256", bad_hash.stderr)

    def test_vat_correction_cannot_hide_behind_equal_totals(self):
        header = "quarter,book,invoice,event,sheet,source_line,rate,net,vat,gross"
        normal = ["2025-Q1", "sales", "invoice-1", "issue", "regular", "1", "20", 1000, 200, 1200]
        correction = ["2025-Q1", "sales", "invoice-1", "cancel", "additional", "1", "20", -1000, -200, -1200]
        self.export(header, [normal, correction], [normal, correction])
        self.assertEqual(self.command("vat-book").returncode, 0)
        self.export(header, [normal, correction], [normal, correction[:4] + ["regular"] + correction[5:]])
        result = self.command("vat-book")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(json.loads(result.stdout)["differences"]), 2)

    def test_volumes_compare_actual_saved_bytes_and_guid_mapping(self):
        header = "file_guid,owner_guid,owner_type,volume_guid,bytes,sha256"
        left_dir, right_dir = self.base / "source-files", self.base / "ledger-files"
        left_dir.mkdir()
        right_dir.mkdir()
        left_file, right_file = left_dir / (FILE + ".bin"), right_dir / (FILE + ".bin")
        left_file.write_bytes(b"the control file")
        right_file.write_bytes(left_file.read_bytes())
        row = [FILE, OWNER, "invoice", VOLUME, left_file.stat().st_size, sha(left_file)]
        self.export(header, [row], [row])
        cli = ("--reference-payloads", str(left_dir), "--ledger-payloads", str(right_dir))
        self.assertEqual(self.command("attachments", *cli).returncode, 0)
        self.export(header, [row], [row[:1] + [VOLUME] + row[2:]])
        result = self.command("attachments", *cli)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout)["differences"][0]["field"], "owner_guid")
        self.export(header, [row], [row])
        right_file.write_bytes(b"corrupt payload!")
        broken = self.command("attachments", *cli)
        self.assertEqual(broken.returncode, 2)
        self.assertIn("payload length or SHA-256", broken.stderr)
        right_file.unlink()
        try:
            right_file.symlink_to(left_file)
        except OSError:
            # Some Windows configurations do not allow unprivileged symlinks.
            return
        self.assertIn("inside its supplied directory", self.command("attachments", *cli).stderr)


if __name__ == "__main__":
    unittest.main()
