// crypto.subtle.digest and getRandomValues, as hex.
const hex = (buf) => Array.from(new Uint8Array(buf), (b) => b.toString(16).padStart(2, '0')).join('')
export default {
  async fetch() {
    const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode('abc'))
    const random = crypto.getRandomValues(new Uint8Array(16))
    return Response.json({ sha256: hex(digest), random: hex(random), utf8: new TextDecoder().decode(new TextEncoder().encode('é€😀')) })
  },
}
