#include "simplegl.h"

vec3 v3(float x,float y,float z){ vec3 v={x,y,z}; return v; }
vec3 vadd(vec3 a,vec3 b){ return v3(a.x+b.x,a.y+b.y,a.z+b.z); }
vec3 vsub(vec3 a,vec3 b){ return v3(a.x-b.x,a.y-b.y,a.z-b.z); }
vec3 vscale(vec3 a,float s){ return v3(a.x*s,a.y*s,a.z*s); }
vec3 vmul(vec3 a,vec3 b){ return v3(a.x*b.x,a.y*b.y,a.z*b.z); }
vec3 vcross(vec3 a,vec3 b){
    return v3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
float vdot(vec3 a,vec3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
float vlen(vec3 a){ return sqrtf(vdot(a,a)); }
vec3 vnorm(vec3 a){ float l=vlen(a); return l>1e-8f? vscale(a,1.0f/l): v3(0,0,1); }
vec3 lerp(vec3 a, vec3 b, float t){ return v3(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t); }

int ray_intersect_aabb(vec3 origin, vec3 dir, vec3 bbMin, vec3 bbMax, float *tOut){
	float tmin=-1e30f, tmax=1e30f;
	float *minV=(float*)&bbMin, *maxV=(float*)&bbMax;
	float *o=(float*)&origin, *d=(float*)&dir;
	for(int i=0;i<3;i++){
		if(fabsf(d[i])<1e-8f){
			if(o[i]<minV[i] || o[i]>maxV[i]) return 0;
			continue;
		}
		float t1=(minV[i]-o[i])/d[i], t2=(maxV[i]-o[i])/d[i];
		if(t1>t2){ float tmp=t1; t1=t2; t2=tmp; }
		if(t1>tmin) tmin=t1;
		if(t2<tmax) tmax=t2;
		if(tmin>tmax) return 0;
	}
	if(tmax<0) return 0;
	if(tmin<0) tmin=0;
	if(tOut) *tOut=tmin;
	return 1;
}

mat4 mat4_identity(void){
    mat4 r={{0}}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f; return r;
}
mat4 mat4_mul(mat4 a, mat4 b){
    mat4 r={{0}};
    for(int c=0;c<4;c++) for(int row=0;row<4;row++){
        float s=0;
        for(int k=0;k<4;k++) s += a.m[k*4+row]*b.m[c*4+k];
        r.m[c*4+row]=s;
    }
    return r;
}
mat4 mat4_translate(vec3 t){
    mat4 r=mat4_identity(); r.m[12]=t.x; r.m[13]=t.y; r.m[14]=t.z; return r;
}
mat4 mat4_scale(vec3 s){
    mat4 r=mat4_identity(); r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; return r;
}
mat4 mat4_rot_x(float deg){
    float a=deg*M_PIf/180.0f, c=cosf(a), s=sinf(a);
    mat4 r=mat4_identity(); r.m[5]=c; r.m[6]=s; r.m[9]=-s; r.m[10]=c; return r;
}
mat4 mat4_rot_y(float deg){
    float a=deg*M_PIf/180.0f, c=cosf(a), s=sinf(a);
    mat4 r=mat4_identity(); r.m[0]=c; r.m[2]=-s; r.m[8]=s; r.m[10]=c; return r;
}
mat4 mat4_rot_z(float deg){
    float a=deg*M_PIf/180.0f, c=cosf(a), s=sinf(a);
    mat4 r=mat4_identity(); r.m[0]=c; r.m[1]=s; r.m[4]=-s; r.m[5]=c; return r;
}
mat4 mat4_rot_xyz(vec3 rdeg){
    return mat4_mul(mat4_rot_z(rdeg.z), mat4_mul(mat4_rot_y(rdeg.y), mat4_rot_x(rdeg.x)));
}
mat4 mat4_affine_inverse(mat4 m){
	float a=m.m[0],b=m.m[4],c=m.m[8],d=m.m[1],e=m.m[5],f=m.m[9],g=m.m[2],h=m.m[6],i=m.m[10];
	float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
	if(fabsf(det)<1e-10f) return mat4_identity();
	float q=1.0f/det;
	mat4 r=mat4_identity();
	r.m[0]=(e*i-f*h)*q; r.m[4]=(c*h-b*i)*q; r.m[8]=(b*f-c*e)*q;
	r.m[1]=(f*g-d*i)*q; r.m[5]=(a*i-c*g)*q; r.m[9]=(c*d-a*f)*q;
	r.m[2]=(d*h-e*g)*q; r.m[6]=(b*g-a*h)*q; r.m[10]=(a*e-b*d)*q;
	vec3 t=v3(m.m[12],m.m[13],m.m[14]);
	vec3 it=vscale(mat4_xform_dir(r,t),-1.0f);
	r.m[12]=it.x; r.m[13]=it.y; r.m[14]=it.z;
	return r;
}
vec3 mat4_xform_point(mat4 m, vec3 p){
    return v3( m.m[0]*p.x+m.m[4]*p.y+m.m[8]*p.z+m.m[12],
               m.m[1]*p.x+m.m[5]*p.y+m.m[9]*p.z+m.m[13],
               m.m[2]*p.x+m.m[6]*p.y+m.m[10]*p.z+m.m[14]);
}
vec3 mat4_xform_dir(mat4 m, vec3 p){
    return v3( m.m[0]*p.x+m.m[4]*p.y+m.m[8]*p.z,
               m.m[1]*p.x+m.m[5]*p.y+m.m[9]*p.z,
               m.m[2]*p.x+m.m[6]*p.y+m.m[10]*p.z);
}
vec3 mat4_xform_normal(mat4 m,vec3 p){
	mat4 inv=mat4_affine_inverse(m);
	return vnorm(v3(inv.m[0]*p.x+inv.m[1]*p.y+inv.m[2]*p.z,
		inv.m[4]*p.x+inv.m[5]*p.y+inv.m[6]*p.z,
		inv.m[8]*p.x+inv.m[9]*p.y+inv.m[10]*p.z));
}
mat4 mat4_perspective(float fovy_deg,float aspect,float zn,float zf){
    float f=1.0f/tanf(fovy_deg*M_PIf/360.0f);
    mat4 r={{0}};
    r.m[0]=f/aspect; r.m[5]=f;
    r.m[10]=(zf+zn)/(zn-zf); r.m[11]=-1.0f;
    r.m[14]=(2*zf*zn)/(zn-zf);
    return r;
}
/* The active camera's projection: orthographic when it has a height (camOrtho, metres), else perspective. */
mat4 scene_camera_projection(float fov,float orthoHeight,float aspect,float zn,float zf){
    if(orthoHeight>0){ float h=orthoHeight*0.5f,w=h*aspect; return mat4_ortho(-w,w,-h,h,zn,zf); }
    return mat4_perspective(fov,aspect,zn,zf);
}
mat4 mat4_ortho(float l,float r_,float b,float t,float zn,float zf){
    mat4 m={{0}};
    m.m[0]=2.0f/(r_-l); m.m[5]=2.0f/(t-b); m.m[10]=-2.0f/(zf-zn);
    m.m[12]=-(r_+l)/(r_-l); m.m[13]=-(t+b)/(t-b); m.m[14]=-(zf+zn)/(zf-zn);
    m.m[15]=1.0f;
    return m;
}
mat4 mat4_lookat(vec3 eye, vec3 center, vec3 up){
    vec3 f=vnorm(vsub(center,eye));
    vec3 s=vnorm(vcross(f,up));
    vec3 u=vcross(s,f);
    mat4 r=mat4_identity();
    r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;
    r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;
    r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
    r.m[12]=-vdot(s,eye); r.m[13]=-vdot(u,eye); r.m[14]=vdot(f,eye);
    return r;
}
