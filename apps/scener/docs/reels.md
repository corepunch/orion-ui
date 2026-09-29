# Reels: programmable motion graphics in Scener

A reel is an XML file (`*.reel`) that Scener renders to an MP4, a PNG
sequence or a still. It composes animated 3D scene layers with type, shapes,
joint-anchored callouts, trails and live measurements. Every attribute may be
an expression of time, compiled once to bytecode, so any frame is a pure
function of `t` and renders the same whether it is a still, a scrub or frame
87 of a video. There are no overlay helpers: everything is drawn by Scener.

The design follows the Lua reel toolkit in `lua-objc/modules/reel`
(attributes as expressions of `t`, motion presets, staggered text reveals),
rebuilt in C around Scener's renderer.

```sh
scener --reel scenes/infographics/ik_fk.reel --check
scener --reel scenes/infographics/ik_fk.reel --output ik-vs-fk.mp4 --poster ik-vs-fk.png
scener --reel scenes/infographics/ik_fk.reel --output still.png --time 1.5
scener --reel scenes/infographics/ik_fk.reel --output-dir frames/
```

`--output FILE.mp4` encodes H.264 directly from GL readback: through
VideoToolbox on macOS (hardware encoder, MP4 written by Scener), or by piping
raw frames to `ffmpeg` elsewhere. No intermediate images are written.
`--output FILE.png|jpg` renders one frame at `--time` (default: the reel's
`poster`). `--output-dir` writes `frame_NNNN.png`. `--poster FILE` also writes
the poster frame. `-no-shadows` applies to scene layers. A video has
`floor(duration × fps)` frames, so a 6 s, 24 fps reel is exactly 144 frames
and loops without a duplicated frame.

## Performance

Measured on an Apple M1 with an `-O2` build, the 1600×1000 IK/FK reel
(two rigged characters, shadowed and 2× supersampled, about 60 text and
vector elements) samples its 145 check frames in 0.7 s and renders plus
encodes 144 frames in 2.9 s, about 20 ms per frame. The default Makefile
build has no optimisation flag and is about 3× slower. Scene
evaluation (`scene_set_time`) is the largest per-frame cost; the 2D pass is
one batched draw call per texture or polyline.

## Document

```xml
<reel width="1600" height="1000" fps="24" duration="6" poster="1.5"
      background="#090E15" supersample="2">
  <style name="title" font="sans-medium" size="46" color="#F0F5FA" tracking="-0.015"/>
  <let name="drift" value="dist(FK.left_palm, FK.left_palm@0)"/>
  <check name="FK hand visibly moves" value="drift" over="max" min="10"/>
  <scene src="ik_fk.blks" camera="Comparison" x="0" y="205" width="1600" height="640"/>
  <text style="title" x="60" y="47" valign="top" reveal="rise" at="0.1">Same motion.</text>
  <text style="value" x="64" y="884" valign="top">{drift:%4.1f} cm</text>
  <polyline anchors="FK.left_upperarm FK.left_forearm FK.left_palm" width="2.5" color="#FFA84A" dots="5"/>
  <rect x="64" y="977.5" width="1472 * t / duration" height="3" color="#40E0D1"/>
</reel>
```

Coordinates are canvas pixels, origin top-left, y down. Colours are
`#RRGGBB` or `#RRGGBBAA` in sRGB; graphics blend in sRGB like design tools.
Unknown elements and attributes are load errors with a line number, so typos
never silently render.

`<reel>` takes `width`, `height` (even), `fps`, `duration`, `poster`,
`background` and `supersample` (1–4, for scene layers).

### Common attributes

Every drawn element takes `x`, `y`, `alpha`, `scale`, `rotation` (degrees),
`from` and `to` (visible while `from ≤ t < to`), `color` and `motion`.
Children of `<group>` draw in the group's transformed space; `clip="x y w h"`
clips a group.

| Element | Attributes |
| --- | --- |
| `<group>` | `clip`, children |
| `<scene>` | `src` (a `.blks` next to the reel), `camera`, `width`, `height`, `time` (scene time, default `t`: remap for slow motion or holds) |
| `<text>` | `style`, `align` (left/center/right), `valign` (baseline/top/middle/bottom), `reveal` (none/rise/fade/type), `at`, `stagger`, `exit`; content with `{expression}` or `{expression:%.1f}` |
| `<rect>` | `width`, `height`, `radius`, `stroke` (outline width; 0 fills), `origin` (topleft/center) |
| `<circle>` | `radius`, `stroke` |
| `<line>` | `x1`, `y1`, `x2`, `y2`, `width`, `dash="on off"` |
| `<polyline>` | `anchors="Inst.joint …"` or `<pt x y/>` children, `width`, `dash`, `dots` (joint marker radius), `dotFill` |
| `<trail>` | `anchor`, `start`, `end` (default `t`), `width`, `dash`: the joint's sampled path, re-projected every frame |
| `<let>` | `name`, `value`: a per-frame variable for later expressions |
| `<check>` | `name`, `value`, `min`, `max`, `over` (every/max/min) |
| `<style>` | `name`, `font`, `size`, `color`, `tracking` (em), `weight` (em dilation), `digits` (tabular/proportional) |
| `<font>` | `name`, `src` (TTF next to the reel or in `share/orion/fonts`) |
| `<curve>` | `name`, `ease`, `loop`; content `time:value` keys; callable as `name(x)` |

Built-in fonts are `sans` (Noto Sans Regular), `sans-medium` and `mono`.
Text content folds line breaks and indentation into one space and keeps runs
of spaces; `{{` and `}}` are literal braces. A missing glyph renders as
U+FFFD, so check coverage: bundled Noto Sans has no arrows (use `›`).

### Expressions

Attributes are numbers or expressions: `+ - * / % ^`, comparisons,
`&&` `||` `!` (write `&lt;` and `&amp;` in XML), and `c ? a : b`. Names:
`t`, `frame`, `duration`, `fps`, `width`, `height`, `pi`, lets, curves.
Functions: `sin cos tan asin acos atan atan2 abs sqrt exp log pow floor ceil
round fract sign min max clamp mix progress(x, a, b) smoothstep(a, b, x)
step spring(dt, response, damping) pulse(x, at, decay) noise(x, seed) hypot
rad deg dist(A, B)` and every easing by name (`outCubic(x)`, `inOutExpo(x)`,
`outBack(x)`, `outBounce(x)`, …).

Joints of any scene layer are anchors. `Inst.joint.x|y|z` is the posed world
position in centimetres at full float precision; `Inst.joint.sx|sy` is its
projection through the layer's camera into canvas pixels. `Inst.joint@1.5`
samples a fixed time. `dist(A, B)` takes anchors or `vec(x, y, z)`.
Expressions that do not depend on time fold to constants when loaded.
Scene layer `time`, placement and size are evaluated before lets and must
use `t` only.

### Motion presets

`motion` composes calls with constant arguments: `pop(at, response,
damping)`, `slam(at, from)`, `enter(at, dx, dy)`, `leave(at, duration, dx,
dy)`, `fadeIn(at, duration)`, `fadeOut(at, duration)`, `punch(at, amount)`,
`rise(at, distance, duration)`. Text reveals: `rise` lifts each word from
behind its baseline mask on a spring, `fade` fades words in, `type` reveals
characters; `exit` reverses them.

### Checks

Before rendering, and for `--check`, Scener samples every frame of the
timeline including its end and evaluates each `<check>`. `over="every"`
(default) requires every frame within `min`/`max`; `over="max"`/`"min"`
tests the timeline's extreme. A failing check prints the first failing time
and exits non-zero without writing output. The IK/FK reel checks its IK
target, arm lengths and planted feet to 0.01 cm.

## Rendering

- **Type** uses signed-distance-field atlases (`orion/user/font_sdf.c`):
  64 px glyphs with an 8 px distance range, generated lazily. Text is sharp at
  any size, scale or rotation, with kerning, tracking, tabular digits and
  weight adjustment done by the distance threshold.
- **Shapes** are analytic signed distances in one shader: rounded boxes,
  rings and capsule segments, antialiased by screen-space derivative.
- **Lines and polylines** draw interior pixels once through the stencil and
  then their fringes, so translucent joints never double up. Dashes follow
  the whole polyline's arc length and have round caps.
- **Scene layers** render with the regular Scener renderer into their own
  sRGB target at `supersample` scale and are box-filtered into the canvas.

Source: `reel.h`, `reel.c` (XML, evaluation, checks), `reel_expr.c`
(compiler and VM), `reel_draw.c` (GL), `reel_video.c` (encoder and MP4).

## Limits

- Anchors and trails are absolute canvas positions: placing them inside a
  scaled or rotated group transforms them again.
- Scene layers use their camera as authored; camera animation comes from the
  scene (for example `follow`), not from reel attributes.
- No image elements, gradients, arrowheads, text wrapping or motion blur yet.
- The editor does not preview reels; render a still with `--time`.
