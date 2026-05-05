/* OpenGL compatibility header for cross-platform support */
#ifndef __GL_COMPAT_H__
#define __GL_COMPAT_H__

#ifdef __APPLE__
  #include <OpenGL/gl3.h>
#elif defined(_WIN32) || defined(_WIN64)
  /* Windows platform - use GLEW for OpenGL extension loading */
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <GL/glew.h>
  #include <GL/gl.h>
  /* windows.h (via winuser.h) defines CW_USEDEFAULT as (int)0x80000000.
   * Orion stores window coordinates in int16_t, so its sentinel must fit in
   * 16 bits.  Restore Orion's value so that all files in this translation
   * unit see a consistent definition. */
  #undef CW_USEDEFAULT
  #define CW_USEDEFAULT (-32768)
#else
  /* Linux and other platforms */
  #include <GL/glcorearb.h>
  #include <GL/gl.h>
#endif

// Safe deletion macros for OpenGL resources and similar cleanup patterns
// NOTE: These macros are designed for scalar resource handles (GLuint, pointers, etc.)

// SAFE_DELETE: for single-parameter delete functions (e.g., glDeleteProgram, free)
// Usage: SAFE_DELETE(my_program, glDeleteProgram)
//        SAFE_DELETE(my_pointer, free)
#define SAFE_DELETE(resource, delete_func) \
  do { \
    if (resource) { \
      delete_func(resource); \
      resource = 0; \
    } \
  } while(0)

// SAFE_DELETE_N: for count+pointer delete functions (e.g., glDeleteTextures, glDeleteBuffers)
// Usage: SAFE_DELETE_N(my_texture, glDeleteTextures)
// NOTE: This macro takes the address of the resource (&resource), so it requires
//       scalar resource handles (GLuint), not pointer variables
#define SAFE_DELETE_N(resource, delete_func) \
  do { \
    if (resource) { \
      delete_func(1, &resource); \
      resource = 0; \
    } \
  } while(0)

#endif /* __GL_COMPAT_H__ */
