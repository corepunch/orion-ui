#include <orion/user/gl_compat.h>
#include <orion/ui.h>
#include <orion/user/image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "simplegl.h"
#include "materials.h"

#define SURFACE_EPSILON 0.000001f
#define SURFACE_NAME_CAPACITY 64
#define WALL_DEFAULT_LENGTH 4.0f
#define WALL_DEFAULT_HEIGHT 2.7f
#define WALL_DEFAULT_THICKNESS 0.2f
#define WALL_DEFAULT_TRIM_DEPTH 0.02f
#define FLOOR_DEFAULT_WIDTH 4.0f
#define FLOOR_DEFAULT_DEPTH 4.0f
#define FLOOR_DEFAULT_THICKNESS 0.18f
#define FLOOR_DEFAULT_TILE_DEPTH 0.02f
#define FLOOR_DEFAULT_TILE_WIDTH 0.2f
#define FLOOR_DEFAULT_TILE_LENGTH 1.2f
#define FLOOR_DEFAULT_GAP 0.002f
#define FLOOR_DEFAULT_VARIATION 0.1f
#define FLOOR_MAX_CELLS 10000
#define FLOOR_PLANE_ROTATION 90.0f
#define FLOOR_HEX_SIDES 6
#define FLOOR_HEX_ROW_STEP 1.5f
#define FLOOR_SQRT_THREE 1.7320508075688772f
#define FLOOR_STONE_BEVEL 0.16f
#define FLOOR_STONE_JITTER 0.22f
#define FLOOR_HASH_ROW 0x9e3779b9u
#define FLOOR_HASH_COLUMN 0x85ebca6bu
#define FLOOR_HASH_MIX 0x7feb352du
#define FLOOR_HASH_SHIFT 16
#define FLOOR_HASH_MASK 0x00ffffffu

#define PICK_TRIANGLE_EPSILON 0.00000001f
#define WINDOW_Z_UP_ROTATION "90 0 0"
#define WINDOW_VALUE_CAPACITY 32
#define WINDOW_DEFAULT_WIDTH 1.2f
#define WINDOW_DEFAULT_HEIGHT 1.8f
#define WINDOW_COTTAGE_HEIGHT 1.4f
#define WINDOW_GOTHIC_HEIGHT 2.2f
#define WINDOW_FRAME_RATIO 0.06f
#define WINDOW_STORYBOOK_FRAME_RATIO 0.10f
#define WINDOW_DEPTH_RATIO 1.5f
#define WINDOW_PANE_DEPTH 0.02f
#define WINDOW_DEFAULT_SEGMENTS 32
#define WINDOW_MIN_SEGMENTS 8
#define WINDOW_MAX_SEGMENTS 128
#define WINDOW_JOIN_OVERLAP 0.0001f
#define WINDOW_EPSILON 0.000001f
#define WINDOW_ALIGNMENT_EPSILON 0.0001f
#define DOOR_DEFAULT_WIDTH 1.0f
#define DOOR_DEFAULT_HEIGHT 2.1f
#define DOOR_LEAF_DEPTH 0.04f
#define DOOR_CLEARANCE 0.002f
#define DOOR_MAX_ANGLE 180.0f
#define DOOR_HANDLE_RADIUS_RATIO 0.025f
#define DOOR_HANDLE_INSET_RATIO 0.12f
#define DOOR_HANDLE_HEIGHT_RATIO 0.45f
#define DOOR_HANDLE_SEGMENTS 16
#define DOOR_WINDOW_WIDTH_RATIO 0.40f
#define DOOR_WINDOW_HEIGHT_RATIO 0.28f
#define DOOR_WINDOW_CENTER_RATIO 0.70f
#define MAX_DOOR_OPENINGS 2
#define RIG_EPSILON 0.00001f
#define RIG_REACH_MARGIN 0.0001f

/* -------------------------------------------------------------- Tiny XML */

typedef struct XmlAttr { char *name, *value; } XmlAttr;
typedef struct XmlNode {
	char *tag;
	XmlAttr *attrs; int nattrs,cattrs;
	struct XmlNode *parent;
	struct XmlNode **kids; int nkids,ckids;
} XmlNode;

static XmlNode* xml_new(const char*tag){
	XmlNode *n=calloc(1,sizeof(XmlNode)); n->tag=strdup(tag); return n;
}
static void xml_free(XmlNode*n){
	if(!n) return;
	for(int i=0;i<n->nattrs;i++){ free(n->attrs[i].name); free(n->attrs[i].value); }
	free(n->attrs);
	for(int i=0;i<n->nkids;i++) xml_free(n->kids[i]);
	free(n->kids); free(n->tag); free(n);
}
static const char* xml_attr(XmlNode*n,const char*name,const char*def){
	for(int i=0;i<n->nattrs;i++) if(!strcmp(n->attrs[i].name,name)) return n->attrs[i].value;
	return def;
}
const char *scene_node_tag(const void *node){
	const XmlNode *n=(const XmlNode*)node;
	return n?n->tag:NULL;
}
const char *scene_node_attr(const void *node,const char *name){
	XmlNode *n=(XmlNode*)node;
	return n&&name?xml_attr(n,name,NULL):NULL;
}
int scene_node_attr_count(const void *node){
	const XmlNode *n=(const XmlNode*)node;
	return n?n->nattrs:0;
}
const char *scene_node_attr_name(const void *node,int index){
	const XmlNode *n=(const XmlNode*)node;
	return n&&index>=0&&index<n->nattrs?n->attrs[index].name:NULL;
}
const char *scene_node_attr_value(const void *node,int index){
	const XmlNode *n=(const XmlNode*)node;
	return n&&index>=0&&index<n->nattrs?n->attrs[index].value:NULL;
}
static vec3 xml_attr_v3(XmlNode*n,const char*name,vec3 def){
	const char*s=xml_attr(n,name,NULL); if(!s) return def;
	vec3 v=def; sscanf(s,"%f %f %f",&v.x,&v.y,&v.z); return v;
}
static float xml_attr_f(XmlNode*n,const char*name,float def){
	const char*s=xml_attr(n,name,NULL); return s? strtof(s,NULL): def;
}
static int xml_attr_i(XmlNode*n,const char*name,int def){
	const char*s=xml_attr(n,name,NULL); return s? atoi(s): def;
}
static float xml_attr_f_cm(XmlNode*n,const char*name,float def){
	const char*s=xml_attr(n,name,NULL); return s? strtof(s,NULL)*0.01f: def;
}
static vec3 xml_attr_v3_cm(XmlNode*n,const char*name,vec3 def){
	const char*s=xml_attr(n,name,NULL); if(!s) return def;
	vec3 v=def; sscanf(s,"%f %f %f",&v.x,&v.y,&v.z);
	v.x*=0.01f; v.y*=0.01f; v.z*=0.01f; return v;
}
static void xml_set_attr(XmlNode *n,const char *name,const char *value){
	for(int i=0;i<n->nattrs;i++) if(!strcmp(n->attrs[i].name,name)){
		free(n->attrs[i].value); n->attrs[i].value=strdup(value); return;
	}
	XmlAttr a={strdup(name),strdup(value)};
	DA_PUSH(n->attrs,n->nattrs,n->cattrs,a);
}
static void xml_set_attr_v3(XmlNode *n,const char *name,vec3 v){
	char value[96];
	snprintf(value,sizeof(value),"%.6g %.6g %.6g",v.x,v.y,v.z);
	xml_set_attr(n,name,value);
}
static void xml_set_attr_v3_cm(XmlNode *n,const char *name,vec3 v){
	xml_set_attr_v3(n,name,vscale(v,100.0f));
}
/* Explicit 3ds Max scenes use X=east, Y=north(depth), Z=up.
   The renderer uses X=east, Y=up, Z=depth(-north).
   pos/dir/rot: (x,y,z) → (x, z, -y)    size: (x,y,z) → (x, z, y) */
static inline vec3 cvt3ds(Scene *s,vec3 v)   { return s->convention3dsMax?v3(v.x, v.z, -v.y):v; }
static inline vec3 cvt3ds_sz(Scene *s,vec3 v){ return s->convention3dsMax?v3(v.x, v.z, v.y):v; }
static inline vec3 cvt3ds_inv(Scene *s,vec3 v){ return s->convention3dsMax?v3(v.x,-v.z, v.y):v; } /* world → 3dsmax */

static mat4 xml_node_transform(Scene *s,XmlNode *n){
	vec3 pos=cvt3ds(s,xml_attr_v3_cm(n,"pos",v3(0,0,0)));
	vec3 rot=cvt3ds(s,xml_attr_v3(n,"rot",v3(0,0,0)));
	vec3 scl=xml_attr_v3(n,"scale",v3(1,1,1));
	vec3 pvt=xml_attr_v3_cm(n,"pivotOffset",v3(0,0,0));
	return mat4_mul(mat4_translate(pos),mat4_mul(mat4_translate(pvt),
		mat4_mul(mat4_rot_xyz(rot),mat4_mul(mat4_translate(vscale(pvt,-1.0f)),mat4_scale(scl)))));
}
static int xml_attr_2f(XmlNode*n,const char*name,float defX,float defY,float *outX,float *outY){
	const char*s=xml_attr(n,name,NULL);
	if(!s){ *outX=defX; *outY=defY; return 0; }
	*outX=defX; *outY=defY;
	int count=sscanf(s,"%f %f",outX,outY);
	if(count==1) *outY=*outX;
	return count>0;
}
static void xp_skip_ws(const char**p){ while(**p && isspace((unsigned char)**p)) (*p)++; }

static XmlNode* xml_parse_node(const char **p);

static void xml_parse_children(const char **p, XmlNode *parent){
	for(;;){
		xp_skip_ws(p);
		if(!**p) return;
		if(!strncmp(*p,"</",2)){ return; }
		if(!strncmp(*p,"<!--",4)){
			const char *end=strstr(*p,"-->");
			*p = end? end+3 : *p+strlen(*p);
			continue;
		}
		if(**p=='<'){
			XmlNode *child=xml_parse_node(p);
			if(!child) return;
			child->parent=parent;
			DA_PUSH(parent->kids,parent->nkids,parent->ckids,child);
			continue;
		}
		while(**p && **p!='<') (*p)++;
	}
}
static XmlNode* xml_parse_node(const char **p){
	xp_skip_ws(p);
	if(**p!='<') return NULL;
	if(!strncmp(*p,"<?",2)){ const char*e=strstr(*p,"?>"); *p=e?e+2:*p+strlen(*p); return xml_parse_node(p); }
	if(!strncmp(*p,"<!--",4)){ const char*e=strstr(*p,"-->"); *p=e?e+3:*p+strlen(*p); return xml_parse_node(p); }
	(*p)++;
	char tag[64]; int ti=0;
	while(**p && !isspace((unsigned char)**p) && **p!='>' && **p!='/' && ti<63) tag[ti++]=*(*p)++;
	tag[ti]=0;
	XmlNode *n=xml_new(tag);
	for(;;){
		xp_skip_ws(p);
		if(!**p) return n;
		if(**p=='/' && (*p)[1]=='>'){ (*p)+=2; return n; }
		if(**p=='>'){ (*p)++; break; }
		char aname[64]; int ai=0;
		while(**p && **p!='=' && !isspace((unsigned char)**p) && **p!='>' && **p!='/' && ai<63) aname[ai++]=*(*p)++;
		aname[ai]=0;
		xp_skip_ws(p);
		char aval[256]={0};
		if(**p=='='){
			(*p)++; xp_skip_ws(p);
			if(**p=='"'||**p=='\''){
				char q=*(*p)++; int vi=0;
				while(**p && **p!=q && vi<255) aval[vi++]=*(*p)++;
				if(**p==q) (*p)++;
				aval[vi]=0;
			}
		}
		if(ai>0){
			XmlAttr a={ strdup(aname), strdup(aval) };
			DA_PUSH(n->attrs,n->nattrs,n->cattrs,a);
		}
	}
	xml_parse_children(p, n);
	xp_skip_ws(p);
	if(!strncmp(*p,"</",2)){
		const char *end=strchr(*p,'>');
		*p = end? end+1 : *p+strlen(*p);
	}
	return n;
}
static XmlNode* xml_parse(const char *buf){
	const char *p=buf;
	for(;;){
		xp_skip_ws(&p);
		if(!*p) return NULL;
		if(!strncmp(p,"<?",2)){ const char*e=strstr(p,"?>"); p=e?e+2:p+strlen(p); continue; }
		if(!strncmp(p,"<!--",4)){ const char*e=strstr(p,"-->"); p=e?e+3:p+strlen(p); continue; }
		break;
	}
	return xml_parse_node(&p);
}

/* ------------------------------------------------------------- Scene ------ */

void scene_free(Scene *s){
	xml_free((XmlNode*)s->sceneRoot);
	for(int i=0;i<s->nprefabs;i++) xml_free((XmlNode*)s->prefabs[i].root);
	for(int i=0;i<s->nprefabs;i++) free(s->prefabs[i].attaches);
	for(int i=0;i<s->nobjs;i++){
		mesh_free(&s->objs[i].mesh);
		for(int j=0;j<s->objs[i].nshadowParts;j++) free(s->objs[i].shadowParts[j].verts);
		free(s->objs[i].shadowParts);
	}
	for(int i=0;i<s->nlights;i++) free(s->svols[i].verts);
	for(int i=0;i<s->nshapes;i++) shape2d_free(&s->shapes[i]);
	for(int i=0;i<s->nnegativeProfiles;i++) shape2d_free(&s->negativeProfiles[i].profile);
	free(s->negativeProfiles);
	for(int i=0;i<s->ncameras;i++) free(s->cameras[i].transforms);
	free(s->lights); free(s->mats); free(s->objs); free(s->svols); free(s->cameras);
	free(s->prefabs); free(s->instances); free(s->rigRotations); free(s->rigTargets); free(s->rigJointWorlds); free(s->negativeBoxes); free(s->negativeArches);
	free(s->negativeCylinders); free(s->overlayLines); free(s->charDefs); free(s->shapes);
	free(s->dragStartVerts); free(s->dragObjIndices); free(s->dragVertOffsets);
	for(int i=0;i<s->nscreenTextures;i++) image_free(s->screenTextures[i].pixels);
	free(s->screenTextures);
	memset(s,0,sizeof(*s));
}

static Material preset_materials[] = {
	{ "wall",     {0.80f,0.78f,0.72f}, 6.0f },
	{ "floor",    {0.35f,0.28f,0.22f}, 12.0f },
	{ "wood",     {0.50f,0.32f,0.18f}, 20.0f },
	{ "metal",    {0.70f,0.70f,0.75f}, 60.0f },
	{ "glass",    {0.65f,0.80f,0.85f}, 90.0f },
	{ "stone",    {0.38f,0.36f,0.33f}, 8.0f },
	{ "concrete", {0.52f,0.50f,0.46f}, 4.0f },
	{ "plaster",  {0.90f,0.88f,0.80f}, 3.0f },
	{ "bronze",   {0.48f,0.30f,0.14f}, 40.0f },
	{ "iron",     {0.28f,0.28f,0.30f}, 55.0f },
};
static const int npreset_mats = (int)(sizeof(preset_materials)/sizeof(preset_materials[0]));

static Material* find_material(Scene*s, const char*id){
	if(!id) return NULL;
	for(int i=0;i<s->nmats;i++) if(!strcmp(s->mats[i].id,id)) return &s->mats[i];
	for(int i=0;i<npreset_mats;i++) if(!strcmp(preset_materials[i].id,id)) return &preset_materials[i];
	return NULL;
}

void scene_add_obj(Scene *s, Mesh mesh, mat4 M, mat4 R, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	mesh_transform(&mesh, M, R);
	if(castsShadow && mesh_signed_volume(&mesh) < 0.0f) mesh_flip_winding(&mesh);
	mesh_compute_face_normals(&mesh);
	if(castsShadow) mesh_build_edges(&mesh);
	SceneObj o={0};
	o.mesh=mesh; o.color=color; o.shininess=shin; o.castsShadow=castsShadow;
	o.renderable=renderable; o.unlit=unlit; o.sanityIgnore=s->sanityIgnoreActive;
	o.sanityFloor=s->sanityFloorActive; o.sanityCheck=s->sanityCheckActive;
	o.editNode=s->activeEditNode; o.editMatrix=s->activeEditMatrix;
	o.texIndex=s->activeTexIndex;
	o.screenTexture=s->activeScreenTexture>=0&&s->activeScreenTexture<s->nscreenTextures?s->activeScreenTexture:-1;
	DA_PUSH(s->objs,s->nobjs,s->cobjs,o);
}

/* ---------------------------------------------------- Modifiers ---------- */

static char* read_file(const char*path);
static void warn_unknown_elements(XmlNode *root, const char *path, int prefab);

typedef void (*modifier_parser_fn)(Mesh *m, XmlNode *n);

static char mod_axis(XmlNode *n){ const char *a=xml_attr(n,"axis","y"); return a[0]? a[0] : 'y'; }

static void parse_mod_taper(Mesh *m, XmlNode *n){
	mesh_apply_taper(m, xml_attr_f(n,"amount",0.0f), xml_attr_f(n,"curvature",1.0f), mod_axis(n));
}
static void parse_mod_twist(Mesh *m, XmlNode *n){
	mesh_apply_twist(m, xml_attr_f(n,"angle",0.0f), mod_axis(n));
}
static void parse_mod_bend(Mesh *m, XmlNode *n){
	mesh_apply_bend(m, xml_attr_f(n,"angle",0.0f), mod_axis(n));
}
static void parse_mod_stretch(Mesh *m, XmlNode *n){
	mesh_apply_stretch(m, xml_attr_f(n,"amount",0.0f), xml_attr_f(n,"amplify",1.0f), mod_axis(n));
}
static void parse_mod_skew(Mesh *m, XmlNode *n){
	mesh_apply_skew(m, xml_attr_f(n,"amount",0.0f), mod_axis(n));
}
static void parse_mod_array(Mesh *m, XmlNode *n){
	mesh_apply_array(m, xml_attr_i(n,"count",1),
		xml_attr_v3_cm(n,"translation",v3(0,0,0)),
		xml_attr_v3(n,"rotation",v3(0,0,0)));
}
static void parse_mod_extrude(Mesh *m, XmlNode *n){
	mesh_apply_extrude(m,xml_attr_f_cm(n,"amount",0.1f),mod_axis(n));
}
static void parse_mod_mirror(Mesh *m, XmlNode *n){
	mesh_apply_mirror(m,mod_axis(n),xml_attr_f(n,"weld",0.001f));
}
static void parse_mod_noise(Mesh *m, XmlNode *n){
	mesh_apply_noise(m,xml_attr_f(n,"strength",0.1f),xml_attr_i(n,"seed",1));
}
static void parse_mod_shell(Mesh *m, XmlNode *n){
	mesh_apply_shell(m,xml_attr_f(n,"amount",0.05f));
}
static void parse_mod_bevel(Mesh *m, XmlNode *n){
	(void)m; (void)n;
	fprintf(stderr,"[scener] bevel requires an extruded 2D profile\n");
}

static const struct {
	const char *tag;
	modifier_parser_fn parse;
} modifier_parsers[] = {
	{ "taper",   parse_mod_taper },
	{ "twist",   parse_mod_twist },
	{ "bend",    parse_mod_bend },
	{ "stretch", parse_mod_stretch },
	{ "skew",    parse_mod_skew },
	{ "array",   parse_mod_array },
	{ "extrude", parse_mod_extrude },
	{ "mirror",  parse_mod_mirror },
	{ "noise",   parse_mod_noise },
	{ "shell",   parse_mod_shell },
	{ "bevel",   parse_mod_bevel },
};

static void apply_modifiers(Mesh *m, XmlNode *n){
	for(int i=0;i<n->nkids;i++){
		XmlNode *c=n->kids[i];
		for(int j=0;j<(int)(sizeof(modifier_parsers)/sizeof(modifier_parsers[0]));j++){
			if(!strcmp(c->tag, modifier_parsers[j].tag)){
				modifier_parsers[j].parse(m, c);
				break;
			}
		}
	}
}

static void apply_profile_modifiers(Mesh *m,XmlNode *n){
	for(int i=0;i<n->nkids;i++){
		XmlNode *c=n->kids[i];
		if(!strcmp(c->tag,"extrude")||!strcmp(c->tag,"bevel")) continue;
		for(int j=0;j<(int)(sizeof(modifier_parsers)/sizeof(modifier_parsers[0]));j++)
			if(!strcmp(c->tag,modifier_parsers[j].tag)){ modifier_parsers[j].parse(m,c); break; }
	}
}

/* ---------------------------------------------------- Shapes & sweeping -- */

static Shape2D* find_shape(Scene *s,const char *id){
	if(!id) return NULL;
	for(int i=0;i<s->nshapes;i++)
		if(!strcmp(s->shapes[i].name,id)) return &s->shapes[i];
	return NULL;
}

static void collect_shapes_from_tree(Scene *s,XmlNode *root){
	for(int i=0;i<root->nkids;i++){
		XmlNode *n=root->kids[i];
		if(!strcmp(n->tag,"shape")){
			const char *id=xml_attr(n,"id",NULL);
			if(!id) continue;
			if(find_shape(s,id)) continue;
			Shape2D sh={0};
			strncpy(sh.name,id,31);
			for(int k=0;k<n->nkids;k++){
				if(!strcmp(n->kids[k]->tag,"v")){
					float x=xml_attr_f(n->kids[k],"x",0), y=xml_attr_f(n->kids[k],"y",0);
					vec3 p=v3(x,y,0); DA_PUSH(sh.pts,sh.npts,sh.cpts,p);
				}
			}
			if(sh.npts<2) continue;
			sh.closed=xml_attr_i(n,"closed",0);
			shape2d_compute_normals(&sh);
			DA_PUSH(s->shapes,s->nshapes,s->cshapes,sh);
		} else if(!strcmp(n->tag,"group")||!strcmp(n->tag,"prefab")){
			collect_shapes_from_tree(s,n);
		}
	}
}

static void parse_shape_tag(Scene *s,XmlNode *n){
	(void)s; (void)n;
}

static void parse_lathe(Scene *s,XmlNode *n,mat4 M,mat4 R,mat4 parentM,vec3 pos,vec3 rot,vec3 color,float shin,int castsShadow,int renderable,int unlit){
	(void)parentM; (void)pos; (void)rot;
	const char *sid=xml_attr(n,"shape",NULL);
	Shape2D *sh=find_shape(s,sid);
	if(!sh){ fprintf(stderr,"lathe: shape '%s' not found\n",sid?sid:"(null)"); return; }
	Mesh mesh=gen_lathe(sh,xml_attr_i(n,"segments",24));
	apply_modifiers(&mesh,n);
	scene_add_obj(s,mesh,M,R,color,shin,castsShadow,renderable,unlit);
}

static void parse_loft(Scene *s,XmlNode *n,mat4 M,mat4 R,mat4 parentM,vec3 pos,vec3 rot,vec3 color,float shin,int castsShadow,int renderable,int unlit){
	(void)parentM; (void)pos; (void)rot;
	const char *ps=xml_attr(n,"pathShape",NULL);
	const char *cs=xml_attr(n,"crossShape",NULL);
	Shape2D *pathS=find_shape(s,ps), *crossS=find_shape(s,cs);
	if(!pathS||!crossS){
		fprintf(stderr,"loft: shapes '%s'/'%s' not found\n",ps?ps:"(null)",cs?cs:"(null)");
		return;
	}
	int lclosed=xml_attr_i(n,"closed",0);
	int nstations=xml_attr_i(n,"segments",pathS->npts);
	if(nstations<2) nstations=2;
	LoftPath lp={0};
	for(int i=0;i<nstations;i++){
		float u=(float)i/(float)nstations;
		int idx=(int)(u*pathS->npts);
		if(idx>=pathS->npts) idx=pathS->npts-1;
		vec3 p=v3(pathS->pts[idx].x,0,pathS->pts[idx].y);
		DA_PUSH(lp.pts,lp.npts,lp.cpts,p);
	}
	Shape2D tmp=crossS->closed?*crossS:(Shape2D){0};
	if(!crossS->closed){
		tmp.pts=malloc(sizeof(vec3)*(size_t)crossS->npts);
		memcpy(tmp.pts,crossS->pts,sizeof(vec3)*(size_t)crossS->npts);
		tmp.npts=crossS->npts; tmp.cpts=crossS->npts;
		tmp.closed=1;
	}
	shape2d_compute_normals(&tmp);
	Mesh mesh=gen_loft(&lp,&tmp,lclosed);
	apply_modifiers(&mesh,n);
	scene_add_obj(s,mesh,M,R,color,shin,castsShadow,renderable,unlit);
	if(!crossS->closed){ free(tmp.pts); free(tmp.nrm); }
	free(lp.pts);
}

/* ---------------------------------------------------- Overlay lines ------- */

static void scene_add_overlay_line(Scene *s, vec3 start, vec3 end, vec3 color, int category, const char *camera){
	OverlayLine ol={start,end,color,category,{0}};
	if(camera) strncpy(ol.camera,camera,31);
	DA_PUSH(s->overlayLines,s->noverlayLines,s->coverlayLines,ol);
}

static void add_circle_lines(Scene *s, vec3 center, float radius, vec3 normal, vec3 color, int n, int category, const char *camera){
	vec3 u,v;
	if(fabsf(normal.x)>0.001f||fabsf(normal.z)>0.001f)
		u=vnorm(v3(-normal.z,0,normal.x));
	else
		u=vnorm(v3(1,0,0));
	v=vnorm(vcross(normal,u));
	vec3 prev=vadd(center,vscale(u,radius));
	for(int i=1;i<=n;i++){
		float angle=2.0f*M_PIf*(float)i/(float)n;
		vec3 next=vadd(center,vadd(vscale(u,radius*cosf(angle)),vscale(v,radius*sinf(angle))));
		scene_add_overlay_line(s,prev,next,color,category,camera);
		prev=next;
	}
}

static void add_character_circle(Scene *s, mat4 M, vec3 center, float radius, vec3 color, const char *camera){
	vec3 prev=mat4_xform_point(M,vadd(center,v3(radius,0,0)));
	for(int i=1;i<=16;i++){
		float angle=2.0f*M_PIf*(float)i/16.0f;
		vec3 next=mat4_xform_point(M,vadd(center,v3(radius*cosf(angle),0,radius*sinf(angle))));
		scene_add_overlay_line(s,prev,next,color,0,camera);
		prev=next;
	}
}

static void add_character_dummy(Scene *s, mat4 M, CharDef *cd, vec3 color, const char *camera, const char *pose, int hasTarget, vec3 target){
	float h=cd->height, r=cd->radius;
	float yHead=cd->top*h;
	float yNeck=cd->neck*h;
	float yPelvis=cd->pelvis*h;
	float yFeet=cd->feet*h;
	float lean=0.0f;
	if(!strcmp(pose,"crouch")){
		yHead*=0.68f; yNeck*=0.66f; yPelvis*=0.72f; lean=h*0.12f;
	} else if(!strcmp(pose,"inspect")) lean=h*0.10f;
	float shoulderW=r*1.8f;
	float hipW=r*1.3f;
	vec3 head=mat4_xform_point(M,v3(0,yHead,lean));
	vec3 neck=mat4_xform_point(M,v3(0,yNeck,lean));
	vec3 pelvis=mat4_xform_point(M,v3(0,yPelvis,0));
	vec3 feet=mat4_xform_point(M,v3(0,yFeet,0));
	vec3 shoulderL=mat4_xform_point(M,v3(-shoulderW,yNeck,lean));
	vec3 shoulderR=mat4_xform_point(M,v3(shoulderW,yNeck,lean));
	vec3 hipL=mat4_xform_point(M,v3(-hipW,yPelvis,0));
	vec3 hipR=mat4_xform_point(M,v3(hipW,yPelvis,0));
	vec3 footL=mat4_xform_point(M,v3(-hipW,yFeet,!strcmp(pose,"walk")?h*0.12f:0));
	vec3 footR=mat4_xform_point(M,v3(hipW,yFeet,!strcmp(pose,"climb")?h*0.12f:(!strcmp(pose,"walk")?-h*0.12f:0)));
	vec3 handL=mat4_xform_point(M,v3(-shoulderW-r*0.7f,yPelvis+h*0.08f,lean+h*0.02f));
	vec3 handR=mat4_xform_point(M,v3(shoulderW+r*0.7f,yPelvis+h*0.08f,lean+h*0.02f));
	if(hasTarget){
		vec3 dirL=vnorm(vsub(target,shoulderL));
		vec3 dirR=vnorm(vsub(target,shoulderR));
		if(!strcmp(pose,"reach") || !strcmp(pose,"inspect")) handR=vadd(shoulderR,vscale(dirR,h*0.42f));
		else if(!strcmp(pose,"work") || !strcmp(pose,"climb")){
			handL=vadd(shoulderL,vscale(dirL,h*0.38f));
			handR=vadd(shoulderR,vscale(dirR,h*0.38f));
		}
		if(!strcmp(pose,"look")) scene_add_overlay_line(s,head,vadd(head,vscale(vnorm(vsub(target,head)),h*0.18f)),color,0,camera);
	}
	vec3 elbowL=lerp(shoulderL,handL,0.52f);
	vec3 elbowR=lerp(shoulderR,handR,0.52f);
	scene_add_overlay_line(s,feet,head,color,0,camera);
	scene_add_overlay_line(s,shoulderL,shoulderR,color,0,camera);
	scene_add_overlay_line(s,hipL,hipR,color,0,camera);
	scene_add_overlay_line(s,shoulderL,hipL,color,0,camera);
	scene_add_overlay_line(s,shoulderR,hipR,color,0,camera);
	scene_add_overlay_line(s,neck,pelvis,color,0,camera);
	scene_add_overlay_line(s,shoulderL,elbowL,color,0,camera);
	scene_add_overlay_line(s,elbowL,handL,color,0,camera);
	scene_add_overlay_line(s,shoulderR,elbowR,color,0,camera);
	scene_add_overlay_line(s,elbowR,handR,color,0,camera);
	scene_add_overlay_line(s,hipL,footL,color,0,camera);
	scene_add_overlay_line(s,hipR,footR,color,0,camera);
	add_character_circle(s,M,v3(0,yHead,lean),r,color,camera);
	add_character_circle(s,M,v3(0,yNeck,lean),r*0.55f,color,camera);
	add_character_circle(s,M,v3(0,yPelvis,0),r*0.85f,color,camera);
	add_character_circle(s,M,v3(0,yFeet,0),r*0.65f,color,camera);
}

static void add_lamp_dummy(Scene *s, vec3 pos, float radius, vec3 color, int category, const char *camera){
	add_circle_lines(s,pos,radius,v3(1,0,0),color,16,category,camera);
	add_circle_lines(s,pos,radius,v3(0,1,0),color,16,category,camera);
	add_circle_lines(s,pos,radius,v3(0,0,1),color,16,category,camera);
}

static void add_camera_dummy(Scene *s, vec3 pos, vec3 look, float fov, float aspect, vec3 color, int category, const char *camera){
	vec3 forward=vnorm(vsub(look,pos));
	vec3 worldUp=v3(0,1,0);
	vec3 right=vnorm(vcross(forward,worldUp));
	vec3 up=vnorm(vcross(right,forward));
	float dist=0.3f;
	float hh=dist*tanf(fov*M_PIf/360.0f);
	float hw=hh*aspect;
	vec3 center=vadd(pos,vscale(forward,dist));
	vec3 tl=vadd(center,vadd(vscale(up,hh),vscale(right,-hw)));
	vec3 tr=vadd(center,vadd(vscale(up,hh),vscale(right,hw)));
	vec3 bl=vadd(center,vadd(vscale(up,-hh),vscale(right,-hw)));
	vec3 br=vadd(center,vadd(vscale(up,-hh),vscale(right,hw)));
	scene_add_overlay_line(s,pos,tl,color,category,camera); scene_add_overlay_line(s,pos,tr,color,category,camera);
	scene_add_overlay_line(s,pos,bl,color,category,camera); scene_add_overlay_line(s,pos,br,color,category,camera);
	scene_add_overlay_line(s,tl,tr,color,category,camera); scene_add_overlay_line(s,tr,br,color,category,camera);
	scene_add_overlay_line(s,br,bl,color,category,camera); scene_add_overlay_line(s,bl,tl,color,category,camera);
}

void scene_rebuild_camera_gizmos(Scene *s, float aspect){
	int w=0;
	for(int i=0;i<s->noverlayLines;i++){
		if(s->overlayLines[i].category!=2) s->overlayLines[w++]=s->overlayLines[i];
	}
	s->noverlayLines=w;
	for(int ci=0;ci<s->ncameras;ci++){
		Camera *c=&s->cameras[ci];
		add_camera_dummy(s,c->pos,c->look,c->fov,aspect,v3(0.2f,0.8f,0.2f),2,NULL);
	}
}

/* ---------------------------------------------------- Shape parsers ------- */

static void parse_nodes(Scene *s, XmlNode *parent, mat4 parentM, mat4 parentR);

typedef void (*shape_parser_fn)(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit);

static void parse_box(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	vec3 sz=cvt3ds_sz(s,xml_attr_v3_cm(n,"size",v3(1,1,1)));
	float insetX=0.0f, insetY=0.0f;
	xml_attr_2f(n,"inset",0.0f,0.0f,&insetX,&insetY);
	Mesh mesh=(insetX>0.0f || insetY>0.0f) ? gen_box_inset(sz.x,sz.y,sz.z,insetX,insetY)
		: gen_box(sz.x,sz.y,sz.z);
	apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

static int screen_texture_index(Scene *s,const char *image){
	char path[1024];
	int length=snprintf(path,sizeof(path),"%s%s%s",image[0]=='/'?"":s->assetRoot,
		image[0]=='/'||!s->assetRoot[0]?"":"/",image);
	if(length<0 || (size_t)length>=sizeof(path)){
		fprintf(stderr,"[scener] screen image path too long: %s\n",image); s->assetError=1; return -1;
	}
	for(int i=0;i<s->nscreenTextures;i++) if(!strcmp(s->screenTextures[i].path,path)) return i;
	ScreenTexture texture={0};
	snprintf(texture.path,sizeof(texture.path),"%s",path);
	texture.pixels=load_image(path,&texture.width,&texture.height);
	if(!texture.pixels){
		fprintf(stderr,"[scener] cannot load screen image: %s\n",path); s->assetError=1; return -1;
	}
	DA_PUSH(s->screenTextures,s->nscreenTextures,s->cscreenTextures,texture);
	return s->nscreenTextures-1;
}

typedef enum { PROFILE_RECT, PROFILE_ROUNDED_RECT, PROFILE_CIRCLE, PROFILE_ELLIPSE, PROFILE_STAR } profile_kind_t;

static void parse_profile_primitive(Scene *s,XmlNode *n,mat4 M,mat4 R,vec3 color,float shin,int castsShadow,int renderable,int unlit,profile_kind_t kind){
	Shape2D profile={0};
	vec3 size=xml_attr_v3_cm(n,"size",v3(0.01f,0.01f,0));
	switch(kind){
	case PROFILE_RECT:         profile=shape2d_rect(size.x,size.y); break;
	case PROFILE_ROUNDED_RECT: profile=shape2d_rounded_rect(size.x,size.y,xml_attr_f_cm(n,"radius",0),xml_attr_i(n,"segments",8)); break;
	case PROFILE_CIRCLE:       profile=shape2d_ellipse(xml_attr_f_cm(n,"radius",0.005f),xml_attr_f_cm(n,"radius",0.005f),xml_attr_i(n,"segments",32)); break;
	case PROFILE_ELLIPSE:      profile=shape2d_ellipse(size.x*0.5f,size.y*0.5f,xml_attr_i(n,"segments",48)); break;
	case PROFILE_STAR:         profile=shape2d_star(xml_attr_f_cm(n,"outerRadius",0.005f),xml_attr_f_cm(n,"innerRadius",0.0025f),xml_attr_i(n,"points",5)); break;
	}
	XmlNode *extrude=NULL,*bevel=NULL;
	int stage=0,invalid=0;
	for(int i=0;i<n->nkids;i++){
		XmlNode *child=n->kids[i];
		if(!strcmp(child->tag,"extrude")){
			if(extrude||stage) invalid=1;
			extrude=child; stage=1;
		} else if(!strcmp(child->tag,"bevel")){
			if(!extrude||bevel||stage>1) invalid=1;
			bevel=child; stage=2;
		} else stage=3;
	}
	const char *axis=extrude?xml_attr(extrude,"axis","z"):"z";
	float depth=extrude?xml_attr_f_cm(extrude,"amount",0):0;
	float amount=bevel?xml_attr_f_cm(bevel,"amount",0):0;
	if(!profile.npts||!extrude||invalid||strcmp(axis,"z")){
		fprintf(stderr,"[scener] %s: expected a valid 2D outline and one <extrude axis=\"z\"> before optional <bevel>\n",n->tag);
		shape2d_free(&profile); return;
	}
	Mesh mesh=gen_profile_extrusion_beveled(&profile,depth,amount,bevel?xml_attr_i(bevel,"bevelSegments",4):1);
	shape2d_free(&profile);
	if(!mesh.ntris){ fprintf(stderr,"[scener] %s: invalid extrusion or bevel\n",n->tag); return; }
	apply_profile_modifiers(&mesh,n);
	scene_add_obj(s,mesh,M,R,color,shin,castsShadow,renderable,unlit);
}

#define PROFILE_PARSER(name,kind) \
static void parse_##name(Scene *s,XmlNode *n,mat4 M,mat4 R,mat4 parentM,vec3 pos,vec3 rot,vec3 color,float shin,int castsShadow,int renderable,int unlit){ \
	(void)parentM; (void)pos; (void)rot; parse_profile_primitive(s,n,M,R,color,shin,castsShadow,renderable,unlit,kind); \
}
PROFILE_PARSER(rect,PROFILE_RECT)
PROFILE_PARSER(rounded_rect,PROFILE_ROUNDED_RECT)
PROFILE_PARSER(circle,PROFILE_CIRCLE)
PROFILE_PARSER(ellipse,PROFILE_ELLIPSE)
PROFILE_PARSER(star,PROFILE_STAR)
#undef PROFILE_PARSER

static void parse_screen(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot; (void)castsShadow; (void)unlit;
	if(!xml_attr(n,"material",NULL) && !xml_attr(n,"color",NULL)) color=v3(1,1,1);
	vec3 size=xml_attr_v3_cm(n,"size",v3(0.01f,0.01f,0.0004f));
	Shape2D profile=shape2d_rounded_rect(size.x,size.y,xml_attr_f_cm(n,"radius",0),xml_attr_i(n,"segments",8));
	Mesh mesh=gen_profile_extrusion_beveled(&profile,size.z,0,1);
	shape2d_free(&profile);
	if(!mesh.ntris){ fprintf(stderr,"[scener] screen: invalid size or radius\n"); return; }
	apply_modifiers(&mesh,n);
	const char *image=s->activeScreenImage?s->activeScreenImage:xml_attr(n,"image",NULL);
	int old=s->activeScreenTexture;
	s->activeScreenTexture=image&&image[0]?screen_texture_index(s,image):-1;
	scene_add_obj(s,mesh,M,R,color,shin,xml_attr_i(n,"castShadow",0),renderable,xml_attr_i(n,"unlit",1));
	s->activeScreenTexture=old;
}

static void parse_sphere(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	float r=xml_attr_f_cm(n,"radius",0.5f);
	Mesh mesh=gen_sphere(r,xml_attr_i(n,"rings",16),xml_attr_i(n,"slices",24)); apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

static void parse_cylinder(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	float r=xml_attr_f_cm(n,"radius",0.5f), h=xml_attr_f_cm(n,"height",1.0f);
	float wall=xml_attr_f_cm(n,"tube",0.0f);
	Mesh mesh=wall>0.0f ? gen_cylinder_tube(r,h,wall,xml_attr_i(n,"sides",24))
		: gen_cylinder(r,h,xml_attr_i(n,"sides",24));
	apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

static void parse_prism(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	float r=xml_attr_f_cm(n,"radius",0.5f), h=xml_attr_f_cm(n,"height",1.0f);
	Mesh mesh=gen_prism(r,h,xml_attr_i(n,"sides",6)); apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

static void parse_cone(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	float rb=xml_attr_f_cm(n,"radius",0.5f), rt=xml_attr_f_cm(n,"radiusTop",0.0f), h=xml_attr_f_cm(n,"height",1.0f);
	int sides = xml_attr_i(n,"sides", !strcmp(n->tag,"pyramid")?4:24);
	Mesh mesh=gen_cone(rb,rt,h,sides); apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

static void parse_torus(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	float R_=xml_attr_f_cm(n,"majorRadius",0.5f), r_=xml_attr_f_cm(n,"minorRadius",0.15f);
	Mesh mesh=gen_torus(R_,r_,xml_attr_i(n,"majorSegments",24),xml_attr_i(n,"minorSegments",12)); apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

static void parse_arch(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	float w=xml_attr_f_cm(n,"width",1.0f), h=xml_attr_f_cm(n,"height",1.5f), d=xml_attr_f_cm(n,"depth",0.2f);
	float wall=xml_attr_f_cm(n,"tube",xml_attr_f_cm(n,"thickness",0.0f));
	float archInset=xml_attr_f_cm(n,"inset",0.0f);
	Mesh mesh=gen_arch(w,h,d,wall,xml_attr_i(n,"segments",16),archInset);
	apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

typedef struct {
	const char *name;
	window_outline_t outline;
	float width,height;
	int sill;
} window_preset_t;

static const window_preset_t door_presets[]={
	{ "rectangular", WINDOW_RECTANGLE, DOOR_DEFAULT_WIDTH, DOOR_DEFAULT_HEIGHT, 0 },
	{ "round-arch", WINDOW_ROUND_ARCH, DOOR_DEFAULT_WIDTH, DOOR_DEFAULT_HEIGHT, 0 },
	{ "gothic", WINDOW_POINTED_ARCH, DOOR_DEFAULT_WIDTH, DOOR_DEFAULT_HEIGHT, 0 }
};

static const window_preset_t window_presets[]={
	{ "round-arch", WINDOW_ROUND_ARCH, WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT, 0 },
	{ "cottage", WINDOW_RECTANGLE, WINDOW_DEFAULT_WIDTH, WINDOW_COTTAGE_HEIGHT, 1 },
	{ "gothic", WINDOW_POINTED_ARCH, WINDOW_DEFAULT_WIDTH, WINDOW_GOTHIC_HEIGHT, 0 }
};

typedef struct {
	Shape2D outer,inner;
	float width,height,depth,frame,paneDepth,paneOffset,cutDepth,sillHeight,sillProjection;
	int pane,sill,cutWalls;
} window_spec_t;

static void window_spec_free(window_spec_t *w){
	shape2d_free(&w->outer); shape2d_free(&w->inner);
}

static int window_spec(XmlNode *n,window_spec_t *w){
	memset(w,0,sizeof(*w));
	if(xml_attr(n,"attach",NULL)||n->nkids){
		fprintf(stderr,"[scener] %s: use group/prefab transforms; attach and child modifiers are unsupported\n",n->tag); return 0;
	}
	int door=!strcmp(n->tag,"door");
	const char *preset=xml_attr(n,"preset",door?"rectangular":"round-arch"),*style=xml_attr(n,"style","plain");
	const window_preset_t *p=NULL;
	const window_preset_t *presets=door?door_presets:window_presets;
	int count=door?sizeof(door_presets)/sizeof(door_presets[0]):sizeof(window_presets)/sizeof(window_presets[0]);
	for(int i=0;i<count;i++) if(!strcmp(preset,presets[i].name)) p=&presets[i];
	if(!p|| (strcmp(style,"plain")&&strcmp(style,"storybook"))){
		fprintf(stderr,"[scener] %s: unknown preset '%s' or style '%s'\n",n->tag,preset,style); return 0;
	}
	w->width=xml_attr_f_cm(n,"width",p->width); w->height=xml_attr_f_cm(n,"height",p->height);
	float size=fminf(w->width,w->height);
	w->frame=xml_attr_f_cm(n,"frameWidth",size*(!strcmp(style,"storybook")?WINDOW_STORYBOOK_FRAME_RATIO:WINDOW_FRAME_RATIO));
	w->depth=xml_attr_f_cm(n,"depth",w->frame*WINDOW_DEPTH_RATIO);
	w->paneDepth=door?WINDOW_PANE_DEPTH:xml_attr_f_cm(n,"paneDepth",WINDOW_PANE_DEPTH);
	w->paneOffset=door?0:xml_attr_f_cm(n,"paneOffset",0);
	w->cutDepth=xml_attr_f_cm(n,"cutDepth",w->depth);
	w->sillHeight=door?w->frame:xml_attr_f_cm(n,"sillHeight",w->frame);
	w->sillProjection=door?w->frame:xml_attr_f_cm(n,"sillProjection",w->frame);
	w->pane=door?0:xml_attr_i(n,"pane",1); w->sill=door?0:xml_attr_i(n,"sill",p->sill); w->cutWalls=xml_attr_i(n,"cutWalls",1);
	int segments=xml_attr_i(n,"segments",WINDOW_DEFAULT_SEGMENTS);
	float positive[]={w->width,w->height,w->frame,w->depth,w->paneDepth,w->cutDepth,w->sillHeight};
	int valid=segments>=WINDOW_MIN_SEGMENTS&&segments<=WINDOW_MAX_SEGMENTS;
	for(int i=0;i<(int)(sizeof(positive)/sizeof(positive[0]));i++) valid&=isfinite(positive[i])&&positive[i]>WINDOW_EPSILON;
	valid&=isfinite(w->paneOffset)&&isfinite(w->sillProjection)&&w->sillProjection>=0;
	valid&=!w->pane||(fabsf(w->paneOffset)+w->paneDepth/2<=w->depth/2);
	if(valid){
		w->outer=shape2d_window(p->outline,w->width,w->height,segments);
		w->inner=shape2d_inset(&w->outer,w->frame);
		valid=w->inner.npts>0;
	}
	if(!valid){
		fprintf(stderr,"[scener] %s: invalid dimensions, frame inset, pane placement or segments for '%s'\n",n->tag,preset);
		window_spec_free(w); return 0;
	}
	return 1;
}

static void parse_window(Scene *s,XmlNode *n,mat4 M,mat4 R,mat4 parentM,vec3 pos,vec3 rot,vec3 color,float shin,int castsShadow,int renderable,int unlit){
	(void)parentM; (void)pos; (void)rot;
	window_spec_t w;
	if(!window_spec(n,&w)) return;
	Material *frame=find_material(s,xml_attr(n,"frameMaterial",xml_attr(n,"material","wood")));
	if(frame){ color=frame->color; shin=frame->shininess; }
	Mesh mesh=gen_profile_frame(&w.outer,&w.inner,w.depth);
	scene_add_obj(s,mesh,M,R,color,shin,castsShadow,renderable,unlit);
	if(w.sill){
		/* Seat the sill into the frame to avoid coplanar faces on the wall reveal. */
		mesh=gen_box(w.width+2*w.sillProjection,w.sillHeight,w.depth+w.sillProjection);
		mat4 sillM=mat4_mul(M,mat4_translate(v3(0,-w.height/2-w.sillHeight/2+WINDOW_JOIN_OVERLAP,w.sillProjection/2)));
		scene_add_obj(s,mesh,sillM,R,color,shin,castsShadow,renderable,unlit);
	}
	if(w.pane){
		Material *glass=find_material(s,xml_attr(n,"glassMaterial","glass"));
		mesh=gen_profile_extrusion(&w.inner,w.paneDepth);
		mat4 paneM=mat4_mul(M,mat4_translate(v3(0,0,w.paneOffset)));
		scene_add_obj(s,mesh,paneM,R,glass?glass->color:color,glass?glass->shininess:shin,0,renderable,unlit);
	}
	window_spec_free(&w);
}


typedef struct {
	window_spec_t frame;
	Shape2D leaf,pet,petOuter,windowOuter,windowInner;
	float leafDepth,gap,angle,hingeX,windowPaneDepth;
	int handle,windowPane;
} door_spec_t;

static void door_spec_free(door_spec_t *d){
	window_spec_free(&d->frame); shape2d_free(&d->leaf);
	shape2d_free(&d->pet); shape2d_free(&d->petOuter);
	shape2d_free(&d->windowOuter); shape2d_free(&d->windowInner);
}


static int door_profile_contains(const Shape2D *outer,const Shape2D *inner){
	if(!outer->npts||!inner->npts) return 0;
	for(int i=0;i<outer->npts;i++){
		vec3 a=outer->pts[i],b=outer->pts[(i+1)%outer->npts],e=vnorm(vsub(b,a));
		for(int j=0;j<inner->npts;j++)
			if(vdot(v3(-e.y,e.x,0),vsub(inner->pts[j],a))<-WINDOW_EPSILON) return 0;
	}
	return 1;
}

static int door_window_spec(XmlNode *n,door_spec_t *d){
	const char *kind=xml_attr(n,"window","none");
	if(!strcmp(kind,"none")) return 1;
	window_spec_t *w=&d->frame;
	if(!strcmp(kind,"matching")) kind=xml_attr(n,"preset","rectangular");
	int round=!strcmp(kind,"round");
	const window_preset_t *preset=NULL;
	for(int i=0;i<(int)(sizeof(door_presets)/sizeof(door_presets[0]));i++)
		if(!strcmp(kind,door_presets[i].name)) preset=&door_presets[i];
	if(!round&&!preset) return 0;
	float width=xml_attr_f_cm(n,"windowWidth",w->width*DOOR_WINDOW_WIDTH_RATIO);
	float height=xml_attr_f_cm(n,"windowHeight",round?width:w->height*DOOR_WINDOW_HEIGHT_RATIO);
	float frame=xml_attr_f_cm(n,"windowFrameWidth",w->frame/2);
	float center=xml_attr_f_cm(n,"windowCenter",w->height*DOOR_WINDOW_CENTER_RATIO);
	d->windowPane=xml_attr_i(n,"windowPane",1);
	d->windowPaneDepth=xml_attr_f_cm(n,"windowPaneDepth",fminf(WINDOW_PANE_DEPTH,d->leafDepth));
	float positive[]={width,height,frame,d->windowPaneDepth};
	for(int i=0;i<(int)(sizeof(positive)/sizeof(positive[0]));i++)
		if(!isfinite(positive[i])||positive[i]<=WINDOW_EPSILON) return 0;
	if(!isfinite(center)||d->windowPaneDepth>d->leafDepth||(round&&fabsf(height-width)>WINDOW_EPSILON)) return 0;
	int segments=xml_attr_i(n,"segments",WINDOW_DEFAULT_SEGMENTS);
	if(segments%2) segments++;
	if(round){
		d->windowOuter.closed=1;
		for(int i=0;i<segments;i++){
			float angle=2*M_PIf*i/segments;
			vec3 v=v3(width/2*cosf(angle),width/2*sinf(angle),0);
			DA_PUSH(d->windowOuter.pts,d->windowOuter.npts,d->windowOuter.cpts,v);
		}
	} else d->windowOuter=shape2d_window(preset->outline,width,height,segments);
	for(int i=0;i<d->windowOuter.npts;i++) d->windowOuter.pts[i].y+=center-w->height/2;
	d->windowInner=shape2d_inset(&d->windowOuter,frame);
	if(!d->windowInner.npts||!door_profile_contains(&d->leaf,&d->windowOuter)) return 0;
	float bottom=INFINITY,petTop=-INFINITY;
	for(int i=0;i<d->windowOuter.npts;i++) bottom=fminf(bottom,d->windowOuter.pts[i].y);
	for(int i=0;i<d->petOuter.npts;i++) petTop=fmaxf(petTop,d->petOuter.pts[i].y);
	return bottom>petTop+WINDOW_EPSILON;
}

static int door_spec(XmlNode *n,door_spec_t *d){
	memset(d,0,sizeof(*d));
	if(!window_spec(n,&d->frame)) return 0;
	window_spec_t *w=&d->frame;
	d->leafDepth=xml_attr_f_cm(n,"leafDepth",DOOR_LEAF_DEPTH);
	d->gap=xml_attr_f_cm(n,"clearance",DOOR_CLEARANCE);
	d->angle=xml_attr_f(n,"openAngle",0);
	d->handle=xml_attr_i(n,"handle",1);
	const char *hinge=xml_attr(n,"hinge","left");
	float pw=xml_attr_f_cm(n,"petWidth",0),ph=xml_attr_f_cm(n,"petHeight",0);
	float trim=xml_attr_f_cm(n,"petFrameWidth",w->frame/2);
	int valid=isfinite(d->leafDepth)&&d->leafDepth>WINDOW_EPSILON&&d->leafDepth<=w->depth;
	valid&=isfinite(d->gap)&&d->gap>=0&&isfinite(d->angle)&&fabsf(d->angle)<=DOOR_MAX_ANGLE;
	valid&=!strcmp(hinge,"left")||!strcmp(hinge,"right");
	valid&=isfinite(pw)&&isfinite(ph)&&isfinite(trim)&&pw>=0&&ph>=0&&trim>0;
	valid&=(pw==0&&ph==0)||(pw>0&&ph>pw/2);
	if(valid){
		/* Door jambs end at the floor; only the leaf has a bottom clearance. */
		for(int i=0;i<w->inner.npts;i++)
			if(w->inner.pts[i].y<=-w->height/2+w->frame+WINDOW_EPSILON) w->inner.pts[i].y=-w->height/2;
		d->leaf=shape2d_inset(&w->inner,d->gap);
		valid=d->leaf.npts>0;
		d->hingeX=(!strcmp(hinge,"left")?-1:1)*(w->width/2-w->frame);
		if(pw>0&&valid){
			int segments=xml_attr_i(n,"segments",WINDOW_DEFAULT_SEGMENTS);
			d->pet=shape2d_window(WINDOW_ROUND_ARCH,pw,ph,segments);
			d->petOuter=shape2d_window(WINDOW_ROUND_ARCH,pw+2*trim,ph+trim,segments);
			for(int i=0;i<d->pet.npts;i++) d->pet.pts[i].y+=-w->height/2+d->gap+ph/2;
			for(int i=0;i<d->petOuter.npts;i++) d->petOuter.pts[i].y+=-w->height/2+d->gap+(ph+trim)/2;
			valid=door_profile_contains(&d->leaf,&d->petOuter);
		}
	}
	if(valid) valid=door_window_spec(n,d);
	if(!valid){
		fprintf(stderr,"[scener] door: invalid leaf, hinge, angle, pet passage or window opening\n");
		door_spec_free(d); return 0;
	}
	return 1;
}

static void parse_door(Scene *s,XmlNode *n,mat4 M,mat4 R,mat4 parentM,vec3 pos,vec3 rot,vec3 color,float shin,int castsShadow,int renderable,int unlit){
	(void)parentM; (void)pos; (void)rot;
	door_spec_t d;
	if(!door_spec(n,&d)) return;
	window_spec_t *w=&d.frame;
	Material *frame=find_material(s,xml_attr(n,"frameMaterial",xml_attr(n,"material","wood")));
	Material *leaf=find_material(s,xml_attr(n,"leafMaterial",xml_attr(n,"material","wood")));
	Material *metal=find_material(s,xml_attr(n,"hardwareMaterial","metal"));
	scene_add_obj(s,gen_profile_frame(&w->outer,&w->inner,w->depth),M,R,frame?frame->color:color,frame?frame->shininess:shin,castsShadow,renderable,unlit);
	vec3 pivot=v3(d.hingeX,0,w->depth/2);
	mat4 swing=mat4_rot_xyz(v3(0,d.hingeX<0?-d.angle:d.angle,0));
	mat4 leafM=mat4_mul(M,mat4_mul(mat4_translate(pivot),mat4_mul(swing,mat4_translate(vscale(pivot,-1)))));
	mat4 leafR=mat4_mul(R,swing);
	leafM=mat4_mul(leafM,mat4_translate(v3(0,0,(w->depth-d.leafDepth)/2)));
	Shape2D holes[MAX_DOOR_OPENINGS]; int nholes=0;
	if(d.petOuter.npts) holes[nholes++]=d.petOuter;
	if(d.windowOuter.npts) holes[nholes++]=d.windowOuter;
	Mesh mesh=gen_profile_cutouts(&d.leaf,holes,nholes,d.leafDepth);
	scene_add_obj(s,mesh,leafM,leafR,leaf?leaf->color:color,leaf?leaf->shininess:shin,castsShadow,renderable,unlit);
	if(d.pet.npts){
		mesh=gen_profile_frame(&d.petOuter,&d.pet,d.leafDepth*2);
		scene_add_obj(s,mesh,leafM,leafR,metal?metal->color:color,metal?metal->shininess:shin,castsShadow,renderable,unlit);
	}
	if(d.windowOuter.npts){
		Material *trim=find_material(s,xml_attr(n,"windowFrameMaterial",xml_attr(n,"frameMaterial",xml_attr(n,"material","wood"))));
		mesh=gen_profile_frame(&d.windowOuter,&d.windowInner,d.leafDepth*2);
		scene_add_obj(s,mesh,leafM,leafR,trim?trim->color:color,trim?trim->shininess:shin,castsShadow,renderable,unlit);
		if(d.windowPane){
			Material *glass=find_material(s,xml_attr(n,"windowGlassMaterial","glass"));
			mesh=gen_profile_extrusion(&d.windowInner,d.windowPaneDepth);
			scene_add_obj(s,mesh,leafM,leafR,glass?glass->color:color,glass?glass->shininess:shin,0,renderable,unlit);
		}
	}
	if(d.handle){
		float radius=fminf(w->width,w->height)*DOOR_HANDLE_RADIUS_RATIO;
		float x=-d.hingeX*(1-2*DOOR_HANDLE_INSET_RATIO),y=w->height*(DOOR_HANDLE_HEIGHT_RATIO-1.0f/2);
		for(int side=-1;side<=1;side+=2){
			mat4 handleM=mat4_mul(leafM,mat4_translate(v3(x,y,side*(d.leafDepth/2+radius/2))));
			scene_add_obj(s,gen_sphere(radius,DOOR_HANDLE_SEGMENTS,DOOR_HANDLE_SEGMENTS),handleM,leafR,metal?metal->color:color,metal?metal->shininess:shin,castsShadow,renderable,unlit);
		}
	}
	door_spec_free(&d);
}

static void parse_capsule(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot;
	float r=xml_attr_f_cm(n,"radius",0.5f), h=xml_attr_f_cm(n,"height",1.0f);
	Mesh mesh=gen_capsule(r,h,xml_attr_i(n,"rings",12),xml_attr_i(n,"slices",24));
	apply_modifiers(&mesh,n);
	scene_add_obj(s, mesh, M,R, color,shin,castsShadow,renderable,unlit);
}

static void parse_group(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot; (void)color; (void)shin; (void)castsShadow; (void)renderable; (void)unlit;
	parse_nodes(s, n, M, R);
}

#define OPENING_RECT     0
#define OPENING_ARCH     1
#define OPENING_CYLINDER 2
#define OPENING_PROFILE  3
typedef struct {
	float x,width,height,sill;
	int type;       /* OPENING_RECT / OPENING_ARCH / OPENING_CYLINDER */
	Shape2D profile;
	float cylR;     /* cylinder: radius (in wall-local XY) */
} Opening;
static int mat4_inverse_affine(mat4 m, mat4 *out){
	vec3 a=v3(m.m[0],m.m[1],m.m[2]), b=v3(m.m[4],m.m[5],m.m[6]);
	vec3 c=v3(m.m[8],m.m[9],m.m[10]), t=v3(m.m[12],m.m[13],m.m[14]);
	float det=vdot(a,vcross(b,c));
	if(fabsf(det)<1e-8f) return 0;
	vec3 r0=vscale(vcross(b,c),1.0f/det);
	vec3 r1=vscale(vcross(c,a),1.0f/det);
	vec3 r2=vscale(vcross(a,b),1.0f/det);
	*out=mat4_identity();
	out->m[0]=r0.x; out->m[4]=r0.y; out->m[8]=r0.z;
	out->m[1]=r1.x; out->m[5]=r1.y; out->m[9]=r1.z;
	out->m[2]=r2.x; out->m[6]=r2.y; out->m[10]=r2.z;
	out->m[12]=-vdot(r0,t); out->m[13]=-vdot(r1,t); out->m[14]=-vdot(r2,t);
	return 1;
}

static void add_negative_openings(Scene *s, mat4 wallM, float L,float H,float T,
		Opening **op,int *nop,int *cop){
	mat4 inv;
	if(!mat4_inverse_affine(wallM,&inv)) return;
	for(int i=0;i<s->nnegativeBoxes;i++){
		NegativeBox *b=&s->negativeBoxes[i];
		mat4 local=mat4_mul(inv,b->transform);
		vec3 ax=vnorm(mat4_xform_dir(local,v3(1,0,0)));
		vec3 ay=vnorm(mat4_xform_dir(local,v3(0,1,0)));
		vec3 az=vnorm(mat4_xform_dir(local,v3(0,0,1)));
		if(fabsf(ax.x)<0.999f || fabsf(ay.y)<0.999f || fabsf(az.z)<0.999f) continue;
		vec3 half=vscale(b->size,0.5f);
		vec3 lo=v3(INFINITY,INFINITY,INFINITY), hi=v3(-INFINITY,-INFINITY,-INFINITY);
		for(int x=-1;x<=1;x+=2) for(int y=-1;y<=1;y+=2) for(int z=-1;z<=1;z+=2){
			vec3 p=mat4_xform_point(local,v3(half.x*x,half.y*y,half.z*z));
			if(p.x<lo.x) lo.x=p.x;
			if(p.x>hi.x) hi.x=p.x;
			if(p.y<lo.y) lo.y=p.y;
			if(p.y>hi.y) hi.y=p.y;
			if(p.z<lo.z) lo.z=p.z;
			if(p.z>hi.z) hi.z=p.z;
		}
		if(lo.z>-T*0.5f+0.001f || hi.z<T*0.5f-0.001f) continue;
		if(hi.x<=-L*0.5f || lo.x>=L*0.5f || hi.y<=0 || lo.y>=H) continue;
		if(lo.x<-L*0.5f) lo.x=-L*0.5f;
		if(hi.x>L*0.5f) hi.x=L*0.5f;
		if(lo.y<0) lo.y=0;
		if(hi.y>H) hi.y=H;
		Opening o; memset(&o,0,sizeof(o));
		o.x=lo.x+L*0.5f; o.width=hi.x-lo.x; o.height=hi.y-lo.y; o.sill=lo.y; o.type=OPENING_RECT;
		if(o.width>0.001f && o.height>0.001f) DA_PUSH(*op,*nop,*cop,o);
	}
}

static void add_negative_arch_openings(Scene *s, mat4 wallM, float L,float H,float T,
		Opening **op,int *nop,int *cop){
	mat4 inv;
	if(!mat4_inverse_affine(wallM,&inv)) return;
	for(int i=0;i<s->nnegativeArches;i++){
		NegativeArch *a=&s->negativeArches[i];
		mat4 local=mat4_mul(inv,a->transform);
		vec3 ax=vnorm(mat4_xform_dir(local,v3(1,0,0)));
		vec3 ay=vnorm(mat4_xform_dir(local,v3(0,1,0)));
		vec3 az=vnorm(mat4_xform_dir(local,v3(0,0,1)));
		if(fabsf(ax.x)<0.999f || fabsf(ay.y)<0.999f || fabsf(az.z)<0.999f) continue;
		vec3 center=mat4_xform_point(local,v3(0,0,0));
		float halfW=a->width*0.5f, halfH=a->height*0.5f, halfD=a->depth*0.5f;
		if(center.z-halfD>-T*0.5f+0.001f || center.z+halfD<T*0.5f-0.001f) continue;
		float loX=center.x-halfW, hiX=center.x+halfW;
		float loY=center.y-halfH, hiY=center.y+halfH;
		if(hiX<=-L*0.5f || loX>=L*0.5f || hiY<=0 || loY>=H) continue;
		if(loX<-L*0.5f) loX=-L*0.5f;
		if(hiX>L*0.5f) hiX=L*0.5f;
		if(loY<0) loY=0;
		if(hiY>H) hiY=H;
		Opening o; memset(&o,0,sizeof(o));
		o.x=loX+L*0.5f; o.width=hiX-loX; o.height=hiY-loY; o.sill=loY; o.type=OPENING_ARCH;
		if(o.width>0.001f && o.height>0.001f) DA_PUSH(*op,*nop,*cop,o);
	}
}

static void add_negative_cylinder_openings(Scene *s, mat4 wallM, float L,float H,float T,
		Opening **op,int *nop,int *cop){
	mat4 inv;
	if(!mat4_inverse_affine(wallM,&inv)) return;
	for(int i=0;i<s->nnegativeCylinders;i++){
		NegativeCylinder *c=&s->negativeCylinders[i];
		mat4 local=mat4_mul(inv,c->transform);
		vec3 ax=vnorm(mat4_xform_dir(local,v3(1,0,0)));
		vec3 ay=vnorm(mat4_xform_dir(local,v3(0,1,0)));
		vec3 az=vnorm(mat4_xform_dir(local,v3(0,0,1)));
		if(fabsf(ax.x)<0.999f || fabsf(ay.y)<0.999f || fabsf(az.z)<0.999f) continue;
		vec3 center=mat4_xform_point(local,v3(0,0,0));
		float halfD=c->depth*0.5f;
		if(center.z-halfD>-T*0.5f+0.001f || center.z+halfD<T*0.5f-0.001f) continue;
		float r=c->radius;
		float loX=center.x-r, hiX=center.x+r;
		float loY=center.y-r, hiY=center.y+r;
		if(hiX<=-L*0.5f || loX>=L*0.5f || hiY<=0 || loY>=H) continue;
		if(loX<-L*0.5f) loX=-L*0.5f;
		if(hiX>L*0.5f) hiX=L*0.5f;
		if(loY<0) loY=0;
		if(hiY>H) hiY=H;
		Opening o; memset(&o,0,sizeof(o));
		o.x=loX+L*0.5f; o.width=hiX-loX; o.height=hiY-loY; o.sill=loY;
		o.type=OPENING_CYLINDER; o.cylR=r;
		if(o.width>0.001f && o.height>0.001f) DA_PUSH(*op,*nop,*cop,o);
	}
}

static void add_negative_profile_openings(Scene *s,mat4 wallM,float L,float H,float T,Opening **op,int *nop,int *cop){
	mat4 inv;
	if(!mat4_inverse_affine(wallM,&inv)) return;
	for(int i=0;i<s->nnegativeProfiles;i++){
		negative_profile_t *p=&s->negativeProfiles[i];
		mat4 local=mat4_mul(inv,p->transform);
		vec3 ax=mat4_xform_dir(local,v3(1,0,0)),ay=mat4_xform_dir(local,v3(0,1,0)),az=mat4_xform_dir(local,v3(0,0,1));
		if(vlen(ax)<=WINDOW_EPSILON||vlen(ay)<=WINDOW_EPSILON||vlen(az)<=WINDOW_EPSILON) continue;
		if(fabsf(vnorm(ax).z)>WINDOW_ALIGNMENT_EPSILON||fabsf(vnorm(ay).z)>WINDOW_ALIGNMENT_EPSILON||
			fabsf(vnorm(az).z)<1-WINDOW_ALIGNMENT_EPSILON) continue;
		vec3 center=mat4_xform_point(local,v3(0,0,0));
		float halfDepth=fabsf(az.z)*p->depth/2;
		if(center.z-halfDepth>T/2+WINDOW_EPSILON||center.z+halfDepth<-T/2-WINDOW_EPSILON) continue;
		Opening o={0}; o.type=OPENING_PROFILE; o.profile.closed=1;
		float loX=INFINITY,hiX=-INFINITY,loY=INFINITY,hiY=-INFINITY;
		int reversed=ax.x*ay.y-ax.y*ay.x<0;
		for(int j=0;j<p->profile.npts;j++){
			vec3 v=mat4_xform_point(local,p->profile.pts[reversed?p->profile.npts-1-j:j]); v.z=0;
			DA_PUSH(o.profile.pts,o.profile.npts,o.profile.cpts,v);
			loX=fminf(loX,v.x); hiX=fmaxf(hiX,v.x); loY=fminf(loY,v.y); hiY=fmaxf(hiY,v.y);
		}
		if(hiX<=-L/2||loX>=L/2||hiY<=0||loY>=H){ shape2d_free(&o.profile); continue; }
		o.x=loX+L/2; o.width=hiX-loX; o.height=hiY-loY; o.sill=loY;
		DA_PUSH(*op,*nop,*cop,o);
	}
}

typedef struct { vec3 color; float shin; int texture; } surface_material_t;

static surface_material_t surface_material(Scene *s,XmlNode *n,const char *prefix,surface_material_t fallback){
	char key[SURFACE_NAME_CAPACITY];
	snprintf(key,sizeof(key),"%sMaterial",prefix);
	const char *name=xml_attr(n,key,NULL);
	Material *m=find_material(s,name);
	if(name&&!m) fprintf(stderr,"[scener] %s: unknown %s '%s'\n",n->tag,key,name);
	if(m){ fallback.color=m->color; fallback.shin=m->shininess; fallback.texture=materials_index_for_name(m->id); }
	snprintf(key,sizeof(key),"%sColor",prefix);
	fallback.color=xml_attr_v3(n,key,fallback.color);
	return fallback;
}

static void surface_add(Scene *s,Mesh mesh,mat4 M,mat4 R,surface_material_t material,int shadow,int visible,int unlit){
	if(!mesh.ntris){ mesh_free(&mesh); return; }
	int texture=s->activeTexIndex; s->activeTexIndex=material.texture;
	scene_add_obj(s,mesh,M,R,material.color,material.shin,shadow,visible,unlit);
	s->activeTexIndex=texture;
}

static Shape2D wall_opening_profile(const Opening *o,float length){
	Shape2D p={0}; p.closed=1;
	if(o->type==OPENING_PROFILE){
		for(int j=0;j<o->profile.npts;j++) DA_PUSH(p.pts,p.npts,p.cpts,o->profile.pts[j]);
		return p;
	}
	if(o->type==OPENING_CYLINDER){
		for(int j=0;j<WINDOW_DEFAULT_SEGMENTS;j++){
			float a=(float)j/WINDOW_DEFAULT_SEGMENTS*2*M_PIf;
			vec3 v=v3(cosf(a)*o->cylR,sinf(a)*o->cylR,0); DA_PUSH(p.pts,p.npts,p.cpts,v);
		}
	} else p=shape2d_window(o->type==OPENING_ARCH?WINDOW_ROUND_ARCH:WINDOW_RECTANGLE,o->width,o->height,WINDOW_DEFAULT_SEGMENTS);
	for(int j=0;j<p.npts;j++) p.pts[j]=vadd(p.pts[j],v3(o->x-length/2+o->width/2,o->sill+o->height/2,0));
	return p;
}

static void wall_band(Scene *s,mat4 M,mat4 R,float length,float y0,float y1,float depth,float z,
		Shape2D *holes,int nholes,surface_material_t material,int shadow,int visible,int unlit){
	if(y1-y0<=SURFACE_EPSILON) return;
	Shape2D boundary=shape2d_window(WINDOW_RECTANGLE,length,y1-y0,WINDOW_MIN_SEGMENTS);
	for(int i=0;i<boundary.npts;i++) boundary.pts[i].y+=(y0+y1)/2;
	Mesh mesh=gen_profile_cutouts(&boundary,holes,nholes,depth);
	surface_add(s,mesh,mat4_mul(M,mat4_translate(v3(0,0,z))),R,material,shadow,visible,unlit);
	shape2d_free(&boundary);
}

static void parse_wall(Scene *s,XmlNode *n,mat4 M,mat4 R,mat4 parentM,vec3 pos,vec3 rot,vec3 color,float shin,int castsShadow,int renderable,int unlit){
	(void)parentM; (void)pos; (void)rot;
	float L=xml_attr_f_cm(n,"length",WALL_DEFAULT_LENGTH),H=xml_attr_f_cm(n,"height",WALL_DEFAULT_HEIGHT);
	float T=xml_attr_f_cm(n,"thickness",WALL_DEFAULT_THICKNESS),lower=xml_attr_f_cm(n,"lowerHeight",0);
	float trimDepth=xml_attr_f_cm(n,"trimDepth",WALL_DEFAULT_TRIM_DEPTH);
	const char *side=xml_attr(n,"trimSide","front");
	const char *prefixes[]={"bottomTrim","middleTrim","topTrim"};
	float heights[3],depths[3];
	int valid=isfinite(L)&&L>SURFACE_EPSILON&&isfinite(H)&&H>SURFACE_EPSILON&&isfinite(T)&&T>SURFACE_EPSILON;
	valid&=isfinite(lower)&&lower>=0&&lower<=H&&isfinite(trimDepth)&&trimDepth>SURFACE_EPSILON&&!n->nkids;
	valid&=!strcmp(side,"front")||!strcmp(side,"back")||!strcmp(side,"both");
	for(int i=0;i<(int)(sizeof(prefixes)/sizeof(prefixes[0]));i++){
		char key[SURFACE_NAME_CAPACITY];
		snprintf(key,sizeof(key),"%sHeight",prefixes[i]); heights[i]=xml_attr_f_cm(n,key,0);
		snprintf(key,sizeof(key),"%sDepth",prefixes[i]); depths[i]=xml_attr_f_cm(n,key,trimDepth);
		valid&=isfinite(heights[i])&&heights[i]>=0&&heights[i]<=H&&isfinite(depths[i])&&depths[i]>SURFACE_EPSILON;
	}
	valid&=heights[1]==0||(lower-heights[1]/2>=heights[0]&&lower+heights[1]/2<=H-heights[2]);
	valid&=heights[0]+heights[2]<=H;
	if(!valid){ fprintf(stderr,"[scener] wall: invalid dimensions, overlapping trims, trimSide or children\n"); return; }
	Opening *op=NULL; int nop=0,cop=0;
	add_negative_openings(s,M,L,H,T,&op,&nop,&cop);
	add_negative_arch_openings(s,M,L,H,T,&op,&nop,&cop);
	add_negative_cylinder_openings(s,M,L,H,T,&op,&nop,&cop);
	add_negative_profile_openings(s,M,L,H,T,&op,&nop,&cop);
	Shape2D *holes=NULL; int nholes=0,choles=0;
	for(int i=0;i<nop;i++){
		Shape2D p=wall_opening_profile(&op[i],L); DA_PUSH(holes,nholes,choles,p);
		shape2d_free(&op[i].profile);
	}
	free(op);
	surface_material_t base={color,shin,s->activeTexIndex};
	wall_band(s,M,R,L,0,lower,T,0,holes,nholes,surface_material(s,n,"lower",base),castsShadow,renderable,unlit);
	wall_band(s,M,R,L,lower,H,T,0,holes,nholes,surface_material(s,n,"upper",base),castsShadow,renderable,unlit);
	surface_material_t trim=surface_material(s,n,"trim",base);
	float starts[]={0,lower-heights[1]/2,H-heights[2]};
	for(int i=0;i<(int)(sizeof(prefixes)/sizeof(prefixes[0]));i++){
		if(heights[i]<=SURFACE_EPSILON) continue;
		surface_material_t mat=surface_material(s,n,prefixes[i],trim);
		for(int sign=-1;sign<=1;sign+=2){
			if((sign<0&&!strcmp(side,"front"))||(sign>0&&!strcmp(side,"back"))) continue;
			/* Match cutters against the wall once; shallow frames must also cut projecting trims. */
			wall_band(s,M,R,L,starts[i],starts[i]+heights[i],depths[i],sign*(T+depths[i])/2,holes,nholes,mat,castsShadow,renderable,unlit);
		}
	}
	for(int i=0;i<nholes;i++) shape2d_free(&holes[i]);
	free(holes);
}

static float floor_random(int row,int column,unsigned seed){
	unsigned value=seed^(unsigned)row*FLOOR_HASH_ROW^(unsigned)column*FLOOR_HASH_COLUMN;
	value^=value>>FLOOR_HASH_SHIFT; value*=FLOOR_HASH_MIX; value^=value>>FLOOR_HASH_SHIFT;
	return (float)(value&FLOOR_HASH_MASK)/FLOOR_HASH_MASK;
}

typedef enum { FLOOR_BOARDS,FLOOR_SQUARES,FLOOR_HEXES,FLOOR_STONES } floor_style_t;
static const struct { const char *name; floor_style_t style; } floor_styles[]={
	{"boards",FLOOR_BOARDS},{"squares",FLOOR_SQUARES},{"hexes",FLOOR_HEXES},{"stones",FLOOR_STONES}
};

static Shape2D floor_cell(floor_style_t style,float x,float y,float width,float length,float bevel){
	Shape2D p={0}; p.closed=1;
	if(style==FLOOR_HEXES){
		for(int i=0;i<FLOOR_HEX_SIDES;i++){
			float a=M_PIf/2+(float)i/FLOOR_HEX_SIDES*2*M_PIf;
			vec3 v=v3(x+cosf(a)*width/FLOOR_SQRT_THREE,y+sinf(a)*width/FLOOR_SQRT_THREE,0);
			DA_PUSH(p.pts,p.npts,p.cpts,v);
		}
	} else if(style==FLOOR_STONES){
		vec3 points[]={v3(-width/2+bevel,-length/2,0),v3(width/2-bevel,-length/2,0),
			v3(width/2,-length/2+bevel,0),v3(width/2,length/2-bevel,0),
			v3(width/2-bevel,length/2,0),v3(-width/2+bevel,length/2,0),
			v3(-width/2,length/2-bevel,0),v3(-width/2,-length/2+bevel,0)};
		for(int i=0;i<(int)(sizeof(points)/sizeof(points[0]));i++){
			vec3 v=vadd(points[i],v3(x,y,0)); DA_PUSH(p.pts,p.npts,p.cpts,v);
		}
	} else {
		p=shape2d_window(WINDOW_RECTANGLE,width,length,WINDOW_MIN_SEGMENTS);
		for(int i=0;i<p.npts;i++) p.pts[i]=vadd(p.pts[i],v3(x,y,0));
	}
	return p;
}

static void parse_floor(Scene *s,XmlNode *n,mat4 M,mat4 R,mat4 parentM,vec3 pos,vec3 rot,vec3 color,float shin,int castsShadow,int renderable,int unlit){
	(void)parentM; (void)pos; (void)rot;
	const char *name=xml_attr(n,"style","boards"); int style=-1;
	for(int i=0;i<(int)(sizeof(floor_styles)/sizeof(floor_styles[0]));i++) if(!strcmp(name,floor_styles[i].name)) style=floor_styles[i].style;
	float width=xml_attr_f_cm(n,"width",FLOOR_DEFAULT_WIDTH),depth=xml_attr_f_cm(n,"depth",FLOOR_DEFAULT_DEPTH);
	float thickness=xml_attr_f_cm(n,"thickness",FLOOR_DEFAULT_THICKNESS),tileDepth=xml_attr_f_cm(n,"tileDepth",FLOOR_DEFAULT_TILE_DEPTH);
	float tileWidth=xml_attr_f_cm(n,"tileWidth",FLOOR_DEFAULT_TILE_WIDTH);
	float tileLength=xml_attr_f_cm(n,"tileLength",style==FLOOR_BOARDS?FLOOR_DEFAULT_TILE_LENGTH:tileWidth);
	float gap=xml_attr_f_cm(n,"gap",FLOOR_DEFAULT_GAP),variation=xml_attr_f(n,"colorVariation",FLOOR_DEFAULT_VARIATION);
	unsigned seed=(unsigned)xml_attr_i(n,"seed",1);
	float values[]={width,depth,thickness,tileDepth,tileWidth,tileLength}; int valid=style>=0&&!n->nkids;
	for(int i=0;i<(int)(sizeof(values)/sizeof(values[0]));i++) valid&=isfinite(values[i])&&values[i]>SURFACE_EPSILON;
	valid&=thickness-tileDepth>SURFACE_EPSILON&&isfinite(gap)&&gap>=0&&gap<fminf(tileWidth,tileLength)/2;
	valid&=isfinite(variation)&&variation>=0&&variation<=1;
	if((style==FLOOR_SQUARES||style==FLOOR_HEXES)&&xml_attr(n,"tileLength",NULL)) valid&=tileLength==tileWidth;
	float step=style==FLOOR_HEXES?tileWidth/FLOOR_SQRT_THREE*FLOOR_HEX_ROW_STEP:tileWidth;
	float across=style==FLOOR_HEXES?tileWidth:tileLength;
	double rows=ceil((double)depth/step)+2,columns=ceil((double)width/across)+2;
	valid&=isfinite(rows)&&isfinite(columns)&&rows*columns<=FLOOR_MAX_CELLS;
	if(!valid){ fprintf(stderr,"[scener] floor: invalid style/dimensions, children, colorVariation or cell count (limit %d)\n",FLOOR_MAX_CELLS); return; }
	surface_material_t base={color,shin,s->activeTexIndex};
	surface_material_t grout=surface_material(s,n,"grout",base);
	surface_add(s,gen_box(width,thickness-tileDepth,depth),mat4_mul(M,mat4_translate(v3(0,-(thickness+tileDepth)/2,0))),R,grout,castsShadow,renderable,unlit);
	mat4 plane=mat4_rot_xyz(v3(FLOOR_PLANE_ROTATION,0,0));
	mat4 tileM=mat4_mul(M,mat4_mul(mat4_translate(v3(0,-tileDepth/2,0)),plane)),tileR=mat4_mul(R,plane);
	for(int row=-1;row<(int)rows-1;row++) for(int column=-1;column<(int)columns-1;column++){
		float x=-width/2+(column+1)*across,y=-depth/2+(row+1)*step;
		if(style!=FLOOR_SQUARES) x+=(row&1)*across/2;
		float cellWidth=across,cellLength=step;
		if(style==FLOOR_STONES){
			float left=(floor_random(row,column,seed)-1.0f/2)*across*FLOOR_STONE_JITTER;
			float right=(floor_random(row,column+1,seed)-1.0f/2)*across*FLOOR_STONE_JITTER;
			x+=(left+right)/2; cellWidth+=right-left;
		}
		float bevel=fminf(cellWidth,cellLength)*FLOOR_STONE_BEVEL*(1+floor_random(row,column,seed));
		Shape2D cell=floor_cell(style,x,y,cellWidth,cellLength,bevel);
		Shape2D inset=shape2d_inset(&cell,gap/2),clipped=shape2d_clip_rect(&inset,width,depth);
		Mesh mesh=gen_profile_extrusion(&clipped,tileDepth);
		surface_material_t tile=base;
		float factor=1+(2*floor_random(row,column,seed)-1)*variation;
		tile.color=v3(fminf(1,fmaxf(0,color.x*factor)),fminf(1,fmaxf(0,color.y*factor)),fminf(1,fmaxf(0,color.z*factor)));
		surface_add(s,mesh,tileM,tileR,tile,castsShadow,renderable,unlit);
		shape2d_free(&cell); shape2d_free(&inset); shape2d_free(&clipped);
	}
}

static void parse_line(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)R; (void)parentM; (void)pos; (void)rot; (void)color; (void)shin; (void)castsShadow; (void)renderable; (void)unlit;
	vec3 lcolor=xml_attr_v3(n,"color",v3(0.85f,0.15f,0.15f));
	scene_add_overlay_line(s,
		mat4_xform_point(M,xml_attr_v3_cm(n,"start",v3(0,0,0))),
		mat4_xform_point(M,xml_attr_v3_cm(n,"end",v3(0,1,0))),
		lcolor,0,xml_attr(n,"camera",NULL));
}

static void parse_dummy(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)R; (void)parentM; (void)pos; (void)rot; (void)color; (void)shin; (void)castsShadow; (void)renderable; (void)unlit;
	vec3 lcolor=xml_attr_v3(n,"color",v3(0.85f,0.15f,0.15f));
	const char *type=xml_attr(n,"type","character");
	if(!strcmp(type,"character")){
		const char *ref=xml_attr(n,"ref",NULL);
		CharDef inlineDef={0};
		inlineDef.height=xml_attr_f_cm(n,"height",0.5f);
		inlineDef.radius=xml_attr_f_cm(n,"radius",inlineDef.height*0.10f);
		inlineDef.top=xml_attr_f_cm(n,"top",1.0f);
		inlineDef.neck=xml_attr_f_cm(n,"neck",0.75f);
		inlineDef.pelvis=xml_attr_f_cm(n,"pelvis",0.25f);
		inlineDef.feet=xml_attr_f_cm(n,"feet",0.0f);
		CharDef *cd=&inlineDef;
		if(ref){
			cd=NULL;
			for(int i=0;i<s->ncharDefs;i++) if(!strcmp(s->charDefs[i].name,ref)){ cd=&s->charDefs[i]; break; }
		}
		if(cd){
			const char *targetText=xml_attr(n,"target",NULL);
			add_character_dummy(s,M,cd,lcolor,xml_attr(n,"camera",NULL),xml_attr(n,"pose","stand"),
				targetText!=NULL,xml_attr_v3_cm(n,"target",v3(0,0,0)));
		}
	} else if(!strcmp(type,"lamp")){
		add_lamp_dummy(s,mat4_xform_point(M,v3(0,0,0)),xml_attr_f_cm(n,"radius",0.15f),lcolor,1,xml_attr(n,"camera",NULL));
	} else if(!strcmp(type,"camera")){
		add_camera_dummy(s,mat4_xform_point(M,v3(0,0,0)),xml_attr_v3_cm(n,"look",v3(0,0,-1)),xml_attr_f(n,"fov",60.0f),1.0f,lcolor,2,xml_attr(n,"camera",NULL));
	}
}

static XmlNode* load_prefab(Scene *s, const char *name){
	for(int i=0;i<s->nprefabs;i++)
		if(!strcmp(s->prefabs[i].ref,name)) return (XmlNode*)s->prefabs[i].root;
	char path[1024];
	snprintf(path,sizeof(path),"%s%sprefabs/%s.blk",s->assetRoot,
		s->assetRoot[0]?"/":"",name);
	char *buf=read_file(path);
	if(!buf) return NULL;
	XmlNode *root=xml_parse(buf);
	free(buf);
	if(!root) return NULL;
	warn_unknown_elements(root,path,1);
	PrefabDef pd; memset(&pd,0,sizeof(pd)); strncpy(pd.ref,name,31); pd.root=root;
	strncpy(pd.path,path,sizeof(pd.path)-1);
	for(int i=0;i<root->nkids;i++){
		if(!strcmp(root->kids[i]->tag,"attach")){
			AttachPoint ap;
			strncpy(ap.name,xml_attr(root->kids[i],"name",""),31);
			ap.pos=xml_attr_v3_cm(root->kids[i],"pos",v3(0,0,0));
			DA_PUSH(pd.attaches,pd.nattaches,pd.cattaches,ap);
		}
	}
	DA_PUSH(s->prefabs,s->nprefabs,s->cprefabs,pd);
	collect_shapes_from_tree(s,root);
	return root;
}

static void apply_camera_transform(Scene *s,XmlNode *n,mat4 *M,mat4 *R){
	const char *name=xml_attr(n,"name",NULL);
	if(!name) return;
	vec3 pvt=xml_attr_v3_cm(n,"pivotOffset",v3(0,0,0));
	for(int c=0;c<s->ncameras;c++){
		Camera *cam=&s->cameras[c];
		if(strcmp(cam->name,s->activeCamera)) continue;
		for(int t=0;t<cam->ntransforms;t++){
			CameraTransform *x=&cam->transforms[t];
			if(strcmp(x->target,name)) continue;
			mat4 Tp=mat4_translate(pvt),Tn=mat4_translate(vscale(pvt,-1));
			mat4 D=mat4_mul(mat4_translate(x->pos),mat4_mul(Tp,
				mat4_mul(mat4_rot_xyz(x->rot),mat4_mul(Tn,mat4_scale(x->scale)))));
			*M=mat4_mul(*M,D);
			if(R) *R=mat4_mul(*R,mat4_rot_xyz(x->rot));
		}
		break;
	}
}

static XmlNode *rig_find_joint(XmlNode *root,const char *name){
	if(!root || !name || !*name) return NULL;
	if(!strcmp(root->tag,"group") && !strcmp(xml_attr(root,"name",""),name)) return root;
	for(int i=0;i<root->nkids;i++){
		XmlNode *found=rig_find_joint(root->kids[i],name);
		if(found) return found;
	}
	return NULL;
}

static XmlNode *rig_override_in(XmlNode *container,const char *joint){
	if(!container) return NULL;
	for(int i=0;i<container->nkids;i++) if(!strcmp(container->kids[i]->tag,"joint") &&
		!strcmp(xml_attr(container->kids[i],"target",""),joint)) return container->kids[i];
	return NULL;
}
static XmlNode *rig_ik_for_tip(XmlNode *container,const char *tip);

static XmlNode *rig_pose_for_instance(Scene *s,XmlNode *instance){
	XmlNode *root=(XmlNode*)s->sceneRoot;
	if(!root || !instance) return NULL;
	const char *poseName=xml_attr(instance,"pose",NULL),*instanceName=xml_attr(instance,"name",NULL);
	for(int j=0;j<root->nkids;j++) if(!strcmp(root->kids[j]->tag,"camera") &&
		!strcmp(xml_attr(root->kids[j],"name",""),s->activeCamera)){
		XmlNode *camera=root->kids[j];
		for(int k=0;k<camera->nkids;k++) if(!strcmp(camera->kids[k]->tag,"use-pose") &&
			!strcmp(xml_attr(camera->kids[k],"instance",""),instanceName?instanceName:"")) poseName=xml_attr(camera->kids[k],"name",poseName);
	}
	for(int i=0;poseName && i<root->nkids;i++) if(!strcmp(root->kids[i]->tag,"pose") &&
		!strcmp(xml_attr(root->kids[i],"name",""),poseName)) return root->kids[i];
	return NULL;
}

static mat4 rig_rotation(Scene *s,XmlNode *node){
	for(int i=0;i<s->nrigRotations;i++) if(s->rigRotations[i].node==node) return s->rigRotations[i].rotation;
	return mat4_identity();
}

static void rig_set_rotation(Scene *s,XmlNode *node,mat4 rotation){
	for(int i=0;i<s->nrigRotations;i++) if(s->rigRotations[i].node==node){ s->rigRotations[i].rotation=rotation; return; }
	RigRotation entry={node,rotation}; DA_PUSH(s->rigRotations,s->nrigRotations,s->crigRotations,entry);
}

static mat4 rig_local(Scene *s,XmlNode *node){
	mat4 M=xml_node_transform(s,node);
	const char *name=xml_attr(node,"name",NULL);
	if(!name) return M;
	XmlNode *override=rig_override_in((XmlNode*)s->activeRigInstance,name);
	if(!override) override=rig_override_in((XmlNode*)s->activeRigPose,name);
	if(override){
		mat4 delta=mat4_mul(mat4_translate(cvt3ds(s,xml_attr_v3_cm(override,"pos",v3(0,0,0)))),
			mat4_rot_xyz(cvt3ds(s,xml_attr_v3(override,"rot",v3(0,0,0)))));
		M=mat4_mul(M,delta);
	}
	return mat4_mul(M,rig_rotation(s,node));
}

static mat4 rig_world(Scene *s,XmlNode *node,XmlNode *root,mat4 instanceM){
	if(!node || node==root) return instanceM;
	return mat4_mul(rig_world(s,node->parent,root,instanceM),rig_local(s,node));
}

static mat4 rig_from_to(vec3 from,vec3 to,vec3 fallback){
	vec3 a=vnorm(from),b=vnorm(to); float d=fmaxf(-1,fminf(1,vdot(a,b)));
	vec3 axis=vcross(a,b); float length=vlen(axis);
	if(length<RIG_EPSILON){
		if(d>0) return mat4_identity();
		axis=vcross(a,fallback);
		if(vlen(axis)<RIG_EPSILON) axis=vcross(a,fabsf(a.x)<0.9f?v3(1,0,0):v3(0,1,0));
		axis=vnorm(axis);
	} else axis=vscale(axis,1.0f/length);
	float angle=acosf(d),c=cosf(angle),sn=sinf(angle),t=1-c;
	mat4 m=mat4_identity();
	m.m[0]=t*axis.x*axis.x+c;        m.m[4]=t*axis.x*axis.y-sn*axis.z; m.m[8]=t*axis.x*axis.z+sn*axis.y;
	m.m[1]=t*axis.x*axis.y+sn*axis.z; m.m[5]=t*axis.y*axis.y+c;        m.m[9]=t*axis.y*axis.z-sn*axis.x;
	m.m[2]=t*axis.x*axis.z-sn*axis.y; m.m[6]=t*axis.y*axis.z+sn*axis.x; m.m[10]=t*axis.z*axis.z+c;
	return m;
}

static void rig_solve_ik(Scene *s,XmlNode *ik,XmlNode *root,mat4 instanceM){
	XmlNode *hip=rig_find_joint(root,xml_attr(ik,"root",""));
	XmlNode *knee=rig_find_joint(root,xml_attr(ik,"mid",""));
	XmlNode *tip=rig_find_joint(root,xml_attr(ik,"tip",""));
	if(!hip || !knee || !tip || knee->parent!=hip || tip->parent!=knee){
		fprintf(stderr,"[scener] invalid IK chain root=%s mid=%s tip=%s\n",xml_attr(ik,"root",""),xml_attr(ik,"mid",""),xml_attr(ik,"tip",""));
		fflush(stderr); return;
	}
	mat4 H=rig_world(s,hip,root,instanceM),K=rig_world(s,knee,root,instanceM),T=rig_world(s,tip,root,instanceM),originalT=T;
	vec3 h=mat4_xform_point(H,v3(0,0,0)),k=mat4_xform_point(K,v3(0,0,0)),t=mat4_xform_point(T,v3(0,0,0));
	float upper=vlen(vsub(k,h)),lower=vlen(vsub(t,k));
	vec3 goal=xml_attr_v3_cm(ik,"target",t),pole=xml_attr_v3(ik,"pole",v3(0,-1,0));
	if(!isfinite(upper) || !isfinite(lower) || upper<RIG_EPSILON || lower<RIG_EPSILON ||
		!isfinite(goal.x) || !isfinite(goal.y) || !isfinite(goal.z) ||
		!isfinite(pole.x) || !isfinite(pole.y) || !isfinite(pole.z)){
		fprintf(stderr,"[scener] invalid IK bone or target for tip=%s\n",xml_attr(tip,"name","")); fflush(stderr); return;
	}
	vec3 travel=vsub(goal,h); float requested=vlen(travel);
	vec3 direction=requested>RIG_EPSILON?vscale(travel,1.0f/requested):vnorm(vsub(t,h));
	float minReach=fabsf(upper-lower)+RIG_REACH_MARGIN,maxReach=upper+lower-RIG_REACH_MARGIN;
	if(maxReach<minReach) maxReach=minReach;
	float distance=fmaxf(minReach,fminf(maxReach,requested));
	vec3 bend=vsub(pole,vscale(direction,vdot(pole,direction)));
	if(vlen(bend)<RIG_EPSILON){ bend=vsub(vsub(k,h),vscale(direction,vdot(vsub(k,h),direction))); }
	if(vlen(bend)<RIG_EPSILON) bend=vcross(direction,fabsf(direction.x)<0.9f?v3(1,0,0):v3(0,1,0));
	bend=vnorm(bend);
	float along=(upper*upper-lower*lower+distance*distance)/(2*distance);
	float height=sqrtf(fmaxf(0,upper*upper-along*along));
	vec3 desiredK=vadd(h,vadd(vscale(direction,along),vscale(bend,height)));
	vec3 desiredT=vadd(h,vscale(direction,distance));
	mat4 invH=mat4_affine_inverse(H);
	mat4 first=rig_from_to(mat4_xform_dir(invH,vsub(k,h)),mat4_xform_dir(invH,vsub(desiredK,h)),bend);
	rig_set_rotation(s,hip,mat4_mul(rig_rotation(s,hip),first));
	K=rig_world(s,knee,root,instanceM); T=rig_world(s,tip,root,instanceM);
	k=mat4_xform_point(K,v3(0,0,0)); t=mat4_xform_point(T,v3(0,0,0));
	mat4 invK=mat4_affine_inverse(K);
	mat4 second=rig_from_to(mat4_xform_dir(invK,vsub(t,k)),mat4_xform_dir(invK,vsub(desiredT,k)),bend);
	rig_set_rotation(s,knee,mat4_mul(rig_rotation(s,knee),second));
	if(xml_attr_i(ik,"keepOrientation",0)){
		mat4 newT=rig_world(s,tip,root,instanceM);
		mat4 correction=mat4_mul(mat4_affine_inverse(newT),originalT);
		correction.m[12]=correction.m[13]=correction.m[14]=0;
		rig_set_rotation(s,tip,correction);
	}
	RigTargetStatus status={0};
	snprintf(status.instance,sizeof(status.instance),"%s",xml_attr((XmlNode*)s->activeRigInstance,"name",""));
	snprintf(status.joint,sizeof(status.joint),"%s",xml_attr(tip,"name",""));
	status.target=goal; status.error=fabsf(requested-distance);
	status.reachable=requested<=upper+lower+RIG_EPSILON && requested>=fabsf(upper-lower)-RIG_EPSILON;
	DA_PUSH(s->rigTargets,s->nrigTargets,s->crigTargets,status);
	if(!status.reachable){ fprintf(stderr,"[scener] IK %s:%s target out of reach by %.3f m\n",status.instance,status.joint,status.error); fflush(stderr); }
}

static void collect_negative_boxes(Scene *s, XmlNode *parent, mat4 parentM){
	for(int i=0;i<parent->nkids;i++){
		XmlNode *n=parent->kids[i];
		vec3 pos=cvt3ds(s,xml_attr_v3_cm(n,"pos",v3(0,0,0)));
		vec3 rot=cvt3ds(s,xml_attr_v3(n,"rot",v3(0,0,0)));
		vec3 scl=xml_attr_v3(n,"scale",v3(1,1,1));
		vec3 pvt=xml_attr_v3_cm(n,"pivotOffset",v3(0,0,0));
		mat4 Tp=mat4_translate(pvt), Tn=mat4_translate(v3(-pvt.x,-pvt.y,-pvt.z));
		mat4 local=mat4_mul(mat4_translate(pos),
			mat4_mul(Tp,mat4_mul(mat4_rot_xyz(rot),mat4_mul(Tn,mat4_scale(scl)))));
		mat4 M=mat4_mul(parentM,local);
		apply_camera_transform(s,n,&M,NULL);
		if(!strcmp(n->tag,"window")){
			window_spec_t w;
			if(window_spec(n,&w)){
				if(w.cutWalls){
					negative_profile_t p={M,w.outer,w.cutDepth};
					DA_PUSH(s->negativeProfiles,s->nnegativeProfiles,s->cnegativeProfiles,p);
					memset(&w.outer,0,sizeof(w.outer));
				}
				window_spec_free(&w);
			}
		} else if(!strcmp(n->tag,"door")){
			door_spec_t d;
			if(door_spec(n,&d)){
				if(d.frame.cutWalls){
					negative_profile_t p={M,d.frame.outer,d.frame.cutDepth};
					DA_PUSH(s->negativeProfiles,s->nnegativeProfiles,s->cnegativeProfiles,p);
					memset(&d.frame.outer,0,sizeof(d.frame.outer));
				}
				door_spec_free(&d);
			}
		} else if(!strcmp(n->tag,"bool-negative-box")){
			NegativeBox b={M,cvt3ds_sz(s,xml_attr_v3_cm(n,"size",v3(1,1,1)))};
			DA_PUSH(s->negativeBoxes,s->nnegativeBoxes,s->cnegativeBoxes,b);
		} else if(!strcmp(n->tag,"bool-negative-arch")){
			NegativeArch a;
			a.transform=M;
			a.width=xml_attr_f_cm(n,"width",1.0f);
			a.height=xml_attr_f_cm(n,"height",1.5f);
			a.depth=xml_attr_f_cm(n,"depth",xml_attr_f_cm(n,"size_z",0.3f));
			DA_PUSH(s->negativeArches,s->nnegativeArches,s->cnegativeArches,a);
		} else if(!strcmp(n->tag,"bool-negative-cylinder")){
			NegativeCylinder c;
			c.transform=M;
			c.radius=xml_attr_f_cm(n,"radius",0.5f);
			c.depth=xml_attr_f_cm(n,"depth",xml_attr_f_cm(n,"size_z",0.3f));
			DA_PUSH(s->negativeCylinders,s->nnegativeCylinders,s->cnegativeCylinders,c);
		} else if(!strcmp(n->tag,"group")){
			collect_negative_boxes(s,n,M);
		} else if(!strcmp(n->tag,"prefab")){
			const char *source=xml_attr(n,"source",NULL);
			XmlNode *proot=source?load_prefab(s,source):NULL;
			if(proot) collect_negative_boxes(s,proot,M);
		}
	}
}

static void parse_prefab(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)parentM; (void)pos; (void)rot; (void)shin; (void)castsShadow; (void)renderable; (void)unlit;
	const char *source=xml_attr(n,"source",NULL);
	if(!source) return;
	XmlNode *proot=load_prefab(s,source);
	if(!proot){ fprintf(stderr,"prefab not found: %s\n",source); return; }
	void *oldRigInstance=s->activeRigInstance,*oldRigPose=s->activeRigPose,*oldRigRoot=s->activeRigRoot;
	s->activeRigInstance=n; s->activeRigRoot=proot; s->activeRigPose=rig_pose_for_instance(s,n);
	const char *name=xml_attr(n,"name",NULL);
	if(name){
		InstanceDef inst; memset(&inst,0,sizeof(inst));
		strncpy(inst.name,name,31); strncpy(inst.ref,source,31);
		inst.transform=M; inst.rotMatrix=R;
		DA_PUSH(s->instances,s->ninstances,s->cinstances,inst);
	}
	int oldTintActive=s->prefabTintActive;
	vec3 oldTint=s->prefabTint;
	const char *oldScreenImage=s->activeScreenImage;
	const char *screenImage=xml_attr(n,"screenImage",NULL);
	if(screenImage) s->activeScreenImage=screenImage;
	if(xml_attr(n,"color",NULL)){
		s->prefabTintActive=1;
		s->prefabTint=color;
	}

	int arrayCount=1;
	vec3 arrayTrans=v3(0,0,0), arrayRot=v3(0,0,0);
	for(int k=0;k<n->nkids;k++){
		if(!strcmp(n->kids[k]->tag,"array")){
			XmlNode *arr=n->kids[k];
			arrayCount=xml_attr_i(arr,"count",1);
			arrayTrans=xml_attr_v3_cm(arr,"translation",v3(0,0,0));
			arrayRot=xml_attr_v3(arr,"rotation",v3(0,0,0));
			break;
		}
	}
	for(int step=0;step<arrayCount;step++){
		vec3 off=vscale(arrayTrans,(float)step);
		vec3 r=vscale(arrayRot,(float)step);
		mat4 stepM=mat4_mul(M,mat4_mul(mat4_translate(off),mat4_rot_xyz(r)));
		s->nrigRotations=0;
		XmlNode *pose=(XmlNode*)s->activeRigPose;
		if(pose) for(int i=0;i<pose->nkids;i++) if(!strcmp(pose->kids[i]->tag,"ik") &&
			!rig_ik_for_tip(n,xml_attr(pose->kids[i],"tip",""))) rig_solve_ik(s,pose->kids[i],proot,stepM);
		for(int i=0;i<n->nkids;i++) if(!strcmp(n->kids[i]->tag,"ik")) rig_solve_ik(s,n->kids[i],proot,stepM);
		parse_nodes(s, proot, stepM, R);
	}
	s->nrigRotations=0;
	s->activeRigInstance=oldRigInstance; s->activeRigPose=oldRigPose; s->activeRigRoot=oldRigRoot;

	s->prefabTintActive=oldTintActive;
	s->prefabTint=oldTint;
	s->activeScreenImage=oldScreenImage;
}

static void parse_light(Scene *s, XmlNode *n, mat4 M, mat4 R, mat4 parentM, vec3 pos, vec3 rot, vec3 color, float shin, int castsShadow, int renderable, int unlit){
	(void)R; (void)parentM; (void)pos; (void)rot; (void)color; (void)shin; (void)castsShadow; (void)renderable; (void)unlit;
	Light L={0};
	L.pos=mat4_xform_point(M,v3(0,0,0));
	L.color=xml_attr_v3(n,"color",v3(1,1,1));
	L.intensity=xml_attr_f(n,"intensity",1.0f);
	L.radius=xml_attr_f_cm(n,"radius",0.0f);
	L.castsShadow=xml_attr_i(n,"castShadows",1);
	DA_PUSH(s->lights,s->nlights,s->clights,L);
}

static const struct {
	const char *tag;
	shape_parser_fn parse;
} shape_parsers[] = {
	{ "box",      parse_box },
	{ "rect",     parse_rect },
	{ "rounded-rect", parse_rounded_rect },
	{ "circle",   parse_circle },
	{ "ellipse",  parse_ellipse },
	{ "star",     parse_star },
	{ "screen",   parse_screen },
	{ "sphere",   parse_sphere },
	{ "cylinder", parse_cylinder },
	{ "prism",    parse_prism },
	{ "cone",     parse_cone },
	{ "pyramid",  parse_cone },
	{ "torus",    parse_torus },
	{ "arch",     parse_arch },
	{ "window",   parse_window },
	{ "door",     parse_door },
	{ "capsule",  parse_capsule },
	{ "group",    parse_group },
	{ "light",    parse_light },
	{ "prefab",   parse_prefab },
	{ "wall",     parse_wall },
	{ "floor",    parse_floor },
	{ "line",     parse_line },
	{ "dummy",    parse_dummy },
	{ "lathe",    parse_lathe },
	{ "loft",     parse_loft },
};

static void parse_nodes(Scene *s, XmlNode *parent, mat4 parentM, mat4 parentR){
	for(int i=0;i<parent->nkids;i++){
		XmlNode *n=parent->kids[i];
		void *oldEditNode=s->activeEditNode;
		mat4 oldEditMatrix=s->activeEditMatrix;
		int ownsEditNode=!s->activeEditNode;
		if(ownsEditNode) s->activeEditNode=n;
		int oldIgnore=s->sanityIgnoreActive, oldFloor=s->sanityFloorActive, oldCheck=s->sanityCheckActive;
		s->sanityIgnoreActive |= xml_attr_i(n,"sanityIgnore",0);
		s->sanityFloorActive |= xml_attr_i(n,"sanityFloor",0);
		s->sanityCheckActive |= xml_attr_i(n,"sanityCheck",0);
		char *tag=n->tag;
		vec3 pos=cvt3ds(s,xml_attr_v3_cm(n,"pos",v3(0,0,0)));
		vec3 rot=cvt3ds(s,xml_attr_v3(n,"rot",v3(0,0,0)));
		vec3 scl=xml_attr_v3(n,"scale",v3(1,1,1));
		const char *attach=xml_attr(n,"attach",NULL);
		mat4 attachM=mat4_identity(), attachRmat=mat4_identity();
		if(attach){
			const char *colon=strchr(attach,':');
			if(colon){
				int len=(int)(colon-attach);
				char instName[32]; memcpy(instName,attach,(size_t)(len<31?len:31)); instName[len]=0;
				const char *slot=colon+1;
				for(int k=0;k<s->ninstances;k++){
					if(strcmp(s->instances[k].name,instName)) continue;
					for(int m=0;m<s->nprefabs;m++){
						if(strcmp(s->prefabs[m].ref,s->instances[k].ref)) continue;
						for(int p=0;p<s->prefabs[m].nattaches;p++){
							if(strcmp(s->prefabs[m].attaches[p].name,slot)) continue;
							attachM=mat4_mul(s->instances[k].transform,
								mat4_translate(s->prefabs[m].attaches[p].pos));
							attachRmat=s->instances[k].rotMatrix;
							break;
						}
						break;
					}
					break;
				}
			}
		}
		mat4 R = mat4_mul(parentR, mat4_mul(attachRmat, mat4_rot_xyz(rot)));
		vec3 pvt=xml_attr_v3_cm(n,"pivotOffset",v3(0,0,0));
		mat4 M;
		if(pvt.x!=0.0f||pvt.y!=0.0f||pvt.z!=0.0f){
			mat4 Tp=mat4_translate(pvt);
			mat4 Tn=mat4_translate(v3(-pvt.x,-pvt.y,-pvt.z));
			M=mat4_mul(parentM, mat4_mul(attachM, mat4_mul(mat4_translate(pos),
				mat4_mul(Tp, mat4_mul(mat4_rot_xyz(rot), mat4_mul(Tn, mat4_scale(scl)))))));
		} else {
			M=mat4_mul(parentM, mat4_mul(attachM, mat4_mul(mat4_translate(pos),
				mat4_mul(mat4_rot_xyz(rot), mat4_scale(scl)))));
		}
		apply_camera_transform(s,n,&M,&R);
		if(s->activeRigInstance && !strcmp(tag,"group")){
			const char *name=xml_attr(n,"name",NULL);
			if(name){
				XmlNode *override=rig_override_in((XmlNode*)s->activeRigInstance,name);
				if(!override) override=rig_override_in((XmlNode*)s->activeRigPose,name);
				if(override){
					vec3 offset=cvt3ds(s,xml_attr_v3_cm(override,"pos",v3(0,0,0)));
					mat4 turn=mat4_rot_xyz(cvt3ds(s,xml_attr_v3(override,"rot",v3(0,0,0))));
					M=mat4_mul(M,mat4_mul(mat4_translate(offset),turn)); R=mat4_mul(R,turn);
				}
				mat4 extra=rig_rotation(s,n); M=mat4_mul(M,extra); R=mat4_mul(R,extra);
				RigJointWorld world={s->activeRigInstance,n,M};
				DA_PUSH(s->rigJointWorlds,s->nrigJointWorlds,s->crigJointWorlds,world);
			}
		}
		if(ownsEditNode) s->activeEditMatrix=M;
		const char *matName = xml_attr(n,"material",NULL);
		Material *mat = find_material(s, matName);
		s->activeTexIndex = materials_index_for_name(mat ? mat->id : matName);
		vec3 color = mat? mat->color : xml_attr_v3(n,"color",v3(0.8f,0.8f,0.8f));
		if(s->prefabTintActive && xml_attr_i(n,"tint",0)) color=s->prefabTint;
		float shin = mat? mat->shininess : xml_attr_f(n,"shininess",8.0f);
		int castsShadow = xml_attr_i(n,"castShadow",1);
		int renderable = xml_attr_i(n,"renderable",1);
		int unlit = xml_attr_i(n,"unlit",0);

		for(int j=0;j<(int)(sizeof(shape_parsers)/sizeof(shape_parsers[0]));j++){
			if(!strcmp(tag, shape_parsers[j].tag)){
				shape_parsers[j].parse(s, n, M, R, parentM, pos, rot, color, shin, castsShadow, renderable, unlit);
				break;
			}
		}
		s->sanityIgnoreActive=oldIgnore;
		s->sanityFloorActive=oldFloor;
		s->sanityCheckActive=oldCheck;
		s->activeEditNode=oldEditNode;
		s->activeEditMatrix=oldEditMatrix;
	}
}

/* ---------------------------------------------- Top-level scene tags ------ */

typedef void (*scene_tag_parser_fn)(Scene *s, XmlNode *n);

static void parse_camera_tag(Scene *s, XmlNode *n){
	Camera cam={0}; snprintf(cam.name,sizeof(cam.name),"%s",xml_attr(n,"name","Camera1"));
	strncpy(cam.comment, xml_attr(n,"comment",""), 63);
	cam.pos = cvt3ds(s,xml_attr_v3_cm(n,"pos", s->ncameras>0 ? cvt3ds_inv(s,s->camPos) : (s->convention3dsMax?v3(0,-3.0f,1.6f):v3(0,1.6f,5))));
	cam.look = cvt3ds(s,xml_attr_v3_cm(n,"look", s->ncameras>0 ? cvt3ds_inv(s,s->camLook) : (s->convention3dsMax?v3(0,1.0f,1.2f):v3(0,1.2f,0))));
	cam.fov = xml_attr_f(n,"fov",60.0f);
	for(int i=0;i<n->nkids;i++) if(!strcmp(n->kids[i]->tag,"transform")){
		CameraTransform x={0};
		strncpy(x.target,xml_attr(n->kids[i],"target",""),31);
		x.pos=xml_attr_v3_cm(n->kids[i],"pos",v3(0,0,0));
		x.rot=xml_attr_v3(n->kids[i],"rot",v3(0,0,0));
		x.scale=xml_attr_v3(n->kids[i],"scale",v3(1,1,1));
		if(x.target[0]) DA_PUSH(cam.transforms,cam.ntransforms,cam.ctransforms,x);
	}
	DA_PUSH(s->cameras,s->ncameras,s->ccameras,cam);
	if(s->ncameras==1){
		s->camPos=cam.pos; s->camLook=cam.look; s->camFov=cam.fov;
		snprintf(s->activeCamera,sizeof(s->activeCamera),"%s",cam.name);
	}
}

static const struct { const char *id; vec3 color; } preset_bgs[] = {
	{ "midnight", {0.02f,0.03f,0.07f} },
	{ "twilight", {0.06f,0.05f,0.10f} },
	{ "dusk",     {0.08f,0.10f,0.14f} },
	{ "dawn",     {0.16f,0.10f,0.14f} },
	{ "overcast", {0.25f,0.27f,0.30f} },
	{ "noon",     {0.40f,0.48f,0.64f} },
	{ "neutral",  {0.18f,0.20f,0.24f} },
	{ "black",    {0.00f,0.00f,0.00f} },
};
static const int npreset_bgs = (int)(sizeof(preset_bgs)/sizeof(preset_bgs[0]));

static void parse_material_tag(Scene *s, XmlNode *n){
	Material m={0}; strncpy(m.id, xml_attr(n,"id","mat"), 31);
	m.color = xml_attr_v3(n,"color",v3(0.8f,0.8f,0.8f));
	m.shininess = xml_attr_f(n,"shininess",8.0f);
	DA_PUSH(s->mats,s->nmats,s->cmats,m);
}

static void parse_sun_tag(Scene *s, XmlNode *n){
	Light L={0};
	L.dir = vnorm(cvt3ds(s,xml_attr_v3(n,"dir",v3(1,-1,0))));
	L.color = xml_attr_v3(n,"color",v3(1,1,1));
	L.intensity = xml_attr_f(n,"intensity",1.0f);
	L.radius = 0.0f;
	L.castsShadow = xml_attr_i(n,"castShadows",1);
	L.isDirectional = 1;
	DA_PUSH(s->lights,s->nlights,s->clights,L);
}

static void parse_chardef_tag(Scene *s, XmlNode *n){
	CharDef cd={0};
	strncpy(cd.name,xml_attr(n,"name",""),31);
	cd.height=xml_attr_f_cm(n,"height",1.0f);
	cd.radius=xml_attr_f_cm(n,"radius",0.10f);
	cd.top=xml_attr_f(n,"top",1.0f);
	cd.neck=xml_attr_f(n,"neck",0.75f);
	cd.pelvis=xml_attr_f(n,"pelvis",0.25f);
	cd.feet=xml_attr_f(n,"feet",0.0f);
	DA_PUSH(s->charDefs,s->ncharDefs,s->ccharDefs,cd);
}

vec3 light_to_source(Light *light, vec3 point){
	return light->isDirectional ? vscale(light->dir,-1.0f) : vsub(light->pos,point);
}

static const struct {
	const char *tag;
	scene_tag_parser_fn parse;
} scene_tags[] = {
	{ "camera",     parse_camera_tag },
	{ "material",   parse_material_tag },
	{ "sun",        parse_sun_tag },
	{ "chardef",    parse_chardef_tag },
	{ "shape",      parse_shape_tag },
};

static int has_shape_parser(const char *tag){
	for(int i=0;i<(int)(sizeof(shape_parsers)/sizeof(shape_parsers[0]));i++)
		if(!strcmp(tag,shape_parsers[i].tag)) return 1;
	return 0;
}

static int has_scene_parser(const char *tag){
	for(int i=0;i<(int)(sizeof(scene_tags)/sizeof(scene_tags[0]));i++)
		if(!strcmp(tag,scene_tags[i].tag)) return 1;
	return 0;
}

static int has_modifier_parser(const char *tag){
	for(int i=0;i<(int)(sizeof(modifier_parsers)/sizeof(modifier_parsers[0]));i++)
		if(!strcmp(tag,modifier_parsers[i].tag)) return 1;
	return 0;
}

static void warn_unsupported_tree(XmlNode *n, const char *path, const char *parent){
	fprintf(stderr,"warning: %s: unsupported XML element <%s> in <%s>\n",path,n->tag,parent);
	for(int i=0;i<n->nkids;i++) warn_unsupported_tree(n->kids[i],path,n->tag);
}

static void warn_unknown_children(XmlNode *parent, const char *path, int root, int prefab){
	for(int i=0;i<parent->nkids;i++){
		XmlNode *n=parent->kids[i];
		int supported=0;
		if(root) supported=has_shape_parser(n->tag) || !strcmp(n->tag,"bool-negative-box") || !strcmp(n->tag,"bool-negative-arch") || !strcmp(n->tag,"bool-negative-cylinder") ||
			(prefab ? (!strcmp(n->tag,"attach") || !strcmp(n->tag,"shape")) : has_scene_parser(n->tag) || !strcmp(n->tag,"pose"));
		else if(!strcmp(parent->tag,"group"))
			supported=has_shape_parser(n->tag) || !strcmp(n->tag,"bool-negative-box") || !strcmp(n->tag,"bool-negative-arch") || !strcmp(n->tag,"bool-negative-cylinder") || !strcmp(n->tag,"shape");
		else if(!strcmp(parent->tag,"camera")) supported=!strcmp(n->tag,"transform") || !strcmp(n->tag,"use-pose");
		else if(!strcmp(parent->tag,"pose")) supported=!strcmp(n->tag,"joint") || !strcmp(n->tag,"ik");
		else if(!strcmp(parent->tag,"wall")||!strcmp(parent->tag,"floor")||!strcmp(parent->tag,"window")||!strcmp(parent->tag,"door")) supported=0;
		else if(!strcmp(parent->tag,"prefab")) supported=!strcmp(n->tag,"array") || !strcmp(n->tag,"joint") || !strcmp(n->tag,"ik");
		else if(has_shape_parser(parent->tag)) supported=has_modifier_parser(n->tag);
		if(!supported){
			warn_unsupported_tree(n,path,parent->tag);
			continue;
		}
		warn_unknown_children(n,path,0,prefab);
	}
}

static void warn_unknown_elements(XmlNode *root, const char *path, int prefab){
	const char *expected=prefab?"prefab":"scene";
	if(strcmp(root->tag,expected)){
		warn_unsupported_tree(root,path,"document");
		return;
	}
	warn_unknown_children(root,path,1,prefab);
}

/* --------------------------------------------------------------- IO & load */

static int path_has_prefix(const char *path, const char *prefix) {
	if (!path || !prefix) return 0;
	return strncmp(path, prefix, strlen(prefix)) == 0;
}

static bool path_file_exists(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

static char* read_file(const char*path){
	char candidate[4096];
	const char *tries[3] = { path, NULL, NULL };
	const char *exe = ui_get_exe_dir();
	if (path && path[0] && exe && exe[0]) {
		char root[4096];
		if (snprintf(root, sizeof(root), "%s/../../", exe) > 0) {
			if (path_has_prefix(path, "apps/scener/")) {
				if (snprintf(candidate, sizeof(candidate), "%s%s", root, path) > 0 && path_file_exists(candidate))
					tries[1] = candidate;
			} else if (path_has_prefix(path, "scenes/") || path_has_prefix(path, "prefabs/")) {
				if (snprintf(candidate, sizeof(candidate), "%sapps/scener/%s", root, path) > 0 && path_file_exists(candidate))
					tries[1] = candidate;
			}
		}
	}
	FILE *f = NULL;
	for (int i = 0; i < 3 && !f; i++) {
		if (!tries[i] || !tries[i][0]) continue;
		f = fopen(tries[i], "rb");
	}
	if (!f) {
		fprintf(stderr,"cannot open %s\n",path);
		return NULL;
	}
	fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
	char *buf=malloc((size_t)n+1);
	size_t rd=fread(buf,1,(size_t)n,f); buf[rd]=0; fclose(f);
	return buf;
}

static void scene_clear_view(Scene *s){
	for(int i=0;i<s->nobjs;i++){
		mesh_free(&s->objs[i].mesh);
		for(int j=0;j<s->objs[i].nshadowParts;j++) free(s->objs[i].shadowParts[j].verts);
		free(s->objs[i].shadowParts);
	}
	for(int i=0;i<s->nlights;i++) if(s->svols) free(s->svols[i].verts);
	for(int i=0;i<s->nshapes;i++) shape2d_free(&s->shapes[i]);
	for(int i=0;i<s->nnegativeProfiles;i++) shape2d_free(&s->negativeProfiles[i].profile);
	free(s->negativeProfiles);
	for(int i=0;i<s->ncameras;i++) free(s->cameras[i].transforms);
	free(s->lights); free(s->mats); free(s->objs); free(s->svols); free(s->cameras);
	free(s->instances); free(s->rigRotations); free(s->rigTargets); free(s->rigJointWorlds); free(s->negativeBoxes); free(s->negativeArches);
	free(s->negativeCylinders); free(s->overlayLines); free(s->charDefs); free(s->shapes);
	s->lights=NULL; s->mats=NULL; s->objs=NULL; s->svols=NULL; s->cameras=NULL;
	s->negativeProfiles=NULL; s->nnegativeProfiles=s->cnegativeProfiles=0;
	s->rigRotations=NULL; s->nrigRotations=s->crigRotations=0;
	s->rigTargets=NULL; s->nrigTargets=s->crigTargets=0;
	s->rigJointWorlds=NULL; s->nrigJointWorlds=s->crigJointWorlds=0;
	s->instances=NULL; s->negativeBoxes=NULL; s->negativeArches=NULL;
	s->negativeCylinders=NULL; s->overlayLines=NULL; s->charDefs=NULL; s->shapes=NULL;
	s->nlights=s->clights=s->nmats=s->cmats=s->nobjs=s->cobjs=0;
	s->ncameras=s->ccameras=s->ninstances=s->cinstances=0;
	s->nnegativeBoxes=s->cnegativeBoxes=s->nnegativeArches=s->cnegativeArches=0;
	s->nnegativeCylinders=s->cnegativeCylinders=s->noverlayLines=s->coverlayLines=0;
	s->ncharDefs=s->ccharDefs=s->nshapes=s->cshapes=0;
	free(s->dragStartVerts); free(s->dragObjIndices); free(s->dragVertOffsets);
	s->dragStartVerts=NULL; s->dragObjIndices=NULL; s->dragVertOffsets=NULL;
	s->ndragStartObjs=s->ndragStartVerts=0;
}

static void scene_rebuild_view(Scene *s){
	XmlNode *root=(XmlNode*)s->editRoot;
	s->assetError=0;
	XmlNode *sceneRoot=(XmlNode*)s->sceneRoot;
	int prefabMode=s->prefabDocument||s->editDepth;
	void *selected=s->selectedNode;
	char requestedCamera[MAX_CAMERA_NAME]; snprintf(requestedCamera,sizeof(requestedCamera),"%s",s->activeCamera);
	scene_clear_view(s);
	s->camPos=v3(0,1.6f,5); s->camLook=v3(0,1.2f,0); s->camFov=60;
	s->convention3dsMax=!strcmp(xml_attr(sceneRoot,"convention",""),"3dsmax");
	s->worldUp=!s->convention3dsMax&&!strcmp(xml_attr(sceneRoot,"up","y"),"z")?v3(0,0,1):v3(0,1,0);
	s->ambient=v3(0.12f,0.12f,0.14f); s->bg=v3(0.08f,0.10f,0.14f);
	if(prefabMode){ s->ambient=v3(0.48f,0.50f,0.56f); s->bg=v3(0.14f,0.16f,0.20f); }
	else {
		s->ambient=xml_attr_v3(root,"ambient",s->ambient);
		const char *bg=xml_attr(root,"background",NULL);
		if(bg){
			if(strchr(bg,' ')) sscanf(bg,"%f %f %f",&s->bg.x,&s->bg.y,&s->bg.z);
			else for(int i=0;i<npreset_bgs;i++) if(!strcmp(preset_bgs[i].id,bg)){ s->bg=preset_bgs[i].color; break; }
		}
	}
	mat4 I=mat4_identity();
	collect_shapes_from_tree(s,root);
	if(s->editDepth){
		for(int i=0;i<sceneRoot->nkids;i++) if(!strcmp(sceneRoot->kids[i]->tag,"material"))
			parse_material_tag(s,sceneRoot->kids[i]);
	} else if(!s->prefabDocument) for(int i=0;i<root->nkids;i++) for(int j=0;j<(int)(sizeof(scene_tags)/sizeof(scene_tags[0]));j++)
		if(!strcmp(root->kids[i]->tag,scene_tags[j].tag)){ scene_tags[j].parse(s,root->kids[i]); break; }
	if(requestedCamera[0]) for(int i=0;i<s->ncameras;i++) if(!strcmp(s->cameras[i].name,requestedCamera)){
		s->camPos=s->cameras[i].pos; s->camLook=s->cameras[i].look; s->camFov=s->cameras[i].fov;
		snprintf(s->activeCamera,sizeof(s->activeCamera),"%s",requestedCamera);
		break;
	}
	collect_negative_boxes(s,root,I);
	s->activeEditNode=NULL;
	parse_nodes(s,root,I,I);
	if(prefabMode && s->nlights==0){
		vec3 bmin={1e30f,1e30f,1e30f},bmax={-1e30f,-1e30f,-1e30f};
		for(int i=0;i<s->nobjs;i++) if(s->objs[i].renderable){
			for(int v=0;v<s->objs[i].mesh.nverts;v++){
				vec3 p=s->objs[i].mesh.verts[v].pos;
				if(p.x<bmin.x)bmin.x=p.x; if(p.y<bmin.y)bmin.y=p.y; if(p.z<bmin.z)bmin.z=p.z;
				if(p.x>bmax.x)bmax.x=p.x; if(p.y>bmax.y)bmax.y=p.y; if(p.z>bmax.z)bmax.z=p.z;
			}
		}
		if(bmin.x<=bmax.x){
			vec3 c=vscale(vadd(bmin,bmax),0.5f); float sz=vlen(vsub(bmax,bmin));
			if(sz<1.0f) sz=1.0f;
			Light key={v3(0,0,0),v3(1.0f,0.88f,0.76f),{0},2.8f,sz*2.5f,1,0};
			key.pos=vadd(c,v3(sz*0.9f,sz*0.8f,sz*1.1f));
			DA_PUSH(s->lights,s->nlights,s->clights,key);
			Light rim={v3(0,0,0),v3(0.68f,0.80f,1.0f),{0},1.6f,sz*2.8f,0,0};
			rim.pos=vadd(c,v3(-sz*0.7f,sz*0.9f,-sz*1.0f));
			DA_PUSH(s->lights,s->nlights,s->clights,rim);
		}
	}
	if(!s->ncameras){
		Camera def={0}; snprintf(def.name,sizeof(def.name),"%s","Camera1");
		def.pos=s->camPos; def.look=s->camLook; def.fov=s->camFov;
		DA_PUSH(s->cameras,s->ncameras,s->ccameras,def);
	}
	s->svols=calloc((size_t)s->nlights,sizeof(ShadowVolume));
	if(!prefabMode){
		for(int li=0;li<s->nlights;li++) if(!s->lights[li].isDirectional)
			add_lamp_dummy(s,s->lights[li].pos,0.15f,v3(1.0f,0.7f,0.2f),1,NULL);
		for(int ci=0;ci<s->ncameras;ci++){
			Camera *c=&s->cameras[ci];
			add_camera_dummy(s,c->pos,c->look,c->fov,1.0f,v3(0.2f,0.8f,0.2f),2,NULL);
		}
	}
	s->selectedNode=selected;
	s->selectedObj=-1;
	for(int i=0;i<s->nobjs;i++) if(s->objs[i].editNode==selected){ s->selectedObj=i; break; }
	scene_build_all_shadow_volumes(s);
}

static int scene_insert_source_node(Scene *s,XmlNode *node){
	vec3 up=s->worldUp.z==1?v3(0,0,1):v3(0,1,0);
	if(!s->sceneRoot){
		XmlNode *root=xml_new("scene"); s->sceneRoot=s->editRoot=root;
		xml_set_attr(root,"up",up.z==1?"z":"y");
		xml_set_attr_v3(root,"ambient",s->ambient); xml_set_attr_v3(root,"background",s->bg);
		XmlNode *camera=xml_new("camera"); xml_set_attr(camera,"name","Camera1");
		xml_set_attr_v3_cm(camera,"pos",s->camPos); xml_set_attr_v3_cm(camera,"look",s->camLook);
		DA_PUSH(root->kids,root->nkids,root->ckids,camera);
		for(int i=0;i<s->nlights;i++){
			Light *light=&s->lights[i]; XmlNode *n=xml_new(light->isDirectional?"sun":"light");
			xml_set_attr_v3(n,"color",light->color);
			if(light->isDirectional) xml_set_attr_v3(n,"dir",light->dir);
			else xml_set_attr_v3_cm(n,"pos",light->pos);
			char value[WINDOW_VALUE_CAPACITY]; snprintf(value,sizeof(value),"%g",light->intensity);
			xml_set_attr(n,"intensity",value); xml_set_attr(n,"castShadows",light->castsShadow?"1":"0");
			DA_PUSH(root->kids,root->nkids,root->ckids,n);
		}
	}
	/* Legacy Create tools leave mesh-only objects; keep them during a source rebuild. */
	SceneObj *loose=NULL; int nloose=0,cloose=0;
	for(int i=0;i<s->nobjs;i++) if(!s->objs[i].editNode){
		DA_PUSH(loose,nloose,cloose,s->objs[i]); memset(&s->objs[i],0,sizeof(s->objs[i]));
	}
	XmlNode *root=(XmlNode*)s->editRoot; DA_PUSH(root->kids,root->nkids,root->ckids,node);
	s->selectedNode=node; scene_rebuild_view(s);
	for(int i=0;i<nloose;i++) DA_PUSH(s->objs,s->nobjs,s->cobjs,loose[i]);
	free(loose);
	if(nloose) scene_build_all_shadow_volumes(s);
	return 1;
}

static int scene_create_insert(Scene *s,const char *tag,const char *preset,vec3 ground){
	XmlNode *node=xml_new(tag); xml_set_attr(node,"preset",preset);
	window_spec_t w;
	if(!window_spec(node,&w)){ xml_free(node); return 0; }
	vec3 up=s->worldUp.z==1?v3(0,0,1):v3(0,1,0);
	float lift=w.height/2+(w.sill?w.sillHeight-WINDOW_JOIN_OVERLAP:0);
	xml_set_attr_v3_cm(node,"pos",vadd(ground,vscale(up,lift)));
	if(up.z==1) xml_set_attr(node,"rot",WINDOW_Z_UP_ROTATION);
	window_spec_free(&w);
	if(!scene_insert_source_node(s,node)) return 0;
	fprintf(stderr,"[scener] create %s preset=%s at=(%g,%g,%g)\n",tag,preset,ground.x,ground.y,ground.z);
	return 1;
}

int scene_create_promo_shape(Scene *s,const char *tag,vec3 ground){
	int screen=!strcmp(tag,"screen");
	const struct { const char *tag,*size,*radius,*outer,*inner,*points,*depth,*bevel; float lift; } presets[]={
		{ "rect",         "100 100", NULL, NULL, NULL, NULL, "20", "2", 0.5f },
		{ "rounded-rect", "7.6 16.2", "1.28", NULL, NULL, NULL, "0.8", "0.16", 0.081f },
		{ "circle",       NULL, "50", NULL, NULL, NULL, "20", "2", 0.5f },
		{ "ellipse",      "100 70", NULL, NULL, NULL, NULL, "20", "2", 0.35f },
		{ "star",         NULL, NULL, "50", "25", "5", "20", "2", 0.5f },
	};
	const int npresets=(int)(sizeof(presets)/sizeof(presets[0]));
	int preset=-1;
	for(int i=0;i<npresets;i++) if(!strcmp(tag,presets[i].tag)){ preset=i; break; }
	if(!screen&&preset<0) return 0;
	XmlNode *node=xml_new(tag);
	vec3 up=s->worldUp.z==1?v3(0,0,1):v3(0,1,0);
	float lift=screen?0.0777f:presets[preset].lift;
	if(preset>=0){
		const char *size=presets[preset].size,*radius=presets[preset].radius;
		if(size) xml_set_attr(node,"size",size);
		if(radius) xml_set_attr(node,"radius",radius);
		if(presets[preset].outer) xml_set_attr(node,"outerRadius",presets[preset].outer);
		if(presets[preset].inner) xml_set_attr(node,"innerRadius",presets[preset].inner);
		if(presets[preset].points) xml_set_attr(node,"points",presets[preset].points);
		XmlNode *extrude=xml_new("extrude"); extrude->parent=node;
		xml_set_attr(extrude,"amount",presets[preset].depth);
		DA_PUSH(node->kids,node->nkids,node->ckids,extrude);
		XmlNode *bevel=xml_new("bevel"); bevel->parent=node;
		xml_set_attr(bevel,"amount",presets[preset].bevel); xml_set_attr(bevel,"bevelSegments","4");
		DA_PUSH(node->kids,node->nkids,node->ckids,bevel);
	} else {
		xml_set_attr(node,"size","7.03 15.54 0.03");
		xml_set_attr(node,"radius","0.98");
	}
	xml_set_attr_v3_cm(node,"pos",vadd(ground,vscale(up,lift)));
	if(up.z==1) xml_set_attr(node,"rot",WINDOW_Z_UP_ROTATION);
	if(screen){ xml_set_attr(node,"color","1 1 1"); xml_set_attr(node,"unlit","1"); xml_set_attr(node,"castShadow","0"); }
	if(!scene_insert_source_node(s,node)) return 0;
	fprintf(stderr,"[scener] create %s at=(%g,%g,%g)\n",tag,ground.x,ground.y,ground.z);
	return 1;
}

int scene_create_window(Scene *s,const char *preset,vec3 ground){ return scene_create_insert(s,"window",preset,ground); }
int scene_create_door(Scene *s,const char *preset,vec3 ground){ return scene_create_insert(s,"door",preset,ground); }

int load_scene(const char *path, Scene *s){
	memset(s,0,sizeof(*s));
	s->selectedObj=-1; s->editMode=EDIT_W_MOVE;
	s->activeTexIndex=-1;
	s->activeScreenTexture=-1;
	strncpy(s->scenePath,path,sizeof(s->scenePath)-1);
	const char *content=strstr(path,"/scenes/");
	if(!content) content=strstr(path,"\\scenes\\");
	if(!content) content=strstr(path,"/prefabs/");
	if(!content) content=strstr(path,"\\prefabs\\");
	if(content){
		size_t n=(size_t)(content-path);
		if(n>=sizeof(s->assetRoot)) n=sizeof(s->assetRoot)-1;
		memcpy(s->assetRoot,path,n); s->assetRoot[n]=0;
	} else if(!strncmp(path,"scenes/",7)||!strncmp(path,"scenes\\",7)||
		!strncmp(path,"prefabs/",8)||!strncmp(path,"prefabs\\",8)) strcpy(s->assetRoot,".");
	if(!s->assetRoot[0]){
		const char *slash=strrchr(path,'/');
		if(slash){ size_t n=(size_t)(slash-path); if(n>=sizeof(s->assetRoot)) n=sizeof(s->assetRoot)-1; memcpy(s->assetRoot,path,n); s->assetRoot[n]=0; }
	}
	char *buf=read_file(path); if(!buf) return 0;
	XmlNode *root=xml_parse(buf); free(buf);
	if(!root){ fprintf(stderr,"failed to parse %s\n",path); return 0; }
	const char *up=xml_attr(root,"up","y");
	if(strcmp(up,"y") && strcmp(up,"z")){ fprintf(stderr,"invalid scene up axis: %s\n",up); xml_free(root); return 0; }
	s->prefabDocument=!strcmp(root->tag,"prefab");
	warn_unknown_elements(root,path,s->prefabDocument);
	s->sceneRoot=root; s->editRoot=root;
	scene_rebuild_view(s);
	if(s->assetError){ scene_free(s); return 0; }
	return 1;
}

void scene_select_camera(Scene *s, const char *name){
	for(int i=0;i<s->ncameras;i++){
		if(!strcmp(s->cameras[i].name,name)){
			char selected[MAX_CAMERA_NAME]; snprintf(selected,sizeof(selected),"%s",s->cameras[i].name);
			if(!strcmp(s->activeCamera,selected)){
				s->camPos=s->cameras[i].pos; s->camLook=s->cameras[i].look; s->camFov=s->cameras[i].fov;
				return;
			}
			snprintf(s->activeCamera,sizeof(s->activeCamera),"%s",selected);
			scene_rebuild_view(s);
			return;
		}
	}
}

static XmlNode *rig_instance_root(Scene *s,XmlNode *instance){
	if(!instance || strcmp(instance->tag,"prefab")) return NULL;
	const char *source=xml_attr(instance,"source",NULL);
	return source?load_prefab(s,source):NULL;
}

static int rig_joint_count_tree(XmlNode *node){
	int count=!strcmp(node->tag,"group") && xml_attr(node,"name",NULL)?1:0;
	for(int i=0;i<node->nkids;i++) count+=rig_joint_count_tree(node->kids[i]);
	return count;
}

int scene_rig_joint_count(Scene *s,void *instance){
	XmlNode *root=rig_instance_root(s,(XmlNode*)instance);
	return root?rig_joint_count_tree(root):0;
}

static XmlNode *rig_joint_at_tree(XmlNode *node,int *index,int level,int *depth){
	int isJoint=!strcmp(node->tag,"group") && xml_attr(node,"name",NULL);
	if(isJoint){
		if(*index==0){ if(depth) *depth=level; return node; }
		(*index)--;
	}
	for(int i=0;i<node->nkids;i++){
		XmlNode *found=rig_joint_at_tree(node->kids[i],index,level+isJoint,depth);
		if(found) return found;
	}
	return NULL;
}

void *scene_rig_joint_at(Scene *s,void *instance,int index,int *depth){
	XmlNode *root=rig_instance_root(s,(XmlNode*)instance);
	return root && index>=0?rig_joint_at_tree(root,&index,0,depth):NULL;
}

int scene_rig_select_joint(Scene *s,void *instance,void *joint){
	XmlNode *in=(XmlNode*)instance,*j=(XmlNode*)joint,*root=rig_instance_root(s,in);
	if(!root || !j || rig_find_joint(root,xml_attr(j,"name",""))!=j) return 0;
	s->selectedRigInstance=in; s->selectedRigJoint=j; s->selectedNode=in;
	s->selectedObj=-1;
	for(int i=0;i<s->nobjs;i++) if(s->objs[i].editNode==in){ s->selectedObj=i; break; }
	fprintf(stderr,"[scener] select rig instance=%s joint=%s\n",xml_attr(in,"name",""),xml_attr(j,"name","")); fflush(stderr);
	return 1;
}

int scene_rig_joint_world(Scene *s,mat4 *matrix){
	if(!s || !matrix || !s->selectedRigJoint) return 0;
	for(int i=0;i<s->nrigJointWorlds;i++) if(s->rigJointWorlds[i].instance==s->selectedRigInstance &&
		s->rigJointWorlds[i].joint==s->selectedRigJoint){ *matrix=s->rigJointWorlds[i].matrix; return 1; }
	return 0;
}

static mat4 rig_rest_world(Scene *s,XmlNode *node,XmlNode *root){
	if(!node || node==root) return mat4_identity();
	return mat4_mul(rig_rest_world(s,node->parent,root),xml_node_transform(s,node));
}

int scene_rig_reparent_joint(Scene *s,void *instance,void *joint,const char *parentName){
	XmlNode *in=(XmlNode*)instance,*j=(XmlNode*)joint,*root=rig_instance_root(s,in);
	XmlNode *parent=rig_find_joint(root,parentName);
	if(!root || !j || !parent || rig_find_joint(root,xml_attr(j,"name",""))!=j || j==parent || !j->parent) return 0;
	for(XmlNode *walk=parent;walk;walk=walk->parent) if(walk==j) return 0;
	int scaled=0;
	for(XmlNode *walk=j;walk && walk!=root;walk=walk->parent) scaled|=xml_attr(walk,"scale",NULL)!=NULL;
	for(XmlNode *walk=parent;walk && walk!=root;walk=walk->parent) scaled|=xml_attr(walk,"scale",NULL)!=NULL;
	if(xml_attr(j,"pivotOffset",NULL) || scaled){
		fprintf(stderr,"[scener] reparent requires unscaled joints without pivotOffset\n"); fflush(stderr); return 0;
	}
	mat4 oldWorld=rig_rest_world(s,j,root),newParent=rig_rest_world(s,parent,root);
	mat4 local=mat4_mul(mat4_affine_inverse(newParent),oldWorld);
	vec3 pos=cvt3ds_inv(s,v3(local.m[12],local.m[13],local.m[14]));
	vec3 rot=cvt3ds_inv(s,v3(atan2f(local.m[6],local.m[10])*180/M_PIf,
		asinf(fmaxf(-1,fminf(1,-local.m[2])))*180/M_PIf,atan2f(local.m[1],local.m[0])*180/M_PIf));
	XmlNode *oldParent=j->parent;
	for(int i=0;i<oldParent->nkids;i++) if(oldParent->kids[i]==j){
		memmove(oldParent->kids+i,oldParent->kids+i+1,(size_t)(oldParent->nkids-i-1)*sizeof(*oldParent->kids));
		oldParent->nkids--; break;
	}
	j->parent=parent; DA_PUSH(parent->kids,parent->nkids,parent->ckids,j);
	xml_set_attr_v3_cm(j,"pos",pos); xml_set_attr_v3(j,"rot",rot);
	fprintf(stderr,"[scener] reparent rig joint=%s parent=%s\n",xml_attr(j,"name",""),parentName); fflush(stderr);
	scene_rebuild_view(s);
	return 1;
}

static XmlNode *rig_ensure_instance_joint(XmlNode *instance,const char *name){
	XmlNode *override=rig_override_in(instance,name);
	if(override) return override;
	override=xml_new("joint"); override->parent=instance; xml_set_attr(override,"target",name);
	DA_PUSH(instance->kids,instance->nkids,instance->ckids,override);
	return override;
}

int scene_rig_set_joint(Scene *s,void *instance,void *joint,const char *attribute,vec3 value){
	XmlNode *in=(XmlNode*)instance,*j=(XmlNode*)joint,*root=rig_instance_root(s,in);
	if(!root || !j || rig_find_joint(root,xml_attr(j,"name",""))!=j || !attribute) return 0;
	if(!strcmp(attribute,"pivot")) xml_set_attr_v3_cm(j,"pos",value);
	else if(!strcmp(attribute,"rot") || !strcmp(attribute,"pos")){
		XmlNode *override=rig_ensure_instance_joint(in,xml_attr(j,"name",""));
		if(!strcmp(attribute,"pos")) xml_set_attr_v3_cm(override,"pos",value);
		else xml_set_attr_v3(override,"rot",value);
	} else return 0;
	fprintf(stderr,"[scener] edit rig instance=%s joint=%s attribute=%s value=%g,%g,%g\n",
		xml_attr(in,"name",""),xml_attr(j,"name",""),attribute,value.x,value.y,value.z); fflush(stderr);
	scene_rebuild_view(s);
	return 1;
}

vec3 scene_rig_joint_value(Scene *s,void *instance,void *joint,const char *attribute){
	XmlNode *in=(XmlNode*)instance,*j=(XmlNode*)joint;
	if(!in || !j || !attribute) return v3(0,0,0);
	if(!strcmp(attribute,"pivot")) return xml_attr_v3_cm(j,"pos",v3(0,0,0));
	XmlNode *override=rig_override_in(in,xml_attr(j,"name",""));
	if(!override) override=rig_override_in(rig_pose_for_instance(s,in),xml_attr(j,"name",""));
	if(!override) return v3(0,0,0);
	return !strcmp(attribute,"pos")?xml_attr_v3_cm(override,"pos",v3(0,0,0)):
		xml_attr_v3(override,"rot",v3(0,0,0));
}

int scene_rig_save_pose(Scene *s,void *instance,const char *name){
	XmlNode *in=(XmlNode*)instance,*root=(XmlNode*)s->sceneRoot;
	if(!rig_instance_root(s,in) || !root || !name || !*name) return 0;
	XmlNode *staging=xml_new("pose"),*source=rig_pose_for_instance(s,in);
	for(int layer=0;layer<2;layer++){
		XmlNode *container=layer?in:source;
		if(!container) continue;
		for(int i=0;i<container->nkids;i++) if(!strcmp(container->kids[i]->tag,"joint") || !strcmp(container->kids[i]->tag,"ik")){
			XmlNode *src=container->kids[i],*copy=xml_new(src->tag); copy->parent=staging;
			for(int a=0;a<src->nattrs;a++) xml_set_attr(copy,src->attrs[a].name,src->attrs[a].value);
			for(int j=0;j<staging->nkids;j++) if(!strcmp(staging->kids[j]->tag,copy->tag) &&
				!strcmp(xml_attr(staging->kids[j],!strcmp(copy->tag,"ik")?"tip":"target",""),
					xml_attr(copy,!strcmp(copy->tag,"ik")?"tip":"target",""))){
				xml_free(staging->kids[j]); staging->kids[j]=copy; copy=NULL; break;
			}
			if(copy) DA_PUSH(staging->kids,staging->nkids,staging->ckids,copy);
		}
	}
	XmlNode *pose=NULL;
	for(int i=0;i<root->nkids;i++) if(!strcmp(root->kids[i]->tag,"pose") &&
		!strcmp(xml_attr(root->kids[i],"name",""),name)){ pose=root->kids[i]; break; }
	if(!pose){ pose=xml_new("pose"); pose->parent=root; DA_PUSH(root->kids,root->nkids,root->ckids,pose); }
	for(int i=0;i<pose->nkids;i++) xml_free(pose->kids[i]);
	pose->nkids=0; xml_set_attr(pose,"name",name);
	for(int i=0;i<staging->nkids;i++){ staging->kids[i]->parent=pose; DA_PUSH(pose->kids,pose->nkids,pose->ckids,staging->kids[i]); }
	staging->nkids=0; xml_free(staging);
	fprintf(stderr,"[scener] save pose name=%s instance=%s joints=%d\n",name,xml_attr(in,"name",""),pose->nkids); fflush(stderr);
	return 1;
}

int scene_rig_assign_pose(Scene *s,void *instance,const char *name,int cameraOnly){
	XmlNode *in=(XmlNode*)instance,*root=(XmlNode*)s->sceneRoot,*pose=NULL;
	if(!rig_instance_root(s,in) || !root || !name) return 0;
	for(int i=0;i<root->nkids;i++) if(!strcmp(root->kids[i]->tag,"pose") &&
		!strcmp(xml_attr(root->kids[i],"name",""),name)) pose=root->kids[i];
	if(!pose) return 0;
	if(cameraOnly){
		XmlNode *camera=NULL;
		for(int i=0;i<root->nkids;i++) if(!strcmp(root->kids[i]->tag,"camera") &&
			!strcmp(xml_attr(root->kids[i],"name",""),s->activeCamera)) camera=root->kids[i];
		if(!camera || !xml_attr(in,"name",NULL)) return 0;
		XmlNode *use=NULL;
		for(int i=0;i<camera->nkids;i++) if(!strcmp(camera->kids[i]->tag,"use-pose") &&
			!strcmp(xml_attr(camera->kids[i],"instance",""),xml_attr(in,"name",""))) use=camera->kids[i];
		if(!use){ use=xml_new("use-pose"); use->parent=camera; DA_PUSH(camera->kids,camera->nkids,camera->ckids,use); }
		xml_set_attr(use,"instance",xml_attr(in,"name","")); xml_set_attr(use,"name",name);
	} else xml_set_attr(in,"pose",name);
	fprintf(stderr,"[scener] assign pose name=%s instance=%s camera=%s\n",name,xml_attr(in,"name",""),cameraOnly?s->activeCamera:"all"); fflush(stderr);
	scene_rebuild_view(s);
	return 1;
}

static XmlNode *rig_ik_for_tip(XmlNode *container,const char *tip){
	if(!container) return NULL;
	for(int i=0;i<container->nkids;i++) if(!strcmp(container->kids[i]->tag,"ik") &&
		!strcmp(xml_attr(container->kids[i],"tip",""),tip)) return container->kids[i];
	return NULL;
}

vec3 scene_rig_target_value(Scene *s,void *instance,void *tip,const char *attribute){
	XmlNode *in=(XmlNode*)instance,*j=(XmlNode*)tip;
	if(!in || !j || !attribute) return v3(0,0,0);
	XmlNode *ik=rig_ik_for_tip(in,xml_attr(j,"name",""));
	if(!ik) ik=rig_ik_for_tip(rig_pose_for_instance(s,in),xml_attr(j,"name",""));
	if(!strcmp(attribute,"pole")) return ik?xml_attr_v3(ik,"pole",v3(0,-1,0)):v3(0,-1,0);
	if(ik) return xml_attr_v3_cm(ik,"target",v3(0,0,0));
	for(int i=0;i<s->ninstances;i++) if(!strcmp(s->instances[i].name,xml_attr(in,"name",""))){
		void *oldInstance=s->activeRigInstance,*oldPose=s->activeRigPose,*oldRoot=s->activeRigRoot;
		s->activeRigInstance=in; s->activeRigPose=NULL; s->activeRigRoot=rig_instance_root(s,in);
		vec3 point=mat4_xform_point(rig_world(s,j,(XmlNode*)s->activeRigRoot,s->instances[i].transform),v3(0,0,0));
		s->activeRigInstance=oldInstance; s->activeRigPose=oldPose; s->activeRigRoot=oldRoot;
		return point;
	}
	return v3(0,0,0);
}

int scene_rig_set_target(Scene *s,void *instance,void *tip,const char *attribute,vec3 value){
	XmlNode *in=(XmlNode*)instance,*j=(XmlNode*)tip,*root=rig_instance_root(s,in);
	if(!root || !j || !j->parent || !j->parent->parent ||
		strcmp(j->tag,"group") || strcmp(j->parent->tag,"group") || strcmp(j->parent->parent->tag,"group") ||
		(strcmp(attribute,"target") && strcmp(attribute,"pole"))) return 0;
	if(rig_find_joint(root,xml_attr(j,"name",""))!=j || !xml_attr(j->parent,"name",NULL) || !xml_attr(j->parent->parent,"name",NULL)) return 0;
	XmlNode *ik=rig_ik_for_tip(in,xml_attr(j,"name",""));
	if(!ik){
		ik=xml_new("ik"); ik->parent=in;
		xml_set_attr(ik,"root",xml_attr(j->parent->parent,"name",""));
		xml_set_attr(ik,"mid",xml_attr(j->parent,"name",""));
		xml_set_attr(ik,"tip",xml_attr(j,"name",""));
		xml_set_attr(ik,"keepOrientation","1");
		xml_set_attr_v3_cm(ik,"target",scene_rig_target_value(s,in,j,"target"));
		xml_set_attr_v3(ik,"pole",scene_rig_target_value(s,in,j,"pole"));
		DA_PUSH(in->kids,in->nkids,in->ckids,ik);
	}
	if(!strcmp(attribute,"target")) xml_set_attr_v3_cm(ik,"target",value);
	else xml_set_attr_v3(ik,"pole",value);
	fprintf(stderr,"[scener] edit IK instance=%s tip=%s %s=%g,%g,%g\n",xml_attr(in,"name",""),xml_attr(j,"name",""),attribute,value.x,value.y,value.z); fflush(stderr);
	scene_rebuild_view(s);
	return 1;
}

int scene_rig_mirror_joint(Scene *s,void *instance,void *joint){
	XmlNode *in=(XmlNode*)instance,*j=(XmlNode*)joint,*root=rig_instance_root(s,in);
	if(!root || !j) return 0;
	XmlNode *pair=rig_find_joint(root,xml_attr(j,"pair",""));
	if(!pair || pair==j) return 0;
	const char *name=xml_attr(j,"name","");
	XmlNode *src=rig_override_in(in,name);
	if(!src) src=rig_override_in(rig_pose_for_instance(s,in),name);
	vec3 rot=src?xml_attr_v3(src,"rot",v3(0,0,0)):v3(0,0,0);
	vec3 pos=src?xml_attr_v3_cm(src,"pos",v3(0,0,0)):v3(0,0,0);
	vec3 result=v3(rot.x,-rot.y,-rot.z);
	XmlNode *dest=rig_ensure_instance_joint(in,xml_attr(pair,"name",""));
	xml_set_attr_v3(dest,"rot",result); xml_set_attr_v3_cm(dest,"pos",v3(-pos.x,pos.y,pos.z));
	fprintf(stderr,"[scener] mirror rig instance=%s from=%s to=%s\n",xml_attr(in,"name",""),name,xml_attr(pair,"name","")); fflush(stderr);
	scene_rebuild_view(s);
	return 1;
}

typedef struct { vec3 min,max; } Bounds;

static Bounds scene_obj_bounds(SceneObj *o){
	Bounds b={v3(INFINITY,INFINITY,INFINITY),v3(-INFINITY,-INFINITY,-INFINITY)};
	for(int i=0;i<o->mesh.nverts;i++){
		vec3 p=o->mesh.verts[i].pos;
		if(p.x<b.min.x) b.min.x=p.x; if(p.x>b.max.x) b.max.x=p.x;
		if(p.y<b.min.y) b.min.y=p.y; if(p.y>b.max.y) b.max.y=p.y;
		if(p.z<b.min.z) b.min.z=p.z; if(p.z>b.max.z) b.max.z=p.z;
	}
	return b;
}

static float bounds_overlap(float amin,float amax,float bmin,float bmax){
	float lo=amin>bmin?amin:bmin, hi=amax<bmax?amax:bmax;
	return hi-lo;
}

int scene_sanity_check(Scene *s){
	Bounds *bounds=calloc((size_t)s->nobjs,sizeof(*bounds));
	int errors=0;
	for(int i=0;i<s->nobjs;i++) bounds[i]=scene_obj_bounds(&s->objs[i]);
	for(int i=0;i<s->nobjs;i++){
		SceneObj *a=&s->objs[i];
		if(a->sanityIgnore || a->sanityFloor || !a->sanityCheck) continue;
		for(int j=i+1;j<s->nobjs;j++){
			SceneObj *b=&s->objs[j];
			if(b->sanityIgnore || b->sanityFloor || !b->sanityCheck) continue;
			float x=bounds_overlap(bounds[i].min.x,bounds[i].max.x,bounds[j].min.x,bounds[j].max.x);
			float y=bounds_overlap(bounds[i].min.y,bounds[i].max.y,bounds[j].min.y,bounds[j].max.y);
			float z=bounds_overlap(bounds[i].min.z,bounds[i].max.z,bounds[j].min.z,bounds[j].max.z);
			if(x>0.025f && y>0.025f && z>0.025f){
				fprintf(stderr,"sanity: intersecting objects %d and %d (%.3f %.3f %.3f)\n",i,j,x,y,z);
				errors++;
			}
		}
		if(bounds[i].min.y>0.015f){
			int supported=0;
			for(int j=0;j<s->nobjs;j++){
				SceneObj *b=&s->objs[j];
				if(i==j || b->sanityIgnore || (!b->sanityCheck && !b->sanityFloor)) continue;
				if(fabsf(bounds[i].min.y-bounds[j].max.y)>0.025f) continue;
				if(bounds_overlap(bounds[i].min.x,bounds[i].max.x,bounds[j].min.x,bounds[j].max.x)>0.025f &&
				   bounds_overlap(bounds[i].min.z,bounds[i].max.z,bounds[j].min.z,bounds[j].max.z)>0.025f){ supported=1; break; }
			}
			if(!supported){ fprintf(stderr,"sanity: floating object %d, base y=%.3f\n",i,bounds[i].min.y); errors++; }
		}
	}
	free(bounds);
	if(!errors) fprintf(stderr,"sanity: ok (%d objects)\n",s->nobjs);
	return !errors;
}

void scene_get_obj_bounds(Scene *s, int idx, vec3 *outMin, vec3 *outMax){
	if(idx<0 || idx>=s->nobjs){ *outMin=v3(0,0,0); *outMax=v3(0,0,0); return; }
	void *node=s->objs[idx].editNode;
	Bounds b={v3(INFINITY,INFINITY,INFINITY),v3(-INFINITY,-INFINITY,-INFINITY)};
	for(int i=0;i<s->nobjs;i++) if((node && s->objs[i].editNode==node) || (!node && i==idx)){
		Bounds p=scene_obj_bounds(&s->objs[i]);
		if(p.min.x<b.min.x) b.min.x=p.min.x; if(p.max.x>b.max.x) b.max.x=p.max.x;
		if(p.min.y<b.min.y) b.min.y=p.min.y; if(p.max.y>b.max.y) b.max.y=p.max.y;
		if(p.min.z<b.min.z) b.min.z=p.min.z; if(p.max.z>b.max.z) b.max.z=p.max.z;
	}
	*outMin=b.min; *outMax=b.max;
}

void scene_get_obj_oriented_bounds(Scene *s,int idx,mat4 *matrix,vec3 *outMin,vec3 *outMax){
	if(idx<0 || idx>=s->nobjs){ *matrix=mat4_identity(); *outMin=*outMax=v3(0,0,0); return; }
	void *node=s->objs[idx].editNode;
	*matrix=s->objs[idx].editMatrix;
	mat4 inv=mat4_affine_inverse(*matrix);
	Bounds b={v3(INFINITY,INFINITY,INFINITY),v3(-INFINITY,-INFINITY,-INFINITY)};
	for(int i=0;i<s->nobjs;i++) if((node && s->objs[i].editNode==node) || (!node && i==idx)){
		for(int j=0;j<s->objs[i].mesh.nverts;j++){
			vec3 p=mat4_xform_point(inv,s->objs[i].mesh.verts[j].pos);
			if(p.x<b.min.x) b.min.x=p.x; if(p.x>b.max.x) b.max.x=p.x;
			if(p.y<b.min.y) b.min.y=p.y; if(p.y>b.max.y) b.max.y=p.y;
			if(p.z<b.min.z) b.min.z=p.z; if(p.z>b.max.z) b.max.z=p.z;
		}
	}
	if(!isfinite(b.min.x)) b.min=b.max=v3(0,0,0);
	*outMin=b.min; *outMax=b.max;
}

static int ray_mesh_hit(const Mesh *mesh,vec3 origin,vec3 dir,float *best){
	int hit=0;
	for(int i=0;i<mesh->ntris;i++){
		Tri t=mesh->tris[i]; vec3 a=mesh->verts[t.a].pos;
		vec3 e=vsub(mesh->verts[t.b].pos,a),f=vsub(mesh->verts[t.c].pos,a),p=vcross(dir,f);
		float det=vdot(e,p); if(fabsf(det)<PICK_TRIANGLE_EPSILON) continue;
		vec3 delta=vsub(origin,a); float u=vdot(delta,p)/det;
		if(u<0||u>1) continue;
		vec3 q=vcross(delta,e); float v=vdot(dir,q)/det;
		if(v<0||u+v>1) continue;
		float distance=vdot(f,q)/det;
		if(distance>=0&&distance<*best){ *best=distance; hit=1; }
	}
	return hit;
}

int scene_pick_object(Scene *s, vec3 rayOrigin, vec3 rayDir, float *tOut){
	int hit=-1; float bestT=1e30f;
	for(int i=0;i<s->nobjs;i++){
		if(!s->objs[i].renderable) continue;
		void *node=s->objs[i].editNode;
		int seen=0;
		for(int j=0;j<i;j++) if(node && s->objs[j].editNode==node){ seen=1; break; }
		if(seen) continue;
		mat4 matrix; vec3 bmin,bmax;
		scene_get_obj_oriented_bounds(s,i,&matrix,&bmin,&bmax);
		mat4 inv=mat4_affine_inverse(matrix);
		float t;
		if(ray_intersect_aabb(mat4_xform_point(inv,rayOrigin),mat4_xform_dir(inv,rayDir),bmin,bmax,&t)){
			for(int j=i;j<s->nobjs;j++){
				if(!s->objs[j].renderable || (node?s->objs[j].editNode!=node:j!=i)) continue;
				if(ray_mesh_hit(&s->objs[j].mesh,rayOrigin,rayDir,&bestT)) hit=i;
			}
		}
	}
	s->selectedNode=hit>=0?s->objs[hit].editNode:NULL;
	s->selectedRigInstance=NULL; s->selectedRigJoint=NULL;
	if(tOut) *tOut=bestT;
	return hit;
}

void scene_get_bounds(Scene *s,vec3 *outMin,vec3 *outMax){
	Bounds b={v3(INFINITY,INFINITY,INFINITY),v3(-INFINITY,-INFINITY,-INFINITY)};
	for(int i=0;i<s->nobjs;i++) if(s->objs[i].renderable){
		Bounds p=scene_obj_bounds(&s->objs[i]);
		if(p.min.x<b.min.x) b.min.x=p.min.x; if(p.max.x>b.max.x) b.max.x=p.max.x;
		if(p.min.y<b.min.y) b.min.y=p.min.y; if(p.max.y>b.max.y) b.max.y=p.max.y;
		if(p.min.z<b.min.z) b.min.z=p.min.z; if(p.max.z>b.max.z) b.max.z=p.max.z;
	}
	if(!isfinite(b.min.x)) b.min=b.max=v3(0,0,0);
	*outMin=b.min; *outMax=b.max;
}

int scene_enter_selected_prefab(Scene *s){
	XmlNode *n=(XmlNode*)s->selectedNode;
	if(!n || strcmp(n->tag,"prefab") || s->editDepth>=32) return 0;
	const char *source=xml_attr(n,"source",NULL);
	XmlNode *root=source?load_prefab(s,source):NULL;
	if(!root) return 0;
	s->editStack[s->editDepth++]=s->editRoot;
	s->editRoot=root; s->selectedNode=NULL;
	scene_rebuild_view(s);
	return 1;
}

int scene_exit_prefab(Scene *s){
	if(!s->editDepth) return 0;
	s->editRoot=s->editStack[--s->editDepth];
	s->selectedNode=NULL;
	scene_rebuild_view(s);
	return 1;
}

int scene_selected_prefab_path(Scene *s,char *path,size_t pathSize){
	XmlNode *n=(XmlNode*)s->selectedNode;
	if(!n||strcmp(n->tag,"prefab")||!path||!pathSize) return 0;
	const char *source=xml_attr(n,"source",NULL);
	if(!source||!load_prefab(s,source)) return 0;
	for(int i=0;i<s->nprefabs;i++) if(!strcmp(s->prefabs[i].ref,source)){
		snprintf(path,pathSize,"%s",s->prefabs[i].path);
		return 1;
	}
	return 0;
}

int scene_is_prefab_mode(Scene *s){ return s->prefabDocument||s->editDepth>0; }

static void xml_write_escaped(FILE *f,const char *s){
	for(;*s;s++){
		if(*s=='&') fputs("&amp;",f);
		else if(*s=='\"') fputs("&quot;",f);
		else if(*s=='<') fputs("&lt;",f);
		else if(*s=='>') fputs("&gt;",f);
		else fputc(*s,f);
	}
}

static void xml_write_node(FILE *f,XmlNode *n,int depth){
	for(int i=0;i<depth;i++) fputc('\t',f);
	fprintf(f,"<%s",n->tag);
	for(int i=0;i<n->nattrs;i++){
		fprintf(f," %s=\"",n->attrs[i].name);
		xml_write_escaped(f,n->attrs[i].value);
		fputc('\"',f);
	}
	if(!n->nkids){ fputs(" />\n",f); return; }
	fputs(">\n",f);
	for(int i=0;i<n->nkids;i++) xml_write_node(f,n->kids[i],depth+1);
	for(int i=0;i<depth;i++) fputc('\t',f);
	fprintf(f,"</%s>\n",n->tag);
}

static int xml_save_file(const char *path,XmlNode *root){
	FILE *f=fopen(path,"wb");
	if(!f){ fprintf(stderr,"cannot save %s\n",path); return 0; }
	xml_write_node(f,root,0);
	int error=ferror(f),closed=fclose(f);
	int ok=!error && closed==0;
	if(!ok) fprintf(stderr,"failed saving %s\n",path);
	return ok;
}

int scene_save_all(Scene *s){
	int ok=xml_save_file(s->scenePath,(XmlNode*)s->sceneRoot);
	for(int i=0;i<s->nprefabs;i++) if(!xml_save_file(s->prefabs[i].path,(XmlNode*)s->prefabs[i].root)) ok=0;
	return ok;
}

/* -------------------------------------------- gizmo interaction */

static vec3 mouse_ray(vec3 camRight, vec3 camUp, vec3 camLook,
	float camFov, int mx, int my, int W, int H)
{
	float fovRad=camFov*M_PIf/180.0f;
	float hh=tanf(fovRad*0.5f);
	float hw=hh*(float)W/(float)H;
	float ndcX=(2.0f*mx)/(float)W-1.0f;
	float ndcY=1.0f-(2.0f*my)/(float)H;
	return vnorm(vadd(vadd(vscale(camRight,ndcX*hw),vscale(camUp,ndcY*hh)),camLook));
}

void gizmo_begin_drag(Scene *s,int handle,int mouseX,int mouseY){
	if(s->selectedObj<0 || s->selectedObj>=s->nobjs) return;
	XmlNode *n=(XmlNode*)s->selectedNode;
	if(!n) return;
	if(s->selectedRigJoint){
		mat4 world;
		if(!scene_rig_joint_world(s,&world)) return;
		s->draggingHandle=handle; s->dragStartMouseX=mouseX; s->dragStartMouseY=mouseY;
		s->dragStartCenter=mat4_xform_point(world,v3(0,0,0));
		s->dragStartRot=scene_rig_joint_value(s,s->selectedRigInstance,s->selectedRigJoint,"rot");
		s->dragStartPos=scene_rig_joint_value(s,s->selectedRigInstance,s->selectedRigJoint,"pos");
		s->dragRigTarget=0; s->dragParentMatrix=world;
		for(int i=0;i<s->nrigTargets;i++) if(!strcmp(s->rigTargets[i].instance,xml_attr((XmlNode*)s->selectedRigInstance,"name","")) &&
			!strcmp(s->rigTargets[i].joint,xml_attr((XmlNode*)s->selectedRigJoint,"name",""))){
			s->dragRigTarget=1; s->dragStartPos=scene_rig_target_value(s,s->selectedRigInstance,s->selectedRigJoint,"target");
			s->dragParentMatrix=mat4_identity(); break;
		}
		fprintf(stderr,"[scener] rig drag start joint=%s handle=%d target=%d\n",xml_attr((XmlNode*)s->selectedRigJoint,"name",""),handle,s->dragRigTarget); fflush(stderr);
		return;
	}
	s->draggingHandle=handle;
	s->dragStartMouseX=mouseX; s->dragStartMouseY=mouseY;
	s->dragStartPos=xml_attr_v3_cm(n,"pos",v3(0,0,0));
	s->dragStartRot=xml_attr_v3(n,"rot",v3(0,0,0));
	s->dragStartScale=xml_attr_v3(n,"scale",v3(1,1,1));
	mat4 matrix; vec3 bmin,bmax;
	scene_get_obj_oriented_bounds(s,s->selectedObj,&matrix,&bmin,&bmax);
	s->dragStartEditMatrix=matrix;
	s->dragParentMatrix=mat4_mul(matrix,mat4_affine_inverse(xml_node_transform(s,n)));
	s->dragStartCenter=mat4_xform_point(matrix,v3(0,0,0));
	free(s->dragStartVerts); free(s->dragObjIndices); free(s->dragVertOffsets);
	s->dragStartVerts=NULL; s->dragObjIndices=NULL; s->dragVertOffsets=NULL;
	s->ndragStartObjs=s->ndragStartVerts=0;
	for(int i=0;i<s->nobjs;i++) if(s->objs[i].editNode==s->selectedNode){
		s->ndragStartObjs++; s->ndragStartVerts+=s->objs[i].mesh.nverts;
	}
	s->dragStartVerts=malloc(sizeof(Vertex)*(size_t)s->ndragStartVerts);
	s->dragObjIndices=malloc(sizeof(int)*(size_t)s->ndragStartObjs);
	s->dragVertOffsets=malloc(sizeof(int)*(size_t)s->ndragStartObjs);
	int oi=0,vo=0;
	for(int i=0;i<s->nobjs;i++) if(s->objs[i].editNode==s->selectedNode){
		s->dragObjIndices[oi]=i; s->dragVertOffsets[oi++]=vo;
		memcpy(s->dragStartVerts+vo,s->objs[i].mesh.verts,sizeof(Vertex)*(size_t)s->objs[i].mesh.nverts);
		vo+=s->objs[i].mesh.nverts;
	}
}

static int ray_plane_hit(vec3 ro,vec3 rd,vec3 point,vec3 normal,vec3 *hit){
	float denom=vdot(rd,normal);
	if(fabsf(denom)<1e-6f) return 0;
	float t=vdot(vsub(point,ro),normal)/denom;
	if(t<0) return 0;
	*hit=vadd(ro,vscale(rd,t));
	return 1;
}

static void scene_apply_drag_transform(Scene *s,XmlNode *n){
	mat4 matrix=mat4_mul(s->dragParentMatrix,xml_node_transform(s,n));
	mat4 delta=mat4_mul(matrix,mat4_affine_inverse(s->dragStartEditMatrix));
	for(int k=0;k<s->ndragStartObjs;k++){
		SceneObj *o=&s->objs[s->dragObjIndices[k]];
		Vertex *start=s->dragStartVerts+s->dragVertOffsets[k];
		for(int i=0;i<o->mesh.nverts;i++){
			o->mesh.verts[i].pos=mat4_xform_point(delta,start[i].pos);
			o->mesh.verts[i].nrm=mat4_xform_normal(delta,start[i].nrm);
		}
		o->editMatrix=matrix;
		mesh_compute_face_normals(&o->mesh);
		if(o->castsShadow) mesh_update_edge_positions(&o->mesh);
	}
	scene_rebuild_node_shadow_volumes(s,s->selectedNode);
}

void gizmo_apply_drag(Scene *s,int mX,int mY,int W,int H,
	vec3 camPos,vec3 camRight,vec3 camUp,vec3 camLook,float camFov){
	XmlNode *n=(XmlNode*)s->selectedNode;
	if(!n || s->draggingHandle==GIZMO_NONE) return;
	if(s->selectedRigJoint){
		int h=s->draggingHandle; vec3 center=s->dragStartCenter;
		vec3 sx=v3(1,0,0),sy=v3(0,1,0),sz=v3(0,0,1),a,b;
		vec3 curRay=mouse_ray(camRight,camUp,camLook,camFov,mX,mY,W,H);
		vec3 startRay=mouse_ray(camRight,camUp,camLook,camFov,s->dragStartMouseX,s->dragStartMouseY,W,H);
		if(s->editMode==EDIT_W_MOVE){
			vec3 delta;
			if(h==GIZMO_AXIS_X || h==GIZMO_AXIS_Y || h==GIZMO_AXIS_Z){
				vec3 axis=h==GIZMO_AXIS_X?sx:h==GIZMO_AXIS_Y?sy:sz;
				vec3 pn=vnorm(vsub(camLook,vscale(axis,vdot(camLook,axis))));
				if(!ray_plane_hit(camPos,startRay,center,pn,&a) || !ray_plane_hit(camPos,curRay,center,pn,&b)) return;
				delta=vscale(axis,vdot(vsub(b,a),axis));
			} else {
				vec3 pn=h==GIZMO_PLANE_XY?sz:h==GIZMO_PLANE_XZ?sy:h==GIZMO_PLANE_YZ?sx:v3(0,0,0);
				if(vlen(pn)<0.5f || !ray_plane_hit(camPos,startRay,center,pn,&a) || !ray_plane_hit(camPos,curRay,center,pn,&b)) return;
				delta=vsub(b,a);
			}
			delta=mat4_xform_dir(mat4_affine_inverse(s->dragParentMatrix),delta);
			vec3 value=vadd(s->dragStartPos,delta);
			if(s->dragRigTarget) scene_rig_set_target(s,s->selectedRigInstance,s->selectedRigJoint,"target",value);
			else scene_rig_set_joint(s,s->selectedRigInstance,s->selectedRigJoint,"pos",value);
		} else if(s->editMode==EDIT_E_ROTATE){
			vec3 axis=h==GIZMO_AXIS_X?sx:h==GIZMO_AXIS_Y?sy:h==GIZMO_AXIS_Z?sz:v3(0,0,0);
			if(vlen(axis)<0.5f || !ray_plane_hit(camPos,startRay,center,axis,&a) || !ray_plane_hit(camPos,curRay,center,axis,&b)) return;
			vec3 va=vnorm(vsub(a,center)),vb=vnorm(vsub(b,center));
			float angle=atan2f(vdot(axis,vcross(va,vb)),vdot(va,vb))*180.0f/M_PIf;
			vec3 rot=s->dragStartRot;
			if(h==GIZMO_AXIS_X) rot.x+=angle; else if(h==GIZMO_AXIS_Y) rot.y+=angle; else rot.z+=angle;
			scene_rig_set_joint(s,s->selectedRigInstance,s->selectedRigJoint,"rot",rot);
		}
		return;
	}
	int h=s->draggingHandle;
	vec3 center=s->dragStartCenter;
	vec3 sx=v3(1,0,0),sy=v3(0,1,0),sz=v3(0,0,1);
	vec3 curRay=mouse_ray(camRight,camUp,camLook,camFov,mX,mY,W,H);
	vec3 startRay=mouse_ray(camRight,camUp,camLook,camFov,s->dragStartMouseX,s->dragStartMouseY,W,H);
	if(s->editMode==EDIT_W_MOVE){
		vec3 a,b;
		if(h==GIZMO_AXIS_X || h==GIZMO_AXIS_Y || h==GIZMO_AXIS_Z){
			vec3 axis=h==GIZMO_AXIS_X?sx:h==GIZMO_AXIS_Y?sy:sz;
			vec3 pn=vnorm(vsub(camLook,vscale(axis,vdot(camLook,axis))));
			if(!ray_plane_hit(camPos,startRay,center,pn,&a) || !ray_plane_hit(camPos,curRay,center,pn,&b)) return;
			vec3 delta=vscale(axis,vdot(vsub(b,a),axis));
			xml_set_attr_v3_cm(n,"pos",vadd(s->dragStartPos,mat4_xform_dir(mat4_affine_inverse(s->dragParentMatrix),delta)));
		} else {
			vec3 pn=h==GIZMO_PLANE_XY?sz:h==GIZMO_PLANE_XZ?sy:h==GIZMO_PLANE_YZ?sx:v3(0,0,0);
			if(vlen(pn)<0.5f || !ray_plane_hit(camPos,startRay,center,pn,&a) || !ray_plane_hit(camPos,curRay,center,pn,&b)) return;
			vec3 delta=mat4_xform_dir(mat4_affine_inverse(s->dragParentMatrix),vsub(b,a));
			xml_set_attr_v3_cm(n,"pos",vadd(s->dragStartPos,delta));
		}
	} else if(s->editMode==EDIT_E_ROTATE){
		vec3 axis=h==GIZMO_AXIS_X?sx:h==GIZMO_AXIS_Y?sy:h==GIZMO_AXIS_Z?sz:v3(0,0,0),a,b;
		if(vlen(axis)<0.5f || !ray_plane_hit(camPos,startRay,center,axis,&a) || !ray_plane_hit(camPos,curRay,center,axis,&b)) return;
		vec3 va=vnorm(vsub(a,center)),vb=vnorm(vsub(b,center));
		float d=vdot(va,vb); if(d>1) d=1; if(d<-1) d=-1;
		float angle=acosf(d)*180.0f/M_PIf;
		if(vdot(axis,vcross(va,vb))<0) angle=-angle;
		vec3 rot=s->dragStartRot;
		if(h==GIZMO_AXIS_X) rot.x+=angle;
		else if(h==GIZMO_AXIS_Y) rot.y+=angle;
		else rot.z+=angle;
		xml_set_attr_v3(n,"rot",rot);
	} else if(s->editMode==EDIT_R_SCALE){
		int uniform=h==GIZMO_CENTER;
		vec3 axis=h==GIZMO_AXIS_X?sx:h==GIZMO_AXIS_Y?sy:h==GIZMO_AXIS_Z?sz:v3(0,0,0),a,b;
		vec3 pn=uniform?camLook:vnorm(vsub(camLook,vscale(axis,vdot(camLook,axis))));
		if((!uniform && vlen(axis)<0.5f) || !ray_plane_hit(camPos,startRay,center,pn,&a) || !ray_plane_hit(camPos,curRay,center,pn,&b)) return;
		float from=uniform?vlen(vsub(a,center)):vdot(vsub(a,center),axis);
		float to=uniform?vlen(vsub(b,center)):vdot(vsub(b,center),axis);
		if(fabsf(from)<1e-6f || to/from<=0) return;
		float ratio=to/from;
		vec3 scale=s->dragStartScale;
		if(uniform) scale=vscale(scale,ratio);
		else if(h==GIZMO_AXIS_X) scale.x*=ratio;
		else if(h==GIZMO_AXIS_Y) scale.y*=ratio;
		else scale.z*=ratio;
		xml_set_attr_v3(n,"scale",scale);
	}
	if(s->nnegativeProfiles){
		vec3 eye=s->camPos,look=s->camLook; float fov=s->camFov;
		scene_rebuild_view(s);
		s->camPos=eye; s->camLook=look; s->camFov=fov;
	} else scene_apply_drag_transform(s,n);
}

#ifdef SCENER_USE_TEXTURES
void scene_init_textures(Scene *s){
	materials_init(s->materialTextures);
	s->whiteTexture=materials_create_white_texture();
}

void scene_free_textures(Scene *s){
	for(int i=0;i<s->nscreenTextures;i++) if(s->screenTextures[i].texture)
		glDeleteTextures(1,&s->screenTextures[i].texture);
	materials_free(s->materialTextures);
	glDeleteTextures(1,&s->whiteTexture);
}
#else
void scene_init_textures(Scene *s){ (void)s; }
void scene_free_textures(Scene *s){
	for(int i=0;i<s->nscreenTextures;i++) if(s->screenTextures[i].texture)
		glDeleteTextures(1,&s->screenTextures[i].texture);
}
#endif
