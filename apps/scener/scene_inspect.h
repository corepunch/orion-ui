#ifndef __SCENE_INSPECT_H__
#define __SCENE_INSPECT_H__

/* scener --check and --query: geometry diagnostics and a JSON description of the built scene.
   Everything is reported in authored centimetres and axes, named by file:line. */

#define CHECK_CONTACT_CM 0.5f          /* a gap this small still reads as contact */
#define CHECK_PENETRATION_CM 1.0f      /* deeper interpenetration is reported */
#define CHECK_COPLANAR_CM 0.01f        /* faces this close share a depth-buffer plane and z-fight */
#define CHECK_COPLANAR_OVERLAP_CM 1.0f /* narrower shared-plane strips are butt joints hidden by a neighbour */
#define CHECK_COPLANAR_COS 0.9999f
#define CHECK_EDGE_SAMPLES 4           /* interior points tested on each triangle edge */
#define CHECK_LABEL_CAPACITY 768
#define JSON_ROUNDING 100.0f           /* report centimetres to two decimals */

typedef struct { Bounds b; int obj, unit, structural, renderable, ignore, emitter; } check_item_t;
typedef struct { int a, b; float depth; } check_pair_t;

static const char *xml_peek(XmlNode *n,const char *name){
	for(int i=0;i<n->nattrs;i++) if(!strcmp(n->attrs[i].name,name)) return n->attrs[i].resolved?n->attrs[i].resolved:n->attrs[i].value;
	return NULL;
}

static void node_label(Scene *s,XmlNode *n,char *out,size_t size){
	const char *name=xml_peek(n,"name"),*source=!strcmp(n->tag,"prefab")?xml_peek(n,"source"):NULL;
	const char *file=node_file(s,n);
	char where[SCENE_PREFAB_PATH_CAPACITY+32];
	if(!strcmp(file,s->scenePath)) snprintf(where,sizeof(where),"line %d",n->line);
	else snprintf(where,sizeof(where),"%s:%d",file,n->line);
	snprintf(out,size,"<%s%s%s%s%s%s%s> at %s",n->tag,name?" name=\"":"",name?name:"",name?"\"":"",
		source?" source=\"":"",source?source:"",source?"\"":"",where);
}

/* A group of primitives only is one authored assembly; a group holding prefabs or groups arranges a room. */
static int group_is_assembly(XmlNode *g){
	if(strcmp(g->tag,"group")) return 0;
	for(int i=0;i<g->nkids;i++) if(!strcmp(g->kids[i]->tag,"group") || !strcmp(g->kids[i]->tag,"prefab")) return 0;
	return 1;
}

/* Objects are compared between units: the innermost prefab instance, else an assembly group, else the shape itself.
   An open prefab document is one body, like an instance of it in a scene; its nested instances stay separate. */
static int record_unit(Scene *s,int obj){
	int r=s->objs[obj].record;
	if(r<0) return -2-obj;
	for(int k=r;k>=0;k=s->records[k].parent) if(!strcmp(((XmlNode*)s->records[k].node)->tag,"prefab")) return k;
	if(s->prefabDocument) return s->nrecords;
	int parent=s->records[r].parent;
	return parent>=0 && group_is_assembly((XmlNode*)s->records[parent].node)?parent:r;
}

static void unit_label(Scene *s,int unit,char *out,size_t size){
	if(unit==s->nrecords) snprintf(out,size,"the prefab body");
	else if(unit>=0) node_label(s,(XmlNode*)s->records[unit].node,out,size);
	else snprintf(out,size,"object %d",-2-unit);
}

static int bounds_valid(Bounds b){ return isfinite(b.min.x) && b.min.x<=b.max.x; }
static Bounds bounds_union(Bounds a,Bounds b){
	if(!bounds_valid(a)) return b;
	if(!bounds_valid(b)) return a;
	Bounds u={v3(fminf(a.min.x,b.min.x),fminf(a.min.y,b.min.y),fminf(a.min.z,b.min.z)),v3(fmaxf(a.max.x,b.max.x),fmaxf(a.max.y,b.max.y),fmaxf(a.max.z,b.max.z))};
	return u;
}
static Bounds bounds_empty(void){ Bounds b={v3(INFINITY,INFINITY,INFINITY),v3(-INFINITY,-INFINITY,-INFINITY)}; return b; }
static float bounds_gap(Bounds a,Bounds b){
	float gx=fmaxf(a.min.x-b.max.x,b.min.x-a.max.x),gy=fmaxf(a.min.y-b.max.y,b.min.y-a.max.y),gz=fmaxf(a.min.z-b.max.z,b.min.z-a.max.z);
	return fmaxf(gx,fmaxf(gy,gz));
}
static int point_in_bounds(vec3 p,Bounds b,float margin){
	return p.x>=b.min.x-margin && p.x<=b.max.x+margin && p.y>=b.min.y-margin && p.y<=b.max.y+margin && p.z>=b.min.z-margin && p.z<=b.max.z+margin;
}

static int mesh_closed(Mesh *m){
	if(!m->nedges) mesh_build_edges(m);
	if(!m->nedges) return 0;
	for(int i=0;i<m->nedges;i++) if(m->edges[i].t1<0) return 0;
	return 1;
}

static vec3 closest_on_triangle(vec3 p,vec3 a,vec3 b,vec3 c){
	vec3 ab=vsub(b,a),ac=vsub(c,a),ap=vsub(p,a);
	float d1=vdot(ab,ap),d2=vdot(ac,ap);
	if(d1<=0 && d2<=0) return a;
	vec3 bp=vsub(p,b); float d3=vdot(ab,bp),d4=vdot(ac,bp);
	if(d3>=0 && d4<=d3) return b;
	float vc=d1*d4-d3*d2;
	if(vc<=0 && d1>=0 && d3<=0) return vadd(a,vscale(ab,d1/(d1-d3)));
	vec3 cp=vsub(p,c); float d5=vdot(ab,cp),d6=vdot(ac,cp);
	if(d6>=0 && d5<=d6) return c;
	float vb=d5*d2-d1*d6;
	if(vb<=0 && d2>=0 && d6<=0) return vadd(a,vscale(ac,d2/(d2-d6)));
	float va=d3*d6-d5*d4;
	if(va<=0 && (d4-d3)>=0 && (d5-d6)>=0) return vadd(b,vscale(vsub(c,b),(d4-d3)/((d4-d3)+(d5-d6))));
	float denom=1/(va+vb+vc);
	return vadd(a,vadd(vscale(ab,vb*denom),vscale(ac,vc*denom)));
}

static float mesh_surface_distance(const Mesh *m,vec3 p){
	float best=INFINITY;
	for(int i=0;i<m->ntris;i++){
		vec3 q=closest_on_triangle(p,m->verts[m->tris[i].a].pos,m->verts[m->tris[i].b].pos,m->verts[m->tris[i].c].pos);
		float d=vlen(vsub(p,q)); if(d<best) best=d;
	}
	return best;
}

static Bounds tri_bounds(const Mesh *m,int t){
	Bounds b=bounds_empty();
	int ids[3]={m->tris[t].a,m->tris[t].b,m->tris[t].c};
	for(int k=0;k<3;k++){ vec3 p=m->verts[ids[k]].pos; Bounds q={p,p}; b=bounds_union(b,q); }
	return b;
}

static float point_penetration(const Mesh *b,Bounds bb,vec3 p){
	if(!point_in_bounds(p,bb,0) || !mesh_encloses(b,p)) return 0;
	return mesh_surface_distance(b,p);
}

/* Deepest sample of a inside the closed mesh b, in metres. Edges and faces are sampled as well as
   vertices, so a cylinder pushed through a slab is found although none of its vertices are inside. */
static float mesh_penetration(const Mesh *a,const Mesh *b,Bounds bb){
	float depth=0;
	for(int i=0;i<a->nverts;i++) depth=fmaxf(depth,point_penetration(b,bb,a->verts[i].pos));
	for(int t=0;t<a->ntris;t++){
		vec3 p[3]={a->verts[a->tris[t].a].pos,a->verts[a->tris[t].b].pos,a->verts[a->tris[t].c].pos};
		Bounds tb=tri_bounds(a,t);
		if(bounds_gap(tb,bb)>0) continue;
		depth=fmaxf(depth,point_penetration(b,bb,vscale(vadd(p[0],vadd(p[1],p[2])),1.0f/3)));
		for(int e=0;e<3;e++) for(int k=1;k<CHECK_EDGE_SAMPLES;k++)
			depth=fmaxf(depth,point_penetration(b,bb,lerp(p[e],p[(e+1)%3],(float)k/CHECK_EDGE_SAMPLES)));
	}
	return depth;
}

/* Two same-facing triangles on one plane overlap when no edge normal of either separates their 2D projections. */
static int triangles_overlap_2d(const float (*t)[2],const float (*u)[2],float minOverlap){
	const float (*tris[2])[2]={t,u};
	for(int which=0;which<2;which++) for(int e=0;e<3;e++){
		const float *p=tris[which][e],*q=tris[which][(e+1)%3];
		float nx=q[1]-p[1],ny=p[0]-q[0],len=sqrtf(nx*nx+ny*ny);
		if(len<=0) return 0;
		nx/=len; ny/=len;
		float amin=INFINITY,amax=-INFINITY,bmin=INFINITY,bmax=-INFINITY;
		for(int k=0;k<3;k++){
			float da=t[k][0]*nx+t[k][1]*ny,db=u[k][0]*nx+u[k][1]*ny;
			amin=fminf(amin,da); amax=fmaxf(amax,da); bmin=fminf(bmin,db); bmax=fmaxf(bmax,db);
		}
		if(fminf(amax,bmax)-fmaxf(amin,bmin)<minOverlap) return 0;
	}
	return 1;
}

static int meshes_share_plane(Mesh *a,Bounds ab,Mesh *b,Bounds bb){
	if(!a->triN) mesh_compute_face_normals(a);
	if(!b->triN) mesh_compute_face_normals(b);
	float near=CHECK_COPLANAR_CM/CM_PER_METRE,overlap=CHECK_COPLANAR_OVERLAP_CM/CM_PER_METRE;
	int *bt=malloc(sizeof(int)*(size_t)(b->ntris?b->ntris:1)),nb=0;
	for(int j=0;j<b->ntris;j++){ Bounds tb=tri_bounds(b,j); if(bounds_gap(tb,ab)<=near) bt[nb++]=j; }
	int found=0;
	for(int i=0;i<a->ntris && !found && nb;i++){
		Bounds ta=tri_bounds(a,i);
		if(bounds_gap(ta,bb)>near) continue;
		vec3 n=a->triN[i],a0=a->verts[a->tris[i].a].pos;
		int axis=fabsf(n.x)>fabsf(n.y)?(fabsf(n.x)>fabsf(n.z)?0:2):(fabsf(n.y)>fabsf(n.z)?1:2);
		int u=axis==0?1:0,w=axis==2?1:2;
		int ia[3]={a->tris[i].a,a->tris[i].b,a->tris[i].c};
		float pa[3][2];
		for(int k=0;k<3;k++){ float *c=&a->verts[ia[k]].pos.x; pa[k][0]=c[u]; pa[k][1]=c[w]; }
		for(int k=0;k<nb && !found;k++){
			int j=bt[k];
			if(vdot(n,b->triN[j])<CHECK_COPLANAR_COS) continue;
			if(fabsf(vdot(n,vsub(b->verts[b->tris[j].a].pos,a0)))>near || bounds_gap(ta,tri_bounds(b,j))>near) continue;
			int ib[3]={b->tris[j].a,b->tris[j].b,b->tris[j].c};
			float pb[3][2];
			for(int m=0;m<3;m++){ float *c=&b->verts[ib[m]].pos.x; pb[m][0]=c[u]; pb[m][1]=c[w]; }
			found=triangles_overlap_2d((const float (*)[2])pa,(const float (*)[2])pb,overlap);
		}
	}
	free(bt);
	return found;
}

static int check_item_cmp(const void *a,const void *b){
	float x=((const check_item_t*)a)->b.min.x,y=((const check_item_t*)b)->b.min.x;
	return (x>y)-(x<y);
}

static int check_is_structural(XmlNode *n){ return n && (!strcmp(n->tag,"wall") || !strcmp(n->tag,"floor")); }

static void check_geometry(Scene *s){
	if(!s->nobjs) return;
	float contact=CHECK_CONTACT_CM/CM_PER_METRE,penetration=CHECK_PENETRATION_CM/CM_PER_METRE;
	int up=s->worldUp.z>0.5f?2:1;
	check_item_t *items=calloc((size_t)s->nobjs,sizeof(*items)); int n=0;
	for(int i=0;i<s->nobjs;i++){
		SceneObj *o=&s->objs[i];
		if(!o->mesh.nverts) continue;
		check_item_t it={scene_obj_bounds(o),i,record_unit(s,i),check_is_structural((XmlNode*)o->sourceNode),o->renderable,o->sanityIgnore,o->unlit};
		items[n++]=it;
	}
	qsort(items,(size_t)n,sizeof(*items),check_item_cmp);
	int maxUnit=s->nrecords+1;
	char *touched=calloc((size_t)maxUnit+1,1);
	check_pair_t *hits=NULL; int nhits=0,chits=0;
	for(int i=0;i<n;i++) for(int j=i+1;j<n && items[j].b.min.x<=items[i].b.max.x+contact;j++){
		check_item_t *a=&items[i],*b=&items[j];
		float gap=bounds_gap(a->b,b->b);
		if(gap>contact) continue;
		if(a->unit!=b->unit){
			if(a->unit>=0) touched[a->unit]=1;
			if(b->unit>=0) touched[b->unit]=1;
		}
		if(a->ignore || b->ignore || !a->renderable || !b->renderable || (a->structural && b->structural)) continue;
		SceneObj *oa=&s->objs[a->obj],*ob=&s->objs[b->obj];
		char la[CHECK_LABEL_CAPACITY],lb[CHECK_LABEL_CAPACITY];
		if(a->unit!=b->unit && -gap>penetration){
			float depth=0;
			if(mesh_closed(&ob->mesh)) depth=fmaxf(depth,mesh_penetration(&oa->mesh,&ob->mesh,b->b));
			if(mesh_closed(&oa->mesh)) depth=fmaxf(depth,mesh_penetration(&ob->mesh,&oa->mesh,a->b));
			if(depth>penetration){
				int found=0;
				for(int k=0;k<nhits;k++) if((hits[k].a==a->unit && hits[k].b==b->unit) || (hits[k].a==b->unit && hits[k].b==a->unit)){
					hits[k].depth=fmaxf(hits[k].depth,depth); found=1;
				}
				if(!found){ check_pair_t h={a->unit,b->unit,depth}; DA_PUSH(hits,nhits,chits,h); }
			}
		}
		if(meshes_share_plane(&oa->mesh,a->b,&ob->mesh,b->b)){
			XmlNode *na=(XmlNode*)oa->sourceNode,*nb=(XmlNode*)ob->sourceNode;
			if(a->unit==b->unit && na && nb){ node_label(s,na,la,sizeof(la)); node_label(s,nb,lb,sizeof(lb)); }
			else { unit_label(s,a->unit,la,sizeof(la)); unit_label(s,b->unit,lb,sizeof(lb)); }
			scene_issue(s,SCENE_ISSUE_WARNING,"coplanar",na?node_file(s,na):s->scenePath,na?na->line:0,
				"%s and %s have overlapping faces on one plane and will z-fight; offset or resize one",la,lb);
		}
	}
	for(int k=0;k<nhits;k++){
		char la[CHECK_LABEL_CAPACITY],lb[CHECK_LABEL_CAPACITY];
		unit_label(s,hits[k].a,la,sizeof(la)); unit_label(s,hits[k].b,lb,sizeof(lb));
		int at=hits[k].a>=0 && hits[k].a<s->nrecords?hits[k].a:hits[k].b;
		XmlNode *node=at>=0 && at<s->nrecords?(XmlNode*)s->records[at].node:NULL;
		scene_issue(s,SCENE_ISSUE_WARNING,"intersection",node?node_file(s,node):s->scenePath,node?node->line:0,
			"%s penetrates %s by %.1f cm; separate them, or put one assembly in a <group> or prefab",la,lb,hits[k].depth*CM_PER_METRE);
	}
	free(hits);
	/* A unit is grounded when it reaches the ground plane or touches another unit's bounds.
	   Unlit emitters (bulbs, glints) are light sources, not objects that need support. */
	for(int u=0;u<maxUnit;u++){
		Bounds b=bounds_empty(); int visible=0,ignore=0;
		for(int i=0;i<n;i++) if(items[i].unit==u){ b=bounds_union(b,items[i].b); visible|=items[i].renderable && !items[i].emitter; ignore|=items[i].ignore; }
		if(!bounds_valid(b) || !visible || ignore || touched[u]) continue;
		float base=(&b.min.x)[up];
		if(base<=contact) continue;
		char label[CHECK_LABEL_CAPACITY]; unit_label(s,u,label,sizeof(label));
		XmlNode *node=u<s->nrecords?(XmlNode*)s->records[u].node:(XmlNode*)s->sceneRoot;
		scene_issue(s,SCENE_ISSUE_WARNING,"floating",node_file(s,node),node->line,
			"%s floats: its base is %.1f cm above the ground and it touches nothing",label,base*CM_PER_METRE);
	}
	free(touched); free(items);
}

/* ------------------------------------------------------------ JSON output */

static void json_str(FILE *f,const char *text){
	fputc('"',f);
	for(const unsigned char *c=(const unsigned char*)(text?text:"");*c;c++){
		if(*c=='"' || *c=='\\') fprintf(f,"\\%c",*c);
		else if(*c=='\n') fputs("\\n",f);
		else if(*c=='\t') fputs("\\t",f);
		else if(*c<0x20) fprintf(f,"\\u%04x",*c);
		else fputc(*c,f);
	}
	fputc('"',f);
}
static void json_num(FILE *f,float v){
	v=roundf(v*JSON_ROUNDING)/JSON_ROUNDING;
	fprintf(f,"%g",v==0?0.0f:v);
}
static void json_vec(FILE *f,vec3 v){
	fputc('[',f); json_num(f,v.x); fputs(", ",f); json_num(f,v.y); fputs(", ",f); json_num(f,v.z); fputc(']',f);
}
static vec3 author_point(Scene *s,vec3 p){ return vscale(cvt3ds_inv(s,p),CM_PER_METRE); }
static void json_bounds(FILE *f,Scene *s,Bounds b){
	if(!bounds_valid(b)){ fputs("null",f); return; }
	vec3 p=author_point(s,b.min),q=author_point(s,b.max);
	vec3 lo=v3(fminf(p.x,q.x),fminf(p.y,q.y),fminf(p.z,q.z)),hi=v3(fmaxf(p.x,q.x),fmaxf(p.y,q.y),fmaxf(p.z,q.z));
	fputs("{\"min\": ",f); json_vec(f,lo); fputs(", \"max\": ",f); json_vec(f,hi);
	fputs(", \"size\": ",f); json_vec(f,vsub(hi,lo)); fputc('}',f);
}

static PrefabDef *prefab_def(Scene *s,const char *source){
	for(int i=0;source && i<s->nprefabs;i++) if(!strcmp(s->prefabs[i].ref,source)) return &s->prefabs[i];
	return NULL;
}

/* Axis words such as "-y" become authored unit vectors. */
static int axis_vector(const char *text,vec3 *out){
	if(!text || (text[0]!='+' && text[0]!='-') || !text[1] || text[2]) return 0;
	float sign=text[0]=='-'?-1.0f:1.0f;
	switch(text[1]){
	case 'x': *out=v3(sign,0,0); return 1;
	case 'y': *out=v3(0,sign,0); return 1;
	case 'z': *out=v3(0,0,sign); return 1;
	}
	return 0;
}

static void json_attaches(FILE *f,Scene *s,XmlNode *root,mat4 M,int world){
	fputc('{',f); int first=1;
	for(int i=0;i<root->nkids;i++){
		XmlNode *k=root->kids[i];
		if(strcmp(k->tag,"attach")) continue;
		vec3 p=xml_attr_v3_cm(k,"pos",v3(0,0,0));
		if(world) p=mat4_xform_point(M,p);
		fputs(first?"":", ",f); first=0;
		json_str(f,xml_peek(k,"name")); fputs(": ",f); json_vec(f,world?author_point(s,p):vscale(p,CM_PER_METRE));
	}
	fputc('}',f);
}

static int record_is_ancestor(Scene *s,int ancestor,int r){
	for(;r>=0;r=s->records[r].parent) if(r==ancestor) return 1;
	return 0;
}

/* Local bounds of a prefab, measured on its first instance through that instance's inverse transform. */
static Bounds prefab_local_bounds(Scene *s,const char *source){
	Bounds b=bounds_empty();
	for(int r=0;r<s->nrecords;r++){
		XmlNode *n=(XmlNode*)s->records[r].node;
		if(strcmp(n->tag,"prefab") || !xml_peek(n,"source") || strcmp(xml_peek(n,"source"),source)) continue;
		mat4 inv=mat4_affine_inverse(s->records[r].transform);
		for(int i=0;i<s->nobjs;i++) if(record_is_ancestor(s,r,s->objs[i].record))
			for(int v=0;v<s->objs[i].mesh.nverts;v++){ vec3 p=mat4_xform_point(inv,s->objs[i].mesh.verts[v].pos); Bounds q={p,p}; b=bounds_union(b,q); }
		break;
	}
	return b;
}

static void json_prefab_axes(FILE *f,XmlNode *root,int up){
	const char *declared=xml_peek(root,"up"),*front=xml_peek(root,"front");
	if(up){ fputs(", \"up\": ",f); if(declared) json_str(f,declared); else fputs("null",f); }
	fputs(", \"front\": ",f); if(front) json_str(f,front); else fputs("null",f);
}

void scene_query(Scene *s,FILE *f){
	XmlNode *root=(XmlNode*)s->sceneRoot;
	Bounds *rb=malloc(sizeof(Bounds)*(size_t)(s->nrecords?s->nrecords:1)),all=bounds_empty();
	for(int r=0;r<s->nrecords;r++) rb[r]=bounds_empty();
	for(int i=0;i<s->nobjs;i++){
		Bounds b=scene_obj_bounds(&s->objs[i]);
		all=bounds_union(all,b);
		for(int r=s->objs[i].record;r>=0;r=s->records[r].parent) rb[r]=bounds_union(rb[r],b);
	}
	fputs("{\n  \"file\": ",f); json_str(f,s->scenePath);
	fputs(",\n  \"kind\": ",f); json_str(f,s->prefabDocument?"prefab":"scene");
	fputs(",\n  \"description\": ",f); json_str(f,s->description);
	fputs(",\n  \"units\": \"cm\"",f);
	if(s->prefabDocument) json_prefab_axes(f,root,1);
	else { fputs(", \"up\": ",f); json_str(f,s->convention3dsMax?"z":s->worldUp.z>0.5f?"z":"y"); }
	if(s->convention3dsMax) fputs(", \"convention\": \"3dsmax\"",f);
	fputs(",\n  \"bounds\": ",f); json_bounds(f,s,all);
	if(s->prefabDocument){ fputs(",\n  \"attach\": ",f); json_attaches(f,s,root,mat4_identity(),0); }
	fputs(",\n  \"cameras\": [",f);
	int ncameras=s->prefabDocument?0:s->ncameras;
	for(int i=0;i<ncameras;i++){
		Camera *c=&s->cameras[i];
		fputs(i?",\n    {\"name\": ":"\n    {\"name\": ",f); json_str(f,c->name);
		fputs(", \"pos\": ",f); json_vec(f,author_point(s,c->pos)); fputs(", \"look\": ",f); json_vec(f,author_point(s,c->look));
		fputs(", \"fov\": ",f); json_num(f,c->fov); fputc('}',f);
	}
	fputs(ncameras?"\n  ],\n  \"lights\": [":"],\n  \"lights\": [",f);
	int nlights=s->defaultLights?0:s->nlights;
	for(int i=0;i<nlights;i++){
		Light *l=&s->lights[i];
		fputs(i?",\n    {\"kind\": ":"\n    {\"kind\": ",f); json_str(f,l->isDirectional?"sun":"point");
		if(l->isDirectional){ fputs(", \"dir\": ",f); json_vec(f,cvt3ds_inv(s,l->dir)); }
		else { fputs(", \"pos\": ",f); json_vec(f,author_point(s,l->pos)); fputs(", \"radius\": ",f); json_num(f,l->radius*CM_PER_METRE); }
		fputs(", \"color\": ",f); json_vec(f,l->color); fputs(", \"intensity\": ",f); json_num(f,l->intensity);
		fprintf(f,", \"castShadows\": %s}",l->castsShadow?"true":"false");
	}
	fputs(nlights?"\n  ],\n  \"materials\": [":"],\n  \"materials\": [",f);
	for(int i=0;i<s->nmats;i++){
		fputs(i?", ":"",f); fputs("{\"id\": ",f); json_str(f,s->mats[i].id);
		fputs(", \"color\": ",f); json_vec(f,s->mats[i].color); fputs(", \"shininess\": ",f); json_num(f,s->mats[i].shininess);
		if(s->mats[i].ramp>=0){
			fputs(", \"ramp\": [",f);
			for(int k=0;k<MATERIAL_RAMP_SHADES;k++){ fputs(k?", ":"",f); json_vec(f,s->ramps[s->mats[i].ramp].shade[k]); }
			fputc(']',f);
		}
		fputc('}',f);
	}
	fputs("],\n  \"prefabs\": {",f);
	for(int i=0;i<s->nprefabs;i++){
		PrefabDef *p=&s->prefabs[i];
		fputs(i?",\n    ":"\n    ",f); json_str(f,p->ref); fputs(": {\"file\": ",f); json_str(f,p->path);
		fputs(", \"description\": ",f); json_str(f,p->description);
		json_prefab_axes(f,(XmlNode*)p->root,1);
		fputs(", \"bounds\": ",f); json_bounds(f,s,prefab_local_bounds(s,p->ref));
		fputs(", \"attach\": ",f); json_attaches(f,s,(XmlNode*)p->root,mat4_identity(),0); fputc('}',f);
	}
	fputs(s->nprefabs?"\n  },\n  \"nodes\": [":"},\n  \"nodes\": [",f);
	for(int r=0;r<s->nrecords;r++){
		SceneRecord *rec=&s->records[r]; XmlNode *n=(XmlNode*)rec->node;
		const char *name=xml_peek(n,"name"),*source=!strcmp(n->tag,"prefab")?xml_peek(n,"source"):NULL;
		fprintf(f,"%s{\"id\": %d, \"parent\": %d, \"tag\": ",r?",\n    ":"\n    ",r,rec->parent); json_str(f,n->tag);
		if(name){ fputs(", \"name\": ",f); json_str(f,name); }
		if(source){ fputs(", \"source\": ",f); json_str(f,source); }
		fputs(", \"file\": ",f); json_str(f,node_file(s,n)); fprintf(f,", \"line\": %d",n->line);
		fputs(", \"origin\": ",f); json_vec(f,author_point(s,mat4_xform_point(rec->transform,v3(0,0,0))));
		fputs(", \"bounds\": ",f); json_bounds(f,s,rb[r]);
		PrefabDef *def=source?prefab_def(s,source):NULL;
		if(def){
			vec3 front;
			if(axis_vector(xml_peek((XmlNode*)def->root,"front"),&front)){ fputs(", \"front\": ",f); json_vec(f,vnorm(cvt3ds_inv(s,mat4_xform_dir(rec->transform,front)))); }
			fputs(", \"attach\": ",f); json_attaches(f,s,(XmlNode*)def->root,rec->transform,1);
		}
		fputc('}',f);
	}
	fputs(s->nrecords?"\n  ]\n}\n":"]\n}\n",f);
	free(rb);
}

/* Geometry is judged in the built view: the active camera's transforms, poses and time. */
void scene_check_geometry(Scene *s,const char *camera){
	snprintf(s->checkCamera,sizeof(s->checkCamera),"%s",camera?camera:"");
	check_geometry(s);
	s->checkCamera[0]=0;
}

SceneCheckResult scene_check(Scene *s,FILE *f,int json){
	scene_check_geometry(s,NULL);
	return scene_check_report(s,f,json);
}

SceneCheckResult scene_check_report(Scene *s,FILE *f,int json){
	SceneCheckResult result={0,0};
	for(int i=0;i<s->nissues;i++) if(s->issues[i].severity==SCENE_ISSUE_ERROR) result.errors++; else result.warnings++;
	if(!json){
		fprintf(f,"check %s: %d error%s, %d warning%s (%d objects, %d nodes)\n",s->scenePath,result.errors,result.errors==1?"":"s",
			result.warnings,result.warnings==1?"":"s",s->nobjs,s->nrecords);
		return result;
	}
	fputs("{\n  \"file\": ",f); json_str(f,s->scenePath);
	fprintf(f,",\n  \"errors\": %d, \"warnings\": %d, \"objects\": %d, \"nodes\": %d,\n  \"issues\": [",result.errors,result.warnings,s->nobjs,s->nrecords);
	for(int i=0;i<s->nissues;i++){
		SceneIssue *issue=&s->issues[i];
		fputs(i?",\n    {\"severity\": ":"\n    {\"severity\": ",f); json_str(f,issue->severity==SCENE_ISSUE_ERROR?"error":"warning");
		fputs(", \"kind\": ",f); json_str(f,issue->kind); fputs(", \"file\": ",f); json_str(f,issue->file);
		fprintf(f,", \"line\": %d, \"message\": ",issue->line); json_str(f,issue->message);
		if(issue->camera[0]){ fputs(", \"camera\": ",f); json_str(f,issue->camera); }
		fputc('}',f);
	}
	fputs(s->nissues?"\n  ]\n}\n":"]\n}\n",f);
	return result;
}

#endif
