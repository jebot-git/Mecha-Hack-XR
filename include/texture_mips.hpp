#pragma once
#include "graphics.hpp"

namespace iron {
// Filtering limits the sampled LOD on packed sheets. Always allocate the
// complete chain so changing the sampler can never expose undefined levels.
inline void generateTextureMips(GLenum target, GLenum minFilter, float maxLod=1000.f) {
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 1000);
    glGenerateMipmap(target);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameterf(target, GL_TEXTURE_MAX_LOD, maxLod);
}

inline void uploadRgbaTexture(int width, int height, const void* pixels,
                              GLenum minFilter=GL_NEAREST_MIPMAP_LINEAR, float maxLod=1000.f) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    generateTextureMips(GL_TEXTURE_2D, minFilter, maxLod);
}

// Each aligned 32x32 PSX tile owns one texel at level five.
inline void uploadModelAtlas(const void* pixels) {
    uploadRgbaTexture(256, 256, pixels, GL_NEAREST_MIPMAP_LINEAR, 5);
}
}
