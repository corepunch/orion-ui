const MAX_AUDIO = 12 * 1024 * 1024;
const MAX_PROJECT = 20 * 1024 * 1024;
const MAX_BODY = 33 * 1024 * 1024;
const ID_RE = /^[A-Za-z0-9_-]{22}$/;
const encoder = new TextEncoder();

function json(value, status = 200, extra = {}) {
  return new Response(JSON.stringify(value), { status, headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', ...extra } });
}
function error(status, message) { return json({ error: message }, status); }
function randomToken(bytes = 16) {
  return btoa(String.fromCharCode(...crypto.getRandomValues(new Uint8Array(bytes)))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
async function digest(value) {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(value))), b => b.toString(16).padStart(2, '0')).join('');
}
async function boundedBody(request, limit) {
  const length = request.headers.get('Content-Length');
  if (length && (!/^\d+$/.test(length) || Number(length) > limit)) throw new RangeError();
  if (!request.body) return new Uint8Array();
  const reader = request.body.getReader(), chunks = [];
  let total = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.length;
    if (total > limit) { await reader.cancel(); throw new RangeError(); }
    chunks.push(value);
  }
  const body = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) { body.set(chunk, offset); offset += chunk.length; }
  return body;
}
function validMp3(bytes) {
  return bytes.length >= 3 && ((bytes[0] === 0x49 && bytes[1] === 0x44 && bytes[2] === 0x33) || (bytes[0] === 0xff && (bytes[1] & 0xe0) === 0xe0));
}
async function metadata(bucket, id) {
  const object = await bucket.get(`songs/${id}/metadata`);
  if (!object) return null;
  try {
    const data = await object.json();
    if (typeof data.title !== 'string' || typeof data.deleteHash !== 'string' || typeof data.project !== 'boolean') return null;
    return data;
  } catch { return null; }
}
async function upload(request, env, url) {
  const denied = await limitWrite(request, env, 'upload');
  if (denied) return denied;
  if (!/^multipart\/form-data\s*;/i.test(request.headers.get('Content-Type') || '')) return error(415, 'Expected multipart form data');
  const length = request.headers.get('Content-Length');
  if (length && (!/^\d+$/.test(length) || Number(length) > MAX_BODY)) return error(413, 'Upload too large');
  if (!request.body) return error(400, 'Invalid multipart form data');
  let received = 0;
  const body = request.body.pipeThrough(new TransformStream({
    transform(chunk, controller) {
      received += chunk.byteLength;
      if (received > MAX_BODY) controller.error(new RangeError('Upload too large'));
      else controller.enqueue(chunk);
    },
  }));
  let form;
  try {
    form = await new Response(body, { headers: { 'Content-Type': request.headers.get('Content-Type') } }).formData();
  } catch { return received > MAX_BODY ? error(413, 'Upload too large') : error(400, 'Invalid multipart form data'); }
  const title = form.get('title'), audio = form.get('audio'), project = form.get('project');
  if (typeof title !== 'string' || !title.trim() || title.trim().length > 200) return error(400, 'Title must contain 1 to 200 characters');
  if (!(audio instanceof File) || audio.size === 0 || audio.size > MAX_AUDIO) return error(audio instanceof File && audio.size > MAX_AUDIO ? 413 : 400, 'Invalid MP3 file');
  if (project !== null && (!(project instanceof File) || project.size === 0 || project.size > MAX_PROJECT || !/\.groove$/i.test(project.name))) return error(project instanceof File && project.size > MAX_PROJECT ? 413 : 400, 'Invalid Jamsheet project');
  if (!validMp3(new Uint8Array(await audio.slice(0, 3).arrayBuffer()))) return error(400, 'Invalid MP3 file');
  if (project && !/^ORION_GROOVE \d+\r?\n/.test(await project.slice(0, 32).text())) return error(400, 'Invalid Jamsheet project');
  const id = randomToken(), token = randomToken(32), prefix = `songs/${id}/`;
  try {
    await env.SONGS.put(prefix + 'audio', audio, { httpMetadata: { contentType: 'audio/mpeg' } });
    if (project) await env.SONGS.put(prefix + 'project', project, { httpMetadata: { contentType: 'application/octet-stream' } });
    await env.SONGS.put(prefix + 'metadata', JSON.stringify({ title: title.trim(), project: !!project, deleteHash: await digest(token) }));
  } catch (e) {
    await Promise.allSettled(['audio', 'project', 'metadata'].map(key => env.SONGS.delete(prefix + key)));
    throw e;
  }
  return json({ id, url: `${url.origin}/s/${id}`, deleteToken: token }, 201);
}
async function limitWrite(request, env, action) {
  if (!env.SONGS || !env.UPLOAD_RATE_LIMITER) return error(503, 'Sharing service unavailable');
  const origin = request.headers.get('Origin');
  if (origin && origin !== new URL(request.url).origin) return error(403, 'Forbidden');
  const ip = request.headers.get('CF-Connecting-IP');
  if (!ip) return error(400, 'Client address unavailable');
  const rate = await env.UPLOAD_RATE_LIMITER.limit({ key: `${action}:${ip}` });
  if (!rate.success) return error(429, 'Too many requests', { 'Retry-After': '60' });
  return null;
}
function parseRange(header, size) {
  const m = /^bytes=(\d*)-(\d*)$/.exec(header || '');
  if (!m || (!m[1] && !m[2])) return null;
  let start, end;
  if (!m[1]) { const suffix = Number(m[2]); if (!Number.isSafeInteger(suffix) || suffix <= 0) return null; start = Math.max(0, size - suffix); end = size - 1; }
  else { start = Number(m[1]); end = m[2] ? Number(m[2]) : size - 1; if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start > end || start >= size) return null; end = Math.min(end, size - 1); }
  return { start, end };
}
async function fileResponse(request, env, id, name) {
  const key = `songs/${id}/${name}`, info = await env.SONGS.head(key);
  if (!info) return error(404, 'Not found');
  const headers = new Headers({ 'Content-Type': name === 'audio' ? 'audio/mpeg' : 'application/octet-stream', 'Accept-Ranges': 'bytes', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
  if (name === 'project') headers.set('Content-Disposition', 'attachment; filename="song.groove"');
  let range = null;
  if (request.headers.has('Range')) {
    range = parseRange(request.headers.get('Range'), info.size);
    if (!range) { headers.set('Content-Range', `bytes */${info.size}`); return new Response(null, { status: 416, headers }); }
    headers.set('Content-Range', `bytes ${range.start}-${range.end}/${info.size}`);
  }
  const length = range ? range.end - range.start + 1 : info.size;
  headers.set('Content-Length', String(length));
  if (request.method === 'HEAD') return new Response(null, { status: range ? 206 : 200, headers });
  const object = await env.SONGS.get(key, range ? { range: { offset: range.start, length } } : undefined);
  if (!object) return error(404, 'Not found');
  return new Response(object.body, { status: range ? 206 : 200, headers });
}
async function handle(request, env) {
  const url = new URL(request.url), path = url.pathname;
  if (path === '/api/health' && request.method === 'GET') return json({ uploads: !!(env.SONGS && env.UPLOAD_RATE_LIMITER) });
  if (path === '/api/songs' && request.method === 'POST') return upload(request, env, url);
  const match = /^\/api\/songs\/([^/]+)(?:\/(audio|project|report))?$/.exec(path);
  if (match) {
    const [, id, part] = match;
    if (!ID_RE.test(id)) return error(404, 'Not found');
    const data = await metadata(env.SONGS, id);
    if (!data) return error(404, 'Not found');
    if (!part && request.method === 'GET') return json({ id, title: data.title, audioUrl: `/api/songs/${id}/audio`, projectUrl: data.project ? `/api/songs/${id}/project` : null });
    if ((part === 'audio' || part === 'project') && (request.method === 'GET' || request.method === 'HEAD')) {
      if (part === 'project' && !data.project) return error(404, 'Not found');
      return fileResponse(request, env, id, part);
    }
    if (!part && request.method === 'DELETE') {
      const auth = /^Bearer ([A-Za-z0-9_-]{43})$/.exec(request.headers.get('Authorization') || '');
      if (!auth || await digest(auth[1]) !== data.deleteHash) return error(403, 'Forbidden');
      await Promise.all(['audio', 'project'].map(key => env.SONGS.delete(`songs/${id}/${key}`)));
      await env.SONGS.delete(`songs/${id}/metadata`);
      return new Response(null, { status: 204, headers: { 'Cache-Control': 'no-store' } });
    }
    if (part === 'report' && request.method === 'POST') {
      const denied = await limitWrite(request, env, 'report');
      if (denied) return denied;
      let report;
      try { report = JSON.parse(new TextDecoder().decode(await boundedBody(request, 4096))); } catch (e) { return error(e instanceof RangeError ? 413 : 400, 'Invalid report'); }
      if (!report || typeof report.reason !== 'string' || !report.reason.trim() || report.reason.trim().length > 1000) return error(400, 'Invalid report reason');
      await env.SONGS.put(`reports/${id}/${randomToken()}`, JSON.stringify({ songId: id, reason: report.reason.trim(), createdAt: new Date().toISOString() }));
      return json({ ok: true }, 201);
    }
    return error(405, 'Method not allowed');
  }
  if (path.startsWith('/api/')) return error(404, 'Not found');
  if ((path === '/' || /^\/s\/[A-Za-z0-9_-]{22}$/.test(path)) && (request.method === 'GET' || request.method === 'HEAD')) {
    const assetUrl = new URL('/', url);
    return env.ASSETS.fetch(new Request(assetUrl, request));
  }
  return env.ASSETS.fetch(request);
}
export default { async fetch(request, env) { try { return await handle(request, env); } catch (e) { console.error('share-service request failed', e); return error(500, 'Internal server error'); } } };
