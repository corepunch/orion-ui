#include <orion/user/gl_compat.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "shader.h"
#include <orion/kernel/renderer.h>

#define SRGB_TO_LINEAR_GAMMA 2.2f

static GLuint prog, s_mesh_vao, s_mesh_vbo, s_mesh_ebo;
static GLuint vlocPos, vlocNrm;
static GLint ulocViewProj, ulocViewPos, ulocLightPos, ulocLightColor, ulocLightRadius;
static GLint ulocLightRange, ulocSky, ulocSkyOn, ulocSpecular, ulocMetallic, ulocColor, ulocShininess, ulocRamp, ulocRampOn, ulocGradOn, ulocGradFrom, ulocGradTo;
static GLint vlocGrad;
#ifdef SCENER_USE_TEXTURES
static GLint ulocTex;
#endif

static const char *vs_src =
"#version 150\n"
"in vec3 aPos;\n"
"in vec3 aNrm;\n"
"in float aGrad;\n"
"uniform mat4 uViewProj;\n"
"out float vGrad;\n"
"out vec3 vWorldPos;\n"
"out vec3 vWorldNrm;\n"
"out vec2 vWorldUV;\n"
"void main(){\n"
"    vWorldPos=aPos;\n"
"    vWorldNrm=aNrm;\n"
"    vGrad=aGrad;\n"
"    vec3 n=abs(aNrm);\n"
"    if(n.y>n.x&&n.y>n.z) vWorldUV=aPos.xz*0.5;\n"
"    else if(n.x>n.y&&n.x>n.z) vWorldUV=aPos.yz*0.5;\n"
"    else vWorldUV=aPos.xy*0.5;\n"
"    gl_Position=uViewProj*vec4(aPos,1.0);\n"
"}\n";

static const char *fs_src =
"#version 150\n"
"#define PI 3.14159\n"
"#define SHININESS_SCALE 18.0\n"
"#define SPECULAR_TO_F0 0.08\n"
"#define MIN_ROUGHNESS 0.005\n"
"#define MAX_ROUGHNESS 1.0\n"
"#define MIN_SPECULAR 0.001\n"
"#define LIGHT_ATT_QUAD 2.0\n"
"#define FRESNEL_EXP 5.0\n"
"#define NOISE_SCALE 52.9829189\n"
"#define NOISE_PARAM vec2(0.06711056,0.00583715)\n"
"#define NOISE_SEED vec3(17.0,59.0,113.0)\n"
"#define NOISE_DIV 255.0\n"
"#define RAMP_SHADES 16\n"
"#define RAMP_DITHER 0.5\n"
"#define LUMA vec3(0.2126,0.7152,0.0722)\n"
"in vec3 vWorldPos;\n"
"in vec3 vWorldNrm;\n"
"in vec2 vWorldUV;\n"
"in float vGrad;\n"
"out vec4 fragColor;\n"
"uniform vec3 uViewPos;\n"
"uniform vec4 uLightPos;\n"
"uniform vec3 uLightColor;\n"
"uniform float uLightRadius;\n"
"uniform float uLightRange;\n"
"uniform vec3 uColor;\n"
"uniform float uShininess;\n"
"uniform float uSpecular;\n"
"uniform float uMetallic;\n"
"uniform vec3 uSky;\n"
"uniform int uSkyOn;\n"
"uniform vec3 uRamp[RAMP_SHADES];\n"
"uniform int uRampOn;\n"
"uniform int uGradOn;\n"
"uniform vec3 uGradFrom;\n"
"uniform vec3 uGradTo;\n"
#ifdef SCENER_USE_TEXTURES
"uniform sampler2D uTex;\n"
#endif
"void main(){\n"
"    vec3 N=normalize(vWorldNrm);\n"
"    vec3 V=normalize(uViewPos-vWorldPos);\n"
"    vec3 L=normalize(uLightPos.xyz-vWorldPos*uLightPos.w);\n"
"    vec3 H=normalize(V+L);\n"
"    float NdotL=max(dot(N,L),0.0);\n"
"    float NdotH=max(dot(N,H),0.0);\n"
"    float NdotV=max(dot(N,V),0.0);\n"
"    float roughness=clamp(exp(-uShininess/SHININESS_SCALE),MIN_ROUGHNESS,MAX_ROUGHNESS);\n"
"    float alpha=roughness*roughness;\n"
"    float a2=alpha*alpha;\n"
"    float denom=NdotH*NdotH*(a2-1.0)+1.0;\n"
"    float D=a2/(PI*denom*denom);\n"
"    float k=alpha/2.0;\n"
"    float G1=NdotL/(NdotL*(1.0-k)+k);\n"
"    float G2=NdotV/(NdotV*(1.0-k)+k);\n"
"    float G=G1*G2;\n"
"    vec3 albedo=uColor;\n"
"    vec3 F0=mix(vec3(SPECULAR_TO_F0*uSpecular),albedo,uMetallic);\n"
"    vec3 F=F0+(vec3(1.0)-F0)*pow(1.0-max(dot(V,H),0.0),FRESNEL_EXP);\n"
"    if(uSkyOn>0){\n"
"        /* The sky in glossy surfaces: reflection direction picks sky above, ground below; Fresnel is damped by roughness. */\n"
"        vec3 Rv=reflect(-V,N);\n"
"        float up=clamp(Rv.y*0.5+0.5,0.0,1.0);\n"
"        vec3 Fr=F0+(max(vec3(1.0-roughness),F0)-F0)*pow(1.0-NdotV,FRESNEL_EXP);\n"
"        fragColor=vec4(clamp(uSky*Fr*up*up*(1.0-0.5*roughness),0.0,1.0),1.0);\n"
"        return;\n"
"    }\n"
#ifdef SCENER_USE_TEXTURES
"    vec3 texColor=texture(uTex,vWorldUV).rgb;\n"
#else
"    vec3 texColor=vec3(1.0);\n"
#endif
"    if(uGradOn>0) texColor*=mix(uGradFrom,uGradTo,clamp(vGrad,0.0,1.0));\n"
"    /* The diffuse term drops its 1/pi, so a light colour is irradiance/pi; the specular lobe times NdotL needs the pi back. */\n"
"    vec3 spec=(D*G*F)*PI/(max(4.0*NdotV,MIN_SPECULAR))*uLightColor;\n"
"    vec3 diff=uColor*texColor*(vec3(1.0)-F)*(1.0-uMetallic)*NdotL*uLightColor;\n"
"    float att=1.0;\n"
"    if(uLightRadius>0.0 && uLightPos.w>0.0){\n"
"        float dist=length(uLightPos.xyz-vWorldPos);\n"
"        att=1.0/(1.0+LIGHT_ATT_QUAD*dist/uLightRadius+(dist*dist)/(uLightRadius*uLightRadius));\n"
"    }\n"
"    if(uLightRange>0.0 && uLightPos.w>0.0){ /* a smooth window ends the light at its range */\n"
"        float q=length(uLightPos.xyz-vWorldPos)/uLightRange;\n"
"        float w=clamp(1.0-q*q*q*q,0.0,1.0);\n"
"        att*=w*w;\n"
"    }\n"
"    float seed=dot(uLightPos.xyz,NOISE_SEED);\n"
"    float noise=fract(NOISE_SCALE*fract(dot(gl_FragCoord.xy+seed,NOISE_PARAM)))-0.5;\n"
"    vec3 lit;\n"
"    if(uRampOn>0){\n"
"        /* A ramp material is a palette run: the light's brightness picks the shade, its hue only tints. */\n"
"        float luma=dot(uLightColor,LUMA);\n"
"        float shade=clamp(NdotL*att*luma,0.0,1.0)*float(RAMP_SHADES-1);\n"
"        int step=int(clamp(floor(shade+0.5+noise*RAMP_DITHER),0.0,float(RAMP_SHADES-1)));\n"
"        lit=uRamp[step]*texColor*(luma>0.0?uLightColor/luma:vec3(1.0));\n"
"    } else lit=(diff+spec)*att;\n"
"    vec3 encoded=clamp(lit + noise/NOISE_DIV,0.0,1.0);\n"
"    fragColor=vec4(encoded,1.0);\n"
"}\n";

static vec3 srgb_to_linear(vec3 color){
	return v3(powf(color.x,2.2f),powf(color.y,2.2f),powf(color.z,2.2f));
}

void shader_init(void){
	if (!ui_load_program_from_source(vs_src, fs_src, "aPos", "aNrm", NULL, &prog)) return;

	vlocPos = 0;
	vlocNrm = 1;
	ulocViewProj    = glGetUniformLocation(prog, "uViewProj");
	ulocViewPos     = glGetUniformLocation(prog, "uViewPos");
	ulocLightPos    = glGetUniformLocation(prog, "uLightPos");
	ulocLightColor  = glGetUniformLocation(prog, "uLightColor");
	ulocLightRadius = glGetUniformLocation(prog, "uLightRadius");
	ulocLightRange  = glGetUniformLocation(prog, "uLightRange");
	ulocColor       = glGetUniformLocation(prog, "uColor");
	ulocShininess   = glGetUniformLocation(prog, "uShininess");
	ulocSpecular    = glGetUniformLocation(prog, "uSpecular");
	ulocSky         = glGetUniformLocation(prog, "uSky");
	ulocSkyOn       = glGetUniformLocation(prog, "uSkyOn");
	ulocMetallic    = glGetUniformLocation(prog, "uMetallic");
	ulocRamp        = glGetUniformLocation(prog, "uRamp");
	ulocRampOn      = glGetUniformLocation(prog, "uRampOn");
	ulocGradOn      = glGetUniformLocation(prog, "uGradOn");
	ulocGradFrom    = glGetUniformLocation(prog, "uGradFrom");
	ulocGradTo      = glGetUniformLocation(prog, "uGradTo");
	vlocGrad        = glGetAttribLocation(prog, "aGrad");
#ifdef SCENER_USE_TEXTURES
	ulocTex         = glGetUniformLocation(prog, "uTex");
#endif
}

void shader_deinit(void){
	if(prog) glDeleteProgram(prog);
	if(s_mesh_vao) glDeleteVertexArrays(1,&s_mesh_vao);
	if(s_mesh_vbo) glDeleteBuffers(1,&s_mesh_vbo);
	if(s_mesh_ebo) glDeleteBuffers(1,&s_mesh_ebo);
	prog = 0;
	s_mesh_vao=s_mesh_vbo=s_mesh_ebo=0;
}

void shader_bind(void){
	glUseProgram(prog);
}

void shader_unbind(void){
	glUseProgram(0);
}

void shader_set_viewproj(mat4 m){
	glUniformMatrix4fv(ulocViewProj, 1, GL_FALSE, m.m);
}

void shader_set_camera_pos(vec3 pos){
	glUniform3f(ulocViewPos, pos.x, pos.y, pos.z);
}

void shader_set_light(Light *L){
	vec3 color=srgb_to_linear(L->color);
	if(L->isDirectional)
		glUniform4f(ulocLightPos, -L->dir.x, -L->dir.y, -L->dir.z, 0.0f);
	else
		glUniform4f(ulocLightPos, L->pos.x, L->pos.y, L->pos.z, 1.0f);
	glUniform3f(ulocLightColor, color.x*L->intensity, color.y*L->intensity, color.z*L->intensity);
	glUniform1f(ulocLightRadius, L->isDirectional ? 0.0f : L->radius);
	glUniform1f(ulocLightRange, L->isDirectional ? 0.0f : L->range);
}

void shader_set_material(vec3 color, float shininess, float specular, float metallic){
	color=srgb_to_linear(color);
	glUniform3f(ulocColor, color.x, color.y, color.z);
	glUniform1f(ulocShininess, shininess);
	glUniform1f(ulocSpecular, specular);
	glUniform1f(ulocMetallic, metallic);
}

void shader_set_sky(int on, vec3 color){
	glUniform1i(ulocSkyOn, on ? 1 : 0);
	if(on){ color=srgb_to_linear(color); glUniform3f(ulocSky, color.x, color.y, color.z); }
}

void shader_set_ramp(const vec3 *shades){
	if(!shades){ glUniform1i(ulocRampOn, 0); return; }
	float linear[MATERIAL_RAMP_SHADES*3];
	for(int i=0;i<MATERIAL_RAMP_SHADES;i++){ vec3 c=srgb_to_linear(shades[i]); linear[i*3]=c.x; linear[i*3+1]=c.y; linear[i*3+2]=c.z; }
	glUniform3fv(ulocRamp, MATERIAL_RAMP_SHADES, linear);
	glUniform1i(ulocRampOn, 1);
}

void shader_set_gradient(int on, vec3 from, vec3 to){
	glUniform1i(ulocGradOn, on ? 1 : 0);
	if(!on) return;
	vec3 a=srgb_to_linear(from), b=srgb_to_linear(to);
	glUniform3f(ulocGradFrom, a.x, a.y, a.z);
	glUniform3f(ulocGradTo, b.x, b.y, b.z);
}

#ifdef SCENER_USE_TEXTURES
void shader_set_texture(unsigned int tex){
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(ulocTex, 0);
}
#endif

void shader_draw_mesh(Mesh *m){
	if(!s_mesh_vao){
		glGenVertexArrays(1,&s_mesh_vao);
		glGenBuffers(1,&s_mesh_vbo);
		glGenBuffers(1,&s_mesh_ebo);
	}
	glBindVertexArray(s_mesh_vao);
	glBindBuffer(GL_ARRAY_BUFFER,s_mesh_vbo);
	glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(m->nverts*sizeof(Vertex)),m->verts,GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,s_mesh_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(m->ntris*sizeof(Tri)),m->tris,GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(vlocPos);
	glEnableVertexAttribArray(vlocNrm);
	if(vlocGrad>=0) glEnableVertexAttribArray(vlocGrad);
	glVertexAttribPointer(vlocPos,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)0);
	glVertexAttribPointer(vlocNrm,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(3*sizeof(float)));
	if(vlocGrad>=0) glVertexAttribPointer(vlocGrad,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,g));
	glDrawElements(GL_TRIANGLES,m->ntris*3,GL_UNSIGNED_INT,(void*)0);
	glDisableVertexAttribArray(vlocPos);
	glDisableVertexAttribArray(vlocNrm);
	if(vlocGrad>=0) glDisableVertexAttribArray(vlocGrad);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER,0);
}
