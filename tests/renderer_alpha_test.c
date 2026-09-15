#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>

#if defined(__APPLE__) && !TARGET_OS_IOS
#include <OpenGL/OpenGL.h>

extern bool ui_init_prog(void);
extern void ui_shutdown_prog(void);

static uint32_t viewport_texture;
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
#else
static void test_onion_alpha(void) {
  TEST("Faded strokes keep an opaque canvas opaque through window compositing");
  SKIP("Offscreen test requires macOS CGL");
}
#endif

int main(void) {
  TEST_START("Renderer alpha");
#if defined(__APPLE__) && !TARGET_OS_IOS
  test_fixed_viewport();
#endif
  test_onion_alpha();
  TEST_END();
}
