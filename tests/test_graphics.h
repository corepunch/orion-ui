#ifndef __TEST_GRAPHICS_H__
#define __TEST_GRAPHICS_H__

#include <orion/ui.h>
#include <orion/user/gl_compat.h>

extern bool ui_init_prog(void);
extern void ui_shutdown_prog(void);
extern void init_ui_white_texture(void);
extern void shutdown_white_texture(void);

#if defined(__APPLE__) && !TARGET_OS_IOS
#include <OpenGL/OpenGL.h>
static CGLContextObj test_gl_context;

static void test_context_destroy(void) {
  CGLSetCurrentContext(NULL);
  if (test_gl_context) CGLDestroyContext(test_gl_context);
  test_gl_context = NULL;
}

static bool test_context_create(void) {
  CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile,
    (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
  CGLPixelFormatObj format = NULL;
  GLint count = 0;
  if (CGLChoosePixelFormat(attrs, &format, &count) != kCGLNoError || !format) return false;
  CGLError error = CGLCreateContext(format, NULL, &test_gl_context);
  CGLDestroyPixelFormat(format);
  return error == kCGLNoError && CGLSetCurrentContext(test_gl_context) == kCGLNoError;
}
#elif defined(__linux__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
static EGLDisplay test_gl_display = EGL_NO_DISPLAY;
static EGLContext test_gl_context = EGL_NO_CONTEXT;
static EGLSurface test_gl_surface = EGL_NO_SURFACE;

static void test_context_destroy(void) {
  if (test_gl_display != EGL_NO_DISPLAY) {
    eglMakeCurrent(test_gl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (test_gl_surface != EGL_NO_SURFACE) eglDestroySurface(test_gl_display, test_gl_surface);
    if (test_gl_context != EGL_NO_CONTEXT) eglDestroyContext(test_gl_display, test_gl_context);
    eglTerminate(test_gl_display);
  }
  test_gl_display = EGL_NO_DISPLAY; test_gl_context = EGL_NO_CONTEXT; test_gl_surface = EGL_NO_SURFACE;
}

static bool test_context_create(void) {
  PFNEGLGETPLATFORMDISPLAYEXTPROC get_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (!get_display) return false;
  test_gl_display = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (test_gl_display == EGL_NO_DISPLAY || !eglInitialize(test_gl_display, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API)) return false;
  const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  if (!eglChooseConfig(test_gl_display, attrs, &config, 1, &count) || !count) return false;
  const EGLint context_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  test_gl_context = eglCreateContext(test_gl_display, config, EGL_NO_CONTEXT, context_attrs);
  if (test_gl_context == EGL_NO_CONTEXT) return false;
  const EGLint surface_attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  test_gl_surface = eglCreatePbufferSurface(test_gl_display, config, surface_attrs);
  return test_gl_surface != EGL_NO_SURFACE &&
    eglMakeCurrent(test_gl_display, test_gl_surface, test_gl_surface, test_gl_context);
}
#else
static void test_context_destroy(void) {}
static bool test_context_create(void) { return false; }
#endif

// Only resource/pixel tests need a context. No native window or display server.
static bool test_graphics_init(int width, int height) {
#if defined(_WIN32) || TARGET_OS_IOS
  return ui_init_graphics(UI_INIT_HIDDEN, "test-graphics", width, height);
#else
  if (!test_context_create()) {
    fprintf(stderr, "[test-graphics] offscreen context creation failed\n");
    fflush(stderr);
    test_context_destroy();
    return false;
  }
  if (!ui_init_prog()) { test_context_destroy(); return false; }
  ui_update_screen_size(width * UI_WINDOW_SCALE, height * UI_WINDOW_SCALE);
  init_text_rendering();
  init_ui_white_texture();
  glViewport(0, 0, width, height);
  return true;
#endif
}

static void test_graphics_shutdown(void) {
#if defined(_WIN32) || TARGET_OS_IOS
  ui_shutdown_graphics();
#else
  shutdown_text_rendering();
  shutdown_white_texture();
  ui_shutdown_prog();
  test_context_destroy();
#endif
}

#endif
