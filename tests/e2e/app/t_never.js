// Never answers and keeps a timer pending: only the handler deadline ends it.
export default {
  fetch() {
    setTimeout(() => {}, 60000)
    return new Promise(() => {})
  },
}
