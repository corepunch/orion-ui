#!/usr/bin/env python3
"""Integration checks for Xcode's project navigator file references."""
import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest

import generate


class FileReferenceTests(unittest.TestCase):
    def test_cloud_build_configurations_enable_shine(self):
        with tempfile.TemporaryDirectory(prefix='orion shine ') as directory:
            output = Path(directory)
            subprocess.run(['python3', str(Path(generate.__file__)), 'groove', '--unsigned',
                            '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
            for bundle in (output / 'groove.xcodeproj', generate.ROOT / 'apps/groove/groove.xcodeproj'):
                objects = generate.read_project(bundle / 'project.pbxproj')
                project = next(obj for obj in objects.values() if obj['isa'] == 'PBXProject')
                configs = objects[project['buildConfigurationList']]['buildConfigurations']
                self.assertEqual({objects[uid]['name'] for uid in configs}, {'Debug', 'Release'})
                for uid in configs:
                    self.assertIn('GROOVE_USE_SHINE', objects[uid]['buildSettings']['GCC_PREPROCESSOR_DEFINITIONS'])
                for target in (obj for obj in objects.values() if obj['isa'] == 'PBXNativeTarget'):
                    for uid in objects[target['buildConfigurationList']]['buildConfigurations']:
                        definitions = objects[uid]['buildSettings'].get('GCC_PREPROCESSOR_DEFINITIONS', ['$(inherited)'])
                        self.assertTrue('$(inherited)' in definitions or 'GROOVE_USE_SHINE' in definitions)

    def test_generated_sources_have_declared_build_outputs(self):
        with tempfile.TemporaryDirectory(prefix='orion xcode ') as directory:
            output = Path(directory)
            subprocess.run(['python3', str(Path(generate.__file__)), 'groove', '--unsigned',
                            '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
            for bundle in (output / 'groove.xcodeproj', generate.ROOT / 'apps/groove/groove.xcodeproj'):
                objects = generate.read_project(bundle / 'project.pbxproj')
                for target in (obj for obj in objects.values() if obj['isa'] == 'PBXNativeTarget'):
                    outputs = set()
                    for uid in target['buildPhases']:
                        phase = objects[uid]
                        outputs.update(phase.get('outputPaths', []))
                        if phase['isa'] != 'PBXSourcesBuildPhase':
                            continue
                        for source in phase['files']:
                            ref = objects[objects[source]['fileRef']]
                            if ref['sourceTree'] == 'DERIVED_FILE_DIR':
                                self.assertIn('$(DERIVED_FILE_DIR)/' + ref['path'], outputs,
                                              f"{bundle}: {target['name']}")

    def test_microphone_privacy_and_capture_are_in_both_targets(self):
        with tempfile.TemporaryDirectory(prefix='orion microphone ') as directory:
            output = Path(directory)
            subprocess.run(['python3', str(Path(generate.__file__)), 'groove', '--unsigned',
                            '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
            bundle = output / 'groove.xcodeproj'
            objects = generate.read_project(bundle / 'project.pbxproj')
            for target in (obj for obj in objects.values() if obj['isa'] == 'PBXNativeTarget'):
                phase = next(objects[uid] for uid in target['buildPhases'] if objects[uid]['isa'] == 'PBXSourcesBuildPhase')
                files = [objects[objects[uid]['fileRef']]['path'] for uid in phase['files']]
                self.assertIn('u_audio_input.m', files)
                self.assertIn('u_media_export.m', files)
                config = objects[objects[target['buildConfigurationList']]['buildConfigurations'][0]]['buildSettings']
                self.assertIn('AVFoundation', config['OTHER_LDFLAGS'])
                info = config['INFOPLIST_FILE'].replace('$(PROJECT_FILE_PATH)', str(bundle))
                self.assertTrue(plistlib.loads(Path(info).read_bytes())['NSMicrophoneUsageDescription'])
                if target['name'] == 'groove-ipad':
                    self.assertTrue(plistlib.loads(Path(info).read_bytes())['NSPhotoLibraryAddUsageDescription'])
                    self.assertIn('PhotosUI', config['OTHER_LDFLAGS'])
                    self.assertIn('ImageIO', config['OTHER_LDFLAGS'])
                else:
                    self.assertIn('UniformTypeIdentifiers', config['OTHER_LDFLAGS'])
            permissions = plistlib.loads((bundle / 'Generated/macos.entitlements').read_bytes())
            self.assertIs(permissions['com.apple.security.device.audio-input'], True)

    def test_macos_sandbox_entitlements_are_wired_to_both_configurations(self):
        with tempfile.TemporaryDirectory(prefix='orion xcode ') as directory:
            output = Path(directory)
            subprocess.run(['python3', str(Path(generate.__file__)), 'groove', '--unsigned',
                            '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
            for bundle in (output / 'groove.xcodeproj', generate.ROOT / 'apps/groove/groove.xcodeproj'):
                objects = generate.read_project(bundle / 'project.pbxproj')
                targets = [obj for obj in objects.values() if obj['isa'] == 'PBXNativeTarget']
                for target in targets:
                    configs = objects[target['buildConfigurationList']]['buildConfigurations']
                    self.assertEqual({objects[uid]['name'] for uid in configs}, {'Debug', 'Release'})
                    for uid in configs:
                        settings = objects[uid]['buildSettings']
                        if target['name'] == 'groove-ipad':
                            self.assertNotIn('ENABLE_APP_SANDBOX', settings)
                            continue
                        self.assertEqual(settings['CODE_SIGN_STYLE'], 'Automatic')
                        self.assertEqual(settings['ENABLE_APP_SANDBOX'], 'YES')
                        self.assertEqual(settings['ENABLE_USER_SELECTED_FILES'], 'readwrite')
                        path = settings['CODE_SIGN_ENTITLEMENTS'].replace('$(PROJECT_FILE_PATH)', str(bundle))
                        entitlements = plistlib.loads(Path(path).read_bytes())
                        self.assertIs(entitlements['com.apple.security.app-sandbox'], True)
                        self.assertIs(entitlements['com.apple.security.files.user-selected.read-write'], True)

    def test_navigator_and_build_phases_use_only_unity_sources(self):
        with tempfile.TemporaryDirectory(prefix='orion xcode ') as directory:
            output = Path(directory) / 'nested' / 'project'
            subprocess.run(['python3', str(Path(generate.__file__)), 'groove', '--unsigned',
                            '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
            project = output / 'groove.xcodeproj/project.pbxproj'
            data = json.loads(subprocess.check_output(['plutil', '-convert', 'json', '-o', '-', str(project)]))
            objects = data['objects']
            seen, generated = set(), set()

            def visit(uid, parent):
                obj = objects[uid]
                tree = obj.get('sourceTree', '<group>')
                if tree == 'BUILT_PRODUCTS_DIR':
                    return
                if tree == 'DERIVED_FILE_DIR':
                    generated.add(uid)
                    return
                self.assertIn(tree, ('SOURCE_ROOT', '<group>'), obj)
                path = (output if tree == 'SOURCE_ROOT' else parent) / obj.get('path', '')
                if obj['isa'] == 'PBXFileReference':
                    self.assertTrue(path.exists(), f'Missing navigator file: {path}')
                    seen.add(path.resolve())
                for child in obj.get('children', []):
                    visit(child, path)

            visit(objects[data['rootObject']]['mainGroup'], output)
            self.assertIn((generate.ROOT / 'apps/groove/groove.orion').resolve(), seen)
            self.assertIn((generate.ROOT / 'apps/groove/share').resolve(), seen)
            self.assertFalse(any(path.suffix in ('.c', '.h', '.m') for path in seen))
            self.assertEqual(generated, {uid for uid, obj in objects.items() if obj.get('sourceTree') == 'DERIVED_FILE_DIR'})
            main = objects[objects[data['rootObject']]['mainGroup']]
            groups = {objects[uid]['name']: objects[uid] for uid in main['children']}
            self.assertEqual({objects[uid]['path'] for uid in groups['Engine']['children']},
                             {'u_user.c', 'u_kernel.c', 'u_commctl.c', 'u_commdlg.c'})
            for target in (obj for obj in objects.values() if obj['isa'] == 'PBXNativeTarget'):
                phase = next(objects[uid] for uid in target['buildPhases'] if objects[uid]['isa'] == 'PBXSourcesBuildPhase')
                refs = [objects[objects[uid]['fileRef']] for uid in phase['files']]
                platform = target['name'].removeprefix('groove-')
                self.assertEqual({ref['path'] for ref in refs},
                                 {'u_user.c', 'u_kernel.c', 'u_commctl.c', 'u_commdlg.c', 'u_groove.c',
                                  'u_audio_input.m', 'u_media_export.m', 'u_platform_' + platform + '.m'})
                self.assertTrue(all(ref['sourceTree'] == 'DERIVED_FILE_DIR' for ref in refs))
                for uid in phase['files']:
                    build_file = objects[uid]
                    ref = objects[build_file['fileRef']]
                    self.assertEqual(build_file.get('settings', {}).get('COMPILER_FLAGS'),
                                     '-fobjc-arc' if ref['path'] == 'u_platform_ipad.m' else None)
            self.assertFalse(any(output.rglob('*.c')), 'Generated C sources must stay in DerivedData')

    def test_macos_distribution_keeps_automatic_signing_and_notarization_requirements(self):
        with tempfile.TemporaryDirectory(prefix='orion signing ') as directory:
            output = Path(directory)
            subprocess.run(['python3', str(Path(generate.__file__)), 'groove', '--unsigned',
                            '--team', 'TESTTEAM', '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
            for bundle in (output / 'groove.xcodeproj', generate.ROOT / 'apps/groove/groove.xcodeproj'):
                objects = generate.read_project(bundle / 'project.pbxproj')
                target = next(obj for obj in objects.values()
                              if obj['isa'] == 'PBXNativeTarget' and obj['name'] == 'groove-macos')
                for uid in objects[target['buildConfigurationList']]['buildConfigurations']:
                    config = objects[uid]
                    settings = config['buildSettings']
                    self.assertEqual(settings['CODE_SIGN_STYLE'], 'Automatic')
                    self.assertEqual(settings['CODE_SIGN_IDENTITY'], 'Apple Development')
                    self.assertEqual(settings['ENABLE_HARDENED_RUNTIME'], 'YES')
                    if config['name'] == 'Release':
                        self.assertIn('--timestamp', settings['OTHER_CODE_SIGN_FLAGS'])
                        self.assertEqual(settings['CODE_SIGN_INJECT_BASE_ENTITLEMENTS'], 'NO')
                        release = settings
                export = plistlib.loads((bundle / 'Generated/DeveloperIDExportOptions.plist').read_bytes())
                self.assertEqual(export['method'], 'developer-id')
                self.assertEqual(export['signingStyle'], 'automatic')
                self.assertEqual(export['teamID'], release['DEVELOPMENT_TEAM'])

    def test_regeneration_preserves_signing_and_shared_schemes(self):
        with tempfile.TemporaryDirectory(prefix='orion xcode ') as directory:
            output = Path(directory)
            command = ['python3', str(Path(generate.__file__)), 'groove', '--unsigned', '--output', str(output)]
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            project = output / 'groove.xcodeproj/project.pbxproj'
            data = json.loads(subprocess.check_output(['plutil', '-convert', 'json', '-o', '-', str(project)]))
            for obj in data['objects'].values():
                if obj['isa'] == 'XCBuildConfiguration':
                    obj['buildSettings']['DEVELOPMENT_TEAM'] = 'TESTTEAM'
                    obj['buildSettings']['PRODUCT_BUNDLE_IDENTIFIER'] = 'example.groove'
            project.write_text(generate.encode(data))
            scheme = output / 'groove.xcodeproj/xcshareddata/xcschemes/groove-ipad.xcscheme'
            scheme.write_text(scheme.read_text().replace('version="1.3"', 'version="1.7"'))
            original_scheme = scheme.read_text()
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            refreshed = json.loads(subprocess.check_output(['plutil', '-convert', 'json', '-o', '-', str(project)]))
            for obj in refreshed['objects'].values():
                if obj['isa'] == 'XCBuildConfiguration':
                    self.assertEqual(obj['buildSettings']['DEVELOPMENT_TEAM'], 'TESTTEAM')
                    self.assertEqual(obj['buildSettings']['PRODUCT_BUNDLE_IDENTIFIER'], 'example.groove')
            self.assertEqual(scheme.read_text(), original_scheme)


if __name__ == '__main__':
    unittest.main()
