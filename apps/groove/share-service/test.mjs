import { test } from 'node:test';
import assert from 'node:assert/strict';
import { webcrypto } from 'node:crypto';
import { File } from 'node:buffer';
import worker from './worker.mjs';
globalThis.crypto ||= webcrypto;
globalThis.File ||= File;
class Bucket {
  objects = new Map();
  async put(key, value) { this.objects.set(key, typeof value === 'string' ? new TextEncoder().encode(value) : new Uint8Array(await value.arrayBuffer())); }
  async delete(key) { this.objects.delete(key); }
  async head(key) { const value = this.objects.get(key); return value ? { size: value.length } : null; }
  async get(key, options) {
    let value = this.objects.get(key);
    if (!value) return null;
    if (options?.range) value = value.slice(options.range.offset, options.range.offset + options.range.length);
    return { body: value, json: async () => JSON.parse(new TextDecoder().decode(value)) };
  }
}
function setup() {
  const env = { SONGS: new Bucket(), UPLOAD_RATE_LIMITER: { limit: async () => ({ success: true }) }, ASSETS: { fetch: async req => new Response(new URL(req.url).pathname) } };
  const call = (path, options = {}) => worker.fetch(new Request(`https://groove.example${path}`, options), env);
  return { env, call };
}
function form(project = true) {
  const f = new FormData(); f.set('title', ' My song '); f.set('audio', new Blob([new Uint8Array([73, 68, 51, 1, 2, 3])], { type: 'audio/mpeg' }), 'song.mp3');
  if (project) f.set('project', new Blob(['ORION_GROOVE 6\nproject bytes']), 'song.groove');
  return f;
}
const upload = (call, body = form()) => call('/api/songs', { method: 'POST', headers: { 'CF-Connecting-IP': '192.0.2.1' }, body });
const report = (call, id, reason) => call(`/api/songs/${id}/report`, { method: 'POST', headers: { 'CF-Connecting-IP': '192.0.2.1' }, body: JSON.stringify({ reason }) });
test('upload, play, report, download and delete', async () => {
  const { env, call } = setup();
  assert.deepEqual(await (await call('/api/health')).json(), { uploads: true });
  const response = await upload(call); assert.equal(response.status, 201);
  const { id, url, deleteToken } = await response.json(); assert.equal(url, `https://groove.example/s/${id}`);
  const meta = await (await call(`/api/songs/${id}`)).json(); assert.equal(meta.title, 'My song'); assert.equal(meta.projectUrl, `/api/songs/${id}/project`);
  const range = await call(meta.audioUrl, { headers: { Range: 'bytes=1-3' } }); assert.equal(range.status, 206); assert.equal(range.headers.get('Content-Range'), 'bytes 1-3/6'); assert.deepEqual([...new Uint8Array(await range.arrayBuffer())], [68, 51, 1]);
  assert.equal((await call(meta.audioUrl, { method: 'HEAD' })).headers.get('Content-Length'), '6');
  assert.equal((await call(meta.audioUrl, { headers: { Range: 'bytes=99-' } })).status, 416);
  assert.equal(await (await call(meta.projectUrl)).text(), 'ORION_GROOVE 6\nproject bytes');
  assert.equal((await report(call, id, 'abuse')).status, 201);
  assert.ok([...env.SONGS.objects.keys()].some(k => k.startsWith(`reports/${id}/`)));
  assert.equal((await call(`/api/songs/${id}`, { method: 'DELETE', headers: { Authorization: 'Bearer wrong' } })).status, 403);
  assert.equal((await call(`/api/songs/${id}`, { method: 'DELETE', headers: { Authorization: `Bearer ${deleteToken}` } })).status, 204);
  assert.equal((await call(meta.audioUrl)).status, 404);
  assert.equal(await (await call(`/s/${id}`)).text(), '/');
});
test('validation, rate limiting and optional project', async () => {
  const { env, call } = setup();
  let response = await upload(call, form(false)); const { id } = await response.json();
  assert.equal((await (await call(`/api/songs/${id}`)).json()).projectUrl, null);
  assert.equal((await call(`/api/songs/${id}/project`)).status, 404);
  const bad = form(false); bad.set('audio', new Blob(['bad']), 'bad.mp3'); assert.equal((await upload(call, bad)).status, 400);
  const badProject = form(); badProject.set('project', new Blob(['bad']), 'bad.groove'); assert.equal((await upload(call, badProject)).status, 400);
  assert.equal((await report(call, id, '')).status, 400);
  assert.equal((await call('/api/songs', { method: 'POST', headers: { Origin: 'https://elsewhere.example', 'CF-Connecting-IP': '192.0.2.1' }, body: form() })).status, 403);
  env.UPLOAD_RATE_LIMITER.limit = async () => ({ success: false }); assert.equal((await upload(call)).status, 429);
  assert.equal((await report(call, id, 'abuse')).status, 429);
  env.UPLOAD_RATE_LIMITER = null; assert.deepEqual(await (await call('/api/health')).json(), { uploads: false });
});
