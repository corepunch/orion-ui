#include "simplegl.h"

#define QUAT_EPSILON 0.000001f
#define QUAT_LERP_LIMIT 0.9995f
#define GAIT_TWO_PI (2.0f*M_PIf)
#define GAIT_MIN_RAMP 0.05f
#define GAIT_PHASE_EPSILON 0.0001f

/* ---------------------------------------------------------- Quaternions -- */

quat quat_identity(void){ quat q={1,0,0,0}; return q; }

quat quat_mul(quat a,quat b){
	quat q={a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z, a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
	        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x, a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
	return q;
}

static quat quat_normalize(quat q){
	float l=sqrtf(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
	if(l<QUAT_EPSILON) return quat_identity();
	quat r={q.w/l,q.x/l,q.y/l,q.z/l}; return r;
}

quat quat_from_mat4(mat4 m){
	float xx=m.m[0],yx=m.m[1],zx=m.m[2],xy=m.m[4],yy=m.m[5],zy=m.m[6],xz=m.m[8],yz=m.m[9],zz=m.m[10];
	float trace=xx+yy+zz; quat q;
	if(trace>0){ float k=sqrtf(trace+1)*2; q.w=0.25f*k; q.x=(zy-yz)/k; q.y=(xz-zx)/k; q.z=(yx-xy)/k; }
	else if(xx>yy && xx>zz){ float k=sqrtf(1+xx-yy-zz)*2; q.w=(zy-yz)/k; q.x=0.25f*k; q.y=(xy+yx)/k; q.z=(xz+zx)/k; }
	else if(yy>zz){ float k=sqrtf(1+yy-xx-zz)*2; q.w=(xz-zx)/k; q.x=(xy+yx)/k; q.y=0.25f*k; q.z=(yz+zy)/k; }
	else { float k=sqrtf(1+zz-xx-yy)*2; q.w=(yx-xy)/k; q.x=(xz+zx)/k; q.y=(yz+zy)/k; q.z=0.25f*k; }
	return quat_normalize(q);
}

mat4 mat4_from_quat(quat q){
	q=quat_normalize(q);
	float x=q.x,y=q.y,z=q.z,w=q.w;
	mat4 m=mat4_identity();
	m.m[0]=1-2*(y*y+z*z); m.m[4]=2*(x*y-w*z);   m.m[8]=2*(x*z+w*y);
	m.m[1]=2*(x*y+w*z);   m.m[5]=1-2*(x*x+z*z); m.m[9]=2*(y*z-w*x);
	m.m[2]=2*(x*z-w*y);   m.m[6]=2*(y*z+w*x);   m.m[10]=1-2*(x*x+y*y);
	return m;
}

quat quat_slerp(quat a,quat b,float t){
	float d=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;
	if(d<0){ d=-d; b.w=-b.w; b.x=-b.x; b.y=-b.y; b.z=-b.z; }
	float ka=1-t,kb=t;
	if(d<QUAT_LERP_LIMIT){ float angle=acosf(d),s=sinf(angle); ka=sinf(ka*angle)/s; kb=sinf(kb*angle)/s; }
	quat q={a.w*ka+b.w*kb,a.x*ka+b.x*kb,a.y*ka+b.y*kb,a.z*ka+b.z*kb};
	return quat_normalize(q);
}

/* Reflection across the body's left-right plane (x -> -x). */
quat quat_mirror_x(quat q){ quat r={q.w,q.x,-q.y,-q.z}; return r; }

float motion_ease(float t){ t=fmaxf(0,fminf(1,t)); return t*t*(3-2*t); }

/* ------------------------------------------------------------------ Gait -- */
/* CATMotion-style walk along the body's forward axis. Everything is a function of
   the cycle phase: the body eases in over the first `ramp` cycles and out over the
   last, steps grow and shrink with its speed, and feet land on fixed plant points,
   so nothing slides. Leg phase offsets are in cycles: 0 steps first, 0.5 half a
   cycle later. Each leg's last step lands on the destination. */

static float gait_ramp(const gait_params_t *p,float total){ return fmaxf(GAIT_MIN_RAMP,fminf(p->ramp,total*0.5f)); }

/* Distance covered by the body, in strides, before velocity normalisation. */
static float gait_progress(const gait_params_t *p,float phase,float total){
	float a=gait_ramp(p,total);
	if(phase<=0) return 0;
	if(phase>=total) return total-a;
	if(phase<a) return phase*phase/(2*a);
	if(phase>total-a){ float left=total-phase; return total-a-left*left/(2*a); }
	return phase-a*0.5f;
}

static float gait_speed_factor(const gait_params_t *p,float phase,float total){
	float a=gait_ramp(p,total);
	return fmaxf(0,fminf(1,fminf(phase,total-phase)/a));
}

float gait_travel(const gait_params_t *p,float phase,float total,float distance){
	float full=gait_progress(p,total,total);
	return full>0?distance*gait_progress(p,phase,total)/full:0;
}

static float gait_landing_phase(float legPhase,int step){ return step+legPhase+0.5f; }

static float gait_plant(const gait_params_t *p,float legPhase,int step,float total,float distance){
	if(step<0) return 0;
	float landing=gait_landing_phase(legPhase,step);
	if(landing>total-1+GAIT_PHASE_EPSILON) return distance;
	return fminf(distance,gait_travel(p,landing,total,distance)+p->lead*p->stride*gait_speed_factor(p,landing,total));
}

/* The walk ends on the first landing of any leg once the body has covered the distance. */
float gait_total_phase(const gait_params_t *p,const float *legPhase,int nlegs,float distance){
	if(p->stride<=0 || distance<=0 || nlegs<=0) return 0;
	float needed=distance/p->stride+p->ramp,total=INFINITY;
	for(int i=0;i<nlegs;i++){
		int step=(int)ceilf(needed-legPhase[i]-0.5f);
		total=fminf(total,gait_landing_phase(legPhase[i],step<0?0:step));
	}
	return total;
}

float gait_phase_at(const gait_params_t *p,float total,float seconds){
	float cadence=p->speed/p->stride;
	if(cadence<=0 || total<=0 || seconds<=0) return 0;
	return fminf(total,cadence*seconds);
}

float gait_duration(const gait_params_t *p,float total){
	float cadence=p->speed/p->stride;
	return cadence>0 && total>0?total/cadence:0;
}

float gait_amplitude(const gait_params_t *p,float phase,float total){ return motion_ease(gait_speed_factor(p,phase,total)); }

void gait_foot(const gait_params_t *p,float legPhase,float phase,float total,float distance,float *forward,float *lift){
	int step=(int)ceilf(phase-legPhase-0.5f);
	if(step<0) step=0;
	float from=gait_plant(p,legPhase,step-1,total,distance),to=gait_plant(p,legPhase,step,total,distance);
	float landing=gait_landing_phase(legPhase,step),s=p->swing>0?(phase-(landing-p->swing))/p->swing:1;
	*forward=from; *lift=0;
	if(landing>total+GAIT_PHASE_EPSILON || s<=0) return;
	if(s>=1){ *forward=to; return; }
	*forward=from+(to-from)*motion_ease(s);
	*lift=p->lift*fminf(1,(to-from)/p->stride*2)*sinf(M_PIf*s);
}

void gait_body(const gait_params_t *p,float phase,float total,gait_body_t *out){
	float a=gait_amplitude(p,phase,total),cycle=GAIT_TWO_PI*phase;
	out->bob=-p->bounce*a*(0.5f+0.5f*cosf(2*cycle));
	out->sway=-p->sway*a*sinf(cycle);
	out->hipTwist=-p->hipTwist*a*sinf(cycle);
	out->spineTwist=(p->hipTwist+p->spineTwist)*a*sinf(cycle);
	out->armSwing=-p->armSwing*a*sinf(cycle-GAIT_TWO_PI*GAIT_ARM_LAG);
}
