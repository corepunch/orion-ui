#!/usr/bin/env python3
"""Stage and notarize the macOS desktop release downloads through Xcode."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import time

from bundle import display_name

APPS = ('gitclient', 'ledger', 'scener')
ZIP_NAMES = {app: f'{app}-macos-arm64.zip' for app in APPS}
CHECKSUMS = 'SHA256SUMS-macos-arm64.txt'
INPUTS = 'macos-notarization-inputs'
PLAN = 'release-build.json'
DEFAULT_TEAM = 'BM2R8F5YHC'


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def command(*args, capture=False, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=capture, **kwargs).stdout


def gh(repo, *args, **kwargs):
    return command('gh', *args, '--repo', repo, **kwargs)


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def read_checksums(path):
    entries = {}
    for line in path.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        name = name.lstrip('*')
        require(name not in entries, f'Duplicate checksum for {name}')
        entries[name] = digest
    return entries


def inventory(directory):
    return {path.name: sha256(path) for path in sorted(directory.iterdir()) if path.is_file()}


def release_marker(plan):
    return f'<!-- orion-build {plan["run"]} {plan["commit"]} -->'


def release_view(repo, version):
    result = subprocess.run(
        ['gh', 'release', 'view', f'v{version}', '--repo', repo,
         '--json', 'isDraft,body,assets,targetCommitish'],
        capture_output=True, text=True)
    if result.returncode == 0:
        return json.loads(result.stdout)
    require('release not found' in result.stderr.lower(), result.stderr.strip())
    return None


def require_draft(release, plan):
    require(release is not None, f'Draft release v{plan["version"]} is missing.')
    require(release['isDraft'], f'v{plan["version"]} is already public.')
    require(release_marker(plan) in release['body'],
            f'The draft does not belong to build run {plan["run"]}.')
    require(release['targetCommitish'] == plan['commit'],
            'The draft points at a different source revision.')


def require_inventory(release, expected):
    actual = {asset['name']: asset['digest'] for asset in release['assets']}
    require(actual == {name: f'sha256:{digest}' for name, digest in expected.items()},
            'The draft upload does not match the verified release inventory.')


def verify_tag(repo, plan):
    # A manually staged draft may not have a tag until it is published.
    tag = f'v{plan["version"]}'
    result = subprocess.run(['gh', 'api', f'repos/{repo}/commits/refs/tags/{tag}', '--jq', '.sha'],
                            capture_output=True, text=True)
    if result.returncode:
        require('No commit found for SHA:' in result.stderr, result.stderr.strip())
    else:
        require(result.stdout.strip() == plan['commit'], f'Tag {tag} points at a different source revision.')


def upload_missing(repo, plan, directory, digests):
    release = release_view(repo, plan['version'])
    require_draft(release, plan)
    existing = {asset['name']: asset['digest'] for asset in release['assets']}
    pending = []
    for name, digest in digests.items():
        if name in existing:
            require(existing[name] == f'sha256:{digest}',
                    f'The draft already has a different {name}.')
        else:
            pending.append(str(directory / name))
    if pending:
        gh(repo, 'release', 'upload', f'v{plan["version"]}', *pending)


def stage(args):
    plan = {'version': args.version, 'commit': os.environ['GITHUB_SHA'],
            'run': os.environ['GITHUB_RUN_ID']}
    args.handoff.mkdir(parents=True, exist_ok=True)
    for name in (*ZIP_NAMES.values(), CHECKSUMS):
        shutil.move(args.release / name, args.handoff / name)
    plan['inputs'] = inventory(args.handoff)
    plan['assets'] = inventory(args.release)
    (args.handoff / PLAN).write_text(json.dumps(plan, indent=2) + '\n')
    version = args.version
    tag = f'v{version}'
    verify_tag(args.repo, plan)
    release = release_view(args.repo, version)
    if not release:
        gh(args.repo, 'release', 'create', tag, '--draft', '--target', plan['commit'],
           '--title', f'Orion {version}', '--generate-notes', '--notes', release_marker(plan))
    upload_missing(args.repo, plan, args.release, plan['assets'])
    release = release_view(args.repo, version)
    require_draft(release, plan)
    optional = {*ZIP_NAMES.values(), CHECKSUMS}
    staged = dict(release, assets=[asset for asset in release['assets'] if asset['name'] not in optional])
    require_inventory(staged, plan['assets'])
    print(f'Draft {tag} staged. Sign with make mac-notarize MAC_RELEASE_RUN={plan["run"]}.')


def load_plan(repo, run_id, handoff):
    require(re.fullmatch(r'[0-9]+', run_id), 'Set MAC_RELEASE_RUN to a release-build run ID.')
    plan = json.loads((handoff / PLAN).read_text())
    require(plan['run'] == run_id, 'The downloaded inputs are from a different run.')
    run = json.loads(gh(repo, 'run', 'view', run_id, '--json', 'workflowName,headSha,conclusion',
                        capture=True))
    require(run == {'workflowName': 'Release Packages', 'headSha': plan['commit'],
                    'conclusion': 'success'}, 'Use a successful Release Packages build run.')
    inputs = {path.name: sha256(path) for path in handoff.iterdir() if path.name != PLAN}
    require(inputs == plan['inputs'], 'The notary inputs differ from the release build.')
    recorded = read_checksums(handoff / CHECKSUMS)
    for name in ZIP_NAMES.values():
        require(recorded[name] == inputs[name], f'Candidate checksum does not match {name}.')
    return plan


def read_bundle(app, slug, version):
    with (app / 'Contents' / 'Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    require(info['CFBundleIdentifier'] == f'com.orion.{slug}' and
            info['CFBundleExecutable'] == slug and
            info['CFBundleShortVersionString'] == version, f'Unexpected bundle identity in {app}')
    architecture = command('lipo', '-archs', str(app / 'Contents' / 'MacOS' / slug), capture=True)
    require(architecture.strip() == 'arm64', f'{app} must contain an arm64 executable.')
    command('codesign', '--verify', '--deep', '--strict', str(app))
    return info


def verify_app(app, slug, version, team):
    read_bundle(app, slug, version)
    result = subprocess.run(['codesign', '-dvvv', str(app)], check=True,
                            capture_output=True, text=True)
    details = result.stderr
    require(re.search(r'^Authority=Developer ID Application:', details, re.MULTILINE) and
            f'TeamIdentifier={team}' in details.splitlines() and
            re.search(r'^CodeDirectory .*\(runtime\)', details, re.MULTILINE) and
            re.search(r'^Timestamp=', details, re.MULTILINE),
            f'{app} needs a timestamped Developer ID Application signature from team {team} with Hardened Runtime.')
    command('xcrun', 'stapler', 'validate', str(app))
    command('syspolicy_check', 'distribution', str(app))
    if slug == 'scener':
        exporters = command(str(app / 'Contents' / 'MacOS' / slug), '--list-exporters',
                            cwd=app.parent, capture=True)
        require({'glb', 'obj'} <= {line.split('\t', 1)[0] for line in exporters.splitlines()},
                f'{app} cannot load its signed GLB and OBJ plugins.')


def verify_zips(directory, version, team):
    digests = {}
    with tempfile.TemporaryDirectory() as work:
        for slug, filename in ZIP_NAMES.items():
            dest = Path(work) / slug
            path = directory / filename
            require(path.is_file(), f'Missing {filename}.')
            command('ditto', '-x', '-k', str(path), str(dest))
            verify_app(dest / f'{display_name(slug)}.app', slug, version, team)
            digests[filename] = sha256(path)
            print(f'Verified {filename} from team {team}.', flush=True)
    return digests


def make_archive(archive, candidate, slug, version):
    name = display_name(slug)
    destination = archive / 'Products' / 'Applications'
    destination.mkdir(parents=True)
    command('ditto', '-x', '-k', str(candidate), str(destination))
    app = destination / f'{name}.app'
    info = read_bundle(app, slug, version)
    # Xcode checks for Hardened Runtime before cloud signing the archive.
    command('codesign', '--force', '--options', 'runtime', '--sign', '-', str(app))
    with (archive / 'Info.plist').open('wb') as stream:
        plistlib.dump({
            'ArchiveVersion': 2, 'CreationDate': datetime.now(timezone.utc).replace(tzinfo=None),
            'Name': name, 'SchemeName': name,
            'ApplicationProperties': {
                'ApplicationPath': f'Applications/{app.name}', 'Architectures': ['arm64'],
                'CFBundleIdentifier': info['CFBundleIdentifier'],
                'CFBundleShortVersionString': version, 'CFBundleVersion': info['CFBundleVersion'],
                'SigningIdentity': '', 'Team': '',
            },
        }, stream)


def notarized_export(archive, destination):
    deadline = time.monotonic() + 3600
    while True:
        result = subprocess.run(['xcodebuild', '-exportNotarizedApp', '-archivePath', str(archive),
                                 '-exportPath', str(destination)], capture_output=True, text=True)
        if result.returncode == 0:
            return
        message = result.stdout + result.stderr
        known_pending = re.search(r"not yet notarized|has(?: not|n't) been notarized|no notarized app|"
                                  r"notarization.*(?:pending|in progress)|"
                                  r"processing and not ready for distribution",
                                  message, re.IGNORECASE)
        require(known_pending and time.monotonic() < deadline, message.strip())
        print(f'Waiting for Apple to notarize {archive.stem}…', flush=True)
        time.sleep(20)


def sign(args):
    require(re.fullmatch(r'[0-9]+', args.run), 'Set MAC_RELEASE_RUN to a release-build run ID.')
    require(os.uname().sysname == 'Darwin', 'Run cloud signing on a Mac with Xcode.')
    output = args.output.resolve() / args.run
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as temp:
        handoff = Path(temp)
        gh(args.repo, 'run', 'download', args.run, '--name', INPUTS, '--dir', temp)
        plan = load_plan(args.repo, args.run, handoff)
        version = plan['version']
        require_draft(release_view(args.repo, version), plan)
        work = output / 'xcode'
        work.mkdir(exist_ok=True)
        state = work / PLAN
        settings = {'inputs': plan['inputs'], 'team': args.team}
        if state.exists():
            require(json.loads(state.read_text()) == settings,
                    f'The build inputs or Apple team changed. Move aside {work} before retrying.')
        else:
            require(not any((output / name).exists() for name in ZIP_NAMES.values()),
                    f'The existing ZIPs in {output} have no signing state.')
            state.write_text(json.dumps(settings))
        if not all((output / name).is_file() for name in ZIP_NAMES.values()):
            cloud_sign(work, handoff, output, version, args.team)
    signed = verify_zips(output, version, args.team)
    upload_missing(args.repo, plan, output, signed)
    print(f'Notarized ZIPs are in {output} and attached to draft v{version}.')
    print(f'Publish with gh workflow run release.yml --repo {args.repo} '
          f'-f version={version} -f publish_run={args.run}')


def cloud_sign(work, handoff, output, version, team):
    options = work / 'ExportOptions.plist'
    with options.open('wb') as stream:
        plistlib.dump({'method': 'developer-id', 'destination': 'upload',
                       'signingStyle': 'automatic', 'teamID': team}, stream)
    for slug, filename in ZIP_NAMES.items():
        archive = work / f'{slug}.xcarchive'
        submitted = work / f'{slug}.submitted'
        if not submitted.exists():
            if archive.exists():
                shutil.rmtree(archive)
            make_archive(archive, handoff / filename, slug, version)
            print(f'Submitting {display_name(slug)} to Apple through Xcode.', flush=True)
            command('xcodebuild', '-exportArchive', '-archivePath', str(archive),
                    '-exportPath', str(work / f'{slug}-submission'), '-exportOptionsPlist',
                    str(options), '-allowProvisioningUpdates')
            submitted.touch()
    signed = handoff / 'signed'
    signed.mkdir()
    for slug, filename in ZIP_NAMES.items():
        destination = handoff / slug
        notarized_export(work / f'{slug}.xcarchive', destination)
        app = destination / f'{display_name(slug)}.app'
        verify_app(app, slug, version, team)
        command('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(app),
                str(signed / filename))
    for filename in ZIP_NAMES.values():
        (signed / filename).replace(output / filename)


def publish(args):
    plan = load_plan(args.repo, args.run, args.handoff)
    require(plan['version'] == args.version, 'The build run prepared a different version.')
    tag = f'v{args.version}'
    require_draft(release_view(args.repo, args.version), plan)
    with tempfile.TemporaryDirectory() as work:
        directory = Path(work)
        patterns = [value for filename in ZIP_NAMES.values() for value in ('--pattern', filename)]
        gh(args.repo, 'release', 'download', tag, '--dir', work, *patterns)
        signed = verify_zips(directory, args.version, args.team)
        original = read_checksums(args.handoff / CHECKSUMS)
        for filename, digest in original.items():
            if filename not in signed:
                require(plan['assets'].get(filename) == digest, f'Unexpected build checksum for {filename}.')
        checksums = directory / CHECKSUMS
        checksums.write_text(''.join(f'{signed.get(filename, digest)}  {filename}\n'
                                     for filename, digest in original.items()))
        checksum_digest = {CHECKSUMS: sha256(checksums)}
        upload_missing(args.repo, plan, directory, checksum_digest)
        release = release_view(args.repo, args.version)
        require_draft(release, plan)
        expected = {**plan['assets'], **signed, **checksum_digest}
        require_inventory(release, expected)
        verify_tag(args.repo, plan)
        gh(args.repo, 'release', 'edit', tag, '--draft=false')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    for name in ('stage', 'sign', 'publish', 'verify'):
        mode = modes.add_parser(name)
        if name != 'verify':
            mode.add_argument('--repo', required=True)
        if name != 'sign':
            mode.add_argument('--version', required=True)
        if name != 'stage':
            mode.add_argument('--team', default=os.environ.get('MAC_NOTARY_TEAM') or DEFAULT_TEAM)
        if name in ('sign', 'publish'):
            mode.add_argument('--run', required=True)
        if name in ('stage', 'publish'):
            mode.add_argument('--handoff', required=True, type=Path)
        if name == 'stage':
            mode.add_argument('--release', required=True, type=Path)
        if name in ('sign', 'verify'):
            mode.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.mode == 'verify':
        verify_zips(args.output, args.version, args.team)
    else:
        {'stage': stage, 'sign': sign, 'publish': publish}[args.mode](args)


if __name__ == '__main__':
    main()
