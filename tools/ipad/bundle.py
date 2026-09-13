#!/usr/bin/env python3
"""Package a standalone Orion editor for iPad, using only SDK tools."""
import argparse
import json
from pathlib import Path
import plistlib
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'target', 'binary'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--app', choices=('imageeditor', 'penciltest'), required=True)
    parser.add_argument('--bundle-id', required=True)
    parser.add_argument('--sdk', choices=('iphoneos', 'iphonesimulator'), required=True)
    parser.add_argument('--sdk-version', required=True)
    parser.add_argument('--minimum', required=True)
    args = parser.parse_args()
    root, target = args.root.resolve(), args.target.resolve()
    if target.suffix != '.app' or target == root or target in root.parents:
        raise SystemExit('Target must be a separate .app bundle')
    icon = root / 'packaging/ipad/icons' / (args.app + '.png')
    if not icon.is_file():
        raise SystemExit('Missing app icon: ' + str(icon))
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    (target / 'bin').mkdir()  # bin/../share paths resolve inside the bundle.
    shutil.copytree(root / 'share', target / 'share/orion')
    shutil.copytree(root / 'apps/imageeditor/share', target / 'share/imageeditor')
    shutil.copy2(args.binary, target / args.app)
    catalog = target.parent / 'AppIcon.xcassets'
    appicons = catalog / 'AppIcon.appiconset'
    appicons.mkdir(parents=True, exist_ok=True)
    images = []
    for size, scale in ((20, 1), (20, 2), (29, 1), (29, 2), (40, 1), (40, 2), (76, 1), (76, 2), (83.5, 2), (1024, 1)):
        pixels = int(size * scale)
        filename = f'icon-{pixels}.png'
        output = appicons / filename
        if not output.exists() or output.stat().st_mtime < icon.stat().st_mtime:
            subprocess.run(['sips', '-z', str(pixels), str(pixels), str(icon), '--out', str(output)], check=True, stdout=subprocess.DEVNULL)
        images.append({'idiom': 'ios-marketing' if size == 1024 else 'ipad', 'size': f'{size}x{size}', 'scale': f'{scale}x', 'filename': filename})
    (appicons / 'Contents.json').write_text(json.dumps({'images': images, 'info': {'version': 1, 'author': 'Orion'}}, indent=2))
    partial = target.parent / 'icon-info.plist'
    subprocess.run(['xcrun', '--sdk', args.sdk, 'actool', str(catalog), '--compile', str(target), '--output-partial-info-plist', str(partial), '--app-icon', 'AppIcon', '--target-device', 'ipad', '--minimum-deployment-target', args.minimum, '--platform', args.sdk, '--output-format', 'human-readable-text'], check=True)
    info = plistlib.loads(partial.read_bytes())
    info.update({
        'CFBundleIdentifier': args.bundle_id,
        'CFBundleExecutable': args.app,
        'CFBundleName': args.app,
        'CFBundleDisplayName': 'Pencil Test' if args.app == 'penciltest' else 'Image Editor',
        'CFBundleDevelopmentRegion': 'en',
        'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundlePackageType': 'APPL',
        'CFBundleShortVersionString': '1.0',
        'CFBundleVersion': '1',
        'UIDeviceFamily': [2],
        'MinimumOSVersion': args.minimum,
        'CFBundleSupportedPlatforms': ['iPhoneOS' if args.sdk == 'iphoneos' else 'iPhoneSimulator'],
        'DTPlatformName': args.sdk,
        'DTPlatformVersion': args.sdk_version,
        'DTSDKName': args.sdk + args.sdk_version,
        'LSRequiresIPhoneOS': True,
        'UIRequiredDeviceCapabilities': ['opengles-3'],
        'UIFileSharingEnabled': True,
        'LSSupportsOpeningDocumentsInPlace': True,
        'UILaunchScreen': {},
        'UIRequiresFullScreen': False,
        'UISupportedInterfaceOrientations': ['UIInterfaceOrientationPortrait', 'UIInterfaceOrientationPortraitUpsideDown', 'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'],
        'UIApplicationSceneManifest': {'UIApplicationSupportsMultipleScenes': False, 'UISceneConfigurations': {'UIWindowSceneSessionRoleApplication': [{'UISceneConfigurationName': 'Orion', 'UISceneDelegateClassName': 'AXSceneDelegate'}]}},
    })
    (target / 'Info.plist').write_bytes(plistlib.dumps(info))
    (target / 'PkgInfo').write_bytes(b'APPL????')
    print('Packaged ' + str(target))


if __name__ == '__main__':
    main()
