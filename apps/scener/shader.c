#include <orion/user/gl_compat.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "vertex_cache.h"
#include "shader.h"
#include <orion/kernel/renderer.h>

#define SRGB_TO_LINEAR_GAMMA 2.2f

static GLuint prog;
static GLuint vlocPos, vlocNrm;
static GLint ulocInst, ulocInstBase, ulocViewProj, ulocViewPos, ulocLightPos, ulocLightColor, ulocLightRadius;
static GLint ulocAmbient, ulocAmbientOn, ulocLightRange, ulocSky, ulocSkyOn, ulocRamp, ulocRampOn;
static GLint vlocGrad;
#ifdef SCENER_USE_TEXTURES
static GLint ulocTex;
#endif

/* Objects are drawn in instanced batches of one model. What differs per object (matrix, normal matrix,
   colour, material, gradient) comes from a buffer texture, INSTANCE_TEXELS RGBA32F texels per instance:
   0-2 model rows; 3-5 normal-matrix rows with shininess, specular, metallic in w; 6 colour (linear) and unlit;
   7 gradient from (linear) and on; 8 gradient to; 9 gradient repeat, edge and power (gradient_t);
   10 a radial gradient's centre (local) and 1/radius, 0 for any other gizmo. */
static const char *vs_src =
"#version 150\n"
"in vec3 aPos;\n"
"in vec3 aNrm;\n"
"in float aGrad;\n"
"uniform mat4 uViewProj;\n"
"uniform samplerBuffer uInst;\n"
"uniform int uInstBase;\n"
"out float vGrad;\n"
"out vec3 vWorldPos;\n"
"out vec3 vWorldNrm;\n"
"out vec2 vWorldUV;\n"
"flat out vec3 vColor;\n"
"flat out vec3 vMat;\n"
"flat out float vUnlit;\n"
"flat out vec4 vGradFrom;\n"
"flat out vec3 vGradTo;\n"
"flat out vec3 vGradShape;\n"
"out vec4 vGradRadial;\n"
"void main(){\n"
"    int b=(uInstBase+gl_InstanceID)*11;\n"
"    vec4 m0=texelFetch(uInst,b),m1=texelFetch(uInst,b+1),m2=texelFetch(uInst,b+2);\n"
"    vec4 n0=texelFetch(uInst,b+3),n1=texelFetch(uInst,b+4),n2=texelFetch(uInst,b+5);\n"
"    vec4 c=texelFetch(uInst,b+6);\n"
"    vec4 p=vec4(aPos,1.0);\n"
"    vec4 wp=vec4(dot(m0,p),dot(m1,p),dot(m2,p),1.0);\n"
"    vWorldPos=wp.xyz;\n"
"    vWorldNrm=normalize(vec3(dot(n0.xyz,aNrm),dot(n1.xyz,aNrm),dot(n2.xyz,aNrm)));\n"
"    vGrad=aGrad;\n"
"    vColor=c.rgb; vUnlit=c.a; vMat=vec3(n0.w,n1.w,n2.w);\n"
"    vGradFrom=texelFetch(uInst,b+7); vGradTo=texelFetch(uInst,b+8).rgb; vGradShape=texelFetch(uInst,b+9).xyz;\n"
"    vec4 rc=texelFetch(uInst,b+10);\n"
"    vGradRadial=vec4((aPos-rc.xyz)*rc.w,rc.w);   /* a distance is not linear, so the fragment takes the length */\n"
"    vec3 n=abs(vWorldNrm);\n"
"    if(n.y>n.x&&n.y>n.z) vWorldUV=wp.xz*0.5;\n"
"    else if(n.x>n.y&&n.x>n.z) vWorldUV=wp.yz*0.5;\n"
"    else vWorldUV=wp.xy*0.5;\n"
"    gl_Position=uViewProj*wp;\n"
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
"flat in vec3 vColor;\n"
"flat in vec3 vMat;\n"
"flat in float vUnlit;\n"
"flat in vec4 vGradFrom;\n"
"flat in vec3 vGradTo;\n"
"flat in vec3 vGradShape;\n"
"in vec4 vGradRadial;\n"
"/* gradient_amount() in mesh.c, with the edges filtered over a pixel: repeat wraps the gizmo coordinate,\n"
"   power curves it, edge steps it */\n"
"float gradient_amount(float g){\n"
"    float t=vGradShape.x>0.5?fract(g):clamp(g,0.0,1.0);\n"
"    if(vGradShape.z!=1.0) t=pow(t,vGradShape.z);\n"
"    if(vGradShape.y<=0.0) return t;\n"
"    float w=max(fwidth(g),1e-5);\n"
"    float up=clamp((t-vGradShape.y)/w+0.5,0.0,1.0);\n"
"    return vGradShape.x>0.5?up*clamp((1.0-t)/w+0.5,0.0,1.0)+(1.0-clamp(t/w+0.5,0.0,1.0)):up;\n"
"}\n"
"uniform vec3 uSky;\n"
"uniform vec3 uAmbient;\n"
"uniform int uAmbientOn;\n"
"uniform int uSkyOn;\n"
"uniform vec3 uRamp[RAMP_SHADES];\n"
"uniform int uRampOn;\n"
#ifdef SCENER_USE_TEXTURES
"uniform sampler2D uTex;\n"
#endif
"void main(){\n"
"    vec3 uColor=vColor; float uShininess=vMat.x,uSpecular=vMat.y,uMetallic=vMat.z;\n"
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
#ifdef SCENER_USE_TEXTURES
"    vec3 texColor=texture(uTex,vWorldUV).rgb;\n"
#else
"    vec3 texColor=vec3(1.0);\n"
#endif
"    if(vGradFrom.a>0.5) texColor*=mix(vGradFrom.rgb,vGradTo,gradient_amount(vGradRadial.w>0.0?length(vGradRadial.xyz)+vGrad:vGrad));\n"
"    if(uAmbientOn>0){\n"
"        /* The depth-filling pass: ambient light (an unlit object shows its own colour), plus the sky in glossy\n"
"           surfaces: the reflection direction picks sky above, ground below; Fresnel is damped by roughness. */\n"
"        vec3 base=clamp(uColor*texColor*mix(uAmbient,vec3(1.0),vUnlit),0.0,1.0);\n"
"        if(uSkyOn>0 && vUnlit<0.5){\n"
"            vec3 Rv=reflect(-V,N);\n"
"            float up=clamp(Rv.y*0.5+0.5,0.0,1.0);\n"
"            vec3 Fr=F0+(max(vec3(1.0-roughness),F0)-F0)*pow(1.0-NdotV,FRESNEL_EXP);\n"
"            base+=clamp(uSky*Fr*up*up*(1.0-0.5*roughness),0.0,1.0);\n"
"        }\n"
"        fragColor=vec4(base,1.0);\n"
"        return;\n"
"    }\n"
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
	ulocInst        = glGetUniformLocation(prog, "uInst");
	ulocInstBase    = glGetUniformLocation(prog, "uInstBase");
	ulocViewPos     = glGetUniformLocation(prog, "uViewPos");
	ulocLightPos    = glGetUniformLocation(prog, "uLightPos");
	ulocLightColor  = glGetUniformLocation(prog, "uLightColor");
	ulocLightRadius = glGetUniformLocation(prog, "uLightRadius");
	ulocLightRange  = glGetUniformLocation(prog, "uLightRange");
	ulocSky         = glGetUniformLocation(prog, "uSky");
	ulocAmbient     = glGetUniformLocation(prog, "uAmbient");
	ulocAmbientOn   = glGetUniformLocation(prog, "uAmbientOn");
	ulocSkyOn       = glGetUniformLocation(prog, "uSkyOn");
	ulocRamp        = glGetUniformLocation(prog, "uRamp");
	ulocRampOn      = glGetUniformLocation(prog, "uRampOn");
	vlocGrad        = glGetAttribLocation(prog, "aGrad");
	vcache_set_attribs((int)vlocPos,(int)vlocNrm,(int)vlocGrad);
#ifdef SCENER_USE_TEXTURES
	ulocTex         = glGetUniformLocation(prog, "uTex");
#endif
}

void shader_deinit(void){
	vcache_shutdown();
	if(prog) glDeleteProgram(prog);
	prog = 0;
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

/* The depth-filling pass: ambient light (linear) and, when sky is on, the sky colour (sRGB) glossy surfaces mirror. */
void shader_set_ambient(int on, vec3 linear, int skyOn, vec3 sky){
	glUniform1i(ulocAmbientOn, on ? 1 : 0);
	glUniform1i(ulocSkyOn, on && skyOn ? 1 : 0);
	if(!on) return;
	glUniform3f(ulocAmbient, linear.x, linear.y, linear.z);
	sky=srgb_to_linear(sky);
	glUniform3f(ulocSky, sky.x, sky.y, sky.z);
}

/* One object's texels of the instance buffer (see the vertex shader). */
void shader_fill_instance(float *t, const SceneObj *o){
	const float *m=o->model.m,*n=o->normalM.m;
	float mat[3]={o->shininess,o->specular,o->metallic};
	for(int r=0;r<3;r++){
		t[r*4]=m[r]; t[r*4+1]=m[4+r]; t[r*4+2]=m[8+r]; t[r*4+3]=m[12+r];
		t[12+r*4]=n[r]; t[12+r*4+1]=n[4+r]; t[12+r*4+2]=n[8+r]; t[12+r*4+3]=mat[r];
	}
	const gradient_t *g=&o->gradient;
	vec3 c=srgb_to_linear(o->color),a=srgb_to_linear(g->from),b=srgb_to_linear(g->to);
	t[24]=c.x; t[25]=c.y; t[26]=c.z; t[27]=o->unlit?1.0f:0.0f;
	t[28]=a.x; t[29]=a.y; t[30]=a.z; t[31]=g->on?1.0f:0.0f;
	t[32]=b.x; t[33]=b.y; t[34]=b.z; t[35]=0;
	t[36]=g->repeat?1.0f:0.0f; t[37]=g->edge; t[38]=g->power>0?g->power:1.0f; t[39]=0;
	float len=vlen(vsub(g->end,g->start)); int radial=g->on && g->type==GRADIENT_RADIAL && len>0;
	t[40]=g->start.x; t[41]=g->start.y; t[42]=g->start.z; t[43]=radial?1.0f/len:0.0f;
}

/* The instance buffer texture sits on unit 1 (unit 0 is the material texture). */
void shader_set_instances(unsigned int bufferTexture){
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_BUFFER, bufferTexture);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(ulocInst, 1);
}

void shader_draw_instances(Mesh *m, int first, int count){
	glUniform1i(ulocInstBase, first);
	vcache_draw_mesh(m, count);
}

void shader_set_ramp(const vec3 *shades){
	if(!shades){ glUniform1i(ulocRampOn, 0); return; }
	float linear[MATERIAL_RAMP_SHADES*3];
	for(int i=0;i<MATERIAL_RAMP_SHADES;i++){ vec3 c=srgb_to_linear(shades[i]); linear[i*3]=c.x; linear[i*3+1]=c.y; linear[i*3+2]=c.z; }
	glUniform3fv(ulocRamp, MATERIAL_RAMP_SHADES, linear);
	glUniform1i(ulocRampOn, 1);
}

#ifdef SCENER_USE_TEXTURES
void shader_set_texture(unsigned int tex){
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(ulocTex, 0);
}
#endif

