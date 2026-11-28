// JWT verification (criterion 2) against a JWKS cached in memory: from the board's
// own endpoint over loopback (criterion 4), from a plain HTTP host, and over HTTPS.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer, metrics, route, isRemote } from './lib/target.mjs';
import { request } from './lib/http.mjs';
import { sign, newRsaKey, jwksServer, hasOpenssl } from './lib/jwt.mjs';

const JWKS_PATH = '/.well-known/jwks.json';

async function verify(url, token, opts = {}) {
  const headers = token === undefined ? {} : { authorization: `Bearer ${token}` };
  const res = await request(url, '/verify', { headers, timeoutMs: 15000, ...opts });
  return { status: res.status, body: JSON.parse(res.body), headers: res.headers };
}

describe('JWT verification with our own JWKS endpoint (loopback)', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => {
    const result = await server.stop();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
  });

  const fetches = async () => route(await metrics(server.url), JWKS_PATH).count;

  it('serves our public keys at /.well-known/jwks.json', async () => {
    const res = await request(server.url, JWKS_PATH);
    assert.equal(res.status, 200);
    assert.equal(res.headers['content-type'], 'application/json');
    const kids = JSON.parse(res.body).keys.map((k) => k.kid);
    assert.deepEqual(kids.sort(), ['isere-es256-1', 'isere-rs256-1']);
  });

  it('accepts a valid RS256 token and returns its claims, fetching the JWKS once', async () => {
    const before = await fetches();
    const r = await verify(server.url, sign({ claims: { sub: 'alice' } }));
    assert.equal(r.status, 200, JSON.stringify(r.body));
    assert.equal(r.body.claims.sub, 'alice');
    assert.equal(await fetches(), before + 1);
  });

  it('accepts a valid ES256 token', async () => {
    const r = await verify(server.url, sign({ alg: 'ES256', claims: { sub: 'bob' } }));
    assert.equal(r.status, 200, JSON.stringify(r.body));
    assert.equal(r.body.claims.sub, 'bob');
  });

  it('does not fetch the JWKS again for N requests within the TTL', async () => {
    const before = await fetches();
    for (let i = 0; i < 20; i++) {
      assert.equal((await verify(server.url, sign({ alg: i % 2 ? 'ES256' : 'RS256' }))).status, 200);
    }
    assert.equal(await fetches(), before, 'a fresh cache was refetched');
  });

  it('rejects a bad signature', async () => {
    const token = sign();
    const parts = token.split('.');
    const sig = Buffer.from(parts[2], 'base64url');
    sig[10] ^= 0xff;
    const r = await verify(server.url, `${parts[0]}.${parts[1]}.${sig.toString('base64url')}`);
    assert.equal(r.status, 401);
    assert.equal(r.body.error, 'bad signature');
  });

  it('rejects a token signed with another key under our kid', async () => {
    const other = newRsaKey('isere-rs256-1');
    const r = await verify(server.url, sign({ key: other.key }));
    assert.equal(r.status, 401);
  });

  it('rejects an expired token', async () => {
    const now = Math.floor(Date.now() / 1000);
    const r = await verify(server.url, sign({ claims: { iat: now - 7200, exp: now - 3600 } }));
    assert.equal(r.status, 401);
    assert.equal(r.body.error, 'token expired');
  });

  it('rejects a token that is not yet valid', async () => {
    const now = Math.floor(Date.now() / 1000);
    const r = await verify(server.url, sign({ claims: { nbf: now + 3600 } }));
    assert.equal(r.status, 401);
  });

  it('rejects malformed tokens', async () => {
    const good = sign().split('.');
    for (const token of [
      'not-a-jwt',
      'a.b',
      `${good[0]}.${good[1]}`,
      `${good[0]}..${good[2]}`,
      `!!!.${good[1]}.${good[2]}`,
      `${Buffer.from('not json').toString('base64url')}.${good[1]}.${good[2]}`,
      `${good[0]}.${good[1]}.${good[2]}.extra`,
    ]) {
      const r = await verify(server.url, token);
      assert.equal(r.status, 401, token);
    }
  });

  it('rejects a missing or non-Bearer Authorization header', async () => {
    assert.equal((await verify(server.url, undefined)).status, 401);
    const res = await request(server.url, '/verify', { headers: { authorization: 'Basic abc' } });
    assert.equal(res.status, 401);
    assert.match(res.headers['www-authenticate'], /^Bearer/);
  });

  it('rejects alg "none" and algorithms it does not support', async () => {
    for (const alg of ['none', 'HS256', 'RS512']) {
      const token = sign({ header: { alg } });
      assert.equal((await verify(server.url, token)).status, 401, alg);
    }
  });

  it('rejects a key used with the wrong algorithm', async () => {
    // our RSA kid in an ES256 token
    const token = sign({ alg: 'ES256', kid: 'isere-rs256-1' });
    assert.equal((await verify(server.url, token)).status, 401);
  });

  it('refetches the JWKS exactly once for an unknown kid', async () => {
    const before = await fetches();
    const r = await verify(server.url, sign({ kid: 'rotated-away' }));
    assert.equal(r.status, 401);
    assert.equal(r.body.error, 'unknown key');
    assert.equal(await fetches(), before + 1);
  });

  it('accepts a realistic token over 1 KB in its Authorization header', async () => {
    const claims = { roles: Array.from({ length: 40 }, (_, i) => `role-${i}-with-a-long-name`), org: 'x'.repeat(200) };
    const token = sign({ claims });
    assert.ok(token.length + 7 > 1024, `token is only ${token.length} bytes`);
    const r = await verify(server.url, token);
    assert.equal(r.status, 200, JSON.stringify(r.body));
    assert.equal(r.body.claims.roles.length, 40);
  });

  it('verifies concurrently while serving other requests', async () => {
    const all = await Promise.all([
      ...Array.from({ length: 4 }, () => verify(server.url, sign())),
      request(server.url, '/'),
      request(server.url, JWKS_PATH),
    ]);
    for (const r of all.slice(0, 4)) assert.equal(r.status, 200);
    assert.equal(all[4].status, 200);
  });
});

describe('JWT verification with an external JWKS over HTTP', { skip: isRemote && 'needs a JWKS host the board can reach' }, () => {
  let jwks;
  let server;
  before(async () => {
    jwks = await jwksServer();
    server = await startServer({
      env: { ISERE_CONFIG: JSON.stringify({ jwks_url: jwks.url, jwks_ttl_ms: 60000, jwks_refetch_cooldown_ms: 2000 }) },
    });
  });
  after(async () => {
    await server.stop();
    await jwks.close();
  });

  it('fetches once for N requests within the TTL', async () => {
    for (let i = 0; i < 10; i++) assert.equal((await verify(server.url, sign())).status, 200);
    assert.equal(jwks.state.hits, 1);
  });

  it('picks up a rotated key with exactly one refetch', async () => {
    const rotated = newRsaKey('rotated-1');
    jwks.state.keys.push(rotated.jwk);
    const r = await verify(server.url, sign({ kid: 'rotated-1', key: rotated.key }));
    assert.equal(r.status, 200, JSON.stringify(r.body));
    assert.equal(jwks.state.hits, 2);
  });

  it('does not refetch again for unknown kids within the cooldown', async () => {
    for (let i = 0; i < 5; i++) assert.equal((await verify(server.url, sign({ kid: `bogus-${i}` }))).status, 401);
    assert.equal(jwks.state.hits, 2);
  });

  it('still verifies when the JWKS host is unreachable but the cache is fresh', async () => {
    await jwks.close();
    for (let i = 0; i < 5; i++) assert.equal((await verify(server.url, sign())).status, 200);
  });
});

describe('JWKS cache expiry', { skip: isRemote && 'needs a JWKS host the board can reach' }, () => {
  let jwks;
  let server;
  before(async () => {
    jwks = await jwksServer();
    server = await startServer({
      env: { ISERE_CONFIG: JSON.stringify({ jwks_url: jwks.url, jwks_ttl_ms: 300 }) },
    });
  });
  after(async () => {
    await server.stop();
    await jwks.close();
  });

  it('refetches once the TTL has passed', async () => {
    assert.equal((await verify(server.url, sign())).status, 200);
    assert.equal(jwks.state.hits, 1);
    await new Promise((r) => setTimeout(r, 400));
    assert.equal((await verify(server.url, sign())).status, 200);
    assert.equal((await verify(server.url, sign())).status, 200);
    assert.equal(jwks.state.hits, 2);
  });

  it('keeps using stale keys while the JWKS host is down', async () => {
    await jwks.close();
    await new Promise((r) => setTimeout(r, 400));
    // stale keys are still used when the host is down
    assert.equal((await verify(server.url, sign())).status, 200);
  });
});

describe('JWT verification with an external JWKS over HTTPS', {
  skip: (isRemote && 'needs a JWKS host the board can reach') || (!hasOpenssl() && 'needs the openssl CLI'),
}, () => {
  let jwks;
  let server;
  let untrusting;
  before(async () => {
    jwks = await jwksServer({ tls: true });
    server = await startServer({
      env: { ISERE_CONFIG: JSON.stringify({ jwks_url: jwks.url }), ISERE_CA_FILE: jwks.ca },
    });
  });
  after(async () => {
    await server.stop();
    await untrusting?.stop();
    await jwks.close();
  });

  it('verifies with keys fetched over TLS', async () => {
    const r = await verify(server.url, sign({ alg: 'ES256' }));
    assert.equal(r.status, 200, JSON.stringify(r.body));
    assert.equal(jwks.state.hits, 1);
  });

  it('refuses a JWKS host whose certificate it cannot verify', async () => {
    await server.stop();
    // the default CA bundle does not know our throwaway CA
    untrusting = await startServer({ env: { ISERE_CONFIG: JSON.stringify({ jwks_url: jwks.url }) } });
    const r = await verify(untrusting.url, sign());
    assert.equal(r.status, 503);
    assert.match(r.body.error, /certificate/);
    assert.equal(jwks.state.hits, 1, 'a request reached the untrusted host');
  });
});
