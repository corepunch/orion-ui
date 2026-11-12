#include <orion/user/gl_compat.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "vertex_cache.h"
#include "shader.h"
#include <orion/kernel/renderer.h>

#define SRGB_TO_LINEAR_GAMMA 2.2f

static GLuint prog, occlusion_fallback;
static GLuint vlocPos, vlocNrm;
static GLint ulocInst, ulocInstBase, ulocViewProj, ulocViewPos, ulocLightPos, ulocLightColor, ulocLightRadius;
static GLint ulocAmbient, ulocAmbientOn, ulocLightRange, ulocSky, ulocSkyOn, ulocRamp, ulocRampOn;
static GLint vlocGrad, ulocMaps;
static GLint ulocDepthOnly, ulocOcclusionOn, ulocOcclusionProj, ulocOcclusionView, ulocOcclusionViewport;
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
"in vec2 aUV;\n"
"uniform mat4 uViewProj;\n"
"uniform samplerBuffer uInst;\n"
"uniform int uInstBase;\n"
"uniform bool uDepthOnly;\n"
"out float vGrad;\n"
"out vec3 vLocalPos;\n"
"out vec3 vLocalNrm;\n"
"out vec2 vUV;\n"
"flat out ivec2 vMaps;\n"
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
"    int b=(uInstBase+gl_InstanceID)*12;\n"
"    vec4 m0=texelFetch(uInst,b),m1=texelFetch(uInst,b+1),m2=texelFetch(uInst,b+2);\n"
"    vec4 p=vec4(aPos,1.0);\n"
"    vec4 wp=vec4(dot(m0,p),dot(m1,p),dot(m2,p),1.0);\n"
"    gl_Position=uViewProj*wp;\n"
"    if(uDepthOnly) return;\n"
"    vec4 n0=texelFetch(uInst,b+3),n1=texelFetch(uInst,b+4),n2=texelFetch(uInst,b+5);\n"
"    vec4 c=texelFetch(uInst,b+6);\n"
"    vWorldPos=wp.xyz;\n"
"    vWorldNrm=normalize(vec3(dot(n0.xyz,aNrm),dot(n1.xyz,aNrm),dot(n2.xyz,aNrm)));\n"
"    vGrad=aGrad; vLocalPos=aPos; vLocalNrm=aNrm; vUV=aUV;\n"
"    vec4 mp=texelFetch(uInst,b+11); vMaps=ivec2(int(mp.x),int(mp.y));\n"
"    vColor=c.rgb; vUnlit=c.a; vMat=vec3(n0.w,n1.w,n2.w);\n"
"    vGradFrom=texelFetch(uInst,b+7); vGradTo=texelFetch(uInst,b+8).rgb; vGradShape=texelFetch(uInst,b+9).xyz;\n"
"    vec4 rc=texelFetch(uInst,b+10);\n"
"    vGradRadial=vec4((aPos-rc.xyz)*rc.w,rc.w);   /* a distance is not linear, so the fragment takes the length */\n"
"    vec3 n=abs(vWorldNrm);\n"
"    if(n.y>n.x&&n.y>n.z) vWorldUV=wp.xz*0.5;\n"
"    else if(n.x>n.y&&n.x>n.z) vWorldUV=wp.yz*0.5;\n"
"    else vWorldUV=wp.xy*0.5;\n"
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
"   mirror folds it (0 at both ends, 1 in the middle), power curves it, edge steps it. x is repeat + 2 * mirror. */\n"
"float gradient_amount(float g){\n"
"    bool rep=mod(vGradShape.x,2.0)>0.5,mir=vGradShape.x>1.5;\n"
"    float t=rep?fract(g):clamp(g,0.0,1.0);\n"
"    if(mir) t=1.0-abs(2.0*t-1.0);\n"
"    if(vGradShape.z!=1.0) t=pow(t,vGradShape.z);\n"
"    if(vGradShape.y<=0.0) return t;\n"
"    float w=max(fwidth(g)*(mir?2.0:1.0),1e-5);\n"
"    float up=clamp((t-vGradShape.y)/w+0.5,0.0,1.0);\n"
"    return rep&&!mir?up*clamp((1.0-t)/w+0.5,0.0,1.0)+(1.0-clamp(t/w+0.5,0.0,1.0)):up;\n"
"}\n"
"/* <map>: procedural textures, per pixel, from the map table (MAP_TEXELS texels each, see shader_fill_maps). */\n"
"uniform samplerBuffer uMaps;\n"
"flat in ivec2 vMaps;\n"
"in vec3 vLocalPos;\n"
"in vec3 vLocalNrm;\n"
"in vec2 vUV;\n"
"float map_hash(vec3 p){ p=fract(p*0.1031); p+=dot(p,p.zyx+31.32); return fract((p.x+p.y)*p.z); }\n"
"float map_noise(vec3 p){\n"
"    vec3 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);\n"
"    return mix(mix(mix(map_hash(i),map_hash(i+vec3(1,0,0)),f.x),mix(map_hash(i+vec3(0,1,0)),map_hash(i+vec3(1,1,0)),f.x),f.y),\n"
"               mix(mix(map_hash(i+vec3(0,0,1)),map_hash(i+vec3(1,0,1)),f.x),mix(map_hash(i+vec3(0,1,1)),map_hash(i+vec3(1,1,1)),f.x),f.y),f.z);\n"
"}\n"
"float map_fbm(vec3 p,int octaves){\n"
"    float a=0.5,s=0.0,n=0.0;\n"
"    for(int k=0;k<6;k++){ if(k>=octaves) break; s+=a*map_noise(p); n+=a; p=p*2.03+vec3(17.1,5.3,9.7); a*=0.5; }\n"
"    return s/n;\n"
"}\n"
"/* the 2D coordinates a planar gizmo gives, in metres: box picks the face's main axis, as Max's Box mapping */\n"
"vec2 map_plane(int mapping,vec3 p,vec3 n){\n"
"    if(mapping==3) return p.zy; if(mapping==4) return p.xz; if(mapping==5) return p.xy;\n"
"    vec3 a=abs(n); if(a.x>=a.y&&a.x>=a.z) return p.zy; if(a.y>=a.z) return p.xz; return p.xy;\n"
"}\n"
"void apply_maps(inout vec3 col,inout vec3 N){\n"
"    float height=0.0; bool bumped=false;\n"
"    for(int i=0;i<vMaps.y;i++){\n"
"        int b=(vMaps.x+i)*5;\n"
"        vec4 t0=texelFetch(uMaps,b),t1=texelFetch(uMaps,b+1),t2=texelFetch(uMaps,b+2),t3=texelFetch(uMaps,b+3),t4=texelFetch(uMaps,b+4);\n"
"        int type=int(t0.x)%16,mapping=int(t0.x)/16;\n"
"        vec3 p=vLocalPos-t4.xyz;\n"
"        float amount=0.0,h=0.0; vec3 tint=vec3(1.0);\n"
"        if(type==0){   /* noise: fractal value noise in the object's space (or over u, v) */\n"
"            vec3 q=mapping==2?vec3(vUV/t3.xy,0.0):mapping==0?p/t3.xyz:vec3(map_plane(mapping,p,vLocalNrm)/t3.xy,0.0);\n"
"            amount=map_fbm(q+vec3(t3.w*7.13),int(t0.y)); h=amount;\n"
"        } else {\n"
"            vec2 c=mapping==2?vUV/t3.xy:map_plane(mapping,p,vLocalNrm)/t3.xy;\n"
"            if(type==1){   /* checker */\n"
"                vec2 w=max(fwidth(c),vec2(1e-5)),s=clamp((abs(fract(c)-0.5)-0.25)/w+0.5,0.0,1.0);\n"
"                amount=s.x+s.y-2.0*s.x*s.y;\n"
"            } else {       /* bricks: rows along the second axis, each shifted by bond; amount 1 in the mortar */\n"
"                float row=floor(c.y); c.x+=row*t4.w;\n"
"                vec2 f=fract(c),m=0.5*t2.w/t3.xy,w=max(fwidth(c),vec2(1e-5));\n"
"                vec2 inside=clamp((min(f,1.0-f)-m)/w+0.5,0.0,1.0);\n"
"                float brick=inside.x*inside.y; amount=1.0-brick; h=brick;\n"
"                float r=map_hash(vec3(floor(c),t3.w+3.7));\n"
"                tint=vec3(1.0+t0.w*(2.0*r-1.0)*brick);\n"
"            }\n"
"        }\n"
"        amount=pow(clamp(amount,0.0,1.0),t0.z);\n"
"        col*=mix(t1.rgb,t2.rgb,amount)*tint;\n"
"        if(t1.a!=0.0){ height+=t1.a*h; bumped=true; }\n"
"    }\n"
"    if(bumped){   /* bump without tangents, from the height's screen derivatives (Mikkelsen 2010) */\n"
"        vec3 dpdx=dFdx(vWorldPos),dpdy=dFdy(vWorldPos);\n"
"        vec3 r1=cross(dpdy,N),r2=cross(N,dpdx); float det=dot(dpdx,r1);\n"
"        vec3 g=sign(det)*(dFdx(height)*r1+dFdy(height)*r2);\n"
"        if(abs(det)>1e-12) N=normalize(abs(det)*N-g);\n"
"    }\n"
"}\n"
"#define AO_RADIUS 0.35\n"
"#define AO_BIAS 0.006\n"
"#define AO_SAMPLES 24\n"
"#define AO_STRENGTH 4.0\n"
"#define AO_MIN_VISIBILITY 0.25\n"
"uniform bool uDepthOnly;\n"
"uniform bool uOcclusionOn;\n"
"uniform sampler2D uOcclusionDepth;\n"
"uniform mat4 uOcclusionProj;\n"
"uniform mat3 uOcclusionView;\n"
"uniform vec2 uOcclusionViewport;\n"
"vec3 view_position(vec2 uv,float depth){\n"
"    vec3 ndc=vec3(uv,depth)*2.0-1.0;\n"
"    mat4 p=uOcclusionProj;\n"
"    float z=(p[3][2]-ndc.z*p[3][3])/(ndc.z*p[2][3]-p[2][2]);\n"
"    float w=p[2][3]*z+p[3][3];\n"
"    return vec3((ndc.xy*w-p[3].xy)/vec2(p[0][0],p[1][1]),z);\n"
"}\n"
"float ambient_visibility(vec3 normal){\n"
"    vec2 uv=gl_FragCoord.xy/uOcclusionViewport;\n"
"    vec3 center=view_position(uv,gl_FragCoord.z);\n"
"    vec3 n=uOcclusionView*normal;\n"
"    mat4 p=uOcclusionProj;\n"
"    vec2 radius=0.5*AO_RADIUS*vec2(p[0][0],p[1][1])/(p[2][3]*center.z+p[3][3]);\n"
"    vec2 size=vec2(textureSize(uOcclusionDepth,0));\n"
"    // A fixed golden-angle spiral keeps the taps stable during animation.\n"
"    const mat2 turn=mat2(-0.737369,0.675490,-0.675490,-0.737369);\n"
"    vec2 dir=vec2(1.0,0.0);\n"
"    float sum=0.0;\n"
"    for(int i=0;i<AO_SAMPLES;i++,dir=turn*dir){\n"
"        vec2 sampleUV=uv+dir*radius*((float(i)+0.5)/float(AO_SAMPLES));\n"
"        if(any(lessThan(sampleUV,vec2(0.0)))||any(greaterThanEqual(sampleUV,vec2(1.0)))) continue;\n"
"        ivec2 pixel=ivec2(sampleUV*size);\n"
"        float depth=texelFetch(uOcclusionDepth,pixel,0).r;\n"
"        if(depth>=1.0) continue;\n"
"        vec3 delta=view_position((vec2(pixel)+0.5)/size,depth)-center;\n"
"        float dist2=dot(delta,delta);\n"
"        float angle=max(dot(n,delta)-AO_BIAS,0.0)*inversesqrt(max(dist2,AO_BIAS*AO_BIAS));\n"
"        float falloff=max(1.0-dist2/(AO_RADIUS*AO_RADIUS),0.0);\n"
"        sum+=angle*falloff*falloff;\n"
"    }\n"
"    return max(1.0-sum*(AO_STRENGTH/float(AO_SAMPLES)),AO_MIN_VISIBILITY);\n"
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
"    if(uDepthOnly) return;\n"
"    vec3 uColor=vColor; float uShininess=vMat.x,uSpecular=vMat.y,uMetallic=vMat.z;\n"
"    vec3 N=normalize(vWorldNrm);\n"
"    if(vMaps.y>0) apply_maps(uColor,N);\n"
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
"        if(uOcclusionOn && vUnlit<0.5) base*=ambient_visibility(normalize(vWorldNrm));\n"
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
	ulocMaps        = glGetUniformLocation(prog, "uMaps");
	ulocInstBase    = glGetUniformLocation(prog, "uInstBase");
	ulocViewPos     = glGetUniformLocation(prog, "uViewPos");
	ulocLightPos    = glGetUniformLocation(prog, "uLightPos");
	ulocLightColor  = glGetUniformLocation(prog, "uLightColor");
	ulocLightRadius = glGetUniformLocation(prog, "uLightRadius");
	ulocLightRange  = glGetUniformLocation(prog, "uLightRange");
	ulocSky         = glGetUniformLocation(prog, "uSky");
	ulocAmbient     = glGetUniformLocation(prog, "uAmbient");
	ulocAmbientOn   = glGetUniformLocation(prog, "uAmbientOn");
	ulocDepthOnly   = glGetUniformLocation(prog, "uDepthOnly");
	ulocOcclusionOn = glGetUniformLocation(prog, "uOcclusionOn");
	ulocOcclusionProj = glGetUniformLocation(prog, "uOcclusionProj");
	ulocOcclusionView = glGetUniformLocation(prog, "uOcclusionView");
	ulocOcclusionViewport = glGetUniformLocation(prog, "uOcclusionViewport");
	glUseProgram(prog);
	glUniform1i(glGetUniformLocation(prog, "uOcclusionDepth"), 3);
	glUseProgram(0);
	GLint previous_texture;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
	glGenTextures(1, &occlusion_fallback);
	glBindTexture(GL_TEXTURE_2D, occlusion_fallback);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture);
	ulocSkyOn       = glGetUniformLocation(prog, "uSkyOn");
	ulocRamp        = glGetUniformLocation(prog, "uRamp");
	ulocRampOn      = glGetUniformLocation(prog, "uRampOn");
	vlocGrad        = glGetAttribLocation(prog, "aGrad");
	vcache_set_attribs((int)vlocPos,(int)vlocNrm,(int)vlocGrad,(int)glGetAttribLocation(prog, "aUV"));
#ifdef SCENER_USE_TEXTURES
	ulocTex         = glGetUniformLocation(prog, "uTex");
#endif
}

void shader_deinit(void){
	vcache_shutdown();
	if(prog) glDeleteProgram(prog);
	if(occlusion_fallback) glDeleteTextures(1, &occlusion_fallback);
	prog = occlusion_fallback = 0;
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

void shader_set_depth_only(int on){
	glUniform1i(ulocDepthOnly, on);
}

void shader_set_occlusion(unsigned int depth, mat4 proj, mat4 view, int w, int h){
	glUniform1i(ulocOcclusionOn, depth != 0);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, depth ? depth : occlusion_fallback);
	glActiveTexture(GL_TEXTURE0);
	if(!depth) return;
	float rotation[] = {
		view.m[0], view.m[1], view.m[2],
		view.m[4], view.m[5], view.m[6],
		view.m[8], view.m[9], view.m[10],
	};
	glUniformMatrix4fv(ulocOcclusionProj, 1, GL_FALSE, proj.m);
	glUniformMatrix3fv(ulocOcclusionView, 1, GL_FALSE, rotation);
	glUniform2f(ulocOcclusionViewport, (float)w, (float)h);
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
	t[36]=(g->repeat?1.0f:0.0f)+(g->mirror?2.0f:0.0f); t[37]=g->edge; t[38]=g->power>0?g->power:1.0f; t[39]=0;
	float len=vlen(vsub(g->end,g->start)); int radial=g->on && g->type==GRADIENT_RADIAL && len>0;
	t[40]=g->start.x; t[41]=g->start.y; t[42]=g->start.z; t[43]=radial?1.0f/len:0.0f;
	t[44]=(float)o->mapFirst; t[45]=(float)o->mapCount; t[46]=0; t[47]=0;
}

/* A map's MAP_TEXELS texels of the map table: 0 type + 16 * mapping, octaves, power, vary; 1 from (linear) and bump
   (m); 2 to (linear) and mortar (m); 3 size (m, or u v periods) and seed; 4 offset (m) and bond. */
void shader_fill_maps(float *t, const map_t *maps, int count){
	for(int i=0;i<count;i++,t+=MAP_TEXELS*4){
		const map_t *m=&maps[i];
		vec3 a=srgb_to_linear(m->from),b=srgb_to_linear(m->to);
		float v[MAP_TEXELS*4]={(float)(m->type+16*m->mapping),(float)m->octaves,m->power,m->vary,
		                       a.x,a.y,a.z,m->bump, b.x,b.y,b.z,m->mortar,
		                       m->size.x,m->size.y,m->size.z,m->seed, m->offset.x,m->offset.y,m->offset.z,m->bond};
		memcpy(t,v,sizeof(v));
	}
}

/* The map table buffer texture sits on unit 2. */
void shader_set_maps(unsigned int bufferTexture){
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_BUFFER, bufferTexture);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(ulocMaps, 2);
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

