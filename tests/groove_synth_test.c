#include "test_framework.h"
#include "apps/groove/synth.c"

static sy_ctx_t guitar_context(void) {
  sy_ctx_t c = {0};
  c.n = 3 * GR_SAMPLE_RATE;
  c.bar = GR_SAMPLE_RATE * 240.0 / 140;
  c.rng = 1234567;
  c.buf = calloc((size_t)(c.n + SY_TAIL), sizeof(float));
  return c;
}

static float signal_rms(const float *pcm, int start, int end) {
  double sum = 0;
  for (int i = start; i < end; i++) sum += pcm[i] * pcm[i];
  return sqrt(sum / (end - start));
}

static void test_guitar_pitch_and_gate(void) {
  TEST("guitar voices stay tuned and sustained notes follow their gate");
  const sy_inst_t voices[] = { I_PLUCK, I_CLEAN, I_WAH, I_DIST };
  for (int v = 0; v < ARRAY_LEN(voices); v++) {
    sy_ctx_t c = guitar_context();
    ASSERT_NOT_NULL(c.buf);
    sy_note_t n = { .g = 2 * GR_SAMPLE_RATE, .hz = sy_midi_hz(76), .a = 0.8f };
    sy_voice(&c, voices[v], &n);
    double best = 0;
    int cents = -100;
    for (int offset = -35; offset <= 35; offset++) {
      double hz = n.hz * pow(2.0, offset / 1200.0), re = 0, im = 0;
      for (int i = GR_SAMPLE_RATE / 5; i < GR_SAMPLE_RATE * 3 / 5; i++) {
        double ph = 6.283185307179586 * hz * i / GR_SAMPLE_RATE;
        double window = 0.5 - 0.5 * cos(6.283185307179586 * (i - GR_SAMPLE_RATE / 5) / (GR_SAMPLE_RATE * 0.4));
        re += c.buf[i] * window * cos(ph); im += c.buf[i] * window * sin(ph);
      }
      double power = re * re + im * im;
      if (power > best) { best = power; cents = offset; }
    }
    ASSERT(abs(cents) <= 3, "upper guitar notes are within three cents of concert tuning");
    ASSERT(signal_rms(c.buf, GR_SAMPLE_RATE * 3 / 2, GR_SAMPLE_RATE * 7 / 4) > 0.005f, "held notes do not hit a fixed-duration cutoff");
    ASSERT(signal_rms(c.buf, GR_SAMPLE_RATE * 5 / 2, GR_SAMPLE_RATE * 11 / 4) == 0, "note release ends cleanly");
    free(c.buf);
  }
  sy_ctx_t c = guitar_context();
  ASSERT_NOT_NULL(c.buf);
  sy_note_t n = { .g = GR_SAMPLE_RATE, .hz = 110, .a = 0.8f };
  sy_voice(&c, I_MUTE, &n);
  ASSERT(signal_rms(c.buf, GR_SAMPLE_RATE / 3, GR_SAMPLE_RATE / 2) < 0.002f, "palm mute damps while the gate remains held");
  free(c.buf);
  PASS();
}

static void test_hat_choke(void) {
  TEST("closed hats choke open hats in either lane order, including at the loop seam");
  const char *patterns[] = { "o=x............... h=.x..............", "h=.x.............. o=x..............." };
  for (int p = 0; p < ARRAY_LEN(patterns); p++) {
    sy_ctx_t c = guitar_context();
    ASSERT_NOT_NULL(c.buf);
    c.n = (int)c.bar;
    sy_drums(&c, KIT_909, patterns[p]);
    ASSERT(signal_rms(c.buf, GR_SAMPLE_RATE / 50, GR_SAMPLE_RATE / 10) > 0.001f, "open hat attack is preserved");
    ASSERT(signal_rms(c.buf, GR_SAMPLE_RATE * 3 / 10, GR_SAMPLE_RATE * 4 / 10) == 0, "no open-hat wash after the closed hit");
    memset(c.buf, 0, (size_t)(c.n + SY_TAIL) * sizeof(float));
    sy_drums(&c, KIT_909, "o=...............x h=x...............");
    ASSERT(signal_rms(c.buf, c.n + 100, c.n + 2000) == 0, "the next cycle's closed hat chokes the tail");
    free(c.buf);
  }
  PASS();
}

int main(void) {
  TEST_START("Groove synth");
  test_guitar_pitch_and_gate();
  test_hat_choke();
  TEST_END();
}
