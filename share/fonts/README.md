# Orion fonts

CSS themes set their text faces (docs/styling.md): the Modern family, Navy,
Amber, Porcelain and Metro use IBM Plex Sans 6.4.0 (Regular, Medium, SemiBold,
Bold), from https://github.com/IBM/plex at tag `v6.4.0`
(`IBM-Plex-Sans/fonts/complete/ttf/`). It covers Latin, Greek and Cyrillic;
its license is IBMPlexSans-LICENSE.txt (SIL Open Font License 1.1). Monospace text
(the terminal, the diff view, `--mono-face`) uses IBM Plex Mono 6.4.0 Regular
and Bold from the same tag (`IBM-Plex-Mono/fonts/complete/ttf/`), under the
same license. A face is
installed as `<Family without spaces>-<Weight>.ttf`.

Classic, and any role whose face is missing, uses DejaVu Sans 2.37 with FreeType. The fonts come from
https://dejavu-fonts.github.io/ (release archive SHA-256
fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7). The system and compact text roles
use DejaVuSans.ttf; titles use DejaVuSans-Bold.ttf. Italic and bold italic
variants are included for applications that need them. The font family
supports Latin, Greek and Cyrillic. See DejaVu-LICENSE.txt for its license.

Font sizes are logical em sizes. The glyph cache uses hinted FreeType bitmaps
at display density while retaining logical advance and line metrics, so Retina
increases raster resolution without changing layout. Advances are kept in 1/64
of a logical pixel and summed before rounding, so letter spacing stays even;
each glyph starts on a device pixel. `monoid.ttf` is the fixed-width face
Scener's reel draws with.

FreeType 2.15.0 is included as the `vendor/freetype` Git submodule, pinned to
the `VER-2-15-0` release. Initialize it with
`git submodule update --init --recursive`. Orion's selected modules are listed
in `orion/user/freetype_modules.h`.

This software is based in part on the work of the FreeType Project
(https://freetype.org). License notices are bundled in share/licenses/freetype.
