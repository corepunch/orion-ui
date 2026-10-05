#include <orion/user/gl_compat.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "shader.h"
#include <orion/kernel/renderer.h>

#define SRGB_TO_LINEAR_GAMMA 2.2f

static GLuint prog, s_mesh_vao, s_mesh_vbo, s_mesh_ebo;
static GLuint vlocPos, vlocNrm;
static GLint ulocViewProj, ulocViewPos, ulocLightPos, ulocLightColor, ulocLightRadius;
static GLint ulocColor, ulocShininess;
#ifdef SCENER_USE_TEXTURES
static GLint ulocTex;
#endif

static const char *vs_src =
"#version 150\n"
"in vec3 aPos;\n"
"in vec3 aNrm;\n"
"uniform mat4 uViewProj;\n"
"out vec3 vWorldPos;\n"
"out vec3 vWorldNrm;\n"
"out vec2 vWorldUV;\n"
"void main(){\n"
"    vWorldPos=aPos;\n"
"    vWorldNrm=aNrm;\n"
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
"#define BASE_REFLECTANCE 0.04\n"
"#define MIN_ROUGHNESS 0.005\n"
"#define MAX_ROUGHNESS 1.0\n"
"#define MIN_SPECULAR 0.001\n"
"#define LIGHT_ATT_QUAD 2.0\n"
"#define FRESNEL_EXP 5.0\n"
"#define NOISE_SCALE 52.9829189\n"
"#define NOISE_PARAM vec2(0.06711056,0.00583715)\n"
"#define NOISE_SEED vec3(17.0,59.0,113.0)\n"
"#define NOISE_DIV 255.0\n"
"in vec3 vWorldPos;\n"
"in vec3 vWorldNrm;\n"
"in vec2 vWorldUV;\n"
"out vec4 fragColor;\n"
"uniform vec3 uViewPos;\n"
"uniform vec4 uLightPos;\n"
"uniform vec3 uLightColor;\n"
"uniform float uLightRadius;\n"
"uniform vec3 uColor;\n"
"uniform float uShininess;\n"
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
"    float F=BASE_REFLECTANCE+(1.0-BASE_REFLECTANCE)*pow(1.0-max(dot(V,H),0.0),FRESNEL_EXP);\n"
#ifdef SCENER_USE_TEXTURES
"    vec3 texColor=texture(uTex,vWorldUV).rgb;\n"
#else
"    vec3 texColor=vec3(1.0);\n"
#endif
"    vec3 spec=(D*G*F)/(max(4.0*NdotL*NdotV,MIN_SPECULAR))*uLightColor;\n"
"    vec3 diff=uColor*texColor*(1.0-F)*NdotL*uLightColor;\n"
"    float att=1.0;\n"
"    if(uLightRadius>0.0 && uLightPos.w>0.0){\n"
"        float dist=length(uLightPos.xyz-vWorldPos);\n"
"        att=1.0/(1.0+LIGHT_ATT_QUAD*dist/uLightRadius+(dist*dist)/(uLightRadius*uLightRadius));\n"
"    }\n"
"    vec3 lit=(diff+spec)*att;\n"
"    float seed=dot(uLightPos.xyz,NOISE_SEED);\n"
"    float noise=fract(NOISE_SCALE*fract(dot(gl_FragCoord.xy+seed,NOISE_PARAM)))-0.5;\n"
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
	ulocColor       = glGetUniformLocation(prog, "uColor");
	ulocShininess   = glGetUniformLocation(prog, "uShininess");
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
}

void shader_set_material(vec3 color, float shininess){
	color=srgb_to_linear(color);
	glUniform3f(ulocColor, color.x, color.y, color.z);
	glUniform1f(ulocShininess, shininess);
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
	glVertexAttribPointer(vlocPos,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)0);
	glVertexAttribPointer(vlocNrm,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(3*sizeof(float)));
	glDrawElements(GL_TRIANGLES,m->ntris*3,GL_UNSIGNED_INT,(void*)0);
	glDisableVertexAttribArray(vlocPos);
	glDisableVertexAttribArray(vlocNrm);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER,0);
}
