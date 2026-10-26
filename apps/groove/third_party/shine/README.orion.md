Shine MP3 encoder, upstream https://github.com/toots/shine
Revision: ab5e3526b64af1a2eaa43aa6f441a7312e013519 (2026-10-26).
License: GNU Library General Public License, version 2 or later; see COPYING
and upstream file headers. Added a missing include guard to huffman.h for unity compilation; otherwise source is unchanged. .c files use .inc suffixes
so Orion's recursive application unity build includes them exactly once,
through mp3_encode.c. Architecture-specific headers are upstream originals.
