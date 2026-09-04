// A timer that actually fires before the handler completes.
//
// This matters because the leak is on the *fire* path, not the create path:
// polyfill_timer_callback() clears the slot (tmr->timer = NULL) without
// calling xTimerDelete(), and teardown then skips the slot because it looks
// free. A timer that never fires is deleted correctly at teardown, so a long
// timeout does not expose the bug -- the handler has to wait for it.
export const handler = async function (event, context, done) {
  await new Promise(resolve => setTimeout(resolve, 5))
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'text/plain' },
    body: 'timer fired'
  }
}
