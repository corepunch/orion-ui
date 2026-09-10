#ifndef SIMPLEGL_H
#define SIMPLEGL_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Define SCENER_USE_TEXTURES to enable procedural texture generation and
   shader texture sampling.  Disabled by default — no textures are used. */
/* #define SCENER_USE_TEXTURES */

#define M_PIf 3.14159265358979323846f

#define DA_PUSH(arr,count,cap,item) do{ \
	if((count) >= (cap)){ (cap) = (cap) ? (cap)*2 : 8; \
		(arr) = realloc((arr), (size_t)(cap)*sizeof(*(arr))); } \
	(arr)[(count)++] = (item); }while(0)

typedef struct { float x,y,z; } vec3;
typedef struct { float m[16]; } mat4;

vec3 v3(float x,float y,float z);
vec3 vadd(vec3 a,vec3 b);
vec3 vsub(vec3 a,vec3 b);
vec3 vscale(vec3 a,float s);
vec3 vmul(vec3 a,vec3 b);
vec3 vcross(vec3 a,vec3 b);
float vdot(vec3 a,vec3 b);
float vlen(vec3 a);
vec3 vnorm(vec3 a);
vec3 lerp(vec3 a, vec3 b, float t);

mat4 mat4_identity(void);
mat4 mat4_mul(mat4 a,mat4 b);
mat4 mat4_translate(vec3 t);
mat4 mat4_scale(vec3 s);
mat4 mat4_rot_x(float deg);
mat4 mat4_rot_y(float deg);
mat4 mat4_rot_z(float deg);
mat4 mat4_rot_xyz(vec3 rdeg);
mat4 mat4_affine_inverse(mat4 m);
vec3 mat4_xform_point(mat4 m,vec3 p);
vec3 mat4_xform_dir(mat4 m,vec3 p);
vec3 mat4_xform_normal(mat4 m,vec3 p);
mat4 mat4_perspective(float fovy_deg,float aspect,float znear,float zfar);
mat4 mat4_ortho(float left,float right,float bottom,float top,float znear,float zfar);
mat4 mat4_lookat(vec3 eye,vec3 center,vec3 up);
int ray_intersect_aabb(vec3 origin,vec3 dir,vec3 bbMin,vec3 bbMax,float *tOut);

typedef struct { vec3 pos,nrm; } Vertex;
typedef struct { int a,b,c; } Tri;
typedef struct { vec3 p0,p1; int t0,t1; int v0,v1; } Edge;

typedef struct {
	Vertex *verts; int nverts,cverts;
	Tri *tris; int ntris,ctris;
	Edge *edges; int nedges,cedges;
	vec3 *triN;
} Mesh;

typedef struct {
	vec3 *pts; int npts,cpts;
	vec3 *nrm;
	int closed;
	char name[32];
} Shape2D;

typedef struct {
	vec3 *pts; int npts,cpts;
} LoftPath;

typedef enum { WINDOW_RECTANGLE, WINDOW_ROUND_ARCH, WINDOW_POINTED_ARCH } window_outline_t;
Shape2D shape2d_window(window_outline_t outline,float width,float height,int segments);
Shape2D shape2d_inset(const Shape2D *profile,float distance);
Mesh gen_profile_extrusion(const Shape2D *profile,float depth);
Mesh gen_profile_frame(const Shape2D *outer,const Shape2D *inner,float depth);
Mesh gen_profile_cutouts(const Shape2D *boundary,const Shape2D *holes,int nholes,float depth);

void shape2d_free(Shape2D *s);
void shape2d_compute_normals(Shape2D *s);
Mesh gen_lathe(Shape2D *profile,int segments);
Mesh gen_loft(LoftPath *path,Shape2D *cross,int closed);

void mesh_free(Mesh *m);
int mesh_add_vert(Mesh *m,vec3 p,vec3 n);
void mesh_add_tri(Mesh *m,int a,int b,int c);
void mesh_transform(Mesh *m,mat4 posM,mat4 rotM);
void mesh_compute_face_normals(Mesh *m);
void mesh_build_edges(Mesh *m);
void mesh_update_edge_positions(Mesh *m);
float mesh_signed_volume(Mesh *m);
void mesh_flip_winding(Mesh *m);
void mesh_apply_taper(Mesh *m,float amount,float curvature,char axis);
void mesh_apply_twist(Mesh *m,float angle_deg,char axis);
void mesh_apply_bend(Mesh *m,float angle_deg,char axis);
void mesh_apply_stretch(Mesh *m,float amount,float amplify,char axis);
void mesh_apply_skew(Mesh *m,float amount,char axis);
void mesh_apply_array(Mesh *m,int count,vec3 off,vec3 rot);
void mesh_apply_extrude(Mesh *m,float amount,char axis);
void mesh_apply_mirror(Mesh *m,char axis,float weldThreshold);
void mesh_apply_noise(Mesh *m,float strength,int seed);
void mesh_apply_shell(Mesh *m,float amount);
Mesh gen_box(float sx,float sy,float sz);
Mesh gen_box_inset(float sx,float sy,float sz,float insetX,float insetY);
Mesh gen_cylinder_like(int sides,float rBot,float rTop,float height,int smooth);
Mesh gen_cylinder(float r,float h,int sides);
Mesh gen_cylinder_tube(float r,float h,float wall,int sides);
Mesh gen_prism(float r,float h,int sides);
Mesh gen_cone(float rBase,float rTop,float h,int sides);
Mesh gen_sphere(float r,int rings,int slices);
Mesh gen_torus(float R,float r,int majorSeg,int minorSeg);
Mesh gen_capsule(float r,float h,int rings,int slices);
Mesh gen_arch(float width,float height,float depth,float wall,int segments,float inset);
Mesh gen_box_hole_cylinder(float w,float h,float depth,float r,int sides);
Mesh gen_box_hole_arch(float w,float h,float depth,int sides);

typedef struct { char id[32]; vec3 color; float shininess; } Material;
typedef struct { char target[32]; vec3 pos,rot,scale; } CameraTransform;
typedef struct {
	char name[32],comment[64]; vec3 pos,look; float fov;
	CameraTransform *transforms; int ntransforms,ctransforms;
} Camera;
typedef struct { vec3 pos,color,dir; float intensity,radius; int castsShadow,isDirectional; } Light;
typedef struct { float x,y,z,w; } ShadowVertex;
typedef struct { ShadowVertex *verts; int nverts,cverts; } ShadowVolume;
typedef struct { Mesh mesh; vec3 color; float shininess; int castsShadow,renderable,unlit,sanityIgnore,sanityFloor,sanityCheck; void *editNode; mat4 editMatrix; ShadowVolume *shadowParts; int nshadowParts; int texIndex; } SceneObj;
typedef struct { char name[32]; vec3 pos; } AttachPoint;
typedef struct { char ref[32]; char path[256]; void *root; AttachPoint *attaches; int nattaches, cattaches; } PrefabDef;
typedef struct { char name[32]; char ref[32]; mat4 transform, rotMatrix; } InstanceDef;
typedef struct { mat4 transform; Shape2D profile; float depth; } negative_profile_t;
typedef struct { mat4 transform; vec3 size; } NegativeBox;
typedef struct { mat4 transform; float width,height,depth; } NegativeArch;
typedef struct { mat4 transform; float radius,depth; } NegativeCylinder;
typedef struct { vec3 start, end, color; int category; char camera[32]; } OverlayLine;
typedef struct { char name[32]; float height, radius; float top, neck, pelvis, feet; } CharDef;

enum {
	EDIT_Q_SELECT = 0,
	EDIT_W_MOVE,
	EDIT_E_ROTATE,
	EDIT_R_SCALE
};

/* Gizmo handle IDs — returned by gizmo_pick_handle() */
enum {
	GIZMO_NONE   = 0,
	GIZMO_AXIS_X = 1,  /* move: X arrow / rotate: X ring / scale: X cube  */
	GIZMO_AXIS_Y = 2,
	GIZMO_AXIS_Z = 3,
	GIZMO_PLANE_XY = 4, /* move: XY plane quad */
	GIZMO_PLANE_XZ = 5,
	GIZMO_PLANE_YZ = 6,
	GIZMO_CENTER   = 7  /* scale: centre cube */
};

typedef struct {
	vec3 camPos,camLook,worldUp; float camFov;
	int convention3dsMax;
	Camera *cameras; int ncameras,ccameras;
	vec3 ambient,bg;
	Light *lights; int nlights,clights;
	Material *mats; int nmats,cmats;
	SceneObj *objs; int nobjs,cobjs;
	ShadowVolume *svols;
	PrefabDef *prefabs; int nprefabs,cprefabs;
	InstanceDef *instances; int ninstances,cinstances;
	negative_profile_t *negativeProfiles; int nnegativeProfiles,cnegativeProfiles;
	NegativeBox *negativeBoxes; int nnegativeBoxes,cnegativeBoxes;
	NegativeArch *negativeArches; int nnegativeArches,cnegativeArches;
	NegativeCylinder *negativeCylinders; int nnegativeCylinders,cnegativeCylinders;
	Shape2D *shapes; int nshapes,cshapes;
	vec3 prefabTint; int prefabTintActive;
	int sanityIgnoreActive, sanityFloorActive, sanityCheckActive;
	OverlayLine *overlayLines; int noverlayLines, coverlayLines;
	CharDef *charDefs; int ncharDefs, ccharDefs;
	char activeCamera[32];
	char scenePath[512];
	char assetRoot[512];
	int prefabDocument;
	void *sceneRoot, *editRoot, *selectedNode, *activeEditNode;
	mat4 activeEditMatrix;
	int activeTexIndex;
	unsigned int materialTextures[8];
	unsigned int whiteTexture;
	void *editStack[32]; int editDepth;
	int selectedObj;
	int editMode;
	int createMode;    /* ID_CREATE_* while the viewport is placing primitives */
	int hoveredHandle;   /* GIZMO_* — set each frame by gizmo_pick_handle */
	int draggingHandle;  /* GIZMO_* — active drag handle, GIZMO_NONE when idle */
	int axisLock;        /* 0=all, or GIZMO_AXIS_X/Y/Z or GIZMO_PLANE_XY/XZ/YZ (keyboard axis lock) */
	int dragStartMouseX, dragStartMouseY;
	vec3 dragStartCenter; /* object centre at drag-start */
	vec3 dragPrevAnchor; /* anchor point from previous frame (rotate/scale delta) */
	vec3 dragStartPos,dragStartRot,dragStartScale;
	mat4 dragStartEditMatrix,dragParentMatrix;
	Vertex *dragStartVerts; int *dragObjIndices,*dragVertOffsets;
	int ndragStartObjs,ndragStartVerts;
} Scene;

int load_scene(const char *path,Scene *s);
void scene_free(Scene *s);
void scene_select_camera(Scene *s,const char *name);
void scene_add_obj(Scene *s,Mesh mesh,mat4 M,mat4 R,vec3 color,float shin,int castsShadow,int renderable,int unlit);
int scene_sanity_check(Scene *s);
vec3 light_to_source(Light *light,vec3 point);
void scene_rebuild_camera_gizmos(Scene *s,float aspect);
int scene_pick_object(Scene *s, vec3 rayOrigin, vec3 rayDir, float *tOut);
void scene_get_obj_bounds(Scene *s,int idx,vec3 *outMin,vec3 *outMax);
void scene_get_obj_oriented_bounds(Scene *s,int idx,mat4 *matrix,vec3 *outMin,vec3 *outMax);
int scene_enter_selected_prefab(Scene *s);
int scene_exit_prefab(Scene *s);
int scene_selected_prefab_path(Scene *s,char *path,size_t pathSize);
int scene_save_all(Scene *s);
int scene_create_window(Scene *s,const char *preset,vec3 ground);
int scene_is_prefab_mode(Scene *s);
const char *scene_node_tag(const void *node);
const char *scene_node_attr(const void *node,const char *name);
int scene_node_attr_count(const void *node);
const char *scene_node_attr_name(const void *node,int index);
const char *scene_node_attr_value(const void *node,int index);
void scene_get_bounds(Scene *s,vec3 *outMin,vec3 *outMax);
void scene_init_textures(Scene *s);
void scene_free_textures(Scene *s);
/* Gizmo interaction helpers */
int gizmo_pick_handle(Scene *s,vec3 rayOrigin,vec3 rayDir,vec3 camLook,float camFov,int vpW,int vpH);
void gizmo_draw(Scene *s,vec3 camPos,vec3 camLook,float camFov,int vpW,int vpH);
void gizmo_begin_drag(Scene *s,int handle,int mouseX,int mouseY);
void gizmo_apply_drag(Scene *s, int mX, int mY, int W, int H,
	vec3 camPos, vec3 camRight, vec3 camUp, vec3 camLook, float camFov);


void build_shadow_volume(Mesh *m,vec3 lightPos,vec3 lightDir,int isDir,ShadowVolume *sv);
void scene_build_all_shadow_volumes(Scene *s);
void scene_rebuild_node_shadow_volumes(Scene *s,void *editNode);

#define DBG_NONE            0
#define DBG_NO_SHADOWS      (1 << 0)
#define DBG_WIRE_SHADOWVOL  (1 << 1)
#define DBG_SHOW_STENCIL    (1 << 2)
#define DBG_HIDE_LIGHTS     (1 << 3)
#define DBG_HIDE_CHARS      (1 << 4)
#define DBG_HIDE_GIZMOS     (1 << 5)
#define DBG_WIREFRAME       (1 << 6)
#define DBG_FLAT            (1 << 7)

void render_frame(Scene *s,int w,int h,mat4 proj,mat4 view,vec3 camPos,vec3 camLook,int debugFlags);

#endif
