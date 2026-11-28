// isere.asset(): t_asset.bin is "isère asset\n" (13 bytes of UTF-8) followed by bytes 0..255.
export default {
  fetch() {
    const a = isere.asset('t_asset.bin')
    const bytes = a.read(13 + 250, 6)
    let outOfRange = 'no error'
    try { a.read(a.byteLength - 2, 3) } catch (e) { outOfRange = e.name }
    return Response.json({
      byteLength: a.byteLength,
      text: a.text(0, 13),
      bytes: Array.from(bytes),
      whole: a.read().length,
      outOfRange,
      missing: isere.asset('nope.bin'),
      canParse: [URL.canParse('https://ha.mr/x'), URL.canParse('not a url')],
    })
  },
}
