#!/usr/bin/env python3
"""Data-only client for Orion's versioned business command protocol."""
import argparse
import json
import os
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--token-env', default='ORION_TOKEN', help='environment variable containing the access token')
    commands = parser.add_subparsers(dest='action', required=True)
    commands.add_parser('status')
    request = commands.add_parser('request', help='send the exact saved request; reuse it on uncertain network failure')
    request.add_argument('file', type=Path)
    args = parser.parse_args()
    parsed = urllib.parse.urlsplit(args.url)
    if parsed.scheme not in ('http', 'https') or parsed.username or parsed.password or parsed.query or parsed.fragment:
        parser.error('expected an HTTP(S) server URL without credentials, query or fragment')
    if parsed.scheme != 'https' and parsed.hostname not in ('127.0.0.1', 'localhost', '::1'):
        parser.error('remote connections require HTTPS')
    token = os.environ.get(args.token_env)
    if not token:
        parser.error('access token environment variable is empty')
    data = None
    endpoint = '/v1/status'
    if args.action == 'request':
        data = args.file.read_bytes()
        if len(data) > 1048576:
            parser.error('request exceeds 1 MiB')
        json.loads(data)
        endpoint = '/v1/command'
    req = urllib.request.Request(args.url.rstrip('/') + endpoint, data=data,
                                 headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'})
    try:
        with urllib.request.build_opener(NoRedirect).open(req, timeout=130) as response:
            result = json.loads(response.read(4 * 1024 * 1024 + 1))
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 1 if result.get('ok') is False else 0
    except urllib.error.HTTPError as error:
        print(error.read(4 * 1024 * 1024).decode('utf-8', errors='replace'), file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f'[client] {error}. If the outcome is uncertain, retry the same request file.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
