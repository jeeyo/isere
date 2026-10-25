// Tokens signed with the demo keys in tests/e2e/keys, and JWKS servers that count
// how often they are asked.
import crypto from 'node:crypto';
import http from 'node:http';
import https from 'node:https';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';

import { REPO } from './target.mjs';

const keyDir = path.join(REPO, 'tests/e2e/keys');
export const KEYS = {
  RS256: { kid: 'isere-rs256-1', key: crypto.createPrivateKey(readFileSync(path.join(keyDir, 'rs256.pem'))) },
  ES256: { kid: 'isere-es256-1', key: crypto.createPrivateKey(readFileSync(path.join(keyDir, 'es256.pem'))) },
};

const b64url = (buf) => Buffer.from(buf).toString('base64url');

export function sign({ alg = 'RS256', kid, key, claims = {}, header = {} } = {}) {
  const k = KEYS[alg] || KEYS.RS256;
  const now = Math.floor(Date.now() / 1000);
  const h = { alg, typ: 'JWT', kid: kid === undefined ? k.kid : kid, ...header };
  const p = { sub: 'demo-user', iat: now, exp: now + 3600, ...claims };
  const input = b64url(JSON.stringify(h)) + '.' + b64url(JSON.stringify(p));
  const signature = crypto.sign('sha256', Buffer.from(input), {
    key: key || k.key,
    ...(alg === 'ES256' ? { dsaEncoding: 'ieee-p1363' } : {}),
  });
  return input + '.' + b64url(signature);
}

// a fresh RSA key the server doesn't know yet, for rotation tests
export function newRsaKey(kid) {
  const { privateKey, publicKey } = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
  const jwk = publicKey.export({ format: 'jwk' });
  return { kid, key: privateKey, jwk: { kty: 'RSA', kid, alg: 'RS256', use: 'sig', n: jwk.n, e: jwk.e } };
}

export function ourJwks() {
  return JSON.parse(readFileSync(path.join(REPO, 'app/jwks.json'), 'utf8'));
}

// A JWKS endpoint that counts its requests. With `tls`, it serves HTTPS with a
// throwaway CA (needs the openssl CLI); `ca` is then the CA certificate path.
export async function jwksServer({ keys = ourJwks().keys, tls = false } = {}) {
  const state = { keys: [...keys], hits: 0 };
  const handler = (req, res) => {
    state.hits++;
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ keys: state.keys }));
  };

  let server;
  let ca = null;
  if (tls) {
    const dir = mkdtempSync(path.join(tmpdir(), 'isere-tls-'));
    const run = (args) => execFileSync('openssl', args, { cwd: dir, stdio: 'ignore' });
    run(['req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes', '-days', '2',
      '-subj', '/CN=isere test CA', '-keyout', 'ca.key', '-out', 'ca.pem']);
    run(['req', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-nodes', '-subj', '/CN=localhost',
      '-keyout', 'server.key', '-out', 'server.csr']);
    writeFileSync(path.join(dir, 'ext.cnf'), 'subjectAltName=DNS:localhost,IP:127.0.0.1\n');
    run(['x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key', '-CAcreateserial',
      '-days', '2', '-extfile', 'ext.cnf', '-out', 'server.pem']);
    ca = path.join(dir, 'ca.pem');
    server = https.createServer({
      key: readFileSync(path.join(dir, 'server.key')),
      cert: readFileSync(path.join(dir, 'server.pem')),
    }, handler);
  } else {
    server = http.createServer(handler);
  }

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  return {
    url: `${tls ? 'https://localhost' : 'http://127.0.0.1'}:${port}/jwks`,
    ca,
    state,
    close: () => new Promise((resolve) => { server.closeAllConnections?.(); server.close(resolve); }),
  };
}

export function hasOpenssl() {
  try {
    execFileSync('openssl', ['version'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}
