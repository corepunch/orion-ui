#ifndef SHADER_H
#define SHADER_H

#include "simplegl.h"

void shader_init(void);
void shader_deinit(void);
void shader_bind(void);
void shader_unbind(void);
void shader_set_viewproj(mat4 m);
void shader_set_camera_pos(vec3 pos);
void shader_set_light(Light *L);
void shader_set_model(mat4 model, mat4 normalM); /* where the next mesh stands: its matrix and the matrix for its normals */
void shader_set_ambient(int on, vec3 linear); /* on: only colour times this ambient (linear) light, for the depth-filling pass */
void shader_set_sky(int on, vec3 color); /* on: the fragment shader outputs only the sky reflection (additive pass) */
void shader_set_material(vec3 color, float shininess, float specular, float metallic);
void shader_set_gradient(int on, vec3 from, vec3 to); /* sRGB colour multipliers at gradient 0 and 1 */
void shader_set_ramp(const vec3 *shades); /* MATERIAL_RAMP_SHADES sRGB shades, dark to light, or NULL for none */
#ifdef SCENER_USE_TEXTURES
void shader_set_texture(unsigned int tex);
#endif
void shader_draw_mesh(Mesh *m);

#endif
