#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>
#include <orion/user/toolbar.h>
#include <orion/user/image.h>
#include <orion/user/color.h>
#include "test_env.h"
#include <string.h>

#if defined(__APPLE__) && !TARGET_OS_IOS
#include <OpenGL/OpenGL.h>

extern bool ui_init_prog(void);
extern void ui_shutdown_prog(void);
extern void init_ui_white_texture(void);
extern void shutdown_white_texture(void);

static uint32_t viewport_texture;

static result_t pane_surface_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == evPaint && win->role != WINDOW_ROLE_PAGE) fill_rect(get_sys_color(brTextSuccess), get_client_rect(win));
  return false;
}

static uint32_t pane_pixel(window_t *root, int x, int y) {
  uint8_t pixel[4];
  window_surface_t *surf = window_surface(root);
  R_BindWindowTarget(surf->fbo);
  glReadPixels(ui_surface_px(x), surf->h - ui_surface_px(y) - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
  return (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 | (uint32_t)pixel[2] << 16 | (uint32_t)pixel[3] << 24;
}

static void test_page_pane_surfaces(void) {
  TEST("Page panes clip nested content to tab corners and highlight both splitter pixels");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) { SKIP("Offscreen OpenGL unavailable"); }
  CGLError error = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (error != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  test_env_init();
  bool ok = ui_init_prog();
  if (ok) {
    init_ui_white_texture();
    set_theme(THEME_MODERN);
    static const form_ctrl_def_t panes[] = {
      {.class_name = "TabView", .id = 1, .flags = WINDOW_FLEXSPACE},
      {.class_name = "TabView", .id = 2, .flags = WINDOW_FLEXSPACE},
    };
    static const form_ctrl_def_t children[] = {
      {.class_name = "SplitView", .id = 3, .flags = WINDOW_FLEXSPACE,
       .children = panes, .child_count = 2, .lparam = (void *)SPLIT_VERT},
    };
    const form_def_t def = {.name = "", .width = 400, .height = 240,
      .flags = WINDOW_NOTITLE | WINDOW_AUTO_LAYOUT, .role = WINDOW_ROLE_PAGE,
      .children = children, .child_count = 1, .pane_toolbar_replace = true};
    window_t *root = create_window_from_form(&def, 30, 40, NULL, pane_surface_proc, 0, NULL);
    window_t *left = page_pane_at(root, 0);
    window_t *bottom = page_split_pane(root, left, PANE_BOTTOM);
    for (int i = 0; i < page_pane_count(root); i++) {
      window_t *pane = page_pane_at(root, i);
      create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), pane, pane_surface_proc, 0, NULL);
      send_message(pane, evResize, 0, NULL);
      ok &= page_is_pane(pane) && (send_message(pane, tcGetStyle, 0, NULL) & TAB_STYLE_PANE);
    }
    g_ui_runtime.running = true;
    send_message(root, evPaint, 0, NULL);
    uint32_t background = get_sys_color(brPanelDarker);
    for (int i = 0; i < page_pane_count(root); i++) {
      window_t *pane = page_pane_at(root, i);
      ipoint16_t p = window_map_point(pane, root, (ipoint16_t){0, 0});
      int w = pane->frame.w, h = pane->frame.h;
      ok &= pane_pixel(root, p.x - 1, p.y + h / 2) == background;
      ok &= pane_pixel(root, p.x + w, p.y + h / 2) == background;
      ok &= pane_pixel(root, p.x + w / 2, p.y - 1) == background;
      ok &= pane_pixel(root, p.x + w / 2, p.y + h) == background;
      ok &= pane_pixel(root, p.x, p.y) == background;
      ok &= pane_pixel(root, p.x + w - 1, p.y) == background;
      ok &= pane_pixel(root, p.x, p.y + h - 1) == background;
      ok &= pane_pixel(root, p.x + w - 1, p.y + h - 1) == background;
      ok &= pane_pixel(root, p.x + w / 2, p.y + h - 4) == get_sys_color(brTextSuccess);
      ok &= pane_pixel(root, p.x + 20, p.y) != background;
      ok &= pane_pixel(root, p.x + 20, p.y + 2) == get_sys_color(brControlBg);
    }
    create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), left, pane_surface_proc, 0, NULL);
    create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), left, pane_surface_proc, 0, NULL);
    send_message(left, evResize, 0, NULL);
    send_message(root, evPaint, 0, NULL);
    irect16_t tabs[3];
    for (int i = 0; i < 3; i++) send_message(left, tcGetTabRect, i, &tabs[i]);
    ok &= tabs[1].x == tabs[0].x + tabs[0].w && tabs[2].x == tabs[1].x + tabs[1].w;
    ipoint16_t origin = window_map_point(left, root, (ipoint16_t){0, 0});
    uint32_t bar = get_sys_color(brTabBarBg);
    for (int x = tabs[2].x - 2; x < tabs[2].x + 2; x++)
      ok &= pane_pixel(root, origin.x + x, origin.y + 12) == bar;
    ok &= pane_pixel(root, origin.x + tabs[1].x, origin.y + 12) == bar;
    ok &= pane_pixel(root, origin.x + tabs[1].x + 1, origin.y + 12) == bar;
    ok &= pane_pixel(root, origin.x + tabs[1].x + 4, origin.y + 12) != bar;
    ipoint16_t click = window_map_point(left, NULL, (ipoint16_t){tabs[1].x, 12});
    ui_event_t event = {.message = kEventLeftButtonDown, .x = click.x * UI_WINDOW_SCALE, .y = click.y * UI_WINDOW_SCALE};
    dispatch_message(&event);
    event.message = kEventLeftButtonUp; dispatch_message(&event);
    ok &= send_message(left, tcGetSelection, 0, NULL) == 1;
    send_message(left, tcSetSelection, 0, NULL);
    send_message(root, evPaint, 0, NULL);
    window_t *right = page_pane_at(root, 2);
    const toolbar_item_t action = {.type = TOOLBAR_ITEM_CUSTOM, .ident = 999};
    send_message(right, tcSetToolbar, 1, (void *)&action);
    window_t *actions = (window_t *)send_message(right, tcGetToolbar, 0, NULL);
    ipoint16_t action_point = window_map_point(right, root, (ipoint16_t){actions->frame.x + 4, actions->frame.y + 12});
    send_message(root, evPaint, 0, NULL);
    ok &= pane_pixel(root, action_point.x, action_point.y) == bar;
    send_message(actions, tbModifyStyle, TOOLBAR_STYLE_TRANSPARENT, NULL);
    send_message(root, evPaint, 0, NULL);
    ok &= pane_pixel(root, action_point.x, action_point.y) == get_sys_color(brPanelDarker);
    send_message(actions, tbModifyStyle, TOOLBAR_STYLE_TRANSPARENT, (void *)(uintptr_t)TOOLBAR_STYLE_TRANSPARENT);
    send_message(root, evPaint, 0, NULL);
    ok &= pane_pixel(root, action_point.x, action_point.y) == bar;
    window_t *splitters[] = {NULL, NULL};
    window_t *splits[] = {get_window_item(root, 3), bottom->parent};
    for (int i = 0; i < 2; i++) {
      for (window_t *c = splits[i]->children; c; c = c->next)
        if (c->proc == win_splitter) splitters[i] = c;
      window_t *splitter = splitters[i];
      ipoint16_t p = window_map_point(splitter, root, (ipoint16_t){splitter->frame.w / 2, splitter->frame.h / 2});
      ipoint16_t step = i ? (ipoint16_t){0, 1} : (ipoint16_t){1, 0};
      ok &= pane_pixel(root, p.x - step.x, p.y - step.y) == background;
      ok &= pane_pixel(root, p.x, p.y) == background;
      send_message(splitter, evMouseMove, 0, NULL);
      send_message(root, evPaint, 0, NULL);
      uint32_t hot = pane_pixel(root, p.x, p.y);
      ok &= hot != background && hot == pane_pixel(root, p.x - step.x, p.y - step.y);
      ok &= pane_pixel(root, p.x + step.x, p.y + step.y) == background;
      ok &= pane_pixel(root, p.x - 2 * step.x, p.y - 2 * step.y) == background;
      send_message(splitter, evMouseLeave, 0, NULL);
      send_message(root, evPaint, 0, NULL);
      ok &= pane_pixel(root, p.x, p.y) == background;
    }
    ok &= get_draw_surface(root) == root && glGetError() == GL_NO_ERROR;
    g_ui_runtime.running = false;
    destroy_window(root);
    shutdown_white_texture();
    ui_shutdown_prog();
  }
  test_env_shutdown();
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}
static void test_shader_state_restore(void) {
  TEST("Typed sprite state survives raw effects and lazy indexed shader creation");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) { SKIP("Offscreen OpenGL unavailable"); }
  CGLError error = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (error != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  bool initialized = ui_init_prog(), ok = initialized;
  uint32_t fbo = 0, surface = 0, white = 0, indexed = 0;
  int w = 0, h = 0;
  if (ok) {
    ok = R_EnsureWindowTarget(&fbo, &surface, &w, &h, 1, 1);
    white = R_CreateTextureRGBA(1, 1, (uint8_t[]){255, 255, 255, 255}, R_FILTER_NEAREST, R_WRAP_CLAMP);
    indexed = R_CreateTextureR8(1, 1, (uint8_t[]){1}, R_FILTER_NEAREST, R_WRAP_CLAMP);
    ok &= white && indexed;
  }
  if (ok) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 1, 1);
    glDisable(GL_SCISSOR_TEST);
    R_SetFramebufferSRGB(false);
    set_projection(0, 0, 1, 1);
    glClearColor(0, 0, 0, 0);
    uint8_t original[4], effect[4], restored[4], palette_pixel[4];
    glClear(GL_COLOR_BUFFER_BIT);
    draw_sprite_region(white, R(0, 0, 1, 1), NULL, 0x800000FF, 0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, original);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_rect_program_blend(white, 0, 0, 1, 1, 0.75f, UI_LAYER_BLEND_NORMAL, get_sprite_prog(), 0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, effect);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_sprite_region(white, R(0, 0, 1, 1), NULL, 0x800000FF, 0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, restored);
    uint32_t palette[256] = {0};
    palette[1] = 0xFFFF0000;
    glClear(GL_COLOR_BUFFER_BIT);
    draw_indexed_rect(indexed, R(0, 0, 1, 1), palette, 0, 1);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, palette_pixel);
    ok &= original[0] == 128 && original[1] == 0 && original[3] == 128;
    ok &= memcmp(original, effect, 4) != 0 && memcmp(original, restored, 4) == 0;
    ok &= palette_pixel[0] == 0 && palette_pixel[1] == 0 && palette_pixel[2] == 255 && palette_pixel[3] == 255;
    ok &= glGetError() == GL_NO_ERROR;
  }
  R_DeleteTexture(white);
  R_DeleteTexture(indexed);
  R_DestroyWindowTarget(&fbo, &surface, &w, &h);
  if (initialized) ui_shutdown_prog();
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}

static void read_card_pixel(int x, int y, uint8_t pixel[4]) {
  glReadPixels(x, 32 - y - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

static plastic_look_t look_for(ctrl_state_t state) {
  return (plastic_look_t){.pressed = !!(state & CTRL_PRESSED), .hover = !!(state & CTRL_HOVER),
    .selected = !!(state & CTRL_SELECTED), .disabled = !!(state & CTRL_DISABLED),
    .gloss = 1.0f, .rim = 0.55f, .ink = 0.10f, .lift = 0.10f};
}

static const plastic_look_t *look_ptr(ctrl_state_t state) {
  static plastic_look_t look;
  look = look_for(state);
  return &look;
}

static void test_plastic_surface(void) {
  TEST("Plastic states preserve bounds, disable interaction shading and letterpress the glyph");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) { SKIP("Offscreen OpenGL unavailable"); }
  CGLError error = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (error != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  bool initialized = ui_init_prog(), ok = initialized;
  uint32_t fbo = 0, texture = 0, glyph = 0;
  int w = 0, h = 0;
  if (ok) {
    ok = R_EnsureWindowTarget(&fbo, &texture, &w, &h, 32, 32);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 32, 32);
    glDisable(GL_SCISSOR_TEST);
    set_projection(0, 0, 32, 32);
    glClearColor(0, 0, 0, 0);
    uint8_t mask[16 * 16 * 4] = {0};
    for (int y = 4; y < 12; y++) for (int x = 4; x < 12; x++) mask[(y * 16 + x) * 4 + 3] = 255;
    glyph = R_CreateTextureRGBA(16, 16, mask, R_FILTER_LINEAR, R_WRAP_CLAMP);
    uint8_t samples[5][4], disabled_only[32 * 32 * 4], disabled_flags[32 * 32 * 4];
    ctrl_state_t states[] = {CTRL_NORMAL, CTRL_SELECTED, CTRL_PRESSED, CTRL_HOVER, CTRL_DISABLED};
    for (int i = 0; i < ARRAY_LEN(states); i++) {
      glClear(GL_COLOR_BUFFER_BIT);
      render_plastic_surface(R(2, 2, 28, 28), 10, 2, 3, look_ptr(states[i]), WEB(0x48aa36),
                              0x80000000, glyph, NULL, (ipoint16_t){16, 16});
      read_card_pixel(8, 16, samples[i]);
      uint8_t pixels[32 * 32 * 4];
      glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        if (x < 2 || x >= 30 || y < 2 || y >= 30) ok &= pixels[(y * 32 + x) * 4 + 3] == 0;
      }
      if (i == 4) memcpy(disabled_only, pixels, sizeof(pixels));
    }
    ok &= samples[2][1] < samples[0][1] && samples[3][1] > samples[0][1];
    ok &= abs(samples[4][0] - samples[4][1]) <= 1 && abs(samples[4][1] - samples[4][2]) <= 1;
    glClear(GL_COLOR_BUFFER_BIT);
    render_plastic_surface(R(2, 2, 28, 28), 10, 2, 3, look_ptr(CTRL_DISABLED | CTRL_HOVER | CTRL_PRESSED | CTRL_SELECTED),
                            WEB(0x48aa36), 0x80000000, glyph, NULL, (ipoint16_t){16, 16});
    glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, disabled_flags);
    ok &= memcmp(disabled_only, disabled_flags, sizeof(disabled_only)) == 0;
    glClear(GL_COLOR_BUFFER_BIT);
    render_plastic_surface(R(2, 2, 28, 28), 10, 2, 3, look_ptr(CTRL_NORMAL), 0x8036aa48,
                            0x80000000, glyph, NULL, (ipoint16_t){16, 16});
    uint8_t face[4], rim[4], top[4], center[4], under[4], beside[4];
    read_card_pixel(8, 16, face); read_card_pixel(16, 28, rim);
    read_card_pixel(16, 12, top); read_card_pixel(16, 16, center); read_card_pixel(16, 20, under); read_card_pixel(10, 20, beside);
    ok &= face[3] == 128 && rim[3] > 0 && rim[3] < 128 && top[1] < center[1] && center[1] < beside[1] && under[1] > beside[1];
    for (int size = 4; size <= 24; size += 4) {
      glClear(GL_COLOR_BUFFER_BIT);
      render_plastic_surface(R(4, 4, size, 8), 100, 2, 3, look_ptr(CTRL_SELECTED), WEB(0x48aa36), 0x80000000, 0, NULL, (ipoint16_t){0, 0});
    }
    ok &= glGetError() == GL_NO_ERROR;
    if (!ok) fprintf(stderr, "[renderer-test] plastic normal=%u pressed=%u hover=%u alpha=%u,%u glyph=%u,%u catch=%u,%u\n",
                      samples[0][1], samples[2][1], samples[3][1], face[3], rim[3], top[1], center[1], under[1], beside[1]);
  }
  R_DeleteTexture(glyph);
  R_DestroyWindowTarget(&fbo, &texture, &w, &h);
  if (initialized) ui_shutdown_prog();
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}

static void test_selection_gradient(void) {
  TEST("Selection gradients interpolate in linear light, preserve alpha and share one silhouette");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) { SKIP("Offscreen OpenGL unavailable"); }
  CGLError error = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (error != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  bool initialized = ui_init_prog(), ok = initialized;
  uint32_t fbo = 0, texture = 0;
  int w = 0, h = 0;
  if (ok) {
    init_ui_white_texture();
    extern uint32_t ui_white_texture;
    ok = R_EnsureWindowTarget(&fbo, &texture, &w, &h, 32, 32);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 32, 32);
    glDisable(GL_SCISSOR_TEST);
    set_projection(0, 0, 32, 32);
    R_SetFramebufferSRGB(true);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    render_rounded_rect_gradient(ui_white_texture, R(4, 4, 24, 24), 24, 24, 6,
                                  0xffffffff, 0xff000000);
    uint8_t top[4], mid[4], bottom[4], corner[4], outside[4];
    read_card_pixel(16, 8, top); read_card_pixel(16, 16, mid); read_card_pixel(16, 23, bottom);
    read_card_pixel(4, 4, corner); read_card_pixel(2, 16, outside);
    ok &= top[0] > mid[0] && mid[0] > bottom[0] && mid[0] > 170 && mid[0] < 200;
    ok &= top[3] == 255 && bottom[3] == 255 && corner[3] == 0 && outside[3] == 0;
    glClear(GL_COLOR_BUFFER_BIT);
    render_rounded_top_rect_gradient(ui_white_texture, R(4, 4, 24, 24), 24, 24, 6,
                                      0xffffffff, 0xff000000);
    read_card_pixel(16, 8, top); read_card_pixel(16, 23, bottom);
    read_card_pixel(4, 4, corner); read_card_pixel(2, 16, outside);
    ok &= top[0] > bottom[0] && top[3] == 255 && bottom[3] == 255 && corner[3] == 0 && outside[3] == 0;
    read_card_pixel(4, 27, corner); read_card_pixel(16, 27, bottom);
    ok &= corner[3] > 0 && memcmp(corner, bottom, 4) == 0;
    glClear(GL_COLOR_BUFFER_BIT);
    render_rounded_rect_gradient(ui_white_texture, R(4, 4, 24, 24), 24, 24, 0,
                                  0x800000ff, 0x8000ff00);
    read_card_pixel(16, 8, top); read_card_pixel(16, 23, bottom); read_card_pixel(4, 4, corner);
    ok &= top[0] > bottom[0] && bottom[1] > top[1] && top[3] == 128 && bottom[3] == 128 && corner[3] == 128;
    glClear(GL_COLOR_BUFFER_BIT);
    render_rounded_rect(ui_white_texture, R(4, 4, 24, 24), 24, 24, 0, 1, 0xffff0000);
    read_card_pixel(16, 8, top); read_card_pixel(16, 23, bottom);
    ok &= top[2] == 255 && memcmp(top, bottom, 4) == 0 && glGetError() == GL_NO_ERROR;
    R_SetFramebufferSRGB(false);
    shutdown_white_texture();
  }
  R_DestroyWindowTarget(&fbo, &texture, &w, &h);
  if (initialized) ui_shutdown_prog();
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}
static result_t rotated_content_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == evPaint) {
    fill_rect(0xffffffff, wp == WINDOW_PAINT_OVERLAY ? R(2, 2, 3, 3) : R(8, 4, 8, 4));
    return true;
  }
  return false;
}
static void test_view_rotation(void) {
  TEST("Renderer rotates canvas content and restores the projection for viewport UI");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) {
    SKIP("Offscreen OpenGL unavailable");
  }
  CGLError error = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (error != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  bool ok = ui_init_prog();
  if (ok) {
    init_ui_white_texture();
    GLuint fbo = 0, texture = 0;
    int width = 0, height = 0;
    R_EnsureWindowTarget(&fbo, &texture, &width, &height, 64, 64);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 64, 64);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    set_projection(0, 0, 64, 64);
    g_ui_runtime.running = true;
    window_t win = {.frame = {0, 0, 64, 64}, .flags = WINDOW_NOTITLE, .proc = rotated_content_proc,
      .view = &(window_view_t){.enabled = true, .width = 64, .height = 64, .pixel_ratio = 1,
               .matrix = {.a = 0, .b = 1, .tx = 66, .ty = 3}}};
    window_surface_adopt(&win, fbo, texture, 64, 64);
    send_message(&win, evPaint, 0, NULL);
    uint8_t rotated[4], original[4], overlay[4];
    glReadPixels(60, 64 - 15 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rotated);
    glReadPixels(10, 64 - 6 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, original);
    glReadPixels(3, 64 - 3 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, overlay);
    ok = rotated[0] == 255 && original[0] == 0 && overlay[0] == 255 && glGetError() == GL_NO_ERROR;
    g_ui_runtime.running = false;
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &fbo);
    shutdown_white_texture();
    ui_shutdown_prog();
  }
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}

static result_t viewport_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == evPaint) {
    draw_rect_ex(viewport_texture, R(-win->parent->hscroll.pos, -win->parent->vscroll.pos, 400, 300), 0, 1);
    return true;
  }
  return false;
}

static void test_fixed_viewport(void) {
  TEST("Scrolled root keeps child viewport fixed and applies document pan once");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) {
    SKIP("Offscreen OpenGL unavailable");
  }
  CGLError err = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (err != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  bool ok = ui_init_prog();
  if (ok) {
    uint8_t white[] = {255, 255, 255, 255};
    viewport_texture = R_CreateTextureRGBA(1, 1, white, R_FILTER_NEAREST, R_WRAP_CLAMP);
    window_t root = {0}, child = {0};
    root.frame = R(0, 0, 200 + SCROLLBAR_WIDTH, 150 + TITLEBAR_HEIGHT + STATUSBAR_HEIGHT);
    root.flags = WINDOW_HSCROLL | WINDOW_VSCROLL | WINDOW_STATUSBAR;
    root.hscroll.pos = 200;
    root.vscroll.pos = 150;
    root.hscroll.visible = root.vscroll.visible = true;
    child.frame = R(0, 0, 200, 150);
    child.flags = WINDOW_NOTITLE;
    child.parent = &root;
    child.proc = viewport_proc;
    window_surface_t *surf = window_surface_ensure(&root);
    R_EnsureWindowTarget(&surf->fbo, &surf->tex, &surf->w, &surf->h, root.frame.w, root.frame.h);
    glBindFramebuffer(GL_FRAMEBUFFER, surf->fbo);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    g_ui_runtime.running = true;
    send_message(&child, evPaint, 0, NULL);
    uint8_t pixel[4];
    glReadPixels(199, root.frame.h - TITLEBAR_HEIGHT - 149 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    ok = pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255;
    g_ui_runtime.running = false;
    glDeleteTextures(1, &surf->tex);
    glDeleteTextures(1, &viewport_texture);
    glDeleteFramebuffers(1, &surf->fbo);
    window_surface_release(&root);
    ui_shutdown_prog();
  }
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}

static void test_onion_alpha(void) {
  TEST("Faded strokes keep an opaque canvas opaque through window compositing");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, (CGLPixelFormatAttribute)0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  CGLError err = CGLChoosePixelFormat(attrs, &format, &count);
  if (err != kCGLNoError || !format) { SKIP("Offscreen OpenGL unavailable"); }
  err = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (err != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  bool initialized = ui_init_prog();
  bool correct = initialized;
  GLuint targets[2] = {0}, fbo = 0, ink = 0;
  if (initialized) {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(2, targets);
    for (int i = 0; i < 2; i++) {
      glBindTexture(GL_TEXTURE_2D, targets[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    uint8_t black[] = {0, 0, 0, 255};
    ink = R_CreateTextureRGBA(1, 1, black, R_FILTER_NEAREST, R_WRAP_CLAMP);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[0], 0);
    correct = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glViewport(0, 0, 4, 1);
    set_projection(0, 0, 4, 1);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    float opacity[] = {1.0f, 0.5f, 0.25f, 0.125f};
    for (int i = 3; i >= 0; i--)
      draw_rect_ex(ink, R(i, 0, 1, 1), 0, opacity[i]);
    uint8_t canvas[16], screen[16];
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, canvas);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[1], 0);
    glClearColor(0.17f, 0.17f, 0.17f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_rounded_rect(targets[0], R(0, 0, 4, 1), 4, 1, 0, 1);
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, screen);
    int expected[] = {0, 128, 191, 223};
    for (int i = 0; i < 4; i++) {
      printf("\n    opacity=%g canvas=(%u,%u) screen=(%u,%u)",
             opacity[i], canvas[i*4], canvas[i*4+3], screen[i*4], screen[i*4+3]);
      correct &= canvas[i*4+3] == 255 && screen[i*4+3] == 255;
      for (int channel = 0; channel < 3; channel++)
        correct &= abs((int)screen[i*4+channel] - expected[i]) <= 1;
    }
    correct &= glGetError() == GL_NO_ERROR;
    glDeleteTextures(1, &ink);
    glDeleteTextures(2, targets);
    glDeleteFramebuffers(1, &fbo);
    ui_shutdown_prog();
  }
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(correct);
  PASS();
}

static void test_srgb_linear_source_over(void) {
  TEST("sRGB window targets store linear-light source-over results");
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, (CGLPixelFormatAttribute)0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  CGLError err = CGLChoosePixelFormat(attrs, &format, &count);
  if (err != kCGLNoError || !format) { SKIP("Offscreen OpenGL unavailable"); }
  err = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (err != kCGLNoError || !context) { SKIP("Offscreen OpenGL context unavailable"); }
  CGLSetCurrentContext(context);
  bool initialized = ui_init_prog();
  bool correct = initialized;
  uint32_t fbo = 0, surface = 0, black = 0, color_tex = 0, float_tex = 0;
  int width = 0, height = 0;
  if (correct) {
    correct = R_EnsureWindowTarget(&fbo, &surface, &width, &height, 1, 1);
    black = R_CreateTextureRGBA(1, 1, (uint8_t[]){0, 0, 0, 255},
                                R_FILTER_NEAREST, R_WRAP_CLAMP);
    correct &= black != 0;
    color_tex = R_CreateTextureSRGBA8(1, 1, (uint8_t[]){128, 128, 128, 128},
                                      R_FILTER_NEAREST, R_WRAP_CLAMP);
    float_tex = R_CreateTexture(1, 1, R_TEXTURE_RGBA16F_LINEAR,
                                (float[]){0.25f, 0.25f, 0.25f, 0.5f},
                                R_FILTER_NEAREST, R_WRAP_CLAMP);
    correct &= color_tex != 0 && float_tex != 0;
  }
  if (correct) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 1, 1);
    glDisable(GL_SCISSOR_TEST);
    set_projection(0, 0, 1, 1);
    R_SetFramebufferSRGB(true);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_rect_ex((int)black, R(0, 0, 1, 1), 0, 0.5f);
    uint8_t result[4] = {0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result);
    GLenum draw_error = glGetError();
    bool draw_ok = abs((int)result[0] - 188) <= 1 &&
                   abs((int)result[1] - 188) <= 1 &&
                   abs((int)result[2] - 188) <= 1 && result[3] == 255 &&
                   draw_error == GL_NO_ERROR;
    uint8_t surface_result[4] = {0}, color_result[4] = {0}, float_result[4] = {0};
    bool surface_read = R_ReadTextureSRGBA8(surface, 1, 1, surface_result);
    bool color_read = R_ReadTextureSRGBA8(color_tex, 1, 1, color_result);
    bool float_read = R_ReadTextureSRGBA8(float_tex, 1, 1, float_result);
    bool surface_ok = surface_read && abs((int)surface_result[0] - 188) <= 1 && surface_result[3] == 255;
    bool color_ok = color_read && abs((int)color_result[0] - 128) <= 1 && color_result[3] == 128;
    bool float_ok = float_read && abs((int)float_result[0] - 188) <= 1 && float_result[3] == 128;
    correct = draw_ok && surface_ok && color_ok && float_ok;
    if (!correct)
      fprintf(stderr, "[renderer-test] gpu=%s draw=%u,%u,%u,%u error=0x%x surface=%d:%u,%u color=%d:%u,%u float=%d:%u,%u\n",
              glGetString(GL_RENDERER), result[0], result[1], result[2], result[3], draw_error,
              surface_read, surface_result[0], surface_result[3], color_read, color_result[0], color_result[3],
              float_read, float_result[0], float_result[3]);
  }
  R_SetFramebufferSRGB(false);
  if (black) R_DeleteTexture(black);
  if (color_tex) R_DeleteTexture(color_tex);
  if (float_tex) R_DeleteTexture(float_tex);
  R_DestroyWindowTarget(&fbo, &surface, &width, &height);
  if (initialized) ui_shutdown_prog();
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(correct);
  PASS();
}
#else
static void test_onion_alpha(void) {
  TEST("Faded strokes keep an opaque canvas opaque through window compositing");
  SKIP("Offscreen test requires macOS CGL");
}
static void test_srgb_linear_source_over(void) {
  TEST("sRGB window targets store linear-light source-over results");
  SKIP("Offscreen test requires macOS CGL");
}
#endif

int main(void) {
  TEST_START("Renderer alpha");
#if defined(__APPLE__) && !TARGET_OS_IOS
  test_shader_state_restore();
  test_page_pane_surfaces();
  test_plastic_surface();
  test_selection_gradient();
  test_view_rotation();
  test_fixed_viewport();
#endif
  test_onion_alpha();
  test_srgb_linear_source_over();
#if defined(__APPLE__) && !TARGET_OS_IOS
#endif
  TEST_END();
}
