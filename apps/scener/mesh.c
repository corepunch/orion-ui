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
#define MESH_EDGE_WELD_MAX 1e-4f
#define MESH_EDGE_WELD_MIN_LENGTH 1e-8f
#define MESH_EDGE_WELD_FRACTION 0.25f
#define PROFILE_MIN_ELLIPSE_SEGMENTS 8
#define PROFILE_MAX_ELLIPSE_SEGMENTS 128
#define PROFILE_MIN_STAR_POINTS 3
#define PROFILE_MAX_STAR_POINTS 32
#define PROFILE_OFFSET_PARALLEL_EPSILON 1e-6f
#define PROFILE_TRIANGULATION_RATIO 1e-8f
#define PROFILE_TRIANGULATION_MIN_EPSILON 1e-16f

void mesh_free(Mesh *m){
    free(m->verts); free(m->tris); free(m->edges); free(m->triN);
    memset(m,0,sizeof(*m));
}
int mesh_add_vert(Mesh *m, vec3 p, vec3 n){
    Vertex v={0}; v.pos=p; v.nrm=n; DA_PUSH(m->verts,m->nverts,m->cverts,v); return m->nverts-1;
}
void mesh_add_tri(Mesh *m,int a,int b,int c){
    Tri t={a,b,c}; DA_PUSH(m->tris,m->ntris,m->ctris,t);
}
void mesh_transform(Mesh *m, mat4 posM, mat4 rotM){
    for(int i=0;i<m->nverts;i++){
        m->verts[i].pos = mat4_xform_point(posM, m->verts[i].pos);
        m->verts[i].nrm = vnorm(mat4_xform_dir(rotM, m->verts[i].nrm));
    }
}
void mesh_compute_face_normals(Mesh *m){
    if(!m->triN) m->triN = malloc(sizeof(vec3)*(size_t)m->ntris);
    for(int i=0;i<m->ntris;i++){
        Tri t=m->tris[i];
        vec3 a=m->verts[t.a].pos, b=m->verts[t.b].pos, c=m->verts[t.c].pos;
        m->triN[i] = vnorm(vcross(vsub(b,a),vsub(c,a)));
    }
}
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
    int *weld = malloc(sizeof(int)*(size_t)m->nverts);
    for(int i=0;i<m->nverts;i++){
        weld[i]=i;
        for(int j=0;j<i;j++){
            if(vlen(vsub(m->verts[i].pos,m->verts[j].pos)) < threshold){ weld[i]=weld[j]; break; }
        }
    }
    free(m->edges); m->edges=NULL; m->nedges=m->cedges=0;
    for(int i=0;i<m->ntris;i++){
        Tri t=m->tris[i];
        int pairs[3][2]={{t.a,t.b},{t.b,t.c},{t.c,t.a}};
        for(int e=0;e<3;e++){
            int v0=pairs[e][0], v1=pairs[e][1];
            int w0=weld[v0], w1=weld[v1];
            int found=-1;
            for(int k=0;k<m->nedges;k++){
                Edge *ed=&m->edges[k];
                if(ed->t1<0){
                    int ew0 = (weld[ed->v0]==w1);
                    int ew1 = (weld[ed->v1]==w0);
                    if(ew0 && ew1){ found=k; break; }
                }
            }
            if(found>=0){ m->edges[found].t1=i; }
            else{
                Edge ne={ m->verts[v0].pos, m->verts[v1].pos, i, -1, v0, v1 };
                DA_PUSH(m->edges,m->nedges,m->cedges,ne);
            }
        }
    }
    free(weld);
}
void mesh_update_edge_positions(Mesh *m){
    for(int i=0;i<m->nedges;i++){
        Edge *e=&m->edges[i];
        e->p0=m->verts[e->v0].pos;
        e->p1=m->verts[e->v1].pos;
    }
}

float mesh_signed_volume(Mesh *m){
    float vol = 0.0f;
    for(int i=0;i<m->ntris;i++){
        Tri t=m->tris[i];
        vec3 a=m->verts[t.a].pos, b=m->verts[t.b].pos, c=m->verts[t.c].pos;
        vol += vdot(vcross(a,b),c);
    }
    return vol / 6.0f;
}
void mesh_flip_winding(Mesh *m){
    for(int i=0;i<m->ntris;i++){ int t=m->tris[i].b; m->tris[i].b=m->tris[i].c; m->tris[i].c=t; }
    for(int i=0;i<m->nedges;i++){ vec3 p=m->edges[i].p0; m->edges[i].p0=m->edges[i].p1; m->edges[i].p1=p; int v=m->edges[i].v0; m->edges[i].v0=m->edges[i].v1; m->edges[i].v1=v; }
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

Mesh gen_rounded_box_beveled(float sx,float sy,float sz,float radius,float bevel,int segments,int bevel_segments){
	Shape2D profile=shape2d_rounded_rect(sx,sy,radius,segments);
	Mesh mesh=gen_profile_extrusion_beveled(&profile,sz,bevel,bevel_segments);
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
            vec3 n0=vnorm(v3(cosf(a0),0,sinf(a0))), n1=vnorm(v3(cosf(a1),0,sinf(a1)));
            int ib0=mesh_add_vert(&m,b0,n0), ib1=mesh_add_vert(&m,b1,n1);
            int it0=mesh_add_vert(&m,t0,n0), it1=mesh_add_vert(&m,t1,n1);
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
Mesh gen_cone(float rBase,float rTop,float h,int sides){
    return gen_cylinder_like(sides<3?4:sides, rBase, rTop, h, sides>=16);
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

Mesh gen_profile_extrusion_beveled(const Shape2D *profile,float depth,float bevel,int bevel_segments){
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
		if(!profile_offset(profile,bevel,inset)){ free(inset); return mesh; }
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

	for(int j=0;j<segments;j++){
		float a0=(float)j/segments*2.0f*M_PIf;
		float ca0=cosf(a0),sa0=sinf(a0);
		for(int i=0;i<n;i++){
			float px=profile->pts[i].x, py=profile->pts[i].y;
			vec3 nrm=vnorm(v3(
				profile->nrm[i].x*ca0,
				profile->nrm[i].y,
				profile->nrm[i].x*sa0));
			mesh_add_vert(&m,v3(px*ca0,py,px*sa0),nrm);
		}
	}

	for(int j=0;j<segments;j++){
		int jn=(j+1)%segments;
		for(int i=0;i<n-1;i++){
			int v00=j*n+i, v01=j*n+i+1;
			mesh_add_tri(&m,v00,v01,jn*n+i);
			mesh_add_tri(&m,v01,jn*n+i+1,jn*n+i);
		}
	}

	vec3 botN=v3(0,-1,0), topN=v3(0,1,0);
	int vBot=mesh_add_vert(&m,v3(0,profile->pts[0].y,0),botN);
	int vTop=mesh_add_vert(&m,v3(0,profile->pts[n-1].y,0),topN);
	for(int j=0;j<segments;j++){
		int jn=(j+1)%segments;
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

static void mesh_find_bounds(Mesh *m, char axis, float *minV, float *maxV){
	*minV=1e9f; *maxV=-1e9f;
	for(int i=0;i<m->nverts;i++){
		float v = axis=='x' ? m->verts[i].pos.x
		       : axis=='y' ? m->verts[i].pos.y : m->verts[i].pos.z;
		if(v<*minV) *minV=v; if(v>*maxV) *maxV=v;
	}
	if(*maxV-*minV < 1e-6f){ *minV=-0.5f; *maxV=0.5f; }
}

void mesh_apply_taper(Mesh *m, float amount, float curvature, char axis){
	float mn,mx; mesh_find_bounds(m,axis,&mn,&mx);
	float range=mx-mn;
	for(int i=0;i<m->nverts;i++){
		float *pa, *pb, *pc;
		switch(axis){
			case 'x': pa=&m->verts[i].pos.x; pb=&m->verts[i].pos.y; pc=&m->verts[i].pos.z; break;
			case 'y': pa=&m->verts[i].pos.y; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.z; break;
			default:  pa=&m->verts[i].pos.z; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.y; break;
		}
		float t=(*pa-mn)/range;
		float s=1.0f+amount*powf(2.0f*t-1.0f, curvature>0?curvature:1.0f);
		*pb*=s; *pc*=s;
	}
}

void mesh_apply_twist(Mesh *m, float angle_deg, char axis){
	float mn,mx; mesh_find_bounds(m,axis,&mn,&mx);
	float range=mx-mn;
	float rad=angle_deg*M_PIf/180.0f;
	for(int i=0;i<m->nverts;i++){
		float *pa, *pb, *pc, *nb, *nc;
		switch(axis){
			case 'x': pa=&m->verts[i].pos.x; pb=&m->verts[i].pos.y; pc=&m->verts[i].pos.z;
			          nb=&m->verts[i].nrm.y; nc=&m->verts[i].nrm.z; break;
			case 'y': pa=&m->verts[i].pos.y; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.z;
			          nb=&m->verts[i].nrm.x; nc=&m->verts[i].nrm.z; break;
			default:  pa=&m->verts[i].pos.z; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.y;
			          nb=&m->verts[i].nrm.x; nc=&m->verts[i].nrm.y; break;
		}
		float t=(*pa-mn)/range;
		float a=rad*t, ca=cosf(a), sa=sinf(a);
		float b=*pb, c=*pc;
		*pb=b*ca - c*sa; *pc=b*sa + c*ca;
		float n_b=*nb, n_c=*nc;
		*nb=n_b*ca - n_c*sa; *nc=n_b*sa + n_c*ca;
	}
}

void mesh_apply_bend(Mesh *m, float angle_deg, char axis){
	float mn,mx; mesh_find_bounds(m,axis,&mn,&mx);
	float range=mx-mn;
	float alpha=angle_deg*M_PIf/180.0f;
	float R=range/alpha;
	if(fabsf(alpha)<1e-6f) return;
	for(int i=0;i<m->nverts;i++){
		float *pa, *pb, *na, *nb;
		switch(axis){
			case 'x': pa=&m->verts[i].pos.x; pb=&m->verts[i].pos.y;
			          na=&m->verts[i].nrm.x; nb=&m->verts[i].nrm.y; break;
			case 'y': pa=&m->verts[i].pos.y; pb=&m->verts[i].pos.x;
			          na=&m->verts[i].nrm.y; nb=&m->verts[i].nrm.x; break;
			default:  pa=&m->verts[i].pos.z; pb=&m->verts[i].pos.x;
			          na=&m->verts[i].nrm.z; nb=&m->verts[i].nrm.x; break;
		}
		float along=*pa-mn;
		float theta=along/R;
		float cr=cosf(theta), sr=sinf(theta);
		float radial=*pb;
		*pb = (R+radial)*sr;
		*pa = mn + R - (R+radial)*cr;
		float n_al=*na, n_rd=*nb;
		*na = n_rd*sr + n_al*cr;
		*nb = n_rd*cr - n_al*sr;
	}
}

void mesh_apply_stretch(Mesh *m, float amount, float amplify, char axis){
	float mn,mx; mesh_find_bounds(m,axis,&mn,&mx);
	float range=mx-mn;
	for(int i=0;i<m->nverts;i++){
		float *pa, *pb, *pc;
		switch(axis){
			case 'x': pa=&m->verts[i].pos.x; pb=&m->verts[i].pos.y; pc=&m->verts[i].pos.z; break;
			case 'y': pa=&m->verts[i].pos.y; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.z; break;
			default:  pa=&m->verts[i].pos.z; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.y; break;
		}
		float t=(*pa-mn)/range;
		float s=amount*(t*t - t);
		*pb*=1.0f-s*amplify; *pc*=1.0f-s*amplify;
		*pa = mn + t*range*(1.0f+s);
	}
}

void mesh_apply_skew(Mesh *m, float amount, char axis){
	float mn,mx; mesh_find_bounds(m,axis,&mn,&mx);
	float range=mx-mn;
	for(int i=0;i<m->nverts;i++){
		float *pa, *pb, *pc;
		switch(axis){
			case 'x': pa=&m->verts[i].pos.x; pb=&m->verts[i].pos.y; pc=&m->verts[i].pos.z; break;
			case 'y': pa=&m->verts[i].pos.y; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.z; break;
			default:  pa=&m->verts[i].pos.z; pb=&m->verts[i].pos.x; pc=&m->verts[i].pos.y; break;
		}
		float t=(*pa-mn)/range;
		*pb+=amount*t; *pc+=amount*t;
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
		vec3 e0=m->edges[e].p0, e1=m->edges[e].p1;
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

void mesh_apply_mirror(Mesh *m, char axis, float weldThreshold){
	int ov=m->nverts, ot=m->ntris;

	for(int v=0;v<ov;v++){
		vec3 p=m->verts[v].pos, n=m->verts[v].nrm;
		if(axis=='x'){ p.x=-p.x; n.x=-n.x; }
		else if(axis=='y'){ p.y=-p.y; n.y=-n.y; }
		else{ p.z=-p.z; n.z=-n.z; }
		mesh_add_vert(m,p,n);
	}
	for(int t=0;t<ot;t++)
		mesh_add_tri(m,m->tris[t].b+ov,m->tris[t].a+ov,m->tris[t].c+ov);

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

void mesh_apply_noise(Mesh *m, float strength, int seed){
	unsigned int s=(unsigned int)seed;
	for(int i=0;i<m->nverts;i++){
		s=s*1103515245+12345;
		float rx=((float)(s&0xFFFF)/65535.0f-0.5f)*2.0f;
		s=s*1103515245+12345;
		float ry=((float)(s&0xFFFF)/65535.0f-0.5f)*2.0f;
		s=s*1103515245+12345;
		float rz=((float)(s&0xFFFF)/65535.0f-0.5f)*2.0f;
		m->verts[i].pos=vadd(m->verts[i].pos,v3(rx*strength,ry*strength,rz*strength));
	}
}

void mesh_apply_shell(Mesh *m, float amount){
	mesh_compute_face_normals(m);
	int ov=m->nverts, ot=m->ntris;
	vec3 *vNorm=calloc((size_t)ov,sizeof(vec3));
	int *vCount=calloc((size_t)ov,sizeof(int));
	for(int t=0;t<ot;t++){
		Tri tr=m->tris[t];
		vNorm[tr.a]=vadd(vNorm[tr.a],m->triN[t]);
		vNorm[tr.b]=vadd(vNorm[tr.b],m->triN[t]);
		vNorm[tr.c]=vadd(vNorm[tr.c],m->triN[t]);
		vCount[tr.a]++; vCount[tr.b]++; vCount[tr.c]++;
	}
	for(int v=0;v<ov;v++){
		if(vCount[v]>0) vNorm[v]=vnorm(vscale(vNorm[v],1.0f/(float)vCount[v]));
		vec3 p=vadd(m->verts[v].pos,vscale(vNorm[v],amount));
		mesh_add_vert(m,p,vscale(vNorm[v],-1.0f));
	}
	for(int t=0;t<ot;t++)
		mesh_add_tri(m,m->tris[t].b+ov,m->tris[t].a+ov,m->tris[t].c+ov);

	mesh_build_edges(m);
	for(int e=0;e<m->nedges;e++){
		if(m->edges[e].t1>=0) continue;
		vec3 e0=m->edges[e].p0, e1=m->edges[e].p1;
		int v0=-1, v1=-1, nv0=-1, nv1=-1;
		for(int v=0;v<ov;v++){
			vec3 p=m->verts[v].pos;
			if(v0<0 && vlen(vsub(p,e0))<1e-4f) v0=v;
			if(v1<0 && vlen(vsub(p,e1))<1e-4f) v1=v;
		}
		if(v0>=0 && v1>=0){
			for(int v=ov;v<m->nverts;v++){
				float dist0=vlen(vsub(m->verts[v].pos,m->verts[v0].pos));
				float dist1=vlen(vsub(m->verts[v].pos,m->verts[v1].pos));
				if(nv0<0 && dist0<1e-4f) nv0=v;
				if(nv1<0 && dist1<1e-4f) nv1=v;
			}
		}
		if(v0>=0 && v1>=0 && nv0>=0 && nv1>=0){
			mesh_add_tri(m,v0,v1,nv1);
			mesh_add_tri(m,v0,nv1,nv0);
		}
	}
	free(vNorm); free(vCount);
}
