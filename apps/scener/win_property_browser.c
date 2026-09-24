#include "scener.h"
#include <orion/commctl/commctl.h>

enum { PROP_NAME_WIDTH = 104 };
typedef enum { PROP_TEXT, PROP_BOOL, PROP_X, PROP_Y, PROP_Z } prop_kind_t;
typedef struct {
	const char *label, *attr, *fallback, *default_value;
	prop_kind_t kind;
} prop_def_t;
typedef struct {
	const char *tag, *label;
	const prop_def_t *properties;
	int count, common, material;
} node_class_t;
typedef struct { window_t *list_win; } prop_browser_state_t;

#define PROP_COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define P(label,attr,def)       { label, attr, NULL, def, PROP_TEXT }
#define PF(label,attr,alt,def)  { label, attr, alt, def, PROP_TEXT }
#define PB(label,attr,def)      { label, attr, NULL, def, PROP_BOOL }
#define PV(label,attr,def,part) { label, attr, NULL, def, part }

static const prop_def_t kTransformProperties[] = {
	P("Name", "name", ""),
	PV("X", "pos", "0 0 0", PROP_X), PV("Y", "pos", "0 0 0", PROP_Y), PV("Z", "pos", "0 0 0", PROP_Z),
	PV("Rotation X", "rot", "0 0 0", PROP_X), PV("Rotation Y", "rot", "0 0 0", PROP_Y), PV("Rotation Z", "rot", "0 0 0", PROP_Z),
	PV("Scale X", "scale", "1 1 1", PROP_X), PV("Scale Y", "scale", "1 1 1", PROP_Y), PV("Scale Z", "scale", "1 1 1", PROP_Z),
	PV("Pivot X", "pivotOffset", "0 0 0", PROP_X), PV("Pivot Y", "pivotOffset", "0 0 0", PROP_Y), PV("Pivot Z", "pivotOffset", "0 0 0", PROP_Z),
};
static const prop_def_t kMaterialProperties[] = {
	P("Material", "material", ""),
	PV("Color R", "color", "0.8 0.8 0.8", PROP_X), PV("Color G", "color", "0.8 0.8 0.8", PROP_Y), PV("Color B", "color", "0.8 0.8 0.8", PROP_Z),
	P("Shininess", "shininess", "8"), PB("Cast Shadow", "castShadow", "1"),
	PB("Renderable", "renderable", "1"), PB("Unlit", "unlit", "0"),
};
static const prop_def_t kBoxProperties[] = {
	PV("Width", "size", "100 100 100", PROP_X), PV("Height", "size", "100 100 100", PROP_Y), PV("Depth", "size", "100 100 100", PROP_Z),
	PV("Inset X", "inset", "0 0 0", PROP_X), PV("Inset Y", "inset", "0 0 0", PROP_Y),
};
static const prop_def_t kSphereProperties[] = { P("Radius", "radius", "50"), P("Rings", "rings", "16"), P("Slices", "slices", "24") };
static const prop_def_t kCylinderProperties[] = { P("Radius", "radius", "50"), P("Height", "height", "100"), P("Tube", "tube", "0"), P("Sides", "sides", "24") };
static const prop_def_t kPrismProperties[] = { P("Radius", "radius", "50"), P("Height", "height", "100"), P("Sides", "sides", "6") };
static const prop_def_t kConeProperties[] = { P("Base Radius", "radius", "50"), P("Top Radius", "radiusTop", "0"), P("Height", "height", "100"), P("Sides", "sides", "24") };
static const prop_def_t kPyramidProperties[] = { P("Base Radius", "radius", "50"), P("Top Radius", "radiusTop", "0"), P("Height", "height", "100"), P("Sides", "sides", "4") };
static const prop_def_t kTorusProperties[] = { P("Major Radius", "majorRadius", "50"), P("Minor Radius", "minorRadius", "15"), P("Major Segments", "majorSegments", "24"), P("Minor Segments", "minorSegments", "12") };
static const prop_def_t kArchProperties[] = { P("Width", "width", "100"), P("Height", "height", "150"), P("Depth", "depth", "20"), PF("Thickness", "tube", "thickness", "0"), P("Inset", "inset", "0"), P("Segments", "segments", "16") };
static const prop_def_t kWindowProperties[] = {
	P("Preset", "preset", "round-arch"), P("Style", "style", "plain"),
	P("Width", "width", "120"), P("Height", "height", "preset"), P("Frame Width", "frameWidth", "automatic"), P("Depth", "depth", "automatic"),
	PF("Frame Material", "frameMaterial", "material", "wood"), P("Glass Material", "glassMaterial", "glass"), PB("Pane", "pane", "1"),
	P("Pane Depth", "paneDepth", "2"), P("Pane Offset", "paneOffset", "0"), PB("Cut Walls", "cutWalls", "1"), P("Cut Depth", "cutDepth", "frame depth"),
	P("Sill", "sill", "preset"), P("Sill Height", "sillHeight", "frame width"), P("Sill Projection", "sillProjection", "frame width"), P("Segments", "segments", "32"),
};
static const prop_def_t kCapsuleProperties[] = { P("Radius", "radius", "50"), P("Height", "height", "100"), P("Rings", "rings", "12"), P("Slices", "slices", "24") };
static const prop_def_t kWallProperties[] = { P("Length", "length", "400"), P("Height", "height", "270"), P("Thickness", "thickness", "20") };
static const prop_def_t kLightProperties[] = {
	P("Intensity", "intensity", "1"), P("Attenuation Radius", "radius", "0"), PB("Cast Shadows", "castShadows", "1"),
	PV("Color R", "color", "1 1 1", PROP_X), PV("Color G", "color", "1 1 1", PROP_Y), PV("Color B", "color", "1 1 1", PROP_Z),
};
static const prop_def_t kPrefabProperties[] = { P("Source", "source", ""), P("Attach", "attach", ""), PB("Tint", "tint", "0") };
static const prop_def_t kLineProperties[] = {
	PV("Start X", "start", "0 0 0", PROP_X), PV("Start Y", "start", "0 0 0", PROP_Y), PV("Start Z", "start", "0 0 0", PROP_Z),
	PV("End X", "end", "0 100 0", PROP_X), PV("End Y", "end", "0 100 0", PROP_Y), PV("End Z", "end", "0 100 0", PROP_Z), P("Camera", "camera", ""),
};
static const prop_def_t kDummyProperties[] = { P("Type", "type", "character"), P("Reference", "ref", ""), P("Pose", "pose", "stand"), P("Radius", "radius", "15"), P("Height", "height", "50"), P("FOV", "fov", "60"), P("Camera", "camera", "") };
static const prop_def_t kLatheProperties[] = { P("Shape", "shape", ""), P("Segments", "segments", "24") };
static const prop_def_t kLoftProperties[] = { P("Path Shape", "pathShape", ""), P("Cross Shape", "crossShape", ""), P("Segments", "segments", ""), PB("Closed", "closed", "0") };
static const prop_def_t kCameraProperties[] = { P("Name", "name", "Camera1"), P("Comment", "comment", ""), P("Position", "pos", ""), P("Look At", "look", ""), P("FOV", "fov", "60") };
static const prop_def_t kSunProperties[] = { P("Direction", "dir", "1 -1 0"), P("Color", "color", "1 1 1"), P("Intensity", "intensity", "1"), PB("Cast Shadows", "castShadows", "1") };
static const prop_def_t kNegativeBoxProperties[] = { P("Size", "size", "100 100 100") };
static const prop_def_t kNegativeArchProperties[] = { P("Width", "width", "100"), P("Height", "height", "150"), P("Depth", "depth", "20") };
static const prop_def_t kNegativeCylinderProperties[] = { P("Radius", "radius", "50"), PF("Depth", "depth", "size_z", "30") };

static const node_class_t kNodeClasses[] = {
	{ "window", "Window", kWindowProperties, PROP_COUNT(kWindowProperties), 1, 1 },
	{ "box", "Box", kBoxProperties, PROP_COUNT(kBoxProperties), 1, 1 }, { "sphere", "Sphere", kSphereProperties, PROP_COUNT(kSphereProperties), 1, 1 },
	{ "cylinder", "Cylinder", kCylinderProperties, PROP_COUNT(kCylinderProperties), 1, 1 }, { "prism", "Prism", kPrismProperties, PROP_COUNT(kPrismProperties), 1, 1 },
	{ "cone", "Cone", kConeProperties, PROP_COUNT(kConeProperties), 1, 1 }, { "pyramid", "Pyramid", kPyramidProperties, PROP_COUNT(kPyramidProperties), 1, 1 },
	{ "torus", "Torus", kTorusProperties, PROP_COUNT(kTorusProperties), 1, 1 }, { "arch", "Arch", kArchProperties, PROP_COUNT(kArchProperties), 1, 1 },
	{ "capsule", "Capsule", kCapsuleProperties, PROP_COUNT(kCapsuleProperties), 1, 1 }, { "wall", "Wall", kWallProperties, PROP_COUNT(kWallProperties), 1, 1 },
	{ "group", "Group", NULL, 0, 1, 0 }, { "prefab", "Prefab", kPrefabProperties, PROP_COUNT(kPrefabProperties), 1, 0 },
	{ "light", "Point Light", kLightProperties, PROP_COUNT(kLightProperties), 1, 0 }, { "line", "Line", kLineProperties, PROP_COUNT(kLineProperties), 1, 0 },
	{ "dummy", "Helper", kDummyProperties, PROP_COUNT(kDummyProperties), 1, 0 }, { "lathe", "Lathe", kLatheProperties, PROP_COUNT(kLatheProperties), 1, 1 },
	{ "loft", "Loft", kLoftProperties, PROP_COUNT(kLoftProperties), 1, 1 }, { "camera", "Camera", kCameraProperties, PROP_COUNT(kCameraProperties), 0, 0 },
	{ "sun", "Directional Light", kSunProperties, PROP_COUNT(kSunProperties), 0, 0 },
	{ "bool-negative-box", "Negative Box", kNegativeBoxProperties, PROP_COUNT(kNegativeBoxProperties), 1, 0 },
	{ "bool-negative-arch", "Negative Arch", kNegativeArchProperties, PROP_COUNT(kNegativeArchProperties), 1, 0 },
	{ "bool-negative-cylinder", "Negative Cylinder", kNegativeCylinderProperties, PROP_COUNT(kNegativeCylinderProperties), 1, 0 },
};

static void prop_add_row(window_t *list,const char *name,const char *value){
	const char *subs[1]={value?value:""};
	reportview_item_t item={.text=name?name:"",.color=get_sys_color(brTextNormal),.subitems={subs[0]},.subitem_count=1};
	send_message(list,RVM_ADDITEM,0,&item);
}

static const node_class_t *prop_find_class(const char *tag){
	for(int i=0;i<PROP_COUNT(kNodeClasses);i++) if(!strcmp(kNodeClasses[i].tag,tag)) return &kNodeClasses[i];
	return NULL;
}

static const char *prop_value(const void *node,const prop_def_t *prop,char *value,size_t size){
	const char *raw=scene_node_attr(node,prop->attr);
	if(!raw&&prop->fallback) raw=scene_node_attr(node,prop->fallback);
	if(!raw) raw=prop->default_value?prop->default_value:"";
	if(prop->kind==PROP_BOOL){ snprintf(value,size,"%s",atoi(raw)?"Yes":"No"); return value; }
	if(prop->kind>=PROP_X){
		float v[3]={0,0,0};
		sscanf(raw,"%f %f %f",&v[0],&v[1],&v[2]);
		snprintf(value,size,"%.6g",v[prop->kind-PROP_X]);
		return value;
	}
	return raw;
}

static int prop_attr_is_known(const node_class_t *class_def,const char *name){
	const prop_def_t *sets[3]={kTransformProperties,kMaterialProperties,class_def?class_def->properties:NULL};
	int counts[3]={PROP_COUNT(kTransformProperties),PROP_COUNT(kMaterialProperties),class_def?class_def->count:0};
	for(int s=0;s<3;s++) for(int i=0;i<counts[s];i++)
		if(!strcmp(sets[s][i].attr,name)||(sets[s][i].fallback&&!strcmp(sets[s][i].fallback,name))) return 1;
	return 0;
}

static void prop_add_set(window_t *list,const void *node,const prop_def_t *properties,int count){
	char value[128];
	for(int i=0;i<count;i++) prop_add_row(list,properties[i].label,prop_value(node,&properties[i],value,sizeof(value)));
}

void property_browser_refresh(void){
	scener_rig_panel_refresh();
	window_t *win=g_app?g_app->property_browser_win:NULL;
	prop_browser_state_t *st=win?(prop_browser_state_t*)win->userdata:NULL;
	if(!st||!st->list_win) return;
	send_message(st->list_win,RVM_CLEAR,0,NULL);
	scene_doc_t *doc=g_app->active_doc;
	if(!doc){ prop_add_row(st->list_win,"Status","No active document"); return; }
	void *node=doc->scene.selectedNode;
	if(!node){ prop_add_row(st->list_win,"Status","No node selected"); return; }
	if(doc->scene.selectedRigInstance==node && doc->scene.selectedRigJoint){
		void *joint=doc->scene.selectedRigJoint;
		char value[128];
		prop_add_row(st->list_win,"Character",scene_node_attr(node,"name"));
		prop_add_row(st->list_win,"Joint",scene_node_attr(joint,"name"));
		prop_add_row(st->list_win,"Pair",scene_node_attr(joint,"pair"));
		vec3 pivot=scene_rig_joint_value(&doc->scene,node,joint,"pivot");
		snprintf(value,sizeof(value),"%.3g %.3g %.3g cm",pivot.x*100,pivot.y*100,pivot.z*100); prop_add_row(st->list_win,"Pivot",value);
		vec3 rot=scene_rig_joint_value(&doc->scene,node,joint,"rot");
		snprintf(value,sizeof(value),"%.3g %.3g %.3g deg",rot.x,rot.y,rot.z); prop_add_row(st->list_win,"Local pose rotation",value);
		vec3 offset=scene_rig_joint_value(&doc->scene,node,joint,"pos");
		snprintf(value,sizeof(value),"%.3g %.3g %.3g cm",offset.x*100,offset.y*100,offset.z*100); prop_add_row(st->list_win,"Local pose offset",value);
		const char *instance_name=scene_node_attr(node,"name");
		for(int i=0;i<doc->scene.nrigTargets;i++) if(!strcmp(doc->scene.rigTargets[i].instance,instance_name?instance_name:"") &&
			!strcmp(doc->scene.rigTargets[i].joint,scene_node_attr(joint,"name"))){
			snprintf(value,sizeof(value),doc->scene.rigTargets[i].reachable?"Reachable":"Out of reach by %.2f cm",doc->scene.rigTargets[i].error*100);
			prop_add_row(st->list_win,"IK target",value);
		}
		prop_add_row(st->list_win,"Edit","Hierarchy tab controls");
		prop_add_row(st->list_win,"Guide","Pivot at articulation; inspect bent seams");
		return;
	}
	const char *tag=scene_node_tag(node);
	const node_class_t *class_def=prop_find_class(tag);
	prop_add_row(st->list_win,"Class",class_def?class_def->label:tag);
	if(class_def&&class_def->common){ prop_add_row(st->list_win,"Transform",""); prop_add_set(st->list_win,node,kTransformProperties,PROP_COUNT(kTransformProperties)); }
	if(class_def&&class_def->count){ prop_add_row(st->list_win,"Parameters",""); prop_add_set(st->list_win,node,class_def->properties,class_def->count); }
	if(class_def&&class_def->material){ prop_add_row(st->list_win,"Material",""); prop_add_set(st->list_win,node,kMaterialProperties,PROP_COUNT(kMaterialProperties)); }
	int added_other=0;
	for(int i=0;i<scene_node_attr_count(node);i++){
		const char *name=scene_node_attr_name(node,i);
		if(!name||prop_attr_is_known(class_def,name)) continue;
		if(!added_other){ prop_add_row(st->list_win,"Other",""); added_other=1; }
		prop_add_row(st->list_win,name,scene_node_attr_value(node,i));
	}
}

window_t *create_property_browser_window(void){
	if(!g_app) return NULL;
	int sw=ui_get_system_metrics(kSystemMetricScreenWidth),sh=ui_get_system_metrics(kSystemMetricScreenHeight);
	int total_h=sh-MENUBAR_HEIGHT-TOOLBAR_BAND_HEIGHT-40;
	int cmd_h=(int)(total_h*0.60f);
	window_t *win=create_window("Properties",WINDOW_ALWAYSONTOP|WINDOW_NOTRAYBUTTON|WINDOW_NORESIZE,
		MAKERECT(sw-SIDE_PANEL_WIDTH,MENUBAR_HEIGHT+TOOLBAR_BAND_HEIGHT+cmd_h,SIDE_PANEL_WIDTH,total_h-cmd_h),
		NULL,win_property_browser,g_app->hinstance,NULL);
	if(win) show_window(win,true);
	return win;
}

result_t win_property_browser(window_t *win,uint32_t msg,uint32_t wparam,void *lparam){
	(void)wparam; (void)lparam;
	prop_browser_state_t *st=(prop_browser_state_t*)win->userdata;
	switch(msg){
		case evCreate:
			st=allocate_window_data(win,sizeof(*st));
			st->list_win=create_window("",WINDOW_NOTITLE|WINDOW_NOFILL|WINDOW_VSCROLL,MAKERECT(0,0,win->frame.w,win->frame.h),win,win_reportview,0,NULL);
			if(st->list_win){
				send_message(st->list_win,RVM_SETVIEWMODE,RVM_VIEW_REPORT,NULL);
				send_message(st->list_win,RVM_SETCOLUMNTITLESVISIBLE,1,NULL);
				reportview_column_t name={"Property",PROP_NAME_WIDTH},value={"Value",0};
				send_message(st->list_win,RVM_ADDCOLUMN,0,&name); send_message(st->list_win,RVM_ADDCOLUMN,0,&value);
			}
			return true;
		case evResize:
			if(st&&st->list_win) resize_window(st->list_win,win->frame.w,win->frame.h);
			return true;
		case evDestroy:
			if(g_app) g_app->property_browser_win=NULL;
			return false;
		default: return false;
	}
}
