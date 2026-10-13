"""Offline checks for the Gemini REST contract and credential-safe failure handling."""
import base64
import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

import generate_lyria as lyria


def response(*blocks):
    return {'id': 'test_interaction', 'status': 'completed', 'steps': [
        {'type': 'model_output', 'content': list(blocks)},
    ]}


def audio(data=b'ID3-test-audio'):
    return {'type': 'audio', 'mime_type': 'audio/mpeg',
            'data': base64.b64encode(data).decode()}


class LyriaTests(unittest.TestCase):
    def test_preserves_multiple_audio_outputs_and_text(self):
        payload = response({'type': 'text', 'text': 'Lyrics'}, audio(b'first'), audio(b'second'))
        self.assertEqual(lyria.extract_audio(payload), ([b'first', b'second'], ['Lyrics']))

    def test_missing_or_bad_audio_is_rejected(self):
        for payload in [response(), response(audio(b'')), response({**audio(), 'data': '!!!'}),
                        response({**audio(), 'mime_type': 'audio/wav'}), []]:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                lyria.extract_audio(payload)

    def test_key_is_header_only_and_no_retry_on_quota_error(self):
        key = 'secret-test-key'
        error = urllib.error.HTTPError(lyria.ENDPOINT, 429, 'Quota', {}, io.BytesIO(key.encode()))
        with patch.object(lyria.urllib.request, 'urlopen', side_effect=error) as call:
            with self.assertRaises(RuntimeError) as caught:
                lyria.request_interaction(key, {'model': lyria.MODEL, 'input': 'test'}, None, 30)
        request = call.call_args.args[0]
        self.assertEqual(request.full_url, lyria.ENDPOINT)
        self.assertEqual(request.get_method(), 'POST')
        self.assertEqual(request.get_header('X-goog-api-key'), key)
        self.assertNotIn(key, request.full_url)
        self.assertNotIn(key.encode(), request.data)
        self.assertNotIn(key, str(caught.exception))
        self.assertIn('[REDACTED]', str(caught.exception))
        call.assert_called_once()

    def test_resume_fetches_without_generating(self):
        with patch.object(lyria.urllib.request, 'urlopen') as call:
            call.return_value.__enter__.return_value = io.BytesIO(b'{"id":"existing"}')
            self.assertEqual(lyria.request_interaction('key', {}, 'existing', 30), {'id': 'existing'})
        request = call.call_args.args[0]
        self.assertEqual(request.get_method(), 'GET')
        self.assertEqual(request.full_url, lyria.ENDPOINT + '/existing')
        self.assertIsNone(request.data)

    def run_tool(self, root, *extra):
        plan = root / 'plan.json'
        plan.write_text(json.dumps({'slug': 'test_vocal', 'full_prompt': 'Solo vocal, 140 BPM'}))
        argv = ['generate_lyria.py', '--plan', str(plan), '--work-dir', str(root / 'take'), *extra]
        with patch('sys.argv', argv), contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            return lyria.main()

    def test_saves_raw_audio_and_protects_existing_take(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.dict(os.environ, {'GEMINI_API_KEY': 'test-key'}), \
                patch.object(lyria, 'request_interaction', return_value=response(audio())) as call:
            root = Path(directory)
            self.assertEqual(self.run_tool(root), 0)
            self.assertEqual((root / 'take/test_vocal_raw.mp3').read_bytes(), b'ID3-test-audio')
            manifest = json.loads((root / 'take/generation.json').read_text())
            self.assertEqual(manifest['status'], 'audio_saved')
            self.assertEqual(manifest['interaction_id'], 'test_interaction')
            self.assertEqual(self.run_tool(root), 1)
            call.assert_called_once()

    def test_dry_run_needs_no_key_and_creates_no_take(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {}, clear=True), \
                patch.object(lyria, 'request_interaction') as call:
            root = Path(directory)
            self.assertEqual(self.run_tool(root, '--dry-run'), 0)
            self.assertFalse((root / 'take').exists())
            call.assert_not_called()

    def test_failure_manifest_redacts_key(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.dict(os.environ, {'GEMINI_API_KEY': 'secret-test-key'}), \
                patch.object(lyria, 'request_interaction', side_effect=RuntimeError('secret-test-key')):
            root = Path(directory)
            self.assertEqual(self.run_tool(root), 1)
            saved = (root / 'take/generation.json').read_text()
            self.assertNotIn('secret-test-key', saved)
            self.assertEqual(json.loads(saved)['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
