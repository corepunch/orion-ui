#include "groove.h"
#ifdef GROOVE_USE_SHINE
#pragma push_macro("BLKSIZE")
#undef BLKSIZE
#pragma push_macro("BUFFER_SIZE")
#undef BUFFER_SIZE
#pragma push_macro("CBLIMIT")
#undef CBLIMIT
#pragma push_macro("GRANULE_SIZE")
#undef GRANULE_SIZE
#pragma push_macro("HAN_SIZE")
#undef HAN_SIZE
#pragma push_macro("HTN")
#undef HTN
#pragma push_macro("HUFFBITS")
#undef HUFFBITS
#pragma push_macro("LN2")
#undef LN2
#pragma push_macro("LN_TO_LOG10")
#undef LN_TO_LOG10
#pragma push_macro("MAX")
#undef MAX
#pragma push_macro("MAX_CHANNELS")
#undef MAX_CHANNELS
#pragma push_macro("MAX_GRANULES")
#undef MAX_GRANULES
#pragma push_macro("MAX_LENGTH")
#undef MAX_LENGTH
#pragma push_macro("MDCT_CA")
#undef MDCT_CA
#pragma push_macro("MDCT_CA0")
#undef MDCT_CA0
#pragma push_macro("MDCT_CA1")
#undef MDCT_CA1
#pragma push_macro("MDCT_CA2")
#undef MDCT_CA2
#pragma push_macro("MDCT_CA3")
#undef MDCT_CA3
#pragma push_macro("MDCT_CA4")
#undef MDCT_CA4
#pragma push_macro("MDCT_CA5")
#undef MDCT_CA5
#pragma push_macro("MDCT_CA6")
#undef MDCT_CA6
#pragma push_macro("MDCT_CA7")
#undef MDCT_CA7
#pragma push_macro("MDCT_CS")
#undef MDCT_CS
#pragma push_macro("MDCT_CS0")
#undef MDCT_CS0
#pragma push_macro("MDCT_CS1")
#undef MDCT_CS1
#pragma push_macro("MDCT_CS2")
#undef MDCT_CS2
#pragma push_macro("MDCT_CS3")
#undef MDCT_CS3
#pragma push_macro("MDCT_CS4")
#undef MDCT_CS4
#pragma push_macro("MDCT_CS5")
#undef MDCT_CS5
#pragma push_macro("MDCT_CS6")
#undef MDCT_CS6
#pragma push_macro("MDCT_CS7")
#undef MDCT_CS7
#pragma push_macro("MIN")
#undef MIN
#pragma push_macro("MINIMUM")
#undef MINIMUM
#pragma push_macro("MXOFF")
#undef MXOFF
#pragma push_macro("NOREF")
#undef NOREF
#pragma push_macro("PI")
#undef PI
#pragma push_macro("PI12")
#undef PI12
#pragma push_macro("PI36")
#undef PI36
#pragma push_macro("PI4")
#undef PI4
#pragma push_macro("PI64")
#undef PI64
#pragma push_macro("SBLIMIT")
#undef SBLIMIT
#pragma push_macro("SCALE")
#undef SCALE
#pragma push_macro("SCALE_BLOCK")
#undef SCALE_BLOCK
#pragma push_macro("SCALE_RANGE")
#undef SCALE_RANGE
#pragma push_macro("SFB_LMAX")
#undef SFB_LMAX
#pragma push_macro("SHINE_EW")
#undef SHINE_EW
#pragma push_macro("SHINE_EW10")
#undef SHINE_EW10
#pragma push_macro("SHINE_EW2")
#undef SHINE_EW2
#pragma push_macro("SHINE_MAX_SAMPLES")
#undef SHINE_MAX_SAMPLES
#pragma push_macro("SQRT2")
#undef SQRT2
#pragma push_macro("SWAB32")
#undef SWAB32
#pragma push_macro("cmuls")
#undef cmuls
#pragma push_macro("e")
#undef e
#pragma push_macro("en_dif_krit")
#undef en_dif_krit
#pragma push_macro("en_scfsi_band_krit")
#undef en_scfsi_band_krit
#pragma push_macro("en_tot_krit")
#undef en_tot_krit
#pragma push_macro("mul")
#undef mul
#pragma push_macro("mul0")
#undef mul0
#pragma push_macro("muladd")
#undef muladd
#pragma push_macro("mulr")
#undef mulr
#pragma push_macro("muls")
#undef muls
#pragma push_macro("mulsr")
#undef mulsr
#pragma push_macro("mulsub")
#undef mulsub
#pragma push_macro("mulz")
#undef mulz
#pragma push_macro("xm_scfsi_band_krit")
#undef xm_scfsi_band_krit
#include "third_party/shine/bitstream.inc"
#include "third_party/shine/huffman.inc"
#include "third_party/shine/l3bitstream.inc"
#include "third_party/shine/l3loop.inc"
#undef e
#include "third_party/shine/l3mdct.inc"
#include "third_party/shine/l3subband.inc"
#include "third_party/shine/layer3.inc"
#include "third_party/shine/reservoir.inc"
#include "third_party/shine/tables.inc"

bool groove_encode_mp3(const char *wav, const char *path, ui_media_progress_fn progress, void *userdata) {
  FILE *in = fopen(wav, "rb"), *out = NULL;
  shine_config_t cfg = {0};
  shine_set_config_mpeg_defaults(&cfg.mpeg);
  cfg.wave.channels = PCM_STEREO; cfg.wave.samplerate = GR_SAMPLE_RATE;
  cfg.mpeg.mode = STEREO; cfg.mpeg.bitr = 192;
  shine_t encoder = shine_initialise(&cfg);
  bool ok = in && encoder;
  long bytes = 0;
  if (ok) {
    ok = fseek(in, 0, SEEK_END) == 0;
    bytes = ftell(in) - 44;
    ok = ok && bytes > 0 && fseek(in, 44, SEEK_SET) == 0;
    if (ok) out = fopen(path, "wb");
    ok = ok && out;
  }
  int frames = encoder ? shine_samples_per_pass(encoder) : 0;
  int16_t pcm[SHINE_MAX_SAMPLES * 2];
  for (long used = 0; ok && used < bytes + frames * 4;) {
    if (progress && !progress(userdata, (float)used / (bytes + frames * 4))) { ok = false; break; }
    size_t n = used < bytes ? (size_t)MIN(bytes - used, frames * 4) : 0;
    memset(pcm, 0, sizeof(pcm));
    ok = fread(pcm, 1, n, in) == n;
    // The WAV writer is little endian; Shine takes native signed samples.
    for (size_t i = n / 2; i-- > 0;) {
      uint8_t *p = (uint8_t *)pcm + i * 2;
      pcm[i] = (int16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
    }
    int written = 0;
    unsigned char *data = shine_encode_buffer_interleaved(encoder, pcm, &written);
    if (written && fwrite(data, 1, (size_t)written, out) != (size_t)written) ok = false;
    used += n ? (long)n : frames * 4;
  }
  if (ok) {
    int written = 0;
    unsigned char *data = shine_flush(encoder, &written);
    ok = !written || fwrite(data, 1, (size_t)written, out) == (size_t)written;
    // Shine buffers 0..3 final bytes in its 32-bit bitstream cache. Its public
    // flush returns only completed words; publish the remaining frame bytes.
    for (int bits = 32 - encoder->bs.cache_bits; ok && bits > 0; bits -= 8) {
      uint8_t byte = (uint8_t)(encoder->bs.cache >> (bits - 8 + encoder->bs.cache_bits));
      ok = fwrite(&byte, 1, 1, out) == 1;
    }
  }
  if (encoder) shine_close(encoder);
  if (in) fclose(in);
  if (out && fclose(out)) ok = false;
  if (!ok) remove(path);
  return ok;
}

#pragma pop_macro("xm_scfsi_band_krit")
#pragma pop_macro("mulz")
#pragma pop_macro("mulsub")
#pragma pop_macro("mulsr")
#pragma pop_macro("muls")
#pragma pop_macro("mulr")
#pragma pop_macro("muladd")
#pragma pop_macro("mul0")
#pragma pop_macro("mul")
#pragma pop_macro("en_tot_krit")
#pragma pop_macro("en_scfsi_band_krit")
#pragma pop_macro("en_dif_krit")
#pragma pop_macro("e")
#pragma pop_macro("cmuls")
#pragma pop_macro("SWAB32")
#pragma pop_macro("SQRT2")
#pragma pop_macro("SHINE_MAX_SAMPLES")
#pragma pop_macro("SHINE_EW2")
#pragma pop_macro("SHINE_EW10")
#pragma pop_macro("SHINE_EW")
#pragma pop_macro("SFB_LMAX")
#pragma pop_macro("SCALE_RANGE")
#pragma pop_macro("SCALE_BLOCK")
#pragma pop_macro("SCALE")
#pragma pop_macro("SBLIMIT")
#pragma pop_macro("PI64")
#pragma pop_macro("PI4")
#pragma pop_macro("PI36")
#pragma pop_macro("PI12")
#pragma pop_macro("PI")
#pragma pop_macro("NOREF")
#pragma pop_macro("MXOFF")
#pragma pop_macro("MINIMUM")
#pragma pop_macro("MIN")
#pragma pop_macro("MDCT_CS7")
#pragma pop_macro("MDCT_CS6")
#pragma pop_macro("MDCT_CS5")
#pragma pop_macro("MDCT_CS4")
#pragma pop_macro("MDCT_CS3")
#pragma pop_macro("MDCT_CS2")
#pragma pop_macro("MDCT_CS1")
#pragma pop_macro("MDCT_CS0")
#pragma pop_macro("MDCT_CS")
#pragma pop_macro("MDCT_CA7")
#pragma pop_macro("MDCT_CA6")
#pragma pop_macro("MDCT_CA5")
#pragma pop_macro("MDCT_CA4")
#pragma pop_macro("MDCT_CA3")
#pragma pop_macro("MDCT_CA2")
#pragma pop_macro("MDCT_CA1")
#pragma pop_macro("MDCT_CA0")
#pragma pop_macro("MDCT_CA")
#pragma pop_macro("MAX_LENGTH")
#pragma pop_macro("MAX_GRANULES")
#pragma pop_macro("MAX_CHANNELS")
#pragma pop_macro("MAX")
#pragma pop_macro("LN_TO_LOG10")
#pragma pop_macro("LN2")
#pragma pop_macro("HUFFBITS")
#pragma pop_macro("HTN")
#pragma pop_macro("HAN_SIZE")
#pragma pop_macro("GRANULE_SIZE")
#pragma pop_macro("CBLIMIT")
#pragma pop_macro("BUFFER_SIZE")
#pragma pop_macro("BLKSIZE")
#endif
