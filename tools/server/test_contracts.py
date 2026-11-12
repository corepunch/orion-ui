"""Published compatibility matrix must describe the actual shipped contracts."""
import json
import importlib.util
from pathlib import Path
import re
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


class ContractTests(unittest.TestCase):
    @unittest.skipUnless((ROOT / 'build/bin/orion-service').is_file(), 'build orion-service first')
    def test_host_version_query_matches_contract(self):
        matrix = json.loads((ROOT / 'docs/business-platform-api.json').read_text())
        result = subprocess.run([str(ROOT / 'build/bin/orion-service'), '--version'],
                                capture_output=True, check=True, timeout=10)
        self.assertEqual(json.loads(result.stdout), {'host_api': 1, 'runtime_api': matrix['runtime_api']})

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
        server = (ROOT / 'tools/orion_server.py').read_text()
        self.assertEqual(matrix['service_api'], int(re.search(r'^SERVICE_API = (\d+)', server, re.M)[1]))

    def test_plugin_contract_matches_loader_and_package_validator(self):
        matrix = json.loads((ROOT / 'docs/business-platform-api.json').read_text())
        loader = (ROOT / 'share/lua/orion/plugins.lua').read_text()
        self.assertEqual(matrix['plugin_loader_api'], int(re.search(r'local Plugins = \{api = (\d+)\}', loader)[1]))
        spec = importlib.util.spec_from_file_location('packages_contract', ROOT / 'tools/orion_packages.py')
        packages = importlib.util.module_from_spec(spec); spec.loader.exec_module(packages)
        self.assertEqual(matrix['plugin_package_format'], packages.PACKAGE_FORMAT)
        valid = {'format': matrix['plugin_package_format'], 'kind': 'plugin', 'id': 'example',
                 'publisher': 'local', 'release': '1.0.0', 'runtime_api': matrix['runtime_api'],
                 'interface': 1, 'dependencies': {}}
        self.assertEqual(packages.metadata(valid), valid)
        for invalid in (True, matrix['plugin_package_format'] + 1):
            with self.subTest(format=invalid), self.assertRaises(ValueError):
                packages.metadata(dict(valid, format=invalid))


if __name__ == '__main__':
    unittest.main()
