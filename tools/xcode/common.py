"""Shared unity source layout for native Orion Xcode projects and build phases."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def unity_sources(root, app):
    app_dir = root / 'apps' / app
    app_sources = []
    for directory, dirs, files in os.walk(app_dir):
        dirs[:] = [name for name in dirs
                   if name not in {'components', 'tests', 'reel', 'build', 'Build', 'DerivedData'}
                   and not name.endswith(('.xcodeproj', '.xcworkspace'))]
        app_sources.extend(Path(directory) / name for name in files
                           if name.endswith('.c') and name != 'main.c')
    units = {
        'u_user.c': [p for p in sorted((root / 'orion/user').glob('*.c'))
                     if p.name not in ('dialog.c', 'component_registry.c')],
        'u_kernel.c': sorted((root / 'orion/kernel').glob('*.c')),
        'u_commctl.c': [p for p in sorted((root / 'orion/commctl').glob('*.c')) if p.name != 'tray.c'],
        'u_commdlg.c': sorted((root / 'orion/commdlg').glob('*.c')),
        'u_' + app + '.c': sorted(app_sources) + [app_dir / 'main.c'],
        'u_audio_input.m': [root / 'orion/kernel/audio_input_apple.m'],
        'u_media_export.m': [root / 'orion/kernel/media_export_apple.m'],
    }
    for platform, backend in (('macos', 'macos'), ('ipad', 'ios')):
        units['u_platform_' + platform + '.m'] = (sorted((root / 'platform' / backend).glob('*.m'))
                                                 + sorted((root / 'platform/unix').glob('*.c'))
                                                 + sorted((root / 'platform/audio').glob('*.c')))
    return units
