// GET /verify: checks `Authorization: Bearer <jwt>` against the configured JWKS
// (env.jwks_url) and answers 200 with the claims or 401.
import { JwksCache, JwtError, verify } from './jwt.js'

let cache = null

function deny(status, message) {
  return Response.json({ error: message }, {
    status,
    headers: status === 401 ? { 'www-authenticate': 'Bearer error="invalid_token"' } : {},
  })
}

export default {
  async fetch(request, env) {
    if (cache === null) {
      cache = new JwksCache({
        url: env.jwks_url,
        ttlMs: env.jwks_ttl_ms,
        refetchCooldownMs: env.jwks_refetch_cooldown_ms,
      })
    }

    const auth = request.headers.get('authorization') || ''
    const m = /^Bearer[ ]+(\S+)$/i.exec(auth)
    if (!m) return deny(401, 'missing bearer token')

    try {
      const claims = await verify(m[1], cache)
      return Response.json({ claims, jwks_fetches: cache.fetches })
    } catch (e) {
      if (e instanceof JwtError) return deny(e.status, e.message)
      throw e
    }
  },
}
