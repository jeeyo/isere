// Per-request and device metrics (criterion 5), and that they are accurate
// (criterion 6): calibration endpoints with known costs must be reported within ±10%.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer, metrics, route, isRemote } from './lib/target.mjs';
import { request } from './lib/http.mjs';

const CALIBRATION_RUNS = 25;

export function serverTiming(header) {
  const out = {};
  for (const part of String(header || '').split(',')) {
    const [name, ...params] = part.trim().split(';');
    const entry = {};
    for (const p of params) {
      const [k, v] = p.split('=');
      entry[k.trim()] = v === undefined ? true : v.replace(/^"|"$/g, '');
    }
    out[name.trim()] = entry;
  }
  return {
    wallMs: Number(out.wall?.dur),
    cpuMs: Number(out.cpu?.dur),
    alloc: Number(out.alloc?.desc),
    peak: Number(out.peak?.desc),
    net: Number(out.net?.desc),
    conn: Number(out.conn?.desc),
  };
}

const median = (xs) => {
  const s = [...xs].sort((a, b) => a - b);
  return s[Math.floor(s.length / 2)];
};

// The CPU `path` costs beyond a no-op. On a slow board a request costs 2.4 ms more when a
// garbage collection lands in it (the FRDM-K64F collects every 3 KB of JS heap growth, so
// every second or third request), and the rhythm shifts with anything that changes the
// heap, even another build of the same code; it can lock onto one side of alternating
// requests and skew every paired difference by the whole collection. So the heap is
// collected before each measured request, which leaves no collection inside it: the
// measurements then agree to within 0.1 ms.
async function extraCpu(url, path, n = CALIBRATION_RUNS) {
  const timingOf = async (p) => {
    assert.equal((await request(url, '/__metrics?gc=1')).status, 200);
    const res = await request(url, p, { timeoutMs: 10000 });
    assert.equal(res.status, 200);
    return serverTiming(res.headers['server-timing']);
  };
  const noop = [];
  const measured = [];
  for (let i = 0; i < n; i++) {
    noop.push(await timingOf('/__calibrate/noop'));
    measured.push(await timingOf(path));
  }
  const pick = (runs, k) => median(runs.map((r) => r[k]));
  const noopMs = pick(noop, 'cpuMs');
  return {
    ms: pick(measured, 'cpuMs') - noopMs,
    wallMs: pick(measured, 'wallMs') - pick(noop, 'wallMs'),
    noopMs,
    detail: `medians of ${n}: ${pick(measured, 'cpuMs').toFixed(3)} - no-op ${noopMs.toFixed(3)}`,
  };
}

async function sample(url, path, n = CALIBRATION_RUNS) {
  const runs = [];
  for (let i = 0; i < n; i++) {
    const res = await request(url, path, { timeoutMs: 10000 });
    assert.equal(res.status, 200);
    runs.push(serverTiming(res.headers['server-timing']));
  }
  const pick = (k) => median(runs.map((r) => r[k]));
  return { wallMs: pick('wallMs'), cpuMs: pick('cpuMs'), alloc: pick('alloc'), net: pick('net') };
}

describe('metrics', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => { await server.stop(); });

  it('puts per-request metrics in a Server-Timing header on every response', async () => {
    for (const path of ['/', '/.well-known/jwks.json', '/__version', '/no-such-route']) {
      const res = await request(server.url, path);
      const t = serverTiming(res.headers['server-timing']);
      for (const k of ['wallMs', 'cpuMs', 'alloc', 'peak', 'net', 'conn']) {
        assert.ok(Number.isFinite(t[k]), `${path}: ${k} missing in ${res.headers['server-timing']}`);
      }
      assert.ok(t.wallMs >= 0 && t.cpuMs >= 0, path);
    }
    const t = serverTiming((await request(server.url, '/')).headers['server-timing']);
    assert.ok(t.alloc > 0, 'a JS request allocated nothing');
    assert.ok(t.peak > 0, 'no peak heap');
  });

  it('reports device-wide metrics at /__metrics', async () => {
    const m = await metrics(server.url);
    assert.ok(m.device.cpu_utilisation >= 0 && m.device.cpu_utilisation <= 1.5, `cpu ${m.device.cpu_utilisation}`);
    assert.ok(m.device.heap_free > 0);
    assert.ok(m.device.heap_min_free > 0 && m.device.heap_min_free <= m.device.heap_free);
    assert.ok(m.device.heap_size > 0);
  });

  it('aggregates per endpoint: count, p50/p99 latency, mean CPU and allocation', async () => {
    for (let i = 0; i < 20; i++) await request(server.url, '/');
    const r = route(await metrics(server.url), '/');
    assert.ok(r.count >= 20);
    assert.ok(r.p50_us > 0 && r.p99_us >= r.p50_us, `p50 ${r.p50_us} p99 ${r.p99_us}`);
    assert.ok(r.mean_cpu_us >= 0);
    assert.ok(r.mean_alloc > 0);
    assert.equal(typeof r.mean_net, 'number');
  });

  it('streams NDJSON: a line per request and a device line every second', { skip: isRemote && 'reads the server output' }, async () => {
    await request(server.url, '/?ndjson');
    await new Promise((r) => setTimeout(r, 1100));
    const lines = server.output().split('\n').filter((l) => l.startsWith('{'));
    const parsed = lines.map((l) => JSON.parse(l));
    const req = parsed.find((o) => o.t === 'req' && o.path === '/');
    assert.ok(req, 'no request line');
    for (const k of ['status', 'wall_us', 'cpu_us', 'alloc', 'peak', 'net']) assert.ok(k in req, k);
    const dev = parsed.find((o) => o.t === 'dev');
    assert.ok(dev, 'no device line');
    for (const k of ['cpu', 'heap_free', 'heap_min_free']) assert.ok(k in dev, k);
  });
});

describe('metrics accuracy (calibration)', () => {
  let server;
  let base;
  before(async () => {
    server = await startServer();
    // warm up, then measure the baseline every endpoint carries
    await sample(server.url, '/__calibrate/noop', 10);
    base = await sample(server.url, '/__calibrate/noop');
  });
  after(async () => { await server.stop(); });

  it('reports a 5 ms busy loop as 5 ms of CPU, ±10%', async () => {
    const extra = await extraCpu(server.url, '/__calibrate/cpu');
    assert.ok(Math.abs(extra.ms - 5) <= 0.5, `cpu ${extra.ms.toFixed(3)} ms (${extra.detail})`);
  });

  it('reports 32 KB allocated and freed as 32 KB allocated, ±10%, with no net change', async () => {
    const s = await sample(server.url, '/__calibrate/alloc');
    const alloc = s.alloc - base.alloc;
    assert.ok(Math.abs(alloc - 32768) <= 3277, `alloc ${alloc} bytes (raw ${s.alloc}, baseline ${base.alloc})`);
    // freed again: JerryScript keeps the small ArrayBuffer object itself until its next GC
    assert.ok(Math.abs(s.net - base.net) <= 32768 / 8, `net ${s.net - base.net} bytes`);
  });

  it('reports a 100 ms sleep as 100 ms of wall time, ±10%, and almost no CPU', async () => {
    // the timer and promise around the sleep cost a little JS work: well under 0.5 ms on a
    // fast host, about 0.5 ms on a 120 MHz board. Sleeping itself must cost nothing; a
    // busy-wait would cost the whole 100 ms.
    const extra = await extraCpu(server.url, '/__calibrate/sleep');
    // wall time beyond a no-op's, which on a board is itself about 11 ms of JS work
    assert.ok(Math.abs(extra.wallMs - 100) <= 10, `wall ${extra.wallMs.toFixed(3)} ms more than a no-op`);
    const allowed = Math.max(0.5, extra.noopMs * 0.25);
    assert.ok(extra.ms < allowed, `sleep cost ${extra.ms.toFixed(3)} ms more CPU than a no-op, allowed ${allowed.toFixed(2)} (${extra.detail})`);
  });
});
