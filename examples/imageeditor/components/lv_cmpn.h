#ifndef __IMAGEEDITOR_LEVELS_COMPONENT_COMMON_H__
#define __IMAGEEDITOR_LEVELS_COMPONENT_COMMON_H__

#include <stdint.h>

#include "../../../user/messages.h"

// Runtime class names exported by the imageeditor components plugin.
#define LV_GRAPH_CLASS_NAME "lv_histogram"
#define LV_STRIP_CLASS_NAME "lv_strip"

// Shared geometry constants used by both host dialog and components.
#define LV_GRAPH_W    260
#define LV_GRAPH_H     84

#define LV_STRIP_BAR_Y      4
#define LV_STRIP_HANDLE_Y   8
#define LV_STRIP_BAR_H      8
#define LV_STRIP_H         13

#define LV_TRACK_L         8
#define LV_TRACK_R       (LV_GRAPH_W - 8)

// Child window IDs used by the levels dialog.
#define LV_ID_GRAPH      1
#define LV_ID_OK         2
#define LV_ID_RESET      3
#define LV_ID_CANCEL     4
#define LV_ID_IN_BLACK   5
#define LV_ID_IN_GAMMA   6
#define LV_ID_IN_WHITE   7
#define LV_ID_OUT_BLACK  8
#define LV_ID_OUT_WHITE  9
#define LV_ID_IN_SLIDER 10
#define LV_ID_OUT_SLIDER 11
#define LV_ID_OUT_STRIP LV_ID_OUT_SLIDER
#define LV_ID_PREVIEW   12

// Notifications sent by levels controls to their parent with evCommand.
// LOWORD(wparam) is the control id.
#define lvStripChanged   0x8001u

#define LV_STRIP_INDEX_MIN 0u
#define LV_STRIP_INDEX_MID 1u
#define LV_STRIP_INDEX_MAX 2u

typedef struct {
	uint8_t black;
	uint8_t white;
	float gamma;
	uint32_t hist[256];
	uint32_t hist_max;
} lv_graph_data_t;

typedef struct {
	int sliders[2];
} lv_strip_data_t;

enum {
	lvGraphSetData = evUser + 300,
	lvStripSetData,
	lvStripGetValue,
};

#endif /* __IMAGEEDITOR_LEVELS_COMPONENT_COMMON_H__ */
