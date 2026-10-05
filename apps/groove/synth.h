#ifndef __GROOVE_SYNTH_H__
#define __GROOVE_SYNTH_H__

// Sound engine behind the block library: drum kits, pitched voices and
// effects, all rendered offline into one mono buffer. How it works and what
// it cannot do is written up in docs/sound-synthesis.md.

#include "groove.h"

#define SY_SR   ((float)GR_SAMPLE_RATE)
#define SY_TAU  6.28318530718f
#define SY_PI   3.14159265359f
#define SY_TAIL (2 * GR_SAMPLE_RATE) // room after the loop for echo and reverb tails

typedef struct {
  float   *buf;   // n + SY_TAIL frames: voices and effects may ring past the loop end
  int      n;     // loop length in frames
  double   bar;   // frames per bar
  uint32_t rng;
  float    swing; // lateness of every second 16th, as a fraction of a 16th
} sy_ctx_t;

typedef enum {
  KIT_CLASSIC, // the starter library's drum voices
  KIT_909,     // punchy dance kit
  KIT_808,     // long sub kick, tight snare
  KIT_HARD,    // overdriven rave kit
  KIT_TECHNO,  // deep kick, tin snare, dry hats
  KIT_BREAK,   // roomy acoustic-style break kit
  KIT_LOFI,    // rounded, dull hip-hop kit
  KIT_COUNT
} sy_kit_t;

typedef enum {
  I_PIANO, I_BASS, I_SUB, I_PLUCK, I_MUTE, I_ACID, I_PAD, I_ARP,          // starter voices
  I_HPIANO, I_EPIANO, I_ORGAN, I_VIBES, I_BELL,                           // keys
  I_FMBASS, I_DONK, I_REESE, I_MOOG, I_SQUARE, I_UPRIGHT,                 // basses
  I_SAW, I_SUPERSAW, I_HOOVER, I_STAB, I_BRASS, I_STRINGS, I_CHOIR,       // leads, stabs, layers
  I_WHISTLE, I_PIZZ, I_BLEEP, I_FMSEQ,
  I_WAH, I_DIST, I_CLEAN,                                                 // guitars
  I_VOX, I_ROBOT,                                                         // formant voices; arg = vowel words
} sy_inst_t;

// Post effects, applied to the whole block in this order.
enum {
  X_DRIVE    = 1 << 0,  // soft clipping
  X_CRUSH    = 1 << 1,  // sample-rate and bit reduction, then a dull low-pass
  X_GATE     = 1 << 2,  // sixteenth-note trance gate
  X_CHORUS   = 1 << 3,  // two modulated delay taps
  X_ECHO     = 1 << 4,  // dotted-eighth feedback delay
  X_ROOM     = 1 << 5,  // short reverb
  X_HALL     = 1 << 6,  // long reverb
  X_UP       = 1 << 7,  // low-pass opening across the block
  X_DOWN     = 1 << 8,  // low-pass closing across the block
  X_PUMP     = 1 << 9,  // quarter-note ducking, as if side-chained to a kick
  X_FOLD     = 1 << 10, // wrap the tail back onto the start so the loop is seamless
  X_SWING    = 1 << 11, // late off-sixteenths (set before the recipe runs)
  X_REVERSE  = 1 << 12, // play the finished block backwards
};

typedef struct {
  int   s, g;        // start and gate length in frames
  int   idx, step;   // position inside a chord, and step number in the line
  float hz, from;    // pitch, and the pitch to glide from (0 = none)
  float a;
  bool  accent;
  const char *arg;
} sy_note_t;

float sy_rnd(sy_ctx_t *c);
float sy_midi_hz(int m);
void  sy_mix(sy_ctx_t *c, int i, float v);

// Drum lanes: "k=x...x...x...x... s=....x.......x...". One character per 16th:
// x hit, o soft, X accent, r two-stroke roll, R three-stroke roll, . rest.
// A lane shorter than the block repeats. Lane keys are listed in synth.c.
void sy_drums(sy_ctx_t *c, sy_kit_t kit, const char *lanes);
// Note steps: "A1 - C2+E2 _ G1! A1~". A note name, chords joined by +, "-" a
// rest, "_" one more step for the previous note, "!" accent, "~" glide from
// the previous note. `spb` steps per beat, `hold` gate length in steps.
void sy_seq(sy_ctx_t *c, const char *steps, int spb, double hold, sy_inst_t inst, const char *arg);
void sy_voice(sy_ctx_t *c, sy_inst_t inst, const sy_note_t *n);
void sy_fx(sy_ctx_t *c, uint32_t fx);

// Primitives for hand-written recipes.
void sy_kick(sy_ctx_t *c, int s, float a);
void sy_snare(sy_ctx_t *c, int s, float a);
void sy_hat(sy_ctx_t *c, int s, float a, float decay, float len);
void sy_clap(sy_ctx_t *c, int s, float a);
void sy_kick_len(sy_ctx_t *c, int s, float a, float decay, float len);
void sy_tom(sy_ctx_t *c, int s, float hz, float a);
void sy_cymbal(sy_ctx_t *c, int s, float a, float decay, float len);
void sy_organ_note(sy_ctx_t *c, int s, int g, float hz, float a);
void sy_vox_ah(sy_ctx_t *c, int s, float hz, float a, float len);
void sy_noise_zip(sy_ctx_t *c, int s, int n, int up);
void sy_noise_bed(sy_ctx_t *c, int len, float rise);
void sy_riser(sy_ctx_t *c, int len);
void sy_impact(sy_ctx_t *c, int s);
void sy_hit(sy_ctx_t *c, sy_kit_t kit, char lane, int s, float a);
// A sine whose pitch moves from hz0 to hz1 over `len` frames (curve > 1 bends late).
void sy_sweep_tone(sy_ctx_t *c, int s, int len, float hz0, float hz1, float curve, float a, float decay);
// Band-passed noise whose centre moves from hz0 to hz1; `swell` > 0 fades in, < 0 fades out.
void sy_noise_sweep(sy_ctx_t *c, int s, int len, float hz0, float hz1, float q, float a, float swell);
// A short voiced sound scrubbed like a record. `turns` back-and-forth moves over `len` frames.
void sy_scratch(sy_ctx_t *c, int s, int len, float turns, float speed);

#endif
