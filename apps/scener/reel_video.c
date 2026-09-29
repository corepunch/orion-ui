/* Reel video: on macOS frames go straight from GL readback to the hardware
   H.264 encoder through VideoToolbox and are muxed into a minimal MP4 (ftyp,
   mdat, moov) here; no image files, helper process or ffmpeg is involved. */
#include "reel.h"

#ifdef __APPLE__
#include <VideoToolbox/VideoToolbox.h>

#define REEL_VIDEO_BITS_PER_PIXEL 0.35
#define REEL_VIDEO_KEY_SECONDS 2

struct reel_video_s {
	FILE *f;
	char path[1024];
	int width, height, frames;
	uint32_t timescale, delta;
	VTCompressionSessionRef session;
	CVPixelBufferPoolRef pool;
	uint32_t *sizes, *sync; uint64_t *offsets; int n, csizes, csync, coffsets, nsync;
	uint8_t *avcc; size_t navcc;
	uint64_t pos, mdat;
	bool failed;
};

typedef struct { uint8_t *d; size_t n, c; } reel_mp4_t;

static void mp4_bytes(reel_mp4_t *b, const void *p, size_t n) {
	if (b->n + n > b->c) { b->c = (b->n + n) * 2; b->d = realloc(b->d, b->c); }
	memcpy(b->d + b->n, p, n); b->n += n;
}
static void mp4_u8(reel_mp4_t *b, uint32_t v) { uint8_t x = (uint8_t)v; mp4_bytes(b, &x, 1); }
static void mp4_u16(reel_mp4_t *b, uint32_t v) { uint8_t x[2] = {(uint8_t)(v >> 8), (uint8_t)v}; mp4_bytes(b, x, 2); }
static void mp4_u32(reel_mp4_t *b, uint32_t v) { uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; mp4_bytes(b, x, 4); }
static void mp4_zero(reel_mp4_t *b, size_t n) { while (n--) mp4_u8(b, 0); }
static size_t mp4_open(reel_mp4_t *b, const char *type) { size_t at = b->n; mp4_u32(b, 0); mp4_bytes(b, type, 4); return at; }
static size_t mp4_full(reel_mp4_t *b, const char *type, uint32_t flags) { size_t at = mp4_open(b, type); mp4_u32(b, flags); return at; }
static void mp4_close(reel_mp4_t *b, size_t at) {
	uint32_t size = (uint32_t)(b->n - at);
	b->d[at] = (uint8_t)(size >> 24); b->d[at + 1] = (uint8_t)(size >> 16); b->d[at + 2] = (uint8_t)(size >> 8); b->d[at + 3] = (uint8_t)size;
}
static void mp4_matrix(reel_mp4_t *b) {
	static const uint32_t m[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
	for (int i = 0; i < 9; i++) mp4_u32(b, m[i]);
}

static void reel_video_output(void *ref, void *frame, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
	(void)frame; (void)flags;
	reel_video_t *v = ref;
	if (status != noErr || !sample) { fprintf(stderr, "[reel] H.264 encoder failed: %d\n", (int)status); v->failed = true; return; }
	if (!v->avcc) {
		CFDictionaryRef atoms = CMFormatDescriptionGetExtension(CMSampleBufferGetFormatDescription(sample),
		                                                        kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
		CFDataRef avcc = atoms ? CFDictionaryGetValue(atoms, CFSTR("avcC")) : NULL;
		if (!avcc) { fprintf(stderr, "[reel] encoder produced no avcC\n"); v->failed = true; return; }
		v->navcc = (size_t)CFDataGetLength(avcc);
		v->avcc = malloc(v->navcc);
		memcpy(v->avcc, CFDataGetBytePtr(avcc), v->navcc);
	}
	bool sync = true;
	CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
	if (attachments && CFArrayGetCount(attachments)) {
		CFBooleanRef not_sync;
		if (CFDictionaryGetValueIfPresent(CFArrayGetValueAtIndex(attachments, 0), kCMSampleAttachmentKey_NotSync, (const void **)&not_sync))
			sync = !CFBooleanGetValue(not_sync);
	}
	CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
	size_t size = CMBlockBufferGetDataLength(block);
	uint8_t *data = malloc(size);
	if (!data || CMBlockBufferCopyDataBytes(block, 0, size, data) != kCMBlockBufferNoErr || fwrite(data, 1, size, v->f) != size) {
		fprintf(stderr, "[reel] cannot write %s\n", v->path); v->failed = true; free(data); return;
	}
	free(data);
	int index = v->n, count = index;
	if (sync) DA_PUSH(v->sync, v->nsync, v->csync, (uint32_t)(index + 1));
	DA_PUSH(v->offsets, count, v->coffsets, v->pos);
	count = index; DA_PUSH(v->sizes, count, v->csizes, (uint32_t)size);
	v->n = index + 1;
	v->pos += size;
}

static void reel_video_set(VTCompressionSessionRef s, CFStringRef key, CFTypeRef value) {
	OSStatus status = VTSessionSetProperty(s, key, value);
	if (status != noErr) { fprintf(stderr, "[reel] encoder ignored %s (%d)\n", CFStringGetCStringPtr(key, kCFStringEncodingUTF8), (int)status); fflush(stderr); }
}

static void reel_video_set_number(VTCompressionSessionRef s, CFStringRef key, double number) {
	CFNumberRef value = CFNumberCreate(NULL, kCFNumberDoubleType, &number);
	reel_video_set(s, key, value);
	CFRelease(value);
}

reel_video_t *reel_video_open(const char *path, int width, int height, float fps) {
	reel_video_t *v = calloc(1, sizeof(*v));
	snprintf(v->path, sizeof(v->path), "%s", path);
	v->width = width; v->height = height;
	v->timescale = (uint32_t)lroundf(fps * 1000); v->delta = 1000;
	v->f = fopen(path, "wb");
	if (!v->f) { fprintf(stderr, "[reel] cannot create %s\n", path); fflush(stderr); free(v); return NULL; }
	reel_mp4_t head = {0};
	size_t ftyp = mp4_open(&head, "ftyp");
	mp4_bytes(&head, "isom", 4); mp4_u32(&head, 512); mp4_bytes(&head, "isomiso2avc1mp41", 16);
	mp4_close(&head, ftyp);
	v->mdat = head.n;
	mp4_u32(&head, 0); mp4_bytes(&head, "mdat", 4);
	fwrite(head.d, 1, head.n, v->f);
	v->pos = head.n;
	free(head.d);
	int32_t format = kCVPixelFormatType_32BGRA, w = width, h = height;
	CFNumberRef numbers[3] = {CFNumberCreate(NULL, kCFNumberSInt32Type, &format), CFNumberCreate(NULL, kCFNumberSInt32Type, &w),
	                          CFNumberCreate(NULL, kCFNumberSInt32Type, &h)};
	CFDictionaryRef surface = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	const void *keys[] = {kCVPixelBufferPixelFormatTypeKey, kCVPixelBufferWidthKey, kCVPixelBufferHeightKey, kCVPixelBufferIOSurfacePropertiesKey};
	const void *values[] = {numbers[0], numbers[1], numbers[2], surface};
	CFDictionaryRef source = CFDictionaryCreate(NULL, keys, values, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	OSStatus status = VTCompressionSessionCreate(NULL, width, height, kCMVideoCodecType_H264, NULL, source, NULL,
	                                             reel_video_output, v, &v->session);
	if (status == noErr && CVPixelBufferPoolCreate(NULL, NULL, source, &v->pool) != kCVReturnSuccess) v->pool = NULL;
	CFRelease(source); CFRelease(surface);
	for (int i = 0; i < 3; i++) CFRelease(numbers[i]);
	if (status != noErr) { fprintf(stderr, "[reel] cannot create an H.264 encoder (%d)\n", (int)status); fflush(stderr); fclose(v->f); free(v); return NULL; }
	reel_video_set(v->session, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
	reel_video_set(v->session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
	reel_video_set(v->session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
	reel_video_set_number(v->session, kVTCompressionPropertyKey_AverageBitRate, REEL_VIDEO_BITS_PER_PIXEL * width * height * fps);
	reel_video_set_number(v->session, kVTCompressionPropertyKey_MaxKeyFrameInterval, ceil(fps * REEL_VIDEO_KEY_SECONDS));
	reel_video_set_number(v->session, kVTCompressionPropertyKey_ExpectedFrameRate, fps);
	reel_video_set(v->session, kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2);
	reel_video_set(v->session, kVTCompressionPropertyKey_TransferFunction, kCVImageBufferTransferFunction_ITU_R_709_2);
	reel_video_set(v->session, kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2);
	VTCompressionSessionPrepareToEncodeFrames(v->session);
	if (!v->pool) { fprintf(stderr, "[reel] cannot allocate BGRA frames\n"); fflush(stderr); reel_video_close(v); return NULL; }
	return v;
}

bool reel_video_write(reel_video_t *v, const uint8_t *rgba) {
	if (!v || v->failed) return false;
	CVPixelBufferRef pb = NULL;
	if (CVPixelBufferPoolCreatePixelBuffer(NULL, v->pool, &pb) != kCVReturnSuccess) { fprintf(stderr, "[reel] no pixel buffer\n"); v->failed = true; return false; }
	CVBufferSetAttachment(pb, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
	CVBufferSetAttachment(pb, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
	CVBufferSetAttachment(pb, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
	CVPixelBufferLockBaseAddress(pb, 0);
	uint8_t *base = CVPixelBufferGetBaseAddress(pb);
	size_t stride = CVPixelBufferGetBytesPerRow(pb);
	OSType format = CVPixelBufferGetPixelFormatType(pb);
	for (int y = 0; y < v->height; y++) {
		const uint8_t *src = rgba + (size_t)y * v->width * 4;
		uint8_t *dst = base + (size_t)y * stride;
		if (format == kCVPixelFormatType_32BGRA)
			for (int x = 0; x < v->width; x++, src += 4, dst += 4) { dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255; }
		else if (format == kCVPixelFormatType_32ARGB)
			for (int x = 0; x < v->width; x++, src += 4, dst += 4) { dst[0] = 255; dst[1] = src[0]; dst[2] = src[1]; dst[3] = src[2]; }
		else { CVPixelBufferUnlockBaseAddress(pb, 0); CVPixelBufferRelease(pb); fprintf(stderr, "[reel] unsupported encoder pixel format %08x\n", (unsigned)format); v->failed = true; return false; }
	}
	CVPixelBufferUnlockBaseAddress(pb, 0);
	OSStatus status = VTCompressionSessionEncodeFrame(v->session, pb, CMTimeMake((int64_t)v->frames * v->delta, (int32_t)v->timescale),
	                                                  CMTimeMake(v->delta, (int32_t)v->timescale), NULL, NULL, NULL);
	CVPixelBufferRelease(pb);
	v->frames++;
	if (status != noErr) { fprintf(stderr, "[reel] frame %d was not encoded (%d)\n", v->frames, (int)status); v->failed = true; }
	return !v->failed;
}

static void reel_video_moov(reel_video_t *v, reel_mp4_t *b) {
	uint64_t media = (uint64_t)v->n * v->delta, movie = media * 1000 / v->timescale;
	size_t moov = mp4_open(b, "moov");
	size_t mvhd = mp4_full(b, "mvhd", 0);
	mp4_u32(b, 0); mp4_u32(b, 0); mp4_u32(b, 1000); mp4_u32(b, (uint32_t)movie);
	mp4_u32(b, 0x00010000); mp4_u16(b, 0x0100); mp4_zero(b, 10); mp4_matrix(b); mp4_zero(b, 24); mp4_u32(b, 2);
	mp4_close(b, mvhd);
	size_t trak = mp4_open(b, "trak");
	size_t tkhd = mp4_full(b, "tkhd", 3);
	mp4_u32(b, 0); mp4_u32(b, 0); mp4_u32(b, 1); mp4_u32(b, 0); mp4_u32(b, (uint32_t)movie);
	mp4_zero(b, 8); mp4_u16(b, 0); mp4_u16(b, 0); mp4_u16(b, 0); mp4_u16(b, 0); mp4_matrix(b);
	mp4_u32(b, (uint32_t)v->width << 16); mp4_u32(b, (uint32_t)v->height << 16);
	mp4_close(b, tkhd);
	size_t mdia = mp4_open(b, "mdia");
	size_t mdhd = mp4_full(b, "mdhd", 0);
	mp4_u32(b, 0); mp4_u32(b, 0); mp4_u32(b, v->timescale); mp4_u32(b, (uint32_t)media); mp4_u16(b, 0x55C4); mp4_u16(b, 0);
	mp4_close(b, mdhd);
	size_t hdlr = mp4_full(b, "hdlr", 0);
	mp4_u32(b, 0); mp4_bytes(b, "vide", 4); mp4_zero(b, 12); mp4_bytes(b, "VideoHandler", 13);
	mp4_close(b, hdlr);
	size_t minf = mp4_open(b, "minf");
	size_t vmhd = mp4_full(b, "vmhd", 1); mp4_zero(b, 8); mp4_close(b, vmhd);
	size_t dinf = mp4_open(b, "dinf"), dref = mp4_full(b, "dref", 0);
	mp4_u32(b, 1); size_t url = mp4_full(b, "url ", 1); mp4_close(b, url);
	mp4_close(b, dref); mp4_close(b, dinf);
	size_t stbl = mp4_open(b, "stbl");
	size_t stsd = mp4_full(b, "stsd", 0);
	mp4_u32(b, 1);
	size_t avc1 = mp4_open(b, "avc1");
	mp4_zero(b, 6); mp4_u16(b, 1); mp4_zero(b, 16); mp4_u16(b, (uint32_t)v->width); mp4_u16(b, (uint32_t)v->height);
	mp4_u32(b, 0x00480000); mp4_u32(b, 0x00480000); mp4_u32(b, 0); mp4_u16(b, 1);
	char compressor[32] = {5, 'S', 'c', 'e', 'n', 'e', 'r'};
	mp4_bytes(b, compressor, 32); mp4_u16(b, 0x0018); mp4_u16(b, 0xFFFF);
	size_t avcc = mp4_open(b, "avcC"); mp4_bytes(b, v->avcc, v->navcc); mp4_close(b, avcc);
	size_t colr = mp4_open(b, "colr"); mp4_bytes(b, "nclx", 4); mp4_u16(b, 1); mp4_u16(b, 1); mp4_u16(b, 1); mp4_u8(b, 0); mp4_close(b, colr);
	mp4_close(b, avc1);
	mp4_close(b, stsd);
	size_t stts = mp4_full(b, "stts", 0); mp4_u32(b, 1); mp4_u32(b, (uint32_t)v->n); mp4_u32(b, v->delta); mp4_close(b, stts);
	size_t stss = mp4_full(b, "stss", 0); mp4_u32(b, (uint32_t)v->nsync);
	for (int i = 0; i < v->nsync; i++) mp4_u32(b, v->sync[i]);
	mp4_close(b, stss);
	size_t stsc = mp4_full(b, "stsc", 0); mp4_u32(b, 1); mp4_u32(b, 1); mp4_u32(b, 1); mp4_u32(b, 1); mp4_close(b, stsc);
	size_t stsz = mp4_full(b, "stsz", 0); mp4_u32(b, 0); mp4_u32(b, (uint32_t)v->n);
	for (int i = 0; i < v->n; i++) mp4_u32(b, v->sizes[i]);
	mp4_close(b, stsz);
	size_t stco = mp4_full(b, "stco", 0); mp4_u32(b, (uint32_t)v->n);
	for (int i = 0; i < v->n; i++) mp4_u32(b, (uint32_t)v->offsets[i]);
	mp4_close(b, stco);
	mp4_close(b, stbl); mp4_close(b, minf); mp4_close(b, mdia); mp4_close(b, trak); mp4_close(b, moov);
}

bool reel_video_close(reel_video_t *v) {
	if (!v) return false;
	if (v->session) {
		VTCompressionSessionCompleteFrames(v->session, kCMTimeInvalid);
		VTCompressionSessionInvalidate(v->session);
		CFRelease(v->session);
	}
	if (v->pool) CVPixelBufferPoolRelease(v->pool);
	bool ok = !v->failed && v->n == v->frames && v->n > 0 && v->avcc;
	if (ok && v->pos > UINT32_MAX) { fprintf(stderr, "[reel] %s exceeds 4 GB\n", v->path); ok = false; }
	if (ok) {
		reel_mp4_t moov = {0};
		reel_video_moov(v, &moov);
		uint32_t mdat = (uint32_t)(v->pos - v->mdat);
		uint8_t size[4] = {(uint8_t)(mdat >> 24), (uint8_t)(mdat >> 16), (uint8_t)(mdat >> 8), (uint8_t)mdat};
		ok = fwrite(moov.d, 1, moov.n, v->f) == moov.n && fseek(v->f, (long)v->mdat, SEEK_SET) == 0 && fwrite(size, 1, 4, v->f) == 4;
		free(moov.d);
	} else if (!v->failed) fprintf(stderr, "[reel] encoder returned %d of %d frames\n", v->n, v->frames);
	if (fclose(v->f) != 0) ok = false;
	if (!ok) { fprintf(stderr, "[reel] video %s is incomplete\n", v->path); remove(v->path); }
	fflush(stderr);
	free(v->sizes); free(v->sync); free(v->offsets); free(v->avcc); free(v);
	return ok;
}

#else

/* Elsewhere, raw RGBA frames are piped to ffmpeg when it is installed. */
struct reel_video_s { FILE *pipe; int width, height; };

reel_video_t *reel_video_open(const char *path, int width, int height, float fps) {
	char command[2048];
	snprintf(command, sizeof(command), "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %g -i - "
	         "-c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -movflags +faststart '%s'", width, height, fps, path);
	FILE *pipe = popen(command, "w");
	if (!pipe) {
		fprintf(stderr, "[reel] MP4 output needs ffmpeg on PATH; render PNG frames with --output-dir instead of %s\n", path);
		fflush(stderr);
		return NULL;
	}
	reel_video_t *v = calloc(1, sizeof(*v));
	v->pipe = pipe; v->width = width; v->height = height;
	return v;
}
bool reel_video_write(reel_video_t *v, const uint8_t *rgba) {
	size_t bytes = (size_t)v->width * v->height * 4;
	return v && fwrite(rgba, 1, bytes, v->pipe) == bytes;
}
bool reel_video_close(reel_video_t *v) {
	if (!v) return false;
	int status = pclose(v->pipe);
	free(v);
	if (status != 0) { fprintf(stderr, "[reel] ffmpeg failed (%d)\n", status); fflush(stderr); }
	return status == 0;
}

#endif
