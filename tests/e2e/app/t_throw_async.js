export default {
  async fetch() {
    await null
    throw new Error('deliberate async failure')
  },
}
