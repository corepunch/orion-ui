#include <orion/user/gl_compat.h>
#include <math.h>
#include <stdio.h>
#include "vertex_cache.h"
#include <stddef.h>
#include <string.h>
#include "simplegl.h"
#include "shader.h"
#include <orion/kernel/renderer.h>

#define MAX_BATCH_VERTS 65536

typedef struct { float x, y, z, r, g, b; } LineVert;

static GLuint s_line_vao, s_line_vbo;
static GLuint s_tri_vao, s_tri_vbo;
static GLuint s_shadow_vao, s_shadow_vbo;
static GLuint s_quad_vao, s_quad_vbo;
static GLuint s_shadow_prog;
static GLuint s_image_prog,s_image_vao,s_image_vbo,s_image_ebo;
static GLint s_image_viewproj_loc,s_image_color_loc,s_image_sampler_loc;
static vec3 render_srgb_to_linear(vec3 color);
static GLint s_shadow_proj_loc, s_shadow_view_loc, s_shadow_color_loc, s_shadow_verts_loc, s_shadow_stride_loc, s_shadow_light_loc, s_shadow_planes_loc, s_shadow_inst_loc, s_shadow_vertbase_loc, s_shadow_planebase_loc, s_shadow_instbase_loc;
GLuint s_line_prog;
GLint s_line_viewproj_loc;

static const char *s_line_vs =
    "#version 150\n"
    "in vec4 aPos;\n"
    "in vec3 aColor;\n"
    "uniform mat4 uViewProj;\n"
    "out vec3 vColor;\n"
    "void main(){ vColor=aColor; gl_Position=uViewProj*aPos; }\n";

static const char *s_line_fs =
    "#version 150\n"
    "in vec3 vColor;\n"
    "out vec4 frag;\n"
    "void main(){ frag=vec4(vColor,1); }\n";

/* Shadow volumes drawn from a model's static shadow mesh (vertex_cache.c), instanced over the objects using it.
   Each vertex names a model vertex and a triangle; a vertex whose triangle faces away from the light is pushed
   to infinity, as Doom 3's shadow.vertex pushes its w=0 copies. Edge quads so become the silhouette's sides,
   triangles the near and far caps. The light arrives in the model's own space with each instance. */
static const char *s_shadow_vs =
    "#version 150\n"
    "in ivec2 aRef;\n"
    "uniform samplerBuffer uVerts;\n"
    "uniform samplerBuffer uPlanes;\n"
    "uniform samplerBuffer uInst;\n"
    "uniform int uStride;\n"
    "uniform int uVertBase;\n"
    "uniform int uPlaneBase;\n"
    "uniform int uInstBase;\n"
    "uniform vec3 uLight;\n"
    "uniform mat4 uProj;\n"
    "uniform mat4 uView;\n"
    "void main(){\n"
    "    int b=(uVertBase+aRef.x)*uStride;\n"
    "    vec4 p=vec4(texelFetch(uVerts,b).r,texelFetch(uVerts,b+1).r,texelFetch(uVerts,b+2).r,1.0);\n"
    "    int i=(uInstBase+gl_InstanceID)*4;\n"
    "    vec4 m0=texelFetch(uInst,i),m1=texelFetch(uInst,i+1),m2=texelFetch(uInst,i+2),L=texelFetch(uInst,i+3);\n"
    "    bool lit=false;\n"
    "    if(aRef.y>=0){ vec4 pl=texelFetch(uPlanes,uPlaneBase+aRef.y); lit=dot(pl.xyz,L.xyz)-pl.w*L.w>0.0; }\n"
    "    vec3 w=vec3(dot(m0,p),dot(m1,p),dot(m2,p));\n"
    "    gl_Position=uProj*uView*(lit?vec4(w,1.0):vec4(w-uLight,0.0));\n"
    "}\n";

static const char *s_shadow_fs =
    "#version 150\n"
    "uniform vec4 uColor;\n"
    "out vec4 frag;\n"
    "void main(){ frag=uColor; }\n";

static const char *s_image_vs =
    "#version 150\n"
    "in vec3 aPos;\n"
    "in vec2 aUV;\n"
    "uniform mat4 uViewProj;\n"
    "out vec2 vUV;\n"
    "void main(){ vUV=aUV; gl_Position=uViewProj*vec4(aPos,1.0); }\n";

static const char *s_image_fs =
    "#version 150\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uImage;\n"
    "uniform vec3 uColor;\n"
    "out vec4 frag;\n"
    "void main(){ vec4 pixel=texture(uImage,vUV); frag=vec4(pixel.rgb*uColor,pixel.a); }\n";

static void ensure_image_prog(void) {
    if (s_image_prog) return;
    if (!ui_load_program_from_source(s_image_vs, s_image_fs, "aPos", "aUV", NULL, &s_image_prog)) return;
    s_image_viewproj_loc = glGetUniformLocation(s_image_prog, "uViewProj");
    s_image_color_loc = glGetUniformLocation(s_image_prog, "uColor");
    s_image_sampler_loc = glGetUniformLocation(s_image_prog, "uImage");
    glGenVertexArrays(1, &s_image_vao); glGenBuffers(1, &s_image_vbo); glGenBuffers(1, &s_image_ebo);
}

static GLuint screen_texture(ScreenTexture *image) {
    if(image->texture) return image->texture;
    if(!image->pixels) return 0;
    glGenTextures(1,&image->texture);
    glBindTexture(GL_TEXTURE_2D,image->texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,image->width,image->height,0,GL_RGBA,GL_UNSIGNED_BYTE,image->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,0);
    return image->texture;
}

/* Front end: the image program and texture exist (creating them uploads), so the object draws as an image. */
static int screen_image_ready(ScreenTexture *image) {
    ensure_image_prog();
    return s_image_prog && screen_texture(image);
}

static int draw_screen_image(SceneObj *obj,ScreenTexture *image,mat4 viewProj) {
    ensure_image_prog();
    GLuint texture=screen_texture(image);
    if(!s_image_prog||!texture) return 0;
    vec3 color=render_srgb_to_linear(obj->color);
    glUseProgram(s_image_prog);
    mat4 mvp=mat4_mul(viewProj,obj->model);
    glUniformMatrix4fv(s_image_viewproj_loc,1,GL_FALSE,mvp.m);
    glUniform3f(s_image_color_loc,color.x,color.y,color.z);
    glUniform1i(s_image_sampler_loc,0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture);
    glBindVertexArray(s_image_vao);
    glBindBuffer(GL_ARRAY_BUFFER,s_image_vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(obj->mesh->nverts*sizeof(Vertex)),obj->mesh->verts,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,s_image_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(obj->mesh->ntris*sizeof(Tri)),obj->mesh->tris,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,pos));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,u));
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDrawElements(GL_TRIANGLES,obj->mesh->ntris*3,GL_UNSIGNED_INT,(void*)0);
    glDisable(GL_BLEND);
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
    glBindVertexArray(0); glBindTexture(GL_TEXTURE_2D,0); glUseProgram(0);
    return 1;
}

void render_deinit(void) {
    if(s_image_ebo) glDeleteBuffers(1,&s_image_ebo);
    if(s_image_vbo) glDeleteBuffers(1,&s_image_vbo);
    if(s_image_vao) glDeleteVertexArrays(1,&s_image_vao);
    if(s_image_prog) glDeleteProgram(s_image_prog);
    s_image_ebo=s_image_vbo=s_image_vao=s_image_prog=0;
}

void ensure_line_prog(void) {
    if (s_line_prog) return;
    if (!ui_load_program_from_source(s_line_vs, s_line_fs, "aPos", "aColor", NULL, &s_line_prog)) return;
    s_line_viewproj_loc = glGetUniformLocation(s_line_prog, "uViewProj");
}

static void ensure_shadow_prog(void) {
    if (s_shadow_prog) return;
    if (!ui_load_program_from_source(s_shadow_vs, s_shadow_fs, "aRef", NULL, NULL, &s_shadow_prog)) return;
    s_shadow_proj_loc = glGetUniformLocation(s_shadow_prog, "uProj");
    s_shadow_view_loc = glGetUniformLocation(s_shadow_prog, "uView");
    s_shadow_color_loc = glGetUniformLocation(s_shadow_prog, "uColor");
    s_shadow_verts_loc = glGetUniformLocation(s_shadow_prog, "uVerts");
    s_shadow_stride_loc = glGetUniformLocation(s_shadow_prog, "uStride");
    s_shadow_light_loc = glGetUniformLocation(s_shadow_prog, "uLight");
    s_shadow_planes_loc = glGetUniformLocation(s_shadow_prog, "uPlanes");
    s_shadow_inst_loc = glGetUniformLocation(s_shadow_prog, "uInst");
    s_shadow_vertbase_loc = glGetUniformLocation(s_shadow_prog, "uVertBase");
    s_shadow_planebase_loc = glGetUniformLocation(s_shadow_prog, "uPlaneBase");
    s_shadow_instbase_loc = glGetUniformLocation(s_shadow_prog, "uInstBase");
}

static void ensure_buffers(void) {
    if (s_line_vao) return;
    glGenVertexArrays(1, &s_line_vao);
    glGenBuffers(1, &s_line_vbo);
    glGenVertexArrays(1, &s_tri_vao);
    glGenBuffers(1, &s_tri_vbo);
    glGenVertexArrays(1, &s_shadow_vao);
    glGenBuffers(1, &s_shadow_vbo);
    glBindVertexArray(s_shadow_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_shadow_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(ShadowVertex), (void *)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenVertexArrays(1, &s_quad_vao);
    glGenBuffers(1, &s_quad_vbo);
}

#ifndef USE_ZPASS
static int extension_present(const char *name) {
    size_t n = strlen(name);
    if (!n || strchr(name, ' ')) return 0;
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; i++) {
        const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (ext && !strcmp(ext, name)) return 1;
    }
    const char *base = (const char *)glGetString(GL_EXTENSIONS), *ext = base;
    if (!ext) return 0;
    while ((ext = strstr(ext, name))) {
        if ((ext == base || ext[-1] == ' ') &&
            (ext[n] == ' ' || ext[n] == '\0')) return 1;
        ext += n;
    }
    return 0;
}

static int shadows_supported(void) {
    static int checked, supported;
    if (!checked) {
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        supported = major > 3 || (major == 3 && minor >= 2) ||
                    extension_present("GL_ARB_depth_clamp") ||
                    extension_present("GL_NV_depth_clamp") ||
                    extension_present("GL_EXT_depth_clamp");
        checked = 1;
    }
    return supported;
}
#endif

static vec3 render_srgb_to_linear(vec3 color) {
    return v3(powf(color.x, 2.2f), powf(color.y, 2.2f), powf(color.z, 2.2f));
}

static void draw_mesh_flat_gradient_vbo(Mesh *m, vec3 color, int gradient, vec3 from, vec3 to);
static void draw_mesh_flat_vbo(Mesh *m, vec3 color) { draw_mesh_flat_gradient_vbo(m, color, 0, v3(1,1,1), v3(1,1,1)); }

/* A gradient multiplies the colour by from..to (sRGB multipliers) along Vertex.g. */
static void draw_mesh_flat_gradient_vbo(Mesh *m, vec3 color, int gradient, vec3 from, vec3 to) {
    ensure_buffers();
    ensure_line_prog();
    if (!s_line_prog) return;

    int n = m->ntris * 3;
    LineVert *verts = malloc((size_t)n * sizeof(LineVert));
    for (int i = 0; i < m->ntris; i++) {
        Tri t = m->tris[i];
        for (int j = 0; j < 3; j++) {
            Vertex *v = j == 0 ? &m->verts[t.a] : j == 1 ? &m->verts[t.b] : &m->verts[t.c];
            vec3 c = color;
            if (gradient) {
                vec3 a = render_srgb_to_linear(from), b = render_srgb_to_linear(to);
                float g = v->g < 0 ? 0 : v->g > 1 ? 1 : v->g;
                c = v3(color.x * (a.x + (b.x - a.x) * g), color.y * (a.y + (b.y - a.y) * g), color.z * (a.z + (b.z - a.z) * g));
            }
            verts[i * 3 + j] = (LineVert){ v->pos.x, v->pos.y, v->pos.z, c.x, c.y, c.z };
        }
    }

    glUseProgram(s_line_prog);
    glBindVertexArray(s_tri_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_tri_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(LineVert)), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void *)(3 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, n);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindVertexArray(0);
    glUseProgram(0);
    free(verts);
}

static void draw_lines_vbo(OverlayLine *lines, int n, int hide_chars, int hide_lights,
                           const char *active_camera) {
    ensure_buffers();
    ensure_line_prog();
    if (!s_line_prog || n <= 0) return;

    LineVert *verts = malloc((size_t)n * 2 * sizeof(LineVert));
    int count = 0;
    for (int i = 0; i < n; i++) {
        OverlayLine *ln = &lines[i];
        if (hide_chars && ln->category == 0) continue;
        if (hide_lights && ln->category >= 1) continue;
        if (ln->camera[0] && active_camera && strcmp(ln->camera, active_camera)) continue;
        vec3 c = render_srgb_to_linear(ln->color);
        verts[count++] = (LineVert){ ln->start.x, ln->start.y, ln->start.z, c.x, c.y, c.z };
        verts[count++] = (LineVert){ ln->end.x,   ln->end.y,   ln->end.z,   c.x, c.y, c.z };
    }

    if (count > 0) {
        glUseProgram(s_line_prog);
        glBindVertexArray(s_line_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_line_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(LineVert)), verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), (void *)(3 * sizeof(float)));
        glDrawArrays(GL_LINES, 0, count);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glBindVertexArray(0);
        glUseProgram(0);
    }
    free(verts);
}

static void draw_stencil_debug_quad(mat4 proj, mat4 view) {
    ensure_buffers();
    ensure_line_prog();
    if (!s_line_prog) return;

    float verts[] = { -1,-1,0, 1,0,0,  1,-1,0, 1,0,0,  1,1,0, 1,0,0,  -1,1,0, 1,0,0 };
    glUseProgram(s_line_prog);
    glBindVertexArray(s_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindVertexArray(0);
    glUseProgram(0);
}

/* ---- Doom 3 style light culling helpers ---- */

#define SHADOW_NEAR_REACH 0.5f   /* m: how far the near plane can reach past the eye into a volume */

typedef struct { int x0, y0, x1, y1; } ScreenRect;   /* pixels, half open, origin bottom left */
static int rect_empty(ScreenRect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }
static ScreenRect rect_isect(ScreenRect a, ScreenRect b) {
    ScreenRect r = { a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1 };
    return r;
}
static ScreenRect rect_union(ScreenRect a, ScreenRect b) {
    if (rect_empty(a)) return b;
    if (rect_empty(b)) return a;
    ScreenRect r = { a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1 };
    return r;
}
static void set_scissor(ScreenRect r) { glScissor(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0); }

/* The pixels a world box can touch. Its edges are clipped to the near plane before projecting, so a box
   around or partly behind the camera still gets the right rectangle (Doom 3's ProjectedFullyClippedBounds);
   empty when the box is wholly behind the camera or off screen. */
static ScreenRect box_screen_rect(mat4 viewProj, vec3 lo, vec3 hi, int w, int h) {
    const float *m = viewProj.m;
    float c[8][4];
    for (int i = 0; i < 8; i++) {
        float x = (i & 1) ? hi.x : lo.x, y = (i & 2) ? hi.y : lo.y, z = (i & 4) ? hi.z : lo.z;
        c[i][0] = m[0] * x + m[4] * y + m[8] * z + m[12];
        c[i][1] = m[1] * x + m[5] * y + m[9] * z + m[13];
        c[i][2] = m[2] * x + m[6] * y + m[10] * z + m[14];
        c[i][3] = m[3] * x + m[7] * y + m[11] * z + m[15];
    }
    float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (int i = 0; i < 8; i++) for (int bit = 1; bit <= 4; bit <<= 1) {
        if (i & bit) continue;
        float a[4], b[4];
        memcpy(a, c[i], sizeof(a)); memcpy(b, c[i | bit], sizeof(b));
        float da = a[2] + a[3], db = b[2] + b[3];   /* >= 0 is in front of the near plane */
        if (da < 0 && db < 0) continue;
        if (da < 0) { float t = da / (da - db); for (int k = 0; k < 4; k++) a[k] += (b[k] - a[k]) * t; }
        else if (db < 0) { float t = db / (db - da); for (int k = 0; k < 4; k++) b[k] += (a[k] - b[k]) * t; }
        float *p[2] = { a, b };
        for (int k = 0; k < 2; k++) {
            float iw = 1.0f / fmaxf(p[k][3], 1e-6f);
            float sx = p[k][0] * iw, sy = p[k][1] * iw;
            x0 = fminf(x0, sx); x1 = fmaxf(x1, sx); y0 = fminf(y0, sy); y1 = fmaxf(y1, sy);
        }
    }
    ScreenRect r = { 0, 0, 0, 0 };
    if (x0 > x1) return r;
    r.x0 = (int)floorf(fmaxf((x0 * 0.5f + 0.5f) * (float)w, 0.0f)) - 1;  r.x1 = (int)ceilf(fminf((x1 * 0.5f + 0.5f) * (float)w, (float)w)) + 1;
    r.y0 = (int)floorf(fmaxf((y0 * 0.5f + 0.5f) * (float)h, 0.0f)) - 1;  r.y1 = (int)ceilf(fminf((y1 * 0.5f + 0.5f) * (float)h, (float)h)) + 1;
    ScreenRect full = { 0, 0, w, h };
    return rect_isect(r, full);
}

/* A light's volume of influence: the range box of a point light, else the scene; origin is where its rays start. */
static void light_bounds(const Light *L, vec3 sceneLo, vec3 sceneHi, vec3 *lo, vec3 *hi, vec3 *origin) {
    if (!L->isDirectional && L->range > 0) {
        *lo = v3(L->pos.x - L->range, L->pos.y - L->range, L->pos.z - L->range);
        *hi = v3(L->pos.x + L->range, L->pos.y + L->range, L->pos.z + L->range);
        *origin = L->pos;
        return;
    }
    *lo = sceneLo; *hi = sceneHi;
    *origin = L->isDirectional ? vscale(L->dir, -10000.0f) : L->pos;
}

/* Doom 3's R_ShadowBounds: the model's box stretched, on each axis where the light origin is not
   beyond it, out to the light's bounds. Everything the caster's shadow can reach lies inside. */
static void shadow_bounds(vec3 mlo, vec3 mhi, vec3 llo, vec3 lhi, vec3 o, vec3 *slo, vec3 *shi) {
    *slo = v3(mlo.x > o.x ? mlo.x : llo.x, mlo.y > o.y ? mlo.y : llo.y, mlo.z > o.z ? mlo.z : llo.z);
    *shi = v3(o.x > mhi.x ? mhi.x : lhi.x, o.y > mhi.y ? mhi.y : lhi.y, o.z > mhi.z ? mhi.z : lhi.z);
}

/* Where the light's rays start, for the shadow shader: a sun stands far away against its direction. */
static vec3 shadow_light_origin(const Light *L) { return L->isDirectional ? vscale(L->dir, -10000.0f) : L->pos; }

/* Doom 3's R_ViewPotentiallyInsideInfiniteShadowVolume, conservatively: the view (grown by the near plane)
   inside the box the caster's shadow can reach needs z-fail and caps; anywhere else z-pass and sides do. */
static int view_may_be_inside(vec3 sLo, vec3 sHi, vec3 eye, float reach) {
    return eye.x >= sLo.x - reach && eye.x <= sHi.x + reach && eye.y >= sLo.y - reach && eye.y <= sHi.y + reach &&
           eye.z >= sLo.z - reach && eye.z <= sHi.z + reach;
}

static void shadow_pass_begin(const Light *L, GLuint instances) {
    ensure_shadow_prog();
    glUseProgram(s_shadow_prog);
    vec3 o = shadow_light_origin(L);
    glUniform3f(s_shadow_light_loc, o.x, o.y, o.z);
    glUniform1i(s_shadow_verts_loc, 0);
    glUniform1i(s_shadow_inst_loc, 1);
    glUniform1i(s_shadow_planes_loc, 2);
    glUniform1i(s_shadow_stride_loc, vcache_vertex_floats());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, instances);
}
static void shadow_pass_end(void) {
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

/* ---- the frame's draw lists (front end output) ---- */

/* Objects of one model and one render state, drawn in one instanced call. */
typedef struct { Mesh *mesh; int mirrored, ramp, tex, first, count; } batch_t;
typedef struct { Mesh *mesh; int mirrored, zfail, first, count; } shadow_batch_t;   /* casters of one model, drawn in one call */
typedef struct { int obj, zfail; } caster_t;
typedef struct { int active, batchFirst, batchCount, castFirst, castCount; ScreenRect scissor; } light_work_t;

static batch_t *s_batch; static int s_nbatch, s_cbatch;
static caster_t *s_cast; static int s_ncast, s_ccast;                 /* scratch: one light's casters */
static shadow_batch_t *s_sbatch; static int s_nsbatch, s_csbatch;
static float *s_sinst; static int s_nsinst, s_csinst;     /* shadow instances: 3 model rows and the light in model space */
static GLuint s_sinst_buf, s_sinst_tex;
#define SHADOW_INSTANCE_FLOATS 16
static float *s_inst; static int s_ninst, s_cinst;        /* instances; SHADER_INSTANCE_FLOATS each */
static light_work_t *s_work; static int s_cwork;
static GLuint s_inst_buf, s_inst_tex;
static const Scene *s_sortScene;

static int batch_key_cmp(const void *pa, const void *pb) {
    const SceneObj *a = &s_sortScene->objs[*(const int *)pa], *b = &s_sortScene->objs[*(const int *)pb];
    if (a->mesh != b->mesh) return a->mesh < b->mesh ? -1 : 1;
    if (a->mirrored != b->mirrored) return a->mirrored - b->mirrored;
    if (a->ramp != b->ramp) return a->ramp - b->ramp;
    return a->texIndex - b->texIndex;
}

/* Sorts the objects by model and state (Doom 3 sorts its draw surfaces by material) and appends their
   instances and batches; returns the first batch, *count how many. */
static int add_batches(Scene *s, int *idx, int n, int *count) {
    int first = s_nbatch;
    s_sortScene = s;
    qsort(idx, (size_t)n, sizeof(int), batch_key_cmp);
    if ((s_ninst + n) > s_cinst) {
        s_cinst = (s_ninst + n) * 2;
        s_inst = realloc(s_inst, (size_t)s_cinst * SHADER_INSTANCE_FLOATS * sizeof(float));
    }
    for (int i = 0; i < n; i++) {
        SceneObj *o = &s->objs[idx[i]];
        batch_t *last = s_nbatch > first ? &s_batch[s_nbatch - 1] : NULL;
        if (!last || last->mesh != o->mesh || last->mirrored != o->mirrored || last->ramp != o->ramp || last->tex != o->texIndex) {
            batch_t b = { o->mesh, o->mirrored, o->ramp, o->texIndex, s_ninst, 0 };
            DA_PUSH(s_batch, s_nbatch, s_cbatch, b);
            last = &s_batch[s_nbatch - 1];
            vcache_prepare_mesh(o->mesh);
        }
        shader_fill_instance(s_inst + (size_t)s_ninst * SHADER_INSTANCE_FLOATS, o);
        s_ninst++; last->count++;
    }
    *count = s_nbatch - first;
    return first;
}

static int caster_cmp(const void *pa, const void *pb) {
    const caster_t *a = pa, *b = pb;
    const SceneObj *oa = &s_sortScene->objs[a->obj], *ob = &s_sortScene->objs[b->obj];
    if (oa->mesh != ob->mesh) return oa->mesh < ob->mesh ? -1 : 1;
    if (oa->mirrored != ob->mirrored) return oa->mirrored - ob->mirrored;
    return a->zfail - b->zfail;
}

/* Turns one light's casters into instanced batches; returns the first, *count how many. */
static int add_shadow_batches(Scene *s, const Light *L, caster_t *cast, int n, int *count) {
    int first = s_nsbatch;
    s_sortScene = s;
    qsort(cast, (size_t)n, sizeof(caster_t), caster_cmp);
    if (s_nsinst + n > s_csinst) {
        s_csinst = (s_nsinst + n) * 2;
        s_sinst = realloc(s_sinst, (size_t)s_csinst * SHADOW_INSTANCE_FLOATS * sizeof(float));
    }
    for (int i = 0; i < n; i++) {
        SceneObj *o = &s->objs[cast[i].obj];
        shadow_batch_t *last = s_nsbatch > first ? &s_sbatch[s_nsbatch - 1] : NULL;
        if (!last || last->mesh != o->mesh || last->mirrored != o->mirrored || last->zfail != cast[i].zfail) {
            shadow_batch_t b = { o->mesh, o->mirrored, cast[i].zfail, s_nsinst, 0 };
            DA_PUSH(s_sbatch, s_nsbatch, s_csbatch, b);
            last = &s_sbatch[s_nsbatch - 1];
            vcache_prepare_mesh(o->mesh);
            vcache_prepare_shadow(o->mesh);
        }
        float *t = s_sinst + (size_t)s_nsinst * SHADOW_INSTANCE_FLOATS;
        const float *m = o->model.m;
        for (int r = 0; r < 3; r++) { t[r * 4] = m[r]; t[r * 4 + 1] = m[4 + r]; t[r * 4 + 2] = m[8 + r]; t[r * 4 + 3] = m[12 + r]; }
        mat4 inv = mat4_affine_inverse(o->model);   /* the light in the model's own space (Doom 3's localLightOrigin) */
        vec3 l = L->isDirectional ? vscale(mat4_xform_dir(inv, L->dir), -1.0f) : mat4_xform_point(inv, L->pos);
        t[12] = l.x; t[13] = l.y; t[14] = l.z; t[15] = L->isDirectional ? 0.0f : 1.0f;
        s_nsinst++; last->count++;
    }
    *count = s_nsbatch - first;
    return first;
}

static void draw_shadow_batches(int first, int count) {
    for (int i = first; i < first + count; i++) {
        shadow_batch_t *b = &s_sbatch[i];
        int firstVertex, firstPlane;
        unsigned verts = vcache_mesh_texture(b->mesh, &firstVertex), planes = vcache_shadow_planes(b->mesh, &firstPlane);
        if (!planes) continue;
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, verts);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_BUFFER, planes);
        glUniform1i(s_shadow_vertbase_loc, firstVertex);
        glUniform1i(s_shadow_planebase_loc, firstPlane);
        glUniform1i(s_shadow_instbase_loc, b->first);
        glFrontFace(b->mirrored ? GL_CW : GL_CCW);   /* a mirrored model's faces turn the other way */
        if (b->zfail) {   /* the view may be inside the volume: count behind the depth, caps included */
            glStencilOpSeparate(GL_BACK, GL_KEEP, GL_INCR_WRAP, GL_KEEP);
            glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_DECR_WRAP, GL_KEEP);
        } else {
            glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
            glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        }
        vcache_draw_shadow(b->mesh, b->zfail, b->count);
    }
    glFrontFace(GL_CCW);
}

static void upload_buffer_texture(GLuint *buf, GLuint *tex, const float *data, size_t floats) {
    if (!*buf) { glGenBuffers(1, buf); glGenTextures(1, tex); }
    glBindBuffer(GL_TEXTURE_BUFFER, *buf);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)((floats ? floats : 4) * sizeof(float)), floats ? data : NULL, GL_STREAM_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, *tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, *buf);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

static void upload_instances(void) {
    upload_buffer_texture(&s_inst_buf, &s_inst_tex, s_inst, (size_t)s_ninst * SHADER_INSTANCE_FLOATS);
    upload_buffer_texture(&s_sinst_buf, &s_sinst_tex, s_sinst, (size_t)s_nsinst * SHADOW_INSTANCE_FLOATS);
}

static void draw_batches(Scene *s, int first, int count, int ramps) {
    for (int i = first; i < first + count; i++) {
        batch_t *b = &s_batch[i];
        if (b->mirrored) glFrontFace(GL_CW);   /* a mirroring matrix turns the faces around */
        shader_set_ramp(ramps && b->ramp >= 0 ? s->ramps[b->ramp].shade : NULL);
#ifdef SCENER_USE_TEXTURES
        shader_set_texture(b->tex >= 0 ? s->materialTextures[b->tex] : s->whiteTexture);
#else
        (void)s;
#endif
        shader_draw_instances(b->mesh, b->first, b->count);
        if (b->mirrored) glFrontFace(GL_CCW);
    }
}

void render_frame(Scene *s, int w, int h, mat4 proj, mat4 view,
                  vec3 camPos, vec3 camLook, int flags) {
    if (!scene_is_prefab_mode(s)) scene_rebuild_camera_gizmos(s, h > 0 ? (float)w / (float)h : 1.0f);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_POLYGON_OFFSET_LINE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE); glStencilMask(0xFF);
    glFrontFace(GL_CCW); glCullFace(GL_BACK); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthRange(0.0, 1.0);
    glEnable(GL_FRAMEBUFFER_SRGB);
    glViewport(0, 0, w, h);
    vcache_collect();   /* vertex cache ranges of meshes freed since the last frame */
    vec3 bg = render_srgb_to_linear(s->bg);
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    ensure_buffers();
    ensure_line_prog();
    ensure_shadow_prog();

    mat4 viewProj = mat4_mul(proj, view);

    if (s_line_prog) {
        glUseProgram(s_line_prog);
        glUniformMatrix4fv(s_line_viewproj_loc, 1, GL_FALSE, viewProj.m);
        glUseProgram(0);
    }
    if (s_shadow_prog) {
        glUseProgram(s_shadow_prog);
        glUniformMatrix4fv(s_shadow_proj_loc, 1, GL_FALSE, proj.m);
        glUniformMatrix4fv(s_shadow_view_loc, 1, GL_FALSE, view.m);
        glUniform4f(s_shadow_color_loc, 1.0f, 0.0f, 0.0f, 1.0f);
        glUseProgram(0);
    }

    if(flags & DBG_WIREFRAME){
        glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
        for(int i=0;i<s->nobjs;i++)if(s->objs[i].renderable){ Mesh w; scene_obj_world_mesh(&s->objs[i],&w); draw_mesh_flat_vbo(&w,v3(1,1,1)); mesh_free(&w); }
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        return;
    }

    /* ---- Front end: decide what the frame draws, build and upload it. Nothing is drawn here, because an upload
       between draws ends the driver's render pass (Doom 3 finishes its front end before the back end runs). ---- */
    vec3 ambient = render_srgb_to_linear(s->ambient);
    s_nbatch = s_ninst = s_nsbatch = s_nsinst = 0;
    static int *list, *screens; static int listCap;
    if (listCap < s->nobjs) {
        free(list); free(screens);
        list = malloc((size_t)s->nobjs * sizeof(int)); screens = malloc((size_t)s->nobjs * sizeof(int)); listCap = s->nobjs;
    }
    if (s_cwork < s->nlights) { free(s_work); s_work = malloc((size_t)s->nlights * sizeof(light_work_t)); s_cwork = s->nlights; }

    /* the depth-filling pass: everything in view; screens with an image draw as images */
    int nlist = 0, nscreens = 0;
    vec3 sceneLo = v3(INFINITY, INFINITY, INFINITY), sceneHi = v3(-INFINITY, -INFINITY, -INFINITY);
    for (int i = 0; i < s->nobjs; i++) {
        SceneObj *o = &s->objs[i];
        if (!o->mesh || !o->mesh->nverts) continue;
        sceneLo = v3(fminf(sceneLo.x, o->bmin.x), fminf(sceneLo.y, o->bmin.y), fminf(sceneLo.z, o->bmin.z));
        sceneHi = v3(fmaxf(sceneHi.x, o->bmax.x), fmaxf(sceneHi.y, o->bmax.y), fmaxf(sceneHi.z, o->bmax.z));
        if (!o->renderable || rect_empty(box_screen_rect(viewProj, o->bmin, o->bmax, w, h))) continue;
        if (o->screenTexture >= 0 && !(flags & DBG_FLAT) && screen_image_ready(&s->screenTextures[o->screenTexture])) screens[nscreens++] = i;
        else list[nlist++] = i;
    }
    int fillCount, fillFirst = add_batches(s, list, nlist, &fillCount);

    /* Lights, the way Doom 3 does them (tr_frontend_addlights.cpp): each light gets the screen rectangle of its
       clipped bounds and drops out when that is empty; only surfaces inside it are lit; a light with no lit
       surface on screen draws no shadows; a caster is drawn only if the box its shadow can reach
       (R_ShadowBounds) meets the lit rectangle, and under that scissor. */
    for (int li = 0; li < s->nlights && !(flags & DBG_FLAT); li++) {
        Light *L = &s->lights[li];
        light_work_t *wk = &s_work[li];
        memset(wk, 0, sizeof(*wk));
        vec3 lightLo, lightHi, lightOrigin;
        light_bounds(L, sceneLo, sceneHi, &lightLo, &lightHi, &lightOrigin);
        ScreenRect lightRect = box_screen_rect(viewProj, lightLo, lightHi, w, h);
        if (rect_empty(lightRect)) continue;   /* the light cannot be seen from here */

        int nlit = 0; ScreenRect litUnion = {0, 0, 0, 0};
        for (int i = 0; i < s->nobjs; i++) {
            SceneObj *o = &s->objs[i];
            if (!o->renderable || o->unlit || !o->mesh || !o->mesh->nverts || !light_reaches_obj(L, o)) continue;
            ScreenRect r = rect_isect(box_screen_rect(viewProj, o->bmin, o->bmax, w, h), lightRect);
            if (rect_empty(r)) continue;
            list[nlit++] = i; litUnion = rect_union(litUnion, r);
        }
        if (!nlit) continue;   /* nothing it lights is on screen, so its shadows would be invisible too */
        wk->active = 1;
        wk->scissor = rect_isect(lightRect, litUnion);
        wk->batchFirst = add_batches(s, list, nlit, &wk->batchCount);

        s_ncast = 0;
        int shadows = !(flags & DBG_NO_SHADOWS) && L->castsShadow;
#ifndef USE_ZPASS
        shadows = shadows && shadows_supported();
#endif
        for (int i = 0; shadows && i < s->nobjs; i++) {
            SceneObj *o = &s->objs[i];
            if (!o->castsShadow || !o->mesh || !o->mesh->nverts || !light_reaches_obj(L, o)) continue;
            vec3 sLo, sHi;
            shadow_bounds(o->bmin, o->bmax, lightLo, lightHi, lightOrigin, &sLo, &sHi);
            if (rect_empty(rect_isect(box_screen_rect(viewProj, sLo, sHi, w, h), wk->scissor))) continue;   /* its shadow cannot reach anything on screen */
            caster_t c = { i, view_may_be_inside(sLo, sHi, camPos, SHADOW_NEAR_REACH) };
            DA_PUSH(s_cast, s_ncast, s_ccast, c);
        }
        wk->castFirst = add_shadow_batches(s, L, s_cast, s_ncast, &wk->castCount);
    }
    vcache_flush();
    upload_instances();
    if (getenv("SCENER_MEM")) fprintf(stderr, "FRAME batches %d (fill %d) instances %d shadow batches %d instances %d\n", s_nbatch, fillCount, s_ninst, s_nsbatch, s_nsinst); /* TEMP */

    /* ---- Back end: draws only. ---- */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    for (int k = 0; k < nscreens; k++) draw_screen_image(&s->objs[screens[k]], &s->screenTextures[s->objs[screens[k]].screenTexture], viewProj);
    shader_bind();
    shader_set_viewproj(viewProj);
    shader_set_camera_pos(camPos);
    shader_set_instances(s_inst_tex);

    /* the depth-filling pass: ambient light and the sky mirrored in glossy surfaces, in one draw per batch */
    if (flags & DBG_FLAT) shader_set_ambient(1, v3(1, 1, 1), 0, v3(0, 0, 0));
    else shader_set_ambient(1, ambient, s->skyReflection > 0, vscale(s->ambient, s->skyReflection));
    draw_batches(s, fillFirst, fillCount, 0);
    shader_set_ambient(0, v3(0, 0, 0), 0, v3(0, 0, 0));
    if (flags & DBG_FLAT) { shader_unbind(); return; }

    for (int li = 0; li < s->nlights; li++) {
        light_work_t *wk = &s_work[li];
        if (!wk->active) continue;
        Light *L = &s->lights[li];
        shader_set_light(L);
        glEnable(GL_SCISSOR_TEST);
        if (wk->castCount > 0) {
            set_scissor(wk->scissor);
            glClear(GL_STENCIL_BUFFER_BIT);
            shader_unbind();
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glDisable(GL_CULL_FACE);
            glEnable(GL_DEPTH_CLAMP);   /* far caps and sides at infinity stay inside the depth range */
            shadow_pass_begin(L, s_sinst_tex);
            if (!getenv("SCENER_NOSH")) draw_shadow_batches(wk->castFirst, wk->castCount);
            shadow_pass_end();
            glDisable(GL_DEPTH_CLAMP);

            if (flags & DBG_WIRE_SHADOWVOL) {
                glDisable(GL_STENCIL_TEST);
                glEnable(GL_POLYGON_OFFSET_LINE);
                glPolygonOffset(-1, -1);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthFunc(GL_LEQUAL);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                shadow_pass_begin(L, s_sinst_tex);
                draw_shadow_batches(wk->castFirst, wk->castCount);
                shadow_pass_end();
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glDisable(GL_POLYGON_OFFSET_LINE);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthFunc(GL_LESS);
                glEnable(GL_STENCIL_TEST);
            }

            shader_bind();
            shader_set_instances(s_inst_tex);
            glEnable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glStencilFunc(GL_EQUAL, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }
        set_scissor(wk->scissor);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        if (!getenv("SCENER_NOLIT")) draw_batches(s, wk->batchFirst, wk->batchCount, 1);
        glDisable(GL_BLEND);
        if (wk->castCount > 0) glDisable(GL_STENCIL_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_SCISSOR_TEST);
        glFlush();   /* hand this light's work to the GPU: the driver otherwise queues every light's passes in memory */
    }

    shader_unbind();
    glDepthMask(GL_TRUE);

    /* Overlay lines */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    draw_lines_vbo(s->overlayLines, s->noverlayLines,
                   flags & DBG_HIDE_CHARS, flags & DBG_HIDE_LIGHTS,
                   s->activeCamera);

    if (flags & DBG_SHOW_STENCIL) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        draw_stencil_debug_quad(proj, view);
        glDisable(GL_STENCIL_TEST);
        glDepthMask(GL_TRUE);
    }

    if (!(flags & DBG_HIDE_GIZMOS))
        gizmo_draw(s, camPos, camLook, s->camFov, w, h);
}
