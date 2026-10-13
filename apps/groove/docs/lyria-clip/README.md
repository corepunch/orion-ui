# Generate Groove vocals with Lyria 3 Clip

Groove's audio production tools can call Google's Gemini API directly using
[`generate_lyria.py`](../../../../tools/groove_audio/generate_lyria.py).
The generator uses Python's standard library; no Google SDK is required.
This is an offline sample-production workflow, not an in-app network feature.

The [Google music generation guide](https://ai.google.dev/gemini-api/docs/music-generation)
documents model `lyria-3-clip-preview` on `POST /v1beta/interactions`.
Clip returns a 30-second stereo MP3. BPM, isolation, lyrics and phrase duration
are prompt instructions, not guarantees. The API returns generated text and
base64 audio inside `steps` with `type: model_output`.

## Generate one take

Run from the repository root in a shell where `GEMINI_API_KEY` is exported.
If the key is set in `~/.zshrc`, use an interactive zsh session that loads it.
Do not copy the key into the plan, source files or command arguments.

```sh
# Inspect the exact request without credentials, files or API calls.
python3 tools/groove_audio/generate_lyria.py \
  --plan apps/groove/docs/lyria-clip/sample-plan.json \
  --work-dir tools/groove_audio/work/lyria-clip/neon-take-01 --dry-run

# Send one generation request. Use a fresh directory for each new take.
python3 tools/groove_audio/generate_lyria.py \
  --plan apps/groove/docs/lyria-clip/sample-plan.json \
  --work-dir tools/groove_audio/work/lyria-clip/neon-take-01
```

The prepared sample is **Neon in My Heart**: an original female Eurodance hook
at 140 BPM, A minor, with a requested two-bar phrase and five planned attacks.
The full prompt, voice description, lyrics and target timings are saved in
[`sample-plan.json`](sample-plan.json) before generation.

The tool saves `generation.json` before sending, preserves `response.json`,
extracts each MP3 audio block unchanged, and records interaction IDs, SHA-256
hashes and generated text. Everything generated stays under ignored
`tools/groove_audio/work/`. The API key is read from the environment and sent
only in the `x-goog-api-key` header. Error messages redact that key.
Existing take directories are protected. Failed calls are not retried automatically;
a timeout can mean Google accepted the request. Inspect the saved manifest and
response before requesting another paid generation. If a saved interaction is
pending, fetch it with the same arguments plus `--resume INTERACTION_ID`.

## Prepare the library sample

Keep the raw 30-second take as source material. Follow
[Generating and verifying Groove samples](../../../../tools/groove_audio/sample-production.md)
to isolate any backing, select the complete phrase, observe its actual attacks,
align with pitch preserved, and verify the final audio using Groove's decoder.
At 140 BPM the requested two-bar slot is 151,200 mono frames at 44.1 kHz
(3.428571 seconds). A duration or BPM tag alone does not verify rhythm.

Only after those checks, export gapless MP3 with `TBPM=140` into
`apps/groove/share/vocals/` and append its `M(...)` row at the end of `kDefs` in
`library.c`, preserving existing block IDs. Never register the unedited
30-second generation as a verified two-bar block.

## First live attempt — 2026-10-13

The model lookup succeeded using the configured key. The prepared sample's
generation request reached Google but was rejected with **HTTP 429**:

> Rate limit exceeded for model lyria-3-clip (limit: 0 input tokens per minute on Free Tier).

No audio was returned and no library entry was added. Google directed the
account to [its rate-limit page](https://ai.dev/rate-limit) to upgrade the tier.
This is a project quota restriction; waiting and retrying does not resolve a
zero-token allowance. Use a project with Lyria quota and rerun into a fresh take
directory. [`attempt.json`](attempt.json) records the sanitized outcome.

Offline checks:

```sh
python3 -m unittest discover -s tools/groove_audio -p 'test_generate_lyria.py'
```

## Retry with reported Google Cloud trial credits — 2026-10-13

A second generation request using the key configured in `~/.zshrc` returned
HTTP 429 with the same zero-token Free Tier limit. No audio was generated.
See [`retry-attempt.json`](retry-attempt.json).

Google's [Gemini API billing documentation](https://ai.google.dev/gemini-api/docs/billing)
explains that Google Cloud Free Trial welcome credits granted after March 2,
2026 do not cover Gemini API or AI Studio usage. A Google Cloud trial balance
therefore does not establish paid-tier access for this key. Activate Gemini API
billing for the key's project in AI Studio and complete any required prepaid
credit purchase before retrying. No account billing settings were changed.
