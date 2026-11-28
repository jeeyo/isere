// Where the tests run: a local Linux build (default) or a remote board.
//
//   ISERE_URL        test an already-running server (e.g. http://192.168.7.1:8080 for the Pico 2)
//   ISERE_BUILD_DIR  Linux build directory holding ./isere (default <repo>/build)
//   ISERE_PORT       port the Linux build listens on (default 8080)

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { setTimeout as sleep } from 'node:timers/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { request } from './http.mjs';

export const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

export const isRemote = Boolean(process.env.ISERE_URL);
export const buildDir = process.env.ISERE_BUILD_DIR || path.join(REPO, 'build');

export const port = Number(process.env.ISERE_PORT || 8080);

// what the test servers run with unless a test says otherwise
const DEFAULT_ENV = {
  // tests that wait for the handler deadline shouldn't wait 5 s each
  ISERE_HANDLER_TIMEOUT_MS: '1000',
};

async function waitForVersion(url, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let lastError;
  while (Date.now() < deadline) {
    try {
      const res = await request(url, '/__version', { timeoutMs: 2000 });
      if (res.status === 200) return JSON.parse(res.body);
    } catch (err) {
      lastError = err;
    }
    await sleep(50);
  }
  throw new Error(`isère did not answer ${url}/__version within ${timeoutMs} ms (${lastError?.message ?? 'no answer'})`);
}

// Starts a server for one test file and returns { url, version, stop, output }.
// stop() resolves to { code, signal, stdout, stderr, hung } once the process is gone.
// `env` adds to the server's environment; tests that need a local server skip on a board.
export async function startServer({ valgrind = null, env = {} } = {}) {
  if (isRemote) {
    const url = process.env.ISERE_URL.replace(/\/$/, '');
    const version = await waitForVersion(url, 10_000);
    return { url, version, stop: async () => ({ remote: true }), output: () => '' };
  }

  const binary = path.join(buildDir, 'isere');
  if (!existsSync(binary)) {
    throw new Error(`no Linux build in ${buildDir}; run tools/linux.sh or set ISERE_BUILD_DIR`);
  }

  const url = `http://127.0.0.1:${port}`;
  try {
    await request(url, '/__version', { timeoutMs: 500 });
    throw new Error(`something is already listening on port ${port}; stop it first`);
  } catch (err) {
    if (err.message.startsWith('something is already')) throw err;
  }

  const [cmd, args] = valgrind ? ['valgrind', [...valgrind, './isere']] : ['./isere', []];
  const child = spawn(cmd, args, {
    cwd: buildDir,
    env: { ...process.env, ...DEFAULT_ENV, ...env },
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  let stdout = '';
  let stderr = '';
  child.stdout.on('data', (d) => { stdout += d; });
  child.stderr.on('data', (d) => { stderr += d; });
  const exited = new Promise((resolve) => child.on('exit', (code, signal) => resolve({ code, signal })));

  let version;
  try {
    version = await Promise.race([
      waitForVersion(url, valgrind ? 60_000 : 10_000),
      exited.then(({ code, signal }) => {
        throw new Error(`isère exited during startup (code ${code}, signal ${signal})\n${stdout}\n${stderr}`);
      }),
    ]);
  } catch (err) {
    child.kill('SIGKILL');
    throw err;
  }

  async function stop() {
    // SIGINT is isère's clean shutdown path; a server that ignores it has hung
    child.kill('SIGINT');
    const timeout = sleep(valgrind ? 60_000 : 5_000).then(() => 'timeout');
    let result = await Promise.race([exited, timeout]);
    let hung = false;
    if (result === 'timeout') {
      hung = true;
      child.kill('SIGKILL');
      result = await exited;
    }
    return { ...result, stdout, stderr, hung };
  }

  return { url, version, stop, output: () => stdout + stderr };
}

// /__metrics as an object
export async function metrics(url) {
  const res = await request(url, '/__metrics', { timeoutMs: 5000 });
  if (res.status !== 200) throw new Error(`/__metrics answered ${res.status}`);
  return JSON.parse(res.body);
}

export function route(m, path) {
  return m.routes.find((r) => r.path === path);
}
