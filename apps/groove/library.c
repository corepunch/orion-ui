// Block library: every sound in the bin, as one table row per block. A row
// names its family tab, its genre tags and a recipe for the engine in
// synth.c. docs/sound-synthesis.md explains the recipe notation.

#include "synth.h"

#define SR  SY_SR
#define TAU SY_TAU

const char *const kCategoryName[CAT_COUNT] = {
  "Drums", "Kicks", "Snares", "Hats", "Claps", "Cymbals", "Perc", "Fills",
  "Bass", "Keys", "Organ", "Guitar", "Synth", "Pads", "Stabs", "Vocals", "Scratch", "FX",
};

const char *const kGenreName[GENRE_COUNT] = { "Dance", "Hip Hop", "Rave", "Techno" };

uint32_t category_color(category_t cat) {
  static const uint32_t col[CAT_COUNT] = {
    WEB(0x1aa6f2), WEB(0xff7a2a), WEB(0xff4fa0), WEB(0xf2d02c), WEB(0xff7f5e), WEB(0x5cc6f8), WEB(0xffa12c), WEB(0xe64fd8),
    WEB(0xa047f2), WEB(0xffb21e), WEB(0x36c95c), WEB(0xff4f6c), WEB(0x1fd2b2), WEB(0x8ad13f), WEB(0xf2553d), WEB(0xff63a6),
    WEB(0x7c8cff), WEB(0x6c5cff),
  };
  return col[cat >= 0 && cat < CAT_COUNT ? cat : 0];
}

int bar_frames_for_bpm(int bpm) {
  bpm = bpm < GR_BPM_MIN ? GR_BPM_MIN : bpm > GR_BPM_MAX ? GR_BPM_MAX : bpm;
  return (int)((double)GR_SAMPLE_RATE * 60.0 * GR_BEATS_BAR / bpm + 0.5) & ~3; // multiple of 4: beats stay integral
}

typedef struct {
  const char *name;
  uint8_t     cat, genres, bars;
  uint8_t     voice;            // sy_kit_t on a drum row, sy_inst_t on a note row
  uint16_t    fx;               // X_* flags
  uint8_t     spb;              // steps per beat on a note row; 0 on a drum row
  float       hold;             // gate length in steps
  const char *pat;              // drum lanes or note steps; NULL when `fn` renders the block
  const char *arg;              // vowel words for the formant voices
  void      (*fn)(sy_ctx_t *c);
  const char *sample;           // optional MP3 sample, rendered instead of a synth recipe
} def_t;

// ── Hand-written recipes ─────────────────────────────────────────────────
#define GEN(fn) static void fn(sy_ctx_t *c)
#define BEAT(i) ((int)((i) * c->bar / 4.0))
#define STEP(i) ((int)((i) * c->bar / 16.0))

GEN(g_riser) { sy_riser(c, c->n); }
GEN(g_sweep) { sy_riser(c, c->n); }
GEN(g_room_pulse) { for (int i = 0; i < 4; i++) sy_kick_len(c, (int)(i * c->bar / 4.0), 0.9f, 4.2f, 0.55f); }
GEN(g_half_kick) { for (int i = 0; i < 2; i++) sy_kick_len(c, (int)(i * c->bar / 2.0), 0.95f, 3.0f, 0.75f); }
GEN(g_kick_run) { sy_kick_len(c, 0, 0.9f, 6.0f, 0.4f); for (int i = 8; i < 16; i++) sy_kick_len(c, (int)(i * c->bar / 16.0), 0.7f, 11.0f, 0.11f); }
GEN(g_clap_stack) { sy_drums(c, KIT_CLASSIC, "c=....x.......x..."); sy_clap(c, (int)(4 * c->bar / 16.0) + 160, 0.55f); sy_clap(c, (int)(12 * c->bar / 16.0) + 160, 0.55f); }
GEN(g_ride_8) { for (int i = 0; i < 8; i++) sy_cymbal(c, (int)(i * c->bar / 8.0), i & 1 ? 0.32f : 0.55f, 16.0f, 0.16f); }
GEN(g_crash_one) { sy_cymbal(c, 0, 0.9f, 2.0f, 1.5f); }
GEN(g_bell_pat) { for (int i = 0; i < 8; i++) sy_cymbal(c, (int)(i * c->bar / 8.0), 0.4f, 26.0f, 0.07f); }
GEN(g_splash) { for (int i = 0; i < 4; i++) sy_cymbal(c, (int)(i * c->bar / 4.0), 0.75f, 7.0f, 0.2f); }
GEN(g_tamb_8) { for (int i = 0; i < 8; i++) sy_hat(c, (int)(i * c->bar / 8.0), 0.75f, 16.0f, 0.14f); }
GEN(g_conga) { static const float hz[8] = { 180, 180, 230, 180, 200, 180, 230, 150 }; for (int i = 0; i < 8; i++) sy_tom(c, (int)(i * c->bar / 8.0), hz[i], 0.8f); }
GEN(g_wood) { for (int i = 0; i < 4; i++) sy_cymbal(c, (int)(i * c->bar / 4.0), 0.45f, 48.0f, 0.04f); }
GEN(g_tom_down) { static const float hz[8] = { 240, 210, 180, 150, 130, 110, 92, 74 }; for (int i = 0; i < 8; i++) sy_tom(c, (int)(i * c->bar / 8.0), hz[i], 0.85f); }
GEN(g_kick_tumble) { sy_kick_len(c, 0, 0.9f, 5.0f, 0.4f); for (int i = 8; i < 16; i++) sy_kick_len(c, (int)(i * c->bar / 16.0), 0.75f, 12.0f, 0.1f); }
GEN(g_zip_up) { sy_noise_zip(c, 0, (int)(c->bar * 0.5), 1); }
GEN(g_zip_down) { sy_noise_zip(c, 0, (int)(c->bar * 0.5), 0); }
GEN(g_chop_loop) { for (int i = 0; i < 4; i++) sy_noise_zip(c, (int)(i * c->bar / 4.0), (int)(c->bar / 4.0 * 0.4), i & 1); }
GEN(g_brake) { sy_noise_zip(c, 0, (int)(c->bar * 0.85), 0); }
GEN(g_org_stab) { for (int i = 0; i < 4; i += 2) { int s = (int)(i * c->bar / 4.0), g = (int)(c->bar / 4.0); sy_organ_note(c, s, g, sy_midi_hz(57), 0.7f); sy_organ_note(c, s, g, sy_midi_hz(60), 0.5f); sy_organ_note(c, s, g, sy_midi_hz(64), 0.45f); } }
GEN(g_org_off) { for (int i = 1; i < 8; i += 2) { int s = (int)(i * c->bar / 8.0), g = (int)(c->bar / 10.0); sy_organ_note(c, s, g, sy_midi_hz(57), 0.65f); sy_organ_note(c, s, g, sy_midi_hz(64), 0.5f); } }
GEN(g_org_hold) { int g = c->n - (int)(0.05f * SR); sy_organ_note(c, 0, g, sy_midi_hz(57), 0.6f); sy_organ_note(c, 0, g, sy_midi_hz(64), 0.45f); sy_organ_note(c, 0, g, sy_midi_hz(69), 0.4f); }
GEN(g_org_fifth) { for (int i = 0; i < 4; i++) { int s = (int)(i * c->bar / 4.0), g = (int)(c->bar / 5.0), m = i < 2 ? 57 : 53; sy_organ_note(c, s, g, sy_midi_hz(m), 0.65f); sy_organ_note(c, s, g, sy_midi_hz(m + 7), 0.5f); } }
GEN(g_noise_up) { sy_noise_bed(c, c->n, 1); }
GEN(g_noise_down) { sy_noise_bed(c, c->n, 0); }
GEN(g_impact) { sy_impact(c, 0); }
GEN(g_air) {
  float low = 0, band = 0, f = 2.0f * sinf(SY_PI * 800.0f / SR);
  for (int i = 0; i < c->n; i++) {
    float u = (float)i / (float)c->n, hi = sy_rnd(c) - low - 0.55f * band;
    band += f * hi; low += f * band;
    sy_mix(c, i, 0.28f * band * sinf(SY_PI * fminf(1.0f, u * 4.0f)));
  }
}

GEN(f_uplifter)    { sy_noise_sweep(c, 0, c->n, 300, 9000, 2.0f, 0.5f, 2.0f); sy_sweep_tone(c, 0, c->n, 110, 880, 2.0f, 0.25f, 0); }
GEN(f_downlifter)  { sy_noise_sweep(c, 0, c->n, 9000, 200, 2.0f, 0.5f, -1.5f); sy_sweep_tone(c, 0, c->n, 880, 55, 0.5f, 0.25f, 1.5f); }
GEN(f_sub_drop)    { sy_sweep_tone(c, 0, c->n, 160, 30, 0.5f, 0.9f, 1.0f); }
GEN(f_boom_drop)   { sy_hit(c, KIT_808, 'k', 0, 1.0f); sy_sweep_tone(c, 0, c->n, 120, 34, 0.4f, 0.8f, 1.6f); }
GEN(f_white_swell) { sy_noise_sweep(c, 0, c->n, 2000, 6000, 0.7f, 0.5f, 1.0f); }
GEN(f_noise_swell) { sy_noise_sweep(c, 0, c->n / 2, 400, 5000, 1.2f, 0.5f, 2.0f); sy_noise_sweep(c, c->n / 2, c->n / 2, 5000, 400, 1.2f, 0.5f, -2.0f); }
GEN(f_swish)       { sy_noise_sweep(c, 0, c->n / 4, 500, 8000, 3.0f, 0.6f, 1.0f); sy_noise_sweep(c, c->n / 4, c->n / 4, 8000, 500, 3.0f, 0.6f, -1.0f); }
GEN(f_cosmic)      { for (int k = 0; k < 4; k++) sy_noise_sweep(c, k * c->n / 4, c->n / 4, k & 1 ? 6000 : 700, k & 1 ? 700 : 6000, 6.0f, 0.5f, 0); }
GEN(f_rain)        { sy_noise_sweep(c, 0, c->n / 2, 7000, 600, 4.0f, 0.6f, -1.0f); }
GEN(f_steam)       { for (int k = 0; k < 4; k++) sy_noise_sweep(c, BEAT(k) + BEAT(1) / 2, BEAT(1) / 3, 3000, 6500, 1.0f, 0.6f, -2.0f); }
GEN(f_high_sweep)  { sy_sweep_tone(c, 0, c->n, 6000, 900, 0.6f, 0.4f, 0); sy_sweep_tone(c, 0, c->n, 6090, 930, 0.6f, 0.4f, 0); }
GEN(f_signal_jam)  { for (int k = 0; k < 8; k++) sy_sweep_tone(c, STEP(2 * k), STEP(2), k & 1 ? 2400 : 300, k & 1 ? 300 : 2400, 0.3f, 0.5f, 0); }
GEN(f_space_jump)  { sy_sweep_tone(c, 0, BEAT(1), 150, 4200, 1.5f, 0.7f, 2.0f); }
GEN(f_drop_boom)   { sy_sweep_tone(c, 0, BEAT(2), 900, 40, 0.5f, 0.6f, 0); sy_impact(c, BEAT(2)); }
GEN(f_boom_hall)   { sy_impact(c, 0); sy_hit(c, KIT_HARD, 'y', 0, 0.5f); }
GEN(f_tech_start)  { sy_noise_sweep(c, 0, c->n, 500, 7000, 3.0f, 0.5f, 3.0f); for (int k = 8; k < 16; k++) sy_hit(c, KIT_TECHNO, 's', STEP(k), 0.3f + 0.04f * k); }
GEN(f_siren)       { // two-tone rise and fall, twice a bar
  double ph = 0;
  for (int i = 0; i < c->n; i++) {
    float u = (float)i / (float)c->bar;
    ph += TAU * 700.0f * powf(2.0f, 0.6f * sinf(TAU * 2.0f * u)) / SR;
    sy_mix(c, i, 0.5f * (sinf((float)ph) + 0.3f * sinf(2.0f * (float)ph)) * fminf(1.0f, (c->n - i) / (0.01f * SR)));
  }
}
GEN(f_dub_siren)   { // fast wobble that slows as it falls
  double ph = 0, lfo = 0;
  for (int i = 0; i < c->n; i++) {
    float u = (float)i / (float)c->n;
    lfo += TAU * (9.0f - 6.0f * u) / SR;
    ph += TAU * (900.0f - 350.0f * u) * (1.0f + 0.25f * sinf((float)lfo)) / SR;
    sy_mix(c, i, 0.5f * (fmod(ph / TAU, 1.0) < 0.5 ? 1.0f : -1.0f) * (1.0f - u) * fminf(1.0f, i / (0.005f * SR)));
  }
}
GEN(f_bubbles)     { for (int k = 0; k < 28; k++) { float hz = 300.0f + 900.0f * fabsf(sy_rnd(c)); sy_sweep_tone(c, (int)(fabsf(sy_rnd(c)) * c->n * 0.95f), (int)(0.07f * SR), hz, hz * 2.2f, 1.0f, 0.5f, 25.0f); } }
GEN(f_static)      { // crackle: sparse clicks over a quiet hiss
  float lp = 0;
  for (int i = 0; i < c->n; i++) {
    float x = sy_rnd(c), click = fabsf(sy_rnd(c)) > 0.9993f ? sy_rnd(c) * 3.0f : 0.0f;
    lp += 0.35f * (click - lp);
    sy_mix(c, i, 0.08f * x + lp);
  }
}
GEN(f_vinyl_stop)  { sy_scratch(c, 0, (int)(c->n * 0.7), 0, 2.0f); }
GEN(f_rewind)      { sy_scratch(c, 0, (int)(c->n * 0.8), -1, 3.0f); }
GEN(f_baby)        { for (int k = 0; k < 4; k++) sy_scratch(c, BEAT(k), BEAT(1) / 2, 1.0f, 1.6f); }
GEN(f_chirp)       { for (int k = 0; k < 8; k++) if (k != 3 && k != 6) sy_scratch(c, STEP(2 * k), (int)(STEP(2) * 0.7), 0.5f, 2.2f); }
GEN(f_transform)   { // one slow drag, chopped by the crossfader on sixteenths
  static const char cut[] = "x.xx.x.xx.x.xxx.";
  int n0 = c->n;
  sy_scratch(c, 0, n0, 1.0f, 0.9f);
  for (int i = 0; i < n0; i++) if (cut[(int)(i / (c->bar / 16.0)) % 16] == '.') c->buf[i] = 0;
}
GEN(f_scratch_fill){ sy_scratch(c, 0, BEAT(1), 2.0f, 1.8f); sy_scratch(c, BEAT(1), BEAT(1), 0.5f, 1.2f); for (int k = 8; k < 16; k += 2) sy_scratch(c, STEP(k), STEP(1), 0.5f, 2.6f); }
GEN(f_scratch_swish){ sy_scratch(c, 0, BEAT(1), 1.0f, 2.0f); sy_noise_sweep(c, BEAT(1), BEAT(1), 600, 7000, 2.5f, 0.5f, 1.0f); sy_scratch(c, BEAT(2), BEAT(1), 1.5f, 1.5f); }
GEN(f_rumble)      { // kick, plus its own reverb tail low-passed and ducked under the next hit
  sy_ctx_t wet = *c;
  int total = c->n + SY_TAIL;
  float lp1 = 0, lp2 = 0;
  wet.buf = calloc((size_t)total, sizeof(float));
  sy_drums(c, KIT_TECHNO, "k=x...");
  if (!wet.buf) { fprintf(stderr, "[synth] rumble allocation failed frames=%d\n", total); fflush(stderr); return; }
  memcpy(wet.buf, c->buf, (size_t)total * sizeof(float));
  sy_fx(&wet, X_HALL);
  for (int i = 0; i < total; i++) { lp1 += 0.02f * (wet.buf[i] - c->buf[i] - lp1); lp2 += 0.02f * (lp1 - lp2); wet.buf[i] = lp2; }
  sy_fx(&wet, X_PUMP);
  for (int i = 0; i < total; i++) c->buf[i] += 6.0f * wet.buf[i];
  free(wet.buf);
}

#define DAN GENRE_DANCE
#define HIP GENRE_HIPHOP
#define RAV GENRE_RAVE
#define TEC GENRE_TECHNO
#define ANY GENRE_ANY
#define LOOP (X_ECHO | X_FOLD)          // echo that wraps into the next pass
#define WIDE (X_HALL | X_FOLD)          // hall that wraps into the next pass
// Drum row, note row, note row with vowel words, hand-written row.
#define D(name, cat, genres, bars, kit, fx, lanes)                    { name, cat, genres, bars, kit,  fx, 0,   0,    lanes, NULL,  NULL, NULL }
#define N(name, cat, genres, bars, inst, fx, spb, hold, steps)        { name, cat, genres, bars, inst, fx, spb, hold, steps, NULL,  NULL, NULL }
#define V(name, cat, genres, bars, inst, fx, spb, hold, steps, words) { name, cat, genres, bars, inst, fx, spb, hold, steps, words, NULL, NULL }
#define F(name, cat, genres, bars, fx, fn)                            { name, cat, genres, bars, 0,    fx, 0,   0,    NULL,  NULL,  fn, NULL }
#define M(name, genres, bars, file)                                   { name, CAT_VOX, genres, bars, 0, 0, 0, 0, NULL, NULL, NULL, file }

// Chords of the house key. Everything tonal is in A minor so blocks stack.
#define Am  "A3+C4+E4"
#define F_  "F3+A3+C4"
#define C_  "C4+E4+G4"
#define G_  "G3+B3+D4"
#define Dm  "D4+F4+A4"
#define Em  "E3+G3+B3"
#define Am7 "A3+C4+E4+G4"

static const def_t kDefs[] = {
  // ── Starter library ──
  D("Four Floor",      CAT_DRUMS,   DAN | RAV | TEC, 1, KIT_CLASSIC, 0, "k=x...x...x...x... h=o.o.o.o.o.o.o.o. o=..x...x...x...x. c=....x.......x..."),
  D("Break Beat",      CAT_DRUMS,   HIP | RAV,       1, KIT_CLASSIC, 0, "k=x.....x...x..... s=....x.......x..x h=x.x.x.x.x.x.x.x."),
  D("Hat Groove",      CAT_DRUMS,   ANY,             1, KIT_CLASSIC, 0, "h=xooxooxooxooxoox"),
  D("Snare Fill",      CAT_DRUMS,   ANY,             1, KIT_CLASSIC, 0, "k=x............... s=o.o.o.o.xoxoxxxx"),
  D("Half Time",       CAT_DRUMS,   HIP,             2, KIT_CLASSIC, 0, "k=x.......x.x.....x.......x....... s=........x...............x.....xx h=x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x. o=..............................x."),
  N("Root Pulse",      CAT_BASS,    DAN | TEC,       1, I_BASS,   0, 2, 0.8f, "A1 A1 A1 A1 A1 A1 A1 A1"),
  N("Walking",         CAT_BASS,    HIP,             2, I_BASS,   0, 1, 0.9f, "A1 C2 D2 E2 G1 E2 D2 C2"),
  N("Sub Drone",       CAT_BASS,    HIP | RAV | TEC, 1, I_SUB,    0, 1, 4.0f, "A1 - - -"),
  N("Funk Bass",       CAT_BASS,    DAN | HIP,       1, I_BASS,   0, 4, 1.5f, "A1 - - A1 - - A2 - A1 - - E2 - G1 - -"),
  N("Am - F",          CAT_KEYS,    DAN | HIP,       2, I_PIANO,  0, 1, 3.6f, "A3+C4+E4 - - - F3+A3+C4 - - -"),
  N("C - G",           CAT_KEYS,    DAN | HIP,       2, I_PIANO,  0, 1, 3.6f, "C4+E4+G4 - - - G3+B3+D4 - - -"),
  N("Stabs",           CAT_KEYS,    DAN | RAV,       1, I_PIANO,  0, 2, 1.0f, "A3+C4+E4 - - A3+C4+E4 - - A3+C4+E4 -"),
  N("Arp Am",          CAT_KEYS,    DAN,             1, I_PIANO,  0, 4, 2.0f, "A3 C4 E4 A4 E4 C4 E4 A4 A3 C4 E4 A4 E4 C4 E4 C4"),
  N("Melody",          CAT_KEYS,    DAN | HIP,       2, I_PIANO,  0, 2, 1.6f, "E5 - - D5 C5 - A4 - - - C5 - D5 - E5 -"),
  N("Am Strum",        CAT_GUITAR,  DAN | HIP,       1, I_PLUCK,  0, 2, 3.0f, "A2+E3+A3+C4+E4 - - A2+E3+A3+C4+E4 - A2+E3+A3+C4+E4 - -"),
  N("Palm Mute",       CAT_GUITAR,  DAN | HIP,       1, I_MUTE,   0, 2, 0.8f, "A2 A2 A2 A2 A2 A2 A2 A2"),
  N("Power Riff",      CAT_GUITAR,  HIP | RAV,       2, I_MUTE,   0, 2, 1.5f, "A2+E3 - - A2+E3 - C3+G3 - D3+A3 - - - E3+B3 - D3+A3 - -"),
  N("Pick Arp",        CAT_GUITAR,  DAN | HIP,       1, I_PLUCK,  0, 2, 1.5f, "E3 A3 C4 E4 C4 A3 C4 A3"),
  N("Acid Line",       CAT_SYNTH,   RAV | TEC,       1, I_ACID,   0, 4, 1.4f, "A1 - A2 A1 - A1 C2 - A1 - A2 - G1 - E2 -"),
  N("Pad Am",          CAT_PAD,     ANY,             2, I_PAD,    0, 1, 8.0f, "A3+C4+E4 - - - - - - -"),
  N("Pad F - G",       CAT_PAD,     ANY,             2, I_PAD,    0, 1, 4.0f, "F3+A3+C4 - - - G3+B3+D4 - - -"),
  N("Pluck Arp",       CAT_SYNTH,   DAN | RAV,       1, I_ARP,    0, 4, 1.0f, "A4 C5 E5 C5 A4 C5 E5 C5 G4 B4 D5 B4 G4 B4 D5 B4"),
  F("Riser",           CAT_FX,      ANY,             2, 0, g_riser),
  D("Boom Bap",        CAT_DRUMS,   HIP,             1, KIT_CLASSIC, 0, "k=x.....x...x..... s=....x.......x... h=x.x.x.x.x.x.x.x."),
  D("Disco",           CAT_DRUMS,   DAN,             1, KIT_CLASSIC, 0, "k=x...x...x...x... s=....x.......x... h=x.x.x.x.x.x.x.x. o=..o...o...o...o."),
  D("Trap Hats",       CAT_DRUMS,   HIP,             1, KIT_CLASSIC, 0, "k=x.....x.....x... s=........x....... h=xoxoxxoxoxxoxoxx"),
  D("Clap Beat",       CAT_DRUMS,   DAN | TEC,       1, KIT_CLASSIC, 0, "k=x...x...x...x... h=..x...x...x...x. c=....x.......x..."),
  D("Shuffle",         CAT_DRUMS,   DAN | HIP,       1, KIT_CLASSIC, 0, "k=x.....x.x.....x. s=....x.......x... h=x.oox.oox.oox.oo"),
  D("Kick Rush",       CAT_DRUMS,   RAV | TEC,       1, KIT_CLASSIC, 0, "k=xoxoxoxoxoxoxoxx"),
  D("Sixteenths",      CAT_DRUMS,   DAN | TEC,       1, KIT_CLASSIC, 0, "k=x...x...x...x... s=....x.......x... h=xoxoxoxoxoxoxoxo"),
  D("Breakdown",       CAT_DRUMS,   DAN | RAV | TEC, 2, KIT_CLASSIC, 0, "k=x.......x.......x...x...x.x.x.xx s=..............................xx h=................x.x.x.x.x.x.x.x."),
  D("Clap Fill",       CAT_DRUMS,   DAN | RAV,       1, KIT_CLASSIC, 0, "k=x.......x....... c=........x.x.xxxx"),
  D("Double Kick",     CAT_DRUMS,   RAV | TEC,       1, KIT_CLASSIC, 0, "k=x.x...x.x.x...x. s=....x.......x... h=x.x.x.x.x.x.x.x."),
  N("Octave Jump",     CAT_BASS,    DAN,             1, I_BASS,   0, 2, 0.8f, "A1 A2 A1 A2 A1 A2 A1 A2"),
  N("Slow Roots",      CAT_BASS,    HIP,             2, I_SUB,    0, 1, 3.6f, "A1 - - - F1 - - -"),
  N("Offbeat",         CAT_BASS,    DAN | RAV,       1, I_BASS,   0, 2, 0.8f, "- A1 - A1 - A1 - A1"),
  N("Gallop",          CAT_BASS,    RAV | TEC,       1, I_BASS,   0, 2, 0.7f, "A1 A1 - A1 A1 A1 - A1"),
  N("Climb",           CAT_BASS,    DAN,             1, I_BASS,   0, 2, 0.9f, "A1 B1 C2 D2 E2 D2 C2 B1"),
  N("Dub Sub",         CAT_BASS,    HIP | TEC,       1, I_SUB,    0, 2, 3.5f, "A1 - - - E1 - - -"),
  N("Slap",            CAT_BASS,    DAN | HIP,       1, I_BASS,   0, 4, 1.2f, "A1 - A2 - A1 - A2 A1 - A1 - A2 - G1 - -"),
  N("Fifths",          CAT_BASS,    DAN,             1, I_BASS,   0, 2, 0.9f, "A1 E2 A1 E2 G1 D2 G1 D2"),
  N("Dm - G",          CAT_KEYS,    DAN | HIP,       2, I_PIANO,  0, 1, 3.6f, "D4+F4+A4 - - - G3+B3+D4 - - -"),
  N("F - C",           CAT_KEYS,    DAN | HIP,       2, I_PIANO,  0, 1, 3.6f, "F3+A3+C4 - - - C4+E4+G4 - - -"),
  N("Comping",         CAT_KEYS,    DAN | HIP,       2, I_PIANO,  0, 1, 1.8f, "A3+C4+E4 - A3+C4+E4 - F3+A3+C4 - G3+B3+D4 -"),
  N("Ballad",          CAT_KEYS,    HIP,             1, I_PIANO,  0, 2, 1.8f, "A3 E4 C5 E4 A3 E4 C5 E4"),
  N("Octaves",         CAT_KEYS,    DAN | RAV,       1, I_PIANO,  0, 2, 1.6f, "A3+A4 - A3+A4 - C4+C5 - E4+E5 -"),
  N("Rolling",         CAT_KEYS,    DAN,             1, I_PIANO,  0, 4, 1.5f, "C4 E4 G4 E4 C4 E4 G4 E4 B3 D4 G4 D4 B3 D4 G4 D4"),
  N("Bells",           CAT_KEYS,    DAN | RAV,       1, I_PIANO,  0, 4, 2.5f, "E5 - B5 - G5 - E5 - D5 - - - A5 - - -"),
  N("Low Chords",      CAT_KEYS,    HIP,             2, I_PIANO,  0, 1, 3.6f, "A2+E3+A3 - - - F2+C3+F3 - - -"),
  N("Gospel",          CAT_KEYS,    DAN | HIP,       2, I_PIANO,  0, 1, 3.6f, "C4+E4+A4 - - - D4+F4+A4 - - -"),
  N("Hook",            CAT_KEYS,    DAN,             2, I_PIANO,  0, 2, 1.6f, "A4 - C5 - E5 - C5 - D5 - B4 - G4 - - -"),
  N("Em Strum",        CAT_GUITAR,  DAN | HIP,       1, I_PLUCK,  0, 2, 3.0f, "E2+B2+E3+G3+B3+E4 - - E2+B2+E3+G3+B3+E4 - E2+B2+E3+G3+B3+E4 - -"),
  N("G Strum",         CAT_GUITAR,  DAN | HIP,       1, I_PLUCK,  0, 2, 3.0f, "G2+D3+G3+B3+D4 - - G2+D3+G3+B3+D4 - G2+D3+G3+B3+D4 - -"),
  N("Chug",            CAT_GUITAR,  HIP | RAV,       1, I_MUTE,   0, 4, 0.8f, "E2 E2 - E2 E2 - E2 - E2 E2 - E2 G2 - E2 -"),
  N("Slow Pick",       CAT_GUITAR,  DAN | HIP,       1, I_PLUCK,  0, 2, 2.0f, "A2 E3 A3 C4 E4 C4 A3 E3"),
  N("Riff E",          CAT_GUITAR,  HIP | RAV,       2, I_MUTE,   0, 2, 1.5f, "E2+B2 - - E2+B2 - G2+D3 - A2+E3 - - - B2+F#3 - A2+E3 - -"),
  N("Chop",            CAT_GUITAR,  DAN | HIP,       1, I_MUTE,   0, 2, 0.5f, "- A3+C4+E4 - A3+C4+E4 - A3+C4+E4 - A3+C4+E4"),
  N("Lead Line",       CAT_GUITAR,  DAN | HIP,       1, I_PLUCK,  0, 4, 1.8f, "E4 - G4 - A4 - B4 - D5 - B4 - A4 - G4 -"),
  N("Ring Out",        CAT_GUITAR,  DAN | HIP,       2, I_PLUCK,  0, 1, 7.0f, "A2+E3+A3+C4+E4 - - - - - - -"),
  N("Acid Two",        CAT_SYNTH,   RAV | TEC,       1, I_ACID,   0, 4, 1.4f, "E1 - E2 E1 - E1 G1 - E1 - E2 - D2 - B1 -"),
  N("Pad Dm",          CAT_PAD,     ANY,             2, I_PAD,    0, 1, 8.0f, "D3+F3+A3 - - - - - - -"),
  N("Pad C - G",       CAT_PAD,     ANY,             2, I_PAD,    0, 1, 4.0f, "C3+E3+G3 - - - G3+B3+D4 - - -"),
  N("Pluck Stabs",     CAT_SYNTH,   DAN | RAV,       1, I_ARP,    0, 2, 1.0f, "A3+C4+E4 - - A3+C4+E4 - - A3+C4+E4 -"),
  N("Fast Arp",        CAT_SYNTH,   DAN | RAV,       1, I_ARP,    0, 4, 1.0f, "A4 E5 A5 E5 A4 E5 A5 E5 C5 G5 C5 G5 C5 G5 C5 E5"),
  N("Sub Pulse",       CAT_SYNTH,   HIP | TEC,       1, I_SUB,    0, 1, 0.8f, "A1 A1 A1 A1"),
  N("Acid Climb",      CAT_SYNTH,   RAV | TEC,       1, I_ACID,   0, 4, 1.6f, "A1 - B1 - C2 - D2 - E2 - D2 - C2 - B1 -"),
  N("Arp Down",        CAT_SYNTH,   DAN | RAV,       1, I_ARP,    0, 4, 1.0f, "E5 C5 A4 E4 E5 C5 A4 E4 D5 B4 G4 D4 D5 B4 G4 D4"),
  N("Arp Dm",          CAT_SYNTH,   DAN | RAV,       1, I_ARP,    0, 4, 1.0f, "D4 F4 A4 D5 A4 F4 A4 D5 D4 F4 A4 D5 A4 F4 A4 F4"),
  F("Sweep",           CAT_FX,      ANY,             1, 0, g_sweep),
  D("Dry Floor",       CAT_KICK,    ANY,             1, KIT_CLASSIC, 0, "k=x...x...x...x..."),
  F("Room Pulse",      CAT_KICK,    ANY,             1, 0, g_room_pulse),
  F("Half Kick",       CAT_KICK,    HIP,             1, 0, g_half_kick),
  F("Kick Run",        CAT_KICK,    RAV | TEC,       1, 0, g_kick_run),
  D("Back Snap",       CAT_SNARE,   ANY,             1, KIT_CLASSIC, 0, "s=....x.......x..."),
  D("Rim Tick",        CAT_SNARE,   ANY,             1, KIT_CLASSIC, 0, "s=..x...x...x...x."),
  D("Ghost Notes",     CAT_SNARE,   ANY,             1, KIT_CLASSIC, 0, "s=o.o.x.o.o.o.x.o."),
  D("Snare Run",       CAT_SNARE,   ANY,             1, KIT_CLASSIC, 0, "s=........xoxoxxxx"),
  D("Closed 8ths",     CAT_HAT,     ANY,             1, KIT_CLASSIC, 0, "h=x.x.x.x.x.x.x.x."),
  D("Open Offbeat",    CAT_HAT,     ANY,             1, KIT_CLASSIC, 0, "o=..x...x...x...x."),
  D("Tick 16ths",      CAT_HAT,     ANY,             1, KIT_CLASSIC, 0, "h=xxxxxxxxxxxxxxxx"),
  D("Shuffle Hat",     CAT_HAT,     ANY,             1, KIT_CLASSIC, 0, "h=x.oox.oox.oox.oo"),
  D("Clap Back",       CAT_CLAP,    ANY,             1, KIT_CLASSIC, 0, "c=....x.......x..."),
  F("Clap Stack",      CAT_CLAP,    ANY,             1, 0, g_clap_stack),
  D("Double Clap",     CAT_CLAP,    ANY,             1, KIT_CLASSIC, 0, "c=....xx......xx.."),
  D("Clap Rush",       CAT_CLAP,    ANY,             1, KIT_CLASSIC, 0, "c=........xoxoxxxx"),
  F("Ride 8ths",       CAT_CYMBAL,  ANY,             1, 0, g_ride_8),
  F("Crash Down",      CAT_CYMBAL,  ANY,             1, 0, g_crash_one),
  F("Bell Pattern",    CAT_CYMBAL,  ANY,             1, 0, g_bell_pat),
  F("Splash",          CAT_CYMBAL,  ANY,             1, 0, g_splash),
  D("Shaker 16",       CAT_PERC,    ANY,             1, KIT_CLASSIC, 0, "h=xoxoxoxoxoxoxoxo"),
  F("Tamb 8ths",       CAT_PERC,    ANY,             1, 0, g_tamb_8),
  F("Conga Loop",      CAT_PERC,    ANY,             1, 0, g_conga),
  F("Wood Tick",       CAT_PERC,    ANY,             1, 0, g_wood),
  F("Tom Down",        CAT_FILL,    ANY,             1, 0, g_tom_down),
  D("Snare Build",     CAT_FILL,    ANY,             1, KIT_CLASSIC, 0, "k=x............... s=o.o.o.o.xoxoxxxx"),
  F("Kick Tumble",     CAT_FILL,    RAV | TEC,       1, 0, g_kick_tumble),
  D("Hat Lift",        CAT_FILL,    ANY,             1, KIT_CLASSIC, 0, "k=x............... h=x.x.x.x.xxxxxxxx"),
  F("Zip Up",          CAT_SCRATCH, DAN | HIP,       1, 0, g_zip_up),
  F("Zip Down",        CAT_SCRATCH, DAN | HIP,       1, 0, g_zip_down),
  F("Chop Loop",       CAT_SCRATCH, DAN | HIP,       1, 0, g_chop_loop),
  F("Brake",           CAT_SCRATCH, DAN | HIP,       1, 0, g_brake),
  F("Organ Stab",      CAT_ORGAN,   DAN | HIP,       1, 0, g_org_stab),
  F("Offbeat Organ",   CAT_ORGAN,   DAN | HIP,       1, 0, g_org_off),
  F("Organ Hold",      CAT_ORGAN,   DAN | HIP,       2, 0, g_org_hold),
  F("Fifth Chop",      CAT_ORGAN,   DAN | HIP,       1, 0, g_org_fifth),
  M("Feel It",         ANY,             1, "01_feel_it_140bpm_1bar.mp3"),
  M("So High",         ANY,             1, "02_so_high_140bpm_1bar.mp3"),
  M("Move With Me",    ANY,             1, "03_move_with_me_140bpm_1bar.mp3"),
  M("Light the Night", ANY,             2, "04_light_the_night_140bpm_2bars.mp3"),
  M("Ooh Yeah",        ANY,             2, "05_ooh_yeah_140bpm_2bars.mp3"),
  F("Noise Up",        CAT_FX,      ANY,             2, 0, g_noise_up),
  F("Noise Down",      CAT_FX,      ANY,             2, 0, g_noise_down),
  F("Impact",          CAT_FX,      ANY,             1, 0, g_impact),
  F("Air Bed",         CAT_FX,      ANY,             2, 0, g_air),

  // ── Dance: four-on-the-floor kit, offbeat bass, house piano, trance leads ──
  D("Euro Floor",      CAT_DRUMS,  DAN,         1, KIT_909,    0,               "k=x...x...x...x... h=oo.ooo.ooo.ooo.o o=..x...x...x...x. c=....x.......x..."),
  D("House Party",     CAT_DRUMS,  DAN,         1, KIT_909,    0,               "k=x...x...x...x... c=....x.......x... o=..x...x...x...x. m=xoxoxoxoxoxoxoxo"),
  D("Dream Beat",      CAT_DRUMS,  DAN,         1, KIT_909,    X_ROOM,          "k=x...x...x...x... s=....x.......x... d=x.x.x.x.x.x.x.x."),
  D("Tribal House",    CAT_DRUMS,  DAN | TEC,   1, KIT_909,    0,               "k=x...x...x...x... g=..x..x....x..x.. G=x.....x.x....... m=x.x.x.x.x.x.x.x. c=............x..."),
  D("Italo Drive",     CAT_DRUMS,  DAN,         1, KIT_909,    0,               "k=x...x...x...x... c=....x.......x... o=..x...x...x...x. b=x..x..x...x..x.."),
  D("Handbag",         CAT_DRUMS,  DAN,         1, KIT_909,    0,               "k=x...x...x...x... c=....x.......x... a=xoxoxoxoxoxoxoxo o=..x...x...x...x."),
  D("Piano House",     CAT_DRUMS,  DAN,         1, KIT_909,    0,               "k=x...x...x...x..x c=....x..o....x... h=x.x.x.x.x.x.x.x. o=..x...x...x...x."),
  D("Dance Pop",       CAT_DRUMS,  DAN | HIP,   1, KIT_909,    X_ROOM,          "k=x.....x.x.....x. s=....x.......x... h=x.x.x.x.x.x.x.x."),
  D("Floor Build",     CAT_DRUMS,  DAN | RAV,   2, KIT_909,    0,               "k=x... o=..x. c=....x.......x................... s=................o.o.o.o.xoxoxxRR"),
  D("909 Floor",       CAT_KICK,   DAN | TEC,   1, KIT_909,    0,               "k=x..."),
  D("Euro Kick",       CAT_KICK,   DAN,         1, KIT_909,    0,               "k=x...x...x...x.x."),
  D("Kick Skip",       CAT_KICK,   DAN | TEC,   1, KIT_909,    0,               "k=x...x..xx...x..."),
  D("Kick & Open Hat", CAT_KICK,   DAN | TEC,   1, KIT_909,    0,               "k=x... o=..x."),
  D("Big Room Kick",   CAT_KICK,   DAN | RAV,   1, KIT_909,    X_ROOM,          "k=x..."),
  D("Kick Doubles",    CAT_KICK,   DAN,         1, KIT_909,    0,               "k=x.x.x...x.x.x..."),
  D("909 Backbeat",    CAT_SNARE,  DAN | TEC,   1, KIT_909,    0,               "s=....x.......x..."),
  D("Snare Offs",      CAT_SNARE,  DAN,         1, KIT_909,    0,               "s=....x..o.o..x..o"),
  D("Rim Skip",        CAT_SNARE,  DAN | TEC,   1, KIT_909,    0,               "r=..x..x....x..x.x"),
  D("Snare Sync",      CAT_SNARE,  DAN,         1, KIT_909,    X_ROOM,          "s=....x.....x.x..."),
  D("Offbeat 909",     CAT_HAT,    DAN | TEC,   1, KIT_909,    0,               "o=..x."),
  D("909 Sixteenths",  CAT_HAT,    DAN | TEC,   1, KIT_909,    0,               "h=Xoxo"),
  D("Hat Gallop",      CAT_HAT,    DAN | RAV,   1, KIT_909,    0,               "h=x.xxx.xxx.xxx.xx"),
  D("Ride Drive",      CAT_HAT,    DAN,         1, KIT_909,    0,               "d=x.x.x.x.x.x.x.x."),
  D("Open Close",      CAT_HAT,    DAN,         1, KIT_909,    0,               "h=xx.xxx.xxx.xxx.x o=..x...x...x...x."),
  D("909 Clap",        CAT_CLAP,   DAN | TEC,   1, KIT_909,    0,               "c=....x.......x..."),
  D("Clap Gallop",     CAT_CLAP,   DAN,         1, KIT_909,    0,               "c=....x..x....x.xx"),
  D("Clap Room",       CAT_CLAP,   DAN | RAV,   1, KIT_909,    X_ROOM,          "c=....x.......x..."),
  D("909 Crash",       CAT_CYMBAL, DAN | TEC,   1, KIT_909,    0,               "y=x..............."),
  D("Ride Bell 4s",    CAT_CYMBAL, DAN,         1, KIT_909,    0,               "d=x...x...x...x..."),
  D("Reverse Crash",   CAT_CYMBAL, ANY,         1, KIT_909,    X_REVERSE,       "y=x..............."),
  D("Crash & Ride",    CAT_CYMBAL, DAN,         1, KIT_909,    0,               "y=x............... d=....x...x...x..."),
  D("Cowbell Offs",    CAT_PERC,   DAN,         1, KIT_909,    0,               "b=..x...x...x...x."),
  D("Conga House",     CAT_PERC,   DAN | TEC,   1, KIT_909,    0,               "g=..x..x....x..x.. G=x.....x.x......."),
  D("Tamb 909",        CAT_PERC,   DAN,         1, KIT_909,    0,               "a=xoxoxoxoxoxoxoxo"),
  D("Shaker House",    CAT_PERC,   DAN | TEC,   1, KIT_909,    0,               "m=xoxoxoxoxoxoxoxo"),
  D("Clave Son",       CAT_PERC,   DAN,         1, KIT_909,    0,               "w=x..x..x...x.x..."),
  D("Bongo Chatter",   CAT_PERC,   DAN,         1, KIT_909,    X_ROOM,          "g=x..x..x...x..x.. G=..x...x.x....x.x"),
  D("909 Snare Roll",  CAT_FILL,   DAN | RAV,   1, KIT_909,    0,               "s=o.o.o.o.xoxoxxRR"),
  D("Tom Fill 909",    CAT_FILL,   DAN,         1, KIT_909,    X_ROOM,          "T=x.x............. t=....x.x......... l=........x.x.x.xx"),
  D("Clap Roll",       CAT_FILL,   DAN,         1, KIT_909,    0,               "c=........x.x.xxrr"),
  D("Crash Out",       CAT_FILL,   DAN | RAV,   1, KIT_909,    0,               "k=x...x...x....... s=........x.x.xxxx"),
  N("Euro Offbeat",    CAT_BASS,   DAN,         1, I_DONK,     0,               2, 0.8f, "- A1 - A1 - A1 - A1"),
  N("Euro Octaves",    CAT_BASS,   DAN,         1, I_FMBASS,   0,               4, 0.9f, "A1 - A2 A1 - A2 A1 - A1 - A2 A1 - A2 A1 A2"),
  N("Rolling 16s",     CAT_BASS,   DAN | TEC,   1, I_FMBASS,   0,               4, 0.8f, "A1 A1 A2 A1 A1 A1 A2 A1 A1 A1 A2 A1 G1 G1 G2 G1"),
  N("Organ Bass",      CAT_BASS,   DAN,         1, I_ORGAN,    0,               4, 1.5f, "A1 - - A1 - - A1 - - - C2 - D2 - C2 -"),
  N("Four Chord Bass", CAT_BASS,   DAN,         4, I_DONK,     0,               2, 0.8f, "- A1 - A1 - A1 - A1 - F1 - F1 - F1 - F1 - C2 - C2 - C2 - C2 - G1 - G1 - G1 - G1"),
  N("Pump Bass",       CAT_BASS,   DAN | TEC,   2, I_MOOG,     X_PUMP,          1, 1.0f, "A1 _ _ _ F1 _ _ _"),
  N("Italo Bass",      CAT_BASS,   DAN,         1, I_SQUARE,   0,               4, 0.7f, "A1 A2 A1 A2 A1 A2 A1 A2 F1 F2 F1 F2 G1 G2 G1 G2"),
  N("House Walk",      CAT_BASS,   DAN,         1, I_FMBASS,   0,               4, 1.0f, "A1 - - C2 - - D2 - E2 - - D2 - C2 - -"),
  N("Deep House Sub",  CAT_BASS,   DAN | TEC,   1, I_SUB,      0,               4, 2.5f, "A1 - - - - - A1 - - - G1 - - - - -"),
  N("Bounce Bass",     CAT_BASS,   DAN,         1, I_DONK,     0,               4, 0.8f, "A1 - A1 A2 - A1 - A2 A1 - A1 A2 - G1 G2 -"),
  N("House Piano Am",  CAT_KEYS,   DAN,         2, I_HPIANO,   0,               4, 1.6f, Am " - - " Am " - - " Am " - - - " Am " - " Am " - - - " F_ " - - " F_ " - - " F_ " - - - " F_ " - " F_ " - - -"),
  N("House Piano C",   CAT_KEYS,   DAN,         2, I_HPIANO,   0,               4, 1.6f, C_ " - - " C_ " - - " C_ " - - - " C_ " - " C_ " - - - " G_ " - - " G_ " - - " G_ " - - - " G_ " - " G_ " - - -"),
  N("Piano Anthem",    CAT_KEYS,   DAN | RAV,   4, I_HPIANO,   X_ROOM,          2, 1.5f, Am " - " Am " " Am " - " Am " " Am " - " G_ " - " G_ " " G_ " - " G_ " " G_ " - " F_ " - " F_ " " F_ " - " F_ " " F_ " - E3+G#3+B3 - E3+G#3+B3 E3+G#3+B3 - E3+G#3+B3 E3+G#3+B3 -"),
  N("Piano Bounce",    CAT_KEYS,   DAN,         1, I_HPIANO,   0,               4, 1.0f, Am " - " Am " - - " Am " - - " G_ " - " G_ " - - " G_ " - -"),
  N("Euro Piano Hook", CAT_KEYS,   DAN,         2, I_HPIANO,   X_ROOM,          2, 1.2f, "E5+E4 - D5+D4 C5+C4 - A4+A3 - C5+C4 D5+D4 - E5+E4 - G5+G4 E5+E4 - -"),
  N("Organ Riff",      CAT_ORGAN,  DAN,         1, I_ORGAN,    0,               4, 0.9f, "A3 - C4 A3 - E4 - A3 D4 - C4 - A3 - G3 -"),
  N("Organ Offs",      CAT_ORGAN,  DAN,         1, I_ORGAN,    0,               2, 0.6f, "- " Am " - " Am " - " F_ " - " G_),
  N("Organ House",     CAT_ORGAN,  DAN,         2, I_ORGAN,    X_ROOM,          4, 1.2f, Am " - - " Am " - - " Am " - " Am " - - " Am " - - - - " Dm " - - " Dm " - - " Dm " - " Em " - - " Em " - - - -"),
  N("Trance Arp",      CAT_SYNTH,  DAN | RAV,   1, I_SAW,      LOOP,            4, 0.6f, "A3 C4 E4 A4 C4 E4 A4 C5 E4 A4 C5 E5 C5 A4 E4 C4"),
  N("Trance Arp F-G",  CAT_SYNTH,  DAN | RAV,   2, I_SAW,      LOOP,            4, 0.6f, "F3 A3 C4 F4 A3 C4 F4 A4 C4 F4 A4 C5 A4 F4 C4 A3 G3 B3 D4 G4 B3 D4 G4 B4 D4 G4 B4 D5 B4 G4 D4 B3"),
  N("Dream Lead",      CAT_SYNTH,  DAN,         2, I_SUPERSAW, LOOP,            2, 1.4f, "E5 - - D5 C5 - D5 - E5 - G5 - E5 - - -"),
  N("Pizzicato Hook",  CAT_SYNTH,  DAN,         2, I_PIZZ,     X_ROOM,          4, 0.8f, "A4 - A4 C5 - A4 - E5 - D5 - C5 - A4 - G4 A4 - A4 C5 - A4 - G5 - E5 - D5 - C5 - -"),
  N("Euro Lead",       CAT_SYNTH,  DAN,         2, I_SAW,      X_ECHO,          2, 0.9f, "A4 A4 C5 A4 D5 A4 E5 D5 C5 C5 E5 C5 G5 E5 D5 C5"),
  N("Bell Hook",       CAT_SYNTH,  DAN,         2, I_BELL,     WIDE,            2, 1.0f, "E5 - C5 - A4 - C5 E5 D5 - B4 - G4 - B4 -"),
  N("Square Riff",     CAT_SYNTH,  DAN | TEC,   1, I_SQUARE,   0,               4, 0.7f, "A3 - A3 C4 - A3 E4 - D4 - C4 - A3 G3 - -"),
  N("Gated Saws",      CAT_SYNTH,  DAN | RAV,   2, I_SUPERSAW, X_GATE,          1, 1.0f, Am " _ _ _ " F_ " _ _ _"),
  N("Supersaw Chords", CAT_SYNTH,  DAN,         4, I_SUPERSAW, X_PUMP,          1, 1.0f, Am " _ _ _ " F_ " _ _ _ " C_ " _ _ _ " G_ " _ _ _"),
  N("Sync Seq",        CAT_SYNTH,  DAN | TEC,   1, I_FMSEQ,    0,               4, 0.6f, "A2 A3 A2 A2 A3 A2 A2 A3 A2 A2 A3 A2 G2 G3 G2 G3"),
  N("String Layer Am", CAT_PAD,    DAN | HIP,   2, I_STRINGS,  X_CHORUS,        1, 1.0f, Am " _ _ _ " F_ " _ _ _"),
  N("String Layer C",  CAT_PAD,    DAN | HIP,   2, I_STRINGS,  X_CHORUS,        1, 1.0f, C_ " _ _ _ " G_ " _ _ _"),
  V("Choir Aah",       CAT_PAD,    DAN | RAV,   2, I_CHOIR,    WIDE,            1, 1.0f, Am " _ _ _ _ _ _ _", "a"),
  V("Choir Ooh",       CAT_PAD,    DAN | TEC,   2, I_CHOIR,    WIDE,            1, 1.0f, "D3+F3+A3 _ _ _ " Em " _ _ _", "u"),
  N("High Strings",    CAT_PAD,    DAN | RAV,   2, I_STRINGS,  WIDE,            1, 1.0f, "A4+E5 _ _ _ G4+D5 _ _ _"),
  N("Pumping Pad",     CAT_PAD,    DAN | TEC,   2, I_STRINGS,  X_PUMP,          1, 1.0f, Am7 " _ _ _ _ _ _ _"),
  N("Dance Stab",      CAT_STAB,   DAN | RAV,   1, I_STAB,     X_ROOM,          4, 1.0f, Am " - - " Am " - - " Am " - - - - - " Am " - - -"),
  N("Offbeat Stabs",   CAT_STAB,   DAN,         1, I_STAB,     0,               2, 0.5f, "- " Am " - " Am " - " F_ " - " G_),
  N("Brass Hits",      CAT_STAB,   DAN | HIP,   1, I_BRASS,    X_ROOM,          4, 1.5f, "A3+E4+A4 - - - - - A3+E4+A4 - A3+E4+A4 - - - G3+D4+G4 - - -"),
  N("Orch Hit",        CAT_STAB,   DAN | RAV | TEC, 1, I_STAB, X_ROOM | X_DRIVE, 4, 2.0f, "A2+A3+C4+E4+A4 - - - - - - - - - A2+A3+C4+E4+A4 - - - - -"),
  N("Piano Stab",      CAT_STAB,   DAN,         1, I_HPIANO,   X_ROOM,          4, 3.0f, "A2+A3+C4+E4 - - - - - - - G2+G3+B3+D4 - - - - - - -"),
  N("Funky Wah",       CAT_GUITAR, DAN | HIP,   1, I_WAH,      0,               4, 0.8f, Am " - " Am " " Am " - " Am " - " Am " - " Am " - " Am " " Am " - " Am " -"),
  N("Disco Chops",     CAT_GUITAR, DAN,         1, I_CLEAN,    X_CHORUS,        4, 0.5f, "- - " Am " - - - " Am " " Am " - - " Am " - - " G_ " " G_ " -"),
  N("Space Guitar",    CAT_GUITAR, DAN,         2, I_CLEAN,    LOOP,            2, 1.5f, "A3 E4 A4 - C5 - A4 E4 G3 D4 G4 - B4 - G4 D4"),
  F("Uplifter",        CAT_FX,     ANY,         2, X_ROOM,     f_uplifter),
  F("Downlifter",      CAT_FX,     ANY,         2, X_ROOM,     f_downlifter),
  D("Laser Zaps",      CAT_FX,     DAN | RAV | TEC, 1, KIT_909, LOOP,           "z=x.....x...x....."),
  F("Sub Drop",        CAT_FX,     ANY,         1, 0,          f_sub_drop),
  F("White Swell",     CAT_FX,     DAN | TEC,   2, X_PUMP,     f_white_swell),
  N("Ping Echo",       CAT_FX,     DAN | RAV | TEC, 1, I_BLEEP, X_ECHO | X_HALL | X_FOLD, 4, 1.0f, "A5 - - - - - - - - - - - - - - -"),

  // ── Hip Hop: 808 and break kits with swing, sub bass, Rhodes, wah guitar, scratches ──
  D("Head Nod",        CAT_DRUMS,  HIP,         1, KIT_BREAK,  X_SWING | X_ROOM, "k=x......x..x..... s=....x.......x... h=x.x.x.x.x.x.x.x."),
  D("Dusty Break",     CAT_DRUMS,  HIP,         1, KIT_LOFI,   X_SWING | X_CRUSH, "k=x.x.......xx.... s=....x..o.o..x..o h=x.x.x.x.x.x.x.x."),
  D("808 Bounce",      CAT_DRUMS,  HIP,         1, KIT_808,    0,               "k=x..x..x...x..x.. c=....x.......x... h=x.x.x.rx.x.x.xrx"),
  D("Laid Back",       CAT_DRUMS,  HIP,         1, KIT_BREAK,  X_SWING,         "k=x.........x.x... s=....x.......x... h=x.xxx.x.x.xxx.x. m=..o...o...o...o."),
  D("G-Funk Beat",     CAT_DRUMS,  HIP,         1, KIT_808,    X_SWING,         "k=x..x....x.x..... c=....x.......x... o=..x...x...x...x. h=xx.xxx.xxx.xxx.x"),
  D("Slow Jam",        CAT_DRUMS,  HIP,         1, KIT_808,    X_ROOM,          "k=x.......x.x..... r=....x.......x... m=x.x.x.x.x.x.x.x. h=..x...x...x...x."),
  D("Backyard Funk",   CAT_DRUMS,  HIP,         1, KIT_BREAK,  X_SWING | X_ROOM, "k=x.x...x...x..x.. s=....x..o.o..x..o h=xoxoxoxoxoxoxoxo"),
  D("Hat Roll Beat",   CAT_DRUMS,  HIP,         1, KIT_808,    0,               "k=x.....x..x...... c=........x....... h=x.x.rrx.x.x.RRrx"),
  D("Old School",      CAT_DRUMS,  HIP,         1, KIT_808,    0,               "k=x..x..x.....x... s=....x.......x... c=....x.......x... b=x.x.x.x.x.x.x.x."),
  D("Jeep Beat",       CAT_DRUMS,  HIP,         2, KIT_BREAK,  X_SWING | X_ROOM, "k=x......x..x.....x.x....x..x..... s=....x.......x.......x.......x.xx h=x.x."),
  D("808 Boom",        CAT_KICK,   HIP,         1, KIT_808,    0,               "k=x.........x....."),
  D("808 Skip",        CAT_KICK,   HIP,         1, KIT_808,    0,               "k=x..x......x..x.."),
  D("Dusty Kick",      CAT_KICK,   HIP,         1, KIT_LOFI,   X_CRUSH,         "k=x......x..x....."),
  D("Boom Tail",       CAT_KICK,   HIP | TEC,   1, KIT_808,    X_ROOM,          "k=x..............."),
  D("Fat Backbeat",    CAT_SNARE,  HIP,         1, KIT_BREAK,  X_ROOM,          "s=....x.......x..."),
  D("Rim 2 & 4",       CAT_SNARE,  HIP,         1, KIT_808,    0,               "r=....x.......x..."),
  D("Snare Drag",      CAT_SNARE,  HIP,         1, KIT_BREAK,  X_SWING,         "s=....x..o.o..x.rr"),
  D("808 Snare",       CAT_SNARE,  HIP | TEC,   1, KIT_808,    0,               "s=....x.......x..x"),
  D("Swing Hats",      CAT_HAT,    HIP,         1, KIT_BREAK,  X_SWING,         "h=x.xox.xox.xox.xo"),
  D("808 Hat Rolls",   CAT_HAT,    HIP,         1, KIT_808,    0,               "h=x.x.x.rxx.x.RRrx"),
  D("Lazy Open",       CAT_HAT,    HIP,         1, KIT_808,    X_SWING,         "o=..x.......x..... h=x...x.x.x...x.x."),
  D("Dusty Hats",      CAT_HAT,    HIP,         1, KIT_LOFI,   X_SWING | X_CRUSH, "h=xoxoxoxoxoxoxoxo"),
  D("808 Clap",        CAT_CLAP,   HIP | TEC,   1, KIT_808,    0,               "c=....x.......x..."),
  D("Lazy Clap",       CAT_CLAP,   HIP,         1, KIT_LOFI,   X_SWING | X_ROOM, "c=....x......xx..."),
  D("Dusty Crash",     CAT_CYMBAL, HIP,         1, KIT_LOFI,   X_CRUSH,         "y=x..............."),
  D("Ride Swing",      CAT_CYMBAL, HIP,         1, KIT_BREAK,  X_SWING,         "d=x...x..xx...x..x"),
  D("Cowbell 808",     CAT_PERC,   HIP,         1, KIT_808,    0,               "b=x..x..x..x..x.x."),
  D("Lazy Tamb",       CAT_PERC,   HIP,         1, KIT_BREAK,  X_SWING,         "a=..x...x...x...x."),
  D("Street Conga",    CAT_PERC,   HIP,         1, KIT_BREAK,  X_SWING | X_ROOM, "g=..x..x.x..x..x.. G=x.....x.....x..."),
  D("Shaker Swing",    CAT_PERC,   HIP,         1, KIT_LOFI,   X_SWING,         "m=xoxoxoxoxoxoxoxo"),
  D("Clave 808",       CAT_PERC,   HIP,         1, KIT_808,    0,               "w=x..x...x..x.x..."),
  D("Can Tap",         CAT_PERC,   HIP,         1, KIT_LOFI,   X_CRUSH,         "w=x.xx.x.xx.xx.x.x r=....x.......x..."),
  D("808 Tom Roll",    CAT_FILL,   HIP,         1, KIT_808,    0,               "T=x.x..x.......... t=......x.x..x.... l=............x.xx"),
  D("Snare Stutter",   CAT_FILL,   HIP,         1, KIT_BREAK,  X_ROOM,          "s=....x..x.x.xxxrR"),
  D("Kick Stumble",    CAT_FILL,   HIP,         1, KIT_808,    0,               "k=x.....x.x..x.xxx"),
  N("808 Sub Line",    CAT_BASS,   HIP,         2, I_SUB,      0,               4, 1.0f, "A1 _ _ _ _ _ - - A1 _ - - C2 _ _ - G1 _ _ _ _ _ - - G1 _ - - E1 _ _ -"),
  N("Fuzz Bass",       CAT_BASS,   HIP,         1, I_MOOG,     X_DRIVE | X_SWING, 4, 0.9f, "A1 - - A1 - - C2 - A1 - - - G1 - A1 -"),
  N("Dub Town",        CAT_BASS,   HIP,         1, I_SUB,      0,               2, 1.5f, "A1 - - - - E1 - G1"),
  N("Moog Funk",       CAT_BASS,   HIP,         1, I_MOOG,     X_SWING,         4, 0.8f, "A1 - A2 - - A1 - C2! - A1 - - G1 A1~ - -"),
  N("Upright Walk",    CAT_BASS,   HIP,         2, I_UPRIGHT,  X_ROOM,          1, 0.9f, "A1 C2 E2 G2 F2 D2 E2 E1"),
  N("G-Funk Bass",     CAT_BASS,   HIP,         2, I_MOOG,     0,               4, 1.2f, "A1 _ - - - - A1 - C2 _ - - D2 - E2 - A1 _ - - - - A1 - G1 _ - - E1 - G1 -"),
  N("Lazy Sub",        CAT_BASS,   HIP,         1, I_SUB,      X_SWING,         4, 1.5f, "A1 _ _ - - - - A1 - - A1 - - - G1 -"),
  N("Octave Funk",     CAT_BASS,   HIP | DAN,   1, I_FMBASS,   X_SWING,         4, 0.7f, "A1 - A2 A1 - - A2 - A1 - A2 A1 - G1 G2 -"),
  N("Jazz Hop Bass",   CAT_BASS,   HIP,         1, I_UPRIGHT,  X_SWING,         4, 1.2f, "A1 - - C2 - - E2 - D2 - - C2 - A1 - -"),
  N("Rhodes Sevenths", CAT_KEYS,   HIP,         2, I_EPIANO,   X_CHORUS,        2, 3.0f, Am7 " - - - - - " Am7 " - D3+F3+A3+C4 - - - - - D3+F3+A3+C4 -"),
  N("Rhodes Stabs",    CAT_KEYS,   HIP,         1, I_EPIANO,   X_SWING,         4, 1.0f, "- - " Am7 " - - - - " Am7 " - - G3+B3+D4+F4 - - - - -"),
  N("EP Lick",         CAT_KEYS,   HIP,         2, I_EPIANO,   LOOP | X_SWING,  4, 1.0f, "E4 - G4 A4 - - C5 - A4 - G4 - E4 - - - D4 - E4 G4 - - A4 - G4 - E4 - D4 C4 - -"),
  N("Night Vibes",     CAT_KEYS,   HIP,         2, I_VIBES,    X_ROOM,          2, 1.5f, "A4 - C5 E5 - D5 - C5 A4 - G4 - E4 - G4 -"),
  N("Lo-Fi Keys",      CAT_KEYS,   HIP,         2, I_EPIANO,   X_CRUSH | X_CHORUS, 2, 3.0f, "F3+A3+C4+E4 - - - E3+G3+B3+D4 - - - D3+F3+A3+C4 - - - E3+G3+B3 - - -"),
  N("Organ Church",    CAT_ORGAN,  HIP,         2, I_ORGAN,    WIDE,            1, 1.0f, "A2+A3+C4+E4 _ _ _ F2+F3+A3+C4 _ G2+G3+B3+D4 _"),
  N("Organ Cool",      CAT_ORGAN,  HIP,         1, I_ORGAN,    X_SWING,         4, 0.9f, "A3 - C4 D4 - E4 - - G4 - E4 - D4 C4 - A3"),
  N("Organ Nervous",   CAT_ORGAN,  HIP | RAV,   1, I_ORGAN,    0,               4, 0.5f, "A3+C4 A3+C4 - A3+C4 - A3+C4 A3+C4 - G3+B3 G3+B3 - G3+B3 - G3+B3 - -"),
  N("Wah Flow",        CAT_GUITAR, HIP,         1, I_WAH,      X_SWING,         4, 0.7f, Am7 " - - " Am7 " - " Am7 " - - " Am7 " - " Am7 " - - " Dm " - -"),
  N("Rasta Skank",     CAT_GUITAR, HIP | DAN,   1, I_CLEAN,    0,               2, 0.4f, "- " Am " - " Am " - " G_ " - " G_),
  N("Desert Flow",     CAT_GUITAR, HIP,         2, I_CLEAN,    LOOP,            2, 2.0f, "A2 E3 A3 C4 E4 C4 A3 E3 F2 C3 F3 A3 C4 A3 F3 C3"),
  N("Slow Jam Lick",   CAT_GUITAR, HIP,         2, I_PLUCK,    WIDE,            2, 1.5f, "E4 - G4 A4 - - C5 A4 G4 - E4 - D4 E4 - -"),
  N("Crunch Riff",     CAT_GUITAR, HIP | RAV,   1, I_DIST,     0,               4, 1.5f, "A2+E3 - - A2+E3 - - C3+G3 - D3+A3 - - D3+A3 - C3+G3 - -"),
  N("Sunset Chords",   CAT_GUITAR, HIP | DAN,   2, I_PLUCK,    X_CHORUS,        1, 3.5f, "A2+E3+A3+C4+E4 - - - F2+C3+F3+A3+C4 - - -"),
  N("G Whistle",       CAT_SYNTH,  HIP,         2, I_WHISTLE,  X_ROOM,          2, 1.0f, "E5 _ _ D5~ E5~ _ G5~ _ E5~ _ _ D5~ C5~ _ A4~ _"),
  N("Synth Brass",     CAT_SYNTH,  HIP,         1, I_BRASS,    X_SWING,         4, 1.0f, "A3 - - C4 - - D4 - E4 - - D4 - C4 - -"),
  N("Vibes Arp",       CAT_SYNTH,  HIP,         1, I_VIBES,    X_SWING,         4, 1.5f, "A3 C4 E4 G4 E4 C4 E4 G4 A4 G4 E4 C4 E4 C4 A3 G3"),
  V("Talk Lead",       CAT_SYNTH,  HIP,         2, I_VOX,      X_ROOM,          2, 1.0f, "A3 - C4 - D4 _ C4 - A3 - G3 - A3 _ - -", "ou uo au"),
  V("Air Pad",         CAT_PAD,    HIP | TEC,   2, I_CHOIR,    WIDE,            1, 1.0f, "A3+E4 _ _ _ _ _ _ _", "u"),
  N("Low Strings",     CAT_PAD,    HIP | TEC,   2, I_STRINGS,  WIDE,            1, 1.0f, "D2+A2+D3+F3 _ _ _ _ _ _ _"),
  N("Sweet Strings",   CAT_PAD,    HIP | DAN,   2, I_STRINGS,  WIDE,            1, 1.0f, "A3+E4+A4 _ _ _ G3+D4+G4 _ F3+C4+F4 _"),
  N("Rhodes Bed",      CAT_PAD,    HIP,         2, I_EPIANO,   X_CHORUS | WIDE, 1, 1.0f, "A2+E3+G3+C4 _ _ _ _ _ _ _"),
  N("Wave Horns",      CAT_STAB,   HIP,         1, I_BRASS,    X_ROOM | X_SWING, 4, 1.2f, Am " - - - - - - " Am " - " G_ " - - - - - -"),
  N("Lo-Fi Orch",      CAT_STAB,   HIP,         1, I_STAB,     X_CRUSH | X_ROOM, 4, 2.0f, "A2+A3+C4+E4 - - - - - - - - - - - - - G2+G3+B3+D4 -"),
  N("Dusty Piano",     CAT_STAB,   HIP,         1, I_PIANO,    X_CRUSH,         4, 3.0f, "A2+A3+C4+E4 - - - - - - A2+A3+C4+E4 - - - - - - - -"),
  F("Baby Scratch",    CAT_SCRATCH, HIP,        1, 0,          f_baby),
  F("Chirp Scratch",   CAT_SCRATCH, HIP,        1, 0,          f_chirp),
  F("Transformer",     CAT_SCRATCH, HIP,        1, 0,          f_transform),
  F("Scratch Fill",    CAT_SCRATCH, HIP,        1, 0,          f_scratch_fill),
  F("Scratch & Swish", CAT_SCRATCH, HIP | DAN,  1, 0,          f_scratch_swish),
  F("Rewind",          CAT_SCRATCH, HIP | RAV,  1, 0,          f_rewind),
  F("Vinyl Stop",      CAT_FX,     HIP | DAN,   1, 0,          f_vinyl_stop),
  F("Dub Siren",       CAT_FX,     HIP | RAV,   1, LOOP,       f_dub_siren),
  F("Radio Static",    CAT_FX,     HIP | TEC,   2, 0,          f_static),
  F("Boom Drop",       CAT_FX,     HIP | RAV,   1, 0,          f_boom_drop),

  // ── Rave: overdriven kit, breakbeats, hoovers, stabs, rave piano ──
  D("Hardcore Floor",  CAT_DRUMS,  RAV,         1, KIT_HARD,   0,               "k=x...x...x...x... o=..x...x...x...x. c=....x.......x..."),
  D("Breakbeat Rush",  CAT_DRUMS,  RAV,         1, KIT_BREAK,  X_ROOM,          "k=x.x.......xx.... s=....x..x.x..x... h=x.x.x.x.x.x.x.x."),
  D("Happy Break",     CAT_DRUMS,  RAV,         1, KIT_HARD,   0,               "k=x...x...x...x... s=....x..o.o..x.o. h=xoxoxoxoxoxoxoxo"),
  D("Jungle Chop",     CAT_DRUMS,  RAV,         2, KIT_BREAK,  X_ROOM,          "k=x.x.......x.....x.x......xx..... s=....x..x.x..x.......x..x.x.xx.x. h=x.x."),
  D("Gabber Stomp",    CAT_DRUMS,  RAV,         1, KIT_HARD,   X_DRIVE,         "k=x...x...x...x... d=x.x.x.x.x.x.x.x."),
  D("Stomp Clap",      CAT_DRUMS,  RAV,         1, KIT_HARD,   0,               "k=x...x...x...x... c=x...x...x...x... o=..x...x...x...x."),
  D("Rolling Break",   CAT_DRUMS,  RAV,         1, KIT_BREAK,  0,               "k=x..x..x...x..x.. s=....x.......x..x d=x.x.x.x.x.x.x.x."),
  D("Amen Style",      CAT_DRUMS,  RAV | HIP,   1, KIT_BREAK,  X_ROOM | X_DRIVE, "k=x.x.......xx.... s=....x..o.o..x..o d=x.x.x.x.x.x.x.x."),
  D("Rave Build",      CAT_DRUMS,  RAV,         2, KIT_HARD,   0,               "k=x... s=........x...x...x.x.x.x.xxxxxxRR y=x..............................."),
  D("Hard Kick 4s",    CAT_KICK,   RAV,         1, KIT_HARD,   0,               "k=x..."),
  D("Gabber Kick",     CAT_KICK,   RAV,         1, KIT_HARD,   X_DRIVE,         "k=x..."),
  D("Hard Gallop",     CAT_KICK,   RAV,         1, KIT_HARD,   0,               "k=x.x.x..xx.x.x..x"),
  D("Kick Roll Up",    CAT_KICK,   RAV | TEC,   1, KIT_HARD,   0,               "k=x...x...x.x.xxRR"),
  D("Break Snare",     CAT_SNARE,  RAV,         1, KIT_BREAK,  X_ROOM,          "s=....x..x.x..x..."),
  D("Snare Rush",      CAT_SNARE,  RAV,         1, KIT_HARD,   0,               "s=Xoxo"),
  D("Clap Snare",      CAT_SNARE,  RAV,         1, KIT_HARD,   0,               "s=....x.......x... c=....x.......x..."),
  D("Rave Open Offs",  CAT_HAT,    RAV,         1, KIT_HARD,   0,               "o=..x."),
  D("Shaker Hats",     CAT_HAT,    RAV,         1, KIT_HARD,   0,               "h=xoxo m=x.x."),
  D("Rave Ride",       CAT_HAT,    RAV,         1, KIT_HARD,   0,               "d=Xoxo"),
  D("Rave Clap",       CAT_CLAP,   RAV,         1, KIT_HARD,   X_ROOM,          "c=....x.......x..."),
  D("Clap On Beat",    CAT_CLAP,   RAV,         1, KIT_HARD,   0,               "c=x..."),
  D("Big Crash",       CAT_CYMBAL, RAV | TEC,   2, KIT_HARD,   X_HALL,          "y=x..............................."),
  D("China Hits",      CAT_CYMBAL, RAV,         1, KIT_HARD,   X_DRIVE,         "y=x.......x......."),
  D("Rave Bell",       CAT_CYMBAL, RAV,         1, KIT_HARD,   0,               "d=x..x..x.x..x..x."),
  D("Fast Congas",     CAT_PERC,   RAV,         1, KIT_BREAK,  0,               "g=x.xx.x.xx.xx.x.x G=..x...x.....x..."),
  N("Samba Whistle",   CAT_PERC,   RAV,         1, I_WHISTLE,  X_ROOM,          4, 0.5f, "A6 - A6 A6 - A6 - A6 A6 - A6 A6 - A6 - -"),
  D("Rave Tamb",       CAT_PERC,   RAV,         1, KIT_HARD,   0,               "a=x.xxx.xxx.xxx.xx"),
  D("Rave Cowbell",    CAT_PERC,   RAV,         1, KIT_HARD,   0,               "b=x.x.xx.x.x.xx.x."),
  D("Knock Knock",     CAT_PERC,   RAV | TEC,   1, KIT_HARD,   X_ROOM,          "w=x..x....x..x.... r=......x.......x."),
  D("Long Snare Fill", CAT_FILL,   RAV | DAN,   2, KIT_HARD,   0,               "s=o...o...o...o...o.o.o.o.xxxxxxRR"),
  D("Amen Fill",       CAT_FILL,   RAV,         1, KIT_BREAK,  X_ROOM,          "k=x.....x......... s=..x.x..x.xx.xxrr"),
  D("Tom Rush",        CAT_FILL,   RAV,         1, KIT_HARD,   0,               "T=xx..xx.......... t=....xx..xx...... l=........xx..xxxx"),
  D("Reverse Fill",    CAT_FILL,   RAV | TEC,   1, KIT_HARD,   X_REVERSE,       "s=xxxxx.x.x...x..."),
  N("Hard Offbeat",    CAT_BASS,   RAV,         1, I_DONK,     X_DRIVE,         2, 0.8f, "- A1 - A1 - A1 - C2"),
  N("Bang Bass",       CAT_BASS,   RAV,         1, I_FMBASS,   X_DRIVE,         4, 0.6f, "A1 A1 - A1 A1 - A1 - A1 A1 - A1 A1 - G1 G1"),
  N("Hoover Bass",     CAT_BASS,   RAV,         1, I_HOOVER,   0,               2, 1.8f, "A1 - - A1 - G1~ - -"),
  N("Reese Roll",      CAT_BASS,   RAV | TEC,   2, I_REESE,    X_DRIVE,         1, 1.0f, "A1 _ _ _ G1 _ F1 _"),
  N("Jungle Sub",      CAT_BASS,   RAV | HIP,   1, I_SUB,      0,               4, 2.0f, "A1 _ - - - - A1 - - - C2 _ - - G1 -"),
  N("Acid Rave",       CAT_BASS,   RAV | TEC,   1, I_ACID,     X_DRIVE,         4, 1.0f, "A1 A2 A1! - A1 C2~ A1 - A1! A2 - A1 G1 A1~ C2! -"),
  N("Chirp Bass",      CAT_BASS,   RAV,         1, I_SQUARE,   0,               4, 0.5f, "A2 - A2 A3 - A2 - A3 A2 - A2 A3 - G2 G3 -"),
  N("Stomp Bass",      CAT_BASS,   RAV,         1, I_MOOG,     X_DRIVE,         4, 0.9f, "A1 - - - A1 - - - A1 - - - A1 - A2 -"),
  N("Rave Piano Am",   CAT_KEYS,   RAV,         2, I_HPIANO,   0,               4, 0.9f, Am " - " Am " " Am " - " Am " " Am " - " Am " - " Am " " Am " - " Am " " Am " - " F_ " - " F_ " " F_ " - " F_ " " F_ " - " F_ " - " F_ " " F_ " - " F_ " " F_ " -"),
  N("Rave Piano C",    CAT_KEYS,   RAV,         2, I_HPIANO,   0,               4, 0.9f, C_ " - " C_ " " C_ " - " C_ " " C_ " - " C_ " - " C_ " " C_ " - " C_ " " C_ " - " G_ " - " G_ " " G_ " - " G_ " " G_ " - " G_ " - " G_ " " G_ " - " G_ " " G_ " -"),
  N("Happy Piano",     CAT_KEYS,   RAV,         2, I_HPIANO,   X_ROOM,          4, 0.8f, "E5+E4 - E5+E4 D5+D4 - C5+C4 - D5+D4 E5+E4 - G5+G4 - E5+E4 - - - D5+D4 - D5+D4 C5+C4 - B4+B3 - C5+C4 D5+D4 - E5+E4 - C5+C4 - - -"),
  N("Tek Piano",       CAT_KEYS,   RAV | TEC,   1, I_HPIANO,   0,               4, 0.4f, Am " " Am " - " Am " " Am " - " Am " - " G_ " " G_ " - " G_ " " G_ " - " G_ " -"),
  N("Rave Organ",      CAT_ORGAN,  RAV,         1, I_ORGAN,    X_DRIVE,         4, 0.8f, "A3+E4 - A3+E4 - C4+G4 - A3+E4 - D4+A4 - C4+G4 - A3+E4 - G3+D4 -"),
  N("Hoover Riff",     CAT_SYNTH,  RAV,         2, I_HOOVER,   X_ROOM,          4, 1.5f, "A3 - - A3 - - C4~ - A3 - - - G3 - A3~ - A3 - - A3 - - D4~ - C4~ - - - A3 - G3~ -"),
  N("Hoover Rise",     CAT_SYNTH,  RAV,         2, I_HOOVER,   WIDE,            1, 1.0f, "A2 _ _ _ A3~ _ _ _"),
  N("Minor Stab Riff", CAT_SYNTH,  RAV,         1, I_STAB,     X_ROOM,          4, 0.8f, Am " - " Am " - - " Am " - - " C_ " - " C_ " - - B3+D4+G4 - -"),
  N("Parade Up",       CAT_SYNTH,  RAV | DAN,   1, I_SUPERSAW, 0,               4, 0.7f, "A3 C4 E4 A4 C5 E5 A5 E5 A3 C4 E4 A4 C5 E5 A5 C6"),
  N("Parade Down",     CAT_SYNTH,  RAV | DAN,   1, I_SUPERSAW, 0,               4, 0.7f, "A5 E5 C5 A4 E4 C4 A3 C4 G5 D5 B4 G4 D4 B3 G3 B3"),
  N("Hyper Seq",       CAT_SYNTH,  RAV | TEC,   1, I_SAW,      X_DRIVE,         4, 0.5f, "A3 A3 A4 A3 A3 A4 A3 A3 A4 A3 A3 A4 G3 G4 G3 G4"),
  N("Pointed Seq",     CAT_SYNTH,  RAV,         1, I_SQUARE,   LOOP,            4, 0.4f, "A4 - E5 - A4 - E5 A5 - E5 - A4 - G4 D5 -"),
  N("Acid Scream",     CAT_SYNTH,  RAV | TEC,   2, I_ACID,     X_DRIVE | X_UP,  4, 1.0f, "A2 A2 A3! A2 A2 C3~ A2 A3! A2 A2 A3 A2! G2 A2~ C3! A3"),
  N("Happy Lead",      CAT_SYNTH,  RAV,         2, I_SAW,      LOOP,            4, 0.9f, "E5 - E5 - D5 - C5 - D5 - E5 - G5 - - - A5 - G5 - E5 - D5 - C5 - D5 - E5 - - -"),
  N("Sphere Am",       CAT_PAD,    RAV | TEC,   2, I_STRINGS,  X_CHORUS | WIDE, 1, 1.0f, "A3+C4+E4+B4 _ _ _ _ _ _ _"),
  N("Night Flight",    CAT_PAD,    RAV,         2, I_STRINGS,  X_UP | WIDE,     1, 1.0f, Em " _ _ _ " F_ " _ _ _"),
  V("Ambient Nine",    CAT_PAD,    RAV | TEC,   2, I_CHOIR,    WIDE,            1, 1.0f, Am7 " _ _ _ _ _ _ _", "o"),
  N("Whistle Song",    CAT_PAD,    RAV,         2, I_WHISTLE,  WIDE,            2, 1.0f, "A5 _ _ G5~ E5~ _ _ _ D5 _ E5~ _ C5~ _ _ _"),
  N("Lonely Bells",    CAT_PAD,    RAV | TEC,   2, I_BELL,     WIDE,            2, 1.0f, "A4 - - E5 - - C5 - G4 - - D5 - - B4 -"),
  N("Rave Stab Am",    CAT_STAB,   RAV,         1, I_STAB,     X_ROOM,          4, 2.0f, "A3+C4+E4+A4 - - - - - - - - - - - - - - -"),
  N("Rave Stab Dm",    CAT_STAB,   RAV,         1, I_STAB,     X_ROOM,          4, 2.0f, "D4+F4+A4+D5 - - - - - - - - - - - - - - -"),
  N("Hoover Hit",      CAT_STAB,   RAV,         1, I_HOOVER,   X_ROOM,          1, 1.5f, "A3 - - -"),
  N("Orch Blast",      CAT_STAB,   RAV,         1, I_STAB,     X_HALL | X_DRIVE, 4, 3.0f, "A1+A2+E3+A3+C4+E4+A4 - - - - - - - - - - - - - - -"),
  N("Siren Stab",      CAT_STAB,   RAV,         1, I_SAW,      X_ROOM,          2, 1.0f, "A4 A5~ - - - - - -"),
  N("Bell Hit",        CAT_STAB,   RAV | TEC,   1, I_BELL,     X_HALL,          1, 2.0f, "A4+E5 - - -"),
  N("Brass Blast",     CAT_STAB,   RAV,         1, I_BRASS,    X_ROOM | X_DRIVE, 4, 1.5f, "A2+A3+E4+A4 - - - - - - - A2+A3+E4+A4 - A2+A3+E4+A4 - - - - -"),
  N("Dirty Flute",     CAT_STAB,   RAV,         1, I_WHISTLE,  X_DRIVE | X_ROOM, 4, 1.0f, "A4 - C5 A4 - - E5 - D5~ - - - - - - -"),
  F("Multi Siren",     CAT_FX,     RAV,         1, X_ROOM,     f_siren),
  F("Space Jump",      CAT_FX,     RAV | TEC,   1, LOOP,       f_space_jump),
  F("Noise Swell",     CAT_FX,     RAV | TEC,   2, X_ROOM,     f_noise_swell),
  F("Drop Boom",       CAT_FX,     RAV | DAN,   1, X_ROOM,     f_drop_boom),
  N("Alarm",           CAT_FX,     RAV | TEC,   1, I_SQUARE,   X_ROOM,          2, 0.9f, "A5 E5"),
  F("Bubbles",         CAT_FX,     RAV,         1, X_ROOM,     f_bubbles),
  F("Thunder Hit",     CAT_FX,     RAV | TEC | DAN, 2, X_HALL,  f_boom_hall),

  // ── Techno: deep kick, tin snare, one-note basses, sequences that open up ──
  D("Techno Drive",    CAT_DRUMS,  TEC,         1, KIT_TECHNO, 0,               "k=x...x...x...x... o=..x...x...x...x. c=....x.......x..."),
  D("Minimal Tick",    CAT_DRUMS,  TEC,         1, KIT_TECHNO, 0,               "k=x...x...x...x... r=..x..x....x..x.. h=o"),
  D("Warehouse",       CAT_DRUMS,  TEC,         1, KIT_TECHNO, X_DRIVE | X_ROOM, "k=x...x...x...x... d=x.x.x.x.x.x.x.x. c=....x.......x..."),
  D("Hard Hats",       CAT_DRUMS,  TEC,         1, KIT_TECHNO, 0,               "k=x...x...x...x... h=Xoxo o=..x."),
  D("Detroit Shuffle", CAT_DRUMS,  TEC,         1, KIT_TECHNO, X_SWING,         "k=x...x...x...x... h=x.xxx.xxx.xxx.xx c=....x.......x..."),
  D("Tribal Tech",     CAT_DRUMS,  TEC,         1, KIT_TECHNO, 0,               "k=x...x...x...x... T=..x.....x..x.... t=.....x.......x.. l=...x......x....."),
  D("Industrial",      CAT_DRUMS,  TEC,         1, KIT_TECHNO, X_DRIVE | X_CRUSH, "k=x...x...x..xx... s=....x.......x... h=x.x.x.x.x.x.x.x. z=......x........."),
  D("Electro Break",   CAT_DRUMS,  TEC | HIP,   1, KIT_808,    0,               "k=x.....x...x..... s=....x.......x... h=xoxxxoxxxoxxxoxx"),
  D("Loop Tool",       CAT_DRUMS,  TEC,         2, KIT_TECHNO, 0,               "k=x... o=..x. c=....x.......x.......x.......x.xx r=..x..x....x..x....x..x....x.x.x."),
  D("Techno Kick",     CAT_KICK,   TEC,         1, KIT_TECHNO, 0,               "k=x..."),
  F("Rumble Kick",     CAT_KICK,   TEC,         1, 0,          f_rumble),
  D("Triplet Kick",    CAT_KICK,   TEC,         1, KIT_TECHNO, 0,               "k=x..x..x.x..x..x."),
  D("Laser Kick",      CAT_KICK,   TEC | RAV,   1, KIT_TECHNO, 0,               "k=x... z=x..."),
  D("Tin Snare",       CAT_SNARE,  TEC,         1, KIT_TECHNO, 0,               "s=....x.......x..."),
  D("Crunch Snare",    CAT_SNARE,  TEC,         1, KIT_TECHNO, X_DRIVE | X_CRUSH, "s=....x..x....x..x"),
  D("Tech Rim",        CAT_SNARE,  TEC,         1, KIT_TECHNO, 0,               "r=x..x..x..x..x.x."),
  D("Snap Hats",       CAT_HAT,    TEC,         1, KIT_TECHNO, 0,               "h=Xoxo"),
  D("Dry Open",        CAT_HAT,    TEC,         1, KIT_TECHNO, 0,               "o=..x."),
  D("Hat Sweep",       CAT_HAT,    TEC | RAV,   2, KIT_TECHNO, X_UP,            "h=x"),
  D("Offbeat Ride",    CAT_HAT,    TEC,         1, KIT_TECHNO, 0,               "d=..x."),
  D("Tech Clap",       CAT_CLAP,   TEC,         1, KIT_TECHNO, X_ROOM,          "c=....x.......x..."),
  D("Off Clap",        CAT_CLAP,   TEC,         1, KIT_TECHNO, 0,               "c=......x.......x."),
  D("Delay Crash",     CAT_CYMBAL, TEC | DAN,   2, KIT_TECHNO, LOOP,            "y=x..............................."),
  D("Tiny Cymbal",     CAT_CYMBAL, TEC,         1, KIT_TECHNO, 0,               "d=x.......x......."),
  D("Plastic Wood",    CAT_PERC,   TEC,         1, KIT_TECHNO, 0,               "w=x.x..x.x..x.x.x."),
  N("Metal Hits",      CAT_PERC,   TEC,         1, I_FMSEQ,    X_ROOM,          4, 0.5f, "A5 - - A5 - - E6 - - A5 - - - D6 - -"),
  D("Tribal Toms",     CAT_PERC,   TEC,         1, KIT_TECHNO, 0,               "T=..x...x.....x... t=x....x....x..x.. l=...x....x......x"),
  D("Zap Perc",        CAT_PERC,   TEC | RAV,   1, KIT_TECHNO, 0,               "z=..x.....x..x...x"),
  D("Click Train",     CAT_PERC,   TEC,         2, KIT_TECHNO, X_UP,            "r=xoxo"),
  D("Tech Bongo",      CAT_PERC,   TEC,         1, KIT_TECHNO, 0,               "g=x.x..x.x..x.x.x. G=...x......x....."),
  D("Tech Roll",       CAT_FILL,   TEC,         1, KIT_TECHNO, 0,               "s=o.o.o.o.xoxoxxRR"),
  D("Hat Rush",        CAT_FILL,   TEC | RAV,   1, KIT_TECHNO, 0,               "h=x.x.x.x.xxxxRRRR"),
  D("Filter Fill",     CAT_FILL,   TEC,         1, KIT_TECHNO, X_UP,            "s=x k=x..."),
  F("Tech Start",      CAT_FILL,   TEC | RAV,   1, 0,          f_tech_start),
  N("Iso Bass",        CAT_BASS,   TEC,         2, I_MOOG,     X_UP,            4, 0.6f, "A1"),
  N("Dig Sub",         CAT_BASS,   TEC,         1, I_SUB,      0,               2, 0.8f, "- A1 - A1 - A1 - A1"),
  N("Tick Bass",       CAT_BASS,   TEC,         1, I_DONK,     0,               4, 0.5f, "A1 - A1 - - A1 - A1 - - A1 - A1 - - A2"),
  N("Heli Bass",       CAT_BASS,   TEC,         2, I_REESE,    X_GATE,          1, 1.0f, "A1 _ _ _ _ _ _ _"),
  N("Inject Acid",     CAT_BASS,   TEC,         1, I_ACID,     0,               4, 1.0f, "A1 - A1 A2! - A1 - A1 C2~ - A1 - A2! - G1 A1~"),
  N("Double Rod",      CAT_BASS,   TEC,         1, I_SQUARE,   0,               4, 0.5f, "A1 A2 - A1 A2 - A1 A2 - A1 A2 - A1 A2 A1 A2"),
  N("Dust Bass",       CAT_BASS,   TEC,         1, I_SQUARE,   X_DRIVE | X_CRUSH, 4, 0.8f, "A1 - - A1 - - A1 - - A1 - - G1 - A1 -"),
  N("North Sub",       CAT_BASS,   TEC,         2, I_SUB,      0,               1, 1.0f, "A1 _ _ _ _ _ G1 _"),
  N("FM Stepper",      CAT_BASS,   TEC,         1, I_FMBASS,   0,               4, 0.7f, "A1 A1 - A1 - A1 A1 - A1 - A1 A1 - A1 C2 -"),
  N("Detroit Chords",  CAT_KEYS,   TEC,         1, I_EPIANO,   LOOP,            4, 1.0f, "- - " Am7 " - - - - " Am7 " - - - - " Am7 " - - -"),
  N("Minor Ninths",    CAT_KEYS,   TEC,         1, I_EPIANO,   X_CHORUS | LOOP, 4, 2.0f, "A3+C4+E4+B4 - - - - - - - - - G3+B3+D4+A4 - - - - -"),
  N("Glass Keys",      CAT_KEYS,   TEC | RAV,   1, I_VIBES,    X_ROOM,          4, 1.0f, "A4 - E5 - - A4 - C5 - - E5 - A4 - - G4"),
  N("Tech Organ",      CAT_ORGAN,  TEC,         1, I_ORGAN,    0,               4, 0.5f, "A2 - A3 - A2 A3 - A2 - A3 - A2 A3 - A2 A3"),
  N("Main Seq",        CAT_SYNTH,  TEC,         2, I_SAW,      X_UP,            4, 0.5f, "A2 A2 A3 A2 C3 A2 A3 A2"),
  N("Morph Seq",       CAT_SYNTH,  TEC,         2, I_FMSEQ,    X_UP | LOOP,     4, 0.6f, "A3 E4 A3 C4 A3 E4 G4 E4"),
  N("Raver Walk",      CAT_SYNTH,  TEC | RAV,   2, I_ACID,     0,               4, 1.0f, "A1 A1 C2~ A1 - A1 E2! - A1 G1~ A1 - C2! A1 - - A1 A1 C2~ A1 - A1 G2! - A1 E2~ D2 - C2! A1 - -"),
  N("Volt Arp",        CAT_SYNTH,  TEC,         1, I_SQUARE,   LOOP,            4, 0.5f, "A3 E4 A4 E4 C4 E4 A4 E4 A3 E4 A4 E4 D4 E4 G4 E4"),
  N("Blip Seq",        CAT_SYNTH,  TEC,         1, I_BLEEP,    LOOP,            4, 0.5f, "A5 - - A5 - - E5 - - A5 - - G5 - - -"),
  N("Big Mod",         CAT_SYNTH,  TEC,         1, I_FMSEQ,    X_DRIVE,         4, 0.8f, "A2 - A2 - A2 A2 - A2 - A2 - A2 A2 - G2 -"),
  N("Chord Chops",     CAT_SYNTH,  TEC | DAN,   1, I_SUPERSAW, 0,               4, 0.6f, Am " - - " Am " - - " Am " - - " Am " - - " G_ " - - -"),
  N("Acid Trip",       CAT_SYNTH,  TEC,         2, I_ACID,     X_UP,            4, 1.0f, "A1 A1 A2! A1 C2~ A1 A1 A2! A1 G1~ A1 A2 A1! C2~ D2~ E2~"),
  N("Bleep Line",      CAT_SYNTH,  TEC | RAV,   1, I_BLEEP,    X_ROOM,          4, 0.8f, "A4 - C5 - E5 - C5 - A4 - G4 - E4 - G4 -"),
  N("Endless Space",   CAT_PAD,    TEC,         4, I_STRINGS,  WIDE,            1, 1.0f, "A2+E3+A3+C4 _ _ _ _ _ _ _ F2+C3+F3+A3 _ _ _ _ _ _ _"),
  N("Bell Sphere",     CAT_PAD,    TEC,         2, I_BELL,     X_ECHO | WIDE,   2, 1.0f, "A4 E5 - C5 - A5 - E5 G4 D5 - B4 - G5 - D5"),
  N("Freeze Over",     CAT_PAD,    TEC,         2, I_STRINGS,  X_DOWN | WIDE,   1, 1.0f, "E5+A5+C6 _ _ _ _ _ _ _"),
  N("Grotto",          CAT_PAD,    TEC,         2, I_REESE,    X_DOWN | WIDE,   1, 1.0f, "D1+D2 _ _ _ _ _ _ _"),
  N("Metal Sphere",    CAT_PAD,    TEC,         2, I_BELL,     X_CHORUS | WIDE, 1, 1.0f, "A3+E4+B4 _ _ _ _ _ _ _"),
  N("Wide Land",       CAT_PAD,    TEC | DAN,   2, I_STRINGS,  X_CHORUS | WIDE, 1, 1.0f, Am " _ _ _ " Em " _ _ _"),
  N("Tender Sweep",    CAT_PAD,    TEC | RAV,   2, I_PAD,      X_UP | WIDE,     1, 1.0f, Am7 " _ _ _ _ _ _ _"),
  N("Techno Stab",     CAT_STAB,   TEC,         1, I_STAB,     X_ROOM | X_DRIVE, 4, 0.7f, "- - " Am " - - - - - - - " Am " - - - - -"),
  N("Dub Chords",      CAT_STAB,   TEC,         2, I_STAB,     X_ECHO | WIDE,   4, 0.6f, Am7 " - - - - - - - - - - - - - - - - - - - - - " Am7 " - - - - - - - - -"),
  N("Blip Stab",       CAT_STAB,   TEC,         1, I_BLEEP,    LOOP,            4, 1.0f, "A5+E6 - - - - - - - - - - - - - - -"),
  N("Moog Phrase",     CAT_STAB,   TEC,         1, I_MOOG,     LOOP,            4, 1.5f, "A3 - - C4~ - - A3~ - E4 - - D4~ - C4~ - -"),
  N("Chord Memory",    CAT_STAB,   TEC | RAV,   1, I_STAB,     0,               4, 0.8f, Am " - - G3+A#3+D4 - - " Am " - - C4+D#4+G4 - - " Am " - - -"),
  F("Signal Jam",      CAT_FX,     TEC,         1, X_CRUSH | LOOP, f_signal_jam),
  F("High Sweep",      CAT_FX,     TEC,         1, X_ROOM,     f_high_sweep),
  F("Rain Down",       CAT_FX,     TEC,         2, LOOP,       f_rain),
  F("Steam Bursts",    CAT_FX,     TEC,         1, 0,          f_steam),
  F("Swish",           CAT_FX,     TEC | DAN,   1, X_ROOM,     f_swish),
  F("Cosmic Swish",    CAT_FX,     TEC | RAV,   2, X_ROOM,     f_cosmic),
  N("Eerie Signal",    CAT_FX,     TEC,         2, I_WHISTLE,  WIDE,            2, 1.0f, "A5 _ _ _ E5~ _ _ _ G5~ _ _ _ _ _ - -"),
};
#define NUM_DEFS ((int)(sizeof(kDefs) / sizeof(kDefs[0])))

static block_t g_blocks[GR_MAX_BLOCKS];
static bool    g_meta;

static void meta(void) {
  if (g_meta) return;
  for (int i = 0; i < NUM_DEFS; i++) {
    g_blocks[i].name = kDefs[i].name; g_blocks[i].cat = kDefs[i].cat; g_blocks[i].genres = kDefs[i].genres; g_blocks[i].bars = kDefs[i].bars;
  }
  g_meta = true;
}

int blocks_count(void) { return NUM_DEFS; }

const block_t *block_get(int id) { meta(); return id >= 0 && id < NUM_DEFS ? &g_blocks[id] : NULL; }

int blocks_in_category(category_t cat, int *ids, int max) {
  int n = 0;
  for (int i = 0; i < NUM_DEFS && n < max; i++) if (kDefs[i].cat == cat) ids[n++] = i;
  return n;
}

// Level: peak at 0.85 unless that would leave a sustained sound much louder
// than a drum loop; then its RMS is capped instead.
static void level(float *buf, int n) {
  float peak = 1e-6f, dc = 0, prev = 0;
  double power = 0;
  for (int i = 0; i < n; i++) { // 15 Hz high-pass: pulse-width and drive stages leave an offset
    float x = buf[i];
    dc = x - prev + 0.9979f * dc;
    prev = x;
    buf[i] = dc;
  }
  for (int i = 0; i < n; i++) { if (fabsf(buf[i]) > peak) peak = fabsf(buf[i]); power += (double)buf[i] * buf[i]; }
  float rms = (float)sqrt(power / n) + 1e-6f, gain = fmaxf(0.4f / peak, fminf(0.85f / peak, 0.2f / rms));
  for (int i = 0; i < n; i++) buf[i] *= gain;
  for (int i = 0; i < 128 && i < n; i++) buf[n - 1 - i] *= i / 128.0f; // de-click the cut tail
}

// A pattern must fill its block or repeat evenly inside it.
static bool recipe_fits(const def_t *d) {
  for (const char *p = d->pat; p && *p;) {
    while (*p == ' ') p++;
    if (!*p) break;
    int steps = d->bars * (d->spb ? GR_BEATS_BAR * d->spb : 16), len = 0;
    if (d->spb) { for (; *p; len++) { p += strcspn(p, " "); while (*p == ' ') p++; } }
    else if (p[1] == '=') { len = (int)strcspn(p + 2, " "); p += 2 + len; }
    if (len < 1 || len > steps || steps % len) {
      fprintf(stderr, "[synth] block '%s' rejected: a pattern of %d steps does not fit %d\n", d->name, len, steps);
      fflush(stderr);
      return false;
    }
  }
  return true;
}

bool block_render(int id, int bpm, block_pcm_t *out) {
  if (id < 0 || id >= NUM_DEFS || !out) {
    fprintf(stderr, "[synth] render rejected block=%d out=%p count=%d\n", id, (void *)out, NUM_DEFS);
    fflush(stderr);
    return false;
  }
  const def_t *d = &kDefs[id];
  if (!recipe_fits(d)) return false;
  int bar = bar_frames_for_bpm(bpm), n = bar * d->bars;
  memset(out, 0, sizeof(*out));
  if (d->sample) {
    if (!groove_mp3_load(d->sample, n, &out->pcm)) return false;
    out->frames = n;
    level(out->pcm, n);
    out->npeaks = d->bars * GR_PEAKS_BAR;
    for (int k = 0; k < out->npeaks; k++) {
      int a = (int)((int64_t)n * k / out->npeaks), b = (int)((int64_t)n * (k + 1) / out->npeaks);
      float m = 0;
      for (int j = a; j < b; j++) if (fabsf(out->pcm[j]) > m) m = fabsf(out->pcm[j]);
      out->peaks[k] = (uint8_t)(fminf(1.0f, m) * 255.0f);
    }
    return true;
  }
  sy_ctx_t c = { calloc((size_t)n + SY_TAIL, sizeof(float)), n, bar, 0x9e3779b9u + (uint32_t)id * 7919u, d->fx & X_SWING ? 0.28f : 0.0f };
  if (!c.buf) { fprintf(stderr, "[synth] allocation failed block=%d frames=%d\n", id, n); fflush(stderr); return false; }
  if (d->fn)       d->fn(&c);
  else if (d->spb) sy_seq(&c, d->pat, d->spb, d->hold, (sy_inst_t)d->voice, d->arg);
  else             sy_drums(&c, (sy_kit_t)d->voice, d->pat);
  sy_fx(&c, d->fx);
  if (d->fx & X_FOLD) for (int i = 0; i < SY_TAIL; i++) c.buf[i % n] += c.buf[n + i];
  if (d->fx & X_REVERSE) for (int i = 0; i < n / 2; i++) { float t = c.buf[i]; c.buf[i] = c.buf[n - 1 - i]; c.buf[n - 1 - i] = t; }
  level(c.buf, n);
  float *fit = realloc(c.buf, (size_t)n * sizeof(float)); // drop the tail
  out->pcm = fit ? fit : c.buf;
  out->frames = n;
  out->npeaks = d->bars * GR_PEAKS_BAR;
  for (int k = 0; k < out->npeaks; k++) {
    int a = (int)((int64_t)n * k / out->npeaks), b = (int)((int64_t)n * (k + 1) / out->npeaks);
    float m = 0;
    for (int j = a; j < b; j++) if (fabsf(out->pcm[j]) > m) m = fabsf(out->pcm[j]);
    out->peaks[k] = (uint8_t)(fminf(1.0f, m) * 255.0f);
  }
  return true;
}

void block_install(int id, int bpm, block_pcm_t *io) {
  meta();
  if (id < 0 || id >= NUM_DEFS || !io) {
    fprintf(stderr, "[synth] install rejected block=%d io=%p count=%d\n", id, (void *)io, NUM_DEFS);
    fflush(stderr);
    return;
  }
  block_pcm_t old = g_blocks[id].audio;
  g_blocks[id].audio = *io;
  g_blocks[id].audio_bpm = bpm;
  g_blocks[id].audio_revision++;
  *io = old;
}

float *block_release(int id) {
  if (id < 0 || id >= NUM_DEFS) return NULL;
  float *pcm = g_blocks[id].audio.pcm;
  g_blocks[id].audio.pcm = NULL;
  g_blocks[id].audio.frames = 0;
  return pcm;
}

void blocks_free(void) {
  for (int i = 0; i < NUM_DEFS; i++) {
    free(g_blocks[i].audio.pcm);
    g_blocks[i].audio = (block_pcm_t){0};
    g_blocks[i].audio_bpm = 0;
    g_blocks[i].audio_revision++;
  }
}

#undef SR
#undef TAU
#undef GEN
#undef BEAT
#undef STEP
#undef DAN
#undef HIP
#undef RAV
#undef TEC
#undef ANY
#undef LOOP
#undef WIDE
#undef D
#undef N
#undef V
#undef F
#undef M
#undef Am
#undef F_
#undef C_
#undef G_
#undef Dm
#undef Em
#undef Am7
