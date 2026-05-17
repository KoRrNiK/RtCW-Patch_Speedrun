/*
===========================================================================

Renderer2 OpenGL 4 loader.

===========================================================================
*/

#ifndef R2_GL_H
#define R2_GL_H

#include "r2_local.h"
#include "../sdl/sdl_local.h"
#include "../sys/core/sys_local.h"
#include "../sys/platform/windows/sys_glw_windows.h"
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_CONTEXT_FLAGS
#define GL_CONTEXT_FLAGS 0x821E
#endif
#ifndef GL_CONTEXT_PROFILE_MASK
#define GL_CONTEXT_PROFILE_MASK 0x9126
#endif
#ifndef GL_CONTEXT_CORE_PROFILE_BIT
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

typedef void ( APIENTRY * r2_glGenVertexArraysProc )( GLsizei n, GLuint *arrays );
typedef void ( APIENTRY * r2_glBindVertexArrayProc )( GLuint array );
typedef void ( APIENTRY * r2_glDeleteVertexArraysProc )( GLsizei n, const GLuint *arrays );
typedef void ( APIENTRY * r2_glGenBuffersProc )( GLsizei n, GLuint *buffers );
typedef void ( APIENTRY * r2_glBindBufferProc )( GLenum target, GLuint buffer );
typedef void ( APIENTRY * r2_glBufferDataProc )( GLenum target, GLsizeiptr size, const void *data, GLenum usage );
typedef void ( APIENTRY * r2_glBufferSubDataProc )( GLenum target, GLintptr offset, GLsizeiptr size, const void *data );
typedef void ( APIENTRY * r2_glDeleteBuffersProc )( GLsizei n, const GLuint *buffers );
typedef GLuint ( APIENTRY * r2_glCreateShaderProc )( GLenum type );
typedef void ( APIENTRY * r2_glShaderSourceProc )( GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length );
typedef void ( APIENTRY * r2_glCompileShaderProc )( GLuint shader );
typedef void ( APIENTRY * r2_glGetShaderivProc )( GLuint shader, GLenum pname, GLint *params );
typedef void ( APIENTRY * r2_glGetShaderInfoLogProc )( GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog );
typedef void ( APIENTRY * r2_glDeleteShaderProc )( GLuint shader );
typedef GLuint ( APIENTRY * r2_glCreateProgramProc )( void );
typedef void ( APIENTRY * r2_glAttachShaderProc )( GLuint program, GLuint shader );
typedef void ( APIENTRY * r2_glLinkProgramProc )( GLuint program );
typedef void ( APIENTRY * r2_glGetProgramivProc )( GLuint program, GLenum pname, GLint *params );
typedef void ( APIENTRY * r2_glGetProgramInfoLogProc )( GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog );
typedef void ( APIENTRY * r2_glUseProgramProc )( GLuint program );
typedef void ( APIENTRY * r2_glDeleteProgramProc )( GLuint program );
typedef GLint ( APIENTRY * r2_glGetUniformLocationProc )( GLuint program, const GLchar *name );
typedef void ( APIENTRY * r2_glUniform1iProc )( GLint location, GLint v0 );
typedef void ( APIENTRY * r2_glUniform1fProc )( GLint location, GLfloat v0 );
typedef void ( APIENTRY * r2_glUniform2fProc )( GLint location, GLfloat v0, GLfloat v1 );
typedef void ( APIENTRY * r2_glUniformMatrix4fvProc )( GLint location, GLsizei count, GLboolean transpose, const GLfloat *value );
typedef void ( APIENTRY * r2_glActiveTextureProc )( GLenum texture );
typedef void ( APIENTRY * r2_glGenerateMipmapProc )( GLenum target );
typedef void ( APIENTRY * r2_glVertexAttribPointerProc )( GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer );
typedef void ( APIENTRY * r2_glEnableVertexAttribArrayProc )( GLuint index );
typedef void ( APIENTRY * r2_glDisableVertexAttribArrayProc )( GLuint index );
typedef void ( APIENTRY * r2_glMultiDrawArraysProc )( GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount );
typedef const GLubyte *( APIENTRY * r2_glGetStringiProc )( GLenum name, GLuint index );

typedef struct {
	r2_glGenVertexArraysProc GenVertexArrays;
	r2_glBindVertexArrayProc BindVertexArray;
	r2_glDeleteVertexArraysProc DeleteVertexArrays;
	r2_glGenBuffersProc GenBuffers;
	r2_glBindBufferProc BindBuffer;
	r2_glBufferDataProc BufferData;
	r2_glBufferSubDataProc BufferSubData;
	r2_glDeleteBuffersProc DeleteBuffers;
	r2_glCreateShaderProc CreateShader;
	r2_glShaderSourceProc ShaderSource;
	r2_glCompileShaderProc CompileShader;
	r2_glGetShaderivProc GetShaderiv;
	r2_glGetShaderInfoLogProc GetShaderInfoLog;
	r2_glDeleteShaderProc DeleteShader;
	r2_glCreateProgramProc CreateProgram;
	r2_glAttachShaderProc AttachShader;
	r2_glLinkProgramProc LinkProgram;
	r2_glGetProgramivProc GetProgramiv;
	r2_glGetProgramInfoLogProc GetProgramInfoLog;
	r2_glUseProgramProc UseProgram;
	r2_glDeleteProgramProc DeleteProgram;
	r2_glGetUniformLocationProc GetUniformLocation;
	r2_glUniform1iProc Uniform1i;
	r2_glUniform1fProc Uniform1f;
	r2_glUniform2fProc Uniform2f;
	r2_glUniformMatrix4fvProc UniformMatrix4fv;
	r2_glActiveTextureProc ActiveTexture;
	r2_glGenerateMipmapProc GenerateMipmap;
	r2_glVertexAttribPointerProc VertexAttribPointer;
	r2_glEnableVertexAttribArrayProc EnableVertexAttribArray;
	r2_glDisableVertexAttribArrayProc DisableVertexAttribArray;
	r2_glMultiDrawArraysProc MultiDrawArrays;
	r2_glGetStringiProc GetStringi;
} r2GLProcs_t;

extern r2GLProcs_t r2gl;

qboolean R2_GL_LoadCoreProcs( void );

#endif /* R2_GL_H */
