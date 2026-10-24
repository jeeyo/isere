#!/usr/bin/env node
// isère's benchmark (criterion 7). One script for the Linux build and a board.
//
//   node tools/bench.mjs --build quickjs=build-linux-quickjs --build jerryscript=build-linux-jerryscript
//   node tools/bench.mjs --url http://192.168.7.1:8080          # a board: one engine, its own config
//
// Options:
//   --duration SEC     seconds per cell (default 50: 12 cells, about 10 minutes per engine)
//   --conns "1 2 4"    concurrency levels; the Pico 2 accepts 4 connections
//   --scenarios LIST   comma-separated: hello, jwks-hit, jwks-miss, mix (default all)
//   --overhead SEC     also measure what metrics cost: hello at 4 connections with
//                      ISERE_METRICS=1 and =0, alternated twice (Linux only; default 20, 0 = off)
//   --json FILE        write every figure as JSON
//   --md FILE          write the results as Markdown tables
//
// Every connection is keep-alive. A response that isn't the expected status, or
// a connection that drops, counts as an error; the run fails if there is any.
// Figures: req/s, client-side p50/p99 latency, and from each response's
// Server-Timing header the mean CPU, allocation, peak heap and net heap change.
// The server's own /__metrics gives the lowest free heap and the heap change
// over the whole cell (after a GC on both sides).

import { spawn, execFileSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as sleep } from 'node:timers/promises';

import { sign } from '../tests/e2e/lib/jwt.mjs';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

// ------------------------------------------------------------------ options

const opts = { builds: [], url: null, duration: 50, conns: [1, 2, 4], scenarios: null, overhead: 20, json: null, md: null, port: 8080 };
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  const v = () => process.argv[++i];
  if (a === '--build') { const [runtime, dir] = v().split('='); opts.builds.push({ runtime, dir: path.resolve(dir) }); }
  else if (a === '--url') opts.url = v().replace(/\/$/, '');
  else if (a === '--duration') opts.duration = Number(v());
  else if (a === '--conns') opts.conns = v().split(/[ ,]+/).map(Number);
  else if (a === '--scenarios') opts.scenarios = v().split(',');
  else if (a === '--overhead') opts.overhead = Number(v());
  else if (a === '--json') opts.json = v();
  else if (a === '--md') opts.md = v();
  else if (a === '--port') opts.port = Number(v());
  else { console.error(`unknown option ${a}`); process.exit(2); }
}
if (!opts.url && opts.builds.length === 0) {
  console.error('give --build runtime=dir (Linux) or --url (a running server)');
  process.exit(2);
}

// ---------------------------------------------------------------- scenarios

const token = sign();
const verify = { path: '/verify', headers: { authorization: `Bearer ${token}` }, status: 200 };
const SCENARIOS = {
  // the smallest JS endpoint
  hello: { requests: [{ path: '/', status: 200 }] },
  // JWT verification with the JWKS in cache
  'jwks-hit': { requests: [verify] },
  // every verification refetches the JWKS from this same server (TTL 0)
  'jwks-miss': { requests: [verify], config: { jwks_ttl_ms: 0 } },
  // a mix across endpoints
  mix: {
    requests: [
      { path: '/', status: 200 },
      verify,
      { path: '/.well-known/jwks.json', status: 200 },
      { path: '/__version', status: 200 },
      { path: '/no-such-route', status: 404 },
    ],
  },
};

// ------------------------------------------------------------------ client

function parseTiming(header) {
  const t = {};
  for (const part of String(header || '').split(',')) {
    const [name, ...params] = part.trim().split(';');
    for (const p of params) {
      const [k, val] = p.split('=');
      if (k === 'dur' || k === 'desc') t[name] = Number(String(val).replace(/"/g, ''));
    }
  }
  return t;
}

// one keep-alive connection, one request in flight at a time
function connection(url) {
  const { hostname, port } = new URL(url);
  const socket = net.connect({ host: hostname, port: Number(port) });
  socket.setNoDelay(true);
  let buf = Buffer.alloc(0);
  let pending = null;
  let closed = false;
  socket.on('data', (d) => {
    buf = buf.length ? Buffer.concat([buf, d]) : d;
    if (!pending) return;
    const end = buf.indexOf('\r\n\r\n');
    if (end < 0) return;
    const head = buf.subarray(0, end).toString('latin1');
    const len = Number(/\r\ncontent-length: *(\d+)/i.exec(head)?.[1] ?? 0);
    if (buf.length < end + 4 + len) return;
    buf = buf.subarray(end + 4 + len);
    const p = pending;
    pending = null;
    p.resolve({ status: Number(head.slice(9, 12)), timing: parseTiming(/\r\nserver-timing: *([^\r]*)/i.exec(head)?.[1]) });
  });
  const fail = (err) => {
    closed = true;
    if (pending) { const p = pending; pending = null; p.reject(err); }
  };
  socket.on('error', fail);
  socket.on('close', () => fail(new Error('connection closed')));
  const ready = new Promise((resolve, reject) => { socket.once('connect', resolve); socket.once('error', reject); });
  ready.catch(() => {});
  return {
    get closed() { return closed; },
    async send(req) {
      await ready;
      return new Promise((resolve, reject) => {
        pending = { resolve, reject };
        let head = `GET ${req.path} HTTP/1.1\r\nHost: bench\r\n`;
        for (const [k, v] of Object.entries(req.headers || {})) head += `${k}: ${v}\r\n`;
        socket.write(head + '\r\n');
      });
    },
    close: () => socket.destroy(),
  };
}

function getJson(url, p) {
  const { hostname, port } = new URL(url);
  return new Promise((resolve, reject) => {
    const s = net.connect({ host: hostname, port: Number(port) }, () => s.write(`GET ${p} HTTP/1.1\r\nHost: bench\r\nConnection: close\r\n\r\n`));
    let data = '';
    s.on('data', (d) => { data += d; });
    s.on('end', () => {
      try { resolve(JSON.parse(data.slice(data.indexOf('\r\n\r\n') + 4))); } catch (e) { reject(e); }
    });
    s.on('error', reject);
  });
}

const percentile = (sorted, q) => sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))] : 0;
const mean = (xs) => xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : 0;

// keeps `conns` connections busy for `seconds`, cycling through the scenario's requests
async function load(url, scenario, conns, seconds) {
  const latencies = [];
  const timing = { cpu: [], alloc: [], peak: [], net: [] };
  let errors = 0;
  const errorKinds = new Map();
  const deadline = Date.now() + seconds * 1000;
  const started = process.hrtime.bigint();

  await Promise.all(Array.from({ length: conns }, async (_, w) => {
    let c = connection(url);
    let i = w;
    while (Date.now() < deadline) {
      const req = scenario.requests[i++ % scenario.requests.length];
      const t0 = process.hrtime.bigint();
      try {
        const res = await c.send(req);
        latencies.push(Number(process.hrtime.bigint() - t0) / 1000);
        if (res.status !== req.status) {
          errors++;
          errorKinds.set(`${req.path} ${res.status}`, (errorKinds.get(`${req.path} ${res.status}`) || 0) + 1);
        }
        for (const k of Object.keys(timing)) if (Number.isFinite(res.timing[k])) timing[k].push(res.timing[k]);
      } catch (err) {
        errors++;
        errorKinds.set(err.message, (errorKinds.get(err.message) || 0) + 1);
        c.close();
        c = connection(url);
      }
    }
    c.close();
  }));

  const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
  const sorted = latencies.sort((a, b) => a - b);
  return {
    requests: latencies.length,
    errors,
    error_kinds: Object.fromEntries(errorKinds),
    rps: latencies.length / elapsed,
    p50_us: percentile(sorted, 0.5),
    p99_us: percentile(sorted, 0.99),
    mean_cpu_us: mean(timing.cpu) * 1000,
    mean_alloc: mean(timing.alloc),
    max_peak: timing.peak.reduce((m, x) => (x > m ? x : m), 0),
    mean_net: mean(timing.net),
  };
}

// ------------------------------------------------------------------ servers

async function waitForServer(url, ms) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    try { return await getJson(url, '/__version'); } catch { await sleep(50); }
  }
  throw new Error(`no answer from ${url}`);
}

async function startLinux(build, env) {
  const url = `http://127.0.0.1:${opts.port}`;
  const child = spawn('./isere', [], {
    cwd: build.dir,
    env: { ...process.env, ISERE_NDJSON: '0', ...env },
    stdio: ['ignore', 'ignore', 'pipe'],
  });
  let stderr = '';
  child.stderr.on('data', (d) => { stderr += d; });
  const exited = new Promise((resolve) => child.on('exit', (code, signal) => resolve({ code, signal })));
  await waitForServer(url, 10000);
  return {
    url,
    async stop() {
      child.kill('SIGINT');
      const r = await Promise.race([exited, sleep(5000).then(() => null)]);
      if (r === null) { child.kill('SIGKILL'); return { crashed: true, hung: true, stderr }; }
      return { crashed: r.code !== 0, code: r.code, signal: r.signal, stderr };
    },
  };
}

// ------------------------------------------------------------------ runs

async function cell(target, name, conns) {
  const scenario = SCENARIOS[name];
  const env = scenario.config ? { ISERE_CONFIG: JSON.stringify(scenario.config) } : {};
  const server = target.url ? { url: target.url, stop: async () => ({ crashed: false }) } : await startLinux(target, env);
  if (target.url && scenario.config) {
    return { skipped: 'needs a config change the board cannot take at run time' };
  }
  try {
    await load(server.url, scenario, conns, Math.min(2, opts.duration / 10)); // warm-up: JWKS fetched, caches primed
    const before = await getJson(server.url, '/__metrics?gc=1');
    const r = await load(server.url, scenario, conns, opts.duration);
    const after = await getJson(server.url, '/__metrics?gc=1');
    r.heap_min_free = after.device.heap_min_free;
    r.heap_change_per_request = (after.device.heap_in_use - before.device.heap_in_use) / Math.max(1, r.requests);
    r.engine_heap_change_per_request = (after.js.heap_used - before.js.heap_used) / Math.max(1, r.requests);
    r.engine_restarts = after.js.restarts - before.js.restarts;
    const alive = await getJson(server.url, '/__version').then(() => true, () => false);
    const stopped = await server.stop();
    r.crashed = !alive || stopped.crashed;
    return r;
  } catch (err) {
    const stopped = await server.stop();
    return { error: err.message, crashed: true, stderr: stopped.stderr };
  }
}

async function overhead(build) {
  // alternate on/off twice, so drift on the host affects both equally
  const rps = { on: [], off: [] };
  for (let round = 0; round < 2; round++) {
    for (const [key, value] of [['on', '1'], ['off', '0']]) {
      const server = await startLinux(build, { ISERE_METRICS: value });
      await load(server.url, SCENARIOS.hello, 4, 2);
      rps[key].push((await load(server.url, SCENARIOS.hello, 4, opts.overhead)).rps);
      await server.stop();
    }
  }
  const on = mean(rps.on);
  const off = mean(rps.off);
  return { rps_metrics_on: on, rps_metrics_off: off, cost_percent: (1 - on / off) * 100 };
}

const commit = (() => {
  try {
    const rev = execFileSync('git', ['-C', REPO, 'rev-parse', '--short', 'HEAD']).toString().trim();
    const dirty = execFileSync('git', ['-C', REPO, 'status', '--porcelain', '--untracked-files=no']).toString().trim();
    return rev + (dirty ? '-dirty' : '');
  } catch { return 'unknown'; }
})();

const targets = opts.url ? [{ url: opts.url }] : opts.builds;
const scenarioNames = opts.scenarios || Object.keys(SCENARIOS);
const results = { commit, date: new Date().toISOString(), duration_s: opts.duration, host: process.platform + '/' + process.arch, engines: [] };
let failed = false;

for (const target of targets) {
  const version = target.url ? await getJson(target.url, '/__version') : null;
  const runtime = target.runtime || version.runtime;
  const engine = { runtime, cells: [] };
  results.engines.push(engine);
  for (const name of scenarioNames) {
    for (const conns of opts.conns) {
      process.stderr.write(`[bench] ${runtime} ${name} c=${conns} ... `);
      const r = await cell(target, name, conns);
      engine.cells.push({ scenario: name, conns, ...r });
      if (r.skipped) { process.stderr.write(`skipped (${r.skipped})\n`); continue; }
      if (r.error || r.errors > 0 || r.crashed) failed = true;
      process.stderr.write(r.error ? `ERROR ${r.error}\n`
        : `${r.rps.toFixed(0)} req/s, p50 ${r.p50_us.toFixed(0)} us, p99 ${r.p99_us.toFixed(0)} us, ${r.errors} errors${r.crashed ? ', CRASHED' : ''}\n`);
    }
  }
  if (!target.url && opts.overhead > 0) {
    process.stderr.write(`[bench] ${runtime} metrics overhead ... `);
    engine.overhead = await overhead(target);
    process.stderr.write(`${engine.overhead.cost_percent.toFixed(1)}%\n`);
  }
}

function markdown() {
  const lines = [];
  lines.push(`Commit \`${results.commit}\`, ${results.date}, ${results.duration_s} s per cell, ${results.host}.`, '');
  for (const e of results.engines) {
    lines.push(`### ${e.runtime}`, '');
    lines.push('| scenario | conns | req/s | p50 | p99 | CPU/req | alloc/req | peak heap | net/req (Server-Timing) | heap Δ/req after GC | min free heap | errors |');
    lines.push('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|');
    for (const c of e.cells) {
      if (c.skipped || c.error) { lines.push(`| ${c.scenario} | ${c.conns} | ${c.skipped || c.error} |||||||||| |`); continue; }
      lines.push(`| ${c.scenario} | ${c.conns} | ${c.rps.toFixed(0)} | ${(c.p50_us / 1000).toFixed(3)} ms | ${(c.p99_us / 1000).toFixed(3)} ms | ` +
        `${c.mean_cpu_us.toFixed(1)} µs | ${c.mean_alloc.toFixed(0)} B | ${(c.max_peak / 1024).toFixed(0)} KB | ${c.mean_net.toFixed(1)} B | ` +
        `${c.heap_change_per_request.toFixed(3)} B | ${(c.heap_min_free / 1024).toFixed(0)} KB | ${c.errors}${c.crashed ? ' (crashed)' : ''} |`);
    }
    if (e.overhead) {
      lines.push('', `Metrics cost: ${e.overhead.rps_metrics_on.toFixed(0)} req/s with, ${e.overhead.rps_metrics_off.toFixed(0)} req/s without (${e.overhead.cost_percent.toFixed(1)}%).`);
    }
    lines.push('');
  }
  return lines.join('\n');
}

if (opts.json) writeFileSync(opts.json, JSON.stringify(results, null, 2) + '\n');
if (opts.md) writeFileSync(opts.md, markdown() + '\n');
if (!opts.json && !opts.md) console.log(markdown());
for (const e of results.engines) {
  if (e.overhead && e.overhead.cost_percent >= 5) {
    console.error(`[bench] ${e.runtime}: metrics cost ${e.overhead.cost_percent.toFixed(1)}% of throughput (limit 5%)`);
    failed = true;
  }
}
process.exit(failed ? 1 : 0);
