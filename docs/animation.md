# Animation

Orion animates the way a window procedure does everything else: with messages.
There is no animation thread, no timer per animation and no Core Animation. A
window that is moving asks for one `evAnimate`, reads the shared frame clock when
it arrives, moves its state, invalidates, and asks again while it is still
moving. The API is in `orion/user/animation.h`.

This is ORCA's `Object.Animate` (`OBJ_RequestAnimate` posts one coalesced
message; components read `ANIM_GetTime`) applied to the WinAPI model:

| WinAPI / Windows | Orion |
|---|---|
| `WM_TIMER` loop around a property | `request_animate(win)` → `evAnimate` |
| `IUIAnimationTimer::GetTime` | `ui_animation_time()`, sampled once per frame |
| `IUIAnimationVariable` + a library transition | `ui_anim_t` + `ui_curve_t` |
| `IDCompositionVisual` transform, clip, opacity | `window_set_visual()` |
| `AnimateWindow`, `DrawAnimatedRects(IDANI_CAPTION)` | `animate_window()` |
| `MapWindowPoints` | `map_window_rect()` |
| `IsHitTestVisible="False"` during a WP transition | a window in `animate_window()` takes no input |

## The loop

```c
case evLeftButtonDown:
  ui_anim_retarget(&st->scale, .93f, UI_CURVE_PRESS);
  request_animate(win);
  return true;
case evAnimate:
  ui_anim_step(&st->scale);          // reads ui_animation_time()
  invalidate_window(win);
  if (st->scale.running) request_animate(win);
  return true;
case evPaint:
  draw_at_scale(ui_anim_value(&st->scale));
```

- `request_animate()` posts `evAnimate`. Posted messages with the same target
  and message merge, so asking twice in a frame delivers once.
- `repost_messages()` samples the clock when the frame starts. Every animation
  stepped in that frame sees the same time, so things that move together stay
  together. Outside a frame (an input handler starting an animation) the clock
  is live, so an animation started after an idle minute starts now.
- The vsynced swap (the display link on iOS) paces the loop: one `evAnimate`
  per window per frame, and none at all once everything rests.
- Values are functions of time, not of frame count. A slow frame skips ahead.
- `ui_animation_set_clock()` replaces the time source. Tests step a fake clock
  16 ms per `repost_messages()` and get the same frames every run.

## Curves

`ui_curve_t` is an eased curve with a duration, or a spring with a response
(its period, in ms) and a damping ratio, as SwiftUI's
`.spring(response:dampingFraction:)`. A spring carries velocity: hand it the
finger's speed and the motion continues the gesture. `ui_anim_retarget()`
restarts from the current value and velocity, so changing a target mid-flight
never jumps.

Use the system curves so controls that move alike feel alike:

| Curve | Use |
|---|---|
| `UI_CURVE_PRESS` | a control sinking under a finger |
| `UI_CURVE_RELEASE` | springing back, with a little bounce |
| `UI_CURVE_PAGING` | snapping to a page after a pan (critically damped) |
| `UI_CURVE_ZOOM` | a view growing out of its thumbnail |

`ui_velocity_t` measures a pointer's speed over the last 100 ms, for handing to
a spring. It reads 0 once the pointer has rested for 50 ms.

## Visuals

`window_set_visual(win, &visual)` makes the window system paint the child's
subtree into its own surface and draw `source` of that surface into `rect` with
`opacity` (both rectangles in the window's own coordinates). The window's frame,
layout and hit-testing do not change; a visual moves pixels, not the window.
`window_set_visual(win, NULL)` restores the identity and frees the surface.

## Bitmap cache

`window_set_cache(win, true)` is WPF's `CacheMode="BitmapCache"`: the child
paints its subtree into its surface once, and later frames composite those
pixels without calling its window procedures. With a visual on top, a cached
window scales or fades as an image, so its text is never laid out or
rasterized again mid-animation. Call `window_invalidate_cache(win)` when its
content changes; a theme change or a new size repaints it too.

## AnimateWindow

`animate_window(win, from, flags, curve)` is `AnimateWindow` with
`DrawAnimatedRects`' zoom, made asynchronous:

- `AW_ZOOM` grows the window out of `from` into its frame. The content fills the
  growing rectangle like a photo fills a frame: scaled to cover it, centred and
  cropped. A landscape reader growing out of a portrait cover therefore never
  stretches.
- `AW_BLEND` fades the window in over the first part of the way, so the
  thumbnail under `from` hands over to it.
- `AW_HIDE` runs the same motion backwards and hides the window at the end.

The window is shown at the start and takes no input until the end. Then its
parent receives `evCommand` with `MAKEDWORD(win->id, awnFinished)` and
`lparam = win`. Starting another animation on the same window ends the running
one first, with its notification.

For the window to grow over a sibling, both must be laid out at once. Put them
in an `OverlayView` (XUL's `<stack>`, SwiftUI's `ZStack`): every child fills the
box, later children paint above earlier ones and take the pointer first.

## Example: Comics

- **Cover press.** `ComicCover` is cached: the artwork, hardcover spine and
  lettered title are baked into one image, and the shelf shows that image. It
  sinks to `--press-scale` (a visual over the cached image) on `UI_CURVE_PRESS`,
  springs back on `UI_CURVE_RELEASE`, and sends `btnClicked` when it is back
  up, so even a quick tap shows the bounce before the book opens.
- **Opening and closing.** The reader is a `ComicStage` over the shelf in an
  `OverlayView`. Opening calls `animate_window(stage, cover, AW_ZOOM | AW_BLEND,
  UI_CURVE_ZOOM)` with the cover's book rectangle mapped into the stage. Closing
  adds `AW_HIDE`. A book whose cover is not in view zooms from the middle.
- **Paging.** The stage follows a horizontal pan with the next spread beside the
  current one, and rubber-bands at either end. On release it projects the
  finger's momentum 150 ms ahead (as UIKit pages) and springs to the nearer
  spread on `UI_CURVE_PAGING`, starting at the finger's speed. A finger that
  catches a moving spread holds it.
