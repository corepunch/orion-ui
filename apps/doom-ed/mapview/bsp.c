#include <mapview/gl_compat.h>
#include <mapview/map.h>

// draw_bsp
// Main entry point for rendering.
// Uses portal-based rendering to traverse connected sectors.
//
void draw_bsp(map_data_t const *map, viewdef_t const *viewdef) {
  if (!map || !viewdef)
    return;
  
  extern void draw_floors(map_data_t const *, mapsector_t const *, viewdef_t const *);
  
  // Find the player's sector and start rendering from there
  mapsector_t const *player_sector = NULL;
  for (int i = 0; i < map->num_sectors; i++) {
    if (point_in_sector(map, viewdef->viewpos[0], viewdef->viewpos[1], i)) {
      player_sector = &map->sectors[i];
      break;
    }
  }
  
  // Fall back to rendering from first sector if player sector not found
  if (!player_sector && map->num_sectors > 0) {
    player_sector = &map->sectors[0];
  }
  
  if (player_sector) {
    draw_floors(map, player_sector, viewdef);
  }
}
