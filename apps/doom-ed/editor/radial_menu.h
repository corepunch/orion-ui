#ifndef RADIAL_MENU_H
#define RADIAL_MENU_H

#include <mapview/gl_compat.h>
#include <stdbool.h>

// Initialize the radial menu system
bool init_radial_menu(void);

// Draw a single radial segment with the specified texture
void draw_radial(GLuint texture, float center_x, float center_y,
                 float radius_min, float radius_max,
                 float angle_min, float angle_max,
                 float alpha, bool highlight);

// Draw a complete radial menu with multiple segments
void draw_radial_menu(GLuint* textures, int texture_count, float center_x, float center_y,
                      float inner_radius, float outer_radius, int selected_index);

// Calculate which segment is selected based on cursor position
int get_selected_segment(float cursor_x, float cursor_y, float center_x, float center_y, int segment_count);

// Clean up radial menu resources
void cleanup_radial_menu(void);

#endif // RADIAL_MENU_H
