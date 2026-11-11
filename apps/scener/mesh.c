#include <stdint.h>
#include "simplegl.h"

#define PROFILE_EPSILON 0.000001f
#define PROFILE_MIN_POINTS 3
#define WINDOW_MIN_SEGMENTS 8
#define WINDOW_MAX_SEGMENTS 128
#define WINDOW_POINTED_RISE_SQUARED 3.0f
#define WINDOW_POINTED_ANGLE_DIVISOR 3.0f
#define ROUNDED_BOX_CORNER_COUNT 4
#define ROUNDED_BOX_MAX_CORNER_SEGMENTS 32
#define ROUNDED_BOX_MAX_BEVEL_SEGMENTS 8
#define ROUNDED_BOX_POINT_EPSILON 1e-6f
#define EDGE_HASH_A 0x9e3779b1u
#define NOISE_HASH_SEED 0x27d4eb2fu
#define NOISE_HASH_MIX 0x165667b1u
#define NOISE_PRIME_X 73856093u
#define NOISE_PRIME_Y 19349663u
#define NOISE_PRIME_Z 83492791u
#define NOISE_CHANNEL_SHIFT 31.7f   /* x, y and z displacements read the field far apart, so they are independent */
#define NOISE_LACUNARITY 2.0f
#define NOISE_MAX_ITERATIONS 10
#define NOISE_MIN_SCALE 1e-4f
#define DEFORM_MIN_EXTENT 1e-6f
#define DEFORM_MIN_ANGLE 1e-6f
#define DEFORM_BIAS_MIN 0.01f
#define EDGE_HASH_B 0x85ebca77u
#define EDGE_HASH_C 0xc2b2ae3du
#define MESH_EDGE_WELD_MAX 1e-4f
#define MESH_EDGE_WELD_MIN_LENGTH 1e-8f
#define MESH_EDGE_WELD_FRACTION 0.25f
#define ELLIPSOID_MIN_TAPER 0.05f
#define PROFILE_MIN_ELLIPSE_SEGMENTS 8
#define PROFILE_MAX_ELLIPSE_SEGMENTS 128
#define PROFILE_MIN_STAR_POINTS 3
#define PROFILE_MAX_STAR_POINTS 32
#define PROFILE_OFFSET_PARALLEL_EPSILON 1e-6f
#define PROFILE_TRIANGULATION_RATIO 1e-8f
#define PROFILE_TRIANGULATION_MIN_EPSILON 1e-16f
#define PROFILE_FILLET_MAX_SEGMENTS 16
#define PROFILE_FILLET_STRAIGHT_COS 0.9999f
#define PROFILE_STROKE_MAX_MITER 4.0f

/* Meshes are freed where no GL context may be current (loading, --check), so the vertex cache only
   learns of it here and releases the range on its next frame. */
static int *s_retired; static int s_nretired,s_cretired;
int mesh_gpu_take_retired(int **chunks){
    int n=s_nretired;
    *chunks=malloc(sizeof(int)*(size_t)(n+1));
    if(n) memcpy(*chunks,s_retired,sizeof(int)*(size_t)n);
    s_nretired=0;
    return n;
}

void mesh_free(Mesh *m){
    if(m->gpuChunk) DA_PUSH(s_retired,s_nretired,s_cretired,m->gpuChunk);
    free(m->verts); free(m->tris); free(m->edges); free(m->triN);
    memset(m,0,sizeof(*m));
}
int mesh_add_vert(Mesh *m, vec3 p, vec3 n){
    Vertex v={0}; v.pos=p; v.nrm=n; DA_PUSH(m->verts,m->nverts,m->cverts,v); return m->nverts-1;
}
void mesh_add_tri(Mesh *m,int a,int b,int c){
    Tri t={a,b,c}; DA_PUSH(m->tris,m->ntris,m->ctris,t);
}
gradient_t gradient_default(void){
	return (gradient_t){.type=GRADIENT_BOX,.axis='y',.from={1,1,1},.to={1,1,1},.tileV=1.0f,.power=1.0f};
}

void mesh_gradient_param(Mesh *m, const gradient_t *g){
	if(m->nverts<=0) return;
	char axis=g->axis;
	float lo=1e30f, hi=-1e30f;
	for(int i=0;g->type==GRADIENT_BOX && i<m->nverts;i++){
		vec3 p=m->verts[i].pos;
		float c=axis=='x'?p.x:axis=='z'?p.z:p.y;
		if(c<lo) lo=c;
		if(c>hi) hi=c;
	}
	float span=hi-lo;
	vec3 d=vsub(g->end,g->start); float len2=vdot(d,d);
	for(int i=0;i<m->nverts;i++){
		Vertex *v=&m->verts[i]; vec3 p=v->pos; float c;
		switch(g->type){
			case GRADIENT_LINE:   c=len2>0?vdot(vsub(p,g->start),d)/len2:0; break;
			case GRADIENT_RADIAL: c=0; break;   /* the distance is taken per fragment (shader.c); g keeps the offset */
			case GRADIENT_UV:     c=v->u*g->tileU+v->v*g->tileV; break;
			default: { float a=axis=='x'?p.x:axis=='z'?p.z:p.y; c=span>1e-6f?(a-lo)/span:0.0f; }
		}
		v->g=c+g->offset;
	}
}

float gradient_amount(const gradient_t *g, float coord){
	float t=g->repeat?coord-floorf(coord):fminf(fmaxf(coord,0.0f),1.0f);
	if(g->mirror) t=1.0f-fabsf(2.0f*t-1.0f);   /* ping-pong, as 3ds Max's mirror tiling: 0 at both ends, 1 in the middle */
	if(g->power>0 && g->power!=1.0f) t=powf(t,g->power);
	if(g->edge>0) t=t<g->edge?0.0f:1.0f;
	return t;
}

/* Positions take posM. Normals take its inverse transpose, so a non-uniform scale (a flattened sphere, a
   stretched lathe) tilts them with the surface; rotM, the rotation alone, is the fallback for a singular posM. */
void mesh_transform(Mesh *m, mat4 posM, mat4 rotM){
    float det=posM.m[0]*(posM.m[5]*posM.m[10]-posM.m[9]*posM.m[6])-posM.m[4]*(posM.m[1]*posM.m[10]-posM.m[9]*posM.m[2])+posM.m[8]*(posM.m[1]*posM.m[6]-posM.m[5]*posM.m[2]);
    int exact=fabsf(det)>1e-10f;
    mat4 inv=exact?mat4_affine_inverse(posM):mat4_identity();
    for(int i=0;i<m->nverts;i++){
        vec3 n=m->verts[i].nrm;
        m->verts[i].pos = mat4_xform_point(posM, m->verts[i].pos);
        m->verts[i].nrm = exact?vnorm(v3(inv.m[0]*n.x+inv.m[1]*n.y+inv.m[2]*n.z,
                                         inv.m[4]*n.x+inv.m[5]*n.y+inv.m[6]*n.z,
                                         inv.m[8]*n.x+inv.m[9]*n.y+inv.m[10]*n.z))
                                : vnorm(mat4_xform_dir(rotM, n));
    }
}
void mesh_compute_face_normals(Mesh *m){
    /* always sized for the triangles there are now: a modifier may have added some since the last time */
    m->triN = realloc(m->triN, sizeof(vec3)*(size_t)(m->ntris?m->ntris:1));
    for(int i=0;i<m->ntris;i++){
        Tri t=m->tris[i];
        vec3 a=m->verts[t.a].pos, b=m->verts[t.b].pos, c=m->verts[t.c].pos;
        m->triN[i] = vnorm(vcross(vsub(b,a),vsub(c,a)));
    }
}
/* Welding and edge pairing use hashes so dense generated meshes (skins) stay
   linear; the results match the original first-match scans exactly. */
static uint32_t edge_hash(uint32_t a,uint32_t b){ return (a*EDGE_HASH_A)^(b*EDGE_HASH_B); }

static int64_t weld_cell(float v,float size){ return (int64_t)floorf(v/size); }

void mesh_build_edges(Mesh *m){
    float shortest=INFINITY;
    for(int i=0;i<m->ntris;i++){
        Tri t=m->tris[i];
        int ids[3]={t.a,t.b,t.c};
        for(int e=0;e<3;e++){
            float length=vlen(vsub(m->verts[ids[e]].pos,m->verts[ids[(e+1)%3]].pos));
            if(length>MESH_EDGE_WELD_MIN_LENGTH && length<shortest) shortest=length;
        }
    }
    float threshold=fminf(MESH_EDGE_WELD_MAX,shortest*MESH_EDGE_WELD_FRACTION);
    int *weld=malloc(sizeof(int)*(size_t)(m->nverts?m->nverts:1));
    size_t buckets=1; while(buckets<(size_t)m->nverts*2+1) buckets<<=1;
    int *head=malloc(sizeof(int)*buckets),*next=malloc(sizeof(int)*(size_t)(m->nverts?m->nverts:1));
    for(size_t i=0;i<buckets;i++) head[i]=-1;
    float cell=threshold>0?threshold:MESH_EDGE_WELD_MAX;
    for(int i=0;i<m->nverts;i++){
        vec3 p=m->verts[i].pos;
        int64_t cx=weld_cell(p.x,cell),cy=weld_cell(p.y,cell),cz=weld_cell(p.z,cell);
        int best=-1;
        for(int dz=-1;dz<=1;dz++) for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){
            uint32_t h=edge_hash((uint32_t)(cx+dx)^((uint32_t)(cz+dz)*EDGE_HASH_C),(uint32_t)(cy+dy))&(uint32_t)(buckets-1);
            for(int j=head[h];j>=0;j=next[j])
                if((best<0 || j<best) && vlen(vsub(p,m->verts[j].pos))<threshold) best=j;
        }
        weld[i]=best>=0?weld[best]:i;
        uint32_t h=edge_hash((uint32_t)cx^((uint32_t)cz*EDGE_HASH_C),(uint32_t)cy)&(uint32_t)(buckets-1);
        next[i]=head[h]; head[h]=i;
    }
    free(head); free(next);
    free(m->edges); m->edges=NULL; m->nedges=m->cedges=0;
    size_t slots=1; while(slots<(size_t)m->ntris*6+1) slots<<=1;
    /* Open edges by welded (start,end), each bucket a FIFO so the oldest match wins. */
    int *first=malloc(sizeof(int)*slots),*last=malloc(sizeof(int)*slots),*chain=malloc(sizeof(int)*(size_t)(m->ntris*3+1));
    for(size_t i=0;i<slots;i++) first[i]=last[i]=-1;
    for(int i=0;i<m->ntris;i++){
        Tri t=m->tris[i];
        int pairs[3][2]={{t.a,t.b},{t.b,t.c},{t.c,t.a}};
        for(int e=0;e<3;e++){
            int v0=pairs[e][0], v1=pairs[e][1];
            int w0=weld[v0], w1=weld[v1];
            uint32_t h=edge_hash((uint32_t)w1,(uint32_t)w0)&(uint32_t)(slots-1);
            int found=-1,prev=-1;
            for(int k=first[h];k>=0;prev=k,k=chain[k]){
                Edge *ed=&m->edges[k];
                if(weld[ed->v0]==w1 && weld[ed->v1]==w0){ found=k; break; }
            }
            if(found>=0){
                m->edges[found].t1=i;
                if(prev<0) first[h]=chain[found]; else chain[prev]=chain[found];
                if(last[h]==found) last[h]=prev;
            } else {
                Edge ne={ i, -1, v0, v1 };
                DA_PUSH(m->edges,m->nedges,m->cedges,ne);
                int k=m->nedges-1;
                uint32_t own=edge_hash((uint32_t)w0,(uint32_t)w1)&(uint32_t)(slots-1);
                chain[k]=-1;
                if(last[own]>=0) chain[last[own]]=k; else first[own]=k;
                last[own]=k;
            }
        }
    }
    free(first); free(last); free(chain);
    free(weld);
}
void mesh_shrink(Mesh *m){
    if(m->nverts && m->cverts>m->nverts){ m->verts=realloc(m->verts,sizeof(Vertex)*(size_t)m->nverts); m->cverts=m->nverts; }
    if(m->ntris && m->ctris>m->ntris){ m->tris=realloc(m->tris,sizeof(Tri)*(size_t)m->ntris); m->ctris=m->ntris; }
    if(m->nedges && m->cedges>m->nedges){ m->edges=realloc(m->edges,sizeof(Edge)*(size_t)m->nedges); m->cedges=m->nedges; }
}

/* Tetrahedra are taken against the first vertex: against the origin, a small mesh far from it
   sums to rounding noise, and a wrong sign turns it inside out when the winding is corrected. */
float mesh_signed_volume(Mesh *m){
    if(!m->nverts) return 0.0f;
    vec3 o=m->verts[0].pos; double vol=0;
    for(int i=0;i<m->ntris;i++){
        Tri t=m->tris[i];
        vec3 a=vsub(m->verts[t.a].pos,o), b=vsub(m->verts[t.b].pos,o), c=vsub(m->verts[t.c].pos,o);
        vol += vdot(vcross(a,b),c);
    }
    return (float)(vol / 6.0);
}
void mesh_flip_winding(Mesh *m){
    for(int i=0;i<m->ntris;i++){ int t=m->tris[i].b; m->tris[i].b=m->tris[i].c; m->tris[i].c=t; }
    for(int i=0;i<m->nedges;i++){ int v=m->edges[i].v0; m->edges[i].v0=m->edges[i].v1; m->edges[i].v1=v; }
}

/* ------------------------------------------------------- primitive gens */
static void add_quad(Mesh *m, vec3 a,vec3 b,vec3 c,vec3 d, vec3 n){
    int ia=mesh_add_vert(m,a,n), ib=mesh_add_vert(m,b,n),
        ic=mesh_add_vert(m,c,n), id=mesh_add_vert(m,d,n);
    mesh_add_tri(m,ia,ic,ib); mesh_add_tri(m,ia,id,ic);
}
static void extrude_polygon(Mesh *m,vec3 *pts,vec3 *side_normals,int n,float depth,int caps,int sides,int flip,int smooth);

static vec3* mirror_profile_x(vec3 *pts,int n){
	vec3 *r=malloc(sizeof(vec3)*(size_t)n);
	for(int i=0;i<n;i++) r[i]=v3(-pts[n-1-i].x,pts[n-1-i].y,0);
	return r;
}

static vec3* mirror_profile_y(vec3 *pts,int n){
	vec3 *r=malloc(sizeof(vec3)*(size_t)n);
	for(int i=0;i<n;i++) r[i]=v3(pts[n-1-i].x,-pts[n-1-i].y,0);
	return r;
}

static void mesh_append(Mesh *dst, Mesh src){
	int base=dst->nverts;
	for(int i=0;i<src.nverts;i++){
		int v=mesh_add_vert(dst,src.verts[i].pos,src.verts[i].nrm);
		dst->verts[v].u=src.verts[i].u; dst->verts[v].v=src.verts[i].v;
	}
	for(int i=0;i<src.ntris;i++) mesh_add_tri(dst,src.tris[i].a+base,src.tris[i].b+base,src.tris[i].c+base);
	mesh_free(&src);
}

static void mesh_append_xform(Mesh *dst, Mesh src, mat4 M, mat4 R){
	mesh_transform(&src,M,R);
	mesh_append(dst,src);
}

Mesh gen_box(float sx,float sy,float sz){
	Mesh m={0};
	float x=sx*0.5f,y=sy*0.5f;
	vec3 p[4]={v3(-x,-y,0),v3(x,-y,0),v3(x,y,0),v3(-x,y,0)};
	extrude_polygon(&m,p,NULL,4,sz,1,1,0,0);
	return m;
}

typedef struct { vec3 center,dir; } rounded_box_point_t;

static int rounded_box_outline(float sx,float sy,float radius,int segments,rounded_box_point_t *outline){
	int count=0;
	for(int corner=0;corner<ROUNDED_BOX_CORNER_COUNT;corner++){
		vec3 center=v3((corner<2?1.0f:-1.0f)*(sx*0.5f-radius),
			(corner==0||corner==ROUNDED_BOX_CORNER_COUNT-1?-1.0f:1.0f)*(sy*0.5f-radius),0);
		float start=((float)corner-1.0f)*M_PIf*0.5f;
		for(int step=0;step<=segments;step++){
			float angle=start+(float)step/(float)segments*M_PIf*0.5f;
			vec3 dir=v3(cosf(angle),sinf(angle),0),point=vadd(center,vscale(dir,radius));
			if(count){
				rounded_box_point_t prev=outline[count-1];
				if(vlen(vsub(point,vadd(prev.center,vscale(prev.dir,radius))))<=ROUNDED_BOX_POINT_EPSILON) continue;
			}
			outline[count++]=(rounded_box_point_t){center,dir};
		}
	}
	if(count>1){
		vec3 first=vadd(outline[0].center,vscale(outline[0].dir,radius));
		vec3 last=vadd(outline[count-1].center,vscale(outline[count-1].dir,radius));
		if(vlen(vsub(first,last))<=ROUNDED_BOX_POINT_EPSILON) count--;
	}
	return count;
}

Shape2D shape2d_rect(float width,float height){
	return shape2d_window(WINDOW_RECTANGLE,width,height,WINDOW_MIN_SEGMENTS);
}

Shape2D shape2d_rounded_rect(float width,float height,float radius,int segments){
	Shape2D profile={0}; profile.closed=1;
	if(!isfinite(width)||!isfinite(height)||!isfinite(radius)||width<=0||height<=0||radius<0) return profile;
	if(radius==0) return shape2d_rect(width,height);
	radius=fminf(radius,fminf(width,height)*0.5f);
	if(segments<2) segments=2;
	if(segments>ROUNDED_BOX_MAX_CORNER_SEGMENTS) segments=ROUNDED_BOX_MAX_CORNER_SEGMENTS;
	rounded_box_point_t outline[ROUNDED_BOX_CORNER_COUNT*(ROUNDED_BOX_MAX_CORNER_SEGMENTS+1)];
	int count=rounded_box_outline(width,height,radius,segments,outline);
	for(int i=0;i<count;i++){
		vec3 point=vadd(outline[i].center,vscale(outline[i].dir,radius));
		DA_PUSH(profile.pts,profile.npts,profile.cpts,point);
	}
	return profile;
}

Shape2D shape2d_ellipse(float radius_x,float radius_y,int segments){
	Shape2D profile={0}; profile.closed=1;
	if(!isfinite(radius_x)||!isfinite(radius_y)||radius_x<=0||radius_y<=0) return profile;
	if(segments<PROFILE_MIN_ELLIPSE_SEGMENTS) segments=PROFILE_MIN_ELLIPSE_SEGMENTS;
	if(segments>PROFILE_MAX_ELLIPSE_SEGMENTS) segments=PROFILE_MAX_ELLIPSE_SEGMENTS;
	for(int i=0;i<segments;i++){
		float angle=(float)i/(float)segments*2.0f*M_PIf;
		DA_PUSH(profile.pts,profile.npts,profile.cpts,v3(cosf(angle)*radius_x,sinf(angle)*radius_y,0));
	}
	return profile;
}

Shape2D shape2d_star(float outer_radius,float inner_radius,int points){
	Shape2D profile={0}; profile.closed=1;
	if(!isfinite(outer_radius)||!isfinite(inner_radius)||outer_radius<=0||inner_radius<=0||inner_radius>=outer_radius) return profile;
	if(points<PROFILE_MIN_STAR_POINTS||points>PROFILE_MAX_STAR_POINTS) return profile;
	for(int i=0;i<points*2;i++){
		float angle=(float)i/(float)(points*2)*2.0f*M_PIf-M_PIf*0.5f;
		float radius=i%2?inner_radius:outer_radius;
		DA_PUSH(profile.pts,profile.npts,profile.cpts,v3(cosf(angle)*radius,sinf(angle)*radius,0));
	}
	return profile;
}

Mesh gen_rounded_box(float sx,float sy,float sz,float radius,int segments){
	Shape2D profile=shape2d_rounded_rect(sx,sy,radius,segments);
	Mesh mesh=gen_profile_extrusion_beveled(&profile,sz,0,1);
	shape2d_free(&profile);
	return mesh;
}

Mesh gen_box_inset(float sx,float sy,float sz,float insetX,float insetY){
	Mesh m={0};
	float halfX=sx*0.5f, halfY=sy*0.5f;
	float innerX=halfX-insetX, innerY=halfY-insetY;
	if(insetX<=1e-6f || insetY<=1e-6f || innerX<=1e-6f || innerY<=1e-6f) return gen_box(sx,sy,sz);
	mesh_append_xform(&m,gen_box(insetX*2.0f,sy,sz),mat4_translate(v3(-halfX+insetX,0,0)),mat4_identity());
	mesh_append_xform(&m,gen_box(insetX*2.0f,sy,sz),mat4_translate(v3(halfX-insetX,0,0)),mat4_identity());
	mesh_append_xform(&m,gen_box(innerX*2.0f,insetY*2.0f,sz),mat4_translate(v3(0,halfY-insetY,0)),mat4_identity());
	mesh_append_xform(&m,gen_box(innerX*2.0f,insetY*2.0f,sz),mat4_translate(v3(0,-halfY+insetY,0)),mat4_identity());
	return m;
}

Mesh gen_cylinder_like(int sides,float rBot,float rTop,float height,int smooth){
    Mesh m={0}; if(sides<3) sides=3;
    float hy=height*0.5f;
    for(int i=0;i<sides;i++){
        float a0=(float)i/sides*2.0f*M_PIf, a1=(float)(i+1)/sides*2.0f*M_PIf;
        vec3 b0=v3(cosf(a0)*rBot,-hy,sinf(a0)*rBot), b1=v3(cosf(a1)*rBot,-hy,sinf(a1)*rBot);
        vec3 t0=v3(cosf(a0)*rTop, hy,sinf(a0)*rTop), t1=v3(cosf(a1)*rTop, hy,sinf(a1)*rTop);
        vec3 flatN=vnorm(vcross(vsub(t0,b0),vsub(b1,b0)));
        if(smooth){
            /* The side normal leans up by the slope, (rBot-rTop)/height; a pointed apex takes the
               segment's mid-angle normal so neighbouring facets do not pinch to a dark tip. */
            float am=(a0+a1)*0.5f, lean=rBot-rTop;
            vec3 n0=vnorm(v3(cosf(a0)*height,lean,sinf(a0)*height)), n1=vnorm(v3(cosf(a1)*height,lean,sinf(a1)*height));
            vec3 nm=vnorm(v3(cosf(am)*height,lean,sinf(am)*height));
            int ib0=mesh_add_vert(&m,b0,n0), ib1=mesh_add_vert(&m,b1,n1);
            int it0=mesh_add_vert(&m,t0,rTop>1e-6f?n0:nm), it1=mesh_add_vert(&m,t1,rTop>1e-6f?n1:nm);
            if(rBot>1e-6f) mesh_add_tri(&m,ib0,it1,ib1);
            if(rTop>1e-6f || rBot>1e-6f) mesh_add_tri(&m,ib0,it0,it1);
        } else {
            add_quad(&m,b0,b1,t1,t0,flatN);
        }
    }
    if(rBot>1e-6f){
        vec3 center=v3(0,-hy,0), n=v3(0,-1,0);
        for(int i=0;i<sides;i++){
            float a0=(float)i/sides*2.0f*M_PIf, a1=(float)(i+1)/sides*2.0f*M_PIf;
            vec3 b0=v3(cosf(a0)*rBot,-hy,sinf(a0)*rBot), b1=v3(cosf(a1)*rBot,-hy,sinf(a1)*rBot);
            int ic=mesh_add_vert(&m,center,n), i0=mesh_add_vert(&m,b0,n), i1=mesh_add_vert(&m,b1,n);
            mesh_add_tri(&m,ic,i0,i1);
        }
    }
    if(rTop>1e-6f){
        vec3 center=v3(0,hy,0), n=v3(0,1,0);
        for(int i=0;i<sides;i++){
            float a0=(float)i/sides*2.0f*M_PIf, a1=(float)(i+1)/sides*2.0f*M_PIf;
            vec3 t0=v3(cosf(a0)*rTop,hy,sinf(a0)*rTop), t1=v3(cosf(a1)*rTop,hy,sinf(a1)*rTop);
            int ic=mesh_add_vert(&m,center,n), i0=mesh_add_vert(&m,t1,n), i1=mesh_add_vert(&m,t0,n);
            mesh_add_tri(&m,ic,i0,i1);
        }
    }
    return m;
}
Mesh gen_cylinder(float r,float h,int sides){
	Mesh m={0};
	if(sides<3) sides=24;
	vec3 *p=malloc(sizeof(vec3)*(size_t)sides);
	for(int i=0;i<sides;i++){
		float a=(float)i/(float)sides*2.0f*M_PIf;
		p[i]=v3(cosf(a)*r,sinf(a)*r,0);
	}
	extrude_polygon(&m,p,NULL,sides,h,1,1,0,1);
	mesh_transform(&m,mat4_rot_x(-90.0f),mat4_rot_x(-90.0f));
	free(p);
	return m;
}
Mesh gen_cylinder_tube(float r,float h,float wall,int sides){
	Mesh m={0};
	if(sides<8) sides=24;
	if(wall<=1e-6f || wall>=r-1e-6f) return gen_cylinder(r,h,sides);
	sides=(sides+3)/4*4;
	int quarter=sides/4,n=quarter*2+2;
	float inner=r-wall;
	vec3 *outer=malloc(sizeof(vec3)*(size_t)sides);
	vec3 *innerPts=malloc(sizeof(vec3)*(size_t)sides);
	for(int i=0;i<sides;i++){
		float a=(float)i/(float)sides*2.0f*M_PIf;
		outer[i]=v3(cosf(a)*r,sinf(a)*r,0);
		innerPts[i]=v3(cosf(a)*inner,sinf(a)*inner,0);
	}
	vec3 *q=malloc(sizeof(vec3)*(size_t)n);
	for(int i=0;i<=quarter;i++){
		float a=(float)i/(float)quarter*M_PIf*0.5f;
		q[i]=v3(cosf(a)*r,sinf(a)*r,0);
		q[quarter+1+i]=v3(sinf(a)*inner,cosf(a)*inner,0);
	}
	vec3 *qx=mirror_profile_x(q,n);
	vec3 *qy=mirror_profile_y(q,n);
	vec3 *qxy=mirror_profile_y(qx,n);
	extrude_polygon(&m,q,NULL,n,h,1,0,0,0);
	extrude_polygon(&m,qx,NULL,n,h,1,0,0,0);
	extrude_polygon(&m,qy,NULL,n,h,1,0,0,0);
	extrude_polygon(&m,qxy,NULL,n,h,1,0,0,0);
	extrude_polygon(&m,outer,NULL,sides,h,0,1,0,1);
	extrude_polygon(&m,innerPts,NULL,sides,h,0,1,1,1);
	mesh_transform(&m,mat4_rot_x(-90.0f),mat4_rot_x(-90.0f));
	free(outer); free(innerPts); free(q); free(qx); free(qy); free(qxy);
	return m;
}
Mesh gen_prism(float r,float h,int sides){ return gen_cylinder_like(sides<3?6:sides,r,r,h,0); }
Mesh gen_cone(float rBase,float rTop,float h,int sides,int smooth){
    return gen_cylinder_like(sides<3?4:sides, rBase, rTop, h, smooth);
}

Mesh gen_sphere(float r,int rings,int slices){
    Mesh m={0}; if(rings<3) rings=12; if(slices<3) slices=16;
    for(int i=0;i<=rings;i++){
        float v=(float)i/rings, phi=v*M_PIf;
        for(int j=0;j<=slices;j++){
            float u=(float)j/slices, th=u*2.0f*M_PIf;
            vec3 n=v3(sinf(phi)*cosf(th), cosf(phi), sinf(phi)*sinf(th));
            mesh_add_vert(&m, vscale(n,r), n);
        }
    }
    int stride=slices+1;
    for(int i=0;i<rings;i++) for(int j=0;j<slices;j++){
        int a=i*stride+j, b=a+1, c=(i+1)*stride+j, d=c+1;
        mesh_add_tri(&m,a,b,d); mesh_add_tri(&m,a,d,c);
    }
    return m;
}

Mesh gen_ellipsoid(vec3 center,vec3 axisX,vec3 axisY,vec3 axisZ,vec3 radii,float taper,int rings,int slices){
    Mesh m={0}; if(rings<3) rings=12; if(slices<3) slices=16;
    if(taper<ELLIPSOID_MIN_TAPER) taper=ELLIPSOID_MIN_TAPER;
    for(int i=0;i<=rings;i++){
        float v=(float)i/rings, phi=v*M_PIf;
        for(int j=0;j<=slices;j++){
            float u=(float)j/slices, th=u*2.0f*M_PIf;
            vec3 n=v3(sinf(phi)*cosf(th), cosf(phi), sinf(phi)*sinf(th));
            /* Taper scales the cross-section linearly from 1 at -Y to taper at +Y. */
            float scale=1.0f+(taper-1.0f)*(n.y+1.0f)*0.5f, slope=(taper-1.0f)/(2.0f*radii.y);
            vec3 p=vadd(center,vadd(vscale(axisX,radii.x*scale*n.x),vadd(vscale(axisY,radii.y*n.y),vscale(axisZ,radii.z*scale*n.z))));
            /* The gradient of the implicit surface keeps shading correct for unequal radii. */
            float gy=n.y/radii.y-(n.x*n.x+n.z*n.z)*slope/scale;
            vec3 g=vadd(vscale(axisX,n.x/(radii.x*scale)),vadd(vscale(axisY,gy),vscale(axisZ,n.z/(radii.z*scale))));
            mesh_add_vert(&m, p, vnorm(g));
        }
    }
    int stride=slices+1;
    for(int i=0;i<rings;i++) for(int j=0;j<slices;j++){
        int a=i*stride+j, b=a+1, c=(i+1)*stride+j, d=c+1;
        mesh_add_tri(&m,a,b,d); mesh_add_tri(&m,a,d,c);
    }
    return m;
}

Mesh gen_torus(float R,float r,int majorSeg,int minorSeg){
	if(majorSeg<3) majorSeg=24; if(minorSeg<3) minorSeg=12;
	LoftPath path={0};
	for(int i=0;i<majorSeg;i++){
		float u=(float)i/majorSeg*2.0f*M_PIf;
		vec3 p=v3(R*cosf(u),0,R*sinf(u));
		DA_PUSH(path.pts,path.npts,path.cpts,p);
	}
	Shape2D cross={0};
	for(int j=0;j<minorSeg;j++){
		float v=(float)j/minorSeg*2.0f*M_PIf;
		vec3 p=v3(cosf(v)*r,sinf(v)*r,0);
		DA_PUSH(cross.pts,cross.npts,cross.cpts,p);
	}
	cross.closed=1;
	shape2d_compute_normals(&cross);
	Mesh m=gen_loft(&path,&cross,1);
	free(path.pts);
	shape2d_free(&cross);
	return m;
}

Mesh gen_capsule(float r,float h,int rings,int slices){
	if(rings<2) rings=6; if(slices<3) slices=16;
	int halfRings=rings/2;
	if(halfRings<1) halfRings=1;

	Mesh top=gen_sphere(r,halfRings,slices);
	Mesh bot=gen_sphere(r,halfRings,slices);
	mesh_transform(&top,mat4_translate(v3(0,h*0.5f,0)),mat4_identity());
	mesh_transform(&bot,mat4_translate(v3(0,-h*0.5f,0)),mat4_identity());

	Mesh cyl=gen_cylinder(r,h,slices);
	Mesh m={0};
	mesh_append(&m,cyl);
	mesh_append(&m,top);
	mesh_append(&m,bot);
	return m;
}

static vec3* arch_profile(float r,float spring,float bottom,float splitX,float splitY,int segments,int *out_n){
	int half=segments/2,n=0;
	vec3 *p=malloc(sizeof(vec3)*(size_t)(segments+10));
	p[n++]=v3(-r,bottom,0);
	if(splitX>1e-6f && splitX<r-1e-6f) p[n++]=v3(-splitX,bottom,0);
	if(splitX>1e-6f && splitX<r-1e-6f) p[n++]=v3(splitX,bottom,0);
	p[n++]=v3(r,bottom,0);
	if(splitY>bottom+1e-6f && splitY<spring-1e-6f) p[n++]=v3(r,splitY,0);
	p[n++]=v3(r,spring,0);
	for(int i=1;i<=half;i++){
		float a=(float)i/(float)half*M_PIf*0.5f;
		p[n++]=v3(cosf(a)*r,spring+sinf(a)*r,0);
	}
	for(int i=half-1;i>=0;i--){
		float a=(float)i/(float)half*M_PIf*0.5f;
		p[n++]=v3(-cosf(a)*r,spring+sinf(a)*r,0);
	}
	if(splitY>bottom+1e-6f && splitY<spring-1e-6f) p[n++]=v3(-r,splitY,0);
	*out_n=n;
	return p;
}

static void extrude_rect_caps(Mesh *m,float x0,float y0,float x1,float y1,float depth){
	vec3 p[4]={v3(x0,y0,0),v3(x1,y0,0),v3(x1,y1,0),v3(x0,y1,0)};
	extrude_polygon(m,p,NULL,4,depth,1,0,0,0);
}

Mesh gen_arch(float width,float height,float depth,float wall,int segments,float inset){
	Mesh m={0};
	if(segments<6) segments=16;
	if(segments%2) segments++;
	float outer=width*0.5f,halfH=height*0.5f,bottom=-halfH;
	float spring=halfH-outer,stem=height-outer;
	if(outer<=1e-6f || stem<1e-6f || depth<=1e-6f) return gen_box(width,height,depth);
	if(inset<0) inset=0;
	if(inset>depth-1e-4f) inset=depth-1e-4f;
	float extrudedDepth=depth-inset;
	int frame=wall>1e-6f && wall<outer-1e-6f;
	if(!frame){
		int n;
		vec3 *profile=arch_profile(outer,spring,bottom,0,bottom,segments,&n);
		extrude_polygon(&m,profile,NULL,n,extrudedDepth,1,1,0,0);
		free(profile);
	} else {
		float inner=outer-wall,sill=bottom+wall;
		int no,ni,half=segments/2;
		vec3 *outerProfile=arch_profile(outer,spring,bottom,inner,sill,segments,&no);
		vec3 *innerProfile=arch_profile(inner,spring,sill,0,sill,segments,&ni);
		extrude_polygon(&m,outerProfile,NULL,no,extrudedDepth,0,1,0,0);
		extrude_polygon(&m,innerProfile,NULL,ni,extrudedDepth,0,1,1,0);
		int nq=(half+1)*2;
		vec3 *q=malloc(sizeof(vec3)*(size_t)nq);
		for(int i=0;i<=half;i++){
			float a=(float)i/(float)half*M_PIf*0.5f;
			q[i]=v3(cosf(a)*outer,spring+sinf(a)*outer,0);
			q[half+1+i]=v3(sinf(a)*inner,spring+cosf(a)*inner,0);
		}
		vec3 *ql=mirror_profile_x(q,nq);
		extrude_polygon(&m,q,NULL,nq,extrudedDepth,1,0,0,0);
		extrude_polygon(&m,ql,NULL,nq,extrudedDepth,1,0,0,0);
		extrude_rect_caps(&m,-outer,bottom,-inner,sill,extrudedDepth);
		extrude_rect_caps(&m,-inner,bottom,inner,sill,extrudedDepth);
		extrude_rect_caps(&m,inner,bottom,outer,sill,extrudedDepth);
		extrude_rect_caps(&m,-outer,sill,-inner,spring,extrudedDepth);
		extrude_rect_caps(&m,inner,sill,outer,spring,extrudedDepth);
		free(outerProfile); free(innerProfile); free(q); free(ql);
	}
	if(inset>0) mesh_transform(&m,mat4_translate(v3(0,0,-inset*0.5f)),mat4_identity());
	return m;
}

Mesh gen_box_hole_cylinder(float w,float h,float depth,float r,int sides){
	Mesh m={0};
	if(sides<8) sides=32;
	sides=(sides+3)/4*4;
	int quarter=sides/4;
	float hw=w*0.5f,hh=h*0.5f;
	if(r<=1e-6f || r>=hw+1e-6f || r>=hh+1e-6f) return gen_box(w,h,depth);
	vec3 box[8]={v3(-hw,-hh,0),v3(0,-hh,0),v3(hw,-hh,0),v3(hw,0,0),
		v3(hw,hh,0),v3(0,hh,0),v3(-hw,hh,0),v3(-hw,0,0)};
	vec3 *circle=malloc(sizeof(vec3)*(size_t)sides);
	for(int i=0;i<sides;i++){
		float a=(float)i/(float)sides*2.0f*M_PIf;
		circle[i]=v3(cosf(a)*r,sinf(a)*r,0);
	}
	vec3 *q=malloc(sizeof(vec3)*(size_t)(quarter+4));
	int n=0;
	q[n++]=v3(r,0,0);
	if(hw>r+1e-6f) q[n++]=v3(hw,0,0);
	q[n++]=v3(hw,hh,0);
	if(hh>r+1e-6f) q[n++]=v3(0,hh,0);
	for(int i=quarter;i>0;i--){
		float a=(float)i/(float)quarter*M_PIf*0.5f;
		q[n++]=v3(cosf(a)*r,sinf(a)*r,0);
	}
	vec3 *qx=mirror_profile_x(q,n);
	vec3 *qy=mirror_profile_y(q,n);
	vec3 *qxy=mirror_profile_y(qx,n);
	extrude_polygon(&m,q,NULL,n,depth,1,0,0,0);
	extrude_polygon(&m,qx,NULL,n,depth,1,0,0,0);
	extrude_polygon(&m,qy,NULL,n,depth,1,0,0,0);
	extrude_polygon(&m,qxy,NULL,n,depth,1,0,0,0);
	extrude_polygon(&m,box,NULL,8,depth,0,1,0,0);
	extrude_polygon(&m,circle,NULL,sides,depth,0,1,1,1);
	free(circle); free(q); free(qx); free(qy); free(qxy);
	return m;
}

/* Extrude a 2D polygon profile (CCW from front) along Z by depth.
 * caps=1: also emit triangulated front (+Z) and back (-Z) cap faces.
 * sides=1: emit the profile boundary walls.
 * side_normals[i]: outward normal for edge pts[i]→pts[(i+1)%n]; NULL = auto-computed.
 * flip=1: reverse all winding and normals (use for inward/tunnel surfaces). */
static float profile_cross(vec3 a,vec3 b,vec3 c){
	return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}

static int profile_point_in_tri(vec3 p,vec3 a,vec3 b,vec3 c,float epsilon){
	float ab=profile_cross(a,b,p),bc=profile_cross(b,c,p),ca=profile_cross(c,a,p);
	return ab>=-epsilon && bc>=-epsilon && ca>=-epsilon;
}

static void extrude_cap(Mesh *m,vec3 *pts,int n,float z,vec3 normal,int reverse){
	int *idx=malloc(sizeof(int)*(size_t)n),nv=n;
	float area=0;
	for(int i=0;i<n;i++) area+=profile_cross(v3(0,0,0),pts[i],pts[(i+1)%n]);
	float epsilon=fmaxf(PROFILE_TRIANGULATION_MIN_EPSILON,fabsf(area)*PROFILE_TRIANGULATION_RATIO);
	for(int i=0;i<n;i++) idx[i]=i;
	while(nv>2){
		int found=0;
		for(int i=0;i<nv;i++){
			int ia=idx[(i+nv-1)%nv],ib=idx[i],ic=idx[(i+1)%nv],inside=0;
			if(profile_cross(pts[ia],pts[ib],pts[ic])<=epsilon) continue;
			for(int j=0;j<nv;j++){
				int ip=idx[j];
				if(ip!=ia && ip!=ib && ip!=ic && profile_point_in_tri(pts[ip],pts[ia],pts[ib],pts[ic],epsilon)){
					inside=1;
					break;
				}
			}
			if(inside) continue;
			int a=mesh_add_vert(m,v3(pts[ia].x,pts[ia].y,z),normal);
			int b=mesh_add_vert(m,v3(pts[ib].x,pts[ib].y,z),normal);
			int c=mesh_add_vert(m,v3(pts[ic].x,pts[ic].y,z),normal);
			if(reverse) mesh_add_tri(m,a,c,b);
			else mesh_add_tri(m,a,b,c);
			memmove(&idx[i],&idx[i+1],sizeof(int)*(size_t)(nv-i-1));
			nv--;
			found=1;
			break;
		}
		if(!found) break;
	}
	free(idx);
}

static void extrude_polygon(Mesh *m,vec3 *pts,vec3 *side_normals,int n,float depth,int caps,int sides,int flip,int smooth){
	float hd=depth*0.5f;

	if(caps){
		vec3 fN = flip ? v3(0,0,-1) : v3(0,0,1);
		vec3 bN = flip ? v3(0,0, 1) : v3(0,0,-1);
		extrude_cap(m,pts,n, hd,fN,flip);
		extrude_cap(m,pts,n,-hd,bN,!flip);
	}

	if(sides) for(int i=0;i<n;i++){
		int j=(i+1)%n;
		vec3 p0=pts[i], p1=pts[j];
		vec3 sn;
		if(side_normals){
			sn = flip ? vscale(side_normals[i],-1.0f) : side_normals[i];
		} else {
			vec3 edge=vsub(p1,p0);
			sn = vnorm(v3(edge.y,-edge.x,0));
			if(flip) sn=vscale(sn,-1.0f);
		}
		vec3 f0=v3(p0.x,p0.y, hd), f1=v3(p1.x,p1.y, hd);
		vec3 b0=v3(p0.x,p0.y,-hd), b1=v3(p1.x,p1.y,-hd);
		vec3 n0=sn,n1=sn;
		if(smooth && !side_normals){
			vec3 pp=pts[(i+n-1)%n],pn=pts[(j+1)%n];
			n0=vnorm(vadd(vnorm(v3(p0.y-pp.y,pp.x-p0.x,0)),vnorm(v3(p1.y-p0.y,p0.x-p1.x,0))));
			n1=vnorm(vadd(vnorm(v3(p1.y-p0.y,p0.x-p1.x,0)),vnorm(v3(pn.y-p1.y,p1.x-pn.x,0))));
			if(flip){ n0=vscale(n0,-1.0f); n1=vscale(n1,-1.0f); }
		}
		int ib0=mesh_add_vert(m,b0,n0),ib1=mesh_add_vert(m,b1,n1);
		int if1=mesh_add_vert(m,f1,n1),if0=mesh_add_vert(m,f0,n0);
		if(!flip){ mesh_add_tri(m,ib0,ib1,if1); mesh_add_tri(m,ib0,if1,if0); }
		else { mesh_add_tri(m,ib0,if1,ib1); mesh_add_tri(m,ib0,if0,if1); }
	}
}

Shape2D shape2d_window(window_outline_t outline,float width,float height,int segments){
	Shape2D p={0}; p.closed=1;
	if(!isfinite(width)||!isfinite(height)||width<=PROFILE_EPSILON||height<=PROFILE_EPSILON) return p;
	if(segments<WINDOW_MIN_SEGMENTS) segments=WINDOW_MIN_SEGMENTS;
	if(segments>WINDOW_MAX_SEGMENTS) segments=WINDOW_MAX_SEGMENTS;
	if(segments%2) segments++;
	float r=width/2,top=height/2,bottom=-top;
	vec3 v=v3(-r,bottom,0); DA_PUSH(p.pts,p.npts,p.cpts,v);
	v=v3(r,bottom,0); DA_PUSH(p.pts,p.npts,p.cpts,v);
	if(outline==WINDOW_RECTANGLE){
		v=v3(r,top,0); DA_PUSH(p.pts,p.npts,p.cpts,v);
		v=v3(-r,top,0); DA_PUSH(p.pts,p.npts,p.cpts,v);
		return p;
	}
	float rise=outline==WINDOW_ROUND_ARCH?r:sqrtf(WINDOW_POINTED_RISE_SQUARED)*r;
	if(height<=rise+PROFILE_EPSILON){ shape2d_free(&p); return p; }
	float spring=top-rise;
	for(int i=0;i<=segments;i++){
		float angle=(float)i/(float)segments*M_PIf;
		if(outline==WINDOW_ROUND_ARCH) v=v3(r*cosf(angle),spring+r*sinf(angle),0);
		else {
			float a=(float)(i<=segments/2?i:segments-i)/(float)(segments/2)*M_PIf/WINDOW_POINTED_ANGLE_DIVISOR;
			float x=-r+width*cosf(a);
			v=v3(i<=segments/2?x:-x,spring+width*sinf(a),0);
		}
		DA_PUSH(p.pts,p.npts,p.cpts,v);
	}
	return p;
}

static float shape2d_area(const Shape2D *p){
	float area=0;
	for(int i=0;i<p->npts;i++){
		vec3 a=p->pts[i],b=p->pts[(i+1)%p->npts]; area+=a.x*b.y-a.y*b.x;
	}
	return area/2;
}


Mesh gen_profile_extrusion(const Shape2D *p,float depth){
	Mesh m={0};
	if(p->npts>=PROFILE_MIN_POINTS && depth>PROFILE_EPSILON)
		extrude_polygon(&m,p->pts,NULL,p->npts,depth,1,1,0,0);
	return m;
}

static int profile_simple(const vec3 *pts,int n){
	if(n<PROFILE_MIN_POINTS) return 0;
	float area=0;
	for(int i=0;i<n;i++){
		vec3 a=pts[i],b=pts[(i+1)%n];
		if(!isfinite(a.x)||!isfinite(a.y)||vlen(vsub(b,a))<=MESH_EDGE_WELD_MIN_LENGTH) return 0;
		area+=a.x*b.y-a.y*b.x;
	}
	if(area<=PROFILE_TRIANGULATION_MIN_EPSILON) return 0;
	for(int i=0;i<n;i++) for(int j=i+2;j<n;j++){
		if(i==0&&j==n-1) continue;
		vec3 a=pts[i],b=pts[(i+1)%n],c=pts[j],d=pts[(j+1)%n];
		float ab_c=profile_cross(a,b,c),ab_d=profile_cross(a,b,d);
		float cd_a=profile_cross(c,d,a),cd_b=profile_cross(c,d,b);
		if(ab_c*ab_d<0&&cd_a*cd_b<0) return 0;
	}
	return 1;
}

static int profile_contains_point(const vec3 *pts,int n,vec3 point){
	int inside=0;
	for(int i=0,j=n-1;i<n;j=i++){
		vec3 a=pts[i],b=pts[j];
		if((a.y>point.y)!=(b.y>point.y) &&
			point.x<(b.x-a.x)*(point.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
	}
	return inside;
}

static int profile_offset(const Shape2D *profile,float amount,vec3 *inset){
	int n=profile->npts;
	for(int i=0;i<n;i++){
		vec3 prev=profile->pts[(i+n-1)%n],p=profile->pts[i],next=profile->pts[(i+1)%n];
		vec3 before=vnorm(vsub(p,prev)),after=vnorm(vsub(next,p));
		vec3 in_before=v3(-before.y,before.x,0),in_after=v3(-after.y,after.x,0);
		float cross=before.x*after.y-before.y*after.x;
		if(fabsf(cross)<PROFILE_OFFSET_PARALLEL_EPSILON){
			if(vdot(before,after)<=0) return 0;
			inset[i]=vadd(p,vscale(in_before,amount));
		} else {
			vec3 delta=vscale(vsub(in_after,in_before),amount);
			float along=(delta.x*after.y-delta.y*after.x)/cross;
			inset[i]=vadd(vadd(p,vscale(in_before,amount)),vscale(before,along));
		}
		if(!isfinite(inset[i].x)||!isfinite(inset[i].y)) return 0;
	}
	if(!profile_simple(inset,n)) return 0;
	for(int i=0;i<n;i++){
		vec3 midpoint=vscale(vadd(inset[i],inset[(i+1)%n]),0.5f);
		if(!profile_contains_point(profile->pts,n,inset[i])||!profile_contains_point(profile->pts,n,midpoint)) return 0;
	}
	return 1;
}

#define PROFILE_BEVEL_CLAMP_STEPS 12
#define PROFILE_BEVEL_RETRIES 8
#define PROFILE_BEVEL_SHRINK 0.8f
static Mesh gen_profile_extrusion_beveled_once(const Shape2D *profile,float depth,float bevel,int bevel_segments){
	Mesh mesh={0};
	if(!profile||!profile->closed||!isfinite(depth)||!isfinite(bevel)||depth<=0||bevel<0||bevel>=depth*0.5f||
		!profile_simple(profile->pts,profile->npts)) return mesh;
	int n=profile->npts;
	if(bevel==0){
		mesh=gen_profile_extrusion(profile,depth);
		if(mesh.ntris!=4*n-4){ mesh_free(&mesh); return (Mesh){0}; }
	}
	else {
		if(bevel_segments<1) bevel_segments=1;
		if(bevel_segments>ROUNDED_BOX_MAX_BEVEL_SEGMENTS) bevel_segments=ROUNDED_BOX_MAX_BEVEL_SEGMENTS;
		vec3 *inset=malloc(sizeof(vec3)*(size_t)n);
		/* Blender's Clamp Overlap: a bevel too wide for a short edge of the outline is narrowed until it fits */
		if(!profile_offset(profile,bevel,inset)){
			float lo=0,hi=bevel;
			for(int k=0;k<PROFILE_BEVEL_CLAMP_STEPS;k++){ float mid=(lo+hi)*0.5f; if(profile_offset(profile,mid,inset)) lo=mid; else hi=mid; }
			if(lo<=PROFILE_EPSILON || !profile_offset(profile,lo,inset)){ free(inset); return mesh; }
			bevel=lo;
		}
		int ring_count=2*(bevel_segments+1);
		for(int ring=0;ring<ring_count;ring++){
			int front=ring>bevel_segments,step=front?ring-bevel_segments-1:ring;
			float angle=(float)step/(float)bevel_segments*M_PIf*0.5f;
			float factor=front?1.0f-cosf(angle):1.0f-sinf(angle);
			float z=front?depth*0.5f-bevel+bevel*sinf(angle):-depth*0.5f+bevel*(1.0f-cosf(angle));
			float radial=front?cosf(angle):sinf(angle),axial=front?sinf(angle):-cosf(angle);
			for(int i=0;i<n;i++){
				vec3 outer=profile->pts[i],offset=vsub(inset[i],outer),normal=vnorm(vsub(outer,inset[i]));
				vec3 point=vadd(outer,vscale(offset,factor)); point.z=z;
				mesh_add_vert(&mesh,point,v3(normal.x*radial,normal.y*radial,axial));
			}
		}
		for(int ring=0;ring<ring_count-1;ring++) for(int i=0;i<n;i++){
			int next=(i+1)%n,a=ring*n+i,b=ring*n+next,c=(ring+1)*n+next,d=(ring+1)*n+i;
			mesh_add_tri(&mesh,a,b,c); mesh_add_tri(&mesh,a,c,d);
		}
		int before=mesh.ntris;
		extrude_cap(&mesh,inset,n,-depth*0.5f,v3(0,0,-1),1);
		extrude_cap(&mesh,inset,n,depth*0.5f,v3(0,0,1),0);
		free(inset);
		if(mesh.ntris-before!=2*(n-2)){ mesh_free(&mesh); return (Mesh){0}; }
	}
	if(!mesh.ntris) return mesh;
	vec3 lo=v3(INFINITY,INFINITY,0),hi=v3(-INFINITY,-INFINITY,0);
	for(int i=0;i<n;i++){
		lo.x=fminf(lo.x,profile->pts[i].x); lo.y=fminf(lo.y,profile->pts[i].y);
		hi.x=fmaxf(hi.x,profile->pts[i].x); hi.y=fmaxf(hi.y,profile->pts[i].y);
	}
	float width=hi.x-lo.x,height=hi.y-lo.y;
	for(int i=0;i<mesh.nverts;i++){
		mesh.verts[i].u=(mesh.verts[i].pos.x-lo.x)/width;
		mesh.verts[i].v=(hi.y-mesh.verts[i].pos.y)/height;
	}
	return mesh;
}

/* Blender's Clamp Overlap, all the way: a bevel the outline cannot take (an inset that folds, or caps that
   no longer triangulate) is narrowed until the solid builds. */
Mesh gen_profile_extrusion_beveled(const Shape2D *profile,float depth,float bevel,int bevel_segments){
	Mesh mesh=gen_profile_extrusion_beveled_once(profile,depth,bevel,bevel_segments);
	for(int k=0;k<PROFILE_BEVEL_RETRIES && !mesh.ntris && bevel>0;k++){
		bevel*=PROFILE_BEVEL_SHRINK;
		mesh=gen_profile_extrusion_beveled_once(profile,depth,bevel,bevel_segments);
	}
	return mesh;
}


static void profile_push_unique(Shape2D *p,vec3 v){
	if(!p->npts||vlen(vsub(p->pts[p->npts-1],v))>PROFILE_EPSILON)
		DA_PUSH(p->pts,p->npts,p->cpts,v);
}

static Shape2D profile_halfplane(const Shape2D *p,vec3 a,vec3 b,int inside){
	Shape2D q={0}; q.closed=1;
	for(int i=0;i<p->npts;i++){
		vec3 u=p->pts[i],v=p->pts[(i+1)%p->npts];
		float du=profile_cross(a,b,u),dv=profile_cross(a,b,v);
		if(!inside){ du=-du; dv=-dv; }
		if(du>=0) profile_push_unique(&q,u);
		if((du<0&&dv>0)||(du>0&&dv<0)) profile_push_unique(&q,lerp(u,v,du/(du-dv)));
	}
	if(q.npts>1&&vlen(vsub(q.pts[0],q.pts[q.npts-1]))<=PROFILE_EPSILON) q.npts--;
	if(q.npts<PROFILE_MIN_POINTS||shape2d_area(&q)<=PROFILE_EPSILON) shape2d_free(&q);
	return q;
}

Shape2D shape2d_clip_rect(const Shape2D *p,float width,float height){
	Shape2D q={0}; q.closed=1;
	Shape2D bounds=shape2d_window(WINDOW_RECTANGLE,width,height,WINDOW_MIN_SEGMENTS);
	for(int i=0;i<p->npts;i++) DA_PUSH(q.pts,q.npts,q.cpts,p->pts[i]);
	for(int i=0;i<bounds.npts&&q.npts;i++){
		Shape2D clipped=profile_halfplane(&q,bounds.pts[i],bounds.pts[(i+1)%bounds.npts],1);
		shape2d_free(&q); q=clipped;
	}
	shape2d_free(&bounds); return q;
}

Shape2D shape2d_outline(const Shape2D *p){
	Shape2D q={0}; q.closed=1;
	if(!p||p->npts<PROFILE_MIN_POINTS) return q;
	for(int i=0;i<p->npts;i++) profile_push_unique(&q,p->pts[i]);
	if(q.npts>1&&vlen(vsub(q.pts[0],q.pts[q.npts-1]))<=PROFILE_EPSILON) q.npts--;
	if(shape2d_area(&q)<0) for(int i=0,j=q.npts-1;i<j;i++,j--){ vec3 t=q.pts[i]; q.pts[i]=q.pts[j]; q.pts[j]=t; }
	if(q.npts<PROFILE_MIN_POINTS||fabsf(shape2d_area(&q))<=PROFILE_EPSILON) shape2d_free(&q);
	return q;
}

Shape2D shape2d_stroke(const Shape2D *path,float left_width,float right_width){
	Shape2D q={0}; q.closed=1;
	int n=path?path->npts:0;
	if(n<2||!isfinite(left_width)||!isfinite(right_width)||left_width<0||right_width<0||left_width+right_width<=PROFILE_EPSILON) return q;
	vec3 *left=malloc(sizeof(vec3)*(size_t)n),*right=malloc(sizeof(vec3)*(size_t)n);
	for(int i=0;i<n;i++){
		vec3 before=i>0?vnorm(vsub(path->pts[i],path->pts[i-1])):v3(0,0,0);
		vec3 after=i<n-1?vnorm(vsub(path->pts[i+1],path->pts[i])):v3(0,0,0);
		vec3 dir=vnorm(vadd(before,after));
		if(vlen(dir)<PROFILE_EPSILON) dir=after;
		vec3 side=v3(-dir.y,dir.x,0),edge=i<n-1?after:before;
		/* A mitre keeps both walls of the slab at the full thickness; cap it so a hairpin turn
		   cannot shoot a corner off to infinity. */
		float cosine=vdot(side,v3(-edge.y,edge.x,0)),miter=cosine>1.0f/PROFILE_STROKE_MAX_MITER?1.0f/cosine:PROFILE_STROKE_MAX_MITER;
		left[i]=vadd(path->pts[i],vscale(side,left_width*miter)); right[i]=vsub(path->pts[i],vscale(side,right_width*miter));
	}
	Shape2D raw={0}; raw.closed=1;
	for(int i=0;i<n;i++) DA_PUSH(raw.pts,raw.npts,raw.cpts,right[i]);
	for(int i=n-1;i>=0;i--) DA_PUSH(raw.pts,raw.npts,raw.cpts,left[i]);
	free(left); free(right);
	q=shape2d_outline(&raw); shape2d_free(&raw);
	return q;
}

Shape2D shape2d_fillet(const Shape2D *p,float radius,int segments){
	Shape2D q={0}; q.closed=p?p->closed:1;
	int n=p?p->npts:0;
	if(n<(q.closed?PROFILE_MIN_POINTS:2)) return q;
	if(segments<1) segments=1;
	if(segments>PROFILE_FILLET_MAX_SEGMENTS) segments=PROFILE_FILLET_MAX_SEGMENTS;
	for(int i=0;i<n;i++){
		if(!q.closed&&(i==0||i==n-1)){ profile_push_unique(&q,p->pts[i]); continue; }
		vec3 a=p->pts[(i+n-1)%n],c=p->pts[i],b=p->pts[(i+1)%n];
		vec3 u=vsub(a,c),w=vsub(b,c);
		float lu=vlen(u),lw=vlen(w);
		u=vscale(u,1.0f/lu); w=vscale(w,1.0f/lw);
		float cosine=vdot(u,w);
		if(radius<=PROFILE_EPSILON||fabsf(cosine)>PROFILE_FILLET_STRAIGHT_COS){ profile_push_unique(&q,c); continue; }
		/* Each tangent point may use at most half of its edge so neighbouring fillets never overlap;
		   a corner on a short edge gets a proportionally smaller radius. */
		float half=acosf(cosine)*0.5f,tangent=radius/tanf(half);
		float limit=fminf(lu,lw)*0.5f;
		if(tangent>limit) tangent=limit;
		float r=tangent*tanf(half);
		vec3 center=vadd(c,vscale(vnorm(vadd(u,w)),r/sinf(half)));
		vec3 t0=vsub(vadd(c,vscale(u,tangent)),center),t1=vsub(vadd(c,vscale(w,tangent)),center);
		float a0=atan2f(t0.y,t0.x),sweep=atan2f(t1.y,t1.x)-a0;
		while(sweep>M_PIf) sweep-=2.0f*M_PIf;
		while(sweep<-M_PIf) sweep+=2.0f*M_PIf;
		for(int k=0;k<=segments;k++){
			float t=a0+sweep*(float)k/(float)segments;
			profile_push_unique(&q,vadd(center,v3(cosf(t)*r,sinf(t)*r,0)));
		}
	}
	if(q.closed&&q.npts>1&&vlen(vsub(q.pts[0],q.pts[q.npts-1]))<=PROFILE_EPSILON) q.npts--;
	if(q.npts<(q.closed?PROFILE_MIN_POINTS:2)) shape2d_free(&q);
	return q;
}

Shape2D shape2d_inset(const Shape2D *p,float distance){
	Shape2D q={0}; q.closed=1;
	if(p->npts<PROFILE_MIN_POINTS||distance<0||!isfinite(distance)) return q;
	for(int i=0;i<p->npts;i++) DA_PUSH(q.pts,q.npts,q.cpts,p->pts[i]);
	for(int i=0;i<p->npts&&q.npts;i++){
		vec3 a=p->pts[i],b=p->pts[(i+1)%p->npts],e=vnorm(vsub(b,a));
		vec3 offset=vscale(v3(-e.y,e.x,0),distance);
		Shape2D clipped=profile_halfplane(&q,vadd(a,offset),vadd(b,offset),1);
		shape2d_free(&q); q=clipped;
	}
	return q;
}

/* Partition in 2D so overlapping cutters remove their union, including at wall edges. */
Mesh gen_profile_cutouts(const Shape2D *boundary,const Shape2D *holes,int nholes,float depth){
	Mesh mesh={0};
	Shape2D *pieces=NULL; int npieces=0,cpieces=0;
	Shape2D initial={0}; initial.closed=1;
	for(int i=0;i<boundary->npts;i++) DA_PUSH(initial.pts,initial.npts,initial.cpts,boundary->pts[i]);
	DA_PUSH(pieces,npieces,cpieces,initial);
	for(int h=0;h<nholes;h++){
		Shape2D *next=NULL; int nnext=0,cnext=0;
		for(int p=0;p<npieces;p++){
			Shape2D remaining=pieces[p];
			for(int e=0;e<holes[h].npts&&remaining.npts;e++){
				vec3 a=holes[h].pts[e],b=holes[h].pts[(e+1)%holes[h].npts];
				Shape2D outside=profile_halfplane(&remaining,a,b,0);
				Shape2D inside=profile_halfplane(&remaining,a,b,1);
				if(outside.npts) DA_PUSH(next,nnext,cnext,outside);
				shape2d_free(&remaining); remaining=inside;
			}
			shape2d_free(&remaining);
		}
		free(pieces); pieces=next; npieces=nnext; cpieces=cnext;
	}
	for(int p=0;p<npieces;p++){
		extrude_polygon(&mesh,pieces[p].pts,NULL,pieces[p].npts,depth,1,1,0,0);
		shape2d_free(&pieces[p]);
	}
	free(pieces);
	return mesh;
}

Mesh gen_profile_frame(const Shape2D *outer,const Shape2D *inner,float depth){
	if(inner->npts<PROFILE_MIN_POINTS||depth<=PROFILE_EPSILON) return (Mesh){0};
	return gen_profile_cutouts(outer,inner,1,depth);
}

/* Wall lunette above a roman-arch opening, extruded along Z.
 * The opening spans the full profile width and height, so the mirrored lunette is the complete solid. */
Mesh gen_box_hole_arch(float w, float h, float depth, int sides){
	Mesh m={0};
	if(sides<8) sides=16;
	if(sides%2) sides++; /* ensure even so half is exact */
	int half=sides/2;
	float hw=w*0.5f, hh=h*0.5f;
	float archR=hw;
	float springY=hh-archR;

	int nLeft=half+2;
	vec3 *leftPts=malloc(sizeof(vec3)*(size_t)nLeft);
	leftPts[0]=v3(-hw,hh,0);
	for(int i=0;i<=half;i++){
		float a = M_PIf - (float)i/(float)sides*M_PIf;
		float ca=cosf(a), sa=sinf(a);
		leftPts[1+i]=v3(ca*archR,springY+sa*archR,0);
	}
	vec3 *rightPts=mirror_profile_x(leftPts,nLeft);
	extrude_polygon(&m,leftPts,NULL,nLeft,depth,1,1,0,0);
	extrude_polygon(&m,rightPts,NULL,nLeft,depth,1,1,0,0);
	free(leftPts);
	free(rightPts);

	return m;
}

/* -------------------------------------------------- shape & loft helpers -- */

void shape2d_free(Shape2D *s){
	free(s->pts); free(s->nrm);
	memset(s,0,sizeof(*s));
}

void shape2d_compute_normals(Shape2D *s){
	free(s->nrm); s->nrm=malloc(sizeof(vec3)*(size_t)s->npts);
	int n=s->npts;
	for(int i=0;i<n;i++){
		int p=(i==0)?(s->closed?n-1:i):i-1;
		int nx=(i==n-1)?(s->closed?0:i):i+1;
		vec3 a=vsub(s->pts[i],s->pts[p]);
		vec3 b=vsub(s->pts[nx],s->pts[i]);
		vec3 na=vnorm(v3(a.y,-a.x,0));
		vec3 nb=vnorm(v3(b.y,-b.x,0));
		s->nrm[i]=vnorm(vadd(na,nb));
	}
}

Mesh gen_lathe(Shape2D *profile,int segments){
	Mesh m={0};
	if(segments<3) segments=24;
	if(profile->npts<2) return m;
	if(profile->closed){
		return m;
	}
	int n=profile->npts;

	/* segments + 1 columns: the last repeats the first, so u (around, 0..1) and v (along the profile, 0..1) run
	   unbroken and a <gradient type="uv"> can stripe a lathe; the seam's two columns share position and normal */
	for(int j=0;j<=segments;j++){
		float a0=(float)j/segments*2.0f*M_PIf;
		float ca0=cosf(a0),sa0=sinf(a0);
		for(int i=0;i<n;i++){
			float px=profile->pts[i].x, py=profile->pts[i].y;
			vec3 nrm=vnorm(v3(
				profile->nrm[i].x*ca0,
				profile->nrm[i].y,
				profile->nrm[i].x*sa0));
			int v=mesh_add_vert(&m,v3(px*ca0,py,px*sa0),nrm);
			m.verts[v].u=(float)j/segments; m.verts[v].v=n>1?(float)i/(n-1):0;
		}
	}

	for(int j=0;j<segments;j++){
		int jn=j+1;
		for(int i=0;i<n-1;i++){
			int v00=j*n+i, v01=j*n+i+1;
			mesh_add_tri(&m,v00,v01,jn*n+i);
			mesh_add_tri(&m,v01,jn*n+i+1,jn*n+i);
		}
	}

	vec3 botN=v3(0,-1,0), topN=v3(0,1,0);
	int vBot=mesh_add_vert(&m,v3(0,profile->pts[0].y,0),botN);
	int vTop=mesh_add_vert(&m,v3(0,profile->pts[n-1].y,0),topN);
	m.verts[vTop].v=1;
	for(int j=0;j<segments;j++){
		int jn=j+1;
		mesh_add_tri(&m,vBot,j*n,jn*n);
		mesh_add_tri(&m,vTop,jn*n+n-1,j*n+n-1);
	}

	return m;
}

Mesh gen_loft(LoftPath *path,Shape2D *cross,int closed){
	Mesh m={0};
	int ns=path->npts, nr=cross->npts;
	if(ns<2||nr<2) return m;

	vec3 *T=malloc(sizeof(vec3)*(size_t)ns);
	vec3 *N=malloc(sizeof(vec3)*(size_t)ns);
	vec3 *B=malloc(sizeof(vec3)*(size_t)ns);
	for(int i=0;i<ns;i++){
		int p=(i==0)?(closed?ns-1:0):i-1;
		int nx=(i==ns-1)?(closed?0:i):i+1;
		T[i]=vnorm(vsub(path->pts[nx],path->pts[p]));
		if(fabsf(T[i].y)<0.999f){
			N[i]=vnorm(vcross(v3(0,1,0),T[i]));
		} else {
			N[i]=vnorm(vcross(v3(0,0,1),T[i]));
		}
		B[i]=vnorm(vcross(T[i],N[i]));
	}

	for(int i=0;i<ns;i++){
		for(int j=0;j<nr;j++){
			vec3 cs=cross->pts[j];
			vec3 wp=vadd(vadd(path->pts[i],vscale(N[i],cs.x)),vscale(B[i],cs.y));
			vec3 nrm=vnorm(vadd(vscale(N[i],cross->nrm[j].x),vscale(B[i],cross->nrm[j].y)));
			mesh_add_vert(&m,wp,nrm);
		}
	}

	int nloops=closed?ns:ns-1;
	for(int i=0;i<nloops;i++){
		int i0=i,i1=(i+1)%ns;
		for(int j=0;j<nr;j++){
			int jn=(j+1)%nr;
			int a=i0*nr+j,b=i0*nr+jn,d=i1*nr+j,c=i1*nr+jn;
			mesh_add_tri(&m,a,b,c);
			mesh_add_tri(&m,a,c,d);
		}
	}

	if(!closed){
		vec3 capN0=vscale(T[0],-1.0f);
		int c0=mesh_add_vert(&m,path->pts[0],capN0);
		for(int j=0;j<nr;j++){
			int jn=(j+1)%nr;
			mesh_add_tri(&m,c0,jn,j);
		}
		int cn=mesh_add_vert(&m,path->pts[ns-1],T[ns-1]);
		for(int j=0;j<nr;j++){
			int jn=(j+1)%nr;
			mesh_add_tri(&m,cn,(ns-1)*nr+j,(ns-1)*nr+jn);
		}
	}

	free(T); free(N); free(B);
	return m;
}

/* ---------------------------------------------------------- modifiers ------ */

/* ---- 3ds Max-style deform modifiers (Taper, Twist, Bend, Stretch, Skew) ----
   Each acts along an axis through its gizmo centre: a is a vertex's distance along the axis from the centre and
   z = a / H, H being the gizmo's extent (the mesh's bounds along the axis, or upper - lower when limited). With
   limits, the effect is computed at a clamped into [lower, upper] and the geometry beyond carries on rigidly, as
   in Max's Limit Effect. Normals take the inverse transpose of each deformation's Jacobian. */
typedef struct { int ia,ib,ic,limited; float c[3],H,lo,hi; } deform_t;

static int deform_setup(const Mesh *m,const deform_gizmo_t *g,deform_t *d){
	memset(d,0,sizeof(*d));
	d->ia=g->axis=='x'?0:g->axis=='y'?1:2; d->ib=g->axis=='x'?1:0; d->ic=g->axis=='z'?1:2;
	if(m->nverts<=0) return 0;
	float mn=INFINITY,mx=-INFINITY;
	for(int i=0;i<m->nverts;i++){ float a=(&m->verts[i].pos.x)[d->ia]; mn=fminf(mn,a); mx=fmaxf(mx,a); }
	if(g->hasCenter){ d->c[0]=g->center.x; d->c[1]=g->center.y; d->c[2]=g->center.z; }
	else d->c[d->ia]=mn;   /* the bottom of the bounds, where a Max primitive keeps its pivot */
	d->limited=g->limited; d->lo=g->lower; d->hi=g->upper;
	d->H=g->limited?g->upper-g->lower:mx-mn;
	return d->H>DEFORM_MIN_EXTENT;
}
/* The vertex's distance along the axis inside the limits, and how far it lies beyond them (0 inside). */
static float deform_clamp(const deform_t *d,float a,float *beyond){
	float ac=d->limited?fminf(fmaxf(a,d->lo),d->hi):a;
	*beyond=a-ac; return ac;
}
static void vec_normalize3(float *n){ float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(l>0){ n[0]/=l; n[1]/=l; n[2]/=l; } }

/* Taper: the crosswise axes named by effect (both by default) scale by 1 + amount*z + curve*z*(1-z);
   symmetry tapers both sides of the centre alike (z = |a| / H). */
void mesh_apply_taper(Mesh *m,const deform_gizmo_t *g,float amount,float curve,int symmetry,const char *effect){
	deform_t d; if(!deform_setup(m,g,&d)) return;
	int eb=1,ec=1;
	if(effect && *effect){ eb=strchr(effect,"xyz"[d.ib])!=NULL; ec=strchr(effect,"xyz"[d.ic])!=NULL; }
	for(int i=0;i<m->nverts;i++){
		float *p=&m->verts[i].pos.x,*n=&m->verts[i].nrm.x,beyond;
		float a=deform_clamp(&d,p[d.ia]-d.c[d.ia],&beyond),z=a/d.H,sg=1.0f;
		if(symmetry && z<0){ z=-z; sg=-1.0f; }
		float sv=1.0f+amount*z+curve*z*(1.0f-z),ds=beyond!=0?0.0f:(amount+curve*(1.0f-2.0f*z))*sg/d.H;
		float sb=eb?sv:1.0f,sc=ec?sv:1.0f,xb=p[d.ib]-d.c[d.ib],xc=p[d.ic]-d.c[d.ic];
		float db=eb?ds*xb:0.0f,dc=ec?ds*xc:0.0f;
		float nb=sc*n[d.ib],nc=sb*n[d.ic],na=-db*sc*n[d.ib]-dc*sb*n[d.ic]+sb*sc*n[d.ia];
		n[d.ib]=nb; n[d.ic]=nc; n[d.ia]=na; vec_normalize3(n);
		p[d.ib]=d.c[d.ib]+xb*sb; p[d.ic]=d.c[d.ic]+xc*sc;
	}
}

/* Twist: the cross-section turns by angle * z about the axis through the centre; bias (-100..100) gathers the
   turn towards the centre (positive) or away from it (negative), as Perlin's bias curve. */
void mesh_apply_twist(Mesh *m,const deform_gizmo_t *g,float angleDeg,float bias){
	deform_t d; if(!deform_setup(m,g,&d)) return;
	float rad=angleDeg*M_PIf/180.0f,b=fminf(fmaxf((bias/100.0f+1.0f)*0.5f,DEFORM_BIAS_MIN),1.0f-DEFORM_BIAS_MIN);
	float e=logf(b)/logf(0.5f);
	for(int i=0;i<m->nverts;i++){
		float *p=&m->verts[i].pos.x,*n=&m->verts[i].nrm.x,beyond;
		float a=deform_clamp(&d,p[d.ia]-d.c[d.ia],&beyond),z=fabsf(a/d.H),sg=a<0?-1.0f:1.0f;
		float th=rad*sg*powf(z,e),dth=beyond!=0||z<DEFORM_MIN_EXTENT?0.0f:rad*e*powf(z,e-1.0f)/d.H;
		float ct=cosf(th),st=sinf(th),xb=p[d.ib]-d.c[d.ib],xc=p[d.ic]-d.c[d.ic];
		float nb=n[d.ib],nc=n[d.ic];
		n[d.ia]-=dth*(-xc*nb+xb*nc);
		n[d.ib]=nb*ct-nc*st; n[d.ic]=nb*st+nc*ct; vec_normalize3(n);
		p[d.ib]=d.c[d.ib]+xb*ct-xc*st; p[d.ic]=d.c[d.ic]+xb*st+xc*ct;
	}
}

/* Bend: the axis curls through angle over the gizmo's extent, about the centre, towards direction (degrees about
   the axis from the first crosswise axis: x for y and z, y for x). Beyond a limit the shape goes on straight. */
void mesh_apply_bend(Mesh *m,const deform_gizmo_t *g,float angleDeg,float directionDeg){
	deform_t d; if(!deform_setup(m,g,&d)) return;
	float rad=angleDeg*M_PIf/180.0f;
	if(fabsf(rad)<DEFORM_MIN_ANGLE) return;
	float R=d.H/rad,dir=directionDeg*M_PIf/180.0f,ub=cosf(dir),uc=sinf(dir);
	for(int i=0;i<m->nverts;i++){
		float *p=&m->verts[i].pos.x,*n=&m->verts[i].nrm.x,beyond;
		float a=deform_clamp(&d,p[d.ia]-d.c[d.ia],&beyond),xb=p[d.ib]-d.c[d.ib],xc=p[d.ic]-d.c[d.ic];
		float u=xb*ub+xc*uc,w=-xb*uc+xc*ub,th=a/R,ct=cosf(th),st=sinf(th);
		float u2=R-(R-u)*ct+beyond*st,a2=(R-u)*st+beyond*ct;
		float k=beyond!=0?1.0f:(R-u)/R; if(fabsf(k)<DEFORM_MIN_EXTENT) k=DEFORM_MIN_EXTENT;
		float nu=n[d.ib]*ub+n[d.ic]*uc,nw=-n[d.ib]*uc+n[d.ic]*ub,na=n[d.ia];
		float nu2=ct*nu+st/k*na,na2=-st*nu+ct/k*na;
		n[d.ib]=nu2*ub-nw*uc; n[d.ic]=nu2*uc+nw*ub; n[d.ia]=na2; vec_normalize3(n);
		p[d.ib]=d.c[d.ib]+u2*ub-w*uc; p[d.ic]=d.c[d.ic]+u2*uc+w*ub; p[d.ia]=d.c[d.ia]+a2;
	}
}

/* Stretch: the axis scales by 1 + stretch from the centre while the cross-section takes the opposite scale,
   1/sqrt(1 + stretch), strongest at the centre and gone at z = 1; amplify (-1..) strengthens or weakens it. */
void mesh_apply_stretch(Mesh *m,const deform_gizmo_t *g,float stretch,float amplify){
	deform_t d; if(!deform_setup(m,g,&d)) return;
	float sa=1.0f+stretch; if(sa<DEFORM_MIN_EXTENT) sa=DEFORM_MIN_EXTENT;
	float q=(1.0f/sqrtf(sa)-1.0f)*(1.0f+amplify);
	for(int i=0;i<m->nverts;i++){
		float *p=&m->verts[i].pos.x,*n=&m->verts[i].nrm.x,beyond;
		float a=deform_clamp(&d,p[d.ia]-d.c[d.ia],&beyond),z=fminf(fabsf(a/d.H),1.0f),sg=a<0?-1.0f:1.0f;
		float k=1.0f+q*(1.0f-z*z),dk=beyond!=0||z>=1.0f?0.0f:-2.0f*q*z*sg/d.H;
		float xb=p[d.ib]-d.c[d.ib],xc=p[d.ic]-d.c[d.ic];
		n[d.ia]=(n[d.ia]-dk*(xb*n[d.ib]+xc*n[d.ic])/k)/sa; n[d.ib]/=k; n[d.ic]/=k; vec_normalize3(n);
		p[d.ib]=d.c[d.ib]+xb*k; p[d.ic]=d.c[d.ic]+xc*k; p[d.ia]=d.c[d.ia]+a*sa+beyond;
	}
}

/* Skew: the cross-section slides amount (metres at z = 1) towards direction (degrees about the axis, as Bend). */
void mesh_apply_skew(Mesh *m,const deform_gizmo_t *g,float amount,float directionDeg){
	deform_t d; if(!deform_setup(m,g,&d)) return;
	float dir=directionDeg*M_PIf/180.0f,ub=cosf(dir),uc=sinf(dir);
	for(int i=0;i<m->nverts;i++){
		float *p=&m->verts[i].pos.x,*n=&m->verts[i].nrm.x,beyond;
		float a=deform_clamp(&d,p[d.ia]-d.c[d.ia],&beyond),o=amount*a/d.H,dO=beyond!=0?0.0f:amount/d.H;
		n[d.ia]-=dO*(ub*n[d.ib]+uc*n[d.ic]); vec_normalize3(n);
		p[d.ib]+=o*ub; p[d.ic]+=o*uc;
	}
}

void mesh_apply_array(Mesh *m, int count, vec3 off, vec3 rot){
	if(count<2) return;
	int ov=m->nverts, ot=m->ntris;
	for(int c=1;c<count;c++){
		mat4 step=mat4_mul(mat4_translate(v3(off.x*c,off.y*c,off.z*c)),
			mat4_rot_xyz(v3(rot.x*c,rot.y*c,rot.z*c)));
		for(int v=0;v<ov;v++){
			vec3 p=mat4_xform_point(step, m->verts[v].pos);
			vec3 n=mat4_xform_dir(mat4_rot_xyz(v3(rot.x*c,rot.y*c,rot.z*c)), m->verts[v].nrm);
			int copy=mesh_add_vert(m,p,n);
			m->verts[copy].u=m->verts[v].u; m->verts[copy].v=m->verts[v].v;
		}
		for(int t=0;t<ot;t++)
			mesh_add_tri(m,m->tris[t].a+c*ov,m->tris[t].b+c*ov,m->tris[t].c+c*ov);
	}
}

void mesh_apply_extrude(Mesh *m, float amount, char axis){
	int ov=m->nverts, ot=m->ntris;
	vec3 dir;
	if(axis=='x') dir=v3(amount,0,0);
	else if(axis=='y') dir=v3(0,amount,0);
	else dir=v3(0,0,amount);

	for(int v=0;v<ov;v++){
		vec3 p=vadd(m->verts[v].pos,dir);
		mesh_add_vert(m,p,m->verts[v].nrm);
	}
	for(int t=0;t<ot;t++)
		mesh_add_tri(m,m->tris[t].b+ov,m->tris[t].a+ov,m->tris[t].c+ov);

	mesh_build_edges(m);
	for(int e=0;e<m->nedges;e++){
		if(m->edges[e].t1>=0) continue;
		vec3 e0=m->verts[m->edges[e].v0].pos, e1=m->verts[m->edges[e].v1].pos;
		int v0=-1, v1=-1, nv0=-1, nv1=-1;
		for(int v=0;v<ov;v++){
			vec3 p=m->verts[v].pos;
			if(v0<0 && vlen(vsub(p,e0))<1e-4f) v0=v;
			if(v1<0 && vlen(vsub(p,e1))<1e-4f) v1=v;
			vec3 np=vadd(p,dir);
			if(nv0<0 && vlen(vsub(np,e0))<1e-4f) nv0=v+ov;
			if(nv1<0 && vlen(vsub(np,e1))<1e-4f) nv1=v+ov;
		}
		if(v0>=0 && v1>=0 && nv0>=0 && nv1>=0){
			mesh_add_tri(m,v0,v1,nv1);
			mesh_add_tri(m,v0,nv1,nv0);
		}
	}
}

/* Mirror, as Max's: reflect across the planes named by axes ("x", "xy", ...) placed offset (metres) from the
   origin along each, either moving the mesh (copy 0) or adding the reflection to it, welded at weld (metres).
   An odd number of planes turns the faces inside out, so their winding is reversed. */
void mesh_apply_mirror(Mesh *m,const char *axes,float offset,int copy,float weldThreshold){
	int flip[3]={strchr(axes,'x')!=NULL,strchr(axes,'y')!=NULL,strchr(axes,'z')!=NULL},odd=(flip[0]+flip[1]+flip[2])&1;
	if(!flip[0]&&!flip[1]&&!flip[2]) return;
	int ov=m->nverts, ot=m->ntris;
	for(int v=0;v<ov;v++){
		Vertex q=m->verts[v]; float *p=&q.pos.x,*n=&q.nrm.x;
		for(int k=0;k<3;k++) if(flip[k]){ p[k]=2.0f*offset-p[k]; n[k]=-n[k]; }
		if(copy){ int c=mesh_add_vert(m,q.pos,q.nrm); m->verts[c].u=q.u; m->verts[c].v=q.v; }
		else m->verts[v]=q;
	}
	if(!copy){ if(odd) for(int t=0;t<ot;t++){ int b=m->tris[t].b; m->tris[t].b=m->tris[t].c; m->tris[t].c=b; } return; }
	for(int t=0;t<ot;t++){
		Tri r=m->tris[t];
		if(odd) mesh_add_tri(m,r.b+ov,r.a+ov,r.c+ov); else mesh_add_tri(m,r.a+ov,r.b+ov,r.c+ov);
	}
	if(weldThreshold>0){
		int *weld=malloc(sizeof(int)*(size_t)m->nverts);
		for(int i=0;i<m->nverts;i++){
			weld[i]=i;
			for(int j=0;j<i;j++)
				if(vlen(vsub(m->verts[i].pos,m->verts[j].pos))<weldThreshold)
					{weld[i]=weld[j]; break;}
		}
		for(int t=0;t<m->ntris;t++){
			m->tris[t].a=weld[m->tris[t].a];
			m->tris[t].b=weld[m->tris[t].b];
			m->tris[t].c=weld[m->tris[t].c];
		}
		free(weld);
	}
}

/* Gradient noise on the integer lattice (Perlin's improved noise, with a hashed gradient per lattice point). */
static unsigned noise_lattice_hash(int x,int y,int z,unsigned seed){
	unsigned h=seed*NOISE_HASH_SEED^(unsigned)x*NOISE_PRIME_X^(unsigned)y*NOISE_PRIME_Y^(unsigned)z*NOISE_PRIME_Z;
	h^=h>>15; h*=NOISE_HASH_MIX; h^=h>>13; return h;
}
static float noise_grad(unsigned h,float x,float y,float z){
	switch(h&15){
		case 0: return x+y;  case 1: return -x+y; case 2: return x-y;  case 3: return -x-y;
		case 4: return x+z;  case 5: return -x+z; case 6: return x-z;  case 7: return -x-z;
		case 8: return y+z;  case 9: return -y+z; case 10: return y-z; case 11: return -y-z;
		case 12: return x+y; case 13: return -y+z; case 14: return -x+y; default: return -y-z;
	}
}
static float noise_fade(float t){ return t*t*t*(t*(t*6.0f-15.0f)+10.0f); }
static float noise_lerp(float a,float b,float t){ return a+(b-a)*t; }
static float noise3(float x,float y,float z,unsigned seed){
	int X=(int)floorf(x),Y=(int)floorf(y),Z=(int)floorf(z);
	float fx=x-X,fy=y-Y,fz=z-Z,u=noise_fade(fx),v=noise_fade(fy),w=noise_fade(fz),c[8];
	for(int k=0;k<8;k++){ int dx=k&1,dy=(k>>1)&1,dz=(k>>2)&1; c[k]=noise_grad(noise_lattice_hash(X+dx,Y+dy,Z+dz,seed),fx-dx,fy-dy,fz-dz); }
	return noise_lerp(noise_lerp(noise_lerp(c[0],c[1],u),noise_lerp(c[2],c[3],u),v),noise_lerp(noise_lerp(c[4],c[5],u),noise_lerp(c[6],c[7],u),v),w);
}

/* Noise, as Max's: each vertex moves by strength (metres per axis) times smooth noise sampled at its position over
   scale (metres; larger is smoother); fractal sums iterations octaves, each roughness times the last. The noise is
   a function of position, so corners a box repeats for each face move together and the surface stays sealed. */
void mesh_apply_noise(Mesh *m,int seed,float scale,vec3 strength,int fractal,float roughness,int iterations){
	if(scale<NOISE_MIN_SCALE) scale=NOISE_MIN_SCALE;
	int octaves=fractal?(iterations<1?1:iterations>NOISE_MAX_ITERATIONS?NOISE_MAX_ITERATIONS:iterations):1;
	float st[3]={strength.x,strength.y,strength.z};
	for(int i=0;i<m->nverts;i++){
		vec3 p=vscale(m->verts[i].pos,1.0f/scale); float *q=&m->verts[i].pos.x;
		for(int k=0;k<3;k++){
			float sum=0,amp=1,f=1,norm=0;
			for(int o=0;o<octaves;o++){ sum+=amp*noise3(p.x*f+k*NOISE_CHANNEL_SHIFT,p.y*f,p.z*f,(unsigned)seed+(unsigned)o); norm+=amp; amp*=roughness; f*=NOISE_LACUNARITY; }
			q[k]+=st[k]*sum/(norm>0?norm:1);
		}
	}
}

/* Shell, as Max's: the surface becomes a solid between outer (metres along each vertex's averaged normal) and
   inner (against it). The original faces move out to the outer skin, a reversed copy lines the inner one, and
   every open edge gets a rim quad joining the two. */
void mesh_apply_shell(Mesh *m,float inner,float outer){
	mesh_compute_face_normals(m);
	int ov=m->nverts, ot=m->ntris;
	vec3 *vNorm=calloc((size_t)(ov?ov:1),sizeof(vec3));
	for(int t=0;t<ot;t++){
		Tri tr=m->tris[t];
		vNorm[tr.a]=vadd(vNorm[tr.a],m->triN[t]); vNorm[tr.b]=vadd(vNorm[tr.b],m->triN[t]); vNorm[tr.c]=vadd(vNorm[tr.c],m->triN[t]);
	}
	for(int v=0;v<ov;v++){
		vec3 nn=vnorm(vNorm[v]),p=m->verts[v].pos;
		int c=mesh_add_vert(m,vsub(p,vscale(nn,inner)),vscale(m->verts[v].nrm,-1.0f));
		m->verts[c].u=m->verts[v].u; m->verts[c].v=m->verts[v].v;
		m->verts[v].pos=vadd(p,vscale(nn,outer));
	}
	for(int t=0;t<ot;t++) mesh_add_tri(m,m->tris[t].b+ov,m->tris[t].a+ov,m->tris[t].c+ov);
	mesh_build_edges(m);
	int ne=m->nedges; Edge *open=malloc(sizeof(Edge)*(size_t)(ne?ne:1)); int no=0;
	for(int e=0;e<ne;e++) if(m->edges[e].t1<0 && m->edges[e].t0<ot) open[no++]=m->edges[e];
	for(int e=0;e<no;e++){
		int v0=open[e].v0,v1=open[e].v1;   /* in t0's winding: the rim continues the outer face across the edge */
		mesh_add_tri(m,v1,v0,v0+ov); mesh_add_tri(m,v1,v0+ov,v1+ov);
	}
	free(open); free(vNorm);
}
