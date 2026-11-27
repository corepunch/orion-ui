#include "scener.h"
#include <orion/user/dock.h>
#include <orion/user/draw.h>
#include <orion/commctl/commctl.h>

typedef struct { window_t *tabs, *rig_panel; } cp_state_t;

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
	cp_state_t *commands = g_app && g_app->command_panel_win ? g_app->command_panel_win->userdata : NULL;
	window_t *panel = commands ? commands->rig_panel : NULL;
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
		irect16_t client = get_client_rect(win);
		int w=client.w,listH=client.h-165;
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
	if (msg == evDestroy) return true;
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

static const segment_glyph_t command_panel_glyphs[] = {
	[SCENER_TAB_CREATE]    = {"scener-cube",               "scener-cube-fill"},
	[SCENER_TAB_MODIFY]    = {"scener-sliders-horizontal", "scener-sliders-horizontal-fill"},
	[SCENER_TAB_HIERARCHY] = {"scener-tree-structure",     "scener-tree-structure-fill"},
	[SCENER_TAB_MOTION]    = {"scener-play-circle",        "scener-play-circle-fill"},
	[SCENER_TAB_DISPLAY]   = {"scener-monitor",            "scener-monitor-fill"},
	[SCENER_TAB_RENDER]    = {"scener-camera",             "scener-camera-fill"},
	[SCENER_TAB_UTILITIES] = {"scener-wrench",             "scener-wrench-fill"},
};

void scener_select_panel_tab(int tab) {
	window_t *panel = g_app ? g_app->command_panel_win : NULL;
	cp_state_t *state = panel ? panel->userdata : NULL;
	if (state && state->tabs) send_message(state->tabs, tcSetSelection, tab, NULL);
}

void scener_inspect_selection(void) {
	window_t *panel = g_app ? g_app->command_panel_win : NULL;
	cp_state_t *state = panel ? panel->userdata : NULL;
	if (state && state->tabs && send_message(state->tabs, tcGetSelection, 0, NULL) == SCENER_TAB_CREATE)
		scener_select_panel_tab(SCENER_TAB_MODIFY);
}

void scener_sync_panel_checks(void) {
	window_t *panel = g_app ? g_app->command_panel_win : NULL;
	if (!panel) return;
	const struct { uint16_t id; bool checked; } checks[] = {
		{ID_COMMAND_PANEL_CAST_SHADOWS,      !(g_app->debug_flags & DBG_NO_SHADOWS)},
		{ID_COMMAND_PANEL_AMBIENT_OCCLUSION, (g_app->debug_flags & DBG_SSAO) != 0},
		{ID_COMMAND_PANEL_SCENE_GUIDES,     !(g_app->debug_flags & DBG_HIDE_LIGHTS)},
		{ID_COMMAND_PANEL_CHARACTER_GUIDES,    !(g_app->debug_flags & DBG_HIDE_CHARS)},
	};
	for (size_t i = 0; i < ARRAY_LEN(checks); i++) {
		window_t *control = get_window_item(panel, checks[i].id);
		uint32_t value = checks[i].checked ? btnStateChecked : btnStateUnchecked;
		if (control && send_message(control, btnGetCheck, 0, NULL) != value) {
			send_message(control, btnSetCheck, value, NULL);
			invalidate_window(control);
		}
	}
}

static result_t win_command_panel(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
	cp_state_t *state = win->userdata;
	switch (msg) {
		case evCreate: {
			state = allocate_window_data(win, sizeof(*state));
			if (!state) return false;
			state->tabs = get_window_item(win, ID_COMMAND_PANEL_COMMAND_TABS);
			if (!state->tabs) return false;
			for (size_t i = 0; i < ARRAY_LEN(command_panel_glyphs); i++) {
				segment_glyph_t glyph = command_panel_glyphs[i];
				send_message(state->tabs, tcSetPageGlyph, i, &glyph);
			}
			send_message(state->tabs, tcSetStyle, TAB_STYLE_ICONS_ONLY, NULL);
			window_t *hierarchy = get_window_item(win, ID_COMMAND_PANEL_HIERARCHY_PAGE);
			state->rig_panel = create_window("", WINDOW_NOTITLE | WINDOW_FLEXSPACE,
				MAKERECT(0, 0, 1, 1), hierarchy, rig_panel_proc, 0, NULL);
			property_browser_init(win);
			return true;
		}
		case evPaint:
			fill_rect(get_sys_color(brControlBg), get_client_rect(win));
			return false;
		case evCommand: {
			uint16_t id = LOWORD(wparam), code = HIWORD(wparam);
			if (code == tcnSelChange) return true;
			if (state && state->rig_panel &&
				((code == btnClicked && id >= RIG_EDIT_ROT && id <= RIG_ASSIGN_SHOT) ||
				 (code == RVN_SELCHANGE && lparam && ((window_t *)lparam)->parent == state->rig_panel)))
				return send_message(state->rig_panel, msg, wparam, lparam);
			if (code != btnClicked) return false;
			switch (id) {
				case ID_COMMAND_PANEL_CAST_SHADOWS:      id = ID_VIEW_SHOW_SHADOWS; break;
				case ID_COMMAND_PANEL_AMBIENT_OCCLUSION: id = ID_VIEW_TOGGLE_SSAO; break;
				case ID_COMMAND_PANEL_SCENE_GUIDES:
				case ID_COMMAND_PANEL_CHARACTER_GUIDES:
					g_app->debug_flags ^= id == ID_COMMAND_PANEL_SCENE_GUIDES ? DBG_HIDE_LIGHTS : DBG_HIDE_CHARS;
					scener_sync_tool_ui();
					return true;
			}
			if (id < ID_COMMAND_BASE || id > ID_HELP_ABOUT) return false;
			handle_menu_command(id);
			return true;
		}
		case evDestroy:
			if (g_app && g_app->command_panel_win == win) g_app->command_panel_win = NULL;
			return false;
		default:
			return false;
	}
}

window_t *create_command_panel_window(void) {
	if (!g_app || !g_app->chrome_win) return NULL;
#ifdef BUILD_AS_GEM
	int top = MENUBAR_HEIGHT + TOOLBAR_BAND_HEIGHT;
	int left = ui_get_system_metrics(kSystemMetricScreenWidth) - SIDE_PANEL_WIDTH;
	window_t *win = create_window_from_form(&scener_command_panel_form, left, top, NULL,
		win_command_panel, g_app->hinstance, NULL);
	if (win) resize_window(win, SIDE_PANEL_WIDTH, MAX(1, ui_get_system_metrics(kSystemMetricScreenHeight) - top));
#else
	window_t *win = create_window_from_form(&scener_command_panel_form, 0, 0, g_app->chrome_win,
		win_command_panel, g_app->hinstance, NULL);
	if (win && !dock_window(win, DOCK_RIGHT, DOCK_EDGE(DOCK_RIGHT), DOCK_NOFLOAT, SIDE_PANEL_WIDTH, SIDE_PANEL_WIDTH)) {
		destroy_window(win);
		return NULL;
	}
#endif
	if (win) show_window(win, true);
	return win;
}
