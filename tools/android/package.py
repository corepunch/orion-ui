#!/usr/bin/env python3
"""Stage an Orion app's assets, AndroidManifest.xml and launcher icon for Gradle.

The assets mirror the iPad bundle (tools/ipad/bundle.py): share/orion holds the
framework resources and themes, share/<app> the app's. android_main unpacks
them to private storage, listed in orion-files.txt and stamped with
orion-version.txt, a digest of their names and contents."""
import argparse
import hashlib
from pathlib import Path
import shutil
from xml.sax.saxutils import quoteattr

ROOT = Path(__file__).resolve().parents[2]
APPS = {'imageeditor': 'Image Editor', 'penciltest': 'Pencil Test', 'groove': 'Jamsheet', 'winamp': 'Winamp'}
MANIFEST = '''<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
  <uses-feature android:glEsVersion="0x00030000" android:required="true" />
  <application android:label={label} android:icon="@mipmap/ic_launcher" android:hasCode="true"
      android:allowBackup="false" android:theme="@android:style/Theme.Material.NoActionBar">
    <activity android:name="org.orion.OrionActivity" android:exported="true" android:launchMode="singleTask"
        android:configChanges="orientation|screenSize|screenLayout|smallestScreenSize|keyboard|keyboardHidden|uiMode|density|navigation"
        android:windowSoftInputMode="adjustNothing|stateHidden">
      <meta-data android:name="android.app.lib_name" android:value="main" />
      <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
      </intent-filter>
    </activity>
  </application>
</manifest>
'''


def ignore(directory, names):
    return [name for name in names if name in ('.DS_Store', '__pycache__') or name.endswith('.orion-cache')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', choices=sorted(APPS), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--display-name')
    args = parser.parse_args()
    output = args.output.resolve()
    source = 'imageeditor' if args.app == 'penciltest' else args.app
    assets = output / 'assets'
    shutil.rmtree(assets, ignore_errors=True)
    shutil.copytree(ROOT / 'share', assets / 'share/orion', ignore=ignore)
    themes = assets / 'share/orion/themes'
    themes.mkdir(parents=True, exist_ok=True)
    for css in (ROOT / 'orion/themes').glob('*.css'):
        shutil.copy2(css, themes / css.name)
    shutil.copytree(ROOT / 'apps' / source / 'share', assets / 'share' / source, ignore=ignore)
    variant = ROOT / 'apps' / source / args.app / 'share'
    if source != args.app and variant.is_dir():
        shutil.copytree(variant, assets / 'share' / source / args.app, ignore=ignore)
    files = sorted(path.relative_to(assets).as_posix() for path in assets.rglob('*') if path.is_file())
    digest = hashlib.sha256()
    for name in files:
        digest.update(name.encode() + b'\0' + (assets / name).read_bytes())
    (assets / 'orion-files.txt').write_text('\n'.join(files) + '\n')
    (assets / 'orion-version.txt').write_text(digest.hexdigest())

    icon = ROOT / 'apps' / args.app / 'share/icon.png'
    if not icon.is_file():
        icon = ROOT / 'packaging/ipad/icons' / (args.app + '.png')
    if not icon.is_file():
        raise SystemExit('Missing app icon: ' + str(icon))
    res = output / 'res'
    shutil.rmtree(res, ignore_errors=True)
    (res / 'mipmap').mkdir(parents=True)
    shutil.copy2(icon, res / 'mipmap/ic_launcher.png')
    label = args.display_name or APPS[args.app]
    (output / 'AndroidManifest.xml').write_text(MANIFEST.format(label=quoteattr(label)))
    print('Staged ' + str(len(files)) + ' files for ' + label)


if __name__ == '__main__':
    main()
