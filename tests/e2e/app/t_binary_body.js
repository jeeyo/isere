// Every byte value, as a Uint8Array.
export default {
  fetch() {
    const bytes = new Uint8Array(256)
    for (let i = 0; i < 256; i++) bytes[i] = i
    return new Response(bytes, { headers: { 'content-type': 'application/octet-stream' } })
  },
}
