// Audit the production library after its normal rendering and levelling.
#include "apps/groove/synth.c"
#include "apps/groove/library.c"
#include "apps/groove/mp3_decode.c"
#include "apps/groove/sample_levels.h"
const char *ui_get_exe_dir(void) { return "build/bin"; }

static void csv_name(const char *name) {
  putchar('"');
  for (; *name; name++) { if (*name == '"') putchar('"'); putchar(*name); }
  putchar('"');
}

int main(int argc, char **argv) {
  int bpm = argc > 1 ? atoi(argv[1]) : GR_BPM_DEFAULT;
  if (bpm < GR_BPM_MIN || bpm > GR_BPM_MAX) { fprintf(stderr, "BPM must be %d to %d\n", GR_BPM_MIN, GR_BPM_MAX); return 1; }
  printf("id,name,category,bpm,peak,rms,active_rms,max_400ms_rms,balance_db\n");
  for (int id = 0; id < blocks_count(); id++) {
    block_pcm_t pcm = {0};
    if (!block_render(id, bpm, &pcm)) return 1;
    for (int i = 0; i < pcm.frames; i++) if (!isfinite(pcm.pcm[i])) {
      fprintf(stderr, "Non-finite audio: block=%d bpm=%d frame=%d\n", id, bpm, i); free(pcm.pcm); blocks_free(); return 2;
    }
    level_measure_t m = sample_measure(pcm.pcm, pcm.frames);
    const block_t *b = block_get(id);
    const level_profile_t *p = &kSampleLevels[b->cat];
    if (m.peak <= 0 || m.peak > p->peak + .00001f || m.active_rms > p->active_rms + .00001f || m.window_rms > p->window_rms + .00001f) {
      fprintf(stderr, "Level outside role limits: block=%d bpm=%d\n", id, bpm); free(pcm.pcm); blocks_free(); return 3;
    }
    printf("%d,", id); csv_name(b->name);
    printf(",%s,%d,%.9f,%.9f,%.9f,%.9f,%d\n", kCategoryName[b->cat], bpm, m.peak, m.rms, m.active_rms, m.window_rms, pcm.balance_db);
    free(pcm.pcm);
  }
  blocks_free();
  return 0;
}
