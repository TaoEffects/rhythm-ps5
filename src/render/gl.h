// SPDX-License-Identifier: GPL-3.0-or-later
// OpenGL 3.3 core. Sur PS5 les prototypes viennent du SDK ps5-opengl (lien statique) ;
// sur Linux de la libGL de Mesa. Dans les deux cas on appelle les fonctions directement.
#pragma once

#if defined(_WIN32)
// Windows : opengl32.dll ne fournit que GL 1.1 ; le reste est chargé à la
// création du contexte (gl::loadFunctions), via les pointeurs ci-dessous.
#include <GL/gl.h>
#include <GL/glext.h>
#define RHYTHM_GL_LOADED(X) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)
#define X(type, name) extern type rhythm_##name;
RHYTHM_GL_LOADED(X)
#undef X
#define glActiveTexture rhythm_glActiveTexture
#define glAttachShader rhythm_glAttachShader
#define glBindBuffer rhythm_glBindBuffer
#define glBindFramebuffer rhythm_glBindFramebuffer
#define glBindVertexArray rhythm_glBindVertexArray
#define glBlendFuncSeparate rhythm_glBlendFuncSeparate
#define glBufferData rhythm_glBufferData
#define glCheckFramebufferStatus rhythm_glCheckFramebufferStatus
#define glCompileShader rhythm_glCompileShader
#define glCreateProgram rhythm_glCreateProgram
#define glCreateShader rhythm_glCreateShader
#define glDeleteBuffers rhythm_glDeleteBuffers
#define glDeleteFramebuffers rhythm_glDeleteFramebuffers
#define glDeleteProgram rhythm_glDeleteProgram
#define glDeleteShader rhythm_glDeleteShader
#define glDeleteVertexArrays rhythm_glDeleteVertexArrays
#define glEnableVertexAttribArray rhythm_glEnableVertexAttribArray
#define glFramebufferTexture2D rhythm_glFramebufferTexture2D
#define glGenBuffers rhythm_glGenBuffers
#define glGenFramebuffers rhythm_glGenFramebuffers
#define glGenVertexArrays rhythm_glGenVertexArrays
#define glGetProgramInfoLog rhythm_glGetProgramInfoLog
#define glGetProgramiv rhythm_glGetProgramiv
#define glGetShaderInfoLog rhythm_glGetShaderInfoLog
#define glGetShaderiv rhythm_glGetShaderiv
#define glGetUniformLocation rhythm_glGetUniformLocation
#define glLinkProgram rhythm_glLinkProgram
#define glShaderSource rhythm_glShaderSource
#define glUniform1f rhythm_glUniform1f
#define glUniform1i rhythm_glUniform1i
#define glUniform2f rhythm_glUniform2f
#define glUseProgram rhythm_glUseProgram
#define glVertexAttribPointer rhythm_glVertexAttribPointer
namespace gl {
// Charge les fonctions au-delà de GL 1.1 ; renvoie false s'il en manque.
bool loadFunctions(void *(*getProc)(const char *));
}
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif
