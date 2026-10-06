#include "standalone_synth.h"
#include <errno.h>

static bool number(const char *text, double *value) {
  char *end;
  errno = 0;
  *value = strtod(text, &end);
  return end != text && *end == '\0' && errno == 0 && isfinite(*value);
}

int main(int argc, char **argv) {
  bool drums = argc == 5 && strcmp(argv[1], "--drums") == 0;
  if (!drums && argc != 7) {
    fprintf(stderr, "usage: render_study notes.csv instrument effects seconds output.f32 bpm\n"
                    "       render_study --drums seconds output.f32 bpm\n");
    return 1;
  }
  double seconds, bpm, instrument = 0, effects = 0;
  const char *output = argv[drums ? 3 : 5];
  if (!number(argv[drums ? 2 : 4], &seconds) || seconds <= 0 || seconds > 300 ||
      !number(argv[drums ? 4 : 6], &bpm) || bpm < 20 || bpm > 400 ||
      (!drums && (!number(argv[2], &instrument) || instrument != floor(instrument) ||
                  instrument < I_PIANO || instrument > I_ROBOT ||
                  !number(argv[3], &effects) || effects != floor(effects) || effects < 0 || effects > 8191))) {
    fprintf(stderr, "[study] invalid duration, tempo, instrument or effects\n");
    return 1;
  }
  sy_ctx_t ctx = {0};
  ctx.n = (int)(seconds * GR_SAMPLE_RATE + 0.5);
  ctx.bar = GR_SAMPLE_RATE * 240.0 / bpm;
  ctx.rng = drums ? 42 : 1234567;
  ctx.buf = calloc((size_t)(ctx.n + SY_TAIL), sizeof(float));
  if (!ctx.buf) {
    fprintf(stderr, "[study] buffer allocation failed: frames=%d\n", ctx.n);
    return 1;
  }
  bool ok = true;
  if (drums) {
    sy_drums(&ctx, KIT_909, "k=x...x...x...x... c=....x.......x... h=x.x.x.x.x.x.x.x. o=..x...x...x...x.");
  } else {
    FILE *input = fopen(argv[1], "r");
    if (!input) {
      fprintf(stderr, "[study] cannot open notes: %s\n", argv[1]);
      free(ctx.buf);
      return 1;
    }
    char line[256], extra;
    int count = 0, midi;
    double start, duration;
    float amplitude;
    while (fgets(line, sizeof(line), input)) {
      count++;
      if (sscanf(line, "%lf,%lf,%d,%f %c", &start, &duration, &midi, &amplitude, &extra) != 4 ||
          !isfinite(start) || !isfinite(duration) || !isfinite(amplitude) ||
          start < 0 || start >= seconds || duration <= 0 || duration > 300 ||
          midi < 0 || midi > 127 || amplitude < 0 || amplitude > 4) {
        fprintf(stderr, "[study] invalid note: file=%s line=%d\n", argv[1], count);
        ok = false;
        break;
      }
      sy_note_t note = {0};
      note.s = (int)(start * GR_SAMPLE_RATE + 0.5);
      note.g = (int)(duration * GR_SAMPLE_RATE + 0.5);
      note.hz = sy_midi_hz(midi);
      note.a = amplitude;
      note.step = count - 1;
      sy_voice(&ctx, (sy_inst_t)instrument, &note);
    }
    if (ferror(input) || !count) {
      fprintf(stderr, "[study] empty or unreadable notes: %s\n", argv[1]);
      ok = false;
    }
    fclose(input);
    if (ok) sy_fx(&ctx, (uint32_t)effects);
  }
  if (ok) {
    for (int i = 0; i < ctx.n; i++) {
      if (!isfinite(ctx.buf[i])) {
        fprintf(stderr, "[study] non-finite output: frame=%d\n", i);
        ok = false;
        break;
      }
    }
  }
  if (ok) {
    FILE *file = fopen(output, "wb");
    if (!file) {
      fprintf(stderr, "[study] cannot open output: %s\n", output);
      ok = false;
    } else {
      ok = fwrite(ctx.buf, sizeof(float), (size_t)ctx.n, file) == (size_t)ctx.n;
      if (fclose(file) != 0) ok = false;
      if (!ok) fprintf(stderr, "[study] output write failed: %s\n", output);
    }
  }
  free(ctx.buf);
  return ok ? 0 : 1;
}
