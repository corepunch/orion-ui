// Encode native Scener frames as a looping GIF for Markdown reviews.
import Foundation
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
guard args.count == 5, let fps = Double(args[3]), fps > 0 else {
    fputs("usage: frames_to_gif.swift FRAME_DIR PREFIX FPS OUTPUT.gif\n", stderr)
    exit(2)
}
let dir = URL(fileURLWithPath: args[1])
let names = try FileManager.default.contentsOfDirectory(atPath: dir.path)
let frames = names.filter { $0.hasPrefix(args[2] + "_") && $0.hasSuffix(".png") }.sorted()
guard !frames.isEmpty,
      let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: args[4]) as CFURL,
                                                 UTType.gif.identifier as CFString, frames.count, nil) else { exit(1) }
CGImageDestinationSetProperties(dest, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFLoopCount: 0]] as CFDictionary)
let properties = [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: 1 / fps]] as CFDictionary
for name in frames {
    guard let source = CGImageSourceCreateWithURL(dir.appendingPathComponent(name) as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { exit(1) }
    CGImageDestinationAddImage(dest, image, properties)
}
guard CGImageDestinationFinalize(dest) else { exit(1) }
print("wrote \(args[4]) (\(frames.count) frames)")
