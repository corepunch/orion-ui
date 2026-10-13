I checked the chapter list for the video. It contains **10 programs**, in this order. The video misspells **Glaxnimate** as “Glaxinmate” in the chapters. ([YouTube][1])

A caveat on the complaints below: bug reports and Reddit threads naturally over-represent unhappy users. I’m treating them as **pain-point signals**, not as evidence that every user has these problems.

## The 10 programs and their pain points

| Time  | Software           | What people complain about                                                                                                                                                                                                                                                                                                                                                                                                                     | What this tells you                                                                                                                   |
| ----- | ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| 00:19 | **Stykz**          | Essentially abandoned/very old; current macOS compatibility problems because of its 32-bit heritage; audio has to be handled externally; very constrained once you want more than stick figures; lots of manual frame work. ([Reddit][2])                                                                                                                                                                                                      | Simplicity is attractive, but don't achieve simplicity by omitting fundamental animation workflow.                                    |
| 01:19 | **TupiTube Desk**  | Older reviews report missing basic drawing tools, awkward selection/object manipulation, frame/object deletion problems, lag, export quality issues. It's deliberately beginner/child-oriented, so advanced users quickly hit the ceiling. ([SourceForge][3])                                                                                                                                                                                  | Beginner-friendly UI is good. “Beginner-only architecture” is not.                                                                    |
| 02:03 | **Pivot Animator** | Great at its narrow job, but limited general drawing/compositing, Windows-only, no proper integrated audio workflow, large projects can become unwieldy. Some useful functionality is deliberately hidden behind keyboard shortcuts. Installer/adware/antivirus warnings have also created trust problems. Current Pivot 5 does have tweening, so old complaints that it has no tweening are obsolete. ([pivotanimator.net][4])                | Pivot's direct manipulation is worth stealing. Hidden commands and platform limitations are not.                                      |
| 03:20 | **Pencil2D**       | Users love the simplicity, but its own FAQ discusses crashes, corrupted projects, audio limitations, missing text/shape/video-import features and export issues. As of **March 2026**, its vector engine is explicitly still not recommended for serious projects, with some tablet issues remaining. ([Pencil2D Animation][5])                                                                                                                | Probably one of your most important references: the UI scope is right, but reliability and production depth need to be much higher.   |
| 03:57 | **Krita**          | Excellent drawing experience, but animation can feel like a secondary subsystem inside a painting application. Large animations consume substantial RAM; users report animation-specific crashes/rough edges and awkwardness in some timeline/curve workflows. ([Reddit][6])                                                                                                                                                                   | A great brush engine isn't enough. Animation data needs its own scalable architecture rather than “lots of paintings kept in memory.” |
| 04:56 | **Synfig Studio**  | Powerful vector tweening/rigging, but the recurring complaint is **complexity**. Drawing directly in it is less pleasant than drawing-oriented software; terminology/workflow takes learning; users have reported crashes and rendering/audio quirks. Recent releases continue fixing stability problems. ([Freelancer][7])                                                                                                                    | Automatic/tweened animation is valuable, but exposing the underlying animation machinery directly creates a huge usability tax.       |
| 05:50 | **OpenToonz**      | The biggest recurring complaints are **crashes, data loss fear and UI complexity**. There are very recent July 2026 reports of crashes even on small projects. Other users complain that seemingly simple operations, audio setup, vector/keyframe editing and rendering can require too much knowledge of OpenToonz's studio-oriented model. ([Reddit][8])                                                                                    | The feature set is impressive. The UX is almost a checklist of what you should *not* expose to a casual animator.                     |
| 06:42 | **Glaxnimate**     | Nicely focused vector animation, but issue reports expose gaps around multi-keyframe operations, timeline performance, text, certain exports/crashes and less-obvious compositing/mask operations. Its ecosystem and learning material are also much smaller than Blender/Krita. ([GitLab][9])                                                                                                                                                 | Basic tweening isn't enough. Animators very quickly need bulk timeline manipulation and obvious masking/compositing.                  |
| 07:38 | **Bforartists**    | It exists specifically because Blender's interface is seen as overly complicated; its own site talks about removing duplicated/crowded UI. But changing Blender's UI creates another problem: smaller tutorial ecosystem and occasional mismatch with the enormous amount of Blender learning material. Some users also find an icon-heavy UI itself cluttered. ([Bforartists][10])                                                            | Merely replacing menus with icons doesn't solve complexity. The *workflow model* needs simplification.                                |
| 08:37 | **Blender**        | Enormous capability, but this is exactly the problem for someone who primarily wants 2D. Complaints include huge discoverability burden, reliance on modes/shortcuts, UI intended around 3D concepts, tablet-unfriendly layouts, hidden state and complicated rendering/export configuration. Grease Pencil also still gets current crash/bug reports; a May 2026 user reported crashes after only several strokes in one file. ([Reddit][11]) | **Do not make “Blender but easier.”** Make the 2D animation operations themselves fundamentally simpler.                              |

---

# The common pain points are remarkably consistent

Across almost all ten programs, the complaints collapse into roughly six problems:

1. **“I can animate easily, until my project gets serious.”**
   Pencil2D, Pivot and TupiTube are pleasant initially, but users run into missing production features.

2. **“It can do everything, but I can't figure out how.”**
   Blender, OpenToonz and Synfig suffer from the opposite problem.

3. **Animation projects are too fragile.**
   Crashes, corrupted projects, autosave problems and RAM exhaustion are disastrous because an animation can represent hundreds of hours of work.

4. **Basic related tasks feel bolted on.**
   Audio, export, masks, camera, text, retiming, compositing.

5. **Timeline manipulation becomes tedious at scale.**
   Moving 40 frames, extending exposures, shifting several layers, copying a motion cycle, changing timing, etc. often requires far too many operations.

6. **“Simple UI” often means hiding things rather than simplifying them.**
   That gives you Pivot's hidden shortcuts on one end and Blender's enormous control surface on the other.

That gives you a fairly clear opportunity.

# What I would make

The sweet spot is essentially:

> **Pencil2D's immediate usability + Krita's pleasant drawing + Pivot's direct manipulation + Synfig's tweening + OpenToonz's production capabilities — without inheriting OpenToonz/Blender's mental model.**

Not all their features. Their **capabilities**, exposed through a much smaller model.

## Must-have V1

| Area               | I would consider mandatory                                                                                    |
| ------------------ | ------------------------------------------------------------------------------------------------------------- |
| **Drawing**        | Pencil/brush, eraser, pressure, smoothing/stabilization, fill, eyedropper, selection, move/scale/rotate, flip |
| **Frames**         | New, duplicate, delete, cut/copy/paste, hold/exposure, drag frames around                                     |
| **Timeline**       | Multiple layers, multi-select, drag retiming, insert/delete time, shift everything after cursor               |
| **Onion skin**     | Previous/next independently, number of frames, opacity, tint                                                  |
| **Layers**         | Raster layers, groups, visibility, lock, opacity                                                              |
| **Playback**       | Real-time looping, frame stepping, range playback                                                             |
| **Audio**          | WAV/MP3 import, **visible waveform**, scrubbing, offset, multiple clips                                       |
| **Camera**         | Pan/zoom/rotate camera with keyframes                                                                         |
| **Undo**           | Deep, fast, reliable undo/redo across absolutely everything                                                   |
| **Tablet**         | Pressure, tilt if available, correct high-DPI support                                                         |
| **Export**         | PNG sequence first and foremost; MP4/WebM/GIF; alpha-capable sequence; spritesheet                            |
| **Project safety** | Autosave, journal/recovery file, incremental backup, missing-resource detection                               |
| **Performance**    | Disk-backed frame cache rather than assuming the entire animation fits in RAM                                 |
| **UI**             | Every operation accessible visibly; shortcuts accelerate actions but aren't required to discover them         |

I'd put **project safety ridiculously high on the priority list**.

A spectacular animation program that loses four hours of work once every three months is worse than a mediocre one that never does.

## Features that could actually differentiate your software

### 1. One unified timeline

This is probably the biggest opportunity.

Don't have one conceptual system for drawings, another for transforms, another for bones, another for camera motion, another for audio.

The user should see:

```text
Character
    Body       | ■────■────■
    Eyes       | ■─■───■────
    Position   | ●────────●
Background     | ■───────────
Camera         | ●────●──────
Audio          | ~~~~~~~~~~~~~
```

A drawing frame and a property keyframe can be different things internally, but the animator shouldn't need a degree in your data model.

---

### 2. Frame-by-frame **and** tweening should coexist

This is where I would go beyond Pencil2D without becoming Synfig.

Draw something.

Move it.

Press a button:

**Tween to here**

Done.

Now you have:

```text
A ●----------● B
```

Choose:

* Linear
* Ease
* Ease in
* Ease out
* Custom curve

And the user can break the tween at any frame and redraw something manually.

This makes it useful both as a traditional animation program and for motion-graphics/cutout animation.

---

### 3. Pivot-style direct manipulation for rigs

Pivot actually demonstrates something important: **rigging doesn't inherently have to be complicated**.

Draw an arm.

Select it.

Add joints:

```text
shoulder ●──────● elbow ──────● hand
```

Drag the hand.

Done.

Then add:

* IK
* joint limits
* bone parenting
* mesh/shape deformation later

Do **not** make users build Synfig/OpenToonz-style object hierarchies before they can make an arm bend.

---

### 4. A proper “retime everything” tool

This sounds boring and would probably become one of the most appreciated features.

Select:

```text
frames 24–73
```

Then simply drag the right edge:

```text
24────────────73

        ↓

24────────────────────────110
```

Everything inside stretches proportionally:

* drawings
* keyframes
* camera
* audio markers
* effects

Likewise:

**Insert 12 frames here**

should mean *insert twelve frames into the entire scene*, not “go fix fifteen layers afterward.”

This directly attacks a lot of timeline pain found in more established programs.

---

### 5. Make hidden state visible

Blender is full of questions like:

> Why can't I draw?

And the answer turns out to be:

* wrong mode,
* wrong object,
* wrong layer,
* hidden collection,
* locked frame,
* wrong Grease Pencil mode,
* cursor focus somewhere else.

Your application could have a tiny diagnostic area:

**Cannot draw: Layer “Character” is locked. [Unlock]**

or:

**Nothing visible: Camera is outside artwork. [Frame artwork]**

or:

**No editable drawing exists at frame 37. [Create frame]**

This is a surprisingly powerful differentiator.

Don't let the application silently ignore the user.

---

### 6. Animation-aware crash recovery

I'd design this from day one rather than adding an autosave timer later.

Something like:

```text
project.anim
project.assets/
project.recovery/
    11-32-17
    11-34-42
    11-36-03
```

Every destructive operation gets journaled cheaply.

After a crash:

> SimpleSketch Animation closed unexpectedly.
>
> Recovered to 11:36:03 — 7 seconds before the crash.
>
> [Open recovered] [Open last saved]

For animators this may matter more than twenty fancy brushes.

---

### 7. Never load the whole movie into RAM

Krita's animation architecture illustrates why this matters: animation scales very differently from a normal painting. Current Pencil2D development likewise spends significant effort on stability. ([Pencil2D Animation][12])

Use:

* compressed/disk-backed frames,
* cached decoded frames around the playhead,
* proxy playback,
* user-visible memory budget,
* background cache eviction.

A 10-minute production should not fundamentally behave differently from a 10-second test.

---

### 8. Render/export preflight

Before rendering:

```text
READY TO EXPORT

1920 × 1080
24 fps
01:42 duration
2 audio tracks
Alpha: No

✓ All fonts available
✓ All linked images available
✓ Audio supported
✓ Output folder writable

[Render Test Frame]
[Export]
```

This fixes an entire category of “why is my exported animation blank/different/silent?” problems.

---

### 9. Audio should be first-class

It is strange how often basic animation software treats sound as an afterthought.

Minimum:

```text
Dialogue  ───████──████────████────
             "Hey!"       "Wait!"

Music     ~~~~~~~~~~~~~~~~~~~~~~~~~~
SFX                  |BOOM|
```

Waveform, scrub, drag clips, cut clips, mute/solo.

Later:

**Analyze dialogue → generate phoneme markers**

Not automatic AI lip animation necessarily. Just markers:

```text
A   M   O   E   FV   A
|   |   |   |   |    |
```

Then mouth poses become trivial.

---

### 10. “Simple” should mean progressive disclosure

This is where I'd disagree with the design philosophy behind many “easy” programs.

Don't remove advanced capabilities.

Start with:

```text
Brush
Eraser
Fill
Select
Transform

Timeline
Layers
Play
```

If the user creates a rig, rig controls appear.

If they animate a property, curve controls appear.

If they add audio, audio controls appear.

If they create a mask, mask controls appear.

**Contextual complexity**, not permanent complexity.

This is the fundamental way to avoid becoming Blender.

# What I would deliberately leave out of V1

I would **not** start by implementing:

* dozens of brushes,
* 3D,
* particles,
* node graphs,
* scripting UI,
* elaborate compositing,
* procedural effects,
* collaboration,
* AI video generation,
* hundreds of import formats.

Especially brushes.

Krita already wins the “giant digital-painting toolbox” contest. Competing on having **87 brushes instead of 62** is pointless for an animation-focused application.

A handful of excellent ink/pencil/paint brushes is enough initially.

Instead spend the engineering effort on:

**drawing → frame → onion skin → timing → audio → playback → save → export**

and make that loop exceptionally good.

# My priority order

| Priority          | Features                                                               |
| ----------------- | ---------------------------------------------------------------------- |
| **P0 — identity** | Canvas/drawing, timeline, onion skin, layers, audio waveform, playback |
| **P0 — trust**    | Undo, crash-proof saving, recovery, performant long projects           |
| **P0 — output**   | Camera + PNG/video/GIF/spritesheet export                              |
| **P1**            | Transform tweening, easing, animation curves                           |
| **P1**            | Multi-frame/multi-layer retiming tools                                 |
| **P1**            | Masks, clipping, text, vector shapes                                   |
| **P1**            | Simple bone rigging + IK                                               |
| **P2**            | Vector path morphing/deformation                                       |
| **P2**            | Lip-sync/phoneme assistance                                            |
| **P2**            | Storyboard/shot management                                             |
| **P2**            | Effects/compositing                                                    |
| **Much later**    | Plugins, collaboration, 3D integration, sophisticated AI               |

The interesting product isn't really **“another free animation program.”**

It's:

> **A 2D animation application you can open after not using it for six months and immediately remember how to animate. Yet it doesn't fall apart when the project becomes serious.**

That gap is very visible in this particular set of ten applications: the simple ones tend to become limiting, while the capable ones tend to become software you have to *study*.

[1]: https://www.youtube.com/watch?v=vODBRfPu7tk&utm_source=chatgpt.com "10 Best Free Animation Software!"
[2]: https://www.reddit.com/r/animation/comments/1cs6c3u?utm_source=chatgpt.com "64-Bit Stick Figure Software for Mac"
[3]: https://sourceforge.net/projects/tupi2d/?utm_source=chatgpt.com "TupiTube Desk download | SourceForge.net"
[4]: https://pivotanimator.net/help5-2/overview.htm?utm_source=chatgpt.com "Topic: 1.1. Overview"
[5]: https://www.pencil2d.org/doc/faq?utm_source=chatgpt.com "FAQ | Pencil2D Animation"
[6]: https://www.reddit.com/r/krita/comments/1976gua/i_had_a_bad_experience_with_krita_for_animation/?utm_source=chatgpt.com "I had a bad experience with Krita for animation"
[7]: https://www.freelancer.com/articles/graphic-design/best-free-animation-software?utm_source=chatgpt.com "The 10 best free animation software in 2021 | Freelancer"
[8]: https://www.reddit.com/r/OpenToonz/comments/1v9k8jd/opentoonz_keeps_crashing_on_pc/?utm_source=chatgpt.com "OpenToonz Keeps crashing on PC"
[9]: https://gitlab.com/mattbas/glaxnimate/-/boards?utm_source=chatgpt.com "Everything"
[10]: https://www.bforartists.de/the-differences-to-blender/?utm_source=chatgpt.com "The Differences to Blender - Bforartists"
[11]: https://www.reddit.com/r/3Dmodeling/comments/1e31675?utm_source=chatgpt.com "Is Blender really difficult for anyone else?"
[12]: https://www.pencil2d.org/2026/03/pencil2d-0.7.2-release.html?utm_source=chatgpt.com "Pencil2D v0.7.2 – Stability and Polish | Pencil2D Animation"
