# Shared shader module

Orion and Open Realm vendor the same GL Shader release under `vendor/gl_shader/`.
See its [contract](../vendor/gl_shader/README.md) for profile support, storage,
errors, allocator hooks and synchronization rules. The code is MIT-licensed and
independent of Orion, SDL, and Open Realm's game types.

`orion/kernel/renderer.c` compiles the implementation once. Backend selection is
GLSL 150 on desktop and ES 300 under `ORION_OPENGL_ES`. Built-in sprite, SDF,
indexed-color, presentation and VGA programs use `shader_desc_t` and
`sprite_state_t`; `load_sprite_program` owns their file bodies and descriptors.
`share/shaders/sprite.vert.glsl` defines `vert()`, and their fragment files
define `frag()`. Their version, declarations and main wrappers are generated.

`sprite_uniforms` maps the shared state to GLSL names; inactive uniforms are
optimized away and skipped. Projection changes update all built-in state structs.
Submission happens before each draw. `push_sprite_args` also submits immediately
because the font renderer draws its own mesh after that public call.
`draw_rect_program_common` invalidates a built-in program's cache before making
direct uniform writes, so subsequent typed submissions restore complete state.

`ui_load_program_from_source` keeps the existing three-attribute API.
`ui_load_program_with_attributes` accepts a longer list, used by Scener reels.
Both route complete sources through the shared compiler. Scener PBR, images,
lines, shadow volumes, viewport presentation, reels and image-editor animation
no longer have private compile/link implementations. `common.vert.glsl` and
image-editor shader/filter files retain complete-source syntax; the legacy
filter importer still owns its constrained syntax conversion. New built-ins
should use descriptors instead of expanding that importer.

The migration preserves the existing linear-light/premultiplied color math.
Framebuffer sRGB policy and FP16 capability checks stay in the renderer.
Scener's other desktop GL operations remain a separate GLES-porting concern.

```sh
make -j4 test build/bin/scener build/bin/imageeditor
build/bin/test_shader_module_test
build/bin/test_renderer_alpha_test
```

The alpha suite creates an offscreen CGL context on macOS and checks plastic
states, card silhouettes, gradients, transforms, canvas/window compositing and
toolbar artwork. It also checks cached sprite state after raw effect writes
and the lazy indexed-color shader's palette sampling. A sandbox without access to graphics services skips those
tests; run outside that sandbox to verify the real pixel path.
