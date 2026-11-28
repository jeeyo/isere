// An interval that clears itself from inside its own callback.
export default {
  fetch() {
    return new Promise((resolve) => {
      let n = 0
      const id = setInterval(() => {
        if (++n === 3) {
          clearInterval(id)
          resolve(new Response('ticks ' + n))
        }
      }, 2)
    })
  },
}
