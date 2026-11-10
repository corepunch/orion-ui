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
#define SHADER_INSTANCE_FLOATS 36   /* floats per object in the instance buffer */
void shader_set_ambient(int on, vec3 linear, int skyOn, vec3 sky); /* on: the depth-filling pass (ambient, plus the mirrored sky when skyOn) */
void shader_fill_instance(float *texels, const SceneObj *o);       /* writes SHADER_INSTANCE_FLOATS */
void shader_set_instances(unsigned int bufferTexture);              /* the RGBA32F buffer texture holding the instances */
void shader_draw_instances(Mesh *m, int first, int count);          /* count objects of one model, from instance first */
void shader_set_ramp(const vec3 *shades); /* MATERIAL_RAMP_SHADES sRGB shades, dark to light, or NULL for none */
#ifdef SCENER_USE_TEXTURES
void shader_set_texture(unsigned int tex);
#endif

#endif
