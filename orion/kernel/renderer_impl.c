/* Renderer API Implementation - OpenGL abstraction layer */
#include "renderer.h"
#include <orion/user/gl_compat.h>
#include <string.h>
#include <stdio.h>

// Initialize a mesh with vertex attributes and drawing mode
void R_MeshInit(R_Mesh* mesh, const R_VertexAttrib* attribs, size_t attrib_count, 
                size_t vertex_size, GLenum draw_mode) {
  if (!mesh) return;
  
  memset(mesh, 0, sizeof(R_Mesh));
  mesh->vertex_size = vertex_size;
  mesh->draw_mode = draw_mode;
  
  // Create VAO and VBO
  glGenVertexArrays(1, &mesh->vao);
  glGenBuffers(1, &mesh->vbo);
  
  // Bind VAO to configure it
  glBindVertexArray(mesh->vao);
  glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
  
  // Set up vertex attributes
  if (attribs && attrib_count > 0) {
    R_SetVertexAttribs(attribs, attrib_count, vertex_size);
  }
  
  // Unbind to avoid accidental modifications
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Upload vertex data to mesh buffer
void R_MeshUpload(R_Mesh* mesh, const void* data, size_t vertex_count) {
  if (!mesh || !data || vertex_count == 0) return;
  
  mesh->vertex_count = vertex_count;
  
  // Bind and upload data
  glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
  glBufferData(GL_ARRAY_BUFFER, vertex_count * mesh->vertex_size, data, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Draw the mesh using its current vertex data
void R_MeshDraw(R_Mesh* mesh) {
  if (!mesh || mesh->vertex_count == 0) return;
  
  glBindVertexArray(mesh->vao);
  glDrawArrays(mesh->draw_mode, 0, mesh->vertex_count);
  glBindVertexArray(0);
}

// Upload and draw in one call (efficient for dynamic geometry)
void R_MeshDrawDynamic(R_Mesh* mesh, const void* data, size_t vertex_count) {
  if (!mesh || !data || vertex_count == 0) return;
  
  mesh->vertex_count = vertex_count;
  
  // Bind VAO
  glBindVertexArray(mesh->vao);
  
  // Upload vertex data
  glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
  glBufferData(GL_ARRAY_BUFFER, vertex_count * mesh->vertex_size, data, GL_DYNAMIC_DRAW);
  
  // Draw
  glDrawArrays(mesh->draw_mode, 0, vertex_count);
  
  // Unbind to avoid accidental modifications
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Destroy mesh and free GPU resources
void R_MeshDestroy(R_Mesh* mesh) {
  if (!mesh) return;
  
  SAFE_DELETE_N(mesh->vao, glDeleteVertexArrays);
  SAFE_DELETE_N(mesh->vbo, glDeleteBuffers);
  SAFE_DELETE_N(mesh->ibo, glDeleteBuffers);
  
  memset(mesh, 0, sizeof(R_Mesh));
}

// Bind texture to current texture unit
void R_TextureBind(R_Texture* texture) {
  if (!texture || texture->id == 0) return;
  glBindTexture(GL_TEXTURE_2D, texture->id);
}

// Unbind texture from current texture unit
void R_TextureUnbind(void) {
  glBindTexture(GL_TEXTURE_2D, 0);
}

// Enable and configure vertex attributes
void R_SetVertexAttribs(const R_VertexAttrib* attribs, size_t count, size_t vertex_size) {
  if (!attribs || count == 0) return;
  
  for (size_t i = 0; i < count; i++) {
    const R_VertexAttrib* attr = &attribs[i];
    glEnableVertexAttribArray(attr->index);
    glVertexAttribPointer(attr->index, attr->size, attr->type, attr->normalized,
                         vertex_size, (void*)attr->offset);
  }
}

// Disable vertex attributes
void R_ClearVertexAttribs(size_t count) {
  for (size_t i = 0; i < count; i++) {
    glDisableVertexAttribArray(i);
  }
}

bool R_UpdateTextureR8(uint32_t tex, int x, int y, int w, int h,
                       const void *pixels) {
  if (!tex || !pixels || w <= 0 || h <= 0)
    return false;
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
                  GL_RED, GL_UNSIGNED_BYTE, pixels);
  return true;
}

// Scene pipelines share the framework context and keep backend calls here.
static void gpu_report_error(const char *operation) {
  GLenum error;
  while ((error = glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "[renderer] %s failed: GPU error=0x%x\n", operation, error);
    fflush(stderr);
  }
}

void R_GpuActiveTexture(GLenum texture) {
  glActiveTexture(texture);
  gpu_report_error("glActiveTexture");
}

void R_GpuAttachShader(GLuint program, GLuint shader) {
  glAttachShader(program, shader);
  gpu_report_error("glAttachShader");
}

void R_GpuBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
  glBindAttribLocation(program, index, name);
  gpu_report_error("glBindAttribLocation");
}

void R_GpuBindBuffer(GLenum target, GLuint buffer) {
  glBindBuffer(target, buffer);
  gpu_report_error("glBindBuffer");
}

void R_GpuBindTexture(GLenum target, GLuint texture) {
  glBindTexture(target, texture);
  gpu_report_error("glBindTexture");
}

void R_GpuBindVertexArray(GLuint array) {
  glBindVertexArray(array);
  gpu_report_error("glBindVertexArray");
}

void R_GpuBlendFunc(GLenum sfactor, GLenum dfactor) {
  glBlendFunc(sfactor, dfactor);
  gpu_report_error("glBlendFunc");
}

void R_GpuBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
  glBufferData(target, size, data, usage);
  gpu_report_error("glBufferData");
}

void R_GpuClear(GLbitfield mask) {
  glClear(mask);
  gpu_report_error("glClear");
}

void R_GpuClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  glClearColor(red, green, blue, alpha);
  gpu_report_error("glClearColor");
}

void R_GpuCompileShader(GLuint shader) {
  glCompileShader(shader);
  gpu_report_error("glCompileShader");
}

GLuint R_GpuCreateProgram(void) {
  GLuint result = glCreateProgram();
  gpu_report_error("glCreateProgram");
  return result;
}

GLuint R_GpuCreateShader(GLenum type) {
  GLuint result = glCreateShader(type);
  gpu_report_error("glCreateShader");
  return result;
}

void R_GpuCullFace(GLenum mode) {
  glCullFace(mode);
  gpu_report_error("glCullFace");
}

void R_GpuDeleteBuffers(GLsizei n, const GLuint *buffers) {
  glDeleteBuffers(n, buffers);
  gpu_report_error("glDeleteBuffers");
}

void R_GpuDeleteProgram(GLuint program) {
  glDeleteProgram(program);
  gpu_report_error("glDeleteProgram");
}

void R_GpuDeleteShader(GLuint shader) {
  glDeleteShader(shader);
  gpu_report_error("glDeleteShader");
}

void R_GpuDeleteTextures(GLsizei n, const GLuint *textures) {
  glDeleteTextures(n, textures);
  gpu_report_error("glDeleteTextures");
}

void R_GpuDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
  glDeleteVertexArrays(n, arrays);
  gpu_report_error("glDeleteVertexArrays");
}

void R_GpuDepthMask(GLboolean flag) {
  glDepthMask(flag);
  gpu_report_error("glDepthMask");
}

void R_GpuDisable(GLenum cap) {
  glDisable(cap);
  gpu_report_error("glDisable");
}

void R_GpuDisableVertexAttribArray(GLuint index) {
  glDisableVertexAttribArray(index);
  gpu_report_error("glDisableVertexAttribArray");
}

void R_GpuDrawArrays(GLenum mode, GLint first, GLsizei count) {
  glDrawArrays(mode, first, count);
  gpu_report_error("glDrawArrays");
}

void R_GpuEnable(GLenum cap) {
  glEnable(cap);
  gpu_report_error("glEnable");
}

void R_GpuEnableVertexAttribArray(GLuint index) {
  glEnableVertexAttribArray(index);
  gpu_report_error("glEnableVertexAttribArray");
}

void R_GpuGenBuffers(GLsizei n, GLuint *buffers) {
  glGenBuffers(n, buffers);
  gpu_report_error("glGenBuffers");
}

void R_GpuGenTextures(GLsizei n, GLuint *textures) {
  glGenTextures(n, textures);
  gpu_report_error("glGenTextures");
}

void R_GpuGenVertexArrays(GLsizei n, GLuint *arrays) {
  glGenVertexArrays(n, arrays);
  gpu_report_error("glGenVertexArrays");
}

void R_GpuGenerateMipmap(GLenum target) {
  glGenerateMipmap(target);
  gpu_report_error("glGenerateMipmap");
}

void R_GpuGetBooleanv(GLenum pname, GLboolean *params) {
  glGetBooleanv(pname, params);
  gpu_report_error("glGetBooleanv");
}

void R_GpuGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
  glGetProgramInfoLog(program, bufSize, length, infoLog);
  gpu_report_error("glGetProgramInfoLog");
}

void R_GpuGetProgramiv(GLuint program, GLenum pname, GLint *params) {
  glGetProgramiv(program, pname, params);
  gpu_report_error("glGetProgramiv");
}

void R_GpuGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
  glGetShaderInfoLog(shader, bufSize, length, infoLog);
  gpu_report_error("glGetShaderInfoLog");
}

void R_GpuGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
  glGetShaderiv(shader, pname, params);
  gpu_report_error("glGetShaderiv");
}

GLint R_GpuGetUniformLocation(GLuint program, const GLchar *name) {
  GLint result = glGetUniformLocation(program, name);
  gpu_report_error("glGetUniformLocation");
  return result;
}

void R_GpuLinkProgram(GLuint program) {
  glLinkProgram(program);
  gpu_report_error("glLinkProgram");
}

void R_GpuPointSize(GLfloat size) {
  glPointSize(size);
  gpu_report_error("glPointSize");
}

void R_GpuReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {
  glReadPixels(x, y, width, height, format, type, pixels);
  gpu_report_error("glReadPixels");
}

void R_GpuShaderSource(GLuint shader, GLsizei count, const GLchar* const *string, const GLint *length) {
  glShaderSource(shader, count, string, length);
  gpu_report_error("glShaderSource");
}

void R_GpuTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
  glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
  gpu_report_error("glTexImage2D");
}

void R_GpuTexParameteri(GLenum target, GLenum pname, GLint param) {
  glTexParameteri(target, pname, param);
  gpu_report_error("glTexParameteri");
}

void R_GpuUniform1f(GLint location, GLfloat v0) {
  glUniform1f(location, v0);
  gpu_report_error("glUniform1f");
}

void R_GpuUniform1i(GLint location, GLint v0) {
  glUniform1i(location, v0);
  gpu_report_error("glUniform1i");
}

void R_GpuUniform2f(GLint location, GLfloat v0, GLfloat v1) {
  glUniform2f(location, v0, v1);
  gpu_report_error("glUniform2f");
}

void R_GpuUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
  glUniform3f(location, v0, v1, v2);
  gpu_report_error("glUniform3f");
}

void R_GpuUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
  glUniform3fv(location, count, value);
  gpu_report_error("glUniform3fv");
}

void R_GpuUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
  glUniform4f(location, v0, v1, v2, v3);
  gpu_report_error("glUniform4f");
}

void R_GpuUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
  glUniform4fv(location, count, value);
  gpu_report_error("glUniform4fv");
}

void R_GpuUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
  glUniformMatrix4fv(location, count, transpose, value);
  gpu_report_error("glUniformMatrix4fv");
}

void R_GpuUseProgram(GLuint program) {
  glUseProgram(program);
  gpu_report_error("glUseProgram");
}

void R_GpuVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
  glVertexAttrib4f(index, x, y, z, w);
  gpu_report_error("glVertexAttrib4f");
}

void R_GpuVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer) {
  glVertexAttribPointer(index, size, type, normalized, stride, pointer);
  gpu_report_error("glVertexAttribPointer");
}
