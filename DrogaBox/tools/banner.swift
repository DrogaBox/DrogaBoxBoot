import Foundation
import CoreGraphics
import CoreText
import ImageIO
import UniformTypeIdentifiers

// README banner for DrogaBoxBoot: dark HUD grid, a ring "eye" on the right, wordmark on the left.
let W = 1600, H = 560
let cs = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: W, height: H, bitsPerComponent: 8, bytesPerRow: 0, space: cs,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor { CGColor(red: r, green: g, blue: b, alpha: a) }
let red = rgb(0.98, 0.24, 0.35), blue = rgb(0.36, 0.56, 0.95), white = rgb(0.95, 0.95, 0.97)
let grey = rgb(0.62, 0.64, 0.70), amber = rgb(0.98, 0.70, 0.25)

// Background with a soft red glow behind the eye.
ctx.setFillColor(rgb(0.04, 0.04, 0.05)); ctx.fill(CGRect(x: 0, y: 0, width: W, height: H))
let eye = CGPoint(x: 1240, y: 280)
let glow = CGGradient(colorsSpace: cs, colors: [rgb(0.45, 0.10, 0.16, 0.55), rgb(0.04, 0.04, 0.05, 0)] as CFArray, locations: [0, 1])!
ctx.drawRadialGradient(glow, startCenter: eye, startRadius: 0, endCenter: eye, endRadius: 420, options: [])

// Grid.
ctx.setStrokeColor(rgb(0.36, 0.56, 0.95, 0.22)); ctx.setLineWidth(1)
for x in stride(from: 0, through: W, by: 64) { ctx.move(to: CGPoint(x: x, y: 0)); ctx.addLine(to: CGPoint(x: x, y: H)) }
for y in stride(from: 0, through: H, by: 64) { ctx.move(to: CGPoint(x: 0, y: y)); ctx.addLine(to: CGPoint(x: W, y: y)) }
ctx.strokePath()

// Rings.
func ring(_ r: CGFloat, _ c: CGColor, _ w: CGFloat, from a0: CGFloat = 0, to a1: CGFloat = 2 * .pi) {
  ctx.setStrokeColor(c); ctx.setLineWidth(w)
  ctx.addArc(center: eye, radius: r, startAngle: a0, endAngle: a1, clockwise: false); ctx.strokePath()
}
ring(230, red, 5, from: 0.35, to: 2.8)
ring(215, red, 1.5)
ring(180, red, 2.5)
ring(150, rgb(1, 1, 1, 0.6), 1)
ring(200, amber, 3, from: 3.75, to: 5.4)
// Ticks.
ctx.setStrokeColor(rgb(1, 1, 1, 0.7)); ctx.setLineWidth(2)
for i in 0..<24 {
  let a = CGFloat(i) * .pi / 12
  let r0: CGFloat = i % 6 == 0 ? 238 : 244, r1: CGFloat = 252
  ctx.move(to: CGPoint(x: eye.x + cos(a) * r0, y: eye.y + sin(a) * r0))
  ctx.addLine(to: CGPoint(x: eye.x + cos(a) * r1, y: eye.y + sin(a) * r1))
}
ctx.strokePath()
// Eye.
ctx.setStrokeColor(red); ctx.setLineWidth(6); ctx.setLineJoin(.round)
ctx.move(to: CGPoint(x: eye.x - 125, y: eye.y))
ctx.addLine(to: CGPoint(x: eye.x - 75, y: eye.y + 42)); ctx.addLine(to: CGPoint(x: eye.x - 30, y: eye.y + 60))
ctx.addLine(to: CGPoint(x: eye.x + 30, y: eye.y + 58)); ctx.addLine(to: CGPoint(x: eye.x + 125, y: eye.y))
ctx.addLine(to: CGPoint(x: eye.x + 30, y: eye.y - 58)); ctx.addLine(to: CGPoint(x: eye.x - 30, y: eye.y - 60))
ctx.addLine(to: CGPoint(x: eye.x - 75, y: eye.y - 42)); ctx.closePath(); ctx.strokePath()
ctx.setFillColor(red); ctx.fillEllipse(in: CGRect(x: eye.x - 34, y: eye.y - 34, width: 68, height: 68))
ctx.setStrokeColor(rgb(1, 0.9, 0.9)); ctx.setLineWidth(4); ctx.strokeEllipse(in: CGRect(x: eye.x - 34, y: eye.y - 34, width: 68, height: 68))
ctx.setFillColor(rgb(0.04, 0.04, 0.06)); ctx.fillEllipse(in: CGRect(x: eye.x - 15, y: eye.y - 15, width: 30, height: 30))
// Circuit traces.
ctx.setStrokeColor(red); ctx.setLineWidth(2)
for (i, y) in [470.0, 448.0, 426.0].enumerated() {
  let x0 = 1080.0 + Double(i) * 60
  ctx.move(to: CGPoint(x: x0, y: y)); ctx.addLine(to: CGPoint(x: x0 + 150, y: y)); ctx.addLine(to: CGPoint(x: x0 + 190, y: y - 30))
  ctx.addLine(to: CGPoint(x: x0 + 300, y: y - 30)); ctx.strokePath()
  ctx.strokeEllipse(in: CGRect(x: x0 + 300, y: y - 36, width: 12, height: 12))
}

// Text.
func text(_ s: String, _ font: String, _ size: CGFloat, _ c: CGColor, _ x: CGFloat, _ y: CGFloat, kern: CGFloat = 0) {
  let f = CTFontCreateWithName(font as CFString, size, nil)
  let a = NSAttributedString(string: s, attributes: [
    NSAttributedString.Key(kCTFontAttributeName as String): f,
    NSAttributedString.Key(kCTForegroundColorAttributeName as String): c,
    NSAttributedString.Key(kCTKernAttributeName as String): kern])
  ctx.textPosition = CGPoint(x: x, y: y); CTLineDraw(CTLineCreateWithAttributedString(a), ctx)
}
ctx.setFillColor(red); ctx.fill(CGRect(x: 96, y: 300, width: 10, height: 120))
text("DROGABOX", "Menlo-Bold", 22, red, 128, 396, kern: 6)
text("DrogaBoxBoot", "HelveticaNeue-Bold", 92, white, 124, 306)
text("OpenCore with an options editor right at the boot picker.", "HelveticaNeue", 28, grey, 128, 240)
ctx.setStrokeColor(red); ctx.setLineWidth(3); ctx.move(to: CGPoint(x: 128, y: 214)); ctx.addLine(to: CGPoint(x: 560, y: 214)); ctx.strokePath()
text("F2  boot-args · config.plist · kexts · drivers · quirks · logs", "Menlo-Regular", 19, white, 128, 172)
text("config*.plist chooser · kext order check with one-key fix", "Menlo-Regular", 19, white, 128, 140)
// Pill, sized to its text.
let pillText = "Based on OpenCore 1.0.9 · made by DrogaBox"
let pillFont = CTFontCreateWithName("Menlo-Regular" as CFString, 15, nil)
let pillLine = CTLineCreateWithAttributedString(NSAttributedString(string: pillText, attributes: [NSAttributedString.Key(kCTFontAttributeName as String): pillFont]))
let pillW = CGFloat(CTLineGetTypographicBounds(pillLine, nil, nil, nil)) + 48 + 24
let pill = CGPath(roundedRect: CGRect(x: 128, y: 62, width: pillW, height: 44), cornerWidth: 22, cornerHeight: 22, transform: nil)
ctx.addPath(pill); ctx.setStrokeColor(blue); ctx.setLineWidth(2); ctx.strokePath()
ctx.setFillColor(rgb(0.35, 0.85, 0.62)); ctx.fillEllipse(in: CGRect(x: 146, y: 76, width: 16, height: 16))
text(pillText, "Menlo-Regular", 15, white, 176, 78)

let img = ctx.makeImage()!
let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: CommandLine.arguments[1]) as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dst, img, nil); CGImageDestinationFinalize(dst)
