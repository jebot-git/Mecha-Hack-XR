#pragma once
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#elif defined(_WIN32)
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <string>
// Shared shaders use only the common desktop GL 3.3 / GLES 3.0 feature set.
inline void ironShaderSource(GLuint shader, const char *source) {
#ifdef __ANDROID__
    std::string text(source);
    const std::string version = "#version 330 core";
    if (text.starts_with(version))
        text.replace(0, version.size(), "#version 300 es\nprecision highp float;\nprecision highp int;");
    const char *code = text.c_str();
    glShaderSource(shader, 1, &code, nullptr);
#else
    glShaderSource(shader, 1, &source, nullptr);
#endif
}
