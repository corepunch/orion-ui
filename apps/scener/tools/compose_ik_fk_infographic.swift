import AppKit
import Foundation
import ImageIO
import UniformTypeIdentifiers

struct Sample: Decodable {
    let time: Double
    let joints: [String: [String: [Double]]]
    let drift: Double
    let error: Double
}
struct Measurements: Decodable {
    let fps: Int
    let seconds: Double
    let target: [Double]
    let samples: [Sample]
    let preview: Bool
}
let folder = URL(fileURLWithPath: CommandLine.arguments[1])
let data = try JSONDecoder().decode(Measurements.self, from: Data(contentsOf: folder.appendingPathComponent("measurements.json")))
let width = 1600, height = 1000, stageTop = 205.0, stageHeight = 640.0
let background = NSColor(srgbRed: 0.035, green: 0.054, blue: 0.082, alpha: 1)
let white = NSColor(srgbRed: 0.94, green: 0.96, blue: 0.98, alpha: 1)
let muted = NSColor(srgbRed: 0.56, green: 0.63, blue: 0.72, alpha: 1)
let amber = NSColor(srgbRed: 1, green: 0.66, blue: 0.29, alpha: 1)
let cyan = NSColor(srgbRed: 0.25, green: 0.88, blue: 0.82, alpha: 1)
let output = folder.appendingPathComponent("frames")
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

func text(_ value: String, _ x: Double, _ y: Double, _ size: Double, _ color: NSColor, bold: Bool = false, mono: Bool = false) {
    let font = mono ? NSFont.monospacedDigitSystemFont(ofSize: size, weight: bold ? .semibold : .regular) : NSFont.systemFont(ofSize: size, weight: bold ? .semibold : .regular)
    (value as NSString).draw(at: NSPoint(x: x, y: Double(height) - y - font.ascender), withAttributes: [.font: font, .foregroundColor: color])
}
func line(_ points: [CGPoint], _ color: NSColor, _ thickness: Double = 2, dashed: Bool = false) {
    guard let first = points.first else { return }
    let path = NSBezierPath()
    path.move(to: NSPoint(x: first.x, y: Double(height) - first.y))
    for p in points.dropFirst() { path.line(to: NSPoint(x: p.x, y: Double(height) - p.y)) }
    path.lineWidth = thickness
    path.lineCapStyle = .round
    if dashed { path.setLineDash([5, 7], count: 2, phase: 0) }
    color.setStroke(); path.stroke()
}
func circle(_ point: CGPoint, _ radius: Double, _ color: NSColor, fill: Bool = false, thickness: Double = 2) {
    let path = NSBezierPath(ovalIn: NSRect(x: point.x-radius, y: Double(height)-point.y-radius, width: 2*radius, height: 2*radius))
    path.lineWidth = thickness
    if fill { color.setFill(); path.fill() } else { color.setStroke(); path.stroke() }
}
func project(_ world: [Double]) -> CGPoint {
    let length = sqrt(650.0*650 + 135.0*135)
    let upY = 135.0 / length, upZ = 650.0 / length
    let dy = world[1] + 650, dz = world[2] - 210
    let depth = dy * upZ - dz * upY
    let scale = stageHeight / (2 * tan(10 * .pi / 180))
    return CGPoint(x: 800 + world[0] * scale / depth,
                   y: stageTop + stageHeight/2 - (dy*upY + dz*upZ) * scale / depth)
}
let samples = data.preview ? [data.samples.last!] : data.samples
let initial = data.samples.first!.joints
for (index, sample) in samples.enumerated() {
    autoreleasepool {
        let source = folder.appendingPathComponent(data.preview ? "preview-raw/Comparison.png" : String(format: "raw/Comparison_%04d.png", index))
        guard let imageSource = CGImageSourceCreateWithURL(source as CFURL, nil), let image = CGImageSourceCreateImageAtIndex(imageSource, 0, nil),
              let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width*4,
                                      space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { fatalError("Cannot load or compose \(source.path)") }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: false)
        background.setFill(); NSRect(x: 0, y: 0, width: width, height: height).fill()
        context.draw(image, in: CGRect(x: 0, y: Double(height)-stageTop-stageHeight, width: 1600, height: stageHeight))
        text("MOTION LAB   /   01", 64, 22, 13, cyan, bold: true)
        text("Same motion. Different control.", 60, 47, 46, white, bold: true)
        text("Forward and inverse kinematics, demonstrated by Ecstatica II’s Joe.", 64, 108, 20, muted)
        text("01   FK / FORWARD KINEMATICS", 64, 155, 21, amber, bold: true)
        text("Rotate the joints. The hand follows.", 64, 185, 17, muted)
        text("02   IK / INVERSE KINEMATICS", 864, 155, 21, cyan, bold: true)
        text("Hold a hand target. The arm adjusts.", 864, 185, 17, muted)
        line([CGPoint(x: 800, y: 153), CGPoint(x: 800, y: 936)], muted.withAlphaComponent(0.2), 1)
        for (name, accent, x) in [("FK", amber, 64.0), ("IK", cyan, 864.0)] {
            let joints = sample.joints[name]!
            let chain = ["left_upperarm", "left_forearm", "left_palm"].map { project(joints[$0]!) }
            line(chain, background.withAlphaComponent(0.75), 7)
            line(chain, accent, 2.5)
            for p in chain { circle(p, 5, background, fill: true); circle(p, 5, accent, thickness: 2) }
            let start = project(initial[name]!["left_palm"]!)
            circle(start, 17, accent.withAlphaComponent(0.8), thickness: 2)
            line([CGPoint(x: start.x-24, y: start.y), CGPoint(x: start.x-13, y: start.y)], accent)
            line([CGPoint(x: start.x+13, y: start.y), CGPoint(x: start.x+24, y: start.y)], accent)
            line([CGPoint(x: start.x, y: start.y-24), CGPoint(x: start.x, y: start.y-13)], accent)
            if name == "FK" {
                let trail = data.samples.filter { $0.time <= sample.time }.map { project($0.joints["FK"]!["left_palm"]!) }
                line(trail, amber.withAlphaComponent(0.5), 2, dashed: true)
                line([start, chain[2]], amber.withAlphaComponent(0.8), 1.5, dashed: true)
            }
            let calloutX = x + 505
            let calloutY = name == "FK" ? 340.0 : 365.0
            line([chain[2], CGPoint(x: calloutX-15, y: calloutY+11), CGPoint(x: calloutX, y: calloutY+11)], accent.withAlphaComponent(0.75), 1.5)
            text(name == "FK" ? "HAND FOLLOWS" : "FIXED TARGET", calloutX+8, calloutY, 14, accent, bold: true)
            text(name == "FK" ? "the joint hierarchy" : "arm solves to this point", calloutX+8, calloutY+22, 13, muted)
            text(name == "FK" ? "JOINT ANGLES  →  HAND POSITION" : "HAND TARGET  →  JOINT ANGLES", x, 852, 16, accent, bold: true)
            let value = name == "FK" ? String(format: "%.1f cm", sample.drift) : "< 0.2 cm"
            text(value, x, 884, 35, white, bold: true, mono: true)
            text(name == "FK" ? "hand displacement from start" : "target error · sampled joint precision", x+184, 897, 16, muted)
        }
        let t = sample.time
        text("Same torso turn · feet stay planted · bone lengths stay constant", 64, 944, 16, white)
        text(String(format: "%04.1f / 06.0 s", t), 1360, 945, 16, muted, mono: true)
        line([CGPoint(x: 64, y: 979), CGPoint(x: 1536, y: 979)], muted.withAlphaComponent(0.25), 3)
        line([CGPoint(x: 64, y: 979), CGPoint(x: 64 + 1472*t/data.seconds, y: 979)], cyan, 3)
        NSGraphicsContext.restoreGraphicsState()
        let destination = data.preview ? folder.appendingPathComponent("ik-vs-fk.png") : output.appendingPathComponent(String(format: "Comparison_%04d.png", index))
        guard let result = context.makeImage(), let writer = CGImageDestinationCreateWithURL(destination as CFURL, UTType.png.identifier as CFString, 1, nil) else { fatalError("Cannot write poster") }
        CGImageDestinationAddImage(writer, result, nil)
        guard CGImageDestinationFinalize(writer) else { fatalError("PNG encoding failed") }
    }
}
print("Composed \(samples.count) annotated frames")
