#include <orion/user/gl_compat.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "simplegl.h"
#include "shader.h"

#define MAX_BATCH_VERTS 65536

typedef struct { float x, y, z, r, g, b; } LineVert;

static GLuint s_line_vao, s_line_vbo;
static GLuint s_tri_vao, s_tri_vbo;
static GLuint s_shadow_vao, s_shadow_vbo;
static GLuint s_quad_vao, s_quad_vbo;
static GLuint s_shadow_prog;
static GLint s_shadow_proj_loc, s_shadow_view_loc, s_shadow_color_loc;
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

static const char *s_shadow_vs =
    "#version 150\n"
    "in vec4 aPos;\n"
    "uniform mat4 uProj;\n"
    "uniform mat4 uView;\n"
    "void main(){ gl_Position=uProj*uView*aPos; }\n";

static const char *s_shadow_fs =
    "#version 150\n"
    "uniform vec4 uColor;\n"
    "out vec4 frag;\n"
    "void main(){ frag=uColor; }\n";

static GLuint compile_line_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s,sizeof(log),NULL,log); fprintf(stderr,"[scener] shader compile failed: %s\n",log); glDeleteShader(s); return 0; }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aColor");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p,sizeof(log),NULL,log); fprintf(stderr,"[scener] shader link failed: %s\n",log); glDeleteProgram(p); return 0; }
    return p;
}

static GLuint link_shadow_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p,sizeof(log),NULL,log); fprintf(stderr,"[scener] shader link failed: %s\n",log); glDeleteProgram(p); return 0; }
    return p;
}

void ensure_line_prog(void) {
    if (s_line_prog) return;
    GLuint vs = compile_line_shader(GL_VERTEX_SHADER, s_line_vs);
    GLuint fs = compile_line_shader(GL_FRAGMENT_SHADER, s_line_fs);
    if (vs && fs) {
        s_line_prog = link_program(vs, fs);
        s_line_viewproj_loc = glGetUniformLocation(s_line_prog, "uViewProj");
        if(s_line_prog)fprintf(stderr,"[scener] ambient shader linked\n");
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
}

static void ensure_shadow_prog(void) {
    if (s_shadow_prog) return;
    GLuint vs = compile_line_shader(GL_VERTEX_SHADER, s_shadow_vs);
    GLuint fs = compile_line_shader(GL_FRAGMENT_SHADER, s_shadow_fs);
    if (vs && fs) {
        s_shadow_prog = link_shadow_program(vs, fs);
        s_shadow_proj_loc = glGetUniformLocation(s_shadow_prog, "uProj");
        s_shadow_view_loc = glGetUniformLocation(s_shadow_prog, "uView");
        s_shadow_color_loc = glGetUniformLocation(s_shadow_prog, "uColor");
        if(s_shadow_prog)fprintf(stderr,"[scener] stencil shader linked\n");
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
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

static void begin_shadow_pass(void) {
#ifdef USE_ZPASS
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
#else
    glEnable(GL_DEPTH_CLAMP);
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_INCR_WRAP, GL_KEEP);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_DECR_WRAP, GL_KEEP);
#endif
}

static void end_shadow_pass(void) {
#ifndef USE_ZPASS
    glDisable(GL_DEPTH_CLAMP);
#endif
}

static vec3 render_srgb_to_linear(vec3 color) {
    return v3(powf(color.x, 2.2f), powf(color.y, 2.2f), powf(color.z, 2.2f));
}

static void draw_mesh_flat_vbo(Mesh *m, vec3 color) {
    ensure_buffers();
    ensure_line_prog();
    if (!s_line_prog) return;

    int n = m->ntris * 3;
    LineVert *verts = malloc((size_t)n * sizeof(LineVert));
    for (int i = 0; i < m->ntris; i++) {
        Tri t = m->tris[i];
        for (int j = 0; j < 3; j++) {
            vec3 p = j == 0 ? m->verts[t.a].pos : j == 1 ? m->verts[t.b].pos : m->verts[t.c].pos;
            verts[i * 3 + j] = (LineVert){ p.x, p.y, p.z, color.x, color.y, color.z };
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

static void draw_shadow_volume_vbo(ShadowVolume *sv) {
    ensure_buffers();
    ensure_shadow_prog();
    if (!s_shadow_prog || sv->nverts <= 0) return;

    glUseProgram(s_shadow_prog);
    glBindVertexArray(s_shadow_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_shadow_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sv->nverts * sizeof(ShadowVertex)), sv->verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, sv->nverts);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
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
        for(int i=0;i<s->nobjs;i++)if(s->objs[i].renderable)draw_mesh_flat_vbo(&s->objs[i].mesh,v3(1,1,1));
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        return;
    }

    /* Pass 1: ambient fill (fills depth buffer) */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    vec3 ambient = render_srgb_to_linear(s->ambient);
    for (int i = 0; i < s->nobjs; i++) {
        if (!s->objs[i].renderable) continue;
        vec3 color = render_srgb_to_linear(s->objs[i].color);
        draw_mesh_flat_vbo(&s->objs[i].mesh, (s->objs[i].unlit || (flags & DBG_FLAT)) ? color : vmul(color, ambient));
    }

    if(flags & DBG_FLAT)return;

    /* Per-light pass: shadow volume + PBR lit */
    shader_bind();
    shader_set_viewproj(viewProj);
    shader_set_camera_pos(camPos);

    for (int li = 0; li < s->nlights; li++) {
        Light *L = &s->lights[li];
        shader_set_light(L);

        int drawShadows = !(flags & DBG_NO_SHADOWS) && L->castsShadow && s->svols[li].nverts > 0;
#ifndef USE_ZPASS
        drawShadows = drawShadows && shadows_supported();
#endif
        if (drawShadows) {
            glClear(GL_STENCIL_BUFFER_BIT);
            shader_unbind();
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glDisable(GL_CULL_FACE);
            begin_shadow_pass();
            draw_shadow_volume_vbo(&s->svols[li]);
            end_shadow_pass();

            if (flags & DBG_WIRE_SHADOWVOL) {
                glDisable(GL_STENCIL_TEST);
                glEnable(GL_POLYGON_OFFSET_LINE);
                glPolygonOffset(-1, -1);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthFunc(GL_LEQUAL);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                draw_shadow_volume_vbo(&s->svols[li]);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glDisable(GL_POLYGON_OFFSET_LINE);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthFunc(GL_LESS);
                glEnable(GL_STENCIL_TEST);
            }

            shader_bind();
            glEnable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glStencilFunc(GL_EQUAL, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            for (int i = 0; i < s->nobjs; i++) {
                if (!s->objs[i].renderable || s->objs[i].unlit) continue;
                shader_set_material(s->objs[i].color, s->objs[i].shininess);
#ifdef SCENER_USE_TEXTURES
                shader_set_texture(s->objs[i].texIndex >= 0
                                   ? s->materialTextures[s->objs[i].texIndex]
                                   : s->whiteTexture);
#endif
                shader_draw_mesh(&s->objs[i].mesh);
            }
            glDisable(GL_BLEND);
            glDisable(GL_STENCIL_TEST);
            glDepthFunc(GL_LESS);
        } else {
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            for (int i = 0; i < s->nobjs; i++) {
                if (!s->objs[i].renderable || s->objs[i].unlit) continue;
                shader_set_material(s->objs[i].color, s->objs[i].shininess);
#ifdef SCENER_USE_TEXTURES
                shader_set_texture(s->objs[i].texIndex >= 0
                                   ? s->materialTextures[s->objs[i].texIndex]
                                   : s->whiteTexture);
#endif
                shader_draw_mesh(&s->objs[i].mesh);
            }
            glDisable(GL_BLEND);
            glDepthFunc(GL_LESS);
        }
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
