#include <orion/user/gl_compat.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vertex_cache.h"

#define VCACHE_CHUNK_VERTS   131072       /* vertices a mesh chunk holds (a larger mesh gets a chunk its size) */
#define VCACHE_INDEX_RATIO   5            /* indices per vertex a chunk reserves (generated meshes run ~4.5) */
#define VCACHE_SHADOW_VERTS  196608       /* shadow vertices a shadow chunk holds (three per triangle) */
#define VCACHE_SHADOW_INDEX_BYTES 8       /* index bytes a chunk reserves per shadow vertex (about four 16-bit indices) */

/* Doom 3 keeps 32-byte idDrawVerts with byte normals and 16-bit indices; the GPU copy here is the same idea:
   position, normal packed into 10-bit components, gradient parameter. 20 bytes instead of 36. */
typedef struct { float x, y, z; unsigned n; float g; } gpu_vertex_t;

typedef struct {
    GLuint vao, vbo, ibo, tex;                  /* tex: the vertices as a buffer texture, read by the shadow shader */
    int vcap, vused, ibytes, iused, live;
    int vsent, isent;                           /* how much the GPU already has */
    gpu_vertex_t *sv; unsigned char *si;        /* CPU staging, sent at flush */
} geom_chunk_t;
/* A model's shadow mesh, the same for every light (the shadow shader picks the silhouette): three vertices per
   triangle, each a reference (model vertex, triangle); the triangles' planes in a buffer texture; indices for a
   quad on every edge, then for every triangle. Staged on the CPU and sent once, at flush. */
typedef struct { int vertex, tri; } shadow_ref_t;   /* tri -1: a vertex that always counts as facing away (open edges) */
typedef struct {
    GLuint vao, vbo, ibo, pbo, ptex;
    int vcap, vused, ibytes, iused, pcap, pused, live;
    int vsent, isent, psent;                         /* how much the GPU already has */
    shadow_ref_t *sv; unsigned char *si; float *sp;  /* CPU staging, dropped at flush */
} shadow_chunk_t;

static geom_chunk_t *s_geom; static int s_ngeom, s_cgeom;
static shadow_chunk_t *s_shadow; static int s_nshadow, s_cshadow;
static GLint s_locPos = -1, s_locNrm = -1, s_locGrad = -1;

void vcache_set_attribs(int pos, int nrm, int grad) { s_locPos = pos; s_locNrm = nrm; s_locGrad = grad; }
int vcache_vertex_floats(void) { return (int)(sizeof(gpu_vertex_t) / sizeof(float)); }

static unsigned pack_normal(vec3 n) {
    int x = (int)lroundf(fmaxf(-1, fminf(1, n.x)) * 511.0f), y = (int)lroundf(fmaxf(-1, fminf(1, n.y)) * 511.0f), z = (int)lroundf(fmaxf(-1, fminf(1, n.z)) * 511.0f);
    return ((unsigned)x & 0x3ffu) | (((unsigned)y & 0x3ffu) << 10) | (((unsigned)z & 0x3ffu) << 20);
}

static int geom_chunk_for(int nv, int ibytes) {
    for (int i = 0; i < s_ngeom; i++) {
        geom_chunk_t *c = &s_geom[i];
        if (!c->live && c->vused) { c->vused = c->iused = c->vsent = c->isent = 0; }
        if (c->vcap - c->vused >= nv && c->ibytes - ((c->iused + 3) & ~3) >= ibytes) return i;
    }
    int vcap = nv > VCACHE_CHUNK_VERTS ? nv : VCACHE_CHUNK_VERTS;
    int icap = vcap * VCACHE_INDEX_RATIO * 2;   /* bytes, for 16-bit indices */
    if (icap < ibytes) icap = ibytes;
    geom_chunk_t c = {0};
    c.vcap = vcap; c.ibytes = icap;
    glGenVertexArrays(1, &c.vao); glGenBuffers(1, &c.vbo); glGenBuffers(1, &c.ibo);
    glBindVertexArray(c.vao);
    glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vcap * sizeof(gpu_vertex_t)), NULL, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)icap, NULL, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)s_locPos);
    glVertexAttribPointer((GLuint)s_locPos, 3, GL_FLOAT, GL_FALSE, sizeof(gpu_vertex_t), (void *)offsetof(gpu_vertex_t, x));
    glEnableVertexAttribArray((GLuint)s_locNrm);
    glVertexAttribPointer((GLuint)s_locNrm, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(gpu_vertex_t), (void *)offsetof(gpu_vertex_t, n));
    if (s_locGrad >= 0) {
        glEnableVertexAttribArray((GLuint)s_locGrad);
        glVertexAttribPointer((GLuint)s_locGrad, 1, GL_FLOAT, GL_FALSE, sizeof(gpu_vertex_t), (void *)offsetof(gpu_vertex_t, g));
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenTextures(1, &c.tex);
    glBindTexture(GL_TEXTURE_BUFFER, c.tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, c.vbo);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    DA_PUSH(s_geom, s_ngeom, s_cgeom, c);
    return s_ngeom - 1;
}

/* Stages a mesh in its chunk; vcache_flush sends it. One upload per chunk instead of two per mesh: every upload
   to a buffer the GPU is using makes the driver keep another copy of it. */
static void mesh_upload(Mesh *m) {
    if (m->gpuChunk && !m->gpuDirty) return;
    if (!m->gpuChunk) {
        int ni = m->ntris * 3, wide = m->nverts > 65536, size = wide ? 4 : 2;
        int k = geom_chunk_for(m->nverts, ni * size);
        geom_chunk_t *c = &s_geom[k];
        if (wide) c->iused = (c->iused + 3) & ~3;
        m->gpuChunk = k + 1; m->gpuVertex = (unsigned)c->vused; m->gpuIndex = (unsigned)c->iused; m->gpuWide = wide;
        c->vused += m->nverts; c->iused += ni * size; c->live++;
        if (!c->si) c->si = malloc((size_t)c->ibytes);
        if (wide) memcpy(c->si + m->gpuIndex, m->tris, (size_t)ni * 4);
        else {
            unsigned short *ix = (unsigned short *)(c->si + m->gpuIndex);
            const int *src = (const int *)m->tris;
            for (int i = 0; i < ni; i++) ix[i] = (unsigned short)src[i];
        }
    }
    geom_chunk_t *c = &s_geom[m->gpuChunk - 1];
    if (!c->sv) c->sv = malloc((size_t)c->vcap * sizeof(gpu_vertex_t));
    gpu_vertex_t *g = c->sv + m->gpuVertex;
    for (int i = 0; i < m->nverts; i++) {
        const Vertex *s = &m->verts[i];
        g[i].x = s->pos.x; g[i].y = s->pos.y; g[i].z = s->pos.z; g[i].n = pack_normal(s->nrm); g[i].g = s->g;
    }
    if ((int)m->gpuVertex < c->vsent) c->vsent = (int)m->gpuVertex;   /* changed vertices of a mesh already sent */
    m->gpuDirty = 0;
}

static void geom_flush(void) {
    for (int i = 0; i < s_ngeom; i++) {
        geom_chunk_t *c = &s_geom[i];
        if (c->sv && c->vused > c->vsent) {
            glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
            glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)((size_t)c->vsent * sizeof(gpu_vertex_t)), (GLsizeiptr)((size_t)(c->vused - c->vsent) * sizeof(gpu_vertex_t)), c->sv + c->vsent);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        if (c->si && c->iused > c->isent) {
            glBindVertexArray(c->vao);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)c->isent, (GLsizeiptr)(c->iused - c->isent), c->si + c->isent);
            glBindVertexArray(0);
        }
        c->vsent = c->vused; c->isent = c->iused;
        if (c->vcap - c->vused < 2048) { free(c->sv); free(c->si); c->sv = NULL; c->si = NULL; }   /* full: no more staging needed */
    }
}

void vcache_prepare_mesh(Mesh *m) { if (m && m->ntris) mesh_upload(m); }

void vcache_draw_mesh(Mesh *m, int instances) {
    if (!m->ntris || instances <= 0 || !m->gpuChunk) return;
    glBindVertexArray(s_geom[m->gpuChunk - 1].vao);
    glDrawElementsInstancedBaseVertex(GL_TRIANGLES, m->ntris * 3, m->gpuWide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (void *)(size_t)m->gpuIndex, instances, (GLint)m->gpuVertex);
    glBindVertexArray(0);
}

unsigned vcache_mesh_texture(Mesh *m, int *firstVertex) {
    if (!m->ntris || !m->gpuChunk) return 0;
    *firstVertex = (int)m->gpuVertex;
    return s_geom[m->gpuChunk - 1].tex;
}

void vcache_collect(void) {
    int *chunks, n = mesh_gpu_take_retired(&chunks);
    for (int i = 0; i < n; i++) {   /* geometry chunks are positive, shadow chunks negative */
        int k = chunks[i] > 0 ? chunks[i] - 1 : -chunks[i] - 1;
        if (chunks[i] > 0 && k < s_ngeom && s_geom[k].live > 0) s_geom[k].live--;
        if (chunks[i] < 0 && k < s_nshadow && s_shadow[k].live > 0) s_shadow[k].live--;
    }
    free(chunks);
}

static int shadow_chunk_for(int nv, int ibytes, int np) {
    for (int i = 0; i < s_nshadow; i++) {
        shadow_chunk_t *c = &s_shadow[i];
        if (!c->live && c->vused) { c->vused = c->iused = c->pused = c->vsent = c->isent = c->psent = 0; }
        if (c->vcap - c->vused >= nv && c->ibytes - ((c->iused + 3) & ~3) >= ibytes && c->pcap - c->pused >= np) return i;
    }
    shadow_chunk_t c = {0};
    c.vcap = nv > VCACHE_SHADOW_VERTS ? nv : VCACHE_SHADOW_VERTS;
    c.ibytes = c.vcap * VCACHE_SHADOW_INDEX_BYTES; if (c.ibytes < ibytes) c.ibytes = ibytes;
    c.pcap = c.vcap / 3 + 1; if (c.pcap < np) c.pcap = np;
    glGenVertexArrays(1, &c.vao); glGenBuffers(1, &c.vbo); glGenBuffers(1, &c.ibo); glGenBuffers(1, &c.pbo); glGenTextures(1, &c.ptex);
    glBindVertexArray(c.vao);
    glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)c.vcap * sizeof(shadow_ref_t)), NULL, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 2, GL_INT, sizeof(shadow_ref_t), (void *)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)c.ibytes, NULL, GL_STATIC_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, c.pbo);
    glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)((size_t)c.pcap * 4 * sizeof(float)), NULL, GL_STATIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, c.ptex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, c.pbo);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    DA_PUSH(s_shadow, s_nshadow, s_cshadow, c);
    return s_nshadow - 1;
}

static int tri_corner(const Mesh *m, int t, vec3 p) {   /* the corner of triangle t standing at p */
    const int *ix = &m->tris[t].a; int best = 0; float bd = INFINITY;
    for (int k = 0; k < 3; k++) { float d = vlen(vsub(m->verts[ix[k]].pos, p)); if (d < bd) { bd = d; best = k; } }
    return best;
}

/* Builds the model's shadow mesh once. Needs its edges and face normals. */
void vcache_prepare_shadow(Mesh *m) {
    if (!m || m->shChunk || !m->ntris || !m->nedges || !m->triN) return;
    int nt = m->ntris, nopen = 0;
    for (int i = 0; i < m->nedges; i++) if (m->edges[i].t1 < 0) nopen++;
    int nv = 3 * nt + 2 * nopen, ni = 6 * m->nedges + 3 * nt, wide = nv > 65536, size = wide ? 4 : 2;
    int k = shadow_chunk_for(nv, ni * size, nt);
    shadow_chunk_t *c = &s_shadow[k];
    if (!c->sv) {
        c->sv = malloc((size_t)c->vcap * sizeof(shadow_ref_t)); c->si = malloc((size_t)c->ibytes); c->sp = malloc((size_t)c->pcap * 4 * sizeof(float));
    }
    if (wide) c->iused = (c->iused + 3) & ~3;
    m->shChunk = k + 1; m->shVertex = c->vused; m->shIndex = c->iused; m->shPlane = c->pused; m->shWide = wide;
    m->shSides = 6 * m->nedges; m->shCaps = 3 * nt;
    shadow_ref_t *sv = c->sv + c->vused; float *sp = c->sp + (size_t)c->pused * 4;
    for (int t = 0; t < nt; t++) {
        const int *ix = &m->tris[t].a; vec3 n = m->triN[t];
        for (int j = 0; j < 3; j++) { sv[3 * t + j].vertex = ix[j]; sv[3 * t + j].tri = t; }
        sp[4 * t] = n.x; sp[4 * t + 1] = n.y; sp[4 * t + 2] = n.z; sp[4 * t + 3] = vdot(n, m->verts[ix[0]].pos);
    }
    unsigned *ix = malloc((size_t)ni * sizeof(unsigned)); int n = 0, open = 3 * nt;
    for (int i = 0; i < m->nedges; i++) {
        const Edge *e = &m->edges[i];
        vec3 p0 = m->verts[e->v0].pos, p1 = m->verts[e->v1].pos;
        unsigned a0 = (unsigned)(3 * e->t0 + tri_corner(m, e->t0, p0)), a1 = (unsigned)(3 * e->t0 + tri_corner(m, e->t0, p1)), b0, b1;
        if (e->t1 >= 0) { b0 = (unsigned)(3 * e->t1 + tri_corner(m, e->t1, p0)); b1 = (unsigned)(3 * e->t1 + tri_corner(m, e->t1, p1)); }
        else {
            sv[open].vertex = e->v0; sv[open].tri = -1; b0 = (unsigned)open++;
            sv[open].vertex = e->v1; sv[open].tri = -1; b1 = (unsigned)open++;
        }
        /* the quad between the edge as face A holds it and as face B holds it: a side where one faces the light, nothing where both agree */
        ix[n++] = a1; ix[n++] = a0; ix[n++] = b0;
        ix[n++] = a1; ix[n++] = b0; ix[n++] = b1;
    }
    for (int t = 0; t < nt; t++) { ix[n++] = (unsigned)(3 * t); ix[n++] = (unsigned)(3 * t + 1); ix[n++] = (unsigned)(3 * t + 2); }
    if (wide) memcpy(c->si + c->iused, ix, (size_t)ni * 4);
    else { unsigned short *d = (unsigned short *)(c->si + c->iused); for (int i = 0; i < ni; i++) d[i] = (unsigned short)ix[i]; }
    free(ix);
    c->vused += nv; c->iused += ni * size; c->pused += nt; c->live++;
}

/* Sends what was staged since the last flush and drops the staging. Uploads end the driver's render pass, so
   everything a frame needs is prepared first and flushed once, before the back end draws. */
void vcache_flush(void) {
    geom_flush();
    for (int i = 0; i < s_nshadow; i++) {
        shadow_chunk_t *c = &s_shadow[i];
        if (!c->sv) continue;
        if (c->vused > c->vsent) {
            glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
            glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)((size_t)c->vsent * sizeof(shadow_ref_t)), (GLsizeiptr)((size_t)(c->vused - c->vsent) * sizeof(shadow_ref_t)), c->sv + c->vsent);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        if (c->iused > c->isent) {
            glBindVertexArray(c->vao);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)c->isent, (GLsizeiptr)(c->iused - c->isent), c->si + c->isent);
            glBindVertexArray(0);
        }
        if (c->pused > c->psent) {
            glBindBuffer(GL_TEXTURE_BUFFER, c->pbo);
            glBufferSubData(GL_TEXTURE_BUFFER, (GLintptr)((size_t)c->psent * 16), (GLsizeiptr)((size_t)(c->pused - c->psent) * 16), c->sp + (size_t)c->psent * 4);
            glBindBuffer(GL_TEXTURE_BUFFER, 0);
        }
        c->vsent = c->vused; c->isent = c->iused; c->psent = c->pused;
        if (c->vcap - c->vused < 4096) { free(c->sv); free(c->si); free(c->sp); c->sv = NULL; c->si = NULL; c->sp = NULL; }   /* full: no more staging needed */
    }
}

unsigned vcache_shadow_planes(Mesh *m, int *firstPlane) {
    if (!m->shChunk) return 0;
    *firstPlane = m->shPlane;
    return s_shadow[m->shChunk - 1].ptex;
}

/* Draws the model's shadow mesh: the edge quads, and with caps also every triangle (z-fail). */
void vcache_draw_shadow(Mesh *m, int caps, int instances) {
    if (!m->shChunk || instances <= 0) return;
    glBindVertexArray(s_shadow[m->shChunk - 1].vao);
    glDrawElementsInstancedBaseVertex(GL_TRIANGLES, m->shSides + (caps ? m->shCaps : 0), m->shWide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                                      (void *)(size_t)m->shIndex, instances, m->shVertex);
}

void vcache_shutdown(void) {
    for (int i = 0; i < s_ngeom; i++) { glDeleteVertexArrays(1, &s_geom[i].vao); glDeleteBuffers(1, &s_geom[i].vbo); glDeleteBuffers(1, &s_geom[i].ibo); glDeleteTextures(1, &s_geom[i].tex); free(s_geom[i].sv); free(s_geom[i].si); }
    for (int i = 0; i < s_nshadow; i++) {
        shadow_chunk_t *c = &s_shadow[i];
        glDeleteVertexArrays(1, &c->vao); glDeleteBuffers(1, &c->vbo); glDeleteBuffers(1, &c->ibo); glDeleteBuffers(1, &c->pbo); glDeleteTextures(1, &c->ptex);
        free(c->sv); free(c->si); free(c->sp);
    }
    free(s_geom); free(s_shadow);
    s_geom = NULL; s_shadow = NULL; s_ngeom = s_cgeom = s_nshadow = s_cshadow = 0;
}
