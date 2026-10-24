// Starts a fetch that won't finish before the response.
export default {
  fetch(request) {
    const url = new URL(request.url).searchParams.get('url')
    fetch(url).then(() => console.log('fetch finished after the response'), () => {})
    return new Response('fetch started')
  },
}
