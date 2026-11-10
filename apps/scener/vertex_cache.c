#include <orion/user/gl_compat.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vertex_cache.h"

#define VCACHE_CHUNK_VERTS   131072       /* vertices a mesh chunk holds (a larger mesh gets a chunk its size) */
#define VCACHE_INDEX_RATIO   5            /* indices per vertex a chunk reserves (generated meshes run ~4.5) */
#define VCACHE_SHADOW_CHUNK  (1 << 20)    /* shadow indices a chunk holds */

/* Doom 3 keeps 32-byte idDrawVerts with byte normals and 16-bit indices; the GPU copy here is the same idea:
   position, normal packed into 10-bit components, gradient parameter. 20 bytes instead of 36. */
typedef struct { float x, y, z; unsigned n; float g; } gpu_vertex_t;

typedef struct {
    GLuint vao, vbo, ibo, tex;                  /* tex: the vertices as a buffer texture, read by the shadow shader */
    int vcap, vused, ibytes, iused, live;
    int vsent, isent;                           /* how much the GPU already has */
    gpu_vertex_t *sv; unsigned char *si;        /* CPU staging, sent and dropped at flush */
} geom_chunk_t;
typedef struct { GLuint vao, ibo; int cap, used, wide; unsigned char *pending; int pendingFirst, pendingBytes, pendingCap; } index_chunk_t;   /* pending: indices allocated since the last flush, one upload away */

static geom_chunk_t *s_geom; static int s_ngeom, s_cgeom;
static index_chunk_t *s_index; static int s_nindex, s_cindex;
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
    if (m->gpuChunk) return;
    int ni = m->ntris * 3, wide = m->nverts > 65536, size = wide ? 4 : 2;
    int k = geom_chunk_for(m->nverts, ni * size);
    geom_chunk_t *c = &s_geom[k];
    if (wide) c->iused = (c->iused + 3) & ~3;
    m->gpuChunk = k + 1; m->gpuVertex = (unsigned)c->vused; m->gpuIndex = (unsigned)c->iused; m->gpuWide = wide;
    if (!c->sv) { c->sv = malloc((size_t)c->vcap * sizeof(gpu_vertex_t)); c->si = malloc((size_t)c->ibytes); }
    if (wide) memcpy(c->si + m->gpuIndex, m->tris, (size_t)ni * 4);
    else {
        unsigned short *ix = (unsigned short *)(c->si + m->gpuIndex);
        const int *src = (const int *)m->tris;
        for (int i = 0; i < ni; i++) ix[i] = (unsigned short)src[i];
    }
    gpu_vertex_t *g = c->sv + m->gpuVertex;
    for (int i = 0; i < m->nverts; i++) {
        const Vertex *s = &m->verts[i];
        g[i].x = s->pos.x; g[i].y = s->pos.y; g[i].z = s->pos.z; g[i].n = pack_normal(s->nrm); g[i].g = s->g;
    }
    c->vused += m->nverts; c->iused += ni * size; c->live++;
}

static void geom_flush(void) {
    for (int i = 0; i < s_ngeom; i++) {
        geom_chunk_t *c = &s_geom[i];
        if (!c->sv) continue;
        if (c->vused > c->vsent) {
            glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
            glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)((size_t)c->vsent * sizeof(gpu_vertex_t)), (GLsizeiptr)((size_t)(c->vused - c->vsent) * sizeof(gpu_vertex_t)), c->sv + c->vsent);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        if (c->iused > c->isent) {
            glBindVertexArray(c->vao);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)c->isent, (GLsizeiptr)(c->iused - c->isent), c->si + c->isent);
            glBindVertexArray(0);
        }
        c->vsent = c->vused; c->isent = c->iused;
        free(c->sv); free(c->si); c->sv = NULL; c->si = NULL;
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
    for (int i = 0; i < n; i++) {
        int k = chunks[i] - 1;
        if (k >= 0 && k < s_ngeom && s_geom[k].live > 0) s_geom[k].live--;
    }
    free(chunks);
}

void vcache_flush(void);
static void index_chunk_flush(index_chunk_t *c) {
    if (!c->pendingBytes) return;
    glBindVertexArray(c->vao);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)((size_t)c->pendingFirst * (c->wide ? 4 : 2)), (GLsizeiptr)c->pendingBytes, c->pending);
    glBindVertexArray(0);
    c->pendingBytes = 0;
}

int vcache_index_alloc(const unsigned *ix, int n, int *chunk, int *first) {
    if (n <= 0) return 0;
    unsigned top = 0;
    for (int i = 0; i < n; i++) if (ix[i] > top) top = ix[i];
    int wide = top > 0xffff, size = wide ? 4 : 2, k = -1;
    for (int i = 0; i < s_nindex; i++) if (s_index[i].wide == wide && s_index[i].cap - s_index[i].used >= n) { k = i; break; }
    if (k < 0) {
        vcache_flush();   /* the chunks before this one are as full as they get: send them and drop their staging */
        int cap = n > VCACHE_SHADOW_CHUNK ? n : VCACHE_SHADOW_CHUNK;
        index_chunk_t c = {0};
        c.cap = cap; c.wide = wide;
        glGenVertexArrays(1, &c.vao); glGenBuffers(1, &c.ibo);
        glBindVertexArray(c.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)cap * (size_t)size), NULL, GL_STATIC_DRAW);
        glBindVertexArray(0);
        DA_PUSH(s_index, s_nindex, s_cindex, c);
        k = s_nindex - 1;
    }
    index_chunk_t *c = &s_index[k];
    int bytes = n * size;
    if (!c->pendingBytes) c->pendingFirst = c->used;
    if (c->pendingBytes + bytes > c->pendingCap) {   /* staged up to the rest of the chunk, so a chunk is sent once */
        c->pendingCap = (c->cap - c->pendingFirst) * size;
        c->pending = realloc(c->pending, (size_t)c->pendingCap);
    }
    if (wide) memcpy(c->pending + c->pendingBytes, ix, (size_t)bytes);
    else {
        unsigned short *narrow = (unsigned short *)(c->pending + c->pendingBytes);
        for (int i = 0; i < n; i++) narrow[i] = (unsigned short)ix[i];
    }
    c->pendingBytes += bytes;
    *chunk = k; *first = c->used; c->used += n;
    return n;
}

/* Sends what is still staged. An upload between draws ends the driver's render pass, and thousands of small
   ones are thousands of passes, so allocations are staged and the back end flushes once before it draws. */
void vcache_flush(void) {
    geom_flush();
    for (int i = 0; i < s_nindex; i++) {
        index_chunk_flush(&s_index[i]);
        free(s_index[i].pending); s_index[i].pending = NULL; s_index[i].pendingCap = 0;
    }
}

void vcache_index_draw(int chunk, int first, int n, int baseVertex) {
    if (chunk < 0 || chunk >= s_nindex || n <= 0) return;
    index_chunk_t *c = &s_index[chunk];
    glBindVertexArray(c->vao);
    glDrawElementsBaseVertex(GL_TRIANGLES, n, c->wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (void *)((size_t)first * (c->wide ? 4 : 2)), baseVertex);
}

void vcache_index_reset(void) {
    for (int i = 0; i < s_nindex; i++) { s_index[i].used = 0; free(s_index[i].pending); s_index[i].pending = NULL; s_index[i].pendingBytes = s_index[i].pendingCap = 0; }
}

void vcache_shutdown(void) {
    for (int i = 0; i < s_ngeom; i++) { glDeleteVertexArrays(1, &s_geom[i].vao); glDeleteBuffers(1, &s_geom[i].vbo); glDeleteBuffers(1, &s_geom[i].ibo); glDeleteTextures(1, &s_geom[i].tex); free(s_geom[i].sv); free(s_geom[i].si); }
    for (int i = 0; i < s_nindex; i++) { glDeleteVertexArrays(1, &s_index[i].vao); glDeleteBuffers(1, &s_index[i].ibo); free(s_index[i].pending); }
    free(s_geom); free(s_index);
    s_geom = NULL; s_index = NULL; s_ngeom = s_cgeom = s_nindex = s_cindex = 0;
}
