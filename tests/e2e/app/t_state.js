// Module state survives between requests only with a warm engine.
let count = 0
export default { fetch: () => Response.json({ count: ++count }) }
