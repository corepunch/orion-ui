#ifndef __MAP__
#define __MAP__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <cglm/cglm.h>

#include <orion/ui.h>

#include "../debug.h"

#define OFFSET_OF(type, field) (void*)((size_t)&(((type *)0)->field))

// Macro to define a collection of elements (pointer and count)
#define DEFINE_COLLECTION(type, name) \
type* name;                   \
int num_##name

// Macro to free a collection and reset its values
#define CLEAR_COLLECTION(map, name) \
if ((map)->name) free((map)->name); \
(map)->name = NULL;               \
(map)->num_##name = 0

// Helper macros for min/max if not already defined
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define EYE_HEIGHT 48 // Typical eye height in Doom is 41 units above floor
#define MAX_WALL_VERTICES 50000  // Adjust based on map complexity
#define P_RADIUS 12.0f        // Player radius
#define PALETTE_WIDTH 24
#define NOTEX_SIZE 64
#define SPRITE_SCALE 2
#define SCALE_POINT(x) ((x)/2)
#define HEXEN
#define HIGHLIGHT(light) ((light)+0.25)
#define CONSOLE_PADDING 2
#define LINE_HEIGHT 8
#define VGA_WIDTH 320
#define VGA_HEGHT 200

#define PLAYER_FOV 90

#define THUMBNAIL_SIZE 64

#define sensitivity_x 0.125f // Adjust sensitivity as needed
#define sensitivity_y 0.175f // Adjust sensitivity as needed


enum {
  PIXEL_MID = 0 << 28,
  PIXEL_BOTTOM = 1 << 28,
  PIXEL_TOP = 2 << 28,
  PIXEL_FLOOR = 3 << 28,
  PIXEL_CEILING = 4 << 28,
  PIXEL_MASK = 7 << 28,
};

#define CHECK_PIXEL(PIXEL, TYPE, ID) ((PIXEL & PIXEL_MASK) == PIXEL_##TYPE && (PIXEL & ~PIXEL_MASK) == ID)


// Constants for rendering
#define FOV 90.0
#define NEAR_Z 0.1
#define FAR_Z 1000.0
//#define MOVEMENT_SPEED 300.0
#define ROTATION_SPEED 3.0

#define ACCELERATION 1000.0f
#define FRICTION     1200.0f
#define MAX_SPEED    300.0f

// Lump order in a map WAD: each map needs a couple of lumps
// to provide a complete scene geometry description.
enum
{
  ML_LABEL,    // A separator, name, ExMx or MAPxx
  ML_THINGS,    // Monsters, items..
  ML_LINEDEFS,    // LineDefs, from editing
  ML_SIDEDEFS,    // SideDefs, from editing
  ML_VERTEXES,    // Vertices, edited and BSP splits generated
  ML_SEGS,    // LineSegs, from LineDefs split by BSP
  ML_SSECTORS,    // SubSectors, list of LineSegs
  ML_NODES,    // BSP nodes
  ML_SECTORS,    // Sectors, from editing
  ML_REJECT,    // LUT, sector-sector visibility
  ML_BLOCKMAP    // LUT, motion clipping, walls/grid element
};

// Type definitions to better represent the WAD format
typedef char wadid_t[4];     // "IWAD" or "PWAD"
typedef char lumpname_t[8];  // Lump name, null-terminated
typedef char texname_t[8];   // Texture name, null-terminated

// Player state
typedef struct player_s {
  float x, y, z;
  float angle;  // in degrees, 0 is east, 90 is north
  float pitch;
  float height;
  float vel_x, vel_y; // Player velocity
  int sector;
  int mouse_x_rel;
  int mouse_y_rel;
  float forward_move;
  float strafe_move;
//  float points[2][2];
//  float points2[2][2];
} player_t;

// Vertex structure for our buffer (xyzuv)
typedef struct {
  int16_t x, y, z;    // Position
  int16_t u, v;       // Texture coordinates
  int8_t nx, ny, nz;  // Normal
  int32_t color;
} wall_vertex_t;

// Struct to represent a texture with OpenGL
typedef struct {
  texname_t name;
  uint32_t texture;
  uint16_t width;
  uint16_t height;
} mapside_texture_t;

// Helper struct for tracking wall sections
typedef struct {
  uint32_t vertex_start;  // Starting index in the vertex buffer
  uint32_t vertex_count;  // Count of vertices
  mapside_texture_t const *texture;  // Texture to use
} wall_section_t;

// WAD header structure
typedef struct {
  wadid_t identification;     // Should be "IWAD" or "PWAD"
  uint32_t numlumps;           // Number of lumps in the WAD
  uint32_t infotableofs;       // Pointer to the location of the directory
} wadheader_t;

// WAD directory entry structure
typedef struct {
  uint32_t filepos;           // File offset of the lump
  uint32_t size;              // Size of the lump in bytes
  lumpname_t name;           // Name of the lump, null-terminated
} filelump_t;

// Patch header structure
typedef struct {
  int16_t width;              // Width of the patch
  int16_t height;             // Height of the patch
  int16_t leftoffset;         // Left offset of the patch
  int16_t topoffset;          // Top offset of the patch
  int32_t columnofs[1];       // Column offsets (variable size)
} patch_t;

#ifdef HEXEN
// Thing structure
typedef struct {
  int16_t tid;
  int16_t x;
  int16_t y;
  int16_t height;
  int16_t angle;
  int16_t type;
  int16_t options;
  int8_t special;
  int8_t args[5];
} mapthing_t;
// Linedef structure
typedef struct {
  uint16_t start;             // Start vertex
  uint16_t end;               // End vertex
  uint16_t flags;             // Flags
  uint8_t special;            // Special type
  uint8_t args[5];            // Arguments for special (e.g., script number, delay)
  uint16_t sidenum[2];        // Sidedef numbers
} maplinedef_t;
#else
// Thing structure
typedef struct {
  int16_t x;                 // X position
  int16_t y;                 // Y position
  int16_t angle;             // Angle facing
  int16_t type;              // Thing type
  int16_t flags;             // Flags
} mapthing_t;
// Linedef structure
typedef struct {
  uint16_t start;             // Start vertex
  uint16_t end;               // End vertex
  uint16_t flags;             // Flags
  uint16_t special;           // Special type
  uint16_t tag;               // Tag number
  uint16_t sidenum[2];        // Sidedef numbers
} maplinedef_t;
#endif

// Vertex structure
typedef struct {
  int16_t x;                 // X coordinate
  int16_t y;                 // Y coordinate
} mapvertex_t;

// Sidedef structure
typedef struct {
  int16_t textureoffset;     // X offset for texture
  int16_t rowoffset;         // Y offset for texture
  texname_t toptexture;      // Name of upper texture
  texname_t bottomtexture;   // Name of lower texture
  texname_t midtexture;      // Name of middle texture
  uint16_t sector;           // Sector number this sidedef faces
} mapsidedef_t;

// Bounding box coordinates (following DOOM convention)
enum {
  BOXTOP,
  BOXBOTTOM,
  BOXLEFT,
  BOXRIGHT
};

// Sector structure
typedef struct {
  int16_t floorheight;       // Floor height
  int16_t ceilingheight;     // Ceiling height
  texname_t floorpic;        // Name of floor texture
  texname_t ceilingpic;      // Name of ceiling texture
  int16_t lightlevel;        // Light level
  int16_t special;           // Special behavior
  int16_t tag;               // Tag number
} mapsector_t;

// Wall section references
typedef struct {
  mapsidedef_t *def;
  mapsector_t *sector;
  wall_section_t upper_section;
  wall_section_t lower_section;
  wall_section_t mid_section;
//  uint32_t frame;
} mapsidedef2_t;

typedef struct {
  mapsector_t *sector;
  wall_section_t floor;
  wall_section_t ceiling;
  uint32_t frame;
  int16_t  bbox[4];
} mapsector2_t;

// Palette structure
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} palette_entry_t;

typedef struct {
  char const *name;
  uint16_t width;
  uint16_t height;
} texdef_t;

extern palette_entry_t *palette;

// Map data structure using the collection macro
typedef struct {
  DEFINE_COLLECTION(mapvertex_t, vertices);
  DEFINE_COLLECTION(maplinedef_t, linedefs);
  DEFINE_COLLECTION(mapsidedef_t, sidedefs);
  DEFINE_COLLECTION(mapthing_t, things);
  DEFINE_COLLECTION(mapsector_t, sectors);

  struct {
    mapsidedef2_t *sections;
    wall_vertex_t vertices[MAX_WALL_VERTICES];
    uint32_t num_vertices;
    uint32_t vao, vbo;
  } walls;
  
  struct {
    mapsector2_t *sectors;
    wall_vertex_t vertices[MAX_WALL_VERTICES];
    uint32_t num_vertices;
    uint32_t vao, vbo;
  } floors;
} map_data_t;

typedef struct {
  mat4 mvp;
  vec4 frustum[6];
  vec3 viewpos;
  player_t player;
  uint32_t frame;
  uint16_t portal;
  uint32_t time;
} viewdef_t;

// Collection to store loaded textures
typedef struct texture_cache_s {
  int num_textures;
  texname_t selected;
  mapside_texture_t textures[1];
} texture_cache_t;

typedef enum {
  EditModeSelect,
  EditModeVertices,
  EditModeLines,
  EditModeSectors,
  EditModeThings,
  EditModeSounds,
  EditModeCount
} editmode_t;

typedef enum {
  ObjTypeNone,
  ObjTypePoint,
  ObjTypeLine,
  ObjTypeSector,
  ObjTypeThing,
} objtype_t;

typedef struct {
  objtype_t type;
  uint16_t index;
} editor_selection_t;

#define has_selection(s, t) (s.type == t && s.index != 0xFFFF)

typedef struct {
  struct window_s *window;
  int16_t cursor[2];
  vec2 camera;
  int grid_size;         // Grid size (8 units by default)
  bool drawing;          // Currently drawing a sector?
  bool dragging;          // Currently dragging a vertex?
  int move_camera;
  int move_thing;
  int num_draw_points;   // Number of points in current sector
  editor_selection_t hover, selected;
  int sel_mode;
  int selected_thing_type;
  float scale;
  uint32_t vao, vbo;
  mapvertex_t sn;
} editor_state_t;

typedef struct {
  int episode;
  int level;
  uint32_t last_time;
  map_data_t map;
  player_t player;
  editor_state_t state;
} game_t;


#define LABEL_WIDTH 54
#define WIDTH_FILL -1
#define THING_SIZE 48

struct window_s;

void clear_window_children(window_t *win);
void load_window_children(window_t *win, windef_t const *def);
void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void invalidate_window(window_t *win);
void set_window_item_text(window_t *win, uint32_t id, const char *fmt, ...);
window_t *get_window_item(window_t const *win, uint32_t id);

void track_mouse(window_t *win);
void set_capture(window_t *win);
void set_focus(window_t* win);
void enable_window(window_t *win, bool enable);

int get_message(ui_event_t *evt);
void dispatch_message(ui_event_t *evt);
void repost_messages(void);
bool is_window(window_t *win);
void end_dialog(window_t *win, uint32_t code);
uint32_t show_dialog(char const *, int, int, struct window_s *, winproc_t, void *param);

extern window_t *g_inspector;
extern game_t *g_game;

bool init_resources(void);
int run(void);

filelump_t *find_lump(const char* name);
void *cache_lump(const char* name);
void *cache_lump_num(uint16_t i);
int find_lump_num(const char* name);
char const *get_lump_name(int i);

int allocate_mapside_textures(void);
int allocate_flat_textures(void);

uint8_t* load_patch(void* patch_lump, int* width, int* height);
void compute_sector_bbox(map_data_t *map, int sector_index);
void compute_all_sector_bboxes(map_data_t *map);
bool point_in_sector(map_data_t const* map, int x, int y, int sector_index);
void assign_thing_sector(map_data_t const *map, mapthing_t *thing);
mapsector_t const *find_player_sector(map_data_t const* map, int x, int y);
mapside_texture_t const *get_texture(const char* name);
mapside_texture_t const *get_flat_texture(const char* name);
void build_wall_vertex_buffer(map_data_t *map);
void build_floor_vertex_buffer(map_data_t *map);
void draw_textured_surface(wall_section_t const *surface, float light, int mode);
void draw_textured_surface_id(wall_section_t const *surface, uint32_t id, int mode);
void draw_bsp(map_data_t const *map, viewdef_t const *viewdef);

void update_player_position_with_sliding(map_data_t const *map, player_t *player,
                                         float move_x, float move_y);

void fill_rect(uint32_t color, irect16_t r);
void draw_rect(int tex, irect16_t r);
void draw_rect_ex(int tex, irect16_t r, int type, float alpha);
void draw_icon8(int icon, int x, int y, uint32_t col);
void draw_icon16(int icon, int x, int y, uint32_t col);
void draw_palette(map_data_t const *map);
char const* get_texture_name(int i);
char const* get_flat_texture_name(int i);
int get_texture_index(char const* name);
int get_flat_texture_index(char const* name);
float dist_sq(float x1, float y1, float x2, float y2);

map_data_t load_map(const char* map_name);
void find_all_maps(void (*proc)(const char *, void *), void *parm);
void print_map_info(map_data_t* map);
void free_map_data(map_data_t* map);

void goto_intermisson(void);
void new_map(void);
void open_map(const char *mapname);

bool init_wad(const char *filename);
void shutdown_wad(void);

void handle_windows(void);

void GetMouseInVirtualCoords(int* vx, int* vy);
extern int g_mouse_x, g_mouse_y;

const char *get_selected_texture(void);
void set_selected_texture(const char *);
const char *get_selected_flat_texture(void);
void set_selected_flat_texture(const char *str);

extern int world_prog_mvp;
extern int world_prog_viewPos;
extern int world_prog_tex0_size;
extern int world_prog_tex0;
extern int world_prog_light;

extern int ui_prog_mvp;
extern int ui_prog_tex0_size;
extern int ui_prog_tex0;
extern int ui_prog_color;


#endif
