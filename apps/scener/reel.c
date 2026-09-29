/* Reel documents: XML loading, per-frame evaluation, sampled tracks and checks. */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <orion/kernel/kernel.h>
#include "reel.h"

#define REEL_FONT_BASE 64.0f
#define REEL_FONT_PADDING 8
#define REEL_FONT_ATLAS 2048
#define REEL_TIME_EPSILON 0.0001f

/* ── XML with text content, entities and line numbers ─────────────────── */

typedef struct reel_xml_s {
	char *tag, *text;
	char **names, **values; bool *used; int nattrs, cattrs;
	struct reel_xml_s **kids; int nkids, ckids;
	int line;
} reel_xml_t;

typedef struct { const char *p, *start; char *error; size_t errsz; } reel_xml_parser_t;

static void reel_xml_free(reel_xml_t *n) {
	if (!n) return;
	for (int i = 0; i < n->nattrs; i++) { free(n->names[i]); free(n->values[i]); }
	for (int i = 0; i < n->nkids; i++) reel_xml_free(n->kids[i]);
	free(n->names); free(n->values); free(n->used); free(n->kids); free(n->tag); free(n->text); free(n);
}

static int reel_xml_line(reel_xml_parser_t *x) {
	int line = 1;
	for (const char *c = x->start; c < x->p; c++) line += *c == '\n';
	return line;
}

/* Decodes entities in [s, e) into a new string. */
static char *reel_xml_decode(const char *s, const char *e) {
	char *out = malloc((size_t)(e - s) + 1), *o = out;
	while (s < e) {
		if (*s != '&') { *o++ = *s++; continue; }
		const char *semi = memchr(s, ';', (size_t)(e - s));
		if (!semi || semi - s > 10) { *o++ = *s++; continue; }
		static const struct { const char *name; char c; } entities[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}};
		bool done = false;
		for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]) && !done; i++)
			if (!strncmp(s, entities[i].name, strlen(entities[i].name))) { *o++ = entities[i].c; done = true; }
		if (!done && s[1] == '#') {
			unsigned long cp = s[2] == 'x' ? strtoul(s + 3, NULL, 16) : strtoul(s + 2, NULL, 10);
			if (cp < 0x80) *o++ = (char)cp;
			else if (cp < 0x800) { *o++ = (char)(0xC0 | (cp >> 6)); *o++ = (char)(0x80 | (cp & 0x3F)); }
			else if (cp < 0x10000) { *o++ = (char)(0xE0 | (cp >> 12)); *o++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *o++ = (char)(0x80 | (cp & 0x3F)); }
			else { *o++ = (char)(0xF0 | (cp >> 18)); *o++ = (char)(0x80 | ((cp >> 12) & 0x3F)); *o++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *o++ = (char)(0x80 | (cp & 0x3F)); }
			done = true;
		}
		if (done) s = semi + 1; else *o++ = *s++;
	}
	*o = 0;
	return out;
}

static void reel_xml_skip(reel_xml_parser_t *x) {
	for (;;) {
		while (isspace((unsigned char)*x->p)) x->p++;
		const char *end = NULL;
		if (!strncmp(x->p, "<!--", 4)) end = strstr(x->p, "-->"), end = end ? end + 3 : NULL;
		else if (!strncmp(x->p, "<?", 2)) end = strstr(x->p, "?>"), end = end ? end + 2 : NULL;
		else return;
		x->p = end ? end : x->p + strlen(x->p);
	}
}

static reel_xml_t *reel_xml_node(reel_xml_parser_t *x) {
	reel_xml_skip(x);
	if (*x->p != '<') { snprintf(x->error, x->errsz, "line %d: expected an element", reel_xml_line(x)); return NULL; }
	reel_xml_t *n = calloc(1, sizeof(*n));
	n->line = reel_xml_line(x);
	const char *s = ++x->p;
	while (*x->p && !isspace((unsigned char)*x->p) && *x->p != '>' && *x->p != '/') x->p++;
	n->tag = reel_xml_decode(s, x->p);
	for (;;) {
		while (isspace((unsigned char)*x->p)) x->p++;
		if (!strncmp(x->p, "/>", 2)) { x->p += 2; return n; }
		if (*x->p == '>') { x->p++; break; }
		s = x->p;
		while (*x->p && *x->p != '=' && !isspace((unsigned char)*x->p) && *x->p != '>' && *x->p != '/') x->p++;
		char *name = reel_xml_decode(s, x->p);
		while (isspace((unsigned char)*x->p)) x->p++;
		char quote = x->p[0] == '=' ? x->p[1] == ' ' ? 0 : x->p[1] : 0;
		if (!*name || (quote != '"' && quote != '\'')) {
			snprintf(x->error, x->errsz, "line %d: malformed attribute in <%s>", reel_xml_line(x), n->tag);
			free(name); reel_xml_free(n); return NULL;
		}
		x->p += 2; s = x->p;
		while (*x->p && *x->p != quote) x->p++;
		if (!*x->p) { snprintf(x->error, x->errsz, "line %d: unterminated attribute %s", n->line, name); free(name); reel_xml_free(n); return NULL; }
		char *value = reel_xml_decode(s, x->p++);
		n->names = realloc(n->names, sizeof(char *) * (size_t)(n->nattrs + 1));
		n->values = realloc(n->values, sizeof(char *) * (size_t)(n->nattrs + 1));
		n->used = realloc(n->used, sizeof(bool) * (size_t)(n->nattrs + 1));
		n->names[n->nattrs] = name; n->values[n->nattrs] = value; n->used[n->nattrs++] = false;
	}
	size_t text_len = 0;
	for (;;) {
		s = x->p;
		while (*x->p && *x->p != '<') x->p++;
		if (x->p > s) {
			char *chunk = reel_xml_decode(s, x->p);
			size_t add = strlen(chunk);
			n->text = realloc(n->text, text_len + add + 1);
			memcpy(n->text + text_len, chunk, add + 1); text_len += add;
			free(chunk);
		}
		if (!*x->p) { snprintf(x->error, x->errsz, "line %d: <%s> is not closed", n->line, n->tag); reel_xml_free(n); return NULL; }
		if (!strncmp(x->p, "<!--", 4)) { const char *e = strstr(x->p, "-->"); x->p = e ? e + 3 : x->p + strlen(x->p); continue; }
		if (!strncmp(x->p, "</", 2)) {
			x->p += 2; s = x->p;
			while (*x->p && *x->p != '>') x->p++;
			if ((size_t)(x->p - s) != strlen(n->tag) || strncmp(s, n->tag, strlen(n->tag))) {
				snprintf(x->error, x->errsz, "line %d: </%.*s> closes <%s>", reel_xml_line(x), (int)(x->p - s), s, n->tag);
				reel_xml_free(n); return NULL;
			}
			if (*x->p) x->p++;
			return n;
		}
		reel_xml_t *kid = reel_xml_node(x);
		if (!kid) { reel_xml_free(n); return NULL; }
		DA_PUSH(n->kids, n->nkids, n->ckids, kid);
	}
}

static const char *reel_attr(reel_xml_t *n, const char *name) {
	for (int i = 0; i < n->nattrs; i++) if (!strcmp(n->names[i], name)) { n->used[i] = true; return n->values[i]; }
	return NULL;
}

/* ── Errors ───────────────────────────────────────────────────────────── */

static char reel_last_error[512];
const char *reel_error(void) { return reel_last_error; }

bool reel_fail(reel_t *r, int line, const char *fmt, ...) {
	char message[512];
	va_list ap; va_start(ap, fmt); vsnprintf(message, sizeof(message), fmt, ap); va_end(ap);
	fprintf(stderr, "[reel] %s:%d: %s\n", r->path, line, message);
	fflush(stderr);
	snprintf(r->error, sizeof(r->error), "%s", message);
	snprintf(reel_last_error, sizeof(reel_last_error), "%s:%d: %s", r->path, line, message);
	return false;
}

static bool reel_expr_attr(reel_t *r, reel_xml_t *x, const char *name, reel_expr_t *e, const char *fallback) {
	const char *source = reel_attr(x, name);
	if (!source) source = fallback;
	if (!source) return true;
	if (!reel_expr_compile(r, source, e)) return reel_fail(r, x->line, "<%s %s>: %s", x->tag, name, r->error);
	return true;
}

/* A number that must not depend on time. */
static bool reel_const_attr(reel_t *r, reel_xml_t *x, const char *name, float *out, float fallback) {
	reel_expr_t e = {0};
	*out = fallback;
	if (!reel_expr_attr(r, x, name, &e, NULL)) return false;
	if (e.set && !e.constant) { reel_expr_free(&e); return reel_fail(r, x->line, "<%s %s> must be constant", x->tag, name); }
	if (e.set) *out = e.value;
	reel_expr_free(&e);
	return true;
}

static bool reel_color_attr(reel_t *r, reel_xml_t *x, const char *name, float out[4], const float *fallback) {
	const char *v = reel_attr(x, name);
	if (!v) { if (fallback) memcpy(out, fallback, sizeof(float) * 4); return true; }
	size_t n = strlen(v);
	unsigned long hex;
	char *end;
	if (v[0] != '#' || (n != 7 && n != 9) || (hex = strtoul(v + 1, &end, 16), *end))
		return reel_fail(r, x->line, "<%s %s=\"%s\"> must be #RRGGBB or #RRGGBBAA", x->tag, name, v);
	if (n == 7) hex = (hex << 8) | 0xFF;
	for (int i = 0; i < 4; i++) out[i] = (float)((hex >> (24 - 8 * i)) & 0xFF) / 255.0f;
	return true;
}

static bool reel_check_unused(reel_t *r, reel_xml_t *x) {
	for (int i = 0; i < x->nattrs; i++)
		if (!x->used[i]) return reel_fail(r, x->line, "<%s> does not support attribute %s", x->tag, x->names[i]);
	return true;
}

/* ── Definitions ──────────────────────────────────────────────────────── */

static bool reel_path(const reel_t *r, const char *src, char *out, size_t size) {
	if (src[0] == '/') snprintf(out, size, "%s", src);
	else snprintf(out, size, "%s/%s", r->dir, src);
	FILE *f = fopen(out, "rb");
	if (f) { fclose(f); return true; }
	return false;
}

static int reel_font_add(reel_t *r, int line, const char *name, const char *src) {
	char path[2048];
	if (!reel_path(r, src, path, sizeof(path))) {
		const char *exe = ui_get_exe_dir();
		snprintf(path, sizeof(path), "%s/../share/orion/fonts/%s", exe, src);
		FILE *f = fopen(path, "rb");
		if (!f) return reel_fail(r, line, "font %s not found next to the reel or in share/orion/fonts", src), -1;
		fclose(f);
	}
	reel_font_t font = {0};
	snprintf(font.name, sizeof(font.name), "%s", name);
	font.font = font_sdf_create(path, REEL_FONT_BASE, REEL_FONT_PADDING, REEL_FONT_ATLAS);
	if (!font.font) return reel_fail(r, line, "cannot load font %s", path), -1;
	DA_PUSH(r->fonts, r->nfonts, r->cfonts, font);
	return r->nfonts - 1;
}

static int reel_font_find(reel_t *r, const char *name) {
	for (int i = 0; i < r->nfonts; i++) if (!strcmp(r->fonts[i].name, name)) return i;
	static const struct { const char *name, *file; } defaults[] = {
		{"sans", "NotoSans-Regular.ttf"}, {"sans-medium", "NotoSans-Medium.ttf"}, {"mono", "monoid.ttf"}};
	for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
		if (!strcmp(defaults[i].name, name)) return reel_font_add(r, r->root.line, name, defaults[i].file);
	return -1;
}

static bool reel_define(reel_t *r, reel_xml_t *x) {
	if (!strcmp(x->tag, "font")) {
		const char *name = reel_attr(x, "name"), *src = reel_attr(x, "src");
		if (!name || !src) return reel_fail(r, x->line, "<font> needs name and src");
		return reel_font_add(r, x->line, name, src) >= 0 && reel_check_unused(r, x);
	}
	if (!strcmp(x->tag, "style")) {
		reel_style_t st = {0};
		const char *name = reel_attr(x, "name"), *font = reel_attr(x, "font");
		if (!name) return reel_fail(r, x->line, "<style> needs a name");
		snprintf(st.name, sizeof(st.name), "%s", name);
		st.font = reel_font_find(r, font ? font : "sans");
		if (st.font < 0) return reel_fail(r, x->line, "<style %s> uses unknown font %s", name, font);
		static const float white[4] = {1, 1, 1, 1};
		const char *tabular = reel_attr(x, "digits");
		st.tabular = tabular && !strcmp(tabular, "tabular");
		if (tabular && !st.tabular && strcmp(tabular, "proportional")) return reel_fail(r, x->line, "<style digits> is tabular or proportional");
		if (!reel_const_attr(r, x, "size", &st.size, 16) || !reel_const_attr(r, x, "tracking", &st.tracking, 0) ||
		    !reel_const_attr(r, x, "weight", &st.weight, 0) || !reel_color_attr(r, x, "color", st.color, white)) return false;
		if (st.size <= 0) return reel_fail(r, x->line, "<style %s> size must be positive", name);
		DA_PUSH(r->styles, r->nstyles, r->cstyles, st);
		return reel_check_unused(r, x);
	}
	if (!strcmp(x->tag, "curve")) {
		reel_curve_t c = {0};
		const char *name = reel_attr(x, "name"), *ease = reel_attr(x, "ease"), *loop = reel_attr(x, "loop");
		if (!name) return reel_fail(r, x->line, "<curve> needs a name");
		snprintf(c.name, sizeof(c.name), "%s", name);
		c.ease = reel_ease_find(ease ? ease : "smooth");
		if (c.ease < 0) return reel_fail(r, x->line, "<curve %s> has unknown ease %s", name, ease);
		c.loop = loop && !strcmp(loop, "1");
		for (const char *p = x->text ? x->text : ""; *p;) {
			while (isspace((unsigned char)*p) || *p == ',') p++;
			if (!*p) break;
			char *end; float t = strtof(p, &end), v;
			if (end == p || *end != ':') return reel_fail(r, x->line, "<curve %s> keys are time:value pairs", name);
			p = end + 1; v = strtof(p, &end);
			if (end == p) return reel_fail(r, x->line, "<curve %s> key at %g has no value", name, t);
			if (c.n && t <= c.t[c.n - 1]) return reel_fail(r, x->line, "<curve %s> key times must increase", name);
			c.t = realloc(c.t, sizeof(float) * (size_t)(c.n + 1)); c.v = realloc(c.v, sizeof(float) * (size_t)(c.n + 1));
			c.t[c.n] = t; c.v[c.n++] = v; p = end;
		}
		if (!c.n) return reel_fail(r, x->line, "<curve %s> has no keys", name);
		DA_PUSH(r->curves, r->ncurves, r->ccurves, c);
		return reel_check_unused(r, x);
	}
	return true;
}

static bool reel_load_layers(reel_t *r, reel_xml_t *x) {
	if (!strcmp(x->tag, "scene")) {
		const char *src = reel_attr(x, "src"), *camera = reel_attr(x, "camera");
		if (!src) return reel_fail(r, x->line, "<scene> needs src");
		reel_layer_t layer = {0};
		if (!reel_path(r, src, layer.src, sizeof(layer.src))) return reel_fail(r, x->line, "scene %s not found", layer.src);
		if (!load_scene(layer.src, &layer.scene)) return reel_fail(r, x->line, "cannot load scene %s", layer.src);
		if (camera) {
			bool found = false;
			for (int i = 0; i < layer.scene.ncameras; i++) found |= !strcmp(layer.scene.cameras[i].name, camera);
			if (!found) { scene_free(&layer.scene); return reel_fail(r, x->line, "scene %s has no camera %s", src, camera); }
			snprintf(layer.camera, sizeof(layer.camera), "%s", camera);
			scene_select_camera(&layer.scene, camera);
		}
		scene_set_time(&layer.scene, 0);
		DA_PUSH(r->layers, r->nlayers, r->clayers, layer);
		/* Attributes are read again when the node is built. */
		for (int i = 0; i < x->nattrs; i++) x->used[i] = false;
	}
	for (int i = 0; i < x->nkids; i++) if (!reel_load_layers(r, x->kids[i])) return false;
	return true;
}

/* ── Nodes ────────────────────────────────────────────────────────────── */

static const struct { const char *name; int kind, args; float defaults[4]; } reel_motion_table[] = {
	{"pop",     REEL_MOTION_POP,      3, {0, 0.43f, 0.5f, 0}},
	{"slam",    REEL_MOTION_SLAM,     2, {0, 1.75f, 0, 0}},
	{"enter",   REEL_MOTION_ENTER,    3, {0, 0, 0, 0}},
	{"leave",   REEL_MOTION_LEAVE,    4, {0, 0.3f, 0, 0}},
	{"fadeIn",  REEL_MOTION_FADE_IN,  2, {0, 0.12f, 0, 0}},
	{"fadeOut", REEL_MOTION_FADE_OUT, 2, {0, 0.12f, 0, 0}},
	{"punch",   REEL_MOTION_PUNCH,    2, {0, 0.028f, 0, 0}},
	{"rise",    REEL_MOTION_RISE,     3, {0, 24, 0.6f, 0}},
};

/* motion="pop(1.2) fadeOut(5.5, 0.3)": calls with constant arguments. */
static bool reel_motion_attr(reel_t *r, reel_xml_t *x, reel_node_t *n) {
	const char *p = reel_attr(x, "motion");
	while (p && *p) {
		while (isspace((unsigned char)*p) || *p == ',') p++;
		if (!*p) break;
		const char *s = p;
		while (isalpha((unsigned char)*p)) p++;
		int m = -1;
		for (size_t i = 0; i < sizeof(reel_motion_table) / sizeof(reel_motion_table[0]); i++)
			if (strlen(reel_motion_table[i].name) == (size_t)(p - s) && !strncmp(reel_motion_table[i].name, s, (size_t)(p - s))) m = (int)i;
		if (m < 0 || *p != '(') return reel_fail(r, x->line, "unknown motion %.*s()", (int)(p - s), s);
		reel_motion_t motion = {reel_motion_table[m].kind, {0}};
		memcpy(motion.a, reel_motion_table[m].defaults, sizeof(motion.a));
		int nargs = 0, depth = 0;
		const char *arg = ++p;
		for (;; p++) {
			if (!*p) return reel_fail(r, x->line, "motion %s( is not closed", reel_motion_table[m].name);
			if (*p == '(') depth++;
			if ((*p == ',' || *p == ')') && !depth) {
				char source[256];
				snprintf(source, sizeof(source), "%.*s", (int)(p - arg), arg);
				bool blank = true;
				for (char *c = source; *c; c++) blank &= isspace((unsigned char)*c) != 0;
				if (!blank) {
					if (nargs >= reel_motion_table[m].args) return reel_fail(r, x->line, "%s() takes at most %d arguments", reel_motion_table[m].name, reel_motion_table[m].args);
					reel_expr_t e = {0};
					if (!reel_expr_compile(r, source, &e)) return reel_fail(r, x->line, "motion %s(): %s", reel_motion_table[m].name, r->error);
					if (!e.constant) { reel_expr_free(&e); return reel_fail(r, x->line, "motion %s() arguments must be constant", reel_motion_table[m].name); }
					motion.a[nargs++] = e.value;
					reel_expr_free(&e);
				}
				if (*p == ')') { p++; break; }
				arg = p + 1;
			} else if (*p == ')') depth--;
		}
		if (!nargs) return reel_fail(r, x->line, "motion %s() needs a start time", reel_motion_table[m].name);
		n->motions = realloc(n->motions, sizeof(reel_motion_t) * (size_t)(n->nmotions + 1));
		n->motions[n->nmotions++] = motion;
	}
	return true;
}

static bool reel_text_segments(reel_t *r, reel_xml_t *x, reel_node_t *n) {
	const char *src = x->text ? x->text : "";
	char *collapsed = malloc(strlen(src) + 1), *o = collapsed;
	/* Line breaks and indentation fold into one space; runs of spaces are kept. */
	for (const char *p = src; *p;) {
		const char *run = p;
		bool folds = false;
		while (isspace((unsigned char)*p)) folds |= *p++ != ' ';
		if (p == run) { *o++ = *p++; continue; }
		if (o == collapsed || !*p) continue;
		if (folds) *o++ = ' ';
		else { memcpy(o, run, (size_t)(p - run)); o += p - run; }
	}
	while (o > collapsed && o[-1] == ' ') o--;
	*o = 0;
	char *literal = malloc(strlen(collapsed) + 1); size_t nl = 0;
	for (const char *p = collapsed;; p++) {
		bool open = *p == '{' && p[1] != '{', end = !*p;
		if (*p == '{' && p[1] == '{') { literal[nl++] = '{'; p++; continue; }
		if (*p == '}' && p[1] == '}') { literal[nl++] = '}'; p++; continue; }
		if ((open || end) && nl) {
			reel_segment_t seg = {0};
			seg.text = malloc(nl + 1); memcpy(seg.text, literal, nl); seg.text[nl] = 0; nl = 0;
			DA_PUSH(n->segments, n->nsegments, n->csegments, seg);
		}
		if (end) break;
		if (!open) { literal[nl++] = *p; continue; }
		const char *close = strchr(p, '}');
		if (!close) { free(literal); free(collapsed); return reel_fail(r, x->line, "<text> has an unclosed {"); }
		char source[512];
		snprintf(source, sizeof(source), "%.*s", (int)(close - p - 1), p + 1);
		reel_segment_t seg = {0};
		seg.expr = true;
		snprintf(seg.format, sizeof(seg.format), "%%g");
		char *colon = strrchr(source, ':');
		if (colon && colon[1] == '%') {
			*colon = 0;
			const char *f = colon + 1, *c = f + 1;
			while (strchr("-+ 0#", *c) && *c) c++;
			while (isdigit((unsigned char)*c) || *c == '.') c++;
			if (!strchr("fgeGE", *c) || !*c || c[1] || strlen(f) >= sizeof(seg.format)) {
				free(literal); free(collapsed);
				return reel_fail(r, x->line, "<text> format %s must be one floating-point conversion", f);
			}
			snprintf(seg.format, sizeof(seg.format), "%s", f);
		}
		if (!reel_expr_compile(r, source, &seg.value)) { free(literal); free(collapsed); return reel_fail(r, x->line, "<text> {%s}: %s", source, r->error); }
		DA_PUSH(n->segments, n->nsegments, n->csegments, seg);
		p = close;
	}
	free(literal); free(collapsed);
	return true;
}

static bool reel_anchor_list(reel_t *r, reel_xml_t *x, const char *list, reel_node_t *n) {
	char buffer[1024]; snprintf(buffer, sizeof(buffer), "%s", list);
	for (char *save = NULL, *tok = strtok_r(buffer, " \t\n,", &save); tok; tok = strtok_r(NULL, " \t\n,", &save)) {
		/* Anchors are compiled as expressions to share name parsing. */
		char source[256]; reel_expr_t e = {0};
		snprintf(source, sizeof(source), "%s.sx", tok);
		char *at = strchr(tok, '@');
		if (at) snprintf(source, sizeof(source), "%.*s.sx%s", (int)(at - tok), tok, at);
		if (!reel_expr_compile(r, source, &e)) return reel_fail(r, x->line, "<%s> anchor %s: %s", x->tag, tok, r->error);
		int index = e.code[1] | (e.code[2] << 8);
		reel_expr_free(&e);
		DA_PUSH(n->anchors, n->nanchors, n->canchors, index);
	}
	return true;
}

static reel_node_t *reel_build(reel_t *r, reel_xml_t *x);

static bool reel_build_kids(reel_t *r, reel_xml_t *x, reel_node_t *n) {
	for (int i = 0; i < x->nkids; i++) {
		reel_xml_t *k = x->kids[i];
		if (!strcmp(k->tag, "font") || !strcmp(k->tag, "style") || !strcmp(k->tag, "curve")) {
			if (!reel_define(r, k)) return false;
			continue;
		}
		reel_node_t *kid = reel_build(r, k);
		if (!kid) return false;
		DA_PUSH(n->kids, n->nkids, n->ckids, kid);
	}
	return true;
}

static void reel_node_free(reel_node_t *n);

static reel_node_t *reel_build(reel_t *r, reel_xml_t *x) {
	static const struct { const char *tag; reel_kind_t kind; } kinds[] = {
		{"group", REEL_GROUP}, {"scene", REEL_SCENE}, {"text", REEL_TEXT}, {"rect", REEL_RECT}, {"circle", REEL_CIRCLE},
		{"line", REEL_LINE}, {"polyline", REEL_POLYLINE}, {"trail", REEL_TRAIL}, {"let", REEL_LET}, {"check", REEL_CHECK}};
	int kind = -1;
	for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) if (!strcmp(kinds[i].tag, x->tag)) kind = (int)kinds[i].kind;
	if (kind < 0) return reel_fail(r, x->line, "unknown element <%s>", x->tag), NULL;
	reel_node_t *n = calloc(1, sizeof(*n));
	n->kind = (reel_kind_t)kind; n->line = x->line; n->layer = -1;
	static const float white[4] = {1, 1, 1, 1};
	bool ok = true;
#define REEL_TRY(expr) do { if (ok && !(expr)) ok = false; } while (0)
	if (kind == REEL_LET || kind == REEL_CHECK) {
		const char *name = reel_attr(x, "name");
		if (!name) { reel_fail(r, x->line, "<%s> needs a name", x->tag); reel_node_free(n); return NULL; }
		snprintf(n->name, sizeof(n->name), "%s", name);
		REEL_TRY(reel_expr_attr(r, x, "value", &n->value, NULL));
		if (ok && !n->value.set) ok = reel_fail(r, x->line, "<%s %s> needs a value", x->tag, name);
		if (kind == REEL_LET && ok) {
			r->let_names = realloc(r->let_names, sizeof(*r->let_names) * (size_t)(r->nlets + 1));
			r->lets = realloc(r->lets, sizeof(float) * (size_t)(r->nlets + 1));
			snprintf(r->let_names[r->nlets], REEL_NAME, "%s", name);
			n->slot = r->nlets++;
			r->lets[n->slot] = reel_expr_eval(&n->value, r);
			DA_PUSH(r->let_nodes, r->nlet_nodes, r->clet_nodes, n);
		}
		if (kind == REEL_CHECK && ok) {
			n->has_min = reel_attr(x, "min") != NULL; n->has_max = reel_attr(x, "max") != NULL;
			REEL_TRY(reel_const_attr(r, x, "min", &n->min, 0));
			REEL_TRY(reel_const_attr(r, x, "max", &n->max, 0));
			if (ok && !n->has_min && !n->has_max) ok = reel_fail(r, x->line, "<check %s> needs min or max", name);
			const char *over = reel_attr(x, "over");
			n->over = !over || !strcmp(over, "every") ? REEL_OVER_EVERY : !strcmp(over, "max") ? REEL_OVER_MAX : !strcmp(over, "min") ? REEL_OVER_MIN : -1;
			if (ok && n->over < 0) ok = reel_fail(r, x->line, "<check %s over> is every, max or min", name);
			DA_PUSH(r->checks, r->nchecks, r->cchecks, n);
		}
		if (ok) ok = reel_check_unused(r, x);
		if (!ok) { reel_node_free(n); return NULL; }
		return n;
	}
	REEL_TRY(reel_expr_attr(r, x, "x", &n->x, "0"));
	REEL_TRY(reel_expr_attr(r, x, "y", &n->y, "0"));
	REEL_TRY(reel_expr_attr(r, x, "alpha", &n->alpha, "1"));
	REEL_TRY(reel_expr_attr(r, x, "scale", &n->scale, "1"));
	REEL_TRY(reel_expr_attr(r, x, "rotation", &n->rotation, "0"));
	REEL_TRY(reel_expr_attr(r, x, "from", &n->from, NULL));
	REEL_TRY(reel_expr_attr(r, x, "to", &n->to, NULL));
	REEL_TRY(reel_color_attr(r, x, "color", n->color, white));
	REEL_TRY(reel_motion_attr(r, x, n));
	switch (n->kind) {
	case REEL_GROUP: {
		const char *clip = reel_attr(x, "clip");
		if (clip && ok) {
			n->has_clip = sscanf(clip, "%f %f %f %f", &n->clip[0], &n->clip[1], &n->clip[2], &n->clip[3]) == 4;
			if (!n->has_clip) ok = reel_fail(r, x->line, "<group clip> is \"x y width height\"");
		}
		REEL_TRY(reel_build_kids(r, x, n));
		break;
	}
	case REEL_SCENE: {
		reel_attr(x, "src"); reel_attr(x, "camera");
		for (int i = 0; i < r->nlayers; i++) if (!r->layers[i].node) { n->layer = i; r->layers[i].node = n; break; }
		REEL_TRY(reel_expr_attr(r, x, "width", &n->w, "width"));
		REEL_TRY(reel_expr_attr(r, x, "height", &n->h, "height"));
		REEL_TRY(reel_expr_attr(r, x, "time", &n->time, "t"));
		break;
	}
	case REEL_TEXT: {
		const char *style = reel_attr(x, "style"), *align = reel_attr(x, "align"), *valign = reel_attr(x, "valign"), *reveal = reel_attr(x, "reveal");
		n->style = -1;
		for (int i = 0; i < r->nstyles; i++) if (style && !strcmp(r->styles[i].name, style)) n->style = i;
		if (n->style < 0 && ok) ok = reel_fail(r, x->line, "<text> needs a defined style (got %s)", style ? style : "none");
		static const char *const aligns[] = {"left", "center", "right"}, *const valigns[] = {"baseline", "top", "middle", "bottom"},
			*const reveals[] = {"none", "rise", "fade", "type"};
		n->align = n->valign = n->reveal = -1;
		for (int i = 0; i < 3; i++) if (!strcmp(align ? align : "left", aligns[i])) n->align = i;
		for (int i = 0; i < 4; i++) if (!strcmp(valign ? valign : "baseline", valigns[i])) n->valign = i;
		for (int i = 0; i < 4; i++) if (!strcmp(reveal ? reveal : "none", reveals[i])) n->reveal = i;
		if (ok && (n->align < 0 || n->valign < 0 || n->reveal < 0)) ok = reel_fail(r, x->line, "<text> align is left|center|right, valign baseline|top|middle|bottom, reveal none|rise|fade|type");
		if (ok && !reel_attr(x, "color")) memcpy(n->color, r->styles[n->style].color, sizeof(n->color));
		REEL_TRY(reel_const_attr(r, x, "at", &n->at, 0));
		REEL_TRY(reel_const_attr(r, x, "stagger", &n->stagger, n->reveal == REEL_REVEAL_TYPE ? 0.03f : 0.07f));
		REEL_TRY(reel_const_attr(r, x, "exit", &n->exit, -1));
		REEL_TRY(reel_text_segments(r, x, n));
		break;
	}
	case REEL_RECT: {
		const char *origin = reel_attr(x, "origin");
		n->centered = origin && !strcmp(origin, "center");
		if (origin && !n->centered && strcmp(origin, "topleft") && ok) ok = reel_fail(r, x->line, "<rect origin> is topleft or center");
		REEL_TRY(reel_expr_attr(r, x, "width", &n->w, "0"));
		REEL_TRY(reel_expr_attr(r, x, "height", &n->h, "0"));
		REEL_TRY(reel_expr_attr(r, x, "radius", &n->radius, "0"));
		REEL_TRY(reel_expr_attr(r, x, "stroke", &n->stroke, "0"));
		break;
	}
	case REEL_CIRCLE:
		REEL_TRY(reel_expr_attr(r, x, "radius", &n->radius, "8"));
		REEL_TRY(reel_expr_attr(r, x, "stroke", &n->stroke, "0"));
		break;
	case REEL_LINE: case REEL_POLYLINE: case REEL_TRAIL: {
		const char *dash = reel_attr(x, "dash");
		if (dash && ok && (sscanf(dash, "%f %f", &n->dash[0], &n->dash[1]) != 2 || n->dash[0] < 0 || n->dash[1] <= 0))
			ok = reel_fail(r, x->line, "<%s dash> is \"on off\" in px", x->tag);
		REEL_TRY(reel_expr_attr(r, x, "width", &n->w, "2"));
		if (n->kind == REEL_LINE) {
			n->px = calloc(2, sizeof(reel_expr_t)); n->py = calloc(2, sizeof(reel_expr_t)); n->npts = n->cpts = 2;
			REEL_TRY(reel_expr_attr(r, x, "x1", &n->px[0], "0")); REEL_TRY(reel_expr_attr(r, x, "y1", &n->py[0], "0"));
			REEL_TRY(reel_expr_attr(r, x, "x2", &n->px[1], "0")); REEL_TRY(reel_expr_attr(r, x, "y2", &n->py[1], "0"));
		} else if (n->kind == REEL_POLYLINE) {
			const char *anchors = reel_attr(x, "anchors");
			if (anchors && ok) ok = reel_anchor_list(r, x, anchors, n);
			for (int i = 0; ok && i < x->nkids; i++) {
				reel_xml_t *pt = x->kids[i];
				if (strcmp(pt->tag, "pt")) { ok = reel_fail(r, pt->line, "<polyline> contains <pt x y/> points"); break; }
				reel_expr_t px = {0}, py = {0};
				ok = reel_expr_attr(r, pt, "x", &px, "0") && reel_expr_attr(r, pt, "y", &py, "0") && reel_check_unused(r, pt);
				n->px = realloc(n->px, sizeof(reel_expr_t) * (size_t)(n->npts + 1));
				n->py = realloc(n->py, sizeof(reel_expr_t) * (size_t)(n->npts + 1));
				n->px[n->npts] = px; n->py[n->npts++] = py; n->cpts = n->npts;
			}
			if (ok && n->nanchors && n->npts) ok = reel_fail(r, x->line, "<polyline> takes anchors or <pt> points, not both");
			if (ok && n->nanchors + n->npts < 2) ok = reel_fail(r, x->line, "<polyline> needs at least two points");
			REEL_TRY(reel_const_attr(r, x, "dots", &n->dots, 0));
			n->has_dot_fill = reel_attr(x, "dotFill") != NULL;
			REEL_TRY(reel_color_attr(r, x, "dotFill", n->dot_fill, NULL));
		} else {
			const char *anchor = reel_attr(x, "anchor");
			if (!anchor && ok) ok = reel_fail(r, x->line, "<trail> needs an anchor");
			if (anchor && ok) ok = reel_anchor_list(r, x, anchor, n);
			if (ok && (n->nanchors != 1 || r->anchors[n->anchors[0]].fixed)) ok = reel_fail(r, x->line, "<trail> follows one live joint");
			if (ok && r->anchors[n->anchors[0]].track < 0) r->anchors[n->anchors[0]].track = r->ntracks++;
			REEL_TRY(reel_expr_attr(r, x, "start", &n->x2, "0"));
			REEL_TRY(reel_expr_attr(r, x, "end", &n->y2, "t"));
			r->has_trails = true;
		}
		break;
	}
	default: break;
	}
#undef REEL_TRY
	if (ok && n->kind != REEL_GROUP && n->kind != REEL_POLYLINE && x->nkids) ok = reel_fail(r, x->line, "<%s> has no child elements", x->tag);
	if (ok) ok = reel_check_unused(r, x);
	if (!ok) { reel_node_free(n); return NULL; }
	return n;
}

static void reel_node_release(reel_node_t *n) {
	reel_expr_t *exprs[] = {&n->x, &n->y, &n->alpha, &n->scale, &n->rotation, &n->from, &n->to, &n->w, &n->h,
		&n->radius, &n->stroke, &n->x2, &n->y2, &n->time, &n->value};
	for (size_t i = 0; i < sizeof(exprs) / sizeof(exprs[0]); i++) reel_expr_free(exprs[i]);
	for (int i = 0; i < n->npts; i++) { reel_expr_free(&n->px[i]); reel_expr_free(&n->py[i]); }
	for (int i = 0; i < n->nsegments; i++) { free(n->segments[i].text); reel_expr_free(&n->segments[i].value); }
	for (int i = 0; i < n->nkids; i++) reel_node_free(n->kids[i]);
	free(n->px); free(n->py); free(n->segments); free(n->anchors); free(n->motions); free(n->kids);
}

static void reel_node_free(reel_node_t *n) {
	if (!n) return;
	reel_node_release(n);
	free(n);
}

/* ── Loading ──────────────────────────────────────────────────────────── */

reel_t *reel_load(const char *path) {
	reel_t *r = calloc(1, sizeof(*r));
	snprintf(r->path, sizeof(r->path), "%s", path);
	snprintf(r->dir, sizeof(r->dir), "%s", path);
	char *slash = strrchr(r->dir, '/');
	if (slash) *slash = 0; else strcpy(r->dir, ".");
	FILE *f = fopen(path, "rb");
	if (!f) { reel_fail(r, 0, "cannot open reel"); reel_free(r); return NULL; }
	fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
	char *text = malloc((size_t)size + 1);
	size_t got = fread(text, 1, (size_t)size, f); text[got] = 0; fclose(f);
	char error[256] = {0};
	reel_xml_parser_t parser = {text, text, error, sizeof(error)};
	reel_xml_skip(&parser);
	reel_xml_t *x = reel_xml_node(&parser);
	free(text);
	if (!x) { reel_fail(r, 0, "%s", error); reel_free(r); return NULL; }
	bool ok = !strcmp(x->tag, "reel") || reel_fail(r, x->line, "root element must be <reel>, not <%s>", x->tag);
	float w = 1920, h = 1080;
	static const float black[4] = {0, 0, 0, 1};
	r->root.kind = REEL_GROUP;
	ok = ok && reel_const_attr(r, x, "width", &w, 1920) && reel_const_attr(r, x, "height", &h, 1080) &&
		reel_const_attr(r, x, "fps", &r->fps, 30) && reel_const_attr(r, x, "duration", &r->duration, 1) &&
		reel_const_attr(r, x, "poster", &r->poster, 0) && reel_color_attr(r, x, "background", r->background, black);
	float ss = 2;
	ok = ok && reel_const_attr(r, x, "supersample", &ss, 2);
	r->width = (int)w; r->height = (int)h; r->supersample = (int)ss;
	if (ok && (r->width < 16 || r->height < 16 || r->width > 8192 || r->height > 8192 || r->width % 2 || r->height % 2))
		ok = reel_fail(r, x->line, "<reel> width and height must be even, 16..8192");
	if (ok && (r->fps <= 0 || r->fps > 240 || r->duration <= 0 || r->supersample < 1 || r->supersample > 4))
		ok = reel_fail(r, x->line, "<reel> needs fps 0..240, a positive duration and supersample 1..4");
	ok = ok && reel_check_unused(r, x) && reel_load_layers(r, x) && reel_build_kids(r, x, &r->root);
	reel_xml_free(x);
	if (!ok) { reel_free(r); return NULL; }
	for (int i = 0; i < r->nlayers; i++) r->layers[i].time = 0;
	reel_seek(r, 0);
	return r;
}

void reel_free(reel_t *r) {
	if (!r) return;
	reel_node_release(&r->root);
	for (int i = 0; i < r->nlayers; i++) scene_free(&r->layers[i].scene);
	for (int i = 0; i < r->nfonts; i++) font_sdf_destroy(r->fonts[i].font);
	for (int i = 0; i < r->ncurves; i++) { free(r->curves[i].t); free(r->curves[i].v); }
	free(r->layers); free(r->anchors); free(r->curves); free(r->fonts); free(r->styles);
	free(r->let_names); free(r->lets); free(r->let_nodes); free(r->checks); free(r->tracks);
	free(r);
}

/* ── Evaluation ───────────────────────────────────────────────────────── */

static void reel_layer_camera(reel_layer_t *l) {
	Scene *s = &l->scene;
	float aspect = l->rect[3] > 0 ? l->rect[2] / l->rect[3] : 1;
	l->view = mat4_lookat(s->camPos, s->camLook, s->worldUp);
	l->proj = mat4_perspective(s->camFov > 0 ? s->camFov : 60, aspect, SCENER_NEAR, SCENER_FAR);
}

void reel_project(const reel_layer_t *l, vec3 world, float *sx, float *sy) {
	mat4 vp = mat4_mul(l->proj, l->view);
	const float *m = vp.m;
	float cx = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
	float cy = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
	float cw = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
	if (fabsf(cw) < 1e-6f) cw = 1e-6f;
	*sx = l->rect[0] + (cx / cw * 0.5f + 0.5f) * l->rect[2];
	*sy = l->rect[1] + (0.5f - cy / cw * 0.5f) * l->rect[3];
}

void reel_seek(reel_t *r, float t) {
	r->t = t;
	r->frame = (int)floorf(t * r->fps + REEL_TIME_EPSILON);
	for (int i = 0; i < r->nlayers; i++) {
		reel_layer_t *l = &r->layers[i];
		reel_node_t *n = l->node;
		float lt = n ? reel_expr_eval(&n->time, r) : t;
		if (lt != l->time || !l->visible) { scene_set_time(&l->scene, lt); l->time = lt; }
		l->visible = true;
		if (n) {
			l->rect[0] = reel_expr_eval(&n->x, r); l->rect[1] = reel_expr_eval(&n->y, r);
			l->rect[2] = reel_expr_eval(&n->w, r); l->rect[3] = reel_expr_eval(&n->h, r);
		} else {
			l->rect[0] = l->rect[1] = 0; l->rect[2] = (float)r->width; l->rect[3] = (float)r->height;
		}
		reel_layer_camera(l);
	}
	for (int i = 0; i < r->nanchors; i++) {
		reel_anchor_t *a = &r->anchors[i];
		reel_layer_t *l = &r->layers[a->layer];
		if (!a->fixed && scene_joint_position(&l->scene, a->instance, a->joint, &a->world))
			for (int c = 0; c < 3; c++) a->value[c] = (&a->world.x)[c] * CM_PER_METRE;
		reel_project(l, a->world, &a->value[REEL_ANCHOR_SX], &a->value[REEL_ANCHOR_SY]);
	}
	for (int i = 0; i < r->nlet_nodes; i++) r->lets[r->let_nodes[i]->slot] = reel_expr_eval(&r->let_nodes[i]->value, r);
}

int reel_frame_count(const reel_t *r) {
	return (int)floorf(r->duration * r->fps + REEL_TIME_EPSILON);
}

/* Samples every frame of the timeline, end included: records trail tracks
   and evaluates checks. Returns false when a check fails. */
bool reel_sample(reel_t *r, FILE *report) {
	if (!r->nchecks && !r->has_trails) return true;
	int frames = reel_frame_count(r) + 1;
	free(r->tracks);
	r->track_frames = frames;
	r->tracks = calloc((size_t)(r->ntracks ? r->ntracks : 1) * (size_t)frames * 3, sizeof(float));
	for (int c = 0; c < r->nchecks; c++) { r->checks[c]->failures = 0; r->checks[c]->worst = NAN; }
	for (int f = 0; f < frames; f++) {
		float t = f / r->fps;
		reel_seek(r, t);
		for (int i = 0; i < r->nanchors; i++) {
			reel_anchor_t *a = &r->anchors[i];
			if (a->track < 0) continue;
			float *p = r->tracks + ((size_t)a->track * frames + f) * 3;
			p[0] = a->world.x; p[1] = a->world.y; p[2] = a->world.z;
		}
		for (int c = 0; c < r->nchecks; c++) {
			reel_node_t *n = r->checks[c];
			float v = reel_expr_eval(&n->value, r);
			if (n->over != REEL_OVER_EVERY) {
				if (isnan(n->worst) || (n->over == REEL_OVER_MAX ? v > n->worst : v < n->worst)) { n->worst = v; n->worst_time = t; }
				continue;
			}
			bool bad = !isfinite(v) || (n->has_min && v < n->min) || (n->has_max && v > n->max);
			float margin = n->has_max ? v : -v;
			if (isnan(n->worst) || margin > (n->has_max ? n->worst : -n->worst)) { n->worst = v; n->worst_time = t; }
			if (bad && !n->failures++) n->first_failure = t;
		}
	}
	bool ok = true;
	for (int c = 0; c < r->nchecks; c++) {
		reel_node_t *n = r->checks[c];
		if (n->over != REEL_OVER_EVERY && (!isfinite(n->worst) || (n->has_min && n->worst < n->min) || (n->has_max && n->worst > n->max))) {
			n->failures = 1; n->first_failure = n->worst_time;
		}
		char limit[64] = "";
		if (n->has_min && n->has_max) snprintf(limit, sizeof(limit), "%g..%g", n->min, n->max);
		else if (n->has_max) snprintf(limit, sizeof(limit), "<= %g", n->max);
		else snprintf(limit, sizeof(limit), ">= %g", n->min);
		static const char *const labels[] = {"worst", "max", "min"};
		if (report) fprintf(report, "check %-28s %s %s %.3f at %.3fs (%s) over %d frames\n", n->name,
		                    n->failures ? "FAIL" : "ok  ", labels[n->over], n->worst, n->worst_time, limit, frames);
		if (n->failures) {
			ok = false;
			fprintf(stderr, "[reel] %s:%d: check %s failed on %d of %d frames, first at %.3fs\n", r->path, n->line, n->name, n->failures, frames, n->first_failure);
			snprintf(r->error, sizeof(r->error), "check %s failed", n->name);
		}
	}
	fflush(stderr);
	return ok;
}
