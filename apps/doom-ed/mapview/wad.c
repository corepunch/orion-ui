#include <mapview/map.h>
#include "../wad.h"

static doom_wad_t archive;
static struct {
  filelump_t *directory;
  int num_lumps;
  void **cache;
  FILE *file;
} wad;

void shutdown_wad(void) {
  for (int i = 0; i < wad.num_lumps; i++) free(wad.cache ? wad.cache[i] : NULL);
  free(wad.directory);
  free(wad.cache);
  memset(&wad, 0, sizeof(wad));
  doom_wad_close(&archive);
}

bool init_wad(const char *filename) {
  shutdown_wad();
  if (!doom_wad_open(&archive, filename) || archive.count > INT32_MAX) {
    shutdown_wad();
    return false;
  }
  wad.directory = calloc(archive.count, sizeof(*wad.directory));
  wad.cache = calloc(archive.count, sizeof(*wad.cache));
  if (archive.count && (!wad.directory || !wad.cache)) {
    shutdown_wad();
    return false;
  }
  wad.file = archive.file;
  wad.num_lumps = (int)archive.count;
  for (int i = 0; i < wad.num_lumps; i++) {
    wad.directory[i].filepos = archive.lumps[i].offset;
    wad.directory[i].size = archive.lumps[i].size;
    memcpy(wad.directory[i].name, archive.lumps[i].name, 8);
  }
  return true;
}

int find_lump_num(const char *name) {
  char key[9] = {0};
  if (!name) return -1;
  strncpy(key, name, 8);
  return (int)doom_wad_find(&archive, key);
}
filelump_t *find_lump(const char *name) {
  int index = find_lump_num(name);
  return index < 0 ? NULL : &wad.directory[index];
}
const char *get_lump_name(int i) {
  return i < 0 || i >= wad.num_lumps ? NULL : archive.lumps[i].name;
}
void *cache_lump_num(uint16_t i) {
  if (i >= wad.num_lumps) return NULL;
  if (wad.cache[i]) return wad.cache[i];
  void *data = malloc(wad.directory[i].size ? wad.directory[i].size : 1);
  if (!data || !doom_wad_read(&archive, i, data, wad.directory[i].size)) {
    free(data);
    return NULL;
  }
  return wad.cache[i] = data;
}
void *cache_lump(const char *name) {
  int index = find_lump_num(name);
  return index < 0 || index > UINT16_MAX ? NULL : cache_lump_num((uint16_t)index);
}

// Function to read data from a file at a specific offset
void* read_lump_data(FILE* file, filelump_t const *lump) {
  void* data = malloc(lump->size);
  if (!data) return NULL;
  
  if (fseek(file, lump->filepos, SEEK_SET) || fread(data, 1, lump->size, file) != lump->size) {
    free(data);
    return NULL;
  }
  return data;
}

bool is_map_block_valid(filelump_t* dir, int index, int total_lumps) {
  if (!dir || index < 0 || total_lumps - index <= 10) return false;
  static const char* expected[] = {
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES",
    "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"
  };
  for (int i = 0; i < 10; i++) {
    if (index + 1 + i >= total_lumps) return false;
    if (strncmp(dir[index + 1 + i].name, expected[i], 8) != 0) return false;
  }
  return true;
}

void find_all_maps(void (*proc)(const char *, void *), void *parm) {
  for (int i = 0; i < wad.num_lumps; i++) {
    if (is_map_block_valid(wad.directory, i, wad.num_lumps)) {
      proc(archive.lumps[i].name, parm);
    }
  }
}

// Function to load map data
map_data_t load_map(const char* map_name) {
  map_data_t map = {0};
  
  // Find the map marker lump
  int map_index = find_lump_num(map_name);
  if (map_index == -1) {
    fprintf(stderr, "Map %s not found!\n", map_name);
    return map;
  }

  // Map lumps follow a specific order after the map marker
  if (is_map_block_valid(wad.directory, map_index, wad.num_lumps)) {
    const int offsets[] = {ML_THINGS, ML_LINEDEFS, ML_SIDEDEFS, ML_VERTEXES, ML_SECTORS};
    const size_t strides[] = {sizeof(mapthing_t), sizeof(maplinedef_t), sizeof(mapsidedef_t), sizeof(mapvertex_t), sizeof(mapsector_t)};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); i++)
      if (wad.directory[map_index + offsets[i]].size % strides[i]) return map;
    // THINGS
    map.num_things = wad.directory[map_index + ML_THINGS].size / sizeof(mapthing_t);
    map.things = read_lump_data(wad.file, &wad.directory[map_index + ML_THINGS]);
    
    // LINEDEFS
    map.num_linedefs = wad.directory[map_index + ML_LINEDEFS].size / sizeof(maplinedef_t);
    map.linedefs = read_lump_data(wad.file, &wad.directory[map_index + ML_LINEDEFS]);
    
    // SIDEDEFS
    map.num_sidedefs = wad.directory[map_index + ML_SIDEDEFS].size / sizeof(mapsidedef_t);
    map.sidedefs = read_lump_data(wad.file, &wad.directory[map_index + ML_SIDEDEFS]);
    
    // VERTEXES
    map.num_vertices = wad.directory[map_index + ML_VERTEXES].size / sizeof(mapvertex_t);
    map.vertices = read_lump_data(wad.file, &wad.directory[map_index + ML_VERTEXES]);

    // SECTORS
    map.num_sectors = wad.directory[map_index + ML_SECTORS].size / sizeof(mapsector_t);
    map.sectors = read_lump_data(wad.file, &wad.directory[map_index + ML_SECTORS]);
  }
  
  if ((map.num_vertices && !map.vertices) || (map.num_linedefs && !map.linedefs) ||
      (map.num_sidedefs && !map.sidedefs) || (map.num_sectors && !map.sectors) || (map.num_things && !map.things)) {
    free_map_data(&map);
    return map;
  }
  for (int i = 0; i < map.num_linedefs; i++) {
    maplinedef_t *line = &map.linedefs[i];
    if (line->start >= map.num_vertices || line->end >= map.num_vertices ||
        (line->sidenum[0] != UINT16_MAX && line->sidenum[0] >= map.num_sidedefs) ||
        (line->sidenum[1] != UINT16_MAX && line->sidenum[1] >= map.num_sidedefs)) {
      free_map_data(&map);
      return map;
    }
  }
  for (int i = 0; i < map.num_sidedefs; i++) {
    if (map.sidedefs[i].sector >= map.num_sectors) {
      free_map_data(&map);
      return map;
    }
  }
  for (int i = 0; i < map.num_things; i++) {
    assign_thing_sector(&map, &map.things[i]);
  }
  
  return map;
}

// Function to free map data
void free_map_data(map_data_t* map) {
  if (map->walls.vao) R_GpuDeleteVertexArrays(1, &map->walls.vao);
  if (map->walls.vbo) R_GpuDeleteBuffers(1, &map->walls.vbo);
  if (map->floors.vao) R_GpuDeleteVertexArrays(1, &map->floors.vao);
  if (map->floors.vbo) R_GpuDeleteBuffers(1, &map->floors.vbo);
  free(map->walls.sections);
  free(map->floors.sectors);
  CLEAR_COLLECTION(map, vertices);
  CLEAR_COLLECTION(map, linedefs);
  CLEAR_COLLECTION(map, sidedefs);
  CLEAR_COLLECTION(map, things);
  CLEAR_COLLECTION(map, sectors);
  memset(map, 0, sizeof(map_data_t));
}

// Function to print basic map info
void print_map_info(map_data_t* map) {
  doom_ed_debug("Map info:\n");
  doom_ed_debug("  Vertices: %d\n", map->num_vertices);
  doom_ed_debug("  Linedefs: %d\n", map->num_linedefs);
  doom_ed_debug("  Sidedefs: %d\n", map->num_sidedefs);
  doom_ed_debug("  Things: %d\n", map->num_things);
  doom_ed_debug("  Sectors: %d\n", map->num_sectors);
  
  if (map->num_things > 0) {
    doom_ed_debug("\nPlayer start position:\n");
    // Find player start (thing type 1)
    for (int i = 0; i < map->num_things; i++) {
      if (map->things[i].type == 1) {
        doom_ed_debug("  Position: (%d, %d), Angle: %d\n",
               map->things[i].x, map->things[i].y, map->things[i].angle);
        break;
      }
    }
  }
}
