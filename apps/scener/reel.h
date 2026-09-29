#ifndef __SCENER_REEL_H__
#define __SCENER_REEL_H__

/* Reels: programmable motion graphics rendered by Scener. A .reel XML file
   composes animated 3D scene layers with type, shapes, joint-anchored callouts,
   trails and measured readouts. Every attribute may be an expression of time,
   compiled once to bytecode, so any frame is a pure function of t.
   See docs/reels.md. */

#include <stdbool.h>
#include <stdint.h>
#include <orion/user/font_sdf.h>
#include "simplegl.h"

#define REEL_NAME 64
#define REEL_STACK 64

typedef struct reel_s reel_t;
typedef struct { float start, duration; } reel_shot_mark_t;

typedef struct {
	uint8_t *code; int ncode, ccode;
	float *k; int nk, ck;
	bool constant, set;
	float value;
} reel_expr_t;

enum { REEL_ANCHOR_X, REEL_ANCHOR_Y, REEL_ANCHOR_Z, REEL_ANCHOR_SX, REEL_ANCHOR_SY };

typedef struct {
	char instance[REEL_NAME], joint[REEL_NAME];
	int layer, track;        /* track: index into reel tracks, or -1 */
	bool fixed; float at;    /* sampled once at `at` (Inst.joint@at) */
	vec3 world;              /* renderer units */
	float value[5];          /* x y z (cm), sx sy (canvas px) */
} reel_anchor_t;

typedef struct { char name[REEL_NAME]; int ease; bool loop; float *t, *v; int n; } reel_curve_t;
typedef struct { char name[REEL_NAME]; font_sdf_t *font; unsigned texture; } reel_font_t;
typedef struct { char name[REEL_NAME]; int font; float size, tracking, weight, color[4]; bool tabular; } reel_style_t;

typedef struct {
	char src[1024], camera[MAX_CAMERA_NAME];
	Scene scene;
	float time;              /* scene time last evaluated */
	float rect[4];           /* canvas x y w h this frame */
	mat4 view, proj;
	unsigned fbo, color, depth; int tw, th;
	bool visible;            /* scene has been evaluated at least once */
	bool needed;             /* anchors read this layer, so it is evaluated on every frame even when hidden */
	char id[REEL_NAME];      /* <scene id> / <shot id>, the target of <point scene> */
	struct reel_node_s *node;
} reel_layer_t;

typedef struct { char name[REEL_NAME]; int layer; vec3 world; } reel_point_t; /* <point>: a fixed world position in a scene layer */

typedef enum {
	REEL_GROUP, REEL_SCENE, REEL_TEXT, REEL_RECT, REEL_CIRCLE, REEL_LINE, REEL_POLYLINE, REEL_TRAIL,
	REEL_LET, REEL_CHECK
} reel_kind_t;

enum { REEL_MOTION_POP, REEL_MOTION_SLAM, REEL_MOTION_ENTER, REEL_MOTION_LEAVE, REEL_MOTION_FADE_IN,
	REEL_MOTION_FADE_OUT, REEL_MOTION_PUNCH, REEL_MOTION_RISE, REEL_MOTIONS };
typedef struct { int kind; float a[4]; } reel_motion_t;

enum { REEL_OVER_EVERY, REEL_OVER_MAX, REEL_OVER_MIN };
enum { REEL_REVEAL_NONE, REEL_REVEAL_RISE, REEL_REVEAL_FADE, REEL_REVEAL_TYPE };

typedef struct { char *text; bool expr; reel_expr_t value; char format[16]; } reel_segment_t;

typedef struct reel_node_s {
	reel_kind_t kind; int line;
	reel_expr_t x, y, alpha, scale, rotation, from, to;
	float color[4];
	reel_motion_t *motions; int nmotions;
	struct reel_node_s **kids; int nkids, ckids;
	/* shapes */
	reel_expr_t w, h, radius, stroke, x2, y2;
	bool centered, has_clip; float clip[4];
	/* polylines, trails and scene layers */
	reel_expr_t *px, *py; int npts, cpts;
	int *anchors; int nanchors, canchors;
	float dash[2], dots, dot_fill[4]; bool has_dot_fill;
	int layer;
	reel_expr_t time;
	/* text */
	int style, align, valign, reveal;
	float at, stagger, exit;
	reel_segment_t *segments; int nsegments, csegments;
	/* lets and checks */
	int slot; char name[REEL_NAME];
	reel_expr_t value;
	bool has_min, has_max; float min, max; int over; /* REEL_OVER_*: every frame, or the timeline's max/min */
	float worst, worst_time; int failures; float first_failure;
} reel_node_t;

struct reel_s {
	char path[1024], dir[1024];
	int width, height, supersample;
	float fps, duration, poster, background[4];
	reel_node_t root;
	reel_layer_t *layers; int nlayers, clayers;
	reel_anchor_t *anchors; int nanchors, canchors;
	reel_curve_t *curves; int ncurves, ccurves;
	reel_font_t *fonts; int nfonts, cfonts;
	reel_style_t *styles; int nstyles, cstyles;
	reel_point_t *points; int npoints, cpoints;
	bool in_shot; int shot_layer; float shot_start, shot_duration, shot_cursor; /* <shot> build state: times inside a shot are shot-local */
	bool shots;
	reel_shot_mark_t *shot_marks; int nshot_marks, cshot_marks; /* one per <shot>, for contact sheets */
	char (*let_names)[REEL_NAME]; float *lets; int nlets;
	reel_node_t **let_nodes; int nlet_nodes, clet_nodes;
	reel_node_t **checks; int nchecks, cchecks;
	float *tracks; int ntracks, track_frames; /* xyz renderer units per tracked anchor per frame */
	bool has_trails;
	float t; int frame;
	void *gl;
	char error[512];
};

/* reel_expr.c */
bool reel_expr_compile(reel_t *r, const char *source, reel_expr_t *out);
float reel_expr_eval(const reel_expr_t *e, const reel_t *r);
void reel_expr_free(reel_expr_t *e);
enum {
	REEL_EASE_LINEAR, REEL_EASE_SMOOTH, REEL_EASE_STEP,
	REEL_EASE_IN_QUAD, REEL_EASE_OUT_QUAD, REEL_EASE_IN_OUT_QUAD,
	REEL_EASE_IN_CUBIC, REEL_EASE_OUT_CUBIC, REEL_EASE_IN_OUT_CUBIC,
	REEL_EASE_IN_QUART, REEL_EASE_OUT_QUART, REEL_EASE_IN_OUT_QUART,
	REEL_EASE_OUT_QUINT, REEL_EASE_IN_OUT_QUINT,
	REEL_EASE_IN_EXPO, REEL_EASE_OUT_EXPO, REEL_EASE_IN_OUT_EXPO,
	REEL_EASE_IN_SINE, REEL_EASE_OUT_SINE, REEL_EASE_IN_OUT_SINE,
	REEL_EASE_OUT_BACK, REEL_EASE_IN_OUT_BACK, REEL_EASE_OUT_ELASTIC, REEL_EASE_OUT_BOUNCE,
	REEL_EASES
};
float reel_ease(int ease, float x);
int reel_ease_find(const char *name);
float reel_spring(float dt, float response, float damping);

/* reel.c */
reel_t *reel_load(const char *path);
void reel_free(reel_t *r);
void reel_seek(reel_t *r, float t);
bool reel_sample(reel_t *r, FILE *report);  /* tracks + checks over the whole timeline */
int reel_frame_count(const reel_t *r);
void reel_project(const reel_layer_t *l, vec3 world, float *sx, float *sy);
bool reel_fail(reel_t *r, int line, const char *fmt, ...);
const char *reel_error(void); /* last load error as path:line: message */

/* reel_draw.c: GL rendering into an offscreen canvas. */
bool reel_gl_init(reel_t *r, int render_flags);
bool reel_gl_render(reel_t *r, float t, uint8_t *rgba_top_down);
void reel_gl_free(reel_t *r);

/* reel_video.c: H.264 MP4 through VideoToolbox (macOS). */
typedef struct reel_video_s reel_video_t;
reel_video_t *reel_video_open(const char *path, int width, int height, float fps);
bool reel_video_write(reel_video_t *v, const uint8_t *rgba_top_down);
bool reel_video_close(reel_video_t *v);

#endif
