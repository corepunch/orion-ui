"""Keep generated Xcode sources out of native and Make unity builds."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from common import ROOT, unity_sources


class SourceDiscoveryTests(unittest.TestCase):
    def test_build_outputs_are_excluded_and_nested_sources_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix='orion-sources-') as directory:
            root = Path(directory)
            app = root / 'apps/groove'
            sources = ['controller.c', 'datasource/library.c', 'dialogs/export.c']
            excluded = ['components/card.c', 'tests/test.c', 'reel/render.c']
            generated = ['Build/Intermediates.noindex/DerivedSources/u_groove.c',
                         'build/generated/u_groove.c', 'DerivedData/Sources/u_groove.c',
                         'groove.xcodeproj/Generated/u_groove.c',
                         'groove.xcworkspace/Generated/u_groove.c',
                         'nested/Build/DerivedSources/u_groove.c']
            for name in ['main.c', 'groove.h', 'components/card.h', *sources, *excluded, *generated]:
                path = app / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            for name in generated:
                (app / name).with_suffix('.h').touch()

            expected = [app / name for name in sorted(sources)]
            self.assertEqual(unity_sources(root, 'groove')['u_groove.c'], expected + [app / 'main.c'])
            makefile = root / 'scan.mk'
            makefile.write_text(f'include {ROOT}/tools/app_sources.mk\n'
                                'sources:\n\t@$(call app_unity_sources,"$(APP_DIR)")\n'
                                "headers:\n\t@$(call find_app_sources,\"$(APP_DIR)\",-name '*.h')\n")
            for target, paths in [('sources', expected),
                                  ('headers', [app / 'groove.h', app / 'components/card.h'])]:
                with self.subTest(target=target):
                    output = subprocess.check_output(['make', '-s', '-f', str(makefile),
                                                      f'APP_DIR={app}', target], text=True)
                    self.assertEqual(sorted(output.splitlines()), sorted(str(p) for p in paths))


if __name__ == '__main__':
    unittest.main()
