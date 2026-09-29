// Encode a numbered image sequence (as written by `scener --render --frames`) into an H.264 MP4
// with AVFoundation, so no external encoder is needed on macOS.
//
// Usage: swift apps/scener/tools/frames_to_mp4.swift FRAME_DIR PREFIX FPS OUTPUT.mp4
// Frames are FRAME_DIR/PREFIX_0000.png (or .jpg), PREFIX_0001 … in order.

import AVFoundation
import CoreGraphics
import Foundation
import ImageIO

let args = CommandLine.arguments
guard args.count == 5, let fps = Int32(args[3]), fps > 0 else {
    FileHandle.standardError.write("usage: frames_to_mp4.swift FRAME_DIR PREFIX FPS OUTPUT.mp4\n".data(using: .utf8)!)
    exit(2)
}
let dir = URL(fileURLWithPath: args[1])
let prefix = args[2] + "_"
let output = URL(fileURLWithPath: args[4])

let names = (try? FileManager.default.contentsOfDirectory(atPath: dir.path)) ?? []
let frames = names.filter { $0.hasPrefix(prefix) && ($0.hasSuffix(".png") || $0.hasSuffix(".jpg")) }.sorted()
guard !frames.isEmpty else {
    FileHandle.standardError.write("no frames named \(prefix)NNNN in \(dir.path)\n".data(using: .utf8)!)
    exit(1)
}

func loadImage(_ name: String) -> CGImage? {
    guard let source = CGImageSourceCreateWithURL(dir.appendingPathComponent(name) as CFURL, nil) else { return nil }
    return CGImageSourceCreateImageAtIndex(source, 0, nil)
}

guard let first = loadImage(frames[0]) else { exit(1) }
// H.264 needs even dimensions.
let width = first.width & ~1, height = first.height & ~1
try? FileManager.default.removeItem(at: output)
let writer = try AVAssetWriter(outputURL: output, fileType: .mp4)
let settings: [String: Any] = [
    AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: width, AVVideoHeightKey: height,
    AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: width * height * 8],
]
let input = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [
    kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32ARGB, kCVPixelBufferWidthKey as String: width,
    kCVPixelBufferHeightKey as String: height,
])
writer.add(input)
writer.startWriting()
writer.startSession(atSourceTime: .zero)

for (index, name) in frames.enumerated() {
    guard let image = loadImage(name) else { continue }
    while !input.isReadyForMoreMediaData { Thread.sleep(forTimeInterval: 0.002) }
    var buffer: CVPixelBuffer?
    CVPixelBufferPoolCreatePixelBuffer(nil, adaptor.pixelBufferPool!, &buffer)
    guard let pixels = buffer else { continue }
    CVPixelBufferLockBaseAddress(pixels, [])
    let context = CGContext(data: CVPixelBufferGetBaseAddress(pixels), width: width, height: height, bitsPerComponent: 8,
                            bytesPerRow: CVPixelBufferGetBytesPerRow(pixels), space: CGColorSpaceCreateDeviceRGB(),
                            bitmapInfo: CGImageAlphaInfo.noneSkipFirst.rawValue)
    context?.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    CVPixelBufferUnlockBaseAddress(pixels, [])
    adaptor.append(pixels, withPresentationTime: CMTime(value: CMTimeValue(index), timescale: fps))
}
input.markAsFinished()
let done = DispatchSemaphore(value: 0)
writer.finishWriting { done.signal() }
done.wait()
if writer.status != .completed {
    FileHandle.standardError.write("encoding failed: \(String(describing: writer.error))\n".data(using: .utf8)!)
    exit(1)
}
print("wrote \(output.path) (\(frames.count) frames, \(width)x\(height), \(fps) fps)")
