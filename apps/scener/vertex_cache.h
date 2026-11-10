#ifndef __SCENER_VERTEX_CACHE_H__
#define __SCENER_VERTEX_CACHE_H__

/* GPU storage as Doom 3's vertex cache: a few large buffers that meshes and shadow volumes are
   sub-allocated from, instead of one GL buffer each. The macOS driver backs every buffer with its own
   memory region, so tens of thousands of small buffers cost gigabytes; ranges in a handful cost what
   the data weighs. Mesh ranges live until the mesh is freed; a chunk whose ranges are all freed is
   reused. A model also gets one shadow mesh, the same for every light: the shadow shader decides per
   triangle whether it faces the light, so shadows need no per-light data and draw instanced. */

#include "simplegl.h"

void vcache_draw_mesh(Mesh *m, int instances);  /* draws that many instances of a prepared, flushed mesh */
void vcache_prepare_mesh(Mesh *m);              /* stages the mesh for the next flush if the GPU does not have it */
void vcache_collect(void);                      /* applies the frees queued by mesh_free; call with a GL context */

unsigned vcache_mesh_texture(Mesh *m, int *firstVertex); /* its chunk's vertices as an R32F buffer texture */
void vcache_prepare_shadow(Mesh *m);            /* builds the model's shadow mesh if it has none (staged until flush) */
unsigned vcache_shadow_planes(Mesh *m, int *firstPlane); /* its triangles' planes as an RGBA32F buffer texture */
void vcache_draw_shadow(Mesh *m, int caps, int instances); /* edge quads, plus every triangle with caps (z-fail) */
void vcache_flush(void);                        /* sends everything staged; call once after the front end, before drawing */
void vcache_shutdown(void);
int  vcache_vertex_floats(void);                  /* floats per GPU vertex: the stride the shadow shader reads with */
void vcache_set_attribs(int pos, int nrm, int grad); /* the lit shader's attribute locations */

#endif
