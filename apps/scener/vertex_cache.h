#ifndef __SCENER_VERTEX_CACHE_H__
#define __SCENER_VERTEX_CACHE_H__

/* GPU storage as Doom 3's vertex cache: a few large buffers that meshes and shadow volumes are
   sub-allocated from, instead of one GL buffer each. The macOS driver backs every buffer with its own
   memory region, so tens of thousands of small buffers cost gigabytes; ranges in a handful cost what
   the data weighs. Mesh ranges live until the mesh is freed; a chunk whose ranges are all freed is
   reused. Shadow volumes are index lists over the mesh vertices (Doom 3's shadow cache), dropped
   together when the scene's volumes go stale. */

#include "simplegl.h"

void vcache_draw_mesh(Mesh *m);                 /* uploads on first use (or after gpuDirty), then draws */
void vcache_collect(void);                      /* applies the frees queued by mesh_free; call with a GL context */

unsigned vcache_mesh_texture(Mesh *m, int *firstVertex); /* uploads if needed; its chunk's vertices as an R32F buffer texture */
int  vcache_index_alloc(const unsigned *ix, int n, int *chunk, int *first); /* stores indices, 16-bit when they fit */
void vcache_index_draw(int chunk, int first, int n, int baseVertex);
void vcache_index_reset(void);                  /* forgets every index range (shadow volumes went stale) */
void vcache_shutdown(void);
int  vcache_vertex_floats(void);                  /* floats per GPU vertex: the stride the shadow shader reads with */
void vcache_set_attribs(int pos, int nrm, int grad); /* the lit shader's attribute locations */

#endif
