#!/usr/bin/env node
// Startup cost of each engine and mode (criterion 10), measured inside isère.
//
//   node tools/startup.mjs --build quickjs=build-linux-quickjs --build jerryscript=build-linux-jerryscript [--md FILE]
//
// For every engine and variant (warm, cold with and without lazy polyfills, and
// QuickJS with precompiled bytecode):
//   - boot to first request served: from main() to the first response, as isère
//     records it, plus what the client saw from spawning the process
//   - the phases of a request: runtime and context creation, polyfill setup,
//     module evaluation, the handler call and teardown. With a warm engine the
//     first four happen once at boot; with a cold engine on every request.
//   - p50 latency of 200 sequential requests to / on a fresh connection each

import { spawn } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { setTimeout as sleep } from 'node:timers/promises';

const builds = [];
let md = null;
const BOOTS = 5;
const REQUESTS = 200;
for (let i = 2; i < process.argv.length; i++) {
  if (process.argv[i] === '--build') {
    const [runtime, dir] = process.argv[++i].split('=');
    builds.push({ runtime, dir: path.resolve(dir) });
  } else if (process.argv[i] === '--md') {
    md = process.argv[++i];
  }
}

const URL_BASE = 'http://127.0.0.1:8080';
const get = (p) => new Promise((resolve, reject) => {
  const req = http.request(URL_BASE + p, { agent: false }, (res) => {
    let body = '';
    res.on('data', (d) => { body += d; });
    res.on('end', () => resolve({ status: res.statusCode, body }));
  });
  req.on('error', reject);
  req.end();
});

const median = (xs) => [...xs].sort((a, b) => a - b)[Math.floor(xs.length / 2)];

async function measure(build, env) {
  const boots = [];
  let phases = null;
  let perRequest = null;
  let p50 = null;
  for (let b = 0; b < BOOTS; b++) {
    const t0 = process.hrtime.bigint();
    const child = spawn('./isere', [], { cwd: build.dir, env: { ...process.env, ISERE_NDJSON: '0', ...env }, stdio: 'ignore' });
    // the first request goes out as soon as the port accepts
    let first;
    for (;;) {
      try { first = await get('/'); break; } catch { await sleep(0.2); }
    }
    const clientMs = Number(process.hrtime.bigint() - t0) / 1e6;
    const m = JSON.parse((await get('/__metrics')).body);
    boots.push({ clientMs, insideMs: m.boot.to_first_request_ms });
    if (b === BOOTS - 1) {
      phases = m.boot.phases_us;
      const lat = [];
      for (let i = 0; i < REQUESTS; i++) {
        const s = process.hrtime.bigint();
        await get('/');
        lat.push(Number(process.hrtime.bigint() - s) / 1000);
      }
      p50 = median(lat);
      perRequest = JSON.parse((await get('/__metrics')).body).routes.find((r) => r.path === '/').mean_phase_us;
    }
    child.kill('SIGINT');
    await new Promise((r) => child.on('exit', r));
  }
  return {
    boot_inside_ms: median(boots.map((x) => x.insideMs)),
    boot_client_ms: median(boots.map((x) => x.clientMs)),
    boot_phases_us: phases,
    request_phases_us: perRequest,
    p50_us: p50,
  };
}

const variants = [
  { name: 'warm', env: {} },
  { name: 'cold, eager polyfills', env: { ISERE_JS_MODE: 'cold', ISERE_JS_LAZY: '0' } },
  { name: 'cold', env: { ISERE_JS_MODE: 'cold' } },
  { name: 'cold + bytecode', env: { ISERE_JS_MODE: 'cold', ISERE_JS_BYTECODE: '1' }, only: 'quickjs' },
  { name: 'warm + bytecode', env: { ISERE_JS_BYTECODE: '1' }, only: 'quickjs' },
];

const rows = [];
for (const build of builds) {
  for (const v of variants) {
    if (v.only && v.only !== build.runtime) continue;
    process.stderr.write(`[startup] ${build.runtime} ${v.name} ... `);
    const r = await measure(build, v.env);
    rows.push({ runtime: build.runtime, variant: v.name, ...r });
    process.stderr.write(`boot ${r.boot_inside_ms.toFixed(2)} ms, p50 ${r.p50_us.toFixed(0)} us\n`);
  }
}

const P = ['runtime', 'context', 'polyfills', 'module', 'call', 'teardown'];
const lines = [
  '| engine | variant | boot to first request (inside / client) | boot phases µs (' + P.slice(0, 4).join(', ') + ') | per-request phases µs (' + P.join(', ') + ') | p50 latency |',
  '|---|---|---|---|---|---:|',
];
for (const r of rows) {
  lines.push(`| ${r.runtime} | ${r.variant} | ${r.boot_inside_ms.toFixed(2)} ms / ${r.boot_client_ms.toFixed(1)} ms | ` +
    `${P.slice(0, 4).map((p) => r.boot_phases_us[p]).join(' / ')} | ${P.map((p) => r.request_phases_us[p]).join(' / ')} | ${r.p50_us.toFixed(0)} µs |`);
}
const out = lines.join('\n') + '\n';
if (md) writeFileSync(md, out);
console.log(out);
