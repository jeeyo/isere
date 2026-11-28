// GET / : answers with the greeting from routes.json's "config", which handlers get as env
export default {
  async fetch(request, env) {
    return new Response(`${env.greeting} from isère\n`, {
      headers: { 'content-type': 'text/plain' },
    })
  },
}
