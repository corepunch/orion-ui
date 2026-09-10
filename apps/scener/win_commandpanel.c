#include "scener.h"
#include <orion/kernel/renderer.h>
#include <orion/user/draw.h>
#include <orion/user/bmp_icon_loader.h>

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
	const cp_datasource_t *datasource;
} cp_state_t;

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
			if (HIWORD(wparam) == tcnSelChange) return true;
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
