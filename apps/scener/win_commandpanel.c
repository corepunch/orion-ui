#include "scener.h"
#include <orion/kernel/renderer.h>
#include <orion/user/draw.h>
#include <orion/user/bmp_icon_loader.h>
#include <orion/commctl/commctl.h>

enum {
	CP_WIDTH = SIDE_PANEL_WIDTH,
	CP_BUTTON_HEIGHT = 20,
};

typedef void (*cp_action_fn)(void *context, uint16_t id);
typedef struct {
	const char *label;
	uint16_t id;
	const char *icon;
	cp_action_fn action;
	void *context;
} cp_command_t;
typedef struct { const char *label; const cp_command_t *commands; int count; } cp_section_t;
typedef struct { const char *label; uint16_t id; const char *icon; const cp_section_t *sections; int count; } cp_tab_t;
typedef struct { const cp_tab_t *tabs; int count; } cp_datasource_t;
typedef struct {
	bitmap_strip_t strip;
	window_t *tabview;
	window_t *rig_panel;
	const cp_datasource_t *datasource;
} cp_state_t;

enum { RIG_EDIT_ROT=6000, RIG_EDIT_OFFSET, RIG_EDIT_PIVOT, RIG_MIRROR, RIG_REPARENT, RIG_IK_TARGET, RIG_IK_POLE, RIG_SAVE_POSE, RIG_ASSIGN_POSE, RIG_ASSIGN_SHOT,
	RIG_VALUE_X, RIG_VALUE_Y, RIG_VALUE_Z, RIG_VALUE_OK, RIG_VALUE_CANCEL, RIG_NAME_EDIT };
typedef struct { window_t *list; void *instance; } rig_panel_state_t;
typedef struct { scene_doc_t *doc; void *instance,*joint; const char *attribute; vec3 value; int accepted; } rig_value_state_t;
typedef struct { scene_doc_t *doc; void *instance; int mode,accepted; char name[64]; } rig_name_state_t;

static void rig_changed(scene_doc_t *doc){
	if(!doc) return;
	doc->modified=true; doc_update_title(doc); property_browser_refresh(); scener_sync_tool_ui();
}

static result_t rig_value_proc(window_t *win,uint32_t msg,uint32_t wparam,void *lparam){
	rig_value_state_t *st=(rig_value_state_t*)win->userdata;
	if(msg==evCreate){
		st=(rig_value_state_t*)lparam; win->userdata=st;
		const char *labels[3]={"X","Y","Z"}; float values[3]={st->value.x,st->value.y,st->value.z};
		for(int i=0;i<3;i++){
			create_window(labels[i],WINDOW_NOTITLE,MAKERECT(16,18+i*31,35,24),win,"Label",0,NULL);
			window_t *edit=create_window("",WINDOW_NOTITLE,MAKERECT(55,18+i*31,205,24),win,"TextEdit",0,NULL);
			if(edit){ char value[48]; snprintf(value,sizeof(value),"%.6g",values[i]); edit->id=RIG_VALUE_X+i; send_message(edit,edSetText,0,value); }
		}
		window_t *ok=create_window("Apply",WINDOW_NOTITLE,MAKERECT(90,120,80,25),win,"Button",0,NULL);
		window_t *cancel=create_window("Cancel",WINDOW_NOTITLE,MAKERECT(180,120,80,25),win,"Button",0,NULL);
		if(ok) ok->id=RIG_VALUE_OK; if(cancel) cancel->id=RIG_VALUE_CANCEL;
		return true;
	}
	if(msg==evCommand && HIWORD(wparam)==btnClicked){
		if(LOWORD(wparam)==RIG_VALUE_CANCEL){ end_dialog(win,0); return true; }
		if(LOWORD(wparam)==RIG_VALUE_OK){
			float result[3];
			for(int i=0;i<3;i++){
				char value[64]={0}; window_t *edit=get_window_item(win,RIG_VALUE_X+i);
				if(edit) send_message(edit,edGetText,sizeof(value),value);
				char *end=NULL; result[i]=strtof(value,&end);
				if(!end || end==value || *end || !isfinite(result[i])){ message_box(win,"Enter three finite numbers.","Joint transform",MB_OK); return true; }
			}
			st->value=v3(result[0],result[1],result[2]); st->accepted=1; end_dialog(win,1); return true;
		}
	}
	return false;
}

static result_t rig_name_proc(window_t *win,uint32_t msg,uint32_t wparam,void *lparam){
	rig_name_state_t *st=(rig_name_state_t*)win->userdata;
	if(msg==evCreate){
		st=(rig_name_state_t*)lparam; win->userdata=st;
		create_window(st->mode==RIG_REPARENT?"New parent joint":"Pose name",WINDOW_NOTITLE,MAKERECT(16,20,150,24),win,"Label",0,NULL);
		window_t *edit=create_window("",WINDOW_NOTITLE,MAKERECT(16,48,250,24),win,"TextEdit",0,NULL);
		if(edit){ edit->id=RIG_NAME_EDIT; send_message(edit,edSetText,0,st->name); }
		window_t *ok=create_window("Apply",WINDOW_NOTITLE,MAKERECT(90,90,80,25),win,"Button",0,NULL);
		window_t *cancel=create_window("Cancel",WINDOW_NOTITLE,MAKERECT(180,90,80,25),win,"Button",0,NULL);
		if(ok) ok->id=RIG_VALUE_OK; if(cancel) cancel->id=RIG_VALUE_CANCEL;
		return true;
	}
	if(msg==evCommand && HIWORD(wparam)==btnClicked){
		if(LOWORD(wparam)==RIG_VALUE_CANCEL){ end_dialog(win,0); return true; }
		if(LOWORD(wparam)==RIG_VALUE_OK){
			window_t *edit=get_window_item(win,RIG_NAME_EDIT);
			if(edit) send_message(edit,edGetText,sizeof(st->name),st->name);
			if(!st->name[0]){ message_box(win,"Enter a pose name.","Pose",MB_OK); return true; }
			st->accepted=1; end_dialog(win,1); return true;
		}
	}
	return false;
}

void scener_rig_panel_refresh(void){
	window_t *panel=g_app&&g_app->command_panel_win?
		((cp_state_t*)g_app->command_panel_win->userdata)->rig_panel:NULL;
	rig_panel_state_t *st=panel?(rig_panel_state_t*)panel->userdata:NULL;
	if(!st || !st->list) return;
	send_message(st->list,RVM_CLEAR,0,NULL); st->instance=NULL;
	scene_doc_t *doc=g_app->active_doc;
	if(!doc) return;
	Scene *s=&doc->scene;
	void *instance=s->selectedRigInstance?s->selectedRigInstance:s->selectedNode;
	int count=scene_rig_joint_count(s,instance);
	if(!count) return;
	st->instance=instance;
	const char *name=scene_node_attr(instance,"name");
	reportview_item_t header={.text=name?name:"Character",.color=get_sys_color(brTextNormal)};
	send_message(st->list,RVM_ADDITEM,0,&header);
	for(int i=0;i<count;i++){
		int depth=0; void *joint=scene_rig_joint_at(s,instance,i,&depth);
		char label[96]; snprintf(label,sizeof(label),"%*s%s",depth*2,"",scene_node_attr(joint,"name"));
		reportview_item_t item={.text=label,.color=get_sys_color(brTextNormal)};
		send_message(st->list,RVM_ADDITEM,0,&item);
		if(s->selectedRigInstance==instance && s->selectedRigJoint==joint)
			send_message(st->list,RVM_SETSELECTION,i+1,NULL);
	}
}

static result_t rig_panel_proc(window_t *win,uint32_t msg,uint32_t wparam,void *lparam){
	rig_panel_state_t *st=(rig_panel_state_t*)win->userdata;
	if(msg==evCreate){
		st=allocate_window_data(win,sizeof(*st));
		st->list=create_window("",WINDOW_NOTITLE|WINDOW_VSCROLL,MAKERECT(4,4,222,180),win,win_reportview,0,NULL);
		if(st->list) send_message(st->list,RVM_SETVIEWMODE,RVM_VIEW_ICON,NULL);
		const char *labels[]={"Rotation","Offset","Pivot","Mirror","Parent","IK Target","IK Pole","Save Pose","Use Pose","Shot Pose"};
		for(int i=0;i<10;i++){
			window_t *button=create_window(labels[i],WINDOW_NOTITLE,MAKERECT(4+(i%2)*112,190+(i/2)*25,108,22),win,"Button",0,NULL);
			if(button) button->id=RIG_EDIT_ROT+i;
		}
		return true;
	}
	if((msg==evResize || msg==evArrange) && st){
		int w=win->frame.w,h=win->frame.h,listH=h-165;
		if(listH<40) listH=40;
		if(st->list){ move_window(st->list,4,4); resize_window(st->list,w-8,listH); }
		for(int i=0;i<10;i++){
			window_t *button=get_window_item(win,RIG_EDIT_ROT+i);
			if(button){ move_window(button,4+(i%2)*(w/2),listH+8+(i/2)*25); resize_window(button,w/2-8,22); }
		}
		return true;
	}
	if(msg==evPaint){
		fill_rect(get_sys_color(brControlBg),get_client_rect(win));
		for(window_t *child=win->children;child;child=child->next) send_message(child,evPaint,0,NULL);
		return true;
	}
	if(msg==evCommand && st && g_app && g_app->active_doc){
		scene_doc_t *doc=g_app->active_doc; Scene *s=&doc->scene;
		if((window_t*)lparam==st->list && HIWORD(wparam)==RVN_SELCHANGE){
			int index=(int)send_message(st->list,RVM_GETSELECTION,0,NULL)-1;
			void *joint=scene_rig_joint_at(s,st->instance,index,NULL);
			if(joint){ scene_rig_select_joint(s,st->instance,joint); property_browser_refresh(); if(doc->viewport_win) invalidate_window(doc->viewport_win); }
			return true;
		}
		if(HIWORD(wparam)!=btnClicked) return false;
		int action=LOWORD(wparam);
		if(action<RIG_EDIT_ROT || action>RIG_ASSIGN_SHOT || !st->instance) return false;
		fprintf(stderr,"[scener] rig control win=%u action=%d instance=%s\n",(unsigned)win->id,action,
			scene_node_attr(st->instance,"name")?scene_node_attr(st->instance,"name"):""); fflush(stderr);
		void *joint=s->selectedRigInstance==st->instance?s->selectedRigJoint:NULL;
		if(action==RIG_MIRROR){ if(joint && scene_rig_mirror_joint(s,st->instance,joint)) rig_changed(doc); return true; }
		if(action==RIG_REPARENT){
			if(!joint) return true;
			rig_name_state_t edit={.doc=doc,.instance=st->instance,.mode=action};
			show_dialog("Reparent joint",285,130,win,rig_name_proc,&edit);
			if(edit.accepted){
				if(scene_rig_reparent_joint(s,st->instance,joint,edit.name)) rig_changed(doc);
				else message_box(win,"Choose another joint in the same rig. Scaled joints cannot be reparented.","Reparent",MB_OK);
			}
			return true;
		}
		if(action<=RIG_EDIT_PIVOT || action==RIG_IK_TARGET || action==RIG_IK_POLE){
			if(!joint) return true;
			const char *attribute=action==RIG_EDIT_ROT?"rot":action==RIG_EDIT_OFFSET?"pos":action==RIG_EDIT_PIVOT?"pivot":action==RIG_IK_TARGET?"target":"pole";
			vec3 value=action>=RIG_IK_TARGET?scene_rig_target_value(s,st->instance,joint,attribute):scene_rig_joint_value(s,st->instance,joint,attribute);
			int centimetres=action==RIG_EDIT_OFFSET || action==RIG_EDIT_PIVOT || action==RIG_IK_TARGET;
			if(centimetres) value=vscale(value,100);
			rig_value_state_t edit={doc,st->instance,joint,attribute,value,0};
			show_dialog(centimetres?"Joint position (cm)":"Joint rotation / pole",280,160,win,rig_value_proc,&edit);
			if(edit.accepted){
				if(centimetres) edit.value=vscale(edit.value,0.01f);
				int ok=action>=RIG_IK_TARGET?scene_rig_set_target(s,st->instance,joint,attribute,edit.value):
					scene_rig_set_joint(s,st->instance,joint,attribute,edit.value);
				if(ok) rig_changed(doc); else message_box(win,"Select the tip of a three-joint chain.","IK",MB_OK);
			}
			return true;
		}
		rig_name_state_t edit={.doc=doc,.instance=st->instance,.mode=action};
		show_dialog(action==RIG_SAVE_POSE?"Save pose":"Apply pose",285,130,win,rig_name_proc,&edit);
		if(edit.accepted){
			int ok=action==RIG_SAVE_POSE?scene_rig_save_pose(s,st->instance,edit.name):
				scene_rig_assign_pose(s,st->instance,edit.name,action==RIG_ASSIGN_SHOT);
			if(ok) rig_changed(doc); else message_box(win,"Pose not found.","Pose",MB_OK);
		}
		return true;
	}
	return false;
}

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void cp_menu_action(void *context, uint16_t id) {
	(void)context;
	handle_menu_command(id);
}

#define CP_MENU_COMMAND(label, id, icon) { label, id, icon, cp_menu_action, NULL }

static const cp_command_t kControlItems[] = {
	CP_MENU_COMMAND("Select",  ID_TOOL_SELECT, "tools/select"),
	CP_MENU_COMMAND("Move",    ID_TOOL_MOVE,   "tools/move"),
	CP_MENU_COMMAND("Rotate",  ID_TOOL_ROTATE, "tools/rotate"),
	CP_MENU_COMMAND("Scale",   ID_TOOL_SCALE,  "tools/scale"),
};

static const cp_command_t kShapeItems[] = {
	CP_MENU_COMMAND("Box",      ID_CREATE_BOX,      "primitives/box"),
	CP_MENU_COMMAND("Rect", ID_CREATE_RECT, "primitives/box"),
	CP_MENU_COMMAND("Rounded Rect", ID_CREATE_ROUNDED_RECT, "primitives/box"),
	CP_MENU_COMMAND("Circle", ID_CREATE_CIRCLE, "primitives/sphere"),
	CP_MENU_COMMAND("Ellipse", ID_CREATE_ELLIPSE, "primitives/sphere"),
	CP_MENU_COMMAND("Star", ID_CREATE_STAR, "primitives/prism"),
	CP_MENU_COMMAND("Screen", ID_CREATE_SCREEN, "primitives/box"),
	CP_MENU_COMMAND("Sphere",   ID_CREATE_SPHERE,   "primitives/sphere"),
	CP_MENU_COMMAND("Cylinder", ID_CREATE_CYLINDER, "primitives/cylinder"),
	CP_MENU_COMMAND("Cone",     ID_CREATE_CONE,     "primitives/cone"),
	CP_MENU_COMMAND("Torus",    ID_CREATE_TORUS,    "primitives/torus"),
	CP_MENU_COMMAND("Prism",    ID_CREATE_PRISM,    "primitives/prism"),
	CP_MENU_COMMAND("Capsule",  ID_CREATE_CAPSULE,  "primitives/capsule"),
	CP_MENU_COMMAND("Arch",     ID_CREATE_ARCH,     "primitives/arch"),
	CP_MENU_COMMAND("Round Window", ID_CREATE_WINDOW_ROUND, "primitives/arch"),
	CP_MENU_COMMAND("Cottage Window", ID_CREATE_WINDOW_COTTAGE, "primitives/box"),
	CP_MENU_COMMAND("Gothic Window", ID_CREATE_WINDOW_GOTHIC, "primitives/arch"),
	CP_MENU_COMMAND("Rectangular Door", ID_CREATE_DOOR_RECTANGULAR, "primitives/box"),
	CP_MENU_COMMAND("Round Door", ID_CREATE_DOOR_ROUND, "primitives/arch"),
	CP_MENU_COMMAND("Gothic Door", ID_CREATE_DOOR_GOTHIC, "primitives/arch"),
};

static const cp_command_t kSceneItems[] = {
	CP_MENU_COMMAND("Point Light", ID_CREATE_POINT_LIGHT,       "scene/point-light"),
	CP_MENU_COMMAND("Directional", ID_CREATE_DIRECTIONAL_LIGHT, "scene/directional-light"),
	CP_MENU_COMMAND("Camera",      ID_CREATE_CAMERA,            "scene/camera"),
};

static const cp_command_t kModifierItems[] = {
	CP_MENU_COMMAND("Taper",   ID_MODIFY_TAPER,   "modifiers/taper"),
	CP_MENU_COMMAND("Twist",   ID_MODIFY_TWIST,   "modifiers/twist"),
	CP_MENU_COMMAND("Bend",    ID_MODIFY_BEND,    "modifiers/bend"),
	CP_MENU_COMMAND("Stretch", ID_MODIFY_STRETCH, "modifiers/stretch"),
	CP_MENU_COMMAND("Skew",    ID_MODIFY_SKEW,    "modifiers/skew"),
	CP_MENU_COMMAND("Extrude", ID_MODIFY_EXTRUDE, "modifiers/extrude"),
	CP_MENU_COMMAND("Mirror",  ID_MODIFY_MIRROR,  "modifiers/mirror"),
	CP_MENU_COMMAND("Noise",   ID_MODIFY_NOISE,   "modifiers/noise"),
	CP_MENU_COMMAND("Shell",   ID_MODIFY_SHELL,   "modifiers/shell"),
	CP_MENU_COMMAND("Array",   ID_MODIFY_ARRAY,   "modifiers/array"),
};

static const cp_section_t kCreateSections[] = {
	{ "Controls",           kControlItems,  COUNT_OF(kControlItems)  },
	{ "Shapes",             kShapeItems,    COUNT_OF(kShapeItems)    },
	{ "Lights and Cameras", kSceneItems,    COUNT_OF(kSceneItems)    },
};

static const cp_section_t kModifySections[] = {
	{ "Modifiers", kModifierItems, COUNT_OF(kModifierItems) },
};

#define CP_ICON_CREATE    "tabs/create"
#define CP_ICON_MODIFY    "tabs/modify"
#define CP_ICON_HIERARCHY "tabs/hierarchy"
#define CP_ICON_MOTION    "tabs/motion"
#define CP_ICON_DISPLAY   "tabs/display"
#define CP_ICON_UTILITIES "tabs/utilities"

/* Tab icon names match kTabs[] order — indices 0..5 used with tcSetTabIcon. */
static const char *kTabIconNames[] = {
	CP_ICON_CREATE, CP_ICON_MODIFY, CP_ICON_HIERARCHY,
	CP_ICON_MOTION, CP_ICON_DISPLAY, CP_ICON_UTILITIES,
};

static const cp_tab_t kTabs[] = {
	{ "Create",    ID_CP_TAB_CREATE,    CP_ICON_CREATE,    kCreateSections, COUNT_OF(kCreateSections) },
	{ "Modify",    ID_CP_TAB_MODIFY,    CP_ICON_MODIFY,    kModifySections, COUNT_OF(kModifySections) },
	{ "Hierarchy", ID_CP_TAB_HIERARCHY, CP_ICON_HIERARCHY, NULL, 0 },
	{ "Motion",    ID_CP_TAB_MOTION,    CP_ICON_MOTION,    NULL, 0 },
	{ "Display",   ID_CP_TAB_DISPLAY,   CP_ICON_DISPLAY,   NULL, 0 },
	{ "Utilities", ID_CP_TAB_UTILITIES, CP_ICON_UTILITIES, NULL, 0 },
};

static const cp_datasource_t kCommandPanelDataSource = {
	.tabs = kTabs, .count = COUNT_OF(kTabs),
};

static bool cp_build_tab_strip(bitmap_strip_t *strip) {
	char icons_dir[1024];
	int n = snprintf(icons_dir, sizeof(icons_dir), "%s/../share/scener/icons", ui_get_exe_dir());
	if (n <= 0 || (size_t)n >= sizeof(icons_dir)) return false;
	int count = COUNT_OF(kTabIconNames);
	return bmp_build_strip(icons_dir, kTabIconNames, count, 24, count, strip, NULL);
}

static window_t *cp_create_page(window_t *tabview, const cp_tab_t *tab) {
	if(tab->id==ID_CP_TAB_HIERARCHY)
		return create_window("Rig Hierarchy",WINDOW_NOTITLE,MAKERECT(0,0,1,1),tabview,rig_panel_proc,0,NULL);
	window_t *page = create_window(tab->label, WINDOW_NOTITLE,
		MAKERECT(0, 0, 1, 1), tabview, "StackView", 0, NULL);
	if (!page) return NULL;
	page->layout.layout_spacing = 4;
	for (int s = 0; s < tab->count; s++) {
		const cp_section_t *section = &tab->sections[s];
		window_t *section_win = create_window("", WINDOW_NOTITLE,
			MAKERECT(0, 0, 1, 1), page, "StackView", 0, NULL);
		if (!section_win) continue;
		section_win->layout.layout_spacing = 4;
		create_window(section->label, WINDOW_NOTITLE,
			MAKERECT(0, 0, 1, CONTROL_HEIGHT), section_win, "Label", 0, NULL);
		window_t *grid = create_window("", WINDOW_NOTITLE,
			MAKERECT(0, 0, 1, 1), section_win, "GridView", 0, NULL);
		if (!grid) continue;
		grid->layout.layout_spacing = 4;
		send_message(grid, evInitChildren, 0, NULL);
		window_t *columns[2] = { grid->children, grid->children ? grid->children->next : NULL };
		for (int i = 0; i < section->count; i++) {
			window_t *column = columns[i % 2];
			if (!column) continue;
			const cp_command_t *command = &section->commands[i];
			window_t *button = create_window(command->label,
				WINDOW_NOTITLE, MAKERECT(0, 0, 1, CP_BUTTON_HEIGHT),
				column, "Button", 0, NULL);
			if (button) button->id = command->id;
		}
	}
	if (!tab->count)
		create_window("No controls available", WINDOW_NOTITLE,
			MAKERECT(0, 0, 1, CONTROL_HEIGHT), page, "Label", 0, NULL);
	return page;
}

static const cp_command_t *cp_find_command(const cp_datasource_t *source, uint16_t id) {
	if (!source) return NULL;
	for (int t = 0; t < source->count; t++)
		for (int s = 0; s < source->tabs[t].count; s++)
			for (int i = 0; i < source->tabs[t].sections[s].count; i++) {
				const cp_command_t *command = &source->tabs[t].sections[s].commands[i];
				if (command->id == id) return command;
			}
	return NULL;
}

result_t win_command_panel(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
	(void)lparam;
	cp_state_t *st = (cp_state_t *)win->userdata;
	switch (msg) {
		case evCreate: {
			st = calloc(1, sizeof(*st));
			if (!st) return false;
			win->userdata = st;
			st->datasource = &kCommandPanelDataSource;
			bool has_strip = cp_build_tab_strip(&st->strip);
			st->tabview = create_window("", WINDOW_NOTITLE | WINDOW_NORESIZE,
				MAKERECT(0, 0, 1, 1), win, "TabView", 0, NULL);
			if (!st->tabview) return false;
			for (int i = 0; i < st->datasource->count; i++) {
				window_t *page = cp_create_page(st->tabview, &st->datasource->tabs[i]);
				if(i==2) st->rig_panel=page;
				if (page) show_window(page, true);
			}
			send_message(st->tabview, tcSetStyle, TAB_STYLE_ICONS_ONLY, NULL);
			if (has_strip) {
				send_message(st->tabview, tcSetImageStrip, 0, &st->strip);
				for (int i = 0; i < st->datasource->count; i++)
					send_message(st->tabview, tcSetTabIcon, i, (void*)(intptr_t)i);
			}
			irect16_t cr = get_client_rect(win);
			layout_arrange_t a = {R(0, 0, cr.w, cr.h)};
			send_message(st->tabview, evArrange, 0, &a);
			show_window(st->tabview, true);
			scener_rig_panel_refresh();
			return true;
		}
		case evPaint: {
			if (!st) return false;
			irect16_t cr = get_client_rect(win);
			fill_rect(get_sys_color(brControlBg), cr);
			for (window_t *c = win->children; c; c = c->next)
				send_message(c, evPaint, 0, NULL);
			return true;
		}
		case evResize: {
			if (!st) return false;
			irect16_t cr = get_client_rect(win);
			for (window_t *c = win->children; c; c = c->next) {
				layout_arrange_t a = {R(0, 0, cr.w, cr.h)};
				send_message(c, evArrange, 0, &a);
			}
			return true;
		}
		case evCommand: {
			if (HIWORD(wparam) == tcnSelChange){ fprintf(stderr,"[scener] command tab selection=%u\n",(unsigned)wparam); fflush(stderr); return true; }
			uint16_t id = LOWORD(wparam);
			for (int i = 0; st && i < st->datasource->count; i++) if (st->datasource->tabs[i].id == id) {
				if (st && st->tabview) send_message(st->tabview, tcSetSelection, i, NULL);
				return true;
			}
			const cp_command_t *command = cp_find_command(st ? st->datasource : NULL, id);
			if (command && command->action) {
				command->action(command->context, command->id);
				return true;
			}
			return false;
		}
		case evDestroy:
			if (st) {
				R_DeleteTexture(st->strip.tex);
				free(st);
				win->userdata = NULL;
			}
			if (g_app) g_app->command_panel_win = NULL;
			return false;
		default:
			return false;
	}
}

window_t *create_command_panel_window(void) {
	if (!g_app) return NULL;
	int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
	int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
	int win_h = (int)((sh - MENUBAR_HEIGHT - TOOLBAR_BAND_HEIGHT - 40) * 0.60f);
	window_t *win = create_window("Command Panel",
		WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
		MAKERECT(sw - CP_WIDTH, MENUBAR_HEIGHT + TOOLBAR_BAND_HEIGHT, CP_WIDTH, win_h),
		NULL, win_command_panel, g_app->hinstance, NULL);
	if (win) show_window(win, true);
	return win;
}
