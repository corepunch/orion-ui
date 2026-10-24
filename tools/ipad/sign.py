#!/usr/bin/env python3
"""Sign an iPad app using a matching installed development identity/profile."""
import argparse
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import plistlib
import re
import shutil
import subprocess


def matches(pattern, identifier):
    return pattern == identifier or (pattern.endswith('.*') and identifier.startswith(pattern[:-1]))


def find_signing(bundle_id, team=None, profile_path=None):
    """Select the installed development profile/identity used by ipad-deploy."""
    identities = subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
    valid = set()
    for line in identities.splitlines():
        match = re.search(r'\b([A-F0-9]{40})\s+"', line)
        if match and 'CSSMERR_' not in line:
            valid.add(match[1])
    if profile_path:
        paths = [profile_path]
    else:
        paths = []
        for folder in ('Library/Developer/Xcode/UserData/Provisioning Profiles',
                       'Library/MobileDevice/Provisioning Profiles'):
            paths.extend((Path.home() / folder).glob('*.mobileprovision'))
    candidates = []
    for path in paths:
        decoded = subprocess.run(['security', 'cms', '-D', '-i', str(path)], capture_output=True)
        if decoded.returncode:
            continue
        profile = plistlib.loads(decoded.stdout)
        entitlements = profile.get('Entitlements', {})
        profile_team = entitlements.get('com.apple.developer.team-identifier')
        prefix = profile.get('ApplicationIdentifierPrefix', [''])[0]
        identifier = prefix + '.' + bundle_id
        if (team and team != profile_team) or not profile_team:
            continue
        if 'iOS' not in profile.get('Platform', []):
            continue
        if profile['ExpirationDate'] <= datetime.now(timezone.utc).replace(tzinfo=None) or not entitlements.get('get-task-allow'):
            continue
        if not matches(entitlements.get('application-identifier', ''), identifier):
            continue
        certificates = {hashlib.sha1(cert).hexdigest().upper() for cert in profile.get('DeveloperCertificates', [])}
        available = sorted(valid & certificates)
        if available:
            candidates.append((profile['ExpirationDate'], str(path), available[0], identifier, profile_team))
    if not candidates:
        raise SystemExit('No matching development profile and signing identity for ' + bundle_id +
                         '. Install a valid profile/certificate or pass PROFILE=/path/to/profile.mobileprovision'
                         ' (and TEAM=... if needed). Simulator builds need neither.')
    return max(candidates)[1:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--team')
    parser.add_argument('--profile', type=Path)
    args = parser.parse_args()
    app = args.app.resolve()
    bundle_id = plistlib.loads((app / 'Info.plist').read_bytes())['CFBundleIdentifier']
    profile, identity, identifier, team = find_signing(bundle_id, args.team, args.profile)
    shutil.copy2(profile, app / 'embedded.mobileprovision')
    entitlement_path = app.parent / (app.stem + '.entitlements.plist')
    entitlement_path.write_bytes(plistlib.dumps({
        'application-identifier': identifier,
        'com.apple.developer.team-identifier': team,
        'get-task-allow': True,
    }))
    subprocess.run(['codesign', '--force', '--sign', identity, '--entitlements', str(entitlement_path),
                    '--generate-entitlement-der', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
    print('Signed ' + bundle_id + ' with team ' + team)


if __name__ == '__main__':
    main()
