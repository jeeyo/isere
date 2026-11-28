// Never yields: the engine has to interrupt it.
export default {
  fetch() {
    for (;;) {}
  },
}
