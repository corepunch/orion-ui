#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include "simplegl.h"

void build_shadow_volume(Mesh *m, vec3 lightPos, vec3 lightDir, int isDir, ShadowVolume *sv){
	memset(sv,0,sizeof(*sv));
	int nt=m->ntris;
	char *facing=alloca((size_t)nt);
	Light light={.pos=lightPos,.dir=lightDir,.isDirectional=isDir};
	for(int i=0;i<nt;i++){
		vec3 p=m->verts[m->tris[i].a].pos;
		vec3 toLight=light_to_source(&light,p);
		facing[i] = vdot(m->triN[i], toLight) > 0.0f;
	}
	#define PUSHP(vv) DA_PUSH(sv->verts,sv->nverts,sv->cverts,((ShadowVertex){(vv).x,(vv).y,(vv).z,1.0f}))
	#define PUSHD(vv) \
		if(isDir) DA_PUSH(sv->verts,sv->nverts,sv->cverts,((ShadowVertex){(vv).x+lightDir.x*1000.0f,(vv).y+lightDir.y*1000.0f,(vv).z+lightDir.z*1000.0f,1.0f})); \
		else DA_PUSH(sv->verts,sv->nverts,sv->cverts,((ShadowVertex){(vv).x-lightPos.x,(vv).y-lightPos.y,(vv).z-lightPos.z,0.0f}))

	for(int i=0;i<m->nedges;i++){
		Edge *e=&m->edges[i];
		int f0=facing[e->t0], f1=(e->t1>=0)?facing[e->t1]:0;
		int silhouette = (e->t1<0) ? f0 : (f0!=f1);
		if(!silhouette) continue;
		vec3 v0,v1;
		vec3 p0=m->verts[e->v0].pos,p1=m->verts[e->v1].pos;
		if(f0){ v0=p0; v1=p1; } else { v0=p1; v1=p0; }
#ifdef USE_ZPASS
		PUSHP(v0); PUSHP(v1); PUSHD(v1);
		PUSHP(v0); PUSHD(v1); PUSHD(v0);
#else
		PUSHP(v1); PUSHP(v0); PUSHD(v0);
		PUSHP(v1); PUSHD(v0); PUSHD(v1);
#endif
	}

#ifndef USE_ZPASS
	for(int i=0;i<nt;i++){
		Tri t=m->tris[i];
		vec3 a=m->verts[t.a].pos, b=m->verts[t.b].pos, c=m->verts[t.c].pos;
		if(facing[i]){ PUSHP(a); PUSHP(b); PUSHP(c); }
		else { PUSHD(a); PUSHD(b); PUSHD(c); }
	}
#endif

	#undef PUSHP
	#undef PUSHD
}

/* Doom 3's shadow volumes without vertices of their own: vertex i of the mesh is 2i at its place and 2i+1
   pushed away from the light (the shadow shader does the pushing), so a volume is just indices. Sides of the
   silhouette come first; the light-facing cap and the far cap follow, needed only when the view may be inside
   the volume (z-fail). Same windings as build_shadow_volume. */
int build_shadow_indices(const Mesh *m,vec3 lightPos,vec3 lightDir,int isDir,unsigned **out,int *nsides){
	int nt=m->ntris;
	char *facing=malloc((size_t)(nt?nt:1));
	Light light={.pos=lightPos,.dir=lightDir,.isDirectional=isDir};
	int nsil=0,nface=0;
	for(int i=0;i<nt;i++){
		vec3 p=m->verts[m->tris[i].a].pos;
		facing[i]=vdot(m->triN[i],light_to_source(&light,p))>0.0f;
		nface+=facing[i];
	}
	for(int i=0;i<m->nedges;i++){
		const Edge *e=&m->edges[i];
		int f0=facing[e->t0],f1=(e->t1>=0)?facing[e->t1]:0;
		if((e->t1<0)?f0:(f0!=f1)) nsil++;
	}
	unsigned *ix=malloc(sizeof(unsigned)*(size_t)(nsil*6+nt*3+1)); int n=0;
	for(int i=0;i<m->nedges;i++){
		const Edge *e=&m->edges[i];
		int f0=facing[e->t0],f1=(e->t1>=0)?facing[e->t1]:0;
		if(!((e->t1<0)?f0:(f0!=f1))) continue;
		unsigned v0=f0?(unsigned)e->v0:(unsigned)e->v1,v1=f0?(unsigned)e->v1:(unsigned)e->v0;
		unsigned P0=2*v0,P1=2*v1,D0=2*v0+1,D1=2*v1+1;
		ix[n++]=P1; ix[n++]=P0; ix[n++]=D0;
		ix[n++]=P1; ix[n++]=D0; ix[n++]=D1;
	}
	*nsides=n;
	for(int i=0;i<nt;i++){
		unsigned a=(unsigned)m->tris[i].a,b=(unsigned)m->tris[i].b,c=(unsigned)m->tris[i].c,far=facing[i]?0u:1u;
		ix[n++]=2*a+far; ix[n++]=2*b+far; ix[n++]=2*c+far;
	}
	(void)nface;
	free(facing);
	*out=ix;
	return n;
}

void shadow_volume_release(ShadowVolume *sv){
	free(sv->verts); free(sv->obj); free(sv->first); free(sv->count);
	memset(sv,0,sizeof(*sv));
}

void scene_obj_update_bounds(SceneObj *o){
	vec3 lo=v3(INFINITY,INFINITY,INFINITY),hi=v3(-INFINITY,-INFINITY,-INFINITY);
	for(int i=0;o->mesh && i<o->mesh->nverts;i++){
		vec3 p=mat4_xform_point(o->model,o->mesh->verts[i].pos);
		lo=v3(fminf(lo.x,p.x),fminf(lo.y,p.y),fminf(lo.z,p.z)); hi=v3(fmaxf(hi.x,p.x),fmaxf(hi.y,p.y),fmaxf(hi.z,p.z));
	}
	o->bmin=lo; o->bmax=hi;
}

void scene_obj_world_mesh(const SceneObj *o,Mesh *out){
	memset(out,0,sizeof(*out));
	const Mesh *m=o->mesh;
	if(!m||!m->nverts) return;
	out->verts=malloc(sizeof(Vertex)*(size_t)m->nverts); out->nverts=out->cverts=m->nverts;
	for(int i=0;i<m->nverts;i++){
		Vertex v=m->verts[i];
		v.pos=mat4_xform_point(o->model,v.pos);
		v.nrm=vnorm(mat4_xform_dir(o->normalM,v.nrm));
		out->verts[i]=v;
	}
	out->tris=malloc(sizeof(Tri)*(size_t)(m->ntris?m->ntris:1)); out->ntris=out->ctris=m->ntris;
	memcpy(out->tris,m->tris,sizeof(Tri)*(size_t)m->ntris);
	if(o->mirrored) for(int i=0;i<out->ntris;i++){ int t=out->tris[i].b; out->tris[i].b=out->tris[i].c; out->tris[i].c=t; }
	if(m->nedges){
		out->edges=malloc(sizeof(Edge)*(size_t)m->nedges); out->nedges=out->cedges=m->nedges;
		memcpy(out->edges,m->edges,sizeof(Edge)*(size_t)m->nedges);
		if(o->mirrored) for(int i=0;i<out->nedges;i++){ int v=out->edges[i].v0; out->edges[i].v0=out->edges[i].v1; out->edges[i].v1=v; }
	}
	mesh_compute_face_normals(out);
}

int light_reaches_obj(const Light *L,const SceneObj *o){
	if(L->isDirectional || L->range<=0 || !o->mesh || !o->mesh->nverts) return 1;
	float dx=fmaxf(fmaxf(o->bmin.x-L->pos.x,L->pos.x-o->bmax.x),0),dy=fmaxf(fmaxf(o->bmin.y-L->pos.y,L->pos.y-o->bmax.y),0),dz=fmaxf(fmaxf(o->bmin.z-L->pos.z,L->pos.z-o->bmax.z),0);
	return dx*dx+dy*dy+dz*dz<=L->range*L->range;
}

/* Shadow volumes are built when a view first needs them, per light and caster, and kept on the GPU
   (render.c), as Doom 3 creates interactions on demand; the scene only says that every cached one is stale. */
void scene_build_all_shadow_volumes(Scene *s){
	for(int oi=0;oi<s->nobjs;oi++) scene_obj_update_bounds(&s->objs[oi]);
	s->shadowRev++;
}

void scene_rebuild_node_shadow_volumes(Scene *s,void *editNode){
	for(int oi=0;oi<s->nobjs;oi++) if(s->objs[oi].editNode==editNode) scene_obj_update_bounds(&s->objs[oi]);
	s->shadowRev++;
}
