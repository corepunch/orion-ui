#include "test_framework.h"
#include "scener.h"
#include <unistd.h>

app_state_t *g_app;

static void test_tool_commands_share_document_state(void) {
  TEST("scener tools: command IDs update the document source of truth");
  app_state_t app = {0};
  scene_doc_t doc = {0};
  g_app = &app;
  app.docs = app.active_doc = &doc;

  handle_menu_command(ID_TOOL_SELECT); ASSERT_EQUAL(doc.scene.editMode, EDIT_Q_SELECT); ASSERT_EQUAL(scener_active_tool(), ID_TOOL_SELECT);
  handle_menu_command(ID_TOOL_MOVE);   ASSERT_EQUAL(doc.scene.editMode, EDIT_W_MOVE);   ASSERT_EQUAL(scener_active_tool(), ID_TOOL_MOVE);
  handle_menu_command(ID_TOOL_ROTATE); ASSERT_EQUAL(doc.scene.editMode, EDIT_E_ROTATE); ASSERT_EQUAL(scener_active_tool(), ID_TOOL_ROTATE);
  handle_menu_command(ID_TOOL_SCALE);  ASSERT_EQUAL(doc.scene.editMode, EDIT_R_SCALE);  ASSERT_EQUAL(scener_active_tool(), ID_TOOL_SCALE);

  g_app = NULL;
  PASS();
}

static void test_prefab_files_open_as_documents(void) {
  TEST("scener documents: prefab files load as editable document roots");
  Scene prefab = {0};
  ASSERT_TRUE(load_scene("apps/scener/prefabs/items/book.blk", &prefab));
  ASSERT_TRUE(scene_is_prefab_mode(&prefab));
  ASSERT_EQUAL(prefab.editDepth, 0);
  ASSERT_TRUE(prefab.nobjs > 0);
  ASSERT_EQUAL(prefab.nlights, 2);
  scene_free(&prefab);

  Scene scene = {0};
  ASSERT_TRUE(load_scene("apps/scener/scenes/test_prefab_tint.blks", &scene));
  scene.selectedObj = 0;
  scene.selectedNode = scene.objs[0].editNode;
  char path[512] = {0};
  ASSERT_TRUE(scene_selected_prefab_path(&scene, path, sizeof(path)));
  ASSERT_TRUE(strstr(path, "prefabs/items/book.blk") != NULL);
  scene_free(&scene);
  PASS();
}

static void test_nested_arch_emits_wall_parts_once(void) {
  TEST("scener walls: a contained arch does not duplicate wall geometry");
  Scene scene = {0};
  ASSERT_TRUE(load_scene("apps/scener/scenes/test_wall_nested_arch.blks", &scene));
  ASSERT_EQUAL(scene.nobjs, 4);
  scene_free(&scene);
  PASS();
}

static void test_explicit_scene_up_axis(void) {
  TEST("scene up axis controls views without reinterpreting geometry");
  Scene scene={0};
  ASSERT_TRUE(load_scene("apps/scener/tests/zup_scene.blks",&scene));
  ASSERT_TRUE(scene.worldUp.z==1 && scene.worldUp.y==0);
  ASSERT_TRUE(scene.camPos.z>scene.camPos.y);
  vec3 lo,hi;scene_get_obj_bounds(&scene,0,&lo,&hi);
  ASSERT_TRUE(fabsf((hi.z-lo.z)-2)<0.001f);
  ASSERT_TRUE(fabsf((hi.y-lo.y)-0.8f)<0.001f);
  scene_select_camera(&scene,"reverse");
  ASSERT_TRUE(scene.worldUp.z==1 && scene.worldUp.y==0);
  scene_free(&scene);
  ASSERT_TRUE(load_scene("apps/scener/scenes/test_prefab_tint.blks",&scene));
  ASSERT_TRUE(scene.worldUp.y==1 && scene.worldUp.z==0);
  scene_free(&scene);
  PASS();
}

#define WINDOW_TEST_VIEWPORT 100
#define WINDOW_TEST_DRAG_START 50
#define WINDOW_TEST_DRAG_FIRST 70
#define WINDOW_TEST_DRAG_SECOND 80
#define WINDOW_TEST_CAMERA_DISTANCE 5
#define WINDOW_TEST_FOV 60
#define WINDOW_TEST_EPSILON 0.0001f
#define WINDOW_TEST_MID_HEIGHT 1.5f
#define WINDOW_TEST_WIDTH 1.2f
#define WINDOW_TEST_HEIGHT 2.2f
#define WINDOW_TEST_FRAME 0.1f
#define WINDOW_TEST_DEPTH 0.2f
#define WINDOW_TEST_SEGMENTS 32
#define WINDOW_TEST_MIN_SEGMENTS 8
#define WINDOW_TEST_MAX_SEGMENTS 128
#define WINDOW_TEST_FRONT_POINTS 3
#define WINDOW_TEST_MAX_PATH 64

static float window_test_area(const Shape2D *p){
	float a=0;
	for(int i=0;i<p->npts;i++){
		vec3 u=p->pts[i],v=p->pts[(i+1)%p->npts]; a+=u.x*v.y-u.y*v.x;
	}
	return a/2;
}

static void test_window_profiles(void){
	TEST("window profiles: inset, watertight frame and exact pane volume for all presets");
	for(int type=WINDOW_RECTANGLE;type<=WINDOW_POINTED_ARCH;type++)
	for(int segments=WINDOW_TEST_MIN_SEGMENTS;segments<=WINDOW_TEST_MAX_SEGMENTS;segments*=2){
		Shape2D outer=shape2d_window(type,WINDOW_TEST_WIDTH,WINDOW_TEST_HEIGHT,segments);
		Shape2D inner=shape2d_inset(&outer,WINDOW_TEST_FRAME);
		ASSERT_TRUE(outer.npts>=WINDOW_TEST_FRONT_POINTS); ASSERT_TRUE(inner.npts>=WINDOW_TEST_FRONT_POINTS);
		for(int i=0;i<outer.npts;i++){
			vec3 a=outer.pts[i],b=outer.pts[(i+1)%outer.npts],edge=vnorm(vsub(b,a));
			float nearest=INFINITY;
			for(int j=0;j<inner.npts;j++) nearest=fminf(nearest,vdot(v3(-edge.y,edge.x,0),vsub(inner.pts[j],a)));
			ASSERT_TRUE(nearest>=WINDOW_TEST_FRAME-WINDOW_TEST_EPSILON);
		}
		Mesh frame=gen_profile_frame(&outer,&inner,WINDOW_TEST_DEPTH);
		Mesh pane=gen_profile_extrusion(&inner,WINDOW_TEST_DEPTH);
		mesh_compute_face_normals(&frame); mesh_build_edges(&frame);
		for(int i=0;i<frame.nedges;i++) ASSERT_TRUE(frame.edges[i].t1>=0);
		for(int i=0;i<frame.ntris;i++){
			Tri t=frame.tris[i];
			ASSERT_TRUE(vlen(vcross(vsub(frame.verts[t.b].pos,frame.verts[t.a].pos),vsub(frame.verts[t.c].pos,frame.verts[t.a].pos)))>0);
		}
		float expected=window_test_area(&outer)*WINDOW_TEST_DEPTH;
		ASSERT_TRUE(fabsf(mesh_signed_volume(&frame)+mesh_signed_volume(&pane)-expected)<WINDOW_TEST_EPSILON);
		Shape2D invalid=shape2d_inset(&outer,WINDOW_TEST_WIDTH);
		ASSERT_EQUAL(invalid.npts,0);
		shape2d_free(&outer); shape2d_free(&inner); shape2d_free(&invalid); mesh_free(&frame); mesh_free(&pane);
	}
	PASS();
}

static int window_test_load(Scene *s,const char *xml){
	char path[WINDOW_TEST_MAX_PATH]="/tmp/scener-window-XXXXXX";
	int fd=mkstemp(path); if(fd<0) return 0;
	FILE *file=fdopen(fd,"w"); if(!file){ close(fd); unlink(path); return 0; }
	fputs(xml,file); fclose(file);
	int result=load_scene(path,s); unlink(path); return result;
}

static void test_scene_coordinate_conventions(void){
	TEST("scene conventions: explicit 3ds Max conversion and native axes stay independent");
	Scene converted={0},native={0};
	ASSERT_TRUE(window_test_load(&converted,"<scene convention='3dsmax'><camera name='view' pos='100 -200 300' look='0 0 100'/>"
		"<box size='100 200 300'/></scene>"));
	ASSERT_TRUE(converted.convention3dsMax); ASSERT_TRUE(converted.worldUp.y==1);
	ASSERT_TRUE(converted.camPos.x==1 && converted.camPos.y==3 && converted.camPos.z==2);
	vec3 lo,hi; scene_get_obj_bounds(&converted,0,&lo,&hi);
	ASSERT_TRUE(fabsf((hi.y-lo.y)-3)<WINDOW_TEST_EPSILON);
	ASSERT_TRUE(fabsf((hi.z-lo.z)-2)<WINDOW_TEST_EPSILON);
	ASSERT_TRUE(window_test_load(&native,"<scene><box size='100 200 300'/></scene>"));
	ASSERT_FALSE(native.convention3dsMax); ASSERT_TRUE(native.worldUp.y==1);
	scene_get_obj_bounds(&native,0,&lo,&hi);
	ASSERT_TRUE(fabsf((hi.y-lo.y)-2)<WINDOW_TEST_EPSILON);
	ASSERT_TRUE(fabsf((hi.z-lo.z)-3)<WINDOW_TEST_EPSILON);
	scene_select_camera(&converted,"view");
	ASSERT_TRUE(converted.camPos.y==3 && converted.camPos.z==2);
	scene_free(&converted); scene_free(&native); PASS();
}

static int window_test_wall_at(Scene *s,vec3 point){
	for(int o=0;o<s->nobjs;o++){
		if(strcmp(scene_node_tag(s->objs[o].editNode),"wall")) continue;
		Mesh *m=&s->objs[o].mesh;
		for(int i=0;i<m->ntris;i++){
			Tri t=m->tris[i]; vec3 a=m->verts[t.a].pos,b=m->verts[t.b].pos,c=m->verts[t.c].pos;
			float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
			if(fabsf(area)<WINDOW_TEST_EPSILON) continue;
			float u=((b.x-point.x)*(c.y-point.y)-(b.y-point.y)*(c.x-point.x))/area;
			float v=((c.x-point.x)*(a.y-point.y)-(c.y-point.y)*(a.x-point.x))/area;
			if(u>=0&&v>=0&&u+v<=1) return 1;
		}
	}
	return 0;
}

static void test_window_wall_cutting(void){
	TEST("window wall cuts follow the outer silhouette, with no authored cutter");
	Scene s={0};
	ASSERT_TRUE(window_test_load(&s,"<scene><wall length='400' height='300' thickness='60'/>"
		"<window preset='round-arch' width='120' height='180' pos='0 150 0' frameWidth='10' depth='12' segments='8'/></scene>"));
	ASSERT_EQUAL(s.nnegativeProfiles,1);
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,2,0)));
	ASSERT_TRUE(window_test_wall_at(&s,v3(0,WINDOW_TEST_FRAME,0)));
	ASSERT_TRUE(window_test_wall_at(&s,v3(WINDOW_TEST_WIDTH/2-WINDOW_TEST_FRAME,WINDOW_TEST_HEIGHT+WINDOW_TEST_FRAME,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,WINDOW_TEST_HEIGHT+WINDOW_TEST_FRAME,0)));
	ASSERT_TRUE(window_test_wall_at(&s,v3(WINDOW_TEST_WIDTH,1,0)));
	ASSERT_EQUAL(s.nobjs,WINDOW_TEST_FRONT_POINTS);
	ASSERT_TRUE(s.objs[1].castsShadow); ASSERT_FALSE(s.objs[2].castsShadow);
	ASSERT_TRUE(s.objs[1].editNode==s.objs[2].editNode);
	scene_free(&s);
	PASS();
}

static void test_window_cut_transforms_and_union(void){
	TEST("window cuts preserve scale, mirrors, overlap, clipping and camera overrides");
	Scene s={0};
	ASSERT_TRUE(window_test_load(&s,"<scene>"
		"<camera name='base'/><camera name='moved'><transform target='insert' pos='100 0 0'/></camera>"
		"<wall length='400' height='300' thickness='60'/>"
		"<group name='insert' pos='0 150 0' scale='-2 1 1'><window preset='cottage' width='100' height='100' sill='0'/></group>"
		"</scene>"));
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,WINDOW_TEST_MID_HEIGHT,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(1-WINDOW_TEST_FRAME,WINDOW_TEST_MID_HEIGHT,0)));
	scene_select_camera(&s,"moved");
	ASSERT_TRUE(window_test_wall_at(&s,v3(0,WINDOW_TEST_MID_HEIGHT,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(-2+WINDOW_TEST_FRAME,WINDOW_TEST_MID_HEIGHT,0)));
	scene_select_camera(&s,"base");
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,WINDOW_TEST_MID_HEIGHT,0)));
	scene_free(&s);
	ASSERT_TRUE(window_test_load(&s,"<scene><wall length='400' height='300' thickness='60'/>"
		"<window preset='cottage' width='100' height='100' pos='0 100 0' sill='0'/>"
		"<window preset='cottage' width='100' height='100' pos='0 200 0' sill='0'/>"
		"<window preset='gothic' width='120' height='220' pos='190 250 0'/>"
		"<bool-negative-box pos='-150 150 0' size='50 50 100'/></scene>"));
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,1,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,2,0)));
	ASSERT_TRUE(window_test_wall_at(&s,v3(1,1,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(-1-WINDOW_TEST_WIDTH/2,1+WINDOW_TEST_WIDTH/2,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(2-WINDOW_TEST_FRAME,2,0)));
	scene_free(&s);
	PASS();
}

static void test_window_invalid_and_opt_out(void){
	TEST("invalid windows never cut walls; cutWalls and pane can be disabled");
	Scene s={0};
	ASSERT_TRUE(window_test_load(&s,"<scene><wall length='400' height='300' thickness='60'/>"
		"<window preset='gothic' width='120' height='20'/>"
		"<window preset='unknown'/><window frameWidth='200'/>"
		"<window preset='cottage' pos='0 150 0' cutWalls='0' pane='0' sill='0'/></scene>"));
	ASSERT_EQUAL(s.nnegativeProfiles,0);
	ASSERT_EQUAL(s.nobjs,2);
	ASSERT_TRUE(window_test_wall_at(&s,v3(0,1,0)));
	scene_free(&s);
	PASS();
}

static void test_window_picking_through_wall(void){
	TEST("window picking: holes in a wall's bounds do not intercept window clicks");
	Scene s={0};
	ASSERT_TRUE(window_test_load(&s,"<scene><wall length='400' height='300' thickness='60'/>"
		"<window pos='0 150 0' width='120' height='180' frameWidth='10' depth='12'/></scene>"));
	int hit=scene_pick_object(&s,v3(0,WINDOW_TEST_MID_HEIGHT,WINDOW_TEST_CAMERA_DISTANCE),v3(0,0,-1),NULL);
	ASSERT_TRUE(hit>=0); ASSERT_TRUE(!strcmp(scene_node_tag(s.selectedNode),"window"));
	hit=scene_pick_object(&s,v3(WINDOW_TEST_WIDTH,WINDOW_TEST_MID_HEIGHT,WINDOW_TEST_CAMERA_DISTANCE),v3(0,0,-1),NULL);
	ASSERT_TRUE(hit>=0); ASSERT_TRUE(!strcmp(scene_node_tag(s.selectedNode),"wall"));
	scene_free(&s); PASS();
}

static void test_window_drag_recuts_wall(void){
	TEST("window drag: old hole closes and new hole follows every mouse move");
	Scene s={0};
	ASSERT_TRUE(window_test_load(&s,"<scene><wall length='600' height='300' thickness='32'/>"
		"<window preset='cottage' pos='0 150 0' width='100' height='100' sill='0'/></scene>"));
	s.selectedObj=1; s.selectedNode=s.objs[1].editNode; s.editMode=EDIT_W_MOVE;
	gizmo_begin_drag(&s,GIZMO_AXIS_X,WINDOW_TEST_DRAG_START,WINDOW_TEST_DRAG_START);
	vec3 eye=v3(0,WINDOW_TEST_MID_HEIGHT,WINDOW_TEST_CAMERA_DISTANCE),right=v3(1,0,0),up=v3(0,1,0),look=v3(0,0,-1);
	gizmo_apply_drag(&s,WINDOW_TEST_DRAG_FIRST,WINDOW_TEST_DRAG_START,WINDOW_TEST_VIEWPORT,WINDOW_TEST_VIEWPORT,eye,right,up,look,WINDOW_TEST_FOV);
	ASSERT_TRUE(window_test_wall_at(&s,v3(0,WINDOW_TEST_MID_HEIGHT,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(1,WINDOW_TEST_MID_HEIGHT,0)));
	gizmo_apply_drag(&s,WINDOW_TEST_DRAG_SECOND,WINDOW_TEST_DRAG_START,WINDOW_TEST_VIEWPORT,WINDOW_TEST_VIEWPORT,eye,right,up,look,WINDOW_TEST_FOV);
	ASSERT_TRUE(window_test_wall_at(&s,v3(1,WINDOW_TEST_MID_HEIGHT,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(2-WINDOW_TEST_DEPTH,WINDOW_TEST_MID_HEIGHT,0)));
	scene_free(&s); PASS();
}

static void test_window_creation_persists(void){
	TEST("window creation: one source element survives save/reload and owns all parts");
	Scene s={0};
	ASSERT_TRUE(window_test_load(&s,"<scene up='z'><wall length='400' height='300' thickness='32' rot='90 0 0'/></scene>"));
	ASSERT_TRUE(scene_create_window(&s,"gothic",v3(0,0,0)));
	ASSERT_EQUAL(s.nnegativeProfiles,1);
	ASSERT_TRUE(s.selectedNode!=NULL);
	ASSERT_TRUE(!strcmp(scene_node_tag(s.selectedNode),"window"));
	ASSERT_TRUE(!strcmp(scene_node_attr(s.selectedNode,"rot"),"90 0 0"));
	int parts=0;
	for(int i=0;i<s.nobjs;i++) if(s.objs[i].editNode==s.selectedNode) parts++;
	ASSERT_EQUAL(parts,2);
	ASSERT_TRUE(scene_save_all(&s));
	Scene restored={0}; ASSERT_TRUE(load_scene(s.scenePath,&restored));
	ASSERT_EQUAL(restored.nnegativeProfiles,1);
	ASSERT_EQUAL(restored.nobjs,s.nobjs);
	unlink(s.scenePath); scene_free(&s); scene_free(&restored);
	PASS();
}

int main(void) {
  TEST_START("scener input and command state");
  test_tool_commands_share_document_state();
  test_prefab_files_open_as_documents();
  test_nested_arch_emits_wall_parts_once();
  test_explicit_scene_up_axis();
  test_scene_coordinate_conventions();
  test_window_profiles();
  test_window_wall_cutting();
  test_window_cut_transforms_and_union();
  test_window_invalid_and_opt_out();
  test_window_creation_persists();
  test_window_drag_recuts_wall();
  test_window_picking_through_wall();
  TEST_END();
}
