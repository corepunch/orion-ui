#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>
#include <orion/user/toolbar.h>
#include <orion/user/image.h>
#include <orion/user/color.h>
#include <string.h>

#if defined(__APPLE__) && !TARGET_OS_IOS
#include <OpenGL/OpenGL.h>

extern bool ui_init_prog(void);
extern void ui_shutdown_prog(void);
extern void init_ui_white_texture(void);
extern void shutdown_white_texture(void);

static uint32_t viewport_texture;
static void test_png_toolbar(void) {
  TEST("PNG toolbar preserves colours, selects pressed row and retains texture after failed reload");
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
  static window_t root;
  toolbar_item_t item = {TOOLBAR_ITEM_BUTTON, 1, "strip:0"};
  irect16_t rect = R(2, 2, 24, 24);
  toolbar_state_t tb = {.items = &item, .item_rects = &rect, .item_count = 1,
    .pressed_item = -1, .hot_item = -1, .btn_size = 24, .style = TOOLBAR_STYLE_PRESSED_STRIP};
  window_t band = {.userdata = &tb};
  root = (window_t){.frame = {0, 0, 64, 64}, .flags = WINDOW_TOOLBAR | WINDOW_NOTITLE,
                    .toolbar = &band, .surface_w = 64, .surface_h = 64};
  if (ok) {
    init_ui_white_texture();
    ok = R_EnsureWindowTarget(&fbo, &texture, &w, &h, 64, 64);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    uint8_t pixels[8 * 16 * 4];
    for (int i = 0; i < 8 * 16; i++) {
      pixels[i * 4] = i < 64 ? 255 : 0; pixels[i * 4 + 1] = i < 64 ? 0 : 255;
      pixels[i * 4 + 2] = 0; pixels[i * 4 + 3] = 255;
    }
    const char *path = "/tmp/orion-toolbar-atlas-test.png";
    ok &= save_image_png(path, pixels, 8, 16);
    g_ui_runtime.running = true;
    ok &= toolbar_handle_message(&root, tbLoadStrip, 8, (void *)path);
    uint32_t original = tb.strip.tex;
    ok &= !toolbar_handle_message(&root, tbLoadStrip, 0, (void *)path) && tb.strip.tex == original;
    uint8_t normal[4], pressed[4];
    toolbar_draw_non_client(&root);
    glReadPixels(14, 64 - 14 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, normal);
    tb.pressed_item = 0;
    toolbar_draw_non_client(&root);
    glReadPixels(14, 64 - 14 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pressed);
    ok &= normal[0] == 255 && normal[1] == 0 && pressed[0] == 0 && pressed[1] == 255;
    tb.pressed_item = -1;
    tb.style |= TOOLBAR_STYLE_IMAGE_BUTTONS;
    toolbar_draw_non_client(&root);
    uint8_t body_margin[4], active_margin[4], active[4];
    glReadPixels(3, 64 - 14 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, body_margin);
    item.flags = TOOLBAR_BUTTON_FLAG_ACTIVE;
    tb.hot_item = 0;
    toolbar_draw_non_client(&root);
    glReadPixels(3, 64 - 14 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, active_margin);
    glReadPixels(14, 64 - 14 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, active);
    ok &= active[0] == 0 && active[1] == 255 && memcmp(body_margin, active_margin, 4) == 0;
    item.flags = TOOLBAR_ITEM_FLAG_DISABLED;
    toolbar_draw_non_client(&root);
    uint8_t disabled[4];
    glReadPixels(14, 64 - 14 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, disabled);
    ok &= disabled[0] > disabled[1] && disabled[0] < normal[0];
    uint8_t states[8 * 40 * 4];
    const uint8_t colours[][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {88, 88, 88}};
    irect16_t regions[] = {R(0, 0, 8, 8), R(0, 8, 8, 8), R(0, 16, 8, 8), R(0, 24, 8, 8), R(0, 32, 8, 8)};
    for (int i = 0; i < 8 * 40; i++) {
      memcpy(states + i * 4, colours[i / 64], 3);
      states[i * 4 + 3] = 255;
    }
    ok &= save_image_png(path, states, 8, 40);
    toolbar_atlas_t atlas = {path, 1, ARRAY_LEN(regions), regions};
    ok &= toolbar_handle_message(&root, tbLoadAtlas, 0, &atlas);
    original = tb.strip.tex;
    regions[0] = R(0, 0, 9, 8);
    ok &= !toolbar_handle_message(&root, tbLoadAtlas, 0, &atlas) && tb.strip.tex == original;
    tb.style = TOOLBAR_STYLE_STATE_STRIP | TOOLBAR_STYLE_IMAGE_BUTTONS;
    for (int row = 0; row < 5; row++) {
      item.flags = row == 1 ? TOOLBAR_BUTTON_FLAG_ACTIVE : row == 4 ? TOOLBAR_ITEM_FLAG_DISABLED : 0;
      tb.pressed_item = row == 2 ? 0 : -1;
      tb.hot_item = row == 3 ? 0 : -1;
      toolbar_draw_non_client(&root);
      uint8_t px[4];
      glReadPixels(14, 64 - 14 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
      uint8_t gray = (uint8_t)(ui_srgb8_to_linear(88) * 255 + 0.5f);
      bool match = row == 4 ? px[0] == gray && px[1] == gray && px[2] == gray && px[3] == 255 : memcmp(px, colours[row], 3) == 0;
      if (!match) fprintf(stderr, "[renderer-test] atlas row=%d pixel=%u,%u,%u\n", row, px[0], px[1], px[2]);
      ok &= match;
    }
    ok &= glGetError() == GL_NO_ERROR;
    if (!ok) fprintf(stderr, "[renderer-test] toolbar normal=%u,%u pressed=%u,%u\n", normal[0], normal[1], pressed[0], pressed[1]);
    g_ui_runtime.running = false;
    R_DeleteTexture(tb.strip_tex);
    free(tb.strip_regions);
    remove(path);
    shutdown_white_texture();
  }
  root.toolbar = NULL;
  R_DestroyWindowTarget(&fbo, &texture, &w, &h);
  if (initialized) ui_shutdown_prog();
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}
static void read_card_pixel(int x, int y, uint8_t pixel[4]) {
  glReadPixels(x, 32 - y - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

static void test_image_background(void) {
  TEST("Image backgrounds preserve end caps, authored states, alpha and bounds at small sizes");
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
  image_atlas_t atlas = {0};
  if (ok) {
    ok = R_EnsureWindowTarget(&fbo, &texture, &w, &h, 32, 32);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 32, 32);
    glDisable(GL_SCISSOR_TEST);
    set_projection(0, 0, 32, 32);
    uint8_t pixels[10 * 30 * 4];
    const uint8_t colours[][3] = {{0, 255, 0}, {255, 255, 0}, {0, 0, 255}, {255, 0, 255}, {88, 88, 88}};
    for (int y = 0; y < 30; y++) for (int x = 0; x < 10; x++) {
      uint8_t *px = pixels + (y * 10 + x) * 4;
      memcpy(px, x < 2 ? (uint8_t[]){255, 0, 0} : x >= 8 ? (uint8_t[]){0, 255, 255} : colours[y / 6], 3);
      px[3] = y % 6 == 0 && (x == 0 || x == 9) ? 0 : 255;
    }
    const char *path = "/tmp/orion-image-background-test.png";
    ok &= save_image_png(path, pixels, 10, 30);
    g_ui_runtime.running = true;
    ok &= image_atlas_load(&atlas, path);
    uint32_t original = atlas.tex;
    ok &= !image_atlas_load(&atlas, "/tmp/orion-missing-skin.png") && atlas.tex == original;
    image_background_t bg = {.atlas = &atlas, .source_border = {2, 0, 2, 0}, .border = {2, 0, 2, 0}};
    for (int i = 0; i < IMAGE_BG_COUNT; i++) bg.states[i] = R(0, i * 6, 10, 6);
    ctrl_state_t states[] = {CTRL_NORMAL, CTRL_SELECTED | CTRL_HOVER, CTRL_PRESSED | CTRL_SELECTED,
                            CTRL_HOVER, CTRL_DISABLED | CTRL_PRESSED | CTRL_SELECTED | CTRL_HOVER};
    for (int i = 0; i < IMAGE_BG_COUNT; i++) {
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      ok &= draw_image_background(R(2, 2, 28, 6), &bg, states[i]);
      uint8_t left[4], middle[4], right[4], outside[4], corner[4];
      read_card_pixel(2, 5, left); read_card_pixel(16, 5, middle); read_card_pixel(29, 5, right);
      read_card_pixel(1, 5, outside); read_card_pixel(2, 2, corner);
      uint8_t gray = (uint8_t)(ui_srgb8_to_linear(88) * 255 + 0.5f);
      ok &= left[0] == 255 && left[1] == 0 && right[1] == 255 && right[2] == 255;
      ok &= i == 4 ? middle[0] == gray && middle[1] == gray && middle[2] == gray : memcmp(middle, colours[i], 3) == 0;
      ok &= middle[3] == 255 && outside[3] == 0 && corner[3] == 0;
    }
    bg.source_border = R(2, 2, 2, 2);
    bg.border = R(4, 4, 4, 4);
    glClear(GL_COLOR_BUFFER_BIT);
    ok &= draw_image_background(R(10, 10, 3, 3), &bg, CTRL_NORMAL);
    uint8_t outside[4];
    read_card_pixel(9, 11, outside); ok &= outside[3] == 0;
    read_card_pixel(13, 11, outside); ok &= outside[3] == 0;
    ok &= glGetError() == GL_NO_ERROR;
    image_atlas_free(&atlas);
    g_ui_runtime.running = false;
    remove(path);
  }
  R_DestroyWindowTarget(&fbo, &texture, &w, &h);
  if (initialized) ui_shutdown_prog();
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
  ASSERT_TRUE(ok);
  PASS();
}

static void test_gradient_card(void) {
  TEST("Gradient card clips sheen and ring, preserves alpha and keeps selection geometry fixed");
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
    ok = R_EnsureWindowTarget(&fbo, &texture, &w, &h, 32, 32);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 32, 32);
    glDisable(GL_SCISSOR_TEST);
    set_projection(0, 0, 32, 32);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    render_gradient_card(R(4, 4, 24, 24), 24, 24, 7, 2, 1, CTRL_NORMAL, 0x80ff8000);
    uint8_t top[4], bottom[4], corner[4], outside[4], normal_ring[4], selected_ring[4], selected_face[4];
    read_card_pixel(16, 8, top); read_card_pixel(16, 23, bottom);
    read_card_pixel(4, 4, corner); read_card_pixel(2, 16, outside); read_card_pixel(4, 16, normal_ring);
    glClear(GL_COLOR_BUFFER_BIT);
    render_gradient_card(R(4, 4, 24, 24), 24, 24, 7, 2, 1, CTRL_SELECTED, 0x80ff8000);
    read_card_pixel(4, 16, selected_ring); read_card_pixel(16, 8, selected_face);
    ok &= top[2] > bottom[2] && top[3] == 128 && bottom[3] == 128;
    ok &= corner[3] == 0 && outside[3] == 0 && normal_ring[3] == 0;
    ok &= selected_ring[3] > 80 && selected_ring[3] <= 128 && memcmp(top, selected_face, 4) == 0;
    glClear(GL_COLOR_BUFFER_BIT);
    render_gradient_card(R(4, 4, 24, 24), 24, 24, 0, 1, 0, CTRL_NORMAL, 0xffff8000);
    read_card_pixel(6, 6, corner);
    ok &= corner[3] == 255 && glGetError() == GL_NO_ERROR;
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
      .surface_fbo = fbo, .surface_tex = texture, .surface_w = 64, .surface_h = 64,
      .view = {.enabled = true, .width = 64, .height = 64, .pixel_ratio = 1,
               .matrix = {.a = 0, .b = 1, .tx = 66, .ty = 3}}};
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
    root.frame = R(0, 0, 217, 188);
    root.flags = WINDOW_HSCROLL | WINDOW_VSCROLL | WINDOW_STATUSBAR;
    root.hscroll.pos = 200;
    root.vscroll.pos = 150;
    root.hscroll.visible = root.vscroll.visible = true;
    child.frame = R(0, 0, 200, 150);
    child.flags = WINDOW_NOTITLE;
    child.parent = &root;
    child.proc = viewport_proc;
    root.surface_w = 217;
    root.surface_h = 188;
    R_EnsureWindowTarget(&root.surface_fbo, &root.surface_tex,
                         &root.surface_w, &root.surface_h, 217, 188);
    glBindFramebuffer(GL_FRAMEBUFFER, root.surface_fbo);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    g_ui_runtime.running = true;
    send_message(&child, evPaint, 0, NULL);
    uint8_t pixel[4];
    glReadPixels(199, 188 - TITLEBAR_HEIGHT - 149 - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    ok = pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255;
    g_ui_runtime.running = false;
    glDeleteTextures(1, &root.surface_tex);
    glDeleteTextures(1, &viewport_texture);
    glDeleteFramebuffers(1, &root.surface_fbo);
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
  test_gradient_card();
  test_image_background();
  test_view_rotation();
  test_fixed_viewport();
#endif
  test_onion_alpha();
  test_srgb_linear_source_over();
#if defined(__APPLE__) && !TARGET_OS_IOS
  test_png_toolbar();
#endif
  TEST_END();
}
