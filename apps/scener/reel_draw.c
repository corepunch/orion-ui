/* Reel rendering: one batched GL program draws every 2D element with analytic
   antialiasing (signed distances for boxes, capsules and SDF glyphs), and
   composites 3D scene layers rendered into their own supersampled targets.
   Graphics blend in sRGB like other design tools; scene pixels arrive linear
   and are encoded once. The canvas is stored top row first. */
#include <orion/user/gl_compat.h>
#include "reel.h"

enum { REEL_KIND_BOX, REEL_KIND_SEGMENT, REEL_KIND_GLYPH, REEL_KIND_IMAGE };
enum { REEL_PASS_ALL, REEL_PASS_INTERIOR, REEL_PASS_FRINGE };
#define REEL_AA_MARGIN 1.5f
#define REEL_NO_CLIP 1e9f

typedef struct { float x, y, u, v, p0[4], p1[4], clip[4]; uint8_t rgba[4]; float kind; } reel_vertex_t;
typedef struct { float m[6], alpha, clip[4]; } reel_xf_t; /* x' = m0 x + m2 y + m4, y' = m1 x + m3 y + m5 */

typedef struct {
	GLuint prog, vao, vbo, fbo, color, depth;
	GLint canvas_loc, atlas_loc, image_loc, pass_loc;
	reel_vertex_t *verts; int nverts, cverts;
	GLuint atlas, image;
	int stencil, flags;
	char text[2048];
} reel_gl_t;

static const char *reel_vs =
	"#version 150\n"
	"in vec2 aPos; in vec2 aUV; in vec4 aP0; in vec4 aP1; in vec4 aClip; in vec4 aColor; in float aKind;\n"
	"uniform vec2 uCanvas;\n"
	"out vec2 vUV, vPos; out vec4 vP0, vP1, vClip, vColor; flat out int vKind;\n"
	"void main(){ vUV=aUV; vPos=aPos; vP0=aP0; vP1=aP1; vClip=aClip; vColor=aColor; vKind=int(aKind+0.5);\n"
	"  gl_Position=vec4(aPos/uCanvas*2.0-1.0,0.0,1.0); }\n";

static const char *reel_fs =
	"#version 150\n"
	"in vec2 vUV, vPos; in vec4 vP0, vP1, vClip, vColor; flat in int vKind;\n"
	"uniform sampler2D uAtlas, uImage; uniform int uPass;\n"
	"out vec4 frag;\n"
	"float cover(float d){ float w=length(vec2(dFdx(d),dFdy(d))); return clamp(0.5-d/max(w,1e-5),0.0,1.0); }\n"
	"vec3 encode(vec3 c){ return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,step(0.0031308,c)); }\n"
	"void main(){\n"
	"  vec4 col=vColor; float c=1.0;\n"
	"  if(vKind==0){\n"
	"    vec2 q=abs(vUV)-vP0.xy+vP0.z; float d=length(max(q,0.0))+min(max(q.x,q.y),0.0)-vP0.z;\n"
	"    if(vP0.w>0.0) d=abs(d)-vP0.w*0.5; c=cover(d);\n"
	"  } else if(vKind==1){\n"
	"    float u=vUV.x, best;\n"
	"    if(vP1.x>0.0){\n"
	"      float per=vP1.x+vP1.y, k=floor(u/per); best=1e9;\n"
	"      for(int i=-1;i<=1;i++){ float s=max((k+float(i))*per,vP0.x), e=min((k+float(i))*per+vP1.x,vP0.y);\n"
	"        if(s<=e) best=min(best,length(vec2(u-clamp(u,s,e),vUV.y))); }\n"
	"    } else best=length(vec2(u-clamp(u,vP0.x,vP0.y),vUV.y));\n"
	"    c=cover(best-vP0.z);\n"
	"  } else if(vKind==2){\n"
	"    vec2 ts=vec2(textureSize(uAtlas,0)), dx=dFdx(vUV*ts), dy=dFdy(vUV*ts);\n"
	"    float texel=max(sqrt(0.5*(dot(dx,dx)+dot(dy,dy))),1e-5);\n"
	"    float dist=(texture(uAtlas,vUV).r*255.0-128.0)*vP0.x/128.0+vP0.y;\n"
	"    c=clamp(dist/texel+0.5,0.0,1.0);\n"
	"  } else {\n"
	"    int n=int(vP0.x+0.5); vec2 dx=dFdx(vUV), dy=dFdy(vUV); vec4 acc=vec4(0.0);\n"
	"    for(int j=0;j<n;j++) for(int i=0;i<n;i++){ vec2 o=(vec2(i,j)+0.5)/float(n)-0.5; acc+=texture(uImage,vUV+dx*o.x+dy*o.y); }\n"
	"    col=vec4(encode(acc.rgb/float(n*n)),vColor.a);\n"
	"  }\n"
	"  c*=clamp(vPos.x-vClip.x+0.5,0.0,1.0)*clamp(vClip.z-vPos.x+0.5,0.0,1.0)*clamp(vPos.y-vClip.y+0.5,0.0,1.0)*clamp(vClip.w-vPos.y+0.5,0.0,1.0);\n"
	"  if(c<=0.0 || (uPass==1 && c<0.999) || (uPass==2 && c>=0.999)) discard;\n"
	"  frag=vec4(col.rgb,col.a*c);\n"
	"}\n";

static GLuint reel_shader(GLenum type, const char *src) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), NULL, log); fprintf(stderr, "[reel] shader: %s\n", log); fflush(stderr); glDeleteShader(s); return 0; }
	return s;
}

static bool reel_target(GLuint *fbo, GLuint *color, GLuint *depth, int w, int h, GLenum format) {
	glGenFramebuffers(1, fbo); glGenTextures(1, color); glGenRenderbuffers(1, depth);
	glBindTexture(GL_TEXTURE_2D, *color);
	glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindRenderbuffer(GL_RENDERBUFFER, *depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
	glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *color, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, *depth);
	bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	if (!ok) { fprintf(stderr, "[reel] %dx%d render target is incomplete\n", w, h); fflush(stderr); }
	return ok;
}

static void reel_target_free(GLuint *fbo, GLuint *color, GLuint *depth) {
	if (*fbo) glDeleteFramebuffers(1, fbo);
	if (*color) glDeleteTextures(1, color);
	if (*depth) glDeleteRenderbuffers(1, depth);
	*fbo = *color = *depth = 0;
}

bool reel_gl_init(reel_t *r, int flags) {
	reel_gl_t *g = calloc(1, sizeof(*g));
	r->gl = g;
	g->flags = flags;
	GLuint vs = reel_shader(GL_VERTEX_SHADER, reel_vs), fs = reel_shader(GL_FRAGMENT_SHADER, reel_fs);
	if (!vs || !fs) return false;
	g->prog = glCreateProgram();
	glAttachShader(g->prog, vs); glAttachShader(g->prog, fs);
	static const char *const names[] = {"aPos", "aUV", "aP0", "aP1", "aClip", "aColor", "aKind"};
	for (int i = 0; i < 7; i++) glBindAttribLocation(g->prog, (GLuint)i, names[i]);
	glBindFragDataLocation(g->prog, 0, "frag");
	glLinkProgram(g->prog);
	glDeleteShader(vs); glDeleteShader(fs);
	GLint linked = 0; glGetProgramiv(g->prog, GL_LINK_STATUS, &linked);
	if (!linked) { fprintf(stderr, "[reel] shader program failed to link\n"); fflush(stderr); return false; }
	g->canvas_loc = glGetUniformLocation(g->prog, "uCanvas");
	g->atlas_loc = glGetUniformLocation(g->prog, "uAtlas");
	g->image_loc = glGetUniformLocation(g->prog, "uImage");
	g->pass_loc = glGetUniformLocation(g->prog, "uPass");
	glGenVertexArrays(1, &g->vao); glGenBuffers(1, &g->vbo);
	glBindVertexArray(g->vao); glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
	const GLsizei stride = sizeof(reel_vertex_t);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(reel_vertex_t, x));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(reel_vertex_t, u));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(reel_vertex_t, p0));
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(reel_vertex_t, p1));
	glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(reel_vertex_t, clip));
	glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)offsetof(reel_vertex_t, rgba));
	glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(reel_vertex_t, kind));
	for (GLuint i = 0; i < 7; i++) glEnableVertexAttribArray(i);
	glBindVertexArray(0);
	for (int i = 0; i < r->nfonts; i++) {
		int w, h;
		const uint8_t *pixels = font_sdf_pixels(r->fonts[i].font, &w, &h);
		glGenTextures(1, &r->fonts[i].texture);
		glBindTexture(GL_TEXTURE_2D, r->fonts[i].texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		int x, y; font_sdf_take_dirty(r->fonts[i].font, &x, &y, &w, &h);
	}
	for (int i = 0; i < r->nlayers; i++) scene_init_textures(&r->layers[i].scene);
	return reel_target(&g->fbo, &g->color, &g->depth, r->width, r->height, GL_RGBA8);
}

void reel_gl_free(reel_t *r) {
	reel_gl_t *g = r->gl;
	if (!g) return;
	for (int i = 0; i < r->nlayers; i++) {
		reel_layer_t *l = &r->layers[i];
		reel_target_free(&l->fbo, &l->color, &l->depth);
		scene_free_textures(&l->scene);
	}
	for (int i = 0; i < r->nfonts; i++) if (r->fonts[i].texture) glDeleteTextures(1, &r->fonts[i].texture);
	reel_target_free(&g->fbo, &g->color, &g->depth);
	if (g->vbo) glDeleteBuffers(1, &g->vbo);
	if (g->vao) glDeleteVertexArrays(1, &g->vao);
	if (g->prog) glDeleteProgram(g->prog);
	free(g->verts); free(g);
	r->gl = NULL;
}

/* ── Batching ─────────────────────────────────────────────────────────── */

static void reel_canvas_state(reel_t *r) {
	reel_gl_t *g = r->gl;
	glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
	glViewport(0, 0, r->width, r->height);
	glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST); glDisable(GL_POLYGON_OFFSET_FILL);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_FALSE); glStencilMask(0xFF);
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void reel_flush(reel_t *r, int pass) {
	reel_gl_t *g = r->gl;
	if (!g->nverts) return;
	glUseProgram(g->prog);
	glUniform2f(g->canvas_loc, (float)r->width, (float)r->height);
	glUniform1i(g->atlas_loc, 0); glUniform1i(g->image_loc, 1);
	glUniform1i(g->pass_loc, pass);
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g->atlas);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g->image);
	glBindVertexArray(g->vao);
	glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(reel_vertex_t) * (size_t)g->nverts), g->verts, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, g->nverts);
	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE0);
	if (pass != REEL_PASS_INTERIOR) g->nverts = 0;
}

static void reel_bind(reel_t *r, GLuint atlas, GLuint image) {
	reel_gl_t *g = r->gl;
	if ((atlas && atlas != g->atlas) || (image && image != g->image)) reel_flush(r, REEL_PASS_ALL);
	if (atlas) g->atlas = atlas;
	if (image) g->image = image;
}

static void reel_xf_apply(const reel_xf_t *xf, float x, float y, float *ox, float *oy) {
	*ox = xf->m[0] * x + xf->m[2] * y + xf->m[4];
	*oy = xf->m[1] * x + xf->m[3] * y + xf->m[5];
}

static float reel_xf_scale(const reel_xf_t *xf) {
	return sqrtf(fabsf(xf->m[0] * xf->m[3] - xf->m[1] * xf->m[2]));
}

/* A quad of local corners (a, b, c, d in order) with SDF coordinates. */
static void reel_quad(reel_t *r, const reel_xf_t *xf, const float pos[8], const float uv[8], const float p0[4],
                      const float p1[4], const float color[4], float alpha, int kind) {
	reel_gl_t *g = r->gl;
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	reel_vertex_t v[4];
	for (int i = 0; i < 4; i++) {
		reel_xf_apply(xf, pos[i * 2], pos[i * 2 + 1], &v[i].x, &v[i].y);
		v[i].u = uv[i * 2]; v[i].v = uv[i * 2 + 1];
		memcpy(v[i].p0, p0, sizeof(v[i].p0));
		if (p1) memcpy(v[i].p1, p1, sizeof(v[i].p1)); else memset(v[i].p1, 0, sizeof(v[i].p1));
		memcpy(v[i].clip, xf->clip, sizeof(v[i].clip));
		for (int c = 0; c < 4; c++) {
			float value = c < 3 ? color[c] : color[3] * alpha * xf->alpha;
			v[i].rgba[c] = (uint8_t)lroundf(fminf(1, fmaxf(0, value)) * 255);
		}
		v[i].kind = (float)kind;
	}
	for (int i = 0; i < 6; i++) DA_PUSH(g->verts, g->nverts, g->cverts, v[order[i]]);
}

static void reel_box(reel_t *r, const reel_xf_t *xf, float x, float y, float w, float h, float radius,
                     float stroke, const float color[4], float alpha) {
	if (w <= 0 || h <= 0) return;
	float hw = w / 2, hh = h / 2, cx = x + hw, cy = y + hh;
	float m = REEL_AA_MARGIN / fmaxf(reel_xf_scale(xf), 1e-4f) + stroke / 2;
	radius = fminf(fmaxf(radius, 0), fminf(hw, hh));
	float ex = hw + m, ey = hh + m;
	float pos[8] = {cx - ex, cy - ey, cx + ex, cy - ey, cx + ex, cy + ey, cx - ex, cy + ey};
	float uv[8] = {-ex, -ey, ex, -ey, ex, ey, -ex, ey};
	float p0[4] = {hw, hh, radius, stroke};
	reel_quad(r, xf, pos, uv, p0, NULL, color, alpha, REEL_KIND_BOX);
}

/* A capsule from a to b; arc is the dash distance already travelled at a. */
static void reel_segment(reel_t *r, const reel_xf_t *xf, float ax, float ay, float bx, float by, float width,
                         float arc, const float dash[2], const float color[4], float alpha) {
	float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
	if (len > 1e-5f) { dx /= len; dy /= len; } else { dx = 1; dy = 0; }
	float nx = -dy, ny = dx, hw = width / 2, e = hw + REEL_AA_MARGIN / fmaxf(reel_xf_scale(xf), 1e-4f);
	float pos[8] = {ax - dx * e + nx * e, ay - dy * e + ny * e, bx + dx * e + nx * e, by + dy * e + ny * e,
	                bx + dx * e - nx * e, by + dy * e - ny * e, ax - dx * e - nx * e, ay - dy * e - ny * e};
	float uv[8] = {arc - e, e, arc + len + e, e, arc + len + e, -e, arc - e, -e};
	float p0[4] = {arc, arc + len, hw, 0}, p1[4] = {dash ? dash[0] : 0, dash ? dash[1] : 0, 0, 0};
	reel_quad(r, xf, pos, uv, p0, p1, color, alpha, REEL_KIND_SEGMENT);
}

/* Polylines draw interior pixels once, then antialiased fringes, so joints
   of translucent lines do not double up. */
static void reel_polyline(reel_t *r, const reel_xf_t *xf, const float *pts, int n, float width, const float dash[2],
                          const float color[4], float alpha) {
	reel_gl_t *g = r->gl;
	if (n < 2 || width <= 0 || alpha * xf->alpha * color[3] <= 0) return;
	reel_flush(r, REEL_PASS_ALL);
	if (++g->stencil > 255) { glClearStencil(0); glClear(GL_STENCIL_BUFFER_BIT); g->stencil = 1; }
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_NOTEQUAL, g->stencil, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	float arc = 0;
	for (int i = 0; i + 1 < n; i++) {
		const float *a = pts + i * 2, *b = a + 2;
		reel_segment(r, xf, a[0], a[1], b[0], b[1], width, arc, dash && dash[1] > 0 ? dash : NULL, color, alpha);
		arc += hypotf(b[0] - a[0], b[1] - a[1]);
	}
	reel_flush(r, REEL_PASS_INTERIOR);
	reel_flush(r, REEL_PASS_FRINGE);
	glDisable(GL_STENCIL_TEST);
}

/* ── Text ─────────────────────────────────────────────────────────────── */

typedef struct { const font_sdf_glyph_t *glyph; float x; int word; } reel_placed_t;

static const char *reel_text_string(reel_t *r, reel_node_t *n) {
	reel_gl_t *g = r->gl;
	size_t len = 0;
	g->text[0] = 0;
	for (int i = 0; i < n->nsegments && len < sizeof(g->text) - 1; i++) {
		reel_segment_t *s = &n->segments[i];
		if (s->expr) len += (size_t)snprintf(g->text + len, sizeof(g->text) - len, s->format, (double)reel_expr_eval(&s->value, r));
		else len += (size_t)snprintf(g->text + len, sizeof(g->text) - len, "%s", s->text);
	}
	return g->text;
}

static void reel_text(reel_t *r, reel_node_t *n, const reel_xf_t *xf, float alpha) {
	const reel_style_t *st = &r->styles[n->style];
	reel_font_t *font = &r->fonts[st->font];
	float base = font_sdf_base_size(font->font), k = st->size / base, ascent, descent;
	font_sdf_vmetrics(font->font, &ascent, &descent, NULL);
	const char *s = reel_text_string(r, n);
	static reel_placed_t placed[1024];
	int count = 0, word = 0;
	float pen = 0, digit = 0;
	if (st->tabular) for (char d = '0'; d <= '9'; d++) { const font_sdf_glyph_t *gd = font_sdf_glyph(font->font, (uint32_t)d); if (gd) digit = fmaxf(digit, gd->advance); }
	const font_sdf_glyph_t *prev = NULL;
	for (uint32_t cp; (cp = font_sdf_utf8_next(&s)) && count < 1024;) {
		const font_sdf_glyph_t *gl = font_sdf_glyph(font->font, cp);
		if (!gl) continue;
		if (cp == ' ') { word++; prev = gl; pen += gl->advance * k + st->tracking * st->size; continue; }
		bool tab = st->tabular && cp >= '0' && cp <= '9';
		if (prev && !tab) pen += font_sdf_kern(font->font, prev, gl) * k;
		placed[count].glyph = gl; placed[count].word = word;
		placed[count].x = pen + (tab ? (digit - gl->advance) * k / 2 : 0);
		count++;
		pen += (tab ? digit : gl->advance) * k + st->tracking * st->size;
		prev = gl;
	}
	float width = pen - (count ? st->tracking * st->size : 0);
	float ox = n->align == 1 ? -width / 2 : n->align == 2 ? -width : 0;
	float baseline = n->valign == 1 ? ascent * k : n->valign == 2 ? (ascent + descent) / 2 * k : n->valign == 3 ? descent * k : 0;
	int atlas_w, atlas_h, dx, dy, dw, dh;
	font_sdf_pixels(font->font, &atlas_w, &atlas_h);
	if (font_sdf_take_dirty(font->font, &dx, &dy, &dw, &dh)) {
		reel_flush(r, REEL_PASS_ALL);
		const uint8_t *pixels = font_sdf_pixels(font->font, NULL, NULL);
		glBindTexture(GL_TEXTURE_2D, font->texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, atlas_w);
		glTexSubImage2D(GL_TEXTURE_2D, 0, dx, dy, dw, dh, GL_RED, GL_UNSIGNED_BYTE, pixels + (size_t)dy * atlas_w + dx);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
	reel_bind(r, font->texture, 0);
	float t = r->t, pad = (float)font_sdf_padding(font->font);
	float p0[4] = {pad, st->weight * base, 0, 0};
	for (int i = 0; i < count; i++) {
		const reel_placed_t *pl = &placed[i];
		const font_sdf_glyph_t *gl = pl->glyph;
		if (!gl->w) continue;
		reel_xf_t local = *xf;
		float a = alpha, lift = 0;
		int order = n->reveal == REEL_REVEAL_TYPE ? i : pl->word;
		float begin = n->at + order * n->stagger;
		if (n->reveal != REEL_REVEAL_NONE && t < begin) continue;
		if (n->reveal == REEL_REVEAL_RISE) {
			lift = (1 - reel_spring(t - begin, 1 / 1.9f, 0.62f)) * st->size * 1.15f;
			float x0 = REEL_NO_CLIP, x1 = -REEL_NO_CLIP;
			for (int j = 0; j < count; j++) if (placed[j].word == pl->word) {
				x0 = fminf(x0, placed[j].x); x1 = fmaxf(x1, placed[j].x + placed[j].glyph->advance * k);
			}
			float cx0, cy0, cx1, cy1;
			reel_xf_apply(xf, ox + x0 - st->size * 0.2f, baseline - st->size * 1.05f, &cx0, &cy0);
			reel_xf_apply(xf, ox + x1 + st->size * 0.2f, baseline + st->size * 0.3f, &cx1, &cy1);
			local.clip[0] = fmaxf(local.clip[0], fminf(cx0, cx1)); local.clip[1] = fmaxf(local.clip[1], fminf(cy0, cy1));
			local.clip[2] = fminf(local.clip[2], fmaxf(cx0, cx1)); local.clip[3] = fminf(local.clip[3], fmaxf(cy0, cy1));
		} else if (n->reveal == REEL_REVEAL_FADE) {
			float p = reel_ease(REEL_EASE_OUT_CUBIC, (t - begin) / 0.35f);
			a *= p; lift = (1 - p) * st->size * 0.25f;
		}
		if (n->exit >= 0 && n->reveal != REEL_REVEAL_NONE) {
			float leave = n->exit + order * 0.035f, out = reel_ease(REEL_EASE_IN_CUBIC, (t - leave) / 0.28f);
			if (out >= 1) continue;
			if (n->reveal == REEL_REVEAL_RISE) lift -= out * st->size * 1.2f; else a *= 1 - out;
		}
		float x = ox + pl->x + gl->x_offset * k, y = baseline + lift + gl->y_offset * k, w = gl->w * k, h = gl->h * k;
		float u0 = (float)gl->x / atlas_w, v0 = (float)gl->y / atlas_h, u1 = (float)(gl->x + gl->w) / atlas_w, v1 = (float)(gl->y + gl->h) / atlas_h;
		float pos[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
		float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
		reel_quad(r, &local, pos, uv, p0, NULL, n->color, a, REEL_KIND_GLYPH);
	}
}

/* ── Scene layers ─────────────────────────────────────────────────────── */

static void reel_scene(reel_t *r, reel_node_t *n, const reel_xf_t *xf, float alpha) {
	reel_gl_t *g = r->gl;
	reel_layer_t *l = &r->layers[n->layer];
	int tw = (int)lroundf(l->rect[2] * r->supersample), th = (int)lroundf(l->rect[3] * r->supersample);
	if (tw < 1 || th < 1 || tw > 16384 || th > 16384) {
		fprintf(stderr, "[reel] scene layer %s has invalid size %dx%d\n", l->src, tw, th); fflush(stderr);
		return;
	}
	reel_flush(r, REEL_PASS_ALL);
	if (tw != l->tw || th != l->th) {
		reel_target_free(&l->fbo, &l->color, &l->depth);
		if (!reel_target(&l->fbo, &l->color, &l->depth, tw, th, GL_SRGB8_ALPHA8)) return;
		l->tw = tw; l->th = th;
	}
	Scene *s = &l->scene;
	glBindFramebuffer(GL_FRAMEBUFFER, l->fbo);
	glViewport(0, 0, tw, th);
	glDisable(GL_BLEND);
	vec3 dir = vsub(s->camLook, s->camPos);
	dir = vlen(dir) > 1e-6f ? vnorm(dir) : v3(0, 0, -1);
	render_frame(s, tw, th, l->proj, l->view, s->camPos, dir, g->flags);
	reel_canvas_state(r);
	reel_bind(r, 0, l->color);
	float w = l->rect[2], h = l->rect[3];
	float pos[8] = {0, 0, w, 0, w, h, 0, h}, uv[8] = {0, 1, 1, 1, 1, 0, 0, 0};
	float p0[4] = {(float)r->supersample, 0, 0, 0};
	static const float white[4] = {1, 1, 1, 1};
	reel_quad(r, xf, pos, uv, p0, NULL, white, alpha, REEL_KIND_IMAGE);
}

/* ── Tree ─────────────────────────────────────────────────────────────── */

static void reel_motions(const reel_node_t *n, float t, float *dx, float *dy, float *scale, float *rotation, float *alpha) {
	for (int i = 0; i < n->nmotions; i++) {
		const reel_motion_t *m = &n->motions[i];
		float at = m->a[0], dt = t - at;
		switch (m->kind) {
		case REEL_MOTION_POP: case REEL_MOTION_SLAM: {
			if (dt < 0) { *alpha = 0; break; }
			float k = m->kind == REEL_MOTION_POP ? reel_spring(dt, m->a[1], m->a[2]) : reel_spring(dt, 0.43f, 0.5f);
			float from = m->kind == REEL_MOTION_POP ? 0 : m->a[1];
			*scale *= from + (1 - from) * k;
			*alpha *= fminf(1, dt / (m->kind == REEL_MOTION_POP ? 0.06f : 0.07f));
			break;
		}
		case REEL_MOTION_ENTER: {
			if (dt < 0) { *alpha = 0; break; }
			float k = reel_spring(dt, 0.53f, 0.68f);
			*dx += m->a[1] * (1 - k); *dy += m->a[2] * (1 - k);
			break;
		}
		case REEL_MOTION_LEAVE: {
			float p = reel_ease(REEL_EASE_IN_QUART, dt / fmaxf(m->a[1], 1e-4f));
			if (p >= 1) { *alpha = 0; break; }
			*dx += m->a[2] * p; *dy += m->a[3] * p;
			break;
		}
		case REEL_MOTION_FADE_IN:  *alpha *= fminf(1, fmaxf(0, dt / fmaxf(m->a[1], 1e-4f))); break;
		case REEL_MOTION_FADE_OUT: *alpha *= 1 - fminf(1, fmaxf(0, dt / fmaxf(m->a[1], 1e-4f))); break;
		case REEL_MOTION_PUNCH:
			if (dt >= 0) *scale *= 1 + m->a[1] * sinf(dt * 2 * M_PIf * 3.2f) * expf(-dt / 0.16f);
			break;
		case REEL_MOTION_RISE: {
			if (dt < 0) { *alpha = 0; break; }
			float p = reel_ease(REEL_EASE_OUT_CUBIC, dt / fmaxf(m->a[2], 1e-4f));
			*alpha *= p; *dy += m->a[1] * (1 - p);
			break;
		}
		}
	}
}

static void reel_draw_node(reel_t *r, reel_node_t *n, const reel_xf_t *parent) {
	if (n->kind == REEL_LET || n->kind == REEL_CHECK) return;
	float t = r->t;
	if (n->from.set && t < reel_expr_eval(&n->from, r)) return;
	if (n->to.set && t >= reel_expr_eval(&n->to, r)) return;
	float dx = 0, dy = 0, scale = reel_expr_eval(&n->scale, r), rotation = reel_expr_eval(&n->rotation, r), alpha = reel_expr_eval(&n->alpha, r);
	reel_motions(n, t, &dx, &dy, &scale, &rotation, &alpha);
	alpha = fminf(1, fmaxf(0, alpha));
	if (alpha <= 0 || scale == 0) return;
	reel_xf_t xf = *parent;
	bool placed = n->kind != REEL_LINE && n->kind != REEL_POLYLINE && n->kind != REEL_TRAIL;
	if (placed) {
		float x = reel_expr_eval(&n->x, r) + dx, y = reel_expr_eval(&n->y, r) + dy;
		if (n->kind == REEL_SCENE) { const reel_layer_t *l = &r->layers[n->layer]; x = l->rect[0] + dx; y = l->rect[1] + dy; }
		float c = cosf(rotation * M_PIf / 180) * scale, s = sinf(rotation * M_PIf / 180) * scale;
		const float *p = parent->m;
		float local[6] = {c, s, -s, c, x, y};
		xf.m[0] = p[0] * local[0] + p[2] * local[1]; xf.m[1] = p[1] * local[0] + p[3] * local[1];
		xf.m[2] = p[0] * local[2] + p[2] * local[3]; xf.m[3] = p[1] * local[2] + p[3] * local[3];
		xf.m[4] = p[0] * local[4] + p[2] * local[5] + p[4]; xf.m[5] = p[1] * local[4] + p[3] * local[5] + p[5];
	} else {
		xf.m[4] += parent->m[0] * dx + parent->m[2] * dy; xf.m[5] += parent->m[1] * dx + parent->m[3] * dy;
	}
	xf.alpha = parent->alpha * alpha;
	if (n->has_clip) {
		float x0, y0, x1, y1;
		reel_xf_apply(&xf, n->clip[0], n->clip[1], &x0, &y0);
		reel_xf_apply(&xf, n->clip[0] + n->clip[2], n->clip[1] + n->clip[3], &x1, &y1);
		xf.clip[0] = fmaxf(xf.clip[0], fminf(x0, x1)); xf.clip[1] = fmaxf(xf.clip[1], fminf(y0, y1));
		xf.clip[2] = fminf(xf.clip[2], fmaxf(x0, x1)); xf.clip[3] = fminf(xf.clip[3], fmaxf(y0, y1));
	}
	switch (n->kind) {
	case REEL_GROUP:
		for (int i = 0; i < n->nkids; i++) reel_draw_node(r, n->kids[i], &xf);
		break;
	case REEL_SCENE: reel_scene(r, n, &xf, 1); break;
	case REEL_TEXT:  reel_text(r, n, &xf, 1); break;
	case REEL_RECT: {
		float w = reel_expr_eval(&n->w, r), h = reel_expr_eval(&n->h, r);
		reel_box(r, &xf, n->centered ? -w / 2 : 0, n->centered ? -h / 2 : 0, w, h, reel_expr_eval(&n->radius, r),
		         reel_expr_eval(&n->stroke, r), n->color, 1);
		break;
	}
	case REEL_CIRCLE: {
		float radius = reel_expr_eval(&n->radius, r);
		reel_box(r, &xf, -radius, -radius, radius * 2, radius * 2, radius, reel_expr_eval(&n->stroke, r), n->color, 1);
		break;
	}
	case REEL_LINE: case REEL_POLYLINE: {
		int count = n->nanchors ? n->nanchors : n->npts;
		float *pts = malloc(sizeof(float) * 2 * (size_t)count);
		for (int i = 0; i < count; i++) {
			if (n->nanchors) { const reel_anchor_t *a = &r->anchors[n->anchors[i]]; pts[i * 2] = a->value[REEL_ANCHOR_SX]; pts[i * 2 + 1] = a->value[REEL_ANCHOR_SY]; }
			else { pts[i * 2] = reel_expr_eval(&n->px[i], r); pts[i * 2 + 1] = reel_expr_eval(&n->py[i], r); }
		}
		float width = reel_expr_eval(&n->w, r);
		reel_polyline(r, &xf, pts, count, width, n->dash, n->color, 1);
		if (n->dots > 0) for (int i = 0; i < count; i++) {
			if (n->has_dot_fill) reel_box(r, &xf, pts[i * 2] - n->dots, pts[i * 2 + 1] - n->dots, n->dots * 2, n->dots * 2, n->dots, 0, n->dot_fill, 1);
			reel_box(r, &xf, pts[i * 2] - n->dots, pts[i * 2 + 1] - n->dots, n->dots * 2, n->dots * 2, n->dots, width, n->color, 1);
		}
		free(pts);
		break;
	}
	case REEL_TRAIL: {
		const reel_anchor_t *a = &r->anchors[n->anchors[0]];
		const reel_layer_t *l = &r->layers[a->layer];
		float start = fmaxf(0, reel_expr_eval(&n->x2, r)), end = fminf(t, reel_expr_eval(&n->y2, r));
		if (!r->tracks || end <= start) break;
		int f0 = (int)ceilf(start * r->fps - 1e-4f), f1 = (int)floorf(end * r->fps + 1e-4f);
		if (f1 >= r->track_frames) f1 = r->track_frames - 1;
		int count = 0;
		float *pts = malloc(sizeof(float) * 2 * (size_t)(f1 - f0 + 3 > 2 ? f1 - f0 + 3 : 2));
		for (int f = f0; f <= f1; f++) {
			const float *w = r->tracks + ((size_t)a->track * r->track_frames + f) * 3;
			reel_project(l, v3(w[0], w[1], w[2]), &pts[count * 2], &pts[count * 2 + 1]);
			count++;
		}
		if (end >= t - 1e-4f) { pts[count * 2] = a->value[REEL_ANCHOR_SX]; pts[count * 2 + 1] = a->value[REEL_ANCHOR_SY]; count++; }
		reel_polyline(r, &xf, pts, count, reel_expr_eval(&n->w, r), n->dash, n->color, 1);
		free(pts);
		break;
	}
	default: break;
	}
}

bool reel_gl_render(reel_t *r, float t, uint8_t *rgba) {
	reel_gl_t *g = r->gl;
	if (!g) return false;
	reel_seek(r, t);
	reel_canvas_state(r);
	glClearColor(r->background[0], r->background[1], r->background[2], 1);
	glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	g->stencil = 0; g->nverts = 0;
	reel_xf_t root = {{1, 0, 0, 1, 0, 0}, 1, {-REEL_NO_CLIP, -REEL_NO_CLIP, REEL_NO_CLIP, REEL_NO_CLIP}};
	for (int i = 0; i < r->root.nkids; i++) reel_draw_node(r, r->root.kids[i], &root);
	reel_flush(r, REEL_PASS_ALL);
	glDisable(GL_BLEND);
	if (!rgba) return true;
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	return glGetError() == GL_NO_ERROR;
}
