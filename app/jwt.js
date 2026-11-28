// JWT verification against a JWKS cached in memory, using only the Workers-style
// APIs isère provides (fetch, crypto.subtle, TextEncoder, atob).
//
// The cache lives as long as the module instance: with a warm engine (the
// default) that is the life of the process, so the JWKS is fetched once per TTL.

const ALGORITHMS = {
  RS256: { kty: 'RSA', import: { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, verify: { name: 'RSASSA-PKCS1-v1_5' } },
  ES256: { kty: 'EC', import: { name: 'ECDSA', namedCurve: 'P-256' }, verify: { name: 'ECDSA', hash: 'SHA-256' } },
}

export class JwtError extends Error {
  constructor(status, message) {
    super(message)
    this.status = status
  }
}

function base64urlBytes(s) {
  if (!/^[A-Za-z0-9_-]*$/.test(s)) throw new JwtError(401, 'malformed token')
  const b64 = s.replace(/-/g, '+').replace(/_/g, '/')
  let bin
  try {
    bin = atob(b64 + '==='.slice((b64.length + 3) % 4))
  } catch (e) {
    throw new JwtError(401, 'malformed token')
  }
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
  return bytes
}

function base64urlJson(s) {
  try {
    const value = JSON.parse(new TextDecoder().decode(base64urlBytes(s)))
    if (value === null || typeof value !== 'object' || Array.isArray(value)) throw new Error('not an object')
    return value
  } catch (e) {
    throw new JwtError(401, 'malformed token')
  }
}

export function decode(token) {
  const parts = String(token).split('.')
  if (parts.length !== 3 || !parts[0] || !parts[1] || !parts[2]) throw new JwtError(401, 'malformed token')
  return {
    header: base64urlJson(parts[0]),
    payload: base64urlJson(parts[1]),
    signingInput: parts[0] + '.' + parts[1],
    signature: base64urlBytes(parts[2]),
  }
}

// Keys by kid, refreshed every ttlMs. An unknown kid causes one refetch (key
// rotation), at most once per refetchCooldownMs. Concurrent misses share one fetch.
export class JwksCache {
  constructor({ url, ttlMs = 300000, refetchCooldownMs = 10000 }) {
    this.url = url
    this.ttlMs = ttlMs
    this.refetchCooldownMs = refetchCooldownMs
    this.keys = null          // kid -> JWK
    this.imported = new Map() // kid -> CryptoKey
    this.fetchedAt = 0
    this.lastUnknownRefetch = -Infinity
    this.inflight = null
    this.fetches = 0
  }

  fresh(now) {
    return this.keys !== null && now - this.fetchedAt < this.ttlMs
  }

  refresh() {
    if (this.inflight) return this.inflight
    this.fetches++
    this.inflight = fetch(this.url, { headers: { accept: 'application/json' } })
      .then((res) => {
        if (!res.ok) throw new JwtError(503, 'JWKS fetch failed with status ' + res.status)
        return res.json()
      })
      .then((jwks) => {
        const keys = new Map()
        for (const k of Array.isArray(jwks.keys) ? jwks.keys : []) {
          if (k && typeof k.kid === 'string') keys.set(k.kid, k)
        }
        this.keys = keys
        this.imported = new Map()
        this.fetchedAt = Date.now()
      })
      .finally(() => { this.inflight = null })
    return this.inflight
  }

  async key(kid, alg) {
    const now = Date.now()
    if (!this.fresh(now)) {
      try {
        await this.refresh()
      } catch (e) {
        // a stale cache still beats no answer when the JWKS host is down
        if (this.keys === null) throw e instanceof JwtError ? e : new JwtError(503, 'JWKS unavailable: ' + e.message)
      }
    } else if (!this.keys.has(kid) && now - this.lastUnknownRefetch >= this.refetchCooldownMs) {
      this.lastUnknownRefetch = now
      try {
        await this.refresh()
      } catch (e) {
        // keep serving the keys we have
      }
    }

    const jwk = this.keys.get(kid)
    if (!jwk) throw new JwtError(401, 'unknown key')
    const spec = ALGORITHMS[alg]
    if (jwk.kty !== spec.kty || (jwk.alg && jwk.alg !== alg)) throw new JwtError(401, 'key does not match alg')

    let key = this.imported.get(kid)
    if (!key) {
      key = await crypto.subtle.importKey('jwk', jwk, spec.import, false, ['verify'])
      this.imported.set(kid, key)
    }
    return key
  }
}

// Returns the claims of a valid token, or throws a JwtError (401, or 503 when
// no keys can be had at all).
// (declared, then exported: JerryScript can't parse `export async function`)
async function verify(token, cache, { leewaySeconds = 60, now = Date.now() } = {}) {
  const { header, payload, signingInput, signature } = decode(token)
  const spec = ALGORITHMS[header.alg]
  if (!spec) throw new JwtError(401, 'unsupported alg')
  if (typeof header.kid !== 'string') throw new JwtError(401, 'missing kid')

  const key = await cache.key(header.kid, header.alg)
  const ok = await crypto.subtle.verify(spec.verify, key, signature, new TextEncoder().encode(signingInput))
  if (!ok) throw new JwtError(401, 'bad signature')

  const seconds = Math.floor(now / 1000)
  if (typeof payload.exp === 'number' && seconds > payload.exp + leewaySeconds) throw new JwtError(401, 'token expired')
  if (typeof payload.nbf === 'number' && seconds + leewaySeconds < payload.nbf) throw new JwtError(401, 'token not yet valid')
  return payload
}

export { verify }
