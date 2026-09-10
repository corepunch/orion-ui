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
  ASSERT_EQUAL(scene.nobjs, 1);
  Shape2D arch=shape2d_window(WINDOW_ROUND_ARCH,2,3,32);
  Mesh opening=gen_profile_extrusion(&arch,0.2f);
  ASSERT_TRUE(fabsf(mesh_signed_volume(&scene.objs[0].mesh)+mesh_signed_volume(&opening)-4.8f)<0.0001f);
  mesh_free(&opening); shape2d_free(&arch);
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
	ASSERT_TRUE(window_test_load(&s,"<scene><wall length='600' height='300' thickness='32' lowerHeight='150' lowerColor='0.2 0.3 0.4' middleTrimHeight='10' bottomTrimHeight='10' trimSide='both'/>"
		"<window preset='cottage' pos='0 150 0' width='100' height='100' sill='0'/></scene>"));
	for(int i=0;i<s.nobjs;i++) if(!strcmp(scene_node_tag(s.objs[i].editNode),"window")){ s.selectedObj=i; break; }
	s.selectedNode=s.objs[s.selectedObj].editNode; s.editMode=EDIT_W_MOVE;
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


#define DOOR_TEST_MAX_XML 1024
#define DOOR_TEST_WIDTH 1.54f
#define DOOR_TEST_HEIGHT 2.70f
#define DOOR_TEST_FRAME 0.10f
#define DOOR_TEST_GAP 0.002f
#define DOOR_TEST_DEPTH 0.20f
#define DOOR_TEST_PET_WIDTH 0.40f
#define DOOR_TEST_PET_HEIGHT 0.48f
#define DOOR_TEST_TRIM 0.02f
#define DOOR_TEST_LEAF_DEPTH 0.08f
#define DOOR_TEST_SEGMENTS 32

static void test_door_fit_swing_and_pet_opening(void){
	TEST("procedural doors: matched frame/leaf, floor passage, hinge swing and fixed wall cutter");
	const char *presets[]={"rectangular","round-arch","gothic"};
	for(int p=0;p<(int)(sizeof(presets)/sizeof(presets[0]));p++){
		Scene closed={0},opened={0}; char xml[DOOR_TEST_MAX_XML];
		snprintf(xml,sizeof(xml),"<scene><door preset='%s' width='154' height='270' frameWidth='10' depth='20' leafDepth='8' clearance='0.2' handle='0' petWidth='40' petHeight='48' petFrameWidth='2'/></scene>",presets[p]);
		ASSERT_TRUE(window_test_load(&closed,xml));
		ASSERT_EQUAL(closed.nnegativeProfiles,1); ASSERT_EQUAL(closed.nobjs,3);
		ASSERT_TRUE(closed.objs[0].editNode==closed.objs[1].editNode&&closed.objs[1].editNode==closed.objs[2].editNode);
		vec3 lo=v3(INFINITY,INFINITY,INFINITY);
		for(int i=0;i<closed.objs[1].mesh.nverts;i++){
			vec3 v=closed.objs[1].mesh.verts[i].pos;lo.x=fminf(lo.x,v.x);lo.y=fminf(lo.y,v.y);
		}
		ASSERT_TRUE(fabsf(lo.x-(-DOOR_TEST_WIDTH/2+DOOR_TEST_FRAME+DOOR_TEST_GAP))<WINDOW_TEST_EPSILON);
		ASSERT_TRUE(fabsf(lo.y-(-DOOR_TEST_HEIGHT/2+DOOR_TEST_GAP))<WINDOW_TEST_EPSILON);
		Shape2D inner=shape2d_inset(&closed.negativeProfiles[0].profile,DOOR_TEST_FRAME);
		for(int i=0;i<inner.npts;i++) if(inner.pts[i].y<=-DOOR_TEST_HEIGHT/2+DOOR_TEST_FRAME+WINDOW_TEST_EPSILON) inner.pts[i].y=-DOOR_TEST_HEIGHT/2;
		Shape2D leaf=shape2d_inset(&inner,DOOR_TEST_GAP);
		Shape2D pet=shape2d_window(WINDOW_ROUND_ARCH,DOOR_TEST_PET_WIDTH+2*DOOR_TEST_TRIM,DOOR_TEST_PET_HEIGHT+DOOR_TEST_TRIM,DOOR_TEST_SEGMENTS);
		float expected=(window_test_area(&leaf)-window_test_area(&pet))*DOOR_TEST_LEAF_DEPTH;
		ASSERT_TRUE(fabsf(mesh_signed_volume(&closed.objs[1].mesh)-expected)<WINDOW_TEST_EPSILON);
		shape2d_free(&inner);shape2d_free(&leaf);shape2d_free(&pet);
		for(int side=-1;side<=1;side+=2){
			snprintf(xml,sizeof(xml),"<scene><door preset='%s' width='154' height='270' frameWidth='10' depth='20' leafDepth='8' clearance='0.2' handle='0' petWidth='40' petHeight='48' petFrameWidth='2' hinge='%s' openAngle='90'/></scene>",presets[p],side<0?"left":"right");
			ASSERT_TRUE(window_test_load(&opened,xml)); ASSERT_EQUAL(opened.nobjs,closed.nobjs);
			ASSERT_EQUAL(opened.negativeProfiles[0].profile.npts,closed.negativeProfiles[0].profile.npts);
			for(int i=0;i<closed.objs[0].mesh.nverts;i++) ASSERT_TRUE(vlen(vsub(closed.objs[0].mesh.verts[i].pos,opened.objs[0].mesh.verts[i].pos))<WINDOW_TEST_EPSILON);
			float hingeX=side*(DOOR_TEST_WIDTH/2-DOOR_TEST_FRAME);
			for(int o=1;o<closed.nobjs;o++) for(int i=0;i<closed.objs[o].mesh.nverts;i++){
				vec3 a=closed.objs[o].mesh.verts[i].pos,b=opened.objs[o].mesh.verts[i].pos;
				ASSERT_TRUE(fabsf(b.x-(hingeX+side*(a.z-DOOR_TEST_DEPTH/2)))<WINDOW_TEST_EPSILON);
				ASSERT_TRUE(fabsf(b.y-a.y)<WINDOW_TEST_EPSILON);
				ASSERT_TRUE(fabsf(b.z-(DOOR_TEST_DEPTH/2-side*(a.x-hingeX)))<WINDOW_TEST_EPSILON);
			}
			scene_free(&opened);
		}
		scene_free(&closed);
	}
	Scene s={0};
	ASSERT_TRUE(window_test_load(&s,"<scene><wall length='400' height='300' thickness='24'/><door preset='round-arch' width='154' height='270' pos='0 135 0'/></scene>"));
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,DOOR_TEST_GAP,0)));
	ASSERT_TRUE(window_test_wall_at(&s,v3(DOOR_TEST_WIDTH/2,DOOR_TEST_HEIGHT-DOOR_TEST_FRAME,0)));
	scene_free(&s);
	const char *invalid[]={"openAngle='nan'","hinge='top'","leafDepth='100'","clearance='90'","petWidth='150' petHeight='60'","petWidth='40'","preset='missing'","petFrameWidth='nan'"};
	for(int i=0;i<(int)(sizeof(invalid)/sizeof(invalid[0]));i++){
		char xml[DOOR_TEST_MAX_XML];snprintf(xml,sizeof(xml),"<scene><door %s/></scene>",invalid[i]);
		ASSERT_TRUE(window_test_load(&s,xml));ASSERT_EQUAL(s.nobjs,0);ASSERT_EQUAL(s.nnegativeProfiles,0);scene_free(&s);
	}
	ASSERT_TRUE(window_test_load(&s,"<scene up='z'/>"));
	ASSERT_TRUE(scene_create_door(&s,"round-arch",v3(0,0,0)));
	ASSERT_TRUE(!strcmp(scene_node_tag(s.selectedNode),"door"));
	snprintf(s.scenePath,sizeof(s.scenePath),"/tmp/scener-door-save-%d.blks",getpid());
	ASSERT_TRUE(scene_save_all(&s));
	Scene restored={0};ASSERT_TRUE(load_scene(s.scenePath,&restored));ASSERT_EQUAL(restored.nobjs,s.nobjs);ASSERT_EQUAL(restored.nnegativeProfiles,1);
	unlink(s.scenePath);scene_free(&s);scene_free(&restored);
	PASS();
}


static void test_long_camera_names(void){
	TEST("camera names retain distinct long Book asset IDs through selection and reload");
	Scene s={0};
	const char *first="workbench-top-examine-half-finished-toys",*second="workbench-top-examine-half-finished-toys-repaired";
	ASSERT_TRUE(window_test_load(&s,"<scene><camera name='workbench-top-examine-half-finished-toys' pos='0 0 500'/><camera name='workbench-top-examine-half-finished-toys-repaired' pos='0 0 600'/></scene>"));
	ASSERT_TRUE(!strcmp(s.cameras[0].name,first));ASSERT_TRUE(!strcmp(s.cameras[1].name,second));
	scene_select_camera(&s,second);ASSERT_TRUE(!strcmp(s.activeCamera,second));
	scene_select_camera(&s,first);ASSERT_TRUE(!strcmp(s.activeCamera,first));
	snprintf(s.scenePath,sizeof(s.scenePath),"/tmp/scener-long-camera-%d.blks",getpid());
	ASSERT_TRUE(scene_save_all(&s));
	Scene restored={0};ASSERT_TRUE(load_scene(s.scenePath,&restored));
	ASSERT_TRUE(!strcmp(restored.cameras[0].name,first));ASSERT_TRUE(!strcmp(restored.cameras[1].name,second));
	unlink(s.scenePath);scene_free(&s);scene_free(&restored);PASS();
}


#define DOOR_WINDOW_ASSERT(expr) do { int passed=(expr); if(!passed) fprintf(stderr,"door window assertion %d: %s\n",__LINE__,#expr); ASSERT_TRUE(passed); } while(0)
#define DOOR_WINDOW_EQUAL(a,b) DOOR_WINDOW_ASSERT((a)==(b))
#define DOOR_WINDOW_TEST_CENTER 0.54f
#define DOOR_WINDOW_TEST_PANE_DEPTH 0.02f
#define DOOR_WINDOW_TEST_CAMERA_DISTANCE 5

static void test_door_windows(void){
	TEST("door windows: optional matching/circular apertures, exact cuts, glazing, hinge and persistence");
	const char *presets[]={"rectangular","round-arch","gothic"};
	const char *windows[]={"round","matching","rectangular","round-arch","gothic"};
	for(int p=0;p<(int)(sizeof(presets)/sizeof(presets[0]));p++){
		Scene plain={0};char xml[DOOR_TEST_MAX_XML];
		snprintf(xml,sizeof(xml),"<scene><door preset='%s' width='154' height='270' frameWidth='10' depth='20' leafDepth='8' handle='0' petWidth='40' petHeight='48' petFrameWidth='2'/></scene>",presets[p]);
		DOOR_WINDOW_ASSERT(window_test_load(&plain,xml));
		for(int w=0;w<(int)(sizeof(windows)/sizeof(windows[0]));w++){
			Scene closed={0},opened={0},empty={0};
			snprintf(xml,sizeof(xml),"<scene><door preset='%s' width='154' height='270' frameWidth='10' depth='20' leafDepth='8' handle='0' petWidth='40' petHeight='48' petFrameWidth='2' window='%s'/></scene>",presets[p],windows[w]);
			DOOR_WINDOW_ASSERT(window_test_load(&closed,xml));DOOR_WINDOW_EQUAL(closed.nobjs,5);DOOR_WINDOW_EQUAL(closed.nnegativeProfiles,1);
			for(int i=0;i<closed.nobjs;i++) DOOR_WINDOW_ASSERT(closed.objs[i].editNode==closed.objs[0].editNode);
			ASSERT_FALSE(closed.objs[4].castsShadow);
			float removed=mesh_signed_volume(&plain.objs[1].mesh)-mesh_signed_volume(&closed.objs[1].mesh);
			float filled=mesh_signed_volume(&closed.objs[3].mesh)/2+mesh_signed_volume(&closed.objs[4].mesh)*DOOR_TEST_LEAF_DEPTH/DOOR_WINDOW_TEST_PANE_DEPTH;
			DOOR_WINDOW_ASSERT(removed>0&&fabsf(removed-filled)<WINDOW_TEST_EPSILON);
			vec3 ray=v3(0,DOOR_WINDOW_TEST_CENTER,DOOR_WINDOW_TEST_CAMERA_DISTANCE);
			DOOR_WINDOW_EQUAL(scene_pick_object(&closed,ray,v3(0,0,-1),NULL),0);
			snprintf(xml,sizeof(xml),"<scene><door preset='%s' width='154' height='270' frameWidth='10' depth='20' leafDepth='8' handle='0' petWidth='40' petHeight='48' petFrameWidth='2' window='%s' windowPane='0'/></scene>",presets[p],windows[w]);
			DOOR_WINDOW_ASSERT(window_test_load(&empty,xml));DOOR_WINDOW_EQUAL(empty.nobjs,4);
			DOOR_WINDOW_EQUAL(scene_pick_object(&empty,ray,v3(0,0,-1),NULL),-1);
			for(int side=-1;side<=1;side+=2){
				snprintf(xml,sizeof(xml),"<scene><door preset='%s' width='154' height='270' frameWidth='10' depth='20' leafDepth='8' handle='0' petWidth='40' petHeight='48' petFrameWidth='2' window='%s' hinge='%s' openAngle='90'/></scene>",presets[p],windows[w],side<0?"left":"right");
				DOOR_WINDOW_ASSERT(window_test_load(&opened,xml));DOOR_WINDOW_EQUAL(opened.nobjs,closed.nobjs);
				float hingeX=side*(DOOR_TEST_WIDTH/2-DOOR_TEST_FRAME);
				for(int o=1;o<closed.nobjs;o++) for(int i=0;i<closed.objs[o].mesh.nverts;i++){
					vec3 a=closed.objs[o].mesh.verts[i].pos,b=opened.objs[o].mesh.verts[i].pos;
					DOOR_WINDOW_ASSERT(fabsf(b.x-(hingeX+side*(a.z-DOOR_TEST_DEPTH/2)))<WINDOW_TEST_EPSILON);
					DOOR_WINDOW_ASSERT(fabsf(b.y-a.y)<WINDOW_TEST_EPSILON);
					DOOR_WINDOW_ASSERT(fabsf(b.z-(DOOR_TEST_DEPTH/2-side*(a.x-hingeX)))<WINDOW_TEST_EPSILON);
				}
				scene_free(&opened);
			}
			scene_free(&closed);scene_free(&empty);
		}
		scene_free(&plain);
	}
	const char *invalid[]={"window='unknown'","window='round' windowWidth='-1'","window='round' windowHeight='60'","window='matching' windowWidth='500'","window='round' windowFrameWidth='100'","window='round' windowCenter='nan'","window='round' windowPaneDepth='100'","window='round' windowCenter='40' petWidth='40' petHeight='48'","window='gothic' windowHeight='1'"};
	for(int i=0;i<(int)(sizeof(invalid)/sizeof(invalid[0]));i++){
		Scene s={0};char xml[DOOR_TEST_MAX_XML];snprintf(xml,sizeof(xml),"<scene><door %s/></scene>",invalid[i]);
		DOOR_WINDOW_ASSERT(window_test_load(&s,xml));DOOR_WINDOW_EQUAL(s.nobjs,0);DOOR_WINDOW_EQUAL(s.nnegativeProfiles,0);scene_free(&s);
	}
	Scene s={0},restored={0};
	DOOR_WINDOW_ASSERT(window_test_load(&s,"<scene up='z'><door window='round' rot='90 0 0' openAngle='65' windowPane='0'/></scene>"));
	snprintf(s.scenePath,sizeof(s.scenePath),"/tmp/scener-door-window-%d.blks",getpid());
	DOOR_WINDOW_ASSERT(scene_save_all(&s));DOOR_WINDOW_ASSERT(load_scene(s.scenePath,&restored));DOOR_WINDOW_EQUAL(restored.nobjs,s.nobjs);
	for(int o=0;o<s.nobjs;o++) for(int i=0;i<s.objs[o].mesh.nverts;i++) DOOR_WINDOW_ASSERT(vlen(vsub(s.objs[o].mesh.verts[i].pos,restored.objs[o].mesh.verts[i].pos))<WINDOW_TEST_EPSILON);
	unlink(s.scenePath);scene_free(&s);scene_free(&restored);PASS();
}

#define SURFACE_ASSERT(condition) do { int surface_ok=(condition); if(!surface_ok) fprintf(stderr,"surface assertion at line %d: %s\n",__LINE__,#condition); ASSERT_TRUE(surface_ok); } while(0)
#define SURFACE_TEST_EPSILON 0.0001f
#define SURFACE_TEST_CAPACITY 2048
#define SURFACE_TEST_WALL_XML "<wall length='400' height='300' thickness='20' lowerHeight='100' lowerColor='0.2 0.3 0.4' upperColor='0.8 0.7 0.6' bottomTrimHeight='10' middleTrimHeight='10' topTrimHeight='10' trimDepth='4' trimColor='0.1 0.1 0.1' trimSide='both'/>"

static int surface_test_closed(Scene *s){
	for(int o=0;o<s->nobjs;o++){
		Mesh *m=&s->objs[o].mesh;
		if(!m->ntris||mesh_signed_volume(m)<=0){ fprintf(stderr,"surface mesh %d: volume %g\n",o,mesh_signed_volume(m)); return 0; }
		for(int e=0;e<m->nedges;e++) if(m->edges[e].t1<0){ fprintf(stderr,"surface mesh %d (%s): open edge %d\n",o,scene_node_tag(s->objs[o].editNode),e); return 0; }
	}
	return 1;
}

static void surface_test_bounds(Mesh *m,vec3 *lo,vec3 *hi){
	*lo=v3(INFINITY,INFINITY,INFINITY); *hi=v3(-INFINITY,-INFINITY,-INFINITY);
	for(int i=0;i<m->nverts;i++){
		vec3 p=m->verts[i].pos;
		lo->x=fminf(lo->x,p.x); lo->y=fminf(lo->y,p.y); lo->z=fminf(lo->z,p.z);
		hi->x=fmaxf(hi->x,p.x); hi->y=fmaxf(hi->y,p.y); hi->z=fmaxf(hi->z,p.z);
	}
}

static void test_wall_sections_and_trims(void){
	TEST("wall sections and trims share exact cuts, materials and closed geometry");
	Scene s={0};
	SURFACE_ASSERT(window_test_load(&s,"<scene>" SURFACE_TEST_WALL_XML "</scene>"));
	ASSERT_EQUAL(s.nobjs,8);
	SURFACE_ASSERT(surface_test_closed(&s));
	vec3 lo,hi; surface_test_bounds(&s.objs[0].mesh,&lo,&hi);
	SURFACE_ASSERT(fabsf(lo.y)<SURFACE_TEST_EPSILON&&fabsf(hi.y-1)<SURFACE_TEST_EPSILON);
	SURFACE_ASSERT(fabsf(s.objs[0].color.x-0.2f)<SURFACE_TEST_EPSILON);
	SURFACE_ASSERT(fabsf(s.objs[1].color.x-0.8f)<SURFACE_TEST_EPSILON);
	float volume=0; for(int i=0;i<s.nobjs;i++) volume+=mesh_signed_volume(&s.objs[i].mesh);
	SURFACE_ASSERT(fabsf(volume-(4*3*0.2f+2*3*4*0.1f*0.04f))<SURFACE_TEST_EPSILON);
	scene_free(&s);
	SURFACE_ASSERT(window_test_load(&s,"<scene>" SURFACE_TEST_WALL_XML
		"<door preset='round-arch' width='100' height='280' depth='2' leafDepth='1' handle='0' frameWidth='4' pos='0 140 0'/>"
		"<window preset='gothic' width='80' height='180' pos='130 190 0' depth='2' frameWidth='4' pane='0'/>"
		"</scene>"));
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,0.05f,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(0,1,0)));
	ASSERT_FALSE(window_test_wall_at(&s,v3(1.3f,1.05f,0)));
	SURFACE_ASSERT(window_test_wall_at(&s,v3(-1,1,0)));
	SURFACE_ASSERT(window_test_wall_at(&s,v3(0,2.95f,0)));
	SURFACE_ASSERT(surface_test_closed(&s));
	scene_free(&s);
	PASS();
}

static void test_wall_surface_transforms_and_invalid(void){
	TEST("wall surfaces preserve grouped transforms, flags, bounds and reject invalid parameters");
	Scene a={0},b={0};
	SURFACE_ASSERT(window_test_load(&a,"<scene>" SURFACE_TEST_WALL_XML "<bool-negative-box pos='0 100 0' size='100 200 30'/></scene>"));
	ASSERT_FALSE(window_test_wall_at(&a,v3(0,0.05f,0)));
	ASSERT_FALSE(window_test_wall_at(&a,v3(0,1,0)));
	SURFACE_ASSERT(window_test_load(&b,"<scene up='z'><group pos='100 200 300' rot='90 0 30' scale='2 2 2'>"
		SURFACE_TEST_WALL_XML "<bool-negative-box pos='0 100 0' size='100 200 30'/></group></scene>"));
	ASSERT_EQUAL(a.nobjs,b.nobjs);
	mat4 transform=mat4_mul(mat4_translate(v3(1,2,3)),mat4_mul(mat4_rot_xyz(v3(90,0,30)),mat4_scale(v3(2,2,2))));
	for(int o=0;o<a.nobjs;o++){
		ASSERT_EQUAL(a.objs[o].mesh.nverts,b.objs[o].mesh.nverts);
		for(int v=0;v<a.objs[o].mesh.nverts;v++)
			SURFACE_ASSERT(vlen(vsub(mat4_xform_point(transform,a.objs[o].mesh.verts[v].pos),b.objs[o].mesh.verts[v].pos))<SURFACE_TEST_EPSILON);
	}
	scene_free(&a); scene_free(&b);
	const char *invalid[]={"length='0'","height='nan'","thickness='-1'","lowerHeight='400'","trimSide='left'",
		"middleTrimHeight='20'","bottomTrimHeight='200' topTrimHeight='100'","lowerHeight='100' middleTrimHeight='20' bottomTrimHeight='100'"};
	for(int i=0;i<(int)(sizeof(invalid)/sizeof(invalid[0]));i++){
		char xml[SURFACE_TEST_CAPACITY]; snprintf(xml,sizeof(xml),"<scene><wall %s/></scene>",invalid[i]);
		SURFACE_ASSERT(window_test_load(&a,xml)); ASSERT_EQUAL(a.nobjs,0); scene_free(&a);
	}
	SURFACE_ASSERT(window_test_load(&a,"<scene><wall lowerHeight='270' renderable='0' castShadow='0' unlit='1' bottomTrimHeight='10'/></scene>"));
	ASSERT_EQUAL(a.nobjs,2);
	for(int o=0;o<a.nobjs;o++){ ASSERT_FALSE(a.objs[o].renderable); ASSERT_FALSE(a.objs[o].castsShadow); SURFACE_ASSERT(a.objs[o].unlit); }
	scene_free(&a); PASS();
}

static void test_procedural_floors(void){
	TEST("floor styles stay within footprint, close every mesh and vary colors deterministically");
	const char *styles[]={"boards","squares","hexes","stones"};
	for(int style=0;style<(int)(sizeof(styles)/sizeof(styles[0]));style++){
		Scene a={0},b={0},c={0}; char xml[SURFACE_TEST_CAPACITY];
		snprintf(xml,sizeof(xml),"<scene><floor style='%s' width='137' depth='113' tileWidth='30' gap='1' color='0.5 0.4 0.3' colorVariation='0.25' seed='42'/></scene>",styles[style]);
		SURFACE_ASSERT(window_test_load(&a,xml)); SURFACE_ASSERT(window_test_load(&b,xml));
		SURFACE_ASSERT(a.nobjs>2); ASSERT_EQUAL(a.nobjs,b.nobjs); SURFACE_ASSERT(surface_test_closed(&a));
		int varied=0; void *node=a.objs[0].editNode;
		for(int o=0;o<a.nobjs;o++){
			vec3 lo,hi; surface_test_bounds(&a.objs[o].mesh,&lo,&hi);
			SURFACE_ASSERT(lo.x>=-0.685f-SURFACE_TEST_EPSILON&&hi.x<=0.685f+SURFACE_TEST_EPSILON);
			SURFACE_ASSERT(lo.z>=-0.565f-SURFACE_TEST_EPSILON&&hi.z<=0.565f+SURFACE_TEST_EPSILON);
			SURFACE_ASSERT(lo.y>=-0.18f-SURFACE_TEST_EPSILON&&hi.y<=SURFACE_TEST_EPSILON);
			SURFACE_ASSERT(a.objs[o].editNode==node);
			SURFACE_ASSERT(vlen(vsub(a.objs[o].color,b.objs[o].color))==0);
			ASSERT_EQUAL(a.objs[o].mesh.nverts,b.objs[o].mesh.nverts);
			for(int v=0;v<a.objs[o].mesh.nverts;v++) SURFACE_ASSERT(vlen(vsub(a.objs[o].mesh.verts[v].pos,b.objs[o].mesh.verts[v].pos))==0);
			if(o){
				SURFACE_ASSERT(fabsf(hi.y)<SURFACE_TEST_EPSILON);
				SURFACE_ASSERT(a.objs[o].color.x>=0.375f&&a.objs[o].color.x<=0.625f);
				varied|=fabsf(a.objs[o].color.x-a.objs[1].color.x)>SURFACE_TEST_EPSILON;
			}
		}
		SURFACE_ASSERT(varied);
		snprintf(xml,sizeof(xml),"<scene><floor style='%s' width='137' depth='113' tileWidth='30' gap='1' color='0.5 0.4 0.3' colorVariation='0' seed='42'/></scene>",styles[style]);
		SURFACE_ASSERT(window_test_load(&c,xml)); ASSERT_EQUAL(a.nobjs,c.nobjs);
		for(int o=1;o<c.nobjs;o++){
			SURFACE_ASSERT(c.objs[o].color.x==0.5f);
			ASSERT_EQUAL(a.objs[o].mesh.nverts,c.objs[o].mesh.nverts);
			for(int v=0;v<a.objs[o].mesh.nverts;v++) SURFACE_ASSERT(vlen(vsub(a.objs[o].mesh.verts[v].pos,c.objs[o].mesh.verts[v].pos))==0);
		}
		scene_free(&a); scene_free(&b); scene_free(&c);
	}
	PASS();
}

static void test_floor_coverage_and_limits(void){
	TEST("floors fill zero-gap bounds, transform as one object, and bound invalid or excessive generation");
	const char *styles[]={"boards","squares","hexes"}; Scene s={0};
	for(int i=0;i<(int)(sizeof(styles)/sizeof(styles[0]));i++){
		char xml[SURFACE_TEST_CAPACITY]; snprintf(xml,sizeof(xml),"<scene up='z'><floor style='%s' width='137' depth='113' tileWidth='30' gap='0' pos='100 200 300' rot='90 0 0'/></scene>",styles[i]);
		SURFACE_ASSERT(window_test_load(&s,xml)); float volume=0;
		for(int o=0;o<s.nobjs;o++) volume+=mesh_signed_volume(&s.objs[o].mesh);
		SURFACE_ASSERT(fabsf(volume-1.37f*1.13f*0.18f)<SURFACE_TEST_EPSILON);
		vec3 lo,hi; scene_get_bounds(&s,&lo,&hi);
		SURFACE_ASSERT(fabsf(hi.z-3)<SURFACE_TEST_EPSILON&&fabsf(lo.z-2.82f)<SURFACE_TEST_EPSILON);
		scene_free(&s);
	}
	const char *invalid[]={"style='unknown'","width='0'","depth='nan'","tileWidth='0.00001'","gap='-1'",
		"tileDepth='18'","colorVariation='1.1'","colorVariation='nan'","style='squares' tileLength='40'","gap='11'"};
	for(int i=0;i<(int)(sizeof(invalid)/sizeof(invalid[0]));i++){
		char xml[SURFACE_TEST_CAPACITY]; snprintf(xml,sizeof(xml),"<scene><floor %s/></scene>",invalid[i]);
		SURFACE_ASSERT(window_test_load(&s,xml)); ASSERT_EQUAL(s.nobjs,0); scene_free(&s);
	}
	SURFACE_ASSERT(window_test_load(&s,"<scene><floor width='1' depth='1' colorVariation='0' castShadow='0' renderable='0' unlit='1'/></scene>"));
	SURFACE_ASSERT(s.nobjs>1);
	for(int o=0;o<s.nobjs;o++){ ASSERT_FALSE(s.objs[o].castsShadow); ASSERT_FALSE(s.objs[o].renderable); SURFACE_ASSERT(s.objs[o].unlit); }
	scene_free(&s); PASS();
}

static void test_surface_materials_and_roundtrip(void){
	TEST("surface material precedence and save/reload retain all generated parts");
	Scene s={0},restored={0};
	SURFACE_ASSERT(window_test_load(&s,"<scene><material id='paint' color='0.2 0.3 0.4' shininess='23'/>"
		"<wall lowerHeight='100' lowerMaterial='paint' lowerColor='0.1 0.2 0.3' upperMaterial='paint' trimMaterial='paint' bottomTrimHeight='10' bottomTrimColor='0.6 0.5 0.4'/>"
		"<floor style='squares' width='40' depth='40' material='paint' colorVariation='0' groutMaterial='paint' groutColor='0.7 0.6 0.5'/></scene>"));
	SURFACE_ASSERT(s.objs[0].shininess==23&&s.objs[0].color.x==0.1f);
	SURFACE_ASSERT(s.objs[1].shininess==23&&s.objs[1].color.x==0.2f);
	SURFACE_ASSERT(s.objs[2].shininess==23&&s.objs[2].color.x==0.6f);
	SURFACE_ASSERT(s.objs[3].shininess==23&&s.objs[3].color.x==0.7f);
	SURFACE_ASSERT(s.objs[4].shininess==23&&s.objs[4].color.x==0.2f);
	SURFACE_ASSERT(scene_save_all(&s)); SURFACE_ASSERT(load_scene(s.scenePath,&restored));
	ASSERT_EQUAL(s.nobjs,restored.nobjs);
	for(int o=0;o<s.nobjs;o++){
		SURFACE_ASSERT(vlen(vsub(s.objs[o].color,restored.objs[o].color))==0);
		ASSERT_EQUAL(s.objs[o].mesh.nverts,restored.objs[o].mesh.nverts);
		for(int v=0;v<s.objs[o].mesh.nverts;v++) SURFACE_ASSERT(vlen(vsub(s.objs[o].mesh.verts[v].pos,restored.objs[o].mesh.verts[v].pos))==0);
	}
	unlink(s.scenePath); scene_free(&s); scene_free(&restored); PASS();
}

int main(void) {
  TEST_START("scener input and command state");
  test_tool_commands_share_document_state();
  test_prefab_files_open_as_documents();
  test_nested_arch_emits_wall_parts_once();
  test_explicit_scene_up_axis();
  test_scene_coordinate_conventions();
  test_window_profiles();
  test_wall_sections_and_trims();
  test_wall_surface_transforms_and_invalid();
  test_procedural_floors();
  test_floor_coverage_and_limits();
  test_surface_materials_and_roundtrip();
  test_long_camera_names();
  test_door_fit_swing_and_pet_opening();
  test_door_windows();
  test_window_wall_cutting();
  test_window_cut_transforms_and_union();
  test_window_invalid_and_opt_out();
  test_window_creation_persists();
  test_window_drag_recuts_wall();
  test_window_picking_through_wall();
  TEST_END();
}
