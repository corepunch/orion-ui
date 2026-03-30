#ifndef __UI_KERNEL_CGLM_COMPAT_H__
#define __UI_KERNEL_CGLM_COMPAT_H__

/* Minimal cglm-compatible math definitions for builds without cglm installed.
 * Provides only what kernel/renderer.c uses:
 *   - vec4 / mat4 (column-major float[4][4])
 *   - glm_ortho()
 */

typedef float vec4[4];
typedef vec4  mat4[4];

/* Build an orthographic projection matrix (column-major, OpenGL convention).
 * Equivalent to cglm's glm_ortho(l, r, b, t, n, f, dest). */
static inline void glm_ortho(float l, float r, float b, float t,
                               float n, float f, mat4 dest) {
  float rl = r - l;
  float tb = t - b;
  float fn = f - n;

  /* col 0 */ dest[0][0] = 2.0f/rl;       dest[0][1] = 0.0f;        dest[0][2] = 0.0f;        dest[0][3] = 0.0f;
  /* col 1 */ dest[1][0] = 0.0f;          dest[1][1] = 2.0f/tb;     dest[1][2] = 0.0f;        dest[1][3] = 0.0f;
  /* col 2 */ dest[2][0] = 0.0f;          dest[2][1] = 0.0f;        dest[2][2] = -2.0f/fn;    dest[2][3] = 0.0f;
  /* col 3 */ dest[3][0] = -(r+l)/rl;     dest[3][1] = -(t+b)/tb;   dest[3][2] = -(f+n)/fn;   dest[3][3] = 1.0f;
}

#endif /* __UI_KERNEL_CGLM_COMPAT_H__ */
