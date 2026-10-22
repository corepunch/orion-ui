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
    ASSERT(abs(cents) <= 8, "recorded upper guitar notes are within eight cents of concert tuning");
    ASSERT_FALSE(c.failed);
    ASSERT(signal_rms(c.buf, GR_SAMPLE_RATE * 3 / 2, GR_SAMPLE_RATE * 7 / 4) > 0.0005f, "held notes retain their natural decay beyond a second");
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

static void test_sampled_instruments(void) {
  TEST("sampled piano, bass and guitars cover their playing registers, respond to velocity and release cleanly");
  const sy_inst_t voices[] = { I_PIANO, I_HPIANO, I_BASS, I_PLUCK, I_CLEAN, I_MUTE, I_WAH, I_DIST };
  for (int v = 0; v < ARRAY_LEN(voices); v++) for (int register_ = 0; register_ < 2; register_++) {
    float power[2];
    for (int velocity = 0; velocity < 2; velocity++) {
      sy_ctx_t c = guitar_context();
      ASSERT_NOT_NULL(c.buf);
      int midi = voices[v] == I_BASS ? 33 + 24 * register_ : 45 + 31 * register_;
      sy_note_t n = { .g = GR_SAMPLE_RATE / 2, .hz = sy_midi_hz(midi), .a = velocity ? 0.85f : 0.3f };
      sy_voice(&c, voices[v], &n);
      ASSERT_FALSE(c.failed);
      for (int i = 0; i < c.n; i++) ASSERT_TRUE(isfinite(c.buf[i]));
      power[velocity] = signal_rms(c.buf, 0, GR_SAMPLE_RATE / 5);
      ASSERT(power[velocity] > 0.0001f, "the selected register has audible sampled attacks");
      ASSERT_EQUAL(signal_rms(c.buf, 2 * GR_SAMPLE_RATE, c.n), 0);
      free(c.buf);
    }
    ASSERT(power[1] > power[0], "a stronger velocity produces a stronger attack");
  }
  sy_ctx_t c = guitar_context();
  ASSERT_NOT_NULL(c.buf);
  sy_note_t n = { .g = GR_SAMPLE_RATE / 2, .hz = 440, .a = 0.8f };
  sampled_instrument(&c, &n, I_PIANO, 999);
  ASSERT_TRUE(c.failed);
  ASSERT_EQUAL(signal_rms(c.buf, 0, c.n), 0);
  free(c.buf);
  sy_samples_free();
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

static double signal_power(const float *pcm, int start, int end, float hz) {
  double re = 0, im = 0;
  for (int i = start; i < end; i++) {
    double ph = 6.283185307179586 * hz * i / GR_SAMPLE_RATE;
    re += pcm[i] * cos(ph); im += pcm[i] * sin(ph);
  }
  return re * re + im * im;
}

static void test_fm_aliasing(void) {
  TEST("bright FM notes suppress folded sidebands while retaining musical harmonics");
  sy_ctx_t c = guitar_context();
  ASSERT_NOT_NULL(c.buf);
  sy_note_t n = { .g = 2 * GR_SAMPLE_RATE, .hz = 5000, .a = 0.8f };
  v_fm(&c, &n, 1, 3.2f, 0, 0, 0, 30, 0.2f, 0);
  double wanted = 0, folded = 0;
  for (int h = 1; h <= 3; h++) wanted += signal_power(c.buf, GR_SAMPLE_RATE / 2, GR_SAMPLE_RATE * 3 / 2, 5000 * h);
  for (int h = 5; h <= 8; h++) folded += signal_power(c.buf, GR_SAMPLE_RATE / 2, GR_SAMPLE_RATE * 3 / 2, GR_SAMPLE_RATE - 5000 * h);
  ASSERT(signal_rms(c.buf, GR_SAMPLE_RATE / 2, GR_SAMPLE_RATE) > 0.05f, "anti-aliasing preserves the note body");
  ASSERT(folded < wanted * 0.0001, "folded sidebands stay at least 40 dB below retained harmonics");
  free(c.buf);
  PASS();
}

static void test_synth_gates(void) {
  TEST("short synth gates release from their current level and arpeggios respect held notes");
  int gate = GR_SAMPLE_RATE / 100;
  float last = env(gate, gate, 0.24f, 6.0f);
  for (int i = gate + 1; i < GR_SAMPLE_RATE; i++) {
    float next = env(i, gate, 0.24f, 6.0f);
    ASSERT(next <= last, "a released slow attack cannot keep swelling");
    last = next;
  }
  float held[2];
  for (int long_gate = 0; long_gate < 2; long_gate++) {
    sy_ctx_t c = guitar_context();
    ASSERT_NOT_NULL(c.buf);
    sy_note_t n = { .g = long_gate ? GR_SAMPLE_RATE / 2 : gate, .hz = 440, .a = 0.8f };
    sy_voice(&c, I_ARP, &n);
    held[long_gate] = signal_rms(c.buf, GR_SAMPLE_RATE / 4, GR_SAMPLE_RATE / 3);
    free(c.buf);
  }
  ASSERT_EQUAL(held[0], 0);
  ASSERT(held[1] > 0.001f, "long arpeggio notes retain a decaying body while held");
  PASS();
}

static void test_synth_repeatability_and_tails(void) {
  TEST("revised synth voices render deterministically across registers with clean tails");
  const sy_inst_t voices[] = { I_ARP, I_PAD, I_ORGAN, I_MOOG, I_SAW, I_STAB, I_BRASS, I_EPIANO, I_BELL, I_DONK, I_FMBASS, I_FMSEQ };
  for (int v = 0; v < ARRAY_LEN(voices); v++) for (int high = 0; high < 2; high++) {
    sy_ctx_t a = guitar_context(), b = guitar_context();
    ASSERT_NOT_NULL(a.buf); ASSERT_NOT_NULL(b.buf);
    sy_note_t n = { .g = GR_SAMPLE_RATE / 5, .hz = sy_midi_hz(high ? 93 : 33), .a = 0.8f };
    sy_voice(&a, voices[v], &n); sy_voice(&b, voices[v], &n);
    ASSERT_FALSE(a.failed); ASSERT_FALSE(b.failed);
    ASSERT_EQUAL(memcmp(a.buf, b.buf, (a.n + SY_TAIL) * sizeof(float)), 0);
    int last = 0;
    for (int i = 0; i < a.n + SY_TAIL; i++) {
      ASSERT_TRUE(isfinite(a.buf[i]));
      if (a.buf[i] != 0) last = i;
    }
    ASSERT(signal_rms(a.buf, 0, GR_SAMPLE_RATE / 5) > 0.00001f, "both registers produce sound");
    ASSERT(last < GR_SAMPLE_RATE * 3 / 2, "released notes terminate within their tails");
    ASSERT(fabsf(a.buf[last]) < 0.0001f, "note truncation stays below -80 dBFS");
    free(a.buf); free(b.buf);
  }
  PASS();
}

int main(void) {
  TEST_START("Groove synth");
  test_guitar_pitch_and_gate();
  test_sampled_instruments();
  test_hat_choke();
  test_fm_aliasing();
  test_synth_gates();
  test_synth_repeatability_and_tails();
  sy_samples_free();
  TEST_END();
}
