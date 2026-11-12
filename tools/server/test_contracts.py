"""Published compatibility matrix must describe the actual shipped contracts."""
import json
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


class ContractTests(unittest.TestCase):
    def test_published_versions_match_runtime_and_application_schemas(self):
        matrix = json.loads((ROOT / 'docs/business-platform-api.json').read_text())
        runtime = (ROOT / 'orion/script/runtime.h').read_text()
        self.assertEqual(matrix['runtime_api'], int(re.search(r'#define ORION_RUNTIME_API (\d+)', runtime)[1]))
        api = ET.parse(ROOT / 'apps/ledger/ledger-api.xml').getroot()
        self.assertEqual(matrix['ledger_api'], int(api.attrib['version']))
        for name, path, database in (('ledger', 'apps/ledger/ledger.orion', 'ib'), ('lodge', 'apps/lodge/share/schema.orion', 'lodge')):
            schema = ET.parse(ROOT / path).getroot().find(f".//database[@name='{database}']")
            self.assertIsNotNone(schema)
            self.assertEqual(matrix['applications'][name]['schema'], int(schema.attrib['version']))
        for code in matrix['applications']['ledger']['configurations']:
            self.assertTrue((ROOT / 'apps/ledger/share/configurations' / code / 'configuration.xml').is_file())
        self.assertEqual(matrix['service_api'], 1)  # /v1 is the advertised wire contract.


if __name__ == '__main__':
    unittest.main()
