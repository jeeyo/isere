// Schedules timers that cannot fire before the response is sent.
//
// Each setTimeout allocates a FreeRTOS timer control block. The callback path
// clears the slot without deleting the timer, and teardown skips slots that
// look free, so repeated requests are expected to grow RSS.
export const handler = async function (event, context, done) {
  for (let i = 0; i < 4; i++) {
    setTimeout(() => {}, 30000)
  }
  return {
    statusCode: 200,
    headers: { 'Content-Type': 'text/plain' },
    body: 'timers scheduled'
  }
}
