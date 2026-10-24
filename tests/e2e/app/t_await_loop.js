// Never finishes but yields to the job queue on every turn.
export default {
  async fetch() {
    for (;;) await null
  },
}
