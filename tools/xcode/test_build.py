"""The app bundle carries every resource the runtime reads from share/."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import build
from common import ROOT, theme_sheets


class ResourceBundleTests(unittest.TestCase):
    def test_theme_sheets_are_bundled_where_the_runtime_reads_them(self):
        sheets = [sheet.name for sheet in theme_sheets(ROOT)]
        self.assertIn('base.css', sheets)
        for platform, resources in (('iphoneos', ''), ('iphonesimulator', ''), ('macosx', 'Contents/Resources')):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory(prefix='orion-bundle-') as directory:
                root = Path(directory) / 'root'
                for name in ('share/fonts/font.ttf', 'apps/groove/share/locale/ru.xml'):
                    (root / name).parent.mkdir(parents=True, exist_ok=True)
                    (root / name).touch()
                (root / 'orion/themes').mkdir(parents=True)
                for name in sheets:
                    (root / 'orion/themes' / name).write_text(':root {}\n')
                bundle = Path(directory) / 'groove.app'
                (bundle / 'Contents').mkdir(parents=True)
                env = dict(TARGET_BUILD_DIR=directory, WRAPPER_NAME='groove.app', PLATFORM_NAME=platform)
                for _ in range(2):  # an incremental build replaces the previous copy
                    with mock.patch.dict(os.environ, env):
                        build.resources(root, Path(directory) / 'derived')
                themes = bundle / resources / 'share/orion/themes'
                self.assertEqual(sorted(p.name for p in themes.iterdir()), sheets)
                # theme_read_share() resolves <executable dir>/../share/orion/<sheet>.
                executable_dir = bundle / ('Contents/MacOS' if resources else 'bin')
                executable_dir.mkdir(exist_ok=True)
                self.assertTrue((executable_dir / '../share/orion/themes/base.css').exists())


if __name__ == '__main__':
    unittest.main()
