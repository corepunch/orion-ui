# make -C apps/imageeditor/penciltest INPUT=/path/to/video.mp4
# Override any setting on the command line, e.g. MIN_WIDTH=2 MIN_LENGTH=10.
.DEFAULT_GOAL := export
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
INPUT ?= input.mp4
OUTPUT_DIR ?= $(ROOT)/build/penciltest-video
THRESHOLD ?= 180
# Lower width/length values preserve more detail; higher threshold includes lighter ink.
MIN_WIDTH ?= 1.25
MIN_LENGTH ?= 10
TEMPORAL ?= 1
# Space-separated X,Y,W,H rectangles, in source/output pixels; empty for other clips.
PROTECT_REGIONS ?=
FIXED_PROTECTION ?= 0
PALETTE ?= default
FFMPEG ?= $(or $(shell command -v ffmpeg 2>/dev/null),$(firstword $(wildcard $(ROOT)/build/lineart-python/lib/python*/site-packages/imageio_ffmpeg/binaries/ffmpeg-*)),ffmpeg)
PYTHON ?= $(if $(wildcard $(ROOT)/build/lineart-python/bin/python3),$(ROOT)/build/lineart-python/bin/python3,python3)
SIZE ?=
FPS ?=
MAX_FRAMES ?=
export ROOT INPUT OUTPUT_DIR THRESHOLD MIN_WIDTH MIN_LENGTH TEMPORAL
export PROTECT_REGIONS FIXED_PROTECTION PALETTE FFMPEG PYTHON SIZE FPS MAX_FRAMES

define EXPORT_METADATA
import json, os, pathlib, sys
e = os.environ
stage = pathlib.Path(sys.argv[1])
settings = {
    "source": str(pathlib.Path(e["INPUT"]).resolve()), "mode": "black",
    "threshold": int(e["THRESHOLD"]), "min_width": float(e["MIN_WIDTH"]),
    "min_length": float(e["MIN_LENGTH"]), "temporal": e["TEMPORAL"] == "1",
    "protect_regions": [list(map(int, r.split(","))) for r in e["PROTECT_REGIONS"].split()],
    "fixed_protection": e["FIXED_PROTECTION"] == "1", "palette": e["PALETTE"],
    "size": e["SIZE"] or None, "fps": e["FPS"] or None,
    "max_frames": e["MAX_FRAMES"] or None, "ffmpeg": e["FFMPEG"],
    "source_pencil": True, "pencil_layer_visible": True, "shadow_fill": False,
    "output": "black.ptf", "frames": "black-frames", "status": "converted",
    "frame_count": len(list((stage / "black-frames").glob("frame-*.png"))),
}
(stage / "export.json").write_text(json.dumps(settings, indent=2) + "\n")
endef
export EXPORT_METADATA

.PHONY: export converter help
converter:
	@$(MAKE) -C "$(ROOT)" penciltest-video

export: converter
	@set -eu; \
	test -f "$$INPUT" || { echo "Input video not found: $$INPUT" >&2; exit 1; }; \
	mkdir -p "$$OUTPUT_DIR"; \
	for project in "$$OUTPUT_DIR"/*.ptf; do \
	  test ! -e "$$project" || test "$$project" = "$$OUTPUT_DIR/black.ptf" || \
	    { echo "Another PTF exists: $$project. Choose an empty OUTPUT_DIR." >&2; exit 1; }; \
	done; \
	stage=$$(mktemp -d "$$OUTPUT_DIR/.export.XXXXXX"); \
	trap 'rm -rf "$$stage"' EXIT HUP INT TERM; \
	set -- --mode black --palette "$$PALETTE" --threshold "$$THRESHOLD" \
	  --min-width "$$MIN_WIDTH" --min-length "$$MIN_LENGTH" \
	  --ffmpeg "$$FFMPEG" --python "$$PYTHON" \
	  --output "$$stage/black.pending" --frames "$$stage/black-frames"; \
	if test "$$TEMPORAL" = 1; then set -- "$$@" --temporal; fi; \
	if test "$$FIXED_PROTECTION" = 1; then set -- "$$@" --fixed-protection; fi; \
	for region in $$PROTECT_REGIONS; do set -- "$$@" --protect-region "$$region"; done; \
	if test -n "$$SIZE"; then set -- "$$@" --size "$$SIZE"; fi; \
	if test -n "$$FPS"; then set -- "$$@" --fps "$$FPS"; fi; \
	if test -n "$$MAX_FRAMES"; then set -- "$$@" --max-frames "$$MAX_FRAMES"; fi; \
	"$$ROOT/build/bin/penciltest-video" "$$@" "$$INPUT"; \
	"$$PYTHON" -c "$$EXPORT_METADATA" "$$stage"; \
	rm -rf "$$OUTPUT_DIR/black-frames"; \
	mv "$$stage/black-frames" "$$OUTPUT_DIR/black-frames"; \
	mv -f "$$stage/black.pending" "$$OUTPUT_DIR/black.ptf"; \
	mv -f "$$stage/export.json" "$$OUTPUT_DIR/export.json"; \
	echo "Project: $$OUTPUT_DIR/black.ptf"

help:
	@echo 'make -C apps/imageeditor/penciltest INPUT=/path/to/video.mp4 [SETTING=value ...]'
	@echo 'Defaults: THRESHOLD=180 MIN_WIDTH=1.25 MIN_LENGTH=10 TEMPORAL=1'
	@echo 'Other settings: OUTPUT_DIR PROTECT_REGIONS FIXED_PROTECTION PALETTE FFMPEG PYTHON SIZE FPS MAX_FRAMES'
	@echo 'Color: cleaned ink on transparency; Pencil: original grayscale, visible beneath Color. Re-export replaces black.ptf.'
