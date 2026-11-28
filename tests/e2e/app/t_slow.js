// Answers after ?ms= milliseconds of waiting.
export default {
  async fetch(request) {
    const ms = Number(new URL(request.url).searchParams.get('ms') || 100)
    await new Promise((resolve) => setTimeout(resolve, ms))
    return new Response('slept ' + ms)
  },
}
