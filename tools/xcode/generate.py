#!/usr/bin/env python3
"""Generate isolated native Xcode projects for Orion apps (currently Groove)."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def encode(value):
    if isinstance(value, dict):
        return '{' + ''.join(f'{encode(k)} = {encode(v)};\n' for k, v in value.items()) + '}'
    if isinstance(value, list):
        return '(' + ''.join(f'{encode(v)},\n' for v in value) + ')'
    if isinstance(value, int):
        return str(value)
    return json.dumps(str(value))


def unity_sources(root, app):
    app_dir = root / 'apps' / app
    units = {
        'user.c': [p for p in sorted((root / 'orion/user').glob('*.c'))
                   if p.name not in ('dialog.c', 'component_registry.c')],
        'kernel.c': sorted((root / 'orion/kernel').glob('*.c')),
        'audio_input.m': [root / 'orion/kernel/audio_input_apple.m'],
        'commctl.c': [p for p in sorted((root / 'orion/commctl').glob('*.c')) if p.name != 'tray.c'],
        'app.c': sorted(p for p in app_dir.rglob('*.c')
                        if p.name != 'main.c' and not {'components', 'tests', 'reel'} & set(p.relative_to(app_dir).parts))
                 + [app_dir / 'main.c'],
    }
    for platform, backend in (('macos', 'macos'), ('ipad', 'ios')):
        units[platform + '.m'] = (sorted((root / 'platform' / backend).glob('*.m'))
                                  + sorted((root / 'platform/unix').glob('*.c'))
                                  + sorted((root / 'platform/audio').glob('*.c')))
    return units


def read_project(path):
    if not path.is_file():
        return {}
    return json.loads(subprocess.check_output(['plutil', '-convert', 'json', '-o', '-', str(path)]))['objects']


class Project:
    def __init__(self, existing):
        self.objects = {}
        self.existing = existing

    def add(self, key, isa, **fields):
        uid = hashlib.sha256(key.encode()).hexdigest()[:24].upper()
        if uid in self.objects:
            raise ValueError(f'Duplicate project object: {key}')
        self.objects[uid] = dict(isa=isa, **fields)
        return uid

    def configurations(self, key, settings):
        configs = []
        for name in ('Debug', 'Release'):
            values = dict(settings, GCC_OPTIMIZATION_LEVEL='0' if name == 'Debug' else 's',
                          DEBUG_INFORMATION_FORMAT='dwarf' if name == 'Debug' else 'dwarf-with-dsym',
                          ONLY_ACTIVE_ARCH='YES' if name == 'Debug' else 'NO')
            uid = self.add(f'{key}/{name}', 'XCBuildConfiguration', name=name, buildSettings=values)
            for field, value in self.existing.get(uid, {}).get('buildSettings', {}).items():
                if field.startswith(('CODE_SIGN', 'DEVELOPMENT_TEAM', 'PROVISIONING_PROFILE', 'PRODUCT_BUNDLE_IDENTIFIER')):
                    values[field] = value
            configs.append(uid)
        return self.add(f'{key}/configurations', 'XCConfigurationList', buildConfigurations=configs,
                        defaultConfigurationIsVisible=0, defaultConfigurationName='Debug')


def signing_settings(args, output):
    spec = importlib.util.spec_from_file_location('ipad_sign', ROOT / 'tools/ipad/sign.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    profile_path, identity, identifier, team = module.find_signing(args.bundle_id, args.team, args.profile)
    profile = plistlib.loads(subprocess.check_output(['security', 'cms', '-D', '-i', profile_path]))
    entitlements = output / 'Generated/ipad.entitlements'
    entitlements.parent.mkdir(parents=True, exist_ok=True)
    entitlements.write_bytes(plistlib.dumps({
        'application-identifier': identifier,
        'com.apple.developer.team-identifier': team,
        'get-task-allow': True,
    }))
    print(f'iPad signing: team {team}, profile {profile["Name"]}')
    return {
        'CODE_SIGN_STYLE': 'Manual',
        'CODE_SIGN_IDENTITY[sdk=iphoneos*]': identity,
        'DEVELOPMENT_TEAM[sdk=iphoneos*]': team,
        'PROVISIONING_PROFILE_SPECIFIER[sdk=iphoneos*]': profile['UUID'],
        'CODE_SIGN_ENTITLEMENTS[sdk=iphoneos*]': '$(PROJECT_FILE_PATH)/Generated/ipad.entitlements',
    }


def scheme(path, project_name, name, target):
    root = ET.Element('Scheme', LastUpgradeVersion='1600', version='1.3')
    action = ET.SubElement(root, 'BuildAction', parallelizeBuildables='YES', buildImplicitDependencies='YES')
    entries = ET.SubElement(action, 'BuildActionEntries')
    entry = ET.SubElement(entries, 'BuildActionEntry', **{f'buildFor{k}': 'YES' for k in
                         ('Testing', 'Running', 'Profiling', 'Archiving', 'Analyzing')})
    reference = dict(BuildableIdentifier='primary', BlueprintIdentifier=target,
                     BuildableName='groove.app', BlueprintName=name, ReferencedContainer=f'container:{project_name}')
    ET.SubElement(entry, 'BuildableReference', **reference)
    for tag, config in (('LaunchAction', 'Debug'), ('ProfileAction', 'Release')):
        action = ET.SubElement(root, tag, buildConfiguration=config, useCustomWorkingDirectory='NO')
        if tag == 'LaunchAction':
            action.set('selectedDebuggerIdentifier', 'Xcode.DebuggerFoundation.Debugger.LLDB')
            action.set('selectedLauncherIdentifier', 'Xcode.IDEFoundation.Launcher.LLDB')
        runnable = ET.SubElement(action, 'BuildableProductRunnable', runnableDebuggingMode='0')
        ET.SubElement(runnable, 'BuildableReference', **reference)
    ET.SubElement(root, 'AnalyzeAction', buildConfiguration='Debug')
    ET.SubElement(root, 'ArchiveAction', buildConfiguration='Release', revealArchiveInOrganizer='YES')
    ET.indent(root)
    write(path, '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(root, encoding='unicode') + '\n')


def generate(args):
    output = (args.output or ROOT / 'apps' / args.app).resolve()
    output.mkdir(parents=True, exist_ok=True)
    bundle = output / (args.app + '.xcodeproj')
    existing = read_project((args.settings_from or bundle) / 'project.pbxproj')
    project = Project(existing if args.unsigned else {})
    relative_root = os.path.relpath(ROOT, output)
    common = {
        'ORION_ROOT': '$(SRCROOT)/' + relative_root,
        'SDKROOT': 'macosx', 'GCC_C_LANGUAGE_STANDARD': 'c11',
        'CLANG_ENABLE_OBJC_ARC': 'NO', 'ENABLE_USER_SCRIPT_SANDBOXING': 'NO',
        'ALWAYS_SEARCH_USER_PATHS': 'NO',
        'GCC_WARN_INHIBIT_ALL_WARNINGS': 'NO', 'GCC_WARN_ABOUT_RETURN_TYPE': 'YES',
        'HEADER_SEARCH_PATHS': ['$(DERIVED_FILE_DIR)/include', '$(ORION_ROOT)',
                               '$(ORION_ROOT)/apps/groove', '$(ORION_ROOT)/components',
                               '$(SDKROOT)/usr/include/libxml2'],
        'GCC_PREPROCESSOR_DEFINITIONS': ['$(inherited)', 'ORION_ALLOW_HIGHDPI=1',
                                       'GL_SILENCE_DEPRECATION', 'STBTT_STATIC', 'HAVE_WINDOWING',
                                       'SHAREDIR=\"../share/groove\"'],
        'OTHER_CFLAGS': ['$(inherited)', '-fblocks', '-Wall', '-Wextra', '-Wno-unused-parameter',
                         '-Wno-unused-function', '-Wno-missing-field-initializers', '-Wno-deprecated-declarations'],
        'PRODUCT_NAME': args.app, 'PRODUCT_BUNDLE_IDENTIFIER': args.bundle_id,
        'MARKETING_VERSION': '1.0', 'CURRENT_PROJECT_VERSION': '1',
        'ENABLE_HARDENED_RUNTIME': 'NO',
    }
    root_configs = project.configurations('project', common)
    icon_ref = project.add('app-icon', 'PBXFileReference', path='Assets.xcassets',
                           sourceTree='DERIVED_FILE_DIR', lastKnownFileType='folder.assetcatalog')
    units = unity_sources(ROOT, args.app)
    source_refs = {}
    for name in units:
        source_refs[name] = project.add('unity/' + name, 'PBXFileReference', path=name,
                                       sourceTree='DERIVED_FILE_DIR', lastKnownFileType='sourcecode.c.objc' if name.endswith('.m') else 'sourcecode.c.c')
    for path in sorted((ROOT / 'orion/commdlg').glob('*.c')):
        source_refs[str(path.relative_to(ROOT))] = project.add(str(path.relative_to(ROOT)), 'PBXFileReference',
            path=os.path.relpath(path, output), sourceTree='SOURCE_ROOT', lastKnownFileType='sourcecode.c.c')
    browse = []
    for folder in ('apps/groove', 'orion', 'platform'):
        files = []
        for path in sorted((ROOT / folder).rglob('*')):
            if not path.is_file() or path.suffix not in ('.c', '.h', '.m', '.orion'):
                continue
            if {'reel', 'promo', 'ejay-samples', 'mtv-samples', 'tests'} & set(path.relative_to(ROOT / folder).parts):
                continue
            rel = path.relative_to(ROOT).as_posix()
            files.append(project.add('browse/' + rel, 'PBXFileReference', name=rel.removeprefix(folder + '/'),
                                     path=os.path.relpath(path, output), sourceTree='SOURCE_ROOT',
                                     lastKnownFileType={'.c': 'sourcecode.c.c', '.h': 'sourcecode.c.h',
                                                        '.m': 'sourcecode.c.objc', '.orion': 'text.xml'}[path.suffix]))
        browse.append(project.add('group/' + folder, 'PBXGroup', name=folder, children=files, sourceTree='<group>'))
    targets, products = [], []
    signing = signing_settings(args, bundle) if not args.unsigned else {'CODE_SIGN_STYLE': 'Automatic'}
    for platform in ('macos', 'ipad'):
        name = args.app + '-' + platform
        ipad = platform == 'ipad'
        settings = dict(INFOPLIST_FILE='$(PROJECT_FILE_PATH)/Generated/' + platform + '-Info.plist',
                        ASSETCATALOG_COMPILER_APPICON_NAME='AppIcon',
                        OTHER_LDFLAGS=['$(inherited)', '-lxml2', '-lm'])
        if ipad:
            settings.update(signing, SDKROOT='iphoneos', SUPPORTED_PLATFORMS='iphoneos iphonesimulator',
                            IPHONEOS_DEPLOYMENT_TARGET='16.0', TARGETED_DEVICE_FAMILY='2', SUPPORTS_MACCATALYST='NO')
            frameworks = ('UIKit', 'CoreGraphics', 'Foundation', 'OpenGLES', 'QuartzCore', 'Security',
                          'UniformTypeIdentifiers', 'AudioToolbox', 'AVFoundation')
        else:
            settings.update(SUPPORTED_PLATFORMS='macosx', MACOSX_DEPLOYMENT_TARGET='12.0',
                            CODE_SIGN_STYLE='Automatic', CODE_SIGN_IDENTITY='-',
                            ENABLE_APP_SANDBOX='YES', ENABLE_USER_SELECTED_FILES='readwrite',
                            CODE_SIGN_ENTITLEMENTS='$(PROJECT_FILE_PATH)/Generated/macos.entitlements')
            if args.team:
                settings['DEVELOPMENT_TEAM'] = args.team
            write(bundle / 'Generated/macos.entitlements', plistlib.dumps({
                'com.apple.security.app-sandbox': True,
                'com.apple.security.device.audio-input': True,
                'com.apple.security.files.user-selected.read-write': True,
            }).decode())
            frameworks = ('AppKit', 'Cocoa', 'OpenGL', 'IOSurface', 'Security', 'CoreFoundation', 'AudioToolbox', 'AVFoundation')
        for framework in frameworks:
            settings['OTHER_LDFLAGS'] += ['-framework', framework]
        configs = project.configurations(name, settings)
        info = dict(CFBundleExecutable='$(EXECUTABLE_NAME)', CFBundleIdentifier='$(PRODUCT_BUNDLE_IDENTIFIER)',
                    CFBundleName='Groove', CFBundleDisplayName='Groove', CFBundlePackageType='APPL',
                    CFBundleInfoDictionaryVersion='6.0', CFBundleShortVersionString='$(MARKETING_VERSION)',
                    CFBundleVersion='$(CURRENT_PROJECT_VERSION)', CFBundleDevelopmentRegion='en',
                    NSMicrophoneUsageDescription='Record your own sounds and vocals as samples in Groove songs.')
        if ipad:
            info.update(LSRequiresIPhoneOS=True, UIFileSharingEnabled=True, LSSupportsOpeningDocumentsInPlace=True,
                        UIRequiredDeviceCapabilities=['opengles-3'],
                        UILaunchScreen={}, UIRequiresFullScreen=False,
                        UISupportedInterfaceOrientations=['UIInterfaceOrientation' + s for s in
                            ('Portrait', 'PortraitUpsideDown', 'LandscapeLeft', 'LandscapeRight')],
                        UIApplicationSceneManifest={'UIApplicationSupportsMultipleScenes': False,
                            'UISceneConfigurations': {'UIWindowSceneSessionRoleApplication': [
                                {'UISceneConfigurationName': 'Orion', 'UISceneDelegateClassName': 'AXSceneDelegate'}]}})
        else:
            info.update(NSHighResolutionCapable=True, NSPrincipalClass='NSApplication',
                        LSApplicationCategoryType='public.app-category.music')
        write(bundle / 'Generated' / (platform + '-Info.plist'), plistlib.dumps(info).decode())
        build_files = []
        for filename, ref in source_refs.items():
            if filename.endswith('.m') and filename not in (platform + '.m', 'audio_input.m'):
                continue
            fields = {'fileRef': ref}
            if filename == 'ipad.m':
                fields['settings'] = {'COMPILER_FLAGS': '-fobjc-arc'}
            build_files.append(project.add(name + '/' + filename, 'PBXBuildFile', **fields))
        forms = project.add(name + '/forms', 'PBXShellScriptBuildPhase', buildActionMask=2147483647,
            name='Prepare Orion sources and forms', shellPath='/bin/sh',
            shellScript='set -eu\n/usr/bin/python3 "$ORION_ROOT/tools/xcode/build.py" forms\n',
            inputPaths=['$(ORION_ROOT)/tools/orionc.c', '$(ORION_ROOT)/apps/groove/groove.orion',
                        '$(ORION_ROOT)/apps/groove/share/icon.png'],
            outputPaths=['$(DERIVED_FILE_DIR)/include/build/generated/apps/groove/groove.h',
                         '$(DERIVED_FILE_DIR)/Assets.xcassets'] +
                        ['$(DERIVED_FILE_DIR)/' + filename for filename in units if not filename.endswith('.m') or filename == platform + '.m'],
            runOnlyForDeploymentPostprocessing=0,
            alwaysOutOfDate=1)
        sources = project.add(name + '/sources', 'PBXSourcesBuildPhase', buildActionMask=2147483647,
                              files=build_files, runOnlyForDeploymentPostprocessing=0)
        icon_file = project.add(name + '/app-icon', 'PBXBuildFile', fileRef=icon_ref)
        icons = project.add(name + '/icons', 'PBXResourcesBuildPhase', buildActionMask=2147483647,
                            files=[icon_file], runOnlyForDeploymentPostprocessing=0)
        resources = project.add(name + '/resources', 'PBXShellScriptBuildPhase', buildActionMask=2147483647,
            name='Bundle Orion resources', shellPath='/bin/sh',
            shellScript='set -eu\n/usr/bin/python3 "$ORION_ROOT/tools/xcode/build.py" resources\n',
            runOnlyForDeploymentPostprocessing=0, alwaysOutOfDate=1)
        product = project.add(name + '/product', 'PBXFileReference', path='groove.app', sourceTree='BUILT_PRODUCTS_DIR',
                              explicitFileType='wrapper.application')
        products.append(product)
        target = project.add(name, 'PBXNativeTarget', name=name, productName=args.app, productReference=product,
            productType='com.apple.product-type.application', buildConfigurationList=configs,
            buildPhases=[forms, sources, icons, resources], buildRules=[], dependencies=[])
        targets.append(target)
        scheme_path = bundle / 'xcshareddata/xcschemes' / (name + '.xcscheme')
        if not scheme_path.exists():
            scheme(scheme_path, bundle.name, name, target)
    products_group = project.add('group/products', 'PBXGroup', name='Products', children=products, sourceTree='<group>')
    main_group = project.add('group/main', 'PBXGroup', children=browse + [products_group], sourceTree='<group>')
    root = project.add('project', 'PBXProject', buildConfigurationList=root_configs, compatibilityVersion='Xcode 14.0',
                       developmentRegion='en', knownRegions=['en', 'Base'], mainGroup=main_group,
                       productRefGroup=products_group, projectDirPath='', projectRoot='', targets=targets)
    if existing.get(root, {}).get('attributes'):
        project.objects[root]['attributes'] = existing[root]['attributes']
    write(bundle / 'project.pbxproj', '// !$*UTF8*$!\n' + encode(dict(
        archiveVersion=1, classes={}, objectVersion=56, objects=project.objects, rootObject=root)) + '\n')
    print(bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', choices=['groove'])
    parser.add_argument('--output', type=Path, help='Project parent directory (default: apps/<app>)')
    parser.add_argument('--settings-from', type=Path, help='Preserve signing settings from an existing .xcodeproj during migration')
    parser.add_argument('--bundle-id', default='com.orion.groove')
    parser.add_argument('--team', default=os.environ.get('TEAM'))
    parser.add_argument('--profile', type=Path, default=os.environ.get('PROFILE'))
    parser.add_argument('--unsigned', action='store_true', help='Leave automatic signing without a team; configure it in Xcode later')
    generate(parser.parse_args())


if __name__ == '__main__':
    main()
