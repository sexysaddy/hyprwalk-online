#pragma once
// Minimal GLES 3 declarations used by this test. Entry points are resolved from
// the active EGL context; no Hyprland or OpenGL development headers are needed.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
extern "C" {
void* eglGetProcAddress(const char*);
void* eglGetDisplay(void*);
unsigned eglInitialize(void*, int*, int*);
unsigned eglBindAPI(unsigned);
void* eglCreateContext(void*, void*, void*, const int*);
unsigned eglMakeCurrent(void*, void*, void*, void*);
unsigned eglDestroyContext(void*, void*);
unsigned eglTerminate(void*);
}
#define GL_API(X) \
X(void, glGenerateMipmap, (GLenum)) \
X(void, glGetRenderbufferParameteriv, (GLenum, GLenum, GLint*)) \
X(void, glGetIntegerv, (GLenum, GLint*)) \
X(void, glGetBooleanv, (GLenum, GLboolean*)) \
X(void, glGetFloatv, (GLenum, GLfloat*)) \
X(const unsigned char*, glGetString, (GLenum)) \
X(GLenum, glGetError, ()) \
X(GLboolean, glIsEnabled, (GLenum)) \
X(void, glEnable, (GLenum)) \
X(void, glDisable, (GLenum)) \
X(void, glActiveTexture, (GLenum)) \
X(void, glUseProgram, (GLuint)) \
X(void, glBindVertexArray, (GLuint)) \
X(void, glBindBuffer, (GLenum, GLuint)) \
X(void, glBindFramebuffer, (GLenum, GLuint)) \
X(void, glBindRenderbuffer, (GLenum, GLuint)) \
X(void, glBindTexture, (GLenum, GLuint)) \
X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
X(void, glScissor, (GLint, GLint, GLsizei, GLsizei)) \
X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
X(void, glBlendEquationSeparate, (GLenum, GLenum)) \
X(void, glBlendFunc, (GLenum, GLenum)) \
X(void, glBlendEquation, (GLenum)) \
X(void, glDepthFunc, (GLenum)) \
X(void, glDepthMask, (GLboolean)) \
X(void, glStencilFuncSeparate, (GLenum, GLenum, GLint, GLuint)) \
X(void, glStencilOpSeparate, (GLenum, GLenum, GLenum, GLenum)) \
X(void, glStencilMaskSeparate, (GLenum, GLuint)) \
X(void, glStencilMask, (GLuint)) \
X(void, glClearStencil, (GLint)) \
X(void, glFrontFace, (GLenum)) \
X(void, glCullFace, (GLenum)) \
X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
X(void, glClearDepthf, (GLfloat)) \
X(void, glClear, (GLbitfield)) \
X(void, glPixelStorei, (GLenum, GLint)) \
X(GLuint, glCreateShader, (GLenum)) \
X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
X(void, glCompileShader, (GLuint)) \
X(void, glGetShaderiv, (GLuint, GLenum, GLint*)) \
X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
X(void, glDeleteShader, (GLuint)) \
X(GLuint, glCreateProgram, ()) \
X(void, glAttachShader, (GLuint, GLuint)) \
X(void, glLinkProgram, (GLuint)) \
X(void, glGetProgramiv, (GLuint, GLenum, GLint*)) \
X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
X(void, glDeleteProgram, (GLuint)) \
X(void, glGenFramebuffers, (GLsizei, GLuint*)) \
X(void, glGenTextures, (GLsizei, GLuint*)) \
X(void, glDeleteRenderbuffers, (GLsizei, const GLuint*)) \
X(void, glGenRenderbuffers, (GLsizei, GLuint*)) \
X(void, glDeleteBuffers, (GLsizei, const GLuint*)) \
X(void, glGenBuffers, (GLsizei, GLuint*)) \
X(void, glDeleteVertexArrays, (GLsizei, const GLuint*)) \
X(void, glGenVertexArrays, (GLsizei, GLuint*)) \
X(void, glDeleteTextures, (GLsizei, const GLuint*)) \
X(void, glDeleteFramebuffers, (GLsizei, const GLuint*)) \
X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
X(GLenum, glCheckFramebufferStatus, (GLenum)) \
X(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
X(GLint, glGetUniformLocation, (GLuint, const GLchar*)) \
X(void, glUniform1i, (GLint, GLint)) \
X(void, glUniform1f, (GLint, GLfloat)) \
X(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
X(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
X(void, glEnableVertexAttribArray, (GLuint)) \
X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))
#define DECLARE(ret, name, args) inline ret (*name) args = nullptr;
GL_API(DECLARE)
#undef DECLARE
inline void loadGL() {
#define LOAD(ret, name, args) name = reinterpret_cast<decltype(name)>(eglGetProcAddress(#name)); if (!name) { std::fprintf(stderr, "missing %s\n", #name); std::exit(1); }
GL_API(LOAD)
#undef LOAD
}
#undef GL_API
inline constexpr GLenum
GL_FALSE=0, GL_TRUE=1, GL_ZERO=0, GL_ONE=1, GL_NO_ERROR=0,
GL_VERSION=0x1F02, GL_RENDERER=0x1F01,
GL_CURRENT_PROGRAM=0x8B8D, GL_VERTEX_ARRAY_BINDING=0x85B5,
GL_ARRAY_BUFFER_BINDING=0x8894, GL_DRAW_FRAMEBUFFER_BINDING=0x8CA6,
GL_READ_FRAMEBUFFER_BINDING=0x8CAA, GL_RENDERBUFFER_BINDING=0x8CA7,
GL_ACTIVE_TEXTURE=0x84E0, GL_VIEWPORT=0x0BA2, GL_SCISSOR_BOX=0x0C10,
GL_TEXTURE0=0x84C0, GL_TEXTURE_BINDING_2D=0x8069,
GL_TEXTURE_BINDING_EXTERNAL_OES=0x8D67, GL_TEXTURE_BINDING_3D=0x806A,
GL_BLEND_SRC_RGB=0x80C9, GL_BLEND_DST_RGB=0x80C8, GL_BLEND_SRC_ALPHA=0x80CB,
GL_BLEND_DST_ALPHA=0x80CA, GL_BLEND_EQUATION_RGB=0x8009, GL_BLEND_EQUATION_ALPHA=0x883D,
GL_DEPTH_FUNC=0x0B74, GL_STENCIL_FUNC=0x0B92, GL_STENCIL_REF=0x0B97,
GL_STENCIL_VALUE_MASK=0x0B93, GL_STENCIL_FAIL=0x0B94,
GL_STENCIL_PASS_DEPTH_FAIL=0x0B95, GL_STENCIL_PASS_DEPTH_PASS=0x0B96,
GL_STENCIL_WRITEMASK=0x0B98, GL_STENCIL_BACK_FUNC=0x8800,
GL_STENCIL_BACK_REF=0x8CA3, GL_STENCIL_BACK_VALUE_MASK=0x8CA4,
GL_STENCIL_BACK_FAIL=0x8801, GL_STENCIL_BACK_PASS_DEPTH_FAIL=0x8802,
GL_STENCIL_BACK_PASS_DEPTH_PASS=0x8803, GL_STENCIL_BACK_WRITEMASK=0x8CA5,
GL_STENCIL_CLEAR_VALUE=0x0B91, GL_FRONT_FACE=0x0B46, GL_CULL_FACE_MODE=0x0B45,
GL_UNPACK_ALIGNMENT=0x0CF5, GL_PACK_ALIGNMENT=0x0D05,
GL_DEPTH_WRITEMASK=0x0B72, GL_COLOR_WRITEMASK=0x0C23, GL_COLOR_CLEAR_VALUE=0x0C22,
GL_DEPTH_CLEAR_VALUE=0x0B73, GL_BLEND=0x0BE2, GL_SCISSOR_TEST=0x0C11,
GL_DEPTH_TEST=0x0B71, GL_CULL_FACE=0x0B44, GL_STENCIL_TEST=0x0B90,
GL_ARRAY_BUFFER=0x8892, GL_DRAW_FRAMEBUFFER=0x8CA9, GL_READ_FRAMEBUFFER=0x8CA8,
GL_RENDERBUFFER=0x8D41, GL_TEXTURE_2D=0x0DE1, GL_TEXTURE_EXTERNAL_OES=0x8D65,
GL_TEXTURE_3D=0x806F, GL_FRONT=0x0404, GL_BACK=0x0405,
GL_VERTEX_SHADER=0x8B31, GL_FRAGMENT_SHADER=0x8B30,
GL_COMPILE_STATUS=0x8B81, GL_LINK_STATUS=0x8B82, GL_INFO_LOG_LENGTH=0x8B84,
GL_TEXTURE_MIN_FILTER=0x2801, GL_TEXTURE_MAG_FILTER=0x2800,
GL_LINEAR=0x2601, GL_NEAREST=0x2600, GL_TEXTURE_WRAP_S=0x2802, GL_TEXTURE_WRAP_T=0x2803,
GL_CLAMP_TO_EDGE=0x812F, GL_MIRRORED_REPEAT=0x8370,
GL_RGBA8=0x8058, GL_RGBA=0x1908, GL_UNSIGNED_BYTE=0x1401,
GL_FRAMEBUFFER=0x8D40, GL_COLOR_ATTACHMENT0=0x8CE0, GL_FRAMEBUFFER_COMPLETE=0x8CD5,
GL_DEPTH24_STENCIL8=0x88F0, GL_DEPTH_STENCIL_ATTACHMENT=0x821A,
GL_UNPACK_ROW_LENGTH=0x0CF2, GL_UNPACK_SKIP_PIXELS=0x0CF4, GL_UNPACK_SKIP_ROWS=0x0CF3,
GL_FUNC_ADD=0x8006, GL_ONE_MINUS_SRC_ALPHA=0x0303, GL_TRIANGLE_STRIP=0x0005,
GL_MAX_TEXTURE_SIZE=0x0D33, GL_MAX_RENDERBUFFER_SIZE=0x84E8, GL_MAX_SAMPLES=0x8D57,
GL_COLOR_BUFFER_BIT=0x4000, GL_DEPTH_BUFFER_BIT=0x0100, GL_STENCIL_BUFFER_BIT=0x0400,
GL_STATIC_DRAW=0x88E4, GL_FLOAT=0x1406;

#ifndef GL_RENDERBUFFER_WIDTH
#define GL_RENDERBUFFER_WIDTH 0x8D42
#define GL_RENDERBUFFER_HEIGHT 0x8D43
#endif


inline constexpr GLenum GL_SRGB8_ALPHA8=0x8C43, GL_LINEAR_MIPMAP_LINEAR=0x2703, GL_REPEAT=0x2901;
