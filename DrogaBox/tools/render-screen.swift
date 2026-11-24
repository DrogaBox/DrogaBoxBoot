import Foundation
import CoreGraphics
import CoreText
import ImageIO
import UniformTypeIdentifiers
let args = CommandLine.arguments
let text = try! String(contentsOfFile: args[1], encoding: .utf8)
let lines = text.components(separatedBy: "\n")
let font = CTFontCreateWithName("Menlo-Regular" as CFString, 22, nil)
let cw: CGFloat = 13.25, ch: CGFloat = 28, pad: CGFloat = 24
let w = Int(cw * 80 + pad * 2), h = Int(ch * 25 + pad * 2)
let cs = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0, space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.setFillColor(CGColor(red: 0, green: 0, blue: 0, alpha: 1)); ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
let fg = CGColor(red: 0.78, green: 0.78, blue: 0.78, alpha: 1)
for (i, l) in lines.prefix(25).enumerated() {
  for (j, c) in l.enumerated() where c != " " {
    let a = NSAttributedString(string: String(c), attributes: [NSAttributedString.Key(kCTFontAttributeName as String): font, NSAttributedString.Key(kCTForegroundColorAttributeName as String): fg])
    let line = CTLineCreateWithAttributedString(a)
    ctx.textPosition = CGPoint(x: pad + CGFloat(j) * cw, y: CGFloat(h) - pad - CGFloat(i + 1) * ch + 8)
    CTLineDraw(line, ctx)
  }
}
let img = ctx.makeImage()!
let dst = CGImageDestinationCreateWithURL(URL(fileURLWithPath: args[2]) as CFURL, UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dst, img, nil); CGImageDestinationFinalize(dst)
