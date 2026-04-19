import AppKit

let out = CommandLine.arguments.dropFirst().first ?? "icon.png"
let size: CGFloat = 1024
let bounds = NSRect(x: 0, y: 0, width: size, height: size)

guard let ctx = CGContext(
    data: nil,
    width: Int(size), height: Int(size),
    bitsPerComponent: 8, bytesPerRow: 0,
    space: CGColorSpaceCreateDeviceRGB(),
    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
) else { exit(1) }

let nsctx = NSGraphicsContext(cgContext: ctx, flipped: false)
NSGraphicsContext.current = nsctx

let radius = size * 0.2237
let bg = NSBezierPath(roundedRect: bounds, xRadius: radius, yRadius: radius)
NSColor(red: 0.10, green: 0.10, blue: 0.12, alpha: 1.0).setFill()
bg.fill()

let font = NSFont.systemFont(ofSize: size * 0.42, weight: .heavy)
let shadow = NSShadow()
shadow.shadowColor = NSColor(white: 0, alpha: 0.5)
shadow.shadowOffset = NSSize(width: 0, height: -6)
shadow.shadowBlurRadius = 12

let text = NSAttributedString(string: "SNG", attributes: [
    .font: font,
    .foregroundColor: NSColor(red: 0.0, green: 1.0, blue: 0.35, alpha: 1.0),
    .shadow: shadow,
    .kern: -size * 0.01
])

let textSize = text.size()
let origin = NSPoint(
    x: (size - textSize.width) / 2,
    y: (size - textSize.height) / 2 - size * 0.01
)
text.draw(at: origin)

guard let image = ctx.makeImage() else { exit(1) }
let rep = NSBitmapImageRep(cgImage: image)
guard let data = rep.representation(using: .png, properties: [:]) else { exit(1) }
try data.write(to: URL(fileURLWithPath: out))
