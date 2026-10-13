"""Generate a raw Groove vocal take with Gemini's Lyria 3 Clip API (stdlib only)."""
import argparse
import base64
import binascii
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request


MODEL = 'lyria-3-clip-preview'
ENDPOINT = 'https://generativelanguage.googleapis.com/v1beta/interactions'


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def load_plan(path):
    plan = json.loads(path.read_text())
    if not isinstance(plan, dict):
        raise ValueError('Plan must be a JSON object')
    if not isinstance(plan.get('slug'), str) or not re.fullmatch(r'[a-z0-9_]+', plan['slug']):
        raise ValueError('Plan slug must contain only lowercase letters, digits and underscores')
    if not isinstance(plan.get('full_prompt'), str) or not plan['full_prompt'].strip():
        raise ValueError('Plan needs a nonempty full_prompt')
    return plan


def extract_audio(response):
    if not isinstance(response, dict):
        raise ValueError('Expected an interaction JSON object')
    audio, text = [], []
    for step in response.get('steps', []):
        if step.get('type') != 'model_output':
            continue
        for block in step.get('content', []):
            if block.get('type') == 'text':
                text.append(block.get('text', ''))
            elif block.get('type') == 'audio':
                mime = block.get('mime_type', '')
                if mime not in ('audio/mpeg', 'audio/mp3'):
                    raise ValueError(f'Expected Clip MP3, received {mime!r}; saved response needs inspection')
                try:
                    data = base64.b64decode(block.get('data', ''), validate=True)
                except (binascii.Error, ValueError) as error:
                    raise ValueError('Invalid base64 audio in saved response') from error
                if not data:
                    raise ValueError('Empty audio in saved response')
                audio.append(data)
    if not audio:
        raise ValueError('No inline audio in saved response; inspect status before generating again')
    return audio, text


def request_interaction(key, payload, interaction_id, timeout):
    url = ENDPOINT
    data = json.dumps(payload).encode() if not interaction_id else None
    if interaction_id:
        if not re.fullmatch(r'[A-Za-z0-9_-]+', interaction_id):
            raise ValueError('Invalid interaction ID')
        url += '/' + interaction_id
    request = urllib.request.Request(url, data=data, headers={
        'Content-Type': 'application/json', 'x-goog-api-key': key,
    })
    try:
        with urllib.request.urlopen(request, timeout=timeout) as connection:
            return json.load(connection)
    except urllib.error.HTTPError as error:
        message = error.read().decode('utf-8', errors='replace').replace(key, '[REDACTED]')
        raise RuntimeError(f'Gemini HTTP {error.code}: {message[:4000]}') from None
    except (urllib.error.URLError, TimeoutError) as error:
        reason = str(error).replace(key, '[REDACTED]')
        raise RuntimeError(f'Gemini connection failed: {reason}. No automatic retry; '
                           'the server may have accepted the request.') from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True,
                        help='New ignored directory for this take; existing takes are protected')
    parser.add_argument('--dry-run', action='store_true', help='Print request without sending or saving it')
    parser.add_argument('--resume', metavar='INTERACTION_ID', help='Fetch an existing interaction; no new generation')
    parser.add_argument('--timeout', type=float, default=300)
    args = parser.parse_args()
    if not 0 < args.timeout <= 600:
        parser.error('--timeout must be positive and at most 600 seconds')
    manifest = None
    key = os.environ.get('GEMINI_API_KEY', '')
    try:
        plan = load_plan(args.plan)
        payload = {'model': MODEL, 'input': plan['full_prompt']}
        if args.dry_run:
            print(json.dumps(payload, indent=2))
            return 0
        if not key:
            raise ValueError('Export GEMINI_API_KEY in your shell before running this tool')
        root = args.work_dir.expanduser().resolve()
        manifest_path = root / 'generation.json'
        if args.resume:
            manifest = json.loads(manifest_path.read_text())
            if manifest.get('request') != payload:
                raise ValueError('Resume plan does not match the saved request')
            saved_id = manifest.get('interaction_id')
            if saved_id and saved_id != args.resume:
                raise ValueError('Resume ID does not match the saved interaction')
            if manifest.get('status') == 'audio_saved':
                raise ValueError('This take already has saved audio')
        else:
            root.mkdir(parents=True, exist_ok=False)
            manifest = {'provider': 'Google Gemini API', 'model': MODEL,
                        'created_at': datetime.now(timezone.utc).isoformat(),
                        'plan': plan, 'request': payload, 'status': 'request_prepared',
                        'limits': 'Raw 30-second generation. Tempo, lyrics and isolation are unverified. '
                                  'Not registered in the Groove library until edited and verified.'}
            write_json(manifest_path, manifest)
        manifest['status'] = 'request_started'
        write_json(manifest_path, manifest)
        response = request_interaction(key, payload, args.resume, args.timeout)
        # Keep the full response local, including audio, for recovery without another paid call.
        write_json(root / 'response.json', response)
        if not isinstance(response, dict):
            raise ValueError('Expected an interaction JSON object')
        manifest.update(status='response_saved', interaction_id=response.get('id'),
                        api_status=response.get('status'))
        write_json(manifest_path, manifest)
        audio, text = extract_audio(response)
        files = []
        for index, data in enumerate(audio, 1):
            suffix = '' if len(audio) == 1 else f'_{index:02d}'
            filename = plan['slug'] + suffix + '_raw.mp3'
            with (root / filename).open('xb') as output:
                output.write(data)
            files.append({'file': filename, 'bytes': len(data),
                          'sha256': hashlib.sha256(data).hexdigest()})
        manifest.update(status='audio_saved', audio=files, generated_text=text)
        write_json(manifest_path, manifest)
        for item in files:
            print(root / item['file'])
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        message = str(error).replace(key, '[REDACTED]') if key else str(error)
        if manifest is not None:
            manifest['status'] = 'failed'
            manifest['error'] = message
            write_json(manifest_path, manifest)
        print(f'Lyria: {message}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
