"""Generator contracts, including sizes that are not complete trade cycles."""
import importlib.util
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

PATH = Path(__file__).resolve().parents[1] / 'tools/generate_large_infobase.py'
spec = importlib.util.spec_from_file_location('generator', PATH)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class GeneratorTest(unittest.TestCase):
    def test_workloads(self):
        for country in ('ru', 'de'):
            for size in (100, 1001, 10000):
                with self.subTest(country=country, size=size):
                    data = generator.generate(country, size)
                    self.assertEqual(data, generator.generate(country, size))
                    self.assertNotEqual(data, generator.generate(country, size, 42))
                    root = ET.fromstring(data)
                    docs = root.findall('./documents/document')
                    self.assertEqual(len(docs), size)
                    self.assertEqual([int(d.get('id')) for d in docs], list(range(1, size + 1)))
                    self.assertEqual([d.get('date') for d in docs], sorted(d.get('date') for d in docs))
                    self.assertEqual(len({d.get('number') for d in docs}), size)
                    cfg = ET.parse(generator.CONFIGS / f'{country}-2026/configuration.xml')
                    types = {t.get('id'): t.get('code') for t in cfg.findall('./doc_types/doc_type')}
                    for code in ('payroll', 'payout', 'closing'):
                        self.assertEqual(sum(types[d.get('doc_type_id')] == code for d in docs), 12)
                    ids = {d.get('id') for d in docs}
                    item_ids = {r.get('id') for r in root.find('items')}
                    employee_ids = {r.get('id') for r in root.find('employees')}
                    party_ids = {r.get('id') for r in root.find('counterparties')}
                    rate_ids = {r.get('id') for r in cfg.findall('./vat_rates/vat_rate')}
                    for d in docs:
                        self.assertIn(d.get('counterparty_id'), party_ids | {'0'})
                    for line in root.find('doc_lines'):
                        self.assertIn(line.get('item_id'), item_ids)
                        self.assertIn(line.get('vat_rate_id'), rate_ids)
                    for line in root.find('payroll_lines'):
                        self.assertIn(line.get('employee_id'), employee_ids)
                    for table in ('entries', 'doc_lines', 'payroll_lines'):
                        for row in root.find(table):
                            self.assertIn(row.get('document_id'), ids)
                    for table in ('organizations', 'counterparties', 'employees', 'items'):
                        self.assertTrue(all(r.get('name').startswith('SYNTHETIC') for r in root.find(table)))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            command = [sys.executable, str(PATH), '--country', 'de', '--documents', '100', '--output', directory]
            subprocess.run(command, check=True, capture_output=True)
            path = Path(directory) / 'infobase.xml'
            original = path.read_bytes()
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(original, path.read_bytes())

    def test_invalid_input(self):
        for country, size in (('us', 1000), ('ru', 99), ('de', 1000001)):
            with self.assertRaises(ValueError):
                generator.generate(country, size)


if __name__ == '__main__':
    unittest.main()
