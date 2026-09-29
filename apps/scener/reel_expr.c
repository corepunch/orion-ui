/* Reel attribute expressions: a Pratt parser that emits stack bytecode once,
   and a VM that evaluates it per frame without allocation. */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include "reel.h"

enum {
	RX_CONST, RX_T, RX_FRAME, RX_LET, RX_ANCHOR, RX_CURVE, RX_CALL,
	RX_ADD, RX_SUB, RX_MUL, RX_DIV, RX_MOD, RX_POW, RX_NEG, RX_NOT,
	RX_LT, RX_LE, RX_GT, RX_GE, RX_EQ, RX_NE, RX_AND, RX_OR, RX_JZ, RX_JMP
};


static const char *const reel_ease_names[REEL_EASES] = {
	"linear", "smooth", "hold",
	"inQuad", "outQuad", "inOutQuad",
	"inCubic", "outCubic", "inOutCubic",
	"inQuart", "outQuart", "inOutQuart",
	"outQuint", "inOutQuint",
	"inExpo", "outExpo", "inOutExpo",
	"inSine", "outSine", "inOutSine",
	"outBack", "inOutBack", "outElastic", "outBounce",
};

int reel_ease_find(const char *name) {
	for (int i = 0; i < REEL_EASES; i++) if (!strcmp(reel_ease_names[i], name)) return i;
	return -1;
}

static float reel_bounce(float x) {
	const float n = 7.5625f, d = 2.75f;
	if (x < 1 / d) return n * x * x;
	if (x < 2 / d) { x -= 1.5f / d; return n * x * x + 0.75f; }
	if (x < 2.5f / d) { x -= 2.25f / d; return n * x * x + 0.9375f; }
	x -= 2.625f / d; return n * x * x + 0.984375f;
}

/* Robert Penner's curves, named as in CSS and After Effects. */
float reel_ease(int ease, float x) {
	x = fminf(1, fmaxf(0, x));
	const float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1, u = 1 - x;
	switch (ease) {
	case REEL_EASE_SMOOTH:         return x * x * (3 - 2 * x);
	case REEL_EASE_STEP:           return x >= 1 ? 1 : 0;
	case REEL_EASE_IN_QUAD:        return x * x;
	case REEL_EASE_OUT_QUAD:       return 1 - u * u;
	case REEL_EASE_IN_OUT_QUAD:    return x < 0.5f ? 2 * x * x : 1 - 2 * u * u;
	case REEL_EASE_IN_CUBIC:       return x * x * x;
	case REEL_EASE_OUT_CUBIC:      return 1 - u * u * u;
	case REEL_EASE_IN_OUT_CUBIC:   return x < 0.5f ? 4 * x * x * x : 1 - 4 * u * u * u;
	case REEL_EASE_IN_QUART:       return x * x * x * x;
	case REEL_EASE_OUT_QUART:      return 1 - u * u * u * u;
	case REEL_EASE_IN_OUT_QUART:   return x < 0.5f ? 8 * x * x * x * x : 1 - 8 * u * u * u * u;
	case REEL_EASE_OUT_QUINT:      return 1 - u * u * u * u * u;
	case REEL_EASE_IN_OUT_QUINT:   return x < 0.5f ? 16 * powf(x, 5) : 1 - 16 * powf(u, 5);
	case REEL_EASE_IN_EXPO:        return x <= 0 ? 0 : powf(2, 10 * x - 10);
	case REEL_EASE_OUT_EXPO:       return x >= 1 ? 1 : 1 - powf(2, -10 * x);
	case REEL_EASE_IN_OUT_EXPO:    return x <= 0 ? 0 : x >= 1 ? 1 : x < 0.5f ? powf(2, 20 * x - 10) / 2 : (2 - powf(2, -20 * x + 10)) / 2;
	case REEL_EASE_IN_SINE:        return 1 - cosf(x * M_PIf / 2);
	case REEL_EASE_OUT_SINE:       return sinf(x * M_PIf / 2);
	case REEL_EASE_IN_OUT_SINE:    return (1 - cosf(M_PIf * x)) / 2;
	case REEL_EASE_OUT_BACK:       return 1 + c3 * u * u * u * -1 + c1 * u * u;
	case REEL_EASE_IN_OUT_BACK:    return x < 0.5f ? (4 * x * x * ((c2 + 1) * 2 * x - c2)) / 2
	                                              : ((2 * x - 2) * (2 * x - 2) * ((c2 + 1) * (2 * x - 2) + c2) + 2) / 2;
	case REEL_EASE_OUT_ELASTIC:    return x <= 0 ? 0 : x >= 1 ? 1 : powf(2, -10 * x) * sinf((x * 10 - 0.75f) * 2 * M_PIf / 3) + 1;
	case REEL_EASE_OUT_BOUNCE:     return reel_bounce(x);
	default:                       return x;
	}
}

/* A damped spring from 0 to 1, dt seconds after release; response is the
   undamped period and damping the damping fraction (SwiftUI's parameters). */
float reel_spring(float dt, float response, float damping) {
	if (dt <= 0) return 0;
	if (response <= 0) return 1;
	float w = 2 * M_PIf / response;
	if (damping >= 1) return 1 - expf(-w * dt) * (1 + w * dt);
	float wd = w * sqrtf(1 - damping * damping);
	return 1 - expf(-damping * w * dt) * (cosf(wd * dt) + damping * w / wd * sinf(wd * dt));
}

/* Deterministic smooth noise in [-1, 1]. */
static float reel_hash(float n) { float s = sinf(n * 127.1f) * 43758.5453f; return s - floorf(s); }
static float reel_noise(float x, float seed) {
	float i = floorf(x), f = x - i, u = f * f * (3 - 2 * f);
	return (reel_hash(i + seed * 17.13f) * (1 - u) + reel_hash(i + 1 + seed * 17.13f) * u) * 2 - 1;
}

static float fn_progress(const float *a) { return a[2] == a[1] ? (a[0] >= a[2]) : fminf(1, fmaxf(0, (a[0] - a[1]) / (a[2] - a[1]))); }
static float fn_smoothstep(const float *a) { float x = fn_progress((float[]){a[2], a[0], a[1]}); return x * x * (3 - 2 * x); }
static float fn_clamp(const float *a) { return fminf(a[2], fmaxf(a[1], a[0])); }
static float fn_mix(const float *a) { return a[0] + (a[1] - a[0]) * a[2]; }
static float fn_dist(const float *a) { return sqrtf((a[0]-a[3])*(a[0]-a[3]) + (a[1]-a[4])*(a[1]-a[4]) + (a[2]-a[5])*(a[2]-a[5])); }
static float fn_pulse(const float *a) { return a[0] < a[1] ? 0 : expf(-(a[0] - a[1]) / fmaxf(1e-4f, a[2])); }

enum { RF_SIN, RF_COS, RF_TAN, RF_ASIN, RF_ACOS, RF_ATAN, RF_ATAN2, RF_ABS, RF_SQRT, RF_EXP, RF_LOG, RF_POW,
	RF_FLOOR, RF_CEIL, RF_ROUND, RF_FRACT, RF_SIGN, RF_MIN, RF_MAX, RF_CLAMP, RF_MIX, RF_PROGRESS, RF_SMOOTHSTEP,
	RF_STEP, RF_SPRING, RF_HYPOT, RF_DIST, RF_NOISE, RF_PULSE, RF_RAD, RF_DEG, RF_EASE };

typedef struct { const char *name; int fn, args; bool vector; } reel_function_t;
static const reel_function_t reel_functions[] = {
	{"sin", RF_SIN, 1},    {"cos", RF_COS, 1},     {"tan", RF_TAN, 1},       {"asin", RF_ASIN, 1},
	{"acos", RF_ACOS, 1},  {"atan", RF_ATAN, 1},   {"atan2", RF_ATAN2, 2},   {"abs", RF_ABS, 1},
	{"sqrt", RF_SQRT, 1},  {"exp", RF_EXP, 1},     {"log", RF_LOG, 1},       {"pow", RF_POW, 2},
	{"floor", RF_FLOOR, 1},{"ceil", RF_CEIL, 1},   {"round", RF_ROUND, 1},   {"fract", RF_FRACT, 1},
	{"sign", RF_SIGN, 1},  {"min", RF_MIN, -2},    {"max", RF_MAX, -2},      {"clamp", RF_CLAMP, 3},
	{"mix", RF_MIX, 3},    {"progress", RF_PROGRESS, 3}, {"smoothstep", RF_SMOOTHSTEP, 3},
	{"step", RF_STEP, 1},  {"spring", RF_SPRING, 3},{"hypot", RF_HYPOT, 2},  {"dist", RF_DIST, 6, true},
	{"noise", RF_NOISE, 2},{"pulse", RF_PULSE, 3}, {"rad", RF_RAD, 1},       {"deg", RF_DEG, 1},
};
#define REEL_FUNCTIONS (int)(sizeof(reel_functions) / sizeof(reel_functions[0]))

static float reel_call(int fn, const float *a, int n) {
	switch (fn) {
	case RF_SIN:   return sinf(a[0]);       case RF_COS:   return cosf(a[0]);       case RF_TAN:  return tanf(a[0]);
	case RF_ASIN:  return asinf(a[0]);      case RF_ACOS:  return acosf(a[0]);      case RF_ATAN: return atanf(a[0]);
	case RF_ATAN2: return atan2f(a[0], a[1]);case RF_ABS:  return fabsf(a[0]);      case RF_SQRT: return sqrtf(a[0]);
	case RF_EXP:   return expf(a[0]);       case RF_LOG:   return logf(a[0]);       case RF_POW:  return powf(a[0], a[1]);
	case RF_FLOOR: return floorf(a[0]);     case RF_CEIL:  return ceilf(a[0]);      case RF_ROUND: return roundf(a[0]);
	case RF_FRACT: return a[0] - floorf(a[0]); case RF_SIGN: return (a[0] > 0) - (a[0] < 0);
	case RF_MIN: { float m = a[0]; for (int i = 1; i < n; i++) m = fminf(m, a[i]); return m; }
	case RF_MAX: { float m = a[0]; for (int i = 1; i < n; i++) m = fmaxf(m, a[i]); return m; }
	case RF_CLAMP: return fn_clamp(a);      case RF_MIX:   return fn_mix(a);        case RF_PROGRESS: return fn_progress(a);
	case RF_SMOOTHSTEP: return fn_smoothstep(a); case RF_STEP: return a[0] > 0 ? 1 : 0;
	case RF_SPRING: return reel_spring(a[0], a[1], a[2]);
	case RF_HYPOT: return hypotf(a[0], a[1]); case RF_DIST: return fn_dist(a);     case RF_NOISE: return reel_noise(a[0], a[1]);
	case RF_PULSE: return fn_pulse(a);      case RF_RAD:   return a[0] * M_PIf / 180; case RF_DEG: return a[0] * 180 / M_PIf;
	default:       return reel_ease(fn - RF_EASE, a[0]);
	}
}

/* ── Compiler ─────────────────────────────────────────────────────────── */

typedef struct { reel_t *r; const char *src, *p; reel_expr_t *e; bool failed, dynamic; int pushes; } reel_parser_t;

static bool rx_fail(reel_parser_t *ps, const char *fmt, ...) {
	if (ps->failed) return false;
	ps->failed = true;
	char message[256];
	va_list ap; va_start(ap, fmt); vsnprintf(message, sizeof(message), fmt, ap); va_end(ap);
	snprintf(ps->r->error, sizeof(ps->r->error), "%s in \"%s\" at column %d", message, ps->src, (int)(ps->p - ps->src) + 1);
	return false;
}

static void rx_byte(reel_parser_t *ps, int b) { DA_PUSH(ps->e->code, ps->e->ncode, ps->e->ccode, (uint8_t)b); }
static void rx_u16(reel_parser_t *ps, int v) { rx_byte(ps, v & 0xFF); rx_byte(ps, (v >> 8) & 0xFF); }
static void rx_const(reel_parser_t *ps, float v) { ps->pushes++; rx_byte(ps, RX_CONST); rx_u16(ps, ps->e->nk); DA_PUSH(ps->e->k, ps->e->nk, ps->e->ck, v); }
static void rx_ws(reel_parser_t *ps) { while (isspace((unsigned char)*ps->p)) ps->p++; }
static bool rx_accept(reel_parser_t *ps, const char *token) {
	rx_ws(ps);
	size_t n = strlen(token);
	if (strncmp(ps->p, token, n)) return false;
	ps->p += n;
	return true;
}
static bool rx_expect(reel_parser_t *ps, const char *token) {
	return rx_accept(ps, token) || rx_fail(ps, "expected '%s'", token);
}

static bool rx_expr(reel_parser_t *ps);

/* Name path: ident('.' ident)* with an optional '@seconds' sample time. */
static int rx_path(reel_parser_t *ps, char parts[3][REEL_NAME], bool *fixed, float *at) {
	int n = 0;
	*fixed = false;
	for (;;) {
		int len = 0;
		while (isalnum((unsigned char)*ps->p) || *ps->p == '_') {
			if (len < REEL_NAME - 1 && n < 3) parts[n][len++] = *ps->p;
			ps->p++;
		}
		if (n < 3) parts[n][len] = 0;
		n++;
		if (*ps->p == '@') {
			char *end; ps->p++;
			*at = strtof(ps->p, &end);
			if (end == ps->p) return rx_fail(ps, "expected a time after '@'"), 0;
			if (end[-1] == '.' && (isalpha((unsigned char)*end) || *end == '_')) end--; /* FK.palm@0.sx */
			ps->p = end; *fixed = true;
		}
		if (*ps->p != '.' || !(isalpha((unsigned char)ps->p[1]) || ps->p[1] == '_')) break;
		ps->p++;
	}
	return n;
}

static int reel_anchor_add(reel_t *r, const char *instance, const char *joint, bool fixed, float at) {
	for (int i = 0; i < r->nanchors; i++) {
		reel_anchor_t *a = &r->anchors[i];
		if (!strcmp(a->instance, instance) && !strcmp(a->joint, joint) && a->fixed == fixed && (!fixed || a->at == at)) return i;
	}
	for (int l = 0; l < r->nlayers; l++) {
		vec3 p;
		if (!scene_joint_position(&r->layers[l].scene, instance, joint, &p)) continue;
		reel_anchor_t a = {0};
		snprintf(a.instance, sizeof(a.instance), "%s", instance);
		snprintf(a.joint, sizeof(a.joint), "%s", joint);
		a.layer = l; a.track = -1; a.fixed = fixed; a.at = at; a.world = p;
		if (fixed) {
			Scene *scene = &r->layers[l].scene;
			scene_set_time(scene, at);
			if (!scene_joint_position(scene, instance, joint, &a.world)) return -1;
			scene_set_time(scene, r->layers[l].time);
		}
		for (int c = 0; c < 3; c++) a.value[c] = (&a.world.x)[c] * CM_PER_METRE;
		DA_PUSH(r->anchors, r->nanchors, r->canchors, a);
		return r->nanchors - 1;
	}
	return -1;
}

static bool rx_anchor(reel_parser_t *ps, const char *instance, const char *joint, bool fixed, float at, int *index) {
	*index = reel_anchor_add(ps->r, instance, joint, fixed, at);
	if (*index < 0) return rx_fail(ps, "no scene layer has joint %s.%s", instance, joint);
	if (!fixed) ps->dynamic = true;
	return true;
}

static void rx_anchor_load(reel_parser_t *ps, int index, int component) {
	ps->pushes++; rx_byte(ps, RX_ANCHOR); rx_u16(ps, index); rx_byte(ps, component);
}

/* A 3D point argument: Inst.joint[@t] or vec(x, y, z); pushes three values. */
static bool rx_vector(reel_parser_t *ps) {
	rx_ws(ps);
	if (rx_accept(ps, "vec(")) {
		for (int i = 0; i < 3; i++) if (!rx_expr(ps) || !rx_expect(ps, i < 2 ? "," : ")")) return false;
		return true;
	}
	char parts[3][REEL_NAME]; bool fixed; float at = 0;
	const char *start = ps->p;
	if (!isalpha((unsigned char)*ps->p) && *ps->p != '_') return rx_fail(ps, "expected a joint or vec(x, y, z)");
	int n = rx_path(ps, parts, &fixed, &at);
	if (ps->failed) return false;
	if (n != 2) { ps->p = start; return rx_fail(ps, "expected Instance.joint"); }
	int index;
	if (!rx_anchor(ps, parts[0], parts[1], fixed, at, &index)) return false;
	for (int c = 0; c < 3; c++) rx_anchor_load(ps, index, c);
	return true;
}

static bool rx_call(reel_parser_t *ps, const char *name) {
	for (int i = 0; i < ps->r->ncurves; i++) if (!strcmp(ps->r->curves[i].name, name)) {
		if (!rx_expr(ps) || !rx_expect(ps, ")")) return false;
		rx_byte(ps, RX_CURVE); rx_u16(ps, i);
		return true;
	}
	int ease = reel_ease_find(name);
	const reel_function_t *f = NULL;
	for (int i = 0; i < REEL_FUNCTIONS; i++) if (!strcmp(reel_functions[i].name, name)) f = &reel_functions[i];
	if (!f && ease < 0) return rx_fail(ps, "unknown function %s()", name);
	int fn = f ? f->fn : RF_EASE + ease, want = f ? f->args : 1, n = 0;
	if (f && f->vector) {
		if (!rx_vector(ps) || !rx_expect(ps, ",") || !rx_vector(ps) || !rx_expect(ps, ")")) return false;
		n = 6;
	} else {
		if (!rx_accept(ps, ")")) {
			do { if (!rx_expr(ps)) return false; n++; } while (rx_accept(ps, ","));
			if (!rx_expect(ps, ")")) return false;
		}
		if (want >= 0 ? n != want : n < -want) return rx_fail(ps, "%s() takes %s%d argument%s", name, want < 0 ? "at least " : "", want < 0 ? -want : want, (want == 1 || want == -1) ? "" : "s");
	}
	if (n > REEL_STACK / 2) return rx_fail(ps, "%s() has too many arguments", name);
	rx_byte(ps, RX_CALL); rx_byte(ps, fn); rx_byte(ps, n);
	return true;
}

static bool rx_name(reel_parser_t *ps) {
	reel_t *r = ps->r;
	char parts[3][REEL_NAME]; bool fixed; float at = 0;
	const char *start = ps->p;
	int n = rx_path(ps, parts, &fixed, &at);
	if (ps->failed) return false;
	if (n == 1 && !fixed) {
		const char *name = parts[0];
		if (rx_accept(ps, "(")) return rx_call(ps, name);
		if (!strcmp(name, "t"))        { rx_byte(ps, RX_T); ps->dynamic = true; ps->pushes++; return true; }
		if (!strcmp(name, "frame"))    { rx_byte(ps, RX_FRAME); ps->dynamic = true; ps->pushes++; return true; }
		if (!strcmp(name, "pi"))       { rx_const(ps, M_PIf); return true; }
		if (!strcmp(name, "duration")) { rx_const(ps, r->duration); return true; }
		if (!strcmp(name, "fps"))      { rx_const(ps, r->fps); return true; }
		if (!strcmp(name, "width"))    { rx_const(ps, (float)r->width); return true; }
		if (!strcmp(name, "height"))   { rx_const(ps, (float)r->height); return true; }
		for (int i = r->nlets - 1; i >= 0; i--) if (!strcmp(r->let_names[i], name)) {
			rx_byte(ps, RX_LET); rx_u16(ps, i); ps->dynamic = true; ps->pushes++;
			return true;
		}
		ps->p = start;
		return rx_fail(ps, "unknown name %s", name);
	}
	static const char *const components[] = {"x", "y", "z", "sx", "sy"};
	int component = -1;
	if (n < 2) { ps->p = start; return rx_fail(ps, "unknown name %s", parts[0]); }
	if (n == 3) for (int c = 0; c < 5; c++) if (!strcmp(parts[2], components[c])) component = c;
	if (component < 0) {
		ps->p = start;
		return rx_fail(ps, n == 2 ? "joint %s.%s needs .x .y .z (cm) or .sx .sy (canvas px)" : "unknown name", parts[0], parts[1]);
	}
	int index;
	if (!rx_anchor(ps, parts[0], parts[1], fixed, at, &index)) return false;
	if (component >= REEL_ANCHOR_SX) ps->dynamic = true;
	rx_anchor_load(ps, index, component);
	return true;
}

static bool rx_primary(reel_parser_t *ps) {
	rx_ws(ps);
	if (rx_accept(ps, "(")) return rx_expr(ps) && rx_expect(ps, ")");
	if (isdigit((unsigned char)*ps->p) || (*ps->p == '.' && isdigit((unsigned char)ps->p[1]))) {
		char *end; float v = strtof(ps->p, &end);
		ps->p = end; rx_const(ps, v);
		return true;
	}
	if (isalpha((unsigned char)*ps->p) || *ps->p == '_') return rx_name(ps);
	return rx_fail(ps, *ps->p ? "unexpected '%c'" : "unexpected end", *ps->p);
}

static bool rx_unary(reel_parser_t *ps) {
	if (rx_accept(ps, "-")) { if (!rx_unary(ps)) return false; rx_byte(ps, RX_NEG); return true; }
	if (rx_accept(ps, "+")) return rx_unary(ps);
	rx_ws(ps);
	if (*ps->p == '!' && ps->p[1] != '=') { ps->p++; if (!rx_unary(ps)) return false; rx_byte(ps, RX_NOT); return true; }
	if (!rx_primary(ps)) return false;
	if (rx_accept(ps, "^")) { if (!rx_unary(ps)) return false; rx_byte(ps, RX_POW); }
	return true;
}

typedef struct { const char *token; int op, prec; } reel_binop_t;
static const reel_binop_t reel_binops[] = {
	{"||", RX_OR, 1}, {"&&", RX_AND, 2},
	{"==", RX_EQ, 3}, {"!=", RX_NE, 3},
	{"<=", RX_LE, 4}, {">=", RX_GE, 4}, {"<", RX_LT, 4}, {">", RX_GT, 4},
	{"+", RX_ADD, 5}, {"-", RX_SUB, 5},
	{"*", RX_MUL, 6}, {"/", RX_DIV, 6}, {"%", RX_MOD, 6},
};

static bool rx_binary(reel_parser_t *ps, int min_prec) {
	if (!rx_unary(ps)) return false;
	for (;;) {
		rx_ws(ps);
		const reel_binop_t *op = NULL;
		for (size_t i = 0; i < sizeof(reel_binops) / sizeof(reel_binops[0]); i++)
			if (!strncmp(ps->p, reel_binops[i].token, strlen(reel_binops[i].token))) { op = &reel_binops[i]; break; }
		if (!op || op->prec < min_prec) return true;
		ps->p += strlen(op->token);
		if (!rx_binary(ps, op->prec + 1)) return false;
		rx_byte(ps, op->op);
	}
}

static void rx_patch(reel_parser_t *ps, int at, int target) {
	ps->e->code[at] = target & 0xFF; ps->e->code[at + 1] = (target >> 8) & 0xFF;
}

static bool rx_expr(reel_parser_t *ps) {
	if (!rx_binary(ps, 1)) return false;
	if (!rx_accept(ps, "?")) return true;
	rx_byte(ps, RX_JZ); int jz = ps->e->ncode; rx_u16(ps, 0);
	if (!rx_expr(ps) || !rx_expect(ps, ":")) return false;
	rx_byte(ps, RX_JMP); int jmp = ps->e->ncode; rx_u16(ps, 0);
	rx_patch(ps, jz, ps->e->ncode);
	if (!rx_expr(ps)) return false;
	rx_patch(ps, jmp, ps->e->ncode);
	return true;
}

bool reel_expr_compile(reel_t *r, const char *source, reel_expr_t *out) {
	reel_expr_free(out);
	reel_parser_t ps = { r, source, source, out, false, false };
	if (!source || !rx_expr(&ps)) { if (!source) rx_fail(&ps, "missing expression"); reel_expr_free(out); return false; }
	rx_ws(&ps);
	if (*ps.p) { rx_fail(&ps, "unexpected '%c'", *ps.p); reel_expr_free(out); return false; }
	if (ps.pushes > REEL_STACK) { rx_fail(&ps, "expression is too long"); reel_expr_free(out); return false; }
	out->set = true;
	if (!ps.dynamic) {
		out->value = reel_expr_eval(out, r);
		out->constant = true;
	}
	return true;
}

void reel_expr_free(reel_expr_t *e) {
	free(e->code); free(e->k);
	memset(e, 0, sizeof(*e));
}

static inline int rx_read16(const uint8_t *c) { return c[0] | (c[1] << 8); }

float reel_expr_eval(const reel_expr_t *e, const reel_t *r) {
	if (!e->set) return 0;
	if (e->constant) return e->value;
	float stack[REEL_STACK]; int sp = 0;
	const uint8_t *code = e->code, *end = code + e->ncode, *ip = code;
	while (ip < end) {
		switch (*ip++) {
		case RX_CONST:  stack[sp++] = e->k[rx_read16(ip)]; ip += 2; break;
		case RX_T:      stack[sp++] = r->t; break;
		case RX_FRAME:  stack[sp++] = (float)r->frame; break;
		case RX_LET:    stack[sp++] = r->lets[rx_read16(ip)]; ip += 2; break;
		case RX_ANCHOR: stack[sp++] = r->anchors[rx_read16(ip)].value[ip[2]]; ip += 3; break;
		case RX_CURVE: {
			const reel_curve_t *c = &r->curves[rx_read16(ip)]; ip += 2;
			float x = stack[sp - 1];
			if (c->loop && c->n > 1) { float span = c->t[c->n - 1] - c->t[0]; if (span > 0) x = c->t[0] + fmodf(fmodf(x - c->t[0], span) + span, span); }
			float v = c->v[0];
			if (x >= c->t[c->n - 1]) v = c->v[c->n - 1];
			else for (int i = 1; i < c->n; i++) if (x < c->t[i]) {
				float p = x <= c->t[i - 1] ? 0 : (x - c->t[i - 1]) / (c->t[i] - c->t[i - 1]);
				v = c->v[i - 1] + (c->v[i] - c->v[i - 1]) * reel_ease(c->ease, p);
				break;
			}
			stack[sp - 1] = x < c->t[0] ? c->v[0] : v;
			break;
		}
		case RX_CALL: { int fn = ip[0], n = ip[1]; ip += 2; sp -= n; stack[sp] = reel_call(fn, stack + sp, n); sp++; break; }
		case RX_ADD: sp--; stack[sp - 1] += stack[sp]; break;
		case RX_SUB: sp--; stack[sp - 1] -= stack[sp]; break;
		case RX_MUL: sp--; stack[sp - 1] *= stack[sp]; break;
		case RX_DIV: sp--; stack[sp - 1] /= stack[sp]; break;
		case RX_MOD: sp--; stack[sp - 1] = fmodf(stack[sp - 1], stack[sp]); break;
		case RX_POW: sp--; stack[sp - 1] = powf(stack[sp - 1], stack[sp]); break;
		case RX_NEG: stack[sp - 1] = -stack[sp - 1]; break;
		case RX_NOT: stack[sp - 1] = stack[sp - 1] == 0; break;
		case RX_LT:  sp--; stack[sp - 1] = stack[sp - 1] <  stack[sp]; break;
		case RX_LE:  sp--; stack[sp - 1] = stack[sp - 1] <= stack[sp]; break;
		case RX_GT:  sp--; stack[sp - 1] = stack[sp - 1] >  stack[sp]; break;
		case RX_GE:  sp--; stack[sp - 1] = stack[sp - 1] >= stack[sp]; break;
		case RX_EQ:  sp--; stack[sp - 1] = stack[sp - 1] == stack[sp]; break;
		case RX_NE:  sp--; stack[sp - 1] = stack[sp - 1] != stack[sp]; break;
		case RX_AND: sp--; stack[sp - 1] = stack[sp - 1] != 0 && stack[sp] != 0; break;
		case RX_OR:  sp--; stack[sp - 1] = stack[sp - 1] != 0 || stack[sp] != 0; break;
		case RX_JZ:  { int target = rx_read16(ip); ip += 2; if (stack[--sp] == 0) ip = code + target; break; }
		case RX_JMP: ip = code + rx_read16(ip); break;
		}
	}
	return sp > 0 ? stack[sp - 1] : 0;
}
