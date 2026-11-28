// Calibration endpoints with known costs (criterion 6). Comparing what isère
// reports for them with what they are known to cost shows whether the per-request
// metrics can be trusted:
//   /__calibrate/noop   the baseline every other figure is compared against
//   /__calibrate/cpu    about 5 ms of CPU (a busy loop)
//   /__calibrate/alloc  32 KB allocated and freed
//   /__calibrate/sleep  100 ms of waiting, which costs no CPU
const CPU_MS = 5
const ALLOC_BYTES = 32 * 1024
const SLEEP_MS = 100

export default {
  async fetch(request) {
    const path = new URL(request.url).pathname
    if (path === '/__calibrate/cpu') {
      isere.spin(CPU_MS)
    } else if (path === '/__calibrate/alloc') {
      isere.alloc(ALLOC_BYTES)
    } else if (path === '/__calibrate/sleep') {
      await new Promise((resolve) => setTimeout(resolve, SLEEP_MS))
    }
    return new Response('ok\n')
  },
}
