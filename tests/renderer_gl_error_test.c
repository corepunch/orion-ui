#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>
#include <string.h>

#if defined(__APPLE__) && !TARGET_OS_IOS
#include <OpenGL/OpenGL.h>

static CGLContextObj create_offscreen_context(void) {
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, (CGLPixelFormatAttribute)0};
  CGLPixelFormatObj format = NULL;
  CGLContextObj context = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) return NULL;
  CGLError error = CGLCreateContext(format, NULL, &context);
  CGLDestroyPixelFormat(format);
  if (error != kCGLNoError || !context) return NULL;
  CGLSetCurrentContext(context);
  return context;
}

static void destroy_offscreen_context(CGLContextObj context) {
  CGLSetCurrentContext(NULL);
  CGLDestroyContext(context);
}

// Leaves GL_INVALID_ENUM pending, as an unrelated earlier GL call would.
static bool raise_stale_error(void) {
  glEnable(0);
  if (glGetError() != GL_INVALID_ENUM) return false;
  glEnable(0);
  return true;
}

static void test_stale_error_does_not_fail_textures(void) {
  TEST("A stale GL error does not fail texture creation or update");
  CGLContextObj context = create_offscreen_context();
  if (!context) { SKIP("Offscreen OpenGL context unavailable"); }
  uint8_t pixels[4 * 4 * 4];
  memset(pixels, 255, sizeof(pixels));
  bool raised = raise_stale_error();
  uint32_t r8 = R_CreateTextureR8(4, 4, pixels, R_FILTER_NEAREST, R_WRAP_CLAMP);
  bool r8_clean = glGetError() == GL_NO_ERROR;
  raised &= raise_stale_error();
  uint32_t rgba = R_CreateTextureRGBA(4, 4, pixels, R_FILTER_NEAREST, R_WRAP_CLAMP);
  bool rgba_clean = glGetError() == GL_NO_ERROR;
  raised &= raise_stale_error();
  bool updated = rgba && R_UpdateTextureRGBA(rgba, 0, 0, 4, 4, pixels);
  bool update_clean = glGetError() == GL_NO_ERROR;
  if (r8) R_DeleteTexture(r8);
  if (rgba) R_DeleteTexture(rgba);
  destroy_offscreen_context(context);
  ASSERT_TRUE(raised);
  ASSERT_TRUE(r8 != 0 && r8_clean);
  ASSERT_TRUE(rgba != 0 && rgba_clean);
  ASSERT_TRUE(updated && update_clean);
  PASS();
}

static void test_real_texture_error_still_fails(void) {
  TEST("A texture creation error raised by its own upload still fails");
  CGLContextObj context = create_offscreen_context();
  if (!context) { SKIP("Offscreen OpenGL context unavailable"); }
  uint32_t r8 = R_CreateTextureR8(-1, 4, NULL, R_FILTER_NEAREST, R_WRAP_CLAMP);
  bool clean = glGetError() == GL_NO_ERROR;
  if (r8) R_DeleteTexture(r8);
  destroy_offscreen_context(context);
  ASSERT_TRUE(r8 == 0);
  ASSERT_TRUE(clean);
  PASS();
}

static void test_capture_leaves_no_error(void) {
  TEST("Framebuffer capture reads the drawn buffer and leaves no GL error");
  CGLContextObj context = create_offscreen_context();
  if (!context) { SKIP("Offscreen OpenGL context unavailable"); }
  uint32_t fbo = 0, texture = 0, after = 0;
  int w = 0, h = 0;
  uint8_t pixels[4 * 4 * 4] = {0};
  bool target = R_EnsureWindowTarget(&fbo, &texture, &w, &h, 4, 4);
  bool captured = false, clean = false, stale_captured = false;
  if (target) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    captured = capture_framebuffer_rgba(4, 4, pixels);
    clean = glGetError() == GL_NO_ERROR;
    after = R_CreateTextureR8(4, 4, pixels, R_FILTER_NEAREST, R_WRAP_CLAMP);
    stale_captured = raise_stale_error() && capture_framebuffer_rgba(4, 4, pixels) &&
                     glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  if (after) R_DeleteTexture(after);
  R_DestroyWindowTarget(&fbo, &texture, &w, &h);
  destroy_offscreen_context(context);
  ASSERT_TRUE(target);
  ASSERT_TRUE(captured && clean);
  ASSERT_TRUE(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255);
  ASSERT_TRUE(after != 0);
  ASSERT_TRUE(stale_captured);
  PASS();
}
#else
static void test_stale_error_does_not_fail_textures(void) {
  TEST("A stale GL error does not fail texture creation or update");
  SKIP("Offscreen test requires macOS CGL");
}
static void test_real_texture_error_still_fails(void) {
  TEST("A texture creation error raised by its own upload still fails");
  SKIP("Offscreen test requires macOS CGL");
}
static void test_capture_leaves_no_error(void) {
  TEST("Framebuffer capture reads the drawn buffer and leaves no GL error");
  SKIP("Offscreen test requires macOS CGL");
}
#endif

int main(void) {
  TEST_START("Renderer GL error handling");
  test_stale_error_does_not_fail_textures();
  test_real_texture_error_still_fails();
  test_capture_leaves_no_error();
  TEST_END();
}
