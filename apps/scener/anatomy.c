#include "simplegl.h"
#include <stdio.h>

/* Anatomical bone shapes. Each kind is drawn in centimetres in a design frame
   (x lateral, y front, z up) for the character's left side at rest, relative
   to the bone's joint. mirror flips x for right bones and a rotation carries
   the design axis onto the authored aim, so a bone may lean without redrawing.
   Parts are ellipsoids and strands (the skin's primitives), so the same shape
   draws the skeleton and fuses into the skin. Landmarks name the bony points
   muscles attach to; bilateral landmarks on midline bones carry left_/right_. */

#define ANATOMY_PATH_STEPS 5
#define ANATOMY_MAX_KNOTS 24
#define ANATOMY_EPSILON 0.00001f
#define SHADE_BONE 1.0f
#define SHADE_CARTILAGE 0.8f
#define SHADE_TENDON 0.92f
#define SHADE_CAVITY 0.22f
#define SHADE_TEETH 1.12f

typedef struct { BoneShape *out; mat4 R; float mirror,sl,sf,su,sr; } shape_ctx_t;
typedef struct { vec3 p; float w,t; } knot_t;

static vec3 d3(float x,float y,float z){ return v3(x,y,z); }

static mat4 rot_from_to(vec3 a,vec3 b){
	a=vnorm(a); b=vnorm(b);
	vec3 axis=vcross(a,b); float s=vlen(axis),c=fmaxf(-1,fminf(1,vdot(a,b)));
	mat4 m=mat4_identity();
	if(s<ANATOMY_EPSILON){
		if(c>0) return m;
		axis=vnorm(vcross(a,fabsf(a.x)<0.9f?v3(1,0,0):v3(0,1,0)));
	} else axis=vscale(axis,1.0f/s);
	float angle=atan2f(s,c),cs=cosf(angle),sn=sinf(angle),t=1-cs;
	m.m[0]=t*axis.x*axis.x+cs;        m.m[4]=t*axis.x*axis.y-sn*axis.z; m.m[8]=t*axis.x*axis.z+sn*axis.y;
	m.m[1]=t*axis.x*axis.y+sn*axis.z; m.m[5]=t*axis.y*axis.y+cs;        m.m[9]=t*axis.y*axis.z-sn*axis.x;
	m.m[2]=t*axis.x*axis.z-sn*axis.y; m.m[6]=t*axis.y*axis.z+sn*axis.x; m.m[10]=t*axis.z*axis.z+cs;
	return m;
}

static vec3 body_vec(const shape_ctx_t *c,vec3 d){ return mat4_xform_dir(c->R,v3(c->mirror*d.x*c->sl,-d.y*c->sf,d.z*c->su)); }
static vec3 body_point(const shape_ctx_t *c,vec3 d){ return vscale(body_vec(c,d),0.01f); }
static vec3 body_dir(const shape_ctx_t *c,vec3 d){ return vnorm(body_vec(c,d)); }
static float body_radius(const shape_ctx_t *c,float r){ return r*c->sr*0.01f; }

static void part_push(shape_ctx_t *c,SkinPrim prim,float shade,int flags){
	BonePart part={prim,shade,flags};
	DA_PUSH(c->out->parts,c->out->nparts,c->out->cparts,part);
}

/* Ellipsoid with radius b along axis, a along wide and the third radius across both. */
static void ell(shape_ctx_t *c,vec3 center,vec3 axis,vec3 wide,float a,float b,float third,float taper,float shade,int flags){
	SkinPrim e; memset(&e,0,sizeof(e));
	e.kind=SKIN_PRIM_ELLIPSOID; e.center=body_point(c,center);
	e.ay=body_dir(c,axis);
	vec3 w=body_dir(c,wide);
	w=vsub(w,vscale(e.ay,vdot(w,e.ay)));
	if(vlen(w)<ANATOMY_EPSILON) w=vcross(e.ay,fabsf(e.ay.z)<0.9f?v3(0,0,1):v3(1,0,0));
	e.ax=vnorm(w); e.az=vcross(e.ax,e.ay);
	e.radii=v3(body_radius(c,a),body_radius(c,b),body_radius(c,third)); e.taper=taper;
	part_push(c,e,shade,flags);
}

static void ball(shape_ctx_t *c,vec3 center,float r,float shade,int flags){ ell(c,center,d3(0,0,1),d3(1,0,0),r,r,r,1,shade,flags); }

static float catmull(float p0,float p1,float p2,float p3,float u){
	return 0.5f*(2*p1+(p2-p0)*u+(2*p0-5*p1+4*p2-p3)*u*u+(3*p1-p0-3*p2+p3)*u*u*u);
}

/* Strand through knots (Catmull-Rom); t is the half thickness along out, w the
   half width across. A zero out lets the strand pick its own section frame. */
static void tube_outs(shape_ctx_t *c,const knot_t *k,int n,const vec3 *outs,vec3 out,float shade,int flags){
	SkinPrim st; memset(&st,0,sizeof(st)); st.kind=SKIN_PRIM_STRAND;
	for(int i=0;i+1<n;i++) for(int s=0;s<ANATOMY_PATH_STEPS || (i+2==n && s==ANATOMY_PATH_STEPS);s++){
		float u=(float)s/ANATOMY_PATH_STEPS;
		const knot_t *k0=&k[i>0?i-1:0],*k1=&k[i],*k2=&k[i+1],*k3=&k[i+2<n?i+2:n-1];
		vec3 p=v3(catmull(k0->p.x,k1->p.x,k2->p.x,k3->p.x,u),catmull(k0->p.y,k1->p.y,k2->p.y,k3->p.y,u),catmull(k0->p.z,k1->p.z,k2->p.z,k3->p.z,u));
		vec3 o=outs?lerp(outs[i],outs[i+1],u):out;
		StrandSample sample={body_point(c,p),vlen(o)>ANATOMY_EPSILON?body_dir(c,o):v3(0,0,0),
			body_radius(c,k1->w+(k2->w-k1->w)*u),body_radius(c,k1->t+(k2->t-k1->t)*u)};
		if(vlen(sample.out)<ANATOMY_EPSILON){
			vec3 dir=body_vec(c,vsub(k2->p,k1->p));
			sample.out=vnorm(vcross(dir,fabsf(vnorm(dir).z)<0.9f?v3(0,0,1):v3(1,0,0)));
		}
		DA_PUSH(st.strand.s,st.strand.n,st.strand.c,sample);
	}
	part_push(c,st,shade,flags);
}

static void tube(shape_ctx_t *c,const knot_t *k,int n,vec3 out,float shade,int flags){ tube_outs(c,k,n,NULL,out,shade,flags); }

static void rod(shape_ctx_t *c,vec3 a,vec3 b,float r0,float r1,float shade,int flags){
	knot_t k[2]={{a,r0,r0},{b,r1,r1}};
	tube(c,k,2,d3(0,0,0),shade,flags);
}

static void mark(shape_ctx_t *c,const char *name,vec3 pos,vec3 out){
	BoneLandmark m; snprintf(m.name,sizeof(m.name),"%s",name);
	m.pos=body_point(c,pos); m.out=body_dir(c,out);
	DA_PUSH(c->out->marks,c->out->nmarks,c->out->cmarks,m);
}

/* Bilateral landmark on a midline bone: side +1 is the character's left. */
static void mark_side(shape_ctx_t *c,int side,const char *name,vec3 pos,vec3 out){
	char full[40]; snprintf(full,sizeof(full),"%s_%s",side>0?"left":"right",name);
	mark(c,full,v3(pos.x*side,pos.y,pos.z),v3(out.x*side,out.y,out.z));
}

/* ------------------------------------------------------------ Head -- */

/* A dental arch of 16 teeth on a half-ellipse; crowns point along grow (down or up). */
static void teeth(shape_ctx_t *c,float z,float back,float width,float depth,float grow){
	for(int i=0;i<16;i++){
		float a=M_PIf*(i+0.5f)/16,x=-width*cosf(a),y=back+depth*sinf(a);
		int front=i>=6 && i<10;
		vec3 tangent=d3(sinf(a)*width,cosf(a)*depth,0);
		ell(c,d3(x,y,z),d3(0,0,grow>0?1:-1),tangent,front?0.38f:0.48f,0.55f,front?0.22f:0.42f,0.8f,SHADE_TEETH,BONE_PART_SKELETON);
	}
}

static void shape_skull(shape_ctx_t *c){
	const int B=BONE_PART_BOTH,K=BONE_PART_SKELETON,S=BONE_PART_SKIN;
	vec3 up=d3(0,0,1),lat=d3(1,0,0),fwd=d3(0,1,0);
	ell(c,d3(0,0.8f,6.6f),up,lat,7.4f,7.9f,9.6f,1,SHADE_BONE,B);
	ell(c,d3(0,5.5f,7.5f),up,lat,6.0f,5.5f,4.6f,1,SHADE_BONE,B);
	ell(c,d3(0,-6.0f,4.8f),up,lat,5.2f,4.2f,3.4f,1,SHADE_BONE,B);
	ell(c,d3(0,9.6f,4.6f),lat,up,0.8f,2.2f,0.8f,1,SHADE_BONE,B);
	ell(c,d3(0,7.4f,0.2f),up,lat,3.6f,2.4f,2.6f,1,SHADE_BONE,B);
	ell(c,d3(0,9.9f,3.0f),d3(0,0.35f,-1),lat,0.55f,0.9f,0.3f,1,SHADE_BONE,B);
	ell(c,d3(0,9.85f,1.3f),fwd,lat,1.0f,0.35f,1.5f,1,SHADE_CAVITY,K);
	for(int side=-1;side<=1;side+=2){
		float x=(float)side;
		/* The orbit is a dark socket framed by a rim: forward at the brow, set back laterally. */
		ell(c,d3(3.1f*x,7.3f,3.2f),fwd,lat,1.95f,0.45f,1.75f,1,SHADE_CAVITY,K);
		knot_t rim[9];
		for(int k=0;k<9;k++){
			float a=2*M_PIf*k/8,cx=cosf(a),cz=sinf(a);
			rim[k]=(knot_t){d3((3.1f+2.25f*cx)*x,8.35f+0.55f*cz-0.55f*cx,3.2f+2.0f*cz),0.5f,0.45f};
		}
		tube(c,rim,9,d3(0,1,0),SHADE_BONE,B);
		ell(c,d3(4.6f*x,7.2f,1.2f),up,lat,1.3f,1.1f,1.5f,1,SHADE_BONE,B);
		knot_t arch[]={{d3(5.2f*x,6.8f,1.4f),0.3f,0.65f},{d3(5.9f*x,3.8f,1.4f),0.28f,0.55f},{d3(5.8f*x,1.4f,1.5f),0.3f,0.6f}};
		tube(c,arch,3,d3(0,0,1),SHADE_BONE,B);
		ell(c,d3(5.6f*x,-0.6f,0.0f),d3(0,0,-1),fwd,0.9f,1.5f,0.9f,1,SHADE_BONE,B);
		ell(c,d3(4.3f*x,6.6f,-1.4f),up,lat,1.5f,2.2f,2.0f,1,SHADE_BONE,S);
		ell(c,d3(6.9f*x,-0.2f,1.8f),d3(0,-0.25f,1),fwd,1.6f,2.9f,0.55f,1,SHADE_BONE,S);
		mark_side(c,side,"mastoid",d3(5.9f,-0.8f,-0.6f),d3(0.6f,-0.3f,-0.7f));
		mark_side(c,side,"nuchal",d3(3.8f,-8.4f,3.2f),d3(0.3f,-1,0));
		mark_side(c,side,"nuchal_lateral",d3(5.6f,-5.6f,1.8f),d3(0.6f,-0.8f,0));
		mark_side(c,side,"zygomatic_arch_front",d3(5.6f,5.8f,1.0f),d3(1,0,-0.3f));
		mark_side(c,side,"zygomatic_arch_back",d3(6.2f,2.6f,0.9f),d3(1,0,-0.3f));
		mark_side(c,side,"temporal_front",d3(6.4f,5.2f,6.6f),d3(1,0.3f,0));
		mark_side(c,side,"temporal_back",d3(7.2f,0.5f,7.4f),d3(1,0,0.2f));
		mark_side(c,side,"eye",d3(3.1f,7.3f,3.2f),d3(0,1,0));
	}
	teeth(c,-1.8f,7.1f,2.75f,3.05f,0.6f);
	ell(c,d3(0,10.9f,0.9f),d3(0,0.45f,-1),lat,0.95f,1.9f,0.9f,1,SHADE_BONE,S);
	ell(c,d3(0,10.5f,-1.7f),lat,up,0.7f,2.2f,0.6f,1,SHADE_BONE,S);
	mark(c,"occiput",d3(0,-8.8f,2.4f),d3(0,-1,-0.3f));
	mark(c,"glabella",d3(0,10.3f,5.0f),d3(0,1,0));
	mark(c,"vertex",d3(0,1,14.5f),d3(0,0,1));
}

static void shape_mandible(shape_ctx_t *c){
	const int B=BONE_PART_BOTH,S=BONE_PART_SKIN;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	for(int side=-1;side<=1;side+=2){
		float x=(float)side;
		ell(c,d3(5.3f*x,0,0),lat,up,0.6f,1.0f,0.55f,1,SHADE_BONE,B);
		knot_t ramus[]={{d3(5.2f*x,-0.2f,-0.4f),1.1f,0.45f},{d3(5.0f*x,-0.2f,-3.2f),1.5f,0.45f},{d3(4.8f*x,-0.5f,-5.2f),1.2f,0.5f}};
		tube(c,ramus,3,d3(x,0,0),SHADE_BONE,B);
		knot_t coronoid[]={{d3(4.8f*x,1.4f,-3.6f),0.7f,0.35f},{d3(4.6f*x,2.3f,-1.0f),0.4f,0.3f}};
		tube(c,coronoid,2,d3(x,0,0),SHADE_BONE,B);
		mark_side(c,side,"angle",d3(5.2f,-0.9f,-5.4f),d3(0.8f,-0.3f,-0.3f));
		mark_side(c,side,"ramus",d3(5.4f,0.4f,-3.4f),d3(1,0,0));
		mark_side(c,side,"coronoid",d3(4.7f,2.3f,-1.0f),d3(0.6f,0.3f,0.6f));
	}
	static const float body[9][2]={{-4.8f,-0.8f},{-4.2f,1.6f},{-3.1f,3.8f},{-1.6f,5.0f},{0,5.4f},{1.6f,5.0f},{3.1f,3.8f},{4.2f,1.6f},{4.8f,-0.8f}};
	knot_t k[9]; vec3 outs[9];
	for(int i=0;i<9;i++){ k[i]=(knot_t){d3(body[i][0],body[i][1],i==0||i==8?-5.4f:-5.9f),1.4f,0.5f}; outs[i]=d3(body[i][0],body[i][1]-1.5f,0); }
	tube_outs(c,k,9,outs,d3(0,0,0),SHADE_BONE,B);
	ell(c,d3(0,5.6f,-6.9f),lat,up,0.9f,1.6f,0.7f,1,SHADE_BONE,B);
	teeth(c,-3.8f,2.8f,2.55f,3.0f,-0.6f);
	ell(c,d3(0,6.9f,-3.9f),lat,up,0.7f,2.0f,0.65f,1,SHADE_BONE,S);
	ell(c,d3(0,6.2f,-6.4f),lat,up,1.2f,1.8f,0.9f,1,SHADE_BONE,S);
	mark(c,"chin",d3(0,6.3f,-6.8f),d3(0,1,-0.3f));
}

/* -------------------------------------------------------- Vertebrae -- */

static void shape_vertebra(shape_ctx_t *c,int cervical,int index,int count){
	const int K=BONE_PART_SKELETON;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	if(cervical){
		/* The first link is C7, the vertebra prominens; the top one carries the skull. */
		float spine=index==0?4.1f:index==count-1?1.6f:2.4f+0.25f*(count-1-index)/(float)count;
		ell(c,d3(0,0.4f,1.05f),up,lat,1.5f,0.72f,1.1f,1,SHADE_BONE,K);
		ell(c,d3(0,0.4f,0.08f),up,lat,1.35f,0.25f,0.95f,1,SHADE_CARTILAGE,K);
		rod(c,d3(0,-0.8f,1.0f),d3(0,-spine,0.5f),0.4f,index==0?0.45f:0.3f,SHADE_BONE,K);
		for(int side=-1;side<=1;side+=2){
			ell(c,d3(2.1f*side,0.1f,1.0f),lat,up,0.45f,0.8f,0.5f,1,SHADE_BONE,K);
			ell(c,d3(1.3f*side,-0.6f,1.0f),up,lat,0.55f,0.8f,0.6f,1,SHADE_BONE,K);
			mark_side(c,side,"transverse",d3(2.8f,0.1f,1.0f),d3(1,0,0));
		}
		mark(c,"spinous",d3(0,-spine-0.35f,0.5f),d3(0,-1,0));
		mark(c,"front",d3(0,1.6f,1.0f),d3(0,1,0));
		return;
	}
	ell(c,d3(0,-2.2f,1.9f),up,lat,2.4f,1.45f,1.7f,1,SHADE_BONE,K);
	ell(c,d3(0,-2.2f,0.2f),up,lat,2.25f,0.34f,1.6f,1,SHADE_CARTILAGE,K);
	knot_t spinous[]={{d3(0,-3.9f,1.9f),1.0f,0.4f},{d3(0,-6.4f,1.6f),0.9f,0.35f}};
	tube(c,spinous,2,d3(1,0,0),SHADE_BONE,K);
	for(int side=-1;side<=1;side+=2){
		rod(c,d3(1.5f*side,-3.5f,2.0f),d3(3.9f*side,-3.9f,2.0f),0.45f,0.4f,SHADE_BONE,K);
		ell(c,d3(1.3f*side,-4.1f,1.9f),up,lat,0.6f,0.8f,0.6f,1,SHADE_BONE,K);
		mark_side(c,side,"transverse",d3(4.1f,-3.9f,2.0f),d3(1,0,0));
		mark_side(c,side,"lamina",d3(1.8f,-5.4f,1.8f),d3(0,-1,0));
	}
	mark(c,"spinous",d3(0,-6.8f,1.6f),d3(0,-1,0));
	mark(c,"front",d3(0,-0.4f,1.8f),d3(0,1,0));
}

/* ------------------------------------------------------------ Thorax -- */

#define RIB_COUNT 12
static const float rib_width[RIB_COUNT]={6.8f,9.6f,11.4f,12.7f,13.6f,14.1f,14.4f,14.4f,14.1f,13.4f,12.3f,10.8f};
static const float rib_depth[RIB_COUNT]={7.6f,8.4f,9.1f,9.7f,10.2f,10.6f,10.8f,10.8f,10.4f,9.8f,9.0f,8.2f};
static const float rib_drop[RIB_COUNT]={3.6f,4.6f,5.6f,6.4f,7.0f,7.4f,7.6f,7.4f,6.6f,4.6f,2.8f,1.8f};
static const float rib_end[RIB_COUNT]={140,145,148,150,150,148,145,135,130,125,100,75};
static const float sternal_z[7]={26.5f,24.4f,22.2f,20.0f,17.8f,15.8f,13.8f};
static const float margin[3][3]={{3.6f,10.0f,11.6f},{6.2f,9.2f,8.8f},{8.6f,7.8f,5.6f}};

static float thoracic_z(int i){ return 30.8f-i*(29.5f/(RIB_COUNT-1)); }
static float thoracic_front(int i){ return -4.4f-1.4f*sinf(M_PIf*i/(RIB_COUNT-1)); }
static float sternum_front(float z){ return 7.2f+(26.5f-z)*(3.2f/13.5f); }

/* Point on rib i (0 = first rib) at angle phi from the back, and its outward normal. */
static vec3 rib_point(int i,float phi,float side,vec3 *out){
	float a=phi*M_PIf/180.0f,u=fmaxf(0,(phi-25)/(rib_end[i]-25));
	float z=thoracic_z(i)-0.6f-rib_drop[i]*powf(u,1.4f);
	if(out) *out=d3(side*sinf(a)/rib_width[i],-cosf(a)/rib_depth[i],0);
	return d3(side*rib_width[i]*sinf(a),-rib_depth[i]*cosf(a),z);
}

static void shape_thorax(shape_ctx_t *c){
	const int K=BONE_PART_SKELETON;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	for(int i=0;i<RIB_COUNT;i++){
		float z=thoracic_z(i),f=thoracic_front(i),size=i<2?0.8f:1.0f;
		ell(c,d3(0,f,z),up,lat,1.9f*size,1.05f,1.45f*size,1,SHADE_BONE,K);
		ell(c,d3(0,f,z-1.33f),up,lat,1.75f*size,0.25f,1.35f*size,1,SHADE_CARTILAGE,K);
		rod(c,d3(0,f-1.6f,z),d3(0,f-4.4f,z-2.2f),0.4f,0.3f,SHADE_BONE,K);
		char name[16]; snprintf(name,sizeof(name),"t%d_spine",i+1);
		mark(c,name,d3(0,f-4.8f,z-2.3f),d3(0,-1,0));
		for(int side=-1;side<=1;side+=2){
			float x=(float)side;
			ell(c,d3(2.6f*x,f-1.9f,z+0.2f),lat,up,0.5f,0.8f,0.5f,1,SHADE_BONE,K);
			knot_t k[ANATOMY_MAX_KNOTS]; vec3 outs[ANATOMY_MAX_KNOTS]; int n=0;
			k[n]=(knot_t){d3(1.8f*x,f-0.3f,z),0.45f,0.45f}; outs[n++]=d3(x,0,0);
			k[n]=(knot_t){d3(3.6f*x,f-1.9f,z-0.2f),0.4f,0.6f}; outs[n++]=d3(x,-1,0);
			for(float phi=25;phi<rib_end[i]+1 && n<ANATOMY_MAX_KNOTS;phi+=15){
				if(phi>rib_end[i]) phi=rib_end[i];
				float u=(phi-25)/(rib_end[i]-25),w=0.85f-0.25f*u;
				k[n].p=rib_point(i,phi,x,&outs[n]); k[n].w=w; k[n].t=0.32f; n++;
			}
			tube_outs(c,k,n,outs,d3(0,0,0),SHADE_BONE,K);
			vec3 end=k[n-1].p,cart[3];
			if(i<7) cart[2]=d3(1.3f*x,sternum_front(sternal_z[i])+0.3f,sternal_z[i]);
			else if(i<10) cart[2]=d3(margin[i-7][0]*x,margin[i-7][1],margin[i-7][2]);
			if(i<10){
				cart[0]=end; cart[1]=d3((end.x+cart[2].x)*0.5f,fmaxf(end.y,cart[2].y)+0.4f,(end.z*0.4f+cart[2].z*0.6f));
				knot_t ck[3]={{cart[0],0.6f,0.3f},{cart[1],0.55f,0.3f},{cart[2],0.5f,0.3f}};
				tube(c,ck,3,d3(0,1,0),SHADE_CARTILAGE,K);
			}
			vec3 o; char rib[24];
			vec3 p=rib_point(i,fminf(rib_end[i]-5,95),1,&o); snprintf(rib,sizeof(rib),"rib%d",i+1); mark_side(c,side,rib,p,o);
			p=rib_point(i,rib_end[i]-8,1,&o); snprintf(rib,sizeof(rib),"rib%d_front",i+1); mark_side(c,side,rib,p,o);
			p=rib_point(i,40,1,&o); snprintf(rib,sizeof(rib),"rib%d_back",i+1); mark_side(c,side,rib,p,o);
		}
	}
	ell(c,d3(0,sternum_front(26.3f),26.3f),up,lat,2.5f,1.9f,0.75f,1,SHADE_BONE,K);
	knot_t sternum[]={{d3(0,sternum_front(24.4f),24.4f),1.5f,0.6f},{d3(0,sternum_front(19),19),1.6f,0.6f},{d3(0,sternum_front(14),14),1.3f,0.55f}};
	tube(c,sternum,3,d3(0,1,0),SHADE_BONE,K);
	ell(c,d3(0,sternum_front(12.6f),12.6f),d3(0,0,-1),lat,0.8f,1.2f,0.3f,1,SHADE_CARTILAGE,K);
	mark(c,"sternum_top",d3(0,sternum_front(27.2f)+0.4f,27.2f),d3(0,1,0.4f));
	mark(c,"sternum_mid",d3(0,sternum_front(19)+0.7f,19),d3(0,1,0));
	mark(c,"xiphoid",d3(0,sternum_front(12.4f)+0.5f,12.4f),d3(0,1,0));
	for(int side=-1;side<=1;side+=2){
		mark_side(c,side,"manubrium",d3(1.2f,sternum_front(26.4f)+0.6f,26.4f),d3(0.2f,1,0.2f));
		mark_side(c,side,"sternal_top",d3(1.8f,sternum_front(25.5f)+0.6f,25.5f),d3(0,1,0));
		mark_side(c,side,"sternal_bottom",d3(1.6f,sternum_front(15)+0.6f,15),d3(0,1,0));
		mark_side(c,side,"costal_6",d3(4.8f,sternum_front(14.5f)+0.3f,13.4f),d3(0.2f,1,0));
		mark_side(c,side,"rectus_medial",d3(1.2f,sternum_front(12.6f)+0.6f,12.6f),d3(0,1,0));
		mark_side(c,side,"rectus_lateral",d3(7.2f,9.6f,11.2f),d3(0.3f,1,0));
		mark_side(c,side,"erector_top",d3(3.2f,thoracic_front(0)-4.0f,28.5f),d3(0,-1,0));
		mark_side(c,side,"erector_mid",d3(3.8f,thoracic_front(6)-4.6f,15.0f),d3(0,-1,0));
	}
}

/* ------------------------------------------------------------ Pelvis -- */

static void shape_pelvis(shape_ctx_t *c){
	const int K=BONE_PART_SKELETON;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	static const float crest[6][3]={{4.2f,-6.6f,11.0f},{7.5f,-6.0f,14.4f},{11.0f,-3.2f,16.0f},{12.8f,0.8f,15.3f},{12.6f,3.8f,13.0f},{11.4f,5.4f,10.5f}};
	for(int side=-1;side<=1;side+=2){
		float x=(float)side;
		vec3 apex=d3(9.0f*x,-0.3f,8.0f);
		ell(c,d3(8.6f*x,0.2f,5.6f),lat,up,2.9f,1.2f,2.9f,1,SHADE_BONE,K);
		knot_t rim[6];
		for(int i=0;i<6;i++){
			vec3 p=d3(crest[i][0]*x,crest[i][1],crest[i][2]);
			rim[i]=(knot_t){p,0.75f,0.75f};
			if(i%2) continue;
			knot_t fan[]={{apex,1.4f,0.6f},{lerp(apex,p,0.5f),2.6f,0.5f},{lerp(apex,p,0.9f),2.0f,0.45f}};
			tube(c,fan,3,d3(x,crest[i][1]*0.08f,0.15f),SHADE_BONE,K);
		}
		tube(c,rim,6,d3(0,0,0),SHADE_BONE,K);
		/* Wing plates: the gluteal surface faces back and out behind, out in front. */
		ell(c,d3(8.0f*x,-4.2f,11.2f),d3(0,0,1),d3(7.8f*x,4.8f,0),4.9f,3.6f,0.45f,1,SHADE_BONE,K);
		ell(c,d3(11.7f*x,1.6f,11.0f),d3(0,0,1),d3(-0.6f*x,7.2f,0),4.2f,3.8f,0.45f,1,SHADE_BONE,K);
		knot_t si[]={{apex,1.4f,0.6f},{d3(6.0f*x,-3.8f,9.0f),1.8f,0.6f},{d3(4.2f*x,-5.4f,9.4f),1.6f,0.8f}};
		tube(c,si,3,d3(0,0,1),SHADE_BONE,K);
		knot_t pubis[]={{d3(7.8f*x,2.6f,4.9f),1.0f,1.0f},{d3(4.5f*x,5.0f,4.0f),0.9f,0.9f},{d3(1.2f*x,5.6f,3.2f),1.0f,1.0f}};
		tube(c,pubis,3,d3(0,0,0),SHADE_BONE,K);
		ell(c,d3(0.7f*x,5.8f,2.4f),up,lat,0.7f,1.9f,1.0f,1,SHADE_CARTILAGE,K);
		knot_t ramus[]={{d3(1.2f*x,5.4f,1.4f),0.75f,0.75f},{d3(3.6f*x,3.6f,-0.4f),0.75f,0.75f},{d3(6.0f*x,-0.2f,-1.4f),0.9f,0.9f}};
		tube(c,ramus,3,d3(0,0,0),SHADE_BONE,K);
		rod(c,d3(8.2f*x,-1.3f,3.6f),d3(7.0f*x,-2.8f,0.2f),1.3f,1.2f,SHADE_BONE,K);
		ell(c,d3(6.4f*x,-2.9f,-0.8f),up,lat,1.3f,1.8f,1.5f,1,SHADE_BONE,K);
		mark_side(c,side,"asis",d3(11.6f,5.6f,10.4f),d3(0.5f,1,0));
		mark_side(c,side,"aiis",d3(10.2f,4.2f,7.6f),d3(0.2f,1,0));
		mark_side(c,side,"iliac_crest_front",d3(12.6f,3.6f,13.8f),d3(0.6f,0,1));
		mark_side(c,side,"iliac_crest_mid",d3(12.0f,-1.8f,16.2f),d3(0.4f,0,1));
		mark_side(c,side,"iliac_crest_back",d3(8.2f,-6.4f,14.4f),d3(0,-1,0.6f));
		mark_side(c,side,"psis",d3(4.2f,-7.1f,11.0f),d3(0,-1,0));
		mark_side(c,side,"glute_med_front",d3(13.0f,2.4f,11.6f),d3(1,0.2f,0));
		mark_side(c,side,"glute_med_mid",d3(12.8f,-1.2f,12.8f),d3(1,0,0));
		mark_side(c,side,"glute_med_back",d3(10.6f,-4.8f,12.6f),d3(0.7f,-0.7f,0));
		mark_side(c,side,"sacrum",d3(2.8f,-7.4f,7.6f),d3(0,-1,0));
		mark_side(c,side,"pubic_tubercle",d3(2.4f,6.4f,3.4f),d3(0,1,0));
		mark_side(c,side,"pubic_crest",d3(1.4f,6.4f,3.8f),d3(0,1,0.4f));
		mark_side(c,side,"pubic_body",d3(1.6f,6.0f,1.2f),d3(0.2f,1,-0.4f));
		mark_side(c,side,"ischial_ramus",d3(4.2f,3.2f,-0.8f),d3(0.2f,0,-1));
		mark_side(c,side,"ischial_tuberosity",d3(6.6f,-3.6f,-1.4f),d3(0,-0.7f,-0.7f));
		mark_side(c,side,"erector_base",d3(3.0f,-7.2f,9.5f),d3(0,-1,0));
	}
	ell(c,d3(0,-5.8f,8.35f),d3(0,0.47f,1),lat,1.7f,4.3f,0.5f,2.8f,SHADE_BONE,K);
	rod(c,d3(0,-7.0f,4.2f),d3(0,-6.4f,1.4f),0.55f,0.3f,SHADE_BONE,K);
	mark(c,"coccyx",d3(0,-7.2f,1.8f),d3(0,-1,0));
}

/* ---------------------------------------------------- Shoulder girdle -- */

static void shape_clavicle(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	knot_t k[]={{d3(0,0,0),1.1f,1.0f},{d3(4.5f,0.6f,0.9f),0.8f,0.8f},{d3(9.5f,-1.6f,1.8f),0.8f,0.75f},{d3(13.5f,-4.9f,2.2f),1.2f,0.55f},{d3(16.1f,-7.5f,2.5f),1.3f,0.5f}};
	tube(c,k,5,d3(0,0,1),SHADE_BONE,B);
	ell(c,d3(0.3f,0.1f,0.1f),d3(1,0,0),d3(0,0,1),1.2f,1.1f,1.2f,1,SHADE_BONE,B);
	mark(c,"sternal",d3(0.8f,0.9f,1.0f),d3(0,0.6f,0.8f));
	mark(c,"medial_front_1",d3(2.6f,1.4f,0.4f),d3(0,1,0));
	mark(c,"medial_front_2",d3(7.8f,0.1f,1.2f),d3(0,1,0));
	mark(c,"lateral_front",d3(12.2f,-3.0f,1.7f),d3(0.3f,1,0));
	mark(c,"lateral_end",d3(15.8f,-6.5f,2.0f),d3(0.6f,0.6f,0));
	mark(c,"lateral_top",d3(14.0f,-5.4f,2.9f),d3(0,0,1));
}

static void shape_scapula(shape_ctx_t *c){
	const int B=BONE_PART_BOTH,K=BONE_PART_SKELETON;
	static const float border[6][3]={{-11,-6.5f,-1.5f},{-11,-7.4f,-3.2f},{-11.2f,-8.0f,-5},{-10.6f,-8.3f,-9},{-10.1f,-8.5f,-13},{-9.5f,-8.5f,-17.5f}};
	vec3 neck=d3(-3.0f,-0.8f,-5.0f),glenoid=d3(-1.6f,0.3f,-4.3f);
	vec3 normal=vnorm(vcross(vsub(d3(border[5][0],border[5][1],border[5][2]),glenoid),vsub(d3(border[0][0],border[0][1],border[0][2]),glenoid)));
	if(normal.y>0) normal=vscale(normal,-1);
	knot_t edge[6];
	for(int i=0;i<6;i++){
		vec3 p=d3(border[i][0],border[i][1],border[i][2]);
		knot_t fan[]={{neck,1.0f,0.4f},{lerp(neck,p,0.5f),2.0f,0.32f},{lerp(neck,p,0.94f),1.4f,0.3f}};
		tube(c,fan,3,normal,SHADE_BONE,K);
		edge[i]=(knot_t){p,0.45f,0.45f};
	}
	tube(c,edge,6,d3(0,0,0),SHADE_BONE,K);
	rod(c,d3(-2.4f,-0.6f,-6.0f),d3(-9.5f,-8.5f,-17.5f),0.8f,0.5f,SHADE_BONE,K);
	knot_t spine[]={{d3(0.4f,-3.6f,0.1f),0.6f,0.9f},{d3(-4.0f,-6.4f,-2.2f),0.5f,1.0f},{d3(-11.0f,-8.2f,-5.0f),0.4f,0.6f}};
	tube(c,spine,3,d3(0,0,1),SHADE_BONE,B);
	ell(c,d3(0.8f,-1.8f,0.3f),d3(0,1,0),d3(1,0,0),2.0f,2.8f,0.6f,1,SHADE_BONE,B);
	knot_t coracoid[]={{d3(-3.0f,-1.0f,-2.5f),0.7f,0.7f},{d3(-3.2f,1.8f,-2.6f),0.6f,0.6f},{d3(-2.6f,3.0f,-3.4f),0.5f,0.5f}};
	tube(c,coracoid,3,d3(0,0,0),SHADE_BONE,K);
	ell(c,glenoid,d3(1,0,0),d3(0,0,1),2.0f,0.5f,1.3f,1,SHADE_BONE,K);
	mark(c,"acromion",d3(1.4f,-1.2f,0.8f),d3(0.3f,0,1));
	mark(c,"acromion_back",d3(0.6f,-3.6f,0.6f),d3(0,-0.5f,1));
	mark(c,"spine_lateral",d3(-0.8f,-4.8f,-0.4f),d3(0,-1,0.5f));
	mark(c,"spine_mid",d3(-5.0f,-7.2f,-2.4f),d3(0,-1,0.4f));
	mark(c,"spine_root",d3(-11.0f,-8.8f,-5.0f),d3(0,-1,0));
	mark(c,"coracoid",d3(-2.6f,3.3f,-3.6f),d3(0,1,0));
	mark(c,"supraglenoid",d3(-1.4f,0.4f,-2.4f),d3(0,0.5f,1));
	mark(c,"infraglenoid",d3(-2.3f,-0.6f,-6.3f),d3(0.5f,-0.7f,0));
	mark(c,"infraspinous",d3(-6.2f,-7.9f,-9.5f),d3(0,-1,0));
	mark(c,"supraspinous",d3(-6.0f,-5.8f,-1.6f),d3(0,-0.5f,1));
	mark(c,"teres",d3(-8.0f,-7.9f,-15.0f),d3(0.3f,-1,0));
	mark(c,"inferior_angle",d3(-9.5f,-9.0f,-17.8f),d3(0,-1,-0.3f));
	mark(c,"medial_top",d3(-11.2f,-6.6f,-1.4f),d3(0,-0.6f,0.8f));
	mark(c,"medial_mid",d3(-11.3f,-8.4f,-9.0f),d3(0,-1,0));
	mark(c,"medial_bottom",d3(-10.2f,-8.7f,-15.5f),d3(0,-1,0));
	mark(c,"medial_front_top",d3(-10.6f,-5.8f,-2.0f),d3(0,1,0));
	mark(c,"medial_front_bottom",d3(-9.6f,-7.6f,-17.0f),d3(0,1,0));
}

/* ------------------------------------------------------------ Arm -- */

static void shape_humerus(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	ball(c,d3(0,0,0),2.4f,SHADE_BONE,B);
	ell(c,d3(2.0f,0.2f,-1.0f),up,lat,1.3f,1.8f,1.6f,1,SHADE_BONE,B);
	ball(c,d3(0.5f,1.9f,-1.4f),0.8f,SHADE_BONE,B);
	knot_t shaft[]={{d3(1.0f,0.3f,-3.0f),1.4f,1.4f},{d3(0.9f,0.4f,-12),1.3f,1.3f},{d3(0.4f,0.3f,-24),1.4f,1.15f},{d3(0.1f,0.3f,-30.5f),2.2f,1.0f},{d3(-0.2f,0.3f,-32.4f),2.9f,0.9f}};
	tube(c,shaft,5,d3(0,1,0),SHADE_BONE,B);
	ell(c,d3(1.6f,0.4f,-14),d3(0,0,-1),lat,0.5f,1.8f,0.6f,1,SHADE_BONE,B);
	ell(c,d3(-0.5f,0.6f,-33.8f),lat,up,1.1f,1.5f,1.2f,1,SHADE_BONE,B);
	ball(c,d3(1.3f,0.9f,-33.8f),1.05f,SHADE_BONE,B);
	ell(c,d3(-2.8f,-0.1f,-32.6f),lat,up,0.8f,1.2f,0.8f,1,SHADE_BONE,B);
	ball(c,d3(2.3f,-0.1f,-32.4f),0.65f,SHADE_BONE,B);
	mark(c,"greater_tubercle",d3(2.9f,0.2f,-1.2f),d3(1,0,0));
	mark(c,"greater_tubercle_back",d3(2.0f,-1.6f,-1.0f),d3(0,-1,0));
	mark(c,"head_front",d3(0.8f,2.2f,0.4f),d3(0.2f,1,0.3f));
	mark(c,"head_top",d3(2.2f,0.2f,1.2f),d3(0.7f,0,0.7f));
	mark(c,"head_back",d3(1.2f,-2.2f,0.4f),d3(0.2f,-1,0.3f));
	mark(c,"lesser_tubercle",d3(0.6f,2.7f,-1.6f),d3(0,1,0));
	mark(c,"bicipital_groove",d3(1.4f,2.3f,-3.0f),d3(0,1,0));
	mark(c,"crest_top",d3(2.0f,1.9f,-4.0f),d3(0.3f,1,0));
	mark(c,"crest_mid",d3(1.9f,1.8f,-7.0f),d3(0.3f,1,0));
	mark(c,"crest_low",d3(1.8f,1.7f,-9.5f),d3(0.3f,1,0));
	mark(c,"medial_lip",d3(0.2f,1.8f,-5.5f),d3(-0.3f,1,0));
	mark(c,"intertubercular_floor",d3(0.9f,2.0f,-6.0f),d3(0,1,0));
	mark(c,"deltoid_tuberosity",d3(2.1f,0.6f,-14.5f),d3(1,0,0));
	mark(c,"anterior_upper",d3(0.6f,1.9f,-7),d3(0,1,0));
	mark(c,"anterior_distal_upper",d3(0.4f,1.4f,-18),d3(0,1,0));
	mark(c,"anterior_distal_lower",d3(0.2f,1.3f,-28),d3(0,1,0));
	mark(c,"posterior_upper",d3(1.2f,-1.6f,-6),d3(0,-1,0));
	mark(c,"posterior_lower",d3(0.4f,-1.4f,-20),d3(0,-1,0));
	mark(c,"posterior_distal",d3(0.2f,-1.4f,-30.0f),d3(0,-1,0));
	mark(c,"lateral_supracondylar_upper",d3(1.9f,0,-22),d3(1,0,0));
	mark(c,"lateral_supracondylar_lower",d3(2.4f,0,-29.5f),d3(1,0,0));
	mark(c,"lateral_epicondyle",d3(2.9f,-0.1f,-32.4f),d3(1,0,0));
	mark(c,"lateral_epicondyle_front",d3(2.4f,1.3f,-32),d3(0.5f,1,0));
	mark(c,"medial_epicondyle",d3(-3.5f,-0.1f,-32.6f),d3(-1,0,0));
	mark(c,"medial_epicondyle_top",d3(-3.0f,0.3f,-31.2f),d3(-1,0.3f,0.3f));
}

static void shape_ulna(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	ell(c,d3(-0.3f,-1.9f,0.9f),up,lat,1.2f,1.9f,1.1f,1,SHADE_BONE,B);
	ball(c,d3(-0.3f,-0.6f,-0.8f),1.3f,SHADE_BONE,B);
	ell(c,d3(-0.3f,0.7f,-1.6f),up,lat,0.9f,0.7f,0.8f,1,SHADE_BONE,B);
	knot_t shaft[]={{d3(-0.4f,-0.8f,-2.0f),1.0f,1.0f},{d3(-0.6f,-0.9f,-12),0.8f,0.8f},{d3(-0.9f,-0.8f,-22),0.6f,0.6f},{d3(-1.0f,-0.7f,-25.3f),0.75f,0.75f}};
	tube(c,shaft,4,d3(0,0,0),SHADE_BONE,B);
	ball(c,d3(-1.0f,-0.6f,-26.0f),0.95f,SHADE_BONE,B);
	ell(c,d3(-1.3f,-1.3f,-26.8f),d3(0,0,-1),lat,0.45f,0.7f,0.45f,1,SHADE_BONE,B);
	mark(c,"olecranon",d3(-0.3f,-3.0f,1.2f),d3(0,-1,0.3f));
	mark(c,"coronoid",d3(-0.3f,1.4f,-2.2f),d3(0,1,0));
	mark(c,"posterior_border",d3(-0.6f,-1.8f,-12),d3(0,-1,0));
	mark(c,"medial_upper",d3(-1.6f,0.2f,-4),d3(-1,0,0));
	mark(c,"ulnar_head_dorsal",d3(-0.2f,-0.8f,-25.6f),d3(1,0,0));
	mark(c,"ulnar_head_palmar",d3(-2.0f,-0.6f,-25.6f),d3(-1,0,0));
}

/* The radius is drawn from its own head, in the same design frame as the ulna. */
static void shape_radius(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	vec3 up=d3(0,0,1),lat=d3(1,0,0),fwd=d3(0,1,0);
	ell(c,d3(0,0,0),up,lat,1.15f,0.55f,1.15f,1,SHADE_BONE,B);
	rod(c,d3(0,0,-0.5f),d3(-0.05f,0.1f,-2.6f),0.62f,0.62f,SHADE_BONE,B);
	ball(c,d3(-0.6f,-0.35f,-3.0f),0.6f,SHADE_BONE,B);
	knot_t shaft[]={{d3(0,0.1f,-2.6f),0.7f,0.7f},{d3(0.4f,0.4f,-9),0.8f,0.8f},{d3(0.2f,0.8f,-17),0.9f,0.9f},{d3(-0.5f,1.1f,-22.5f),1.2f,1.1f}};
	tube(c,shaft,4,d3(0,0,0),SHADE_BONE,B);
	ell(c,d3(-0.8f,1.0f,-24.2f),d3(0,0,-1),fwd,1.9f,1.1f,1.1f,1,SHADE_BONE,B);
	ball(c,d3(-0.6f,2.6f,-25.3f),0.5f,SHADE_BONE,B);
	mark(c,"radial_tuberosity",d3(-0.9f,-0.6f,-3.0f),d3(-0.7f,-0.7f,0));
	mark(c,"mid_lateral",d3(1.2f,0.5f,-10),d3(1,0,0));
	mark(c,"mid_front",d3(0.4f,1.4f,-10),d3(0,1,0));
	mark(c,"styloid_base",d3(0.2f,2.5f,-22.5f),d3(0.5f,1,0));
	mark(c,"distal_dorsal",d3(0.4f,1.0f,-23.0f),d3(1,0,0));
	mark(c,"distal_palmar",d3(-2.0f,1.0f,-23.0f),d3(-1,0,0));
}

/* Neutral hand: palm toward the thigh (medial), thumb forward, fingers relaxed. */
static void finger(shape_ctx_t *c,vec3 knuckle,float spread,const float *lengths,const float *bends,int n,float r,int flags){
	vec3 p=knuckle; float angle=0;
	for(int i=0;i<n;i++){
		angle+=bends[i]*M_PIf/180.0f;
		vec3 d=vnorm(d3(-sinf(angle),spread,-cosf(angle)));
		vec3 q=vadd(p,vscale(d,lengths[i]));
		float rr=r*(1-0.12f*i);
		rod(c,p,q,rr,rr*0.9f,SHADE_BONE,flags);
		ball(c,p,rr*1.12f,SHADE_BONE,flags);
		p=q;
	}
}

static void shape_hand(shape_ctx_t *c){
	const int B=BONE_PART_BOTH,S=BONE_PART_SKIN;
	vec3 up=d3(0,0,1),fwd=d3(0,1,0);
	static const float base[4][3]={{0.2f,1.9f,-3.2f},{0.3f,0.7f,-3.2f},{0.2f,-0.5f,-3.2f},{0,-1.6f,-3.2f}};
	static const float head[4][3]={{0.1f,2.3f,-9.2f},{0.2f,0.8f,-9.5f},{0.1f,-0.7f,-9.0f},{-0.1f,-2.0f,-8.4f}};
	static const float lengths[4][3]={{4.2f,2.5f,1.9f},{4.6f,2.9f,2.0f},{4.3f,2.8f,1.9f},{3.4f,2.0f,1.7f}};
	static const float bends[3]={12,22,12},spread[4]={0.07f,0.02f,-0.03f,-0.08f};
	ell(c,d3(0,0.2f,-1.8f),d3(0,0,-1),fwd,2.5f,1.5f,1.1f,1,SHADE_BONE,B);
	for(int i=0;i<4;i++){
		vec3 a=d3(base[i][0],base[i][1],base[i][2]),b=d3(head[i][0],head[i][1],head[i][2]);
		rod(c,a,b,0.5f,0.48f,SHADE_BONE,B);
		finger(c,b,spread[i],lengths[i],bends,3,i==3?0.44f:0.5f,B);
	}
	rod(c,d3(-0.6f,2.0f,-2.6f),d3(-1.8f,4.1f,-5.9f),0.55f,0.5f,SHADE_BONE,B);
	static const float thumb[2]={3.0f,2.4f},thumbBend[2]={10,15};
	vec3 p=d3(-1.8f,4.1f,-5.9f); float angle=0;
	for(int i=0;i<2;i++){
		angle+=thumbBend[i]*M_PIf/180.0f;
		vec3 d=vnorm(d3(-0.35f-sinf(angle)*0.4f,0.45f,-cosf(angle)));
		vec3 q=vadd(p,vscale(d,thumb[i]));
		rod(c,p,q,0.5f,0.45f,SHADE_BONE,B); ball(c,p,0.56f,SHADE_BONE,B); p=q;
	}
	ell(c,d3(-0.6f,0.2f,-5.8f),d3(0,0,-1),fwd,2.6f,3.2f,0.8f,1,SHADE_BONE,S);
	ell(c,d3(-0.3f,-1.4f,-4.4f),up,fwd,1.0f,2.2f,0.9f,1,SHADE_BONE,S);
	mark(c,"dorsum",d3(0.9f,0.4f,-5.5f),d3(1,0,0));
	mark(c,"knuckles_back_index",d3(0.8f,2.3f,-9.0f),d3(1,0,0));
	mark(c,"knuckles_back_little",d3(0.6f,-2.0f,-8.2f),d3(1,0,0));
	mark(c,"metacarpal2_base_back",d3(0.8f,1.9f,-3.4f),d3(1,0,0));
	mark(c,"metacarpal3_base_back",d3(0.9f,0.7f,-3.4f),d3(1,0,0));
	mark(c,"metacarpal5_base",d3(0.4f,-1.9f,-3.2f),d3(0.3f,-1,0));
	mark(c,"palm_radial",d3(-1.0f,1.6f,-3.2f),d3(-1,0,0));
	mark(c,"palm_ulnar",d3(-1.0f,-1.4f,-3.2f),d3(-1,0,0));
	mark(c,"palm",d3(-1.2f,0.2f,-5.5f),d3(-1,0,0));
	mark(c,"thenar_base",d3(-0.9f,2.2f,-2.2f),d3(-1,0.3f,0));
	mark(c,"thumb_base",d3(-2.0f,4.2f,-6.0f),d3(-1,0,0));
	mark(c,"pisiform",d3(-0.9f,-1.8f,-1.8f),d3(-1,0,0));
}

/* ------------------------------------------------------------ Leg -- */

static void shape_femur(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	ball(c,d3(0,0,0),2.5f,SHADE_BONE,B);
	rod(c,d3(0.6f,0.2f,-0.4f),d3(3.6f,0.3f,-3.0f),1.55f,1.7f,SHADE_BONE,B);
	ell(c,d3(5.0f,-0.6f,-2.2f),up,lat,1.6f,2.6f,2.1f,1,SHADE_BONE,B);
	ball(c,d3(2.6f,-1.4f,-5.8f),0.9f,SHADE_BONE,B);
	knot_t shaft[]={{d3(4.0f,0,-4),1.6f,1.6f},{d3(3.2f,0.6f,-14),1.45f,1.4f},{d3(2.0f,0.9f,-26),1.4f,1.35f},{d3(1.0f,0.6f,-36),1.7f,1.45f},{d3(0.3f,0,-41.5f),2.6f,1.6f}};
	tube(c,shaft,5,d3(0,1,0),SHADE_BONE,B);
	ell(c,d3(-2.1f,-0.7f,-44.0f),up,lat,1.8f,2.4f,2.9f,1,SHADE_BONE,B);
	ell(c,d3(2.2f,-0.7f,-44.0f),up,lat,1.8f,2.4f,2.9f,1,SHADE_BONE,B);
	ell(c,d3(0,1.5f,-43.0f),up,lat,2.2f,1.8f,1.0f,1,SHADE_BONE,B);
	ball(c,d3(-3.4f,-0.4f,-43.2f),0.9f,SHADE_BONE,B);
	ball(c,d3(3.3f,-0.4f,-43.3f),0.8f,SHADE_BONE,B);
	ball(c,d3(-3.2f,-0.8f,-41.2f),0.6f,SHADE_BONE,B);
	mark(c,"greater_trochanter",d3(5.8f,-0.6f,-1.2f),d3(1,0,0.4f));
	mark(c,"greater_trochanter_low",d3(5.4f,0.3f,-4.2f),d3(1,0,0));
	mark(c,"gluteal_tuberosity",d3(4.8f,-1.8f,-8.5f),d3(0.5f,-1,0));
	mark(c,"it_band_top",d3(6.4f,-0.2f,-6.0f),d3(1,0,0));
	mark(c,"lesser_trochanter",d3(2.3f,-2.0f,-6),d3(-0.5f,-1,0));
	mark(c,"intertrochanteric_front",d3(4.0f,1.6f,-4.5f),d3(0,1,0));
	mark(c,"intertrochanteric_low",d3(2.4f,1.0f,-7.0f),d3(-0.4f,1,0));
	mark(c,"linea_aspera_upper",d3(3.6f,-1.6f,-14),d3(0,-1,0));
	mark(c,"linea_aspera_mid",d3(2.4f,-1.8f,-24),d3(0,-1,0));
	mark(c,"linea_aspera_lower",d3(1.4f,-1.8f,-34),d3(0,-1,0));
	mark(c,"linea_aspera_lateral",d3(3.6f,-1.2f,-20),d3(0.5f,-1,0));
	mark(c,"linea_aspera_medial_low",d3(1.0f,-1.6f,-32),d3(-0.5f,-1,0));
	mark(c,"adductor_tubercle",d3(-3.6f,-0.8f,-41.0f),d3(-1,0,0));
	mark(c,"medial_epicondyle",d3(-3.9f,-0.4f,-43.2f),d3(-1,0,0));
	mark(c,"medial_epicondyle_back",d3(-3.2f,-2.4f,-43.0f),d3(-0.6f,-0.8f,0));
	mark(c,"lateral_epicondyle",d3(3.8f,-0.4f,-43.3f),d3(1,0,0));
	mark(c,"medial_condyle_back",d3(-2.2f,-2.8f,-42.0f),d3(0,-1,0));
	mark(c,"lateral_condyle_back",d3(2.3f,-2.8f,-42.0f),d3(0,-1,0));
	mark(c,"anterior_mid",d3(2.0f,2.2f,-24),d3(0,1,0));
	mark(c,"lateral_mid",d3(4.2f,0.6f,-24),d3(1,0,0));
	mark(c,"lateral_distal",d3(3.4f,0.4f,-38),d3(1,0,0));
	mark(c,"anterior_distal",d3(0.8f,2.6f,-40),d3(0,1,0));
	mark(c,"medial_mid",d3(0.5f,0.6f,-24),d3(-1,0,0));
	mark(c,"medial_distal",d3(-2.0f,0.6f,-38),d3(-1,0,0));
}

static void shape_patella(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	knot_t ligament[]={{d3(0,0.1f,0),1.2f,0.35f},{d3(0,0.5f,5.8f),1.1f,0.35f}};
	tube(c,ligament,2,d3(0,1,0),SHADE_TENDON,B);
	ell(c,d3(0,0.8f,8.2f),d3(0,0,1),d3(1,0,0),1.6f,2.4f,0.65f,1.4f,SHADE_BONE,B);
	mark(c,"top",d3(0,1.0f,10.5f),d3(0,0.3f,1));
	mark(c,"top_lateral",d3(1.8f,1.0f,10.0f),d3(0.4f,0.3f,1));
	mark(c,"top_medial",d3(-1.8f,1.0f,10.0f),d3(-0.4f,0.3f,1));
	mark(c,"front",d3(0,1.6f,8.2f),d3(0,1,0));
}

static void shape_tibia(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	ell(c,d3(0,0.2f,-1.4f),up,lat,3.9f,1.4f,2.8f,1,SHADE_BONE,B);
	ell(c,d3(-2.2f,-0.2f,-2.2f),up,lat,1.9f,1.8f,2.4f,1,SHADE_BONE,B);
	ell(c,d3(2.2f,-0.4f,-2.4f),up,lat,1.8f,1.7f,2.2f,1,SHADE_BONE,B);
	ell(c,d3(0,2.6f,-5.0f),up,lat,1.0f,1.8f,0.8f,1,SHADE_BONE,B);
	knot_t shaft[]={{d3(0,0.5f,-5),1.6f,1.6f},{d3(-0.1f,0.4f,-20),1.25f,1.25f},{d3(-0.3f,0.2f,-36),1.2f,1.2f},{d3(-0.4f,0.1f,-41.5f),1.5f,1.5f}};
	tube(c,shaft,4,d3(0,0,0),SHADE_BONE,B);
	ell(c,d3(-0.3f,0.2f,-43.6f),up,lat,2.0f,1.3f,1.8f,1,SHADE_BONE,B);
	ell(c,d3(-2.0f,0.1f,-45.0f),up,lat,0.8f,1.6f,1.0f,1,SHADE_BONE,B);
	mark(c,"tuberosity",d3(0,3.3f,-5.2f),d3(0,1,0));
	mark(c,"gerdy",d3(2.4f,1.8f,-3.0f),d3(0.6f,1,0));
	mark(c,"medial_condyle",d3(-3.4f,-0.4f,-2.2f),d3(-1,0,0));
	mark(c,"medial_condyle_back",d3(-2.2f,-2.4f,-2.8f),d3(0,-1,0));
	mark(c,"pes_anserinus",d3(-2.2f,1.4f,-7.2f),d3(-1,0.5f,0));
	mark(c,"pes_anserinus_back",d3(-2.4f,0.2f,-6.4f),d3(-1,0,0));
	mark(c,"lateral_shaft_upper",d3(1.8f,1.2f,-8),d3(0.7f,0.7f,0));
	mark(c,"lateral_shaft_mid",d3(1.6f,0.9f,-24),d3(0.7f,0.7f,0));
	mark(c,"anterior_distal",d3(0.4f,2.2f,-40.5f),d3(0,1,0));
	mark(c,"anterior_distal_lateral",d3(1.6f,1.9f,-41),d3(0.3f,1,0));
	mark(c,"soleal_line",d3(-0.5f,-1.6f,-11),d3(0,-1,0));
	mark(c,"posterior_mid",d3(-0.3f,-1.5f,-24),d3(0,-1,0));
	mark(c,"posterior_medial",d3(-1.8f,-1.8f,-9),d3(-0.5f,-1,0));
	mark(c,"posterior_lateral",d3(1.4f,-2.2f,-9),d3(0.5f,-1,0));
	mark(c,"medial_malleolus",d3(-2.7f,0.1f,-45),d3(-1,0,0));
}

static void shape_fibula(shape_ctx_t *c){
	const int B=BONE_PART_BOTH;
	vec3 up=d3(0,0,1),lat=d3(1,0,0);
	ell(c,d3(0,0,0),up,lat,1.2f,1.3f,1.1f,1,SHADE_BONE,B);
	knot_t shaft[]={{d3(0,0,-1),0.7f,0.7f},{d3(0.3f,0.1f,-20),0.6f,0.6f},{d3(0.1f,0.3f,-38),0.55f,0.55f},{d3(0,0.3f,-40.5f),0.8f,0.8f}};
	tube(c,shaft,4,d3(0,0,0),SHADE_BONE,B);
	ell(c,d3(0.2f,0.2f,-42.2f),up,lat,0.85f,2.0f,1.1f,1,SHADE_BONE,B);
	mark(c,"fibular_head",d3(1.0f,-0.2f,0.3f),d3(1,0,0.3f));
	mark(c,"posterior_upper",d3(0,-1.1f,-4),d3(0,-1,0));
	mark(c,"lateral_upper",d3(0.9f,0.1f,-5),d3(1,0,0));
	mark(c,"lateral_mid",d3(0.9f,0.2f,-20),d3(1,0,0));
	mark(c,"anterior_upper",d3(0.2f,0.9f,-6),d3(0,1,0));
	mark(c,"lateral_malleolus_back",d3(0.4f,-1.2f,-41.0f),d3(0.3f,-1,0));
	mark(c,"lateral_malleolus",d3(1.0f,0.2f,-42.2f),d3(1,0,0));
}

static void shape_foot(shape_ctx_t *c){
	const int B=BONE_PART_BOTH,S=BONE_PART_SKIN;
	vec3 up=d3(0,0,1),lat=d3(1,0,0),fwd=d3(0,1,0);
	ell(c,d3(0,0.6f,-1.4f),fwd,lat,1.9f,2.8f,1.6f,1,SHADE_BONE,B);
	ell(c,d3(0.3f,-0.8f,-5.2f),d3(0,1,0.2f),lat,1.7f,4.4f,1.9f,1,SHADE_BONE,B);
	ell(c,d3(0.2f,-4.4f,-6.1f),up,lat,1.6f,1.4f,1.5f,1,SHADE_BONE,B);
	ell(c,d3(-1.2f,3.6f,-3.0f),fwd,lat,1.3f,0.8f,1.2f,1,SHADE_BONE,B);
	ell(c,d3(1.6f,4.4f,-5.2f),fwd,lat,1.2f,1.4f,1.0f,1,SHADE_BONE,B);
	ell(c,d3(-0.8f,5.6f,-3.8f),fwd,lat,2.0f,0.9f,1.1f,1,SHADE_BONE,B);
	static const float meta[5][6]={{-2.2f,6.6f,-4.2f,-2.9f,13.6f,-6.6f},{-0.8f,7.0f,-4.0f,-1.1f,14.4f,-6.8f},{0.5f,6.8f,-4.3f,0.4f,14.0f,-6.85f},{1.6f,6.4f,-4.8f,1.8f,13.2f,-6.85f},{2.7f,5.9f,-5.6f,3.2f,12.1f,-6.85f}};
	static const float toe[5]={5.8f,4.4f,4.0f,3.7f,3.4f};
	for(int i=0;i<5;i++){
		vec3 a=d3(meta[i][0],meta[i][1],meta[i][2]),b=d3(meta[i][3],meta[i][4],meta[i][5]);
		float r=i==0?0.95f:0.55f;
		rod(c,a,b,r,r*1.05f,SHADE_BONE,B);
		ball(c,b,r*1.35f,SHADE_BONE,B);
		vec3 tip=d3(b.x-(i==0?0.1f:0),b.y+toe[i],-6.9f);
		knot_t k[]={{b,r*0.85f,r*0.8f},{lerp(b,tip,0.55f),r*0.75f,r*0.75f},{tip,r*0.65f,r*0.6f}};
		tube(c,k,3,d3(0,0,1),SHADE_BONE,B);
	}
	ball(c,d3(3.1f,5.6f,-6.0f),0.7f,SHADE_BONE,B);
	ell(c,d3(0.2f,-4.2f,-6.6f),up,lat,1.8f,1.0f,1.8f,1,SHADE_BONE,S);
	ell(c,d3(0.4f,9.0f,-6.9f),fwd,lat,3.0f,5.0f,0.7f,1,SHADE_BONE,S);
	ell(c,d3(-0.1f,13.5f,-6.9f),lat,fwd,1.8f,3.6f,0.7f,1,SHADE_BONE,S);
	mark(c,"calcaneus_back",d3(0.2f,-6.2f,-6.0f),d3(0,-1,0));
	mark(c,"medial_cuneiform",d3(-2.0f,5.8f,-4.6f),d3(-1,0,-0.4f));
	mark(c,"fifth_metatarsal_base",d3(3.8f,5.4f,-6.2f),d3(1,0,0));
	mark(c,"first_metatarsal_base_under",d3(-1.8f,6.8f,-5.6f),d3(0,0,-1));
	mark(c,"toes_top",d3(0,15.0f,-5.8f),d3(0,0,1));
	mark(c,"dorsum",d3(0,7.0f,-3.0f),d3(0,0,1));
}

/* ------------------------------------------------------------ Registry -- */

typedef struct { const char *kind; float axis[3],length,side,other; void (*draw)(shape_ctx_t *c); } shape_kind_t;
static const shape_kind_t shape_kinds[]={
	{ "skull",    { 0,0,1 },            16,   0,    0,    shape_skull    },
	{ "mandible", { 0,5.5f,-7.0f },     8.9f, 0,    0,    shape_mandible },
	{ "thorax",   { 0,0,1 },            32,   15,   11.5f,shape_thorax   },
	{ "pelvis",   { 0,0,1 },            16,   13,   9.5f, shape_pelvis   },
	{ "clavicle", { 16.1f,-7.5f,2.5f }, 17.9f,0,    0,    shape_clavicle },
	{ "scapula",  { -9.5f,-8.5f,-17.5f},21.7f,0,    0,    shape_scapula  },
	{ "humerus",  { 0,0,-1 },           34,   0,    0,    shape_humerus  },
	{ "ulna",     { 0,0,-1 },           27,   0,    0,    shape_ulna     },
	{ "radius",   { -0.8f,1.1f,-24.8f },24.84f,0,   0,    shape_radius   },
	{ "hand",     { 0,0,-1 },           19,   0,    0,    shape_hand     },
	{ "femur",    { 0,0,-1 },           46,   0,    0,    shape_femur    },
	{ "patella",  { 0,0.5f,5.8f },      5.82f,0,    0,    shape_patella  },
	{ "tibia",    { 0,0,-1 },           45,   0,    0,    shape_tibia    },
	{ "fibula",   { -0.6f,0.5f,-43.2f },43.21f,0,   0,    shape_fibula   },
	{ "foot",     { 0,0.94f,-0.342f },  22,   0,    0,    shape_foot     },
};

int bone_shape_build(const BoneShapeSpec *spec,BoneShape *out){
	memset(out,0,sizeof(*out));
	char kind[32]="",variant[32]="";
	if(!spec->kind || sscanf(spec->kind,"%31s %31s",kind,variant)<1) return 0;
	shape_ctx_t c; memset(&c,0,sizeof(c));
	c.out=out; c.mirror=spec->mirror<0?-1.0f:1.0f;
	if(!strcmp(kind,"vertebra")){
		int cervical=!strcmp(variant,"cervical");
		if(!cervical && strcmp(variant,"lumbar")) return 0;
		c.sl=c.sf=c.su=c.sr=spec->length*100.0f/(cervical?2.1f:3.5f);
		c.R=rot_from_to(v3(0,0,1),spec->dir);
		shape_vertebra(&c,cervical,spec->segment,spec->segments);
		return 1;
	}
	for(size_t i=0;i<sizeof(shape_kinds)/sizeof(shape_kinds[0]);i++){
		const shape_kind_t *k=&shape_kinds[i];
		if(strcmp(k->kind,kind)) continue;
		float s=spec->length*100.0f/k->length;
		c.sl=k->side>0?spec->radiusSide*100.0f/k->side:s;
		c.sf=k->other>0?spec->radiusOther*100.0f/k->other:s;
		c.su=s; c.sr=(c.sl+c.sf+c.su)/3.0f;
		c.R=rot_from_to(v3(c.mirror*k->axis[0],-k->axis[1],k->axis[2]),spec->dir);
		k->draw(&c);
		return 1;
	}
	return 0;
}

void bone_shape_free(BoneShape *shape){
	for(int i=0;i<shape->nparts;i++) free(shape->parts[i].prim.strand.s);
	free(shape->parts); free(shape->marks);
	memset(shape,0,sizeof(*shape));
}

const BoneLandmark *bone_shape_landmark(const BoneShape *shape,const char *name){
	for(int i=0;i<shape->nmarks;i++) if(!strcmp(shape->marks[i].name,name)) return &shape->marks[i];
	return NULL;
}

Mesh bone_part_mesh(const BonePart *part,int rings,int slices){
	const SkinPrim *p=&part->prim;
	if(p->kind==SKIN_PRIM_STRAND) return strand_mesh(&p->strand,slices);
	return gen_ellipsoid(p->center,p->ax,p->ay,p->az,p->radii,p->taper,rings,slices);
}
