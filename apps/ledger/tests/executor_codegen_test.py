"""Reject executor capabilities that could bypass the document ownership contract."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('executor_codegen', ROOT / 'tools/generate_executor_contract.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class ExecutorContracts(unittest.TestCase):
    def reject(self, operation):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'executor.xml'
            schema = ROOT / 'apps/ledger/ledger.orion'
            source.write_text(f'<executors schema="{schema}">{operation}</executors>', encoding='utf-8')
            with self.assertRaises((ValueError, KeyError)):
                generator.generate(source)

    def test_identity_is_not_a_patch_field(self):
        self.reject('<operation name="unsafe" impl="unsafe" recalculate="false"><document fields="id"/></operation>')

    def test_unknown_fields_are_not_exposed(self):
        self.reject('<operation name="unsafe" impl="unsafe" recalculate="false"><document fields="unknown"/></operation>')

    def test_child_table_requires_document_ownership(self):
        self.reject('<operation name="unsafe" impl="unsafe" recalculate="false"><replace table="counterparties" parent="id" fields="name"/></operation>')

    def test_script_cannot_choose_another_parent(self):
        self.reject('<operation name="unsafe" impl="unsafe" recalculate="false"><replace table="payroll_lines" parent="document_id" fields="document_id,amount"/></operation>')

    def test_invalid_recalculation_mode_is_rejected(self):
        self.reject('<operation name="unsafe" impl="unsafe" recalculate="sometimes"/>')


if __name__ == '__main__':
    unittest.main()
