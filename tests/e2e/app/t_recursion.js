// Recurses without end: the engine must stop it before the C stack runs out.
function deeper(n) {
  return n === 0 ? 0 : 1 + deeper(n - 1)
}
export default {
  fetch() {
    return new Response(String(deeper(1e7)))
  },
}
