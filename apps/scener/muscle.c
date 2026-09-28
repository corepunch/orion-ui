#include "simplegl.h"
#include <stdio.h>

/* Muscle and skin geometry. A strand is a world-space centerline whose samples
   carry an elliptical cross-section (half width across the body, half thickness
   out of it). A skin is the smooth union of strands and bone ellipsoids,
   polygonized with surface nets so muscles read as forms under one surface. */

#define SKIN_FAR 1.0f
#define SKIN_PAD_CELLS 3
#define SKIN_MAX_CELLS 12000000
#define SKIN_RELAX_LIMIT 0.5f
#define SKIN_EPSILON 0.0000001f
#define STRAND_EPSILON 0.000001f
#define STRAND_FRAME_EPSILON 0.2f
#define STRAND_VERTICAL 0.9f

/* Orthonormal section frame; out may be nearly parallel to the path after wrapping. */
static void strand_frame(vec3 dir,vec3 out,vec3 *n,vec3 *across){
	vec3 m=vsub(out,vscale(dir,vdot(out,dir)));
	if(vlen(m)<STRAND_FRAME_EPSILON) m=vcross(dir,fabsf(dir.z)<STRAND_VERTICAL?v3(0,0,1):v3(1,0,0));
	*n=vnorm(m); *across=vcross(dir,*n);
}

static float smin(float a,float b,float k){
	if(k<=0) return fminf(a,b);
	float h=fmaxf(k-fabsf(a-b),0)/k;
	return fminf(a,b)-h*h*k*0.25f;
}

float strand_volume(const Strand *st){
	float v=0;
	for(int i=0;i+1<st->n;i++){
		const StrandSample *a=&st->s[i],*b=&st->s[i+1];
		v+=M_PIf*0.5f*(a->w*a->t+b->w*b->t)*vlen(vsub(b->pos,a->pos));
	}
	return v;
}

/* Strand segments with their section frame and a bounding sphere, precomputed
   once per skin so the per-sample distance can skip far segments. */
typedef struct { vec3 a,dir,n,across,center; float len,w0,w1,t0,t1,bound; } strand_seg_t;

static strand_seg_t *strand_segments(const Strand *st){
	strand_seg_t *segs=malloc(sizeof(strand_seg_t)*(size_t)(st->n>1?st->n-1:1));
	for(int i=0;i+1<st->n;i++){
		const StrandSample *a=&st->s[i],*b=&st->s[i+1];
		strand_seg_t *g=&segs[i];
		vec3 d=vsub(b->pos,a->pos);
		g->len=vlen(d); g->a=a->pos; g->dir=g->len>STRAND_EPSILON?vscale(d,1.0f/g->len):v3(0,0,1);
		strand_frame(g->dir,vadd(a->out,b->out),&g->n,&g->across);
		g->w0=a->w; g->w1=b->w; g->t0=a->t; g->t1=b->t;
		g->center=vscale(vadd(a->pos,b->pos),0.5f);
		g->bound=g->len*0.5f+fmaxf(fmaxf(a->w,b->w),fmaxf(a->t,b->t));
	}
	return segs;
}

/* Signed distance estimate to a strand: nearest segment, elliptical section. */
static float strand_distance(const strand_seg_t *segs,int nsegs,vec3 p){
	float best=SKIN_FAR;
	for(int i=0;i<nsegs;i++){
		const strand_seg_t *g=&segs[i];
		float cx=p.x-g->center.x,cy=p.y-g->center.y,cz=p.z-g->center.z;
		if(sqrtf(cx*cx+cy*cy+cz*cz)-g->bound>best) continue;
		float qx=p.x-g->a.x,qy=p.y-g->a.y,qz=p.z-g->a.z;
		float along=qx*g->dir.x+qy*g->dir.y+qz*g->dir.z;
		float u=g->len>STRAND_EPSILON?fmaxf(0,fminf(1,along/g->len)):0;
		float w=g->w0+(g->w1-g->w0)*u,t=g->t0+(g->t1-g->t0)*u;
		float x=(qx*g->across.x+qy*g->across.y+qz*g->across.z)/w,y=(qx*g->n.x+qy*g->n.y+qz*g->n.z)/t;
		float dp=(sqrtf(x*x+y*y)-1)*fminf(w,t),beyond=along-u*g->len;
		if(fabsf(beyond)>STRAND_EPSILON) dp=sqrtf(fmaxf(dp,0)*fmaxf(dp,0)+beyond*beyond)+fminf(dp,0);
		if(dp<best) best=dp;
	}
	return best;
}

static float ellipsoid_distance(const SkinPrim *e,vec3 p){
	vec3 q=vsub(p,e->center);
	vec3 l=v3(vdot(q,e->ax),vdot(q,e->ay),vdot(q,e->az));
	float scale=1.0f+(e->taper-1.0f)*(fmaxf(-1,fminf(1,l.y/e->radii.y))+1.0f)*0.5f;
	vec3 r=v3(e->radii.x*scale,e->radii.y,e->radii.z*scale);
	vec3 a=v3(l.x/r.x,l.y/r.y,l.z/r.z),b=v3(l.x/(r.x*r.x),l.y/(r.y*r.y),l.z/(r.z*r.z));
	float k0=vlen(a),k1=vlen(b);
	return k1>SKIN_EPSILON?k0*(k0-1)/k1:-fminf(r.x,fminf(r.y,r.z));
}


static void prim_bounds(const SkinPrim *p,vec3 *lo,vec3 *hi){
	if(p->kind==SKIN_PRIM_STRAND){
		*lo=v3(INFINITY,INFINITY,INFINITY); *hi=v3(-INFINITY,-INFINITY,-INFINITY);
		for(int i=0;i<p->strand.n;i++){
			const StrandSample *s=&p->strand.s[i]; float r=fmaxf(s->w,s->t);
			lo->x=fminf(lo->x,s->pos.x-r); lo->y=fminf(lo->y,s->pos.y-r); lo->z=fminf(lo->z,s->pos.z-r);
			hi->x=fmaxf(hi->x,s->pos.x+r); hi->y=fmaxf(hi->y,s->pos.y+r); hi->z=fmaxf(hi->z,s->pos.z+r);
		}
		return;
	}
	float r=fmaxf(p->radii.x,fmaxf(p->radii.y,p->radii.z))*fmaxf(1,p->taper);
	*lo=vsub(p->center,v3(r,r,r)); *hi=vadd(p->center,v3(r,r,r));
}

typedef struct { float *f; int nx,ny,nz; vec3 origin; float h; } skin_grid_t;

static float grid_at(const skin_grid_t *g,int i,int j,int k){ return g->f[i+g->nx*(j+g->ny*k)]; }

static float grid_sample(const skin_grid_t *g,vec3 p){
	vec3 u=vscale(vsub(p,g->origin),1.0f/g->h);
	int i=(int)floorf(u.x),j=(int)floorf(u.y),k=(int)floorf(u.z);
	i=i<0?0:i>g->nx-2?g->nx-2:i; j=j<0?0:j>g->ny-2?g->ny-2:j; k=k<0?0:k>g->nz-2?g->nz-2:k;
	float fx=u.x-i,fy=u.y-j,fz=u.z-k,v=0;
	for(int c=0;c<8;c++){
		int dx=c&1,dy=(c>>1)&1,dz=(c>>2)&1;
		v+=grid_at(g,i+dx,j+dy,k+dz)*(dx?fx:1-fx)*(dy?fy:1-fy)*(dz?fz:1-fz);
	}
	return v;
}

static vec3 grid_gradient(const skin_grid_t *g,vec3 p){
	float e=g->h*0.5f;
	return v3(grid_sample(g,vadd(p,v3(e,0,0)))-grid_sample(g,vsub(p,v3(e,0,0))),
	          grid_sample(g,vadd(p,v3(0,e,0)))-grid_sample(g,vsub(p,v3(0,e,0))),
	          grid_sample(g,vadd(p,v3(0,0,e)))-grid_sample(g,vsub(p,v3(0,0,e))));
}

Mesh skin_surface(const SkinPrim *prims,int nprims,float cell,float fat){
	Mesh m={0};
	vec3 lo=v3(INFINITY,INFINITY,INFINITY),hi=v3(-INFINITY,-INFINITY,-INFINITY);
	for(int i=0;i<nprims;i++){
		vec3 a,b; prim_bounds(&prims[i],&a,&b);
		lo=v3(fminf(lo.x,a.x),fminf(lo.y,a.y),fminf(lo.z,a.z)); hi=v3(fmaxf(hi.x,b.x),fmaxf(hi.y,b.y),fmaxf(hi.z,b.z));
	}
	if(!nprims || cell<=0){ fprintf(stderr,"[skin] nothing to polygonize (prims=%d cell=%g)\n",nprims,cell); fflush(stderr); return m; }
	float pad=cell*SKIN_PAD_CELLS+fat;
	for(int i=0;i<nprims;i++) pad=fmaxf(pad,prims[i].blend+cell*SKIN_PAD_CELLS+fat);
	lo=vsub(lo,v3(pad,pad,pad)); hi=vadd(hi,v3(pad,pad,pad));
	skin_grid_t g={NULL,(int)ceilf((hi.x-lo.x)/cell)+1,(int)ceilf((hi.y-lo.y)/cell)+1,(int)ceilf((hi.z-lo.z)/cell)+1,lo,cell};
	if((double)g.nx*g.ny*g.nz>SKIN_MAX_CELLS){ fprintf(stderr,"[skin] grid %dx%dx%d exceeds %d cells; raise resolution\n",g.nx,g.ny,g.nz,SKIN_MAX_CELLS); fflush(stderr); return m; }
	size_t count=(size_t)g.nx*g.ny*g.nz;
	g.f=malloc(count*sizeof(float));
	for(size_t i=0;i<count;i++) g.f[i]=SKIN_FAR;
	/* Primitives only write the samples inside their own padded bounds. */
	for(int p=0;p<nprims;p++){
		vec3 a,b; prim_bounds(&prims[p],&a,&b);
		float r=prims[p].blend+cell*2;
		int i0=(int)floorf((a.x-r-lo.x)/cell),i1=(int)ceilf((b.x+r-lo.x)/cell);
		int j0=(int)floorf((a.y-r-lo.y)/cell),j1=(int)ceilf((b.y+r-lo.y)/cell);
		int k0=(int)floorf((a.z-r-lo.z)/cell),k1=(int)ceilf((b.z+r-lo.z)/cell);
		i0=i0<0?0:i0; j0=j0<0?0:j0; k0=k0<0?0:k0;
		i1=i1>=g.nx?g.nx-1:i1; j1=j1>=g.ny?g.ny-1:j1; k1=k1>=g.nz?g.nz-1:k1;
		int strand=prims[p].kind==SKIN_PRIM_STRAND,nsegs=strand?prims[p].strand.n-1:0;
		strand_seg_t *segs=strand?strand_segments(&prims[p].strand):NULL;
		for(int k=k0;k<=k1;k++) for(int j=j0;j<=j1;j++) for(int i=i0;i<=i1;i++){
			float *f=&g.f[i+g.nx*(j+g.ny*k)];
			vec3 x=v3(lo.x+i*cell,lo.y+j*cell,lo.z+k*cell);
			*f=smin(*f,strand?strand_distance(segs,nsegs,x):ellipsoid_distance(&prims[p],x),prims[p].blend);
		}
		free(segs);
	}
	for(size_t i=0;i<count;i++) g.f[i]-=fat;
	size_t cells=(size_t)(g.nx-1)*(g.ny-1)*(g.nz-1);
	int *vid=malloc(cells*sizeof(int));
	static const int edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
	for(int k=0;k<g.nz-1;k++) for(int j=0;j<g.ny-1;j++) for(int i=0;i<g.nx-1;i++){
		size_t c=i+(size_t)(g.nx-1)*(j+(size_t)(g.ny-1)*k);
		float v[8]; int inside=0;
		for(int n=0;n<8;n++){ v[n]=grid_at(&g,i+(n&1),j+((n>>1)&1),k+((n>>2)&1)); inside+=v[n]<0; }
		vid[c]=-1;
		if(!inside || inside==8) continue;
		vec3 sum=v3(0,0,0); int crossings=0;
		for(int e=0;e<12;e++){
			float a=v[edges[e][0]],b=v[edges[e][1]];
			if((a<0)==(b<0)) continue;
			float t=a/(a-b);
			int na=edges[e][0],nb=edges[e][1];
			vec3 pa=v3((float)(na&1),(float)((na>>1)&1),(float)((na>>2)&1)),pb=v3((float)(nb&1),(float)((nb>>1)&1),(float)((nb>>2)&1));
			sum=vadd(sum,lerp(pa,pb,t)); crossings++;
		}
		vec3 p=vadd(g.origin,vscale(vadd(v3((float)i,(float)j,(float)k),vscale(sum,1.0f/crossings)),cell));
		vec3 grad=grid_gradient(&g,p);
		float gl=vdot(grad,grad);
		/* One Newton step onto the zero set, limited so vertices stay in their cell. */
		if(gl>SKIN_EPSILON){
			vec3 step=vscale(grad,grid_sample(&g,p)*cell/gl);
			if(vlen(step)>cell*SKIN_RELAX_LIMIT) step=vscale(vnorm(step),cell*SKIN_RELAX_LIMIT);
			p=vsub(p,step); grad=grid_gradient(&g,p);
		}
		vid[c]=mesh_add_vert(&m,p,vnorm(grad));
	}
	#define CELL(a,b,c) vid[(a)+(size_t)(g.nx-1)*((b)+(size_t)(g.ny-1)*(c))]
	for(int k=0;k<g.nz;k++) for(int j=0;j<g.ny;j++) for(int i=0;i<g.nx;i++){
		float f0=grid_at(&g,i,j,k);
		for(int axis=0;axis<3;axis++){
			int i1=i+(axis==0),j1=j+(axis==1),k1=k+(axis==2);
			if(i1>=g.nx || j1>=g.ny || k1>=g.nz) continue;
			float f1=grid_at(&g,i1,j1,k1);
			if((f0<0)==(f1<0)) continue;
			int q[4];
			if(axis==0){ if(j<1||k<1||j>=g.ny-1||k>=g.nz-1) continue; q[0]=CELL(i,j-1,k-1); q[1]=CELL(i,j,k-1); q[2]=CELL(i,j,k); q[3]=CELL(i,j-1,k); }
			else if(axis==1){ if(i<1||k<1||i>=g.nx-1||k>=g.nz-1) continue; q[0]=CELL(i-1,j,k-1); q[1]=CELL(i-1,j,k); q[2]=CELL(i,j,k); q[3]=CELL(i,j,k-1); }
			else { if(i<1||j<1||i>=g.nx-1||j>=g.ny-1) continue; q[0]=CELL(i-1,j-1,k); q[1]=CELL(i,j-1,k); q[2]=CELL(i,j,k); q[3]=CELL(i-1,j,k); }
			if(q[0]<0||q[1]<0||q[2]<0||q[3]<0) continue;
			if(f0<0){ mesh_add_tri(&m,q[0],q[1],q[2]); mesh_add_tri(&m,q[0],q[2],q[3]); }
			else { mesh_add_tri(&m,q[0],q[2],q[1]); mesh_add_tri(&m,q[0],q[3],q[2]); }
		}
	}
	#undef CELL
	free(vid); free(g.f);
	return m;
}

/* A visible muscle: elliptical rings along the strand, capped at both tendons. */
Mesh strand_mesh(const Strand *st,int slices){
	Mesh m={0};
	if(st->n<2 || slices<3) return m;
	for(int i=0;i<st->n;i++){
		const StrandSample *s=&st->s[i];
		vec3 dir=vnorm(vsub(st->s[i<st->n-1?i+1:i].pos,st->s[i>0?i-1:i].pos));
		vec3 n,across;
		strand_frame(dir,s->out,&n,&across);
		for(int j=0;j<slices;j++){
			float a=2*M_PIf*j/slices,c=cosf(a),sn=sinf(a);
			vec3 p=vadd(s->pos,vadd(vscale(across,s->w*c),vscale(n,s->t*sn)));
			mesh_add_vert(&m,p,vnorm(vadd(vscale(across,c/s->w),vscale(n,sn/s->t))));
		}
	}
	for(int i=0;i+1<st->n;i++) for(int j=0;j<slices;j++){
		int a=i*slices+j,b=i*slices+(j+1)%slices,c=(i+1)*slices+(j+1)%slices,d=(i+1)*slices+j;
		mesh_add_tri(&m,a,d,c); mesh_add_tri(&m,a,c,b);
	}
	vec3 d0=vnorm(vsub(st->s[0].pos,st->s[1].pos)),d1=vnorm(vsub(st->s[st->n-1].pos,st->s[st->n-2].pos));
	int c0=mesh_add_vert(&m,st->s[0].pos,d0),c1=mesh_add_vert(&m,st->s[st->n-1].pos,d1),last=(st->n-1)*slices;
	for(int j=0;j<slices;j++){
		mesh_add_tri(&m,c0,j,(j+1)%slices);
		mesh_add_tri(&m,c1,last+(j+1)%slices,last+j);
	}
	return m;
}
