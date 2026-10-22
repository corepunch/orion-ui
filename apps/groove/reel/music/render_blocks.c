// Renders Groove library blocks headlessly at 140 BPM through the app's own engine.
// Build from the repository root (see ../README.md). groove_shim.h stands in for
// groove.h so the audio sources compile without the UI framework.
#include "groove.h"
#include "synth.c"
#include "library.c"
#include "mp3_decode.c"
const char *ui_get_exe_dir(void) { return "build/bin"; }
int main(int argc, char **argv) {
  if (argc == 1) { for (int id = 0; id < blocks_count(); id++) { const block_t *b = block_get(id);
      printf("%d\t%s\t%s\t%d\t%d\n", id, b->name, kCategoryName[b->cat], b->bars, b->genres); } return 0; }
  for (int i = 1; i < argc; i++) { int id = atoi(argv[i]); block_pcm_t p = {0};
    if (!block_render(id, 140, &p)) { fprintf(stderr, "fail %d\n", id); continue; }
    char fn[64]; snprintf(fn, sizeof fn, "%s/%d.f32", getenv("BLOCKS_DIR") ? getenv("BLOCKS_DIR") : "blocks", id); FILE *f = fopen(fn, "wb");
    fwrite(p.pcm, 4, p.frames, f); fclose(f); free(p.pcm); }
  return 0;
}
