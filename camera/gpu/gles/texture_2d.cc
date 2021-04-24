/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/gles/texture_2d.h"

#include <utility>

#include "cros-camera/common.h"
#include "gpu/gles/utils.h"

PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_glEGLImageTargetTexture2DOES = nullptr;

namespace cros {

// static
bool Texture2D::IsExternalTextureSupported() {
  static bool supported = []() -> bool {
    g_glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    return g_glEGLImageTargetTexture2DOES != nullptr;
  }();
  return supported;
}

Texture2D::Texture2D(Target target, const EglImage& egl_image)
    : width_(egl_image.width()), height_(egl_image.height()) {
  target_ = [target]() {
    switch (target) {
      case Texture2D::Target::kTarget2D:
        return GL_TEXTURE_2D;
      case Texture2D::Target::kTargetExternal:
        return GL_TEXTURE_EXTERNAL_OES;
    }
  }();

  if (!IsExternalTextureSupported()) {
    LOGF(ERROR) << "Creating external texture isn't supported";
    return;
  }

  glGenTextures(1, &id_);
  if (id_ == 0) {
    LOGF(ERROR) << "Failed to generate texture";
    return;
  }

  Bind();
  g_glEGLImageTargetTexture2DOES(target_, egl_image.handle());
  GLenum result = glGetError();
  if (result != GL_NO_ERROR) {
    LOGF(ERROR) << "Failed to bind external EGL image: "
                << GlGetErrorString(result);
    Invalidate();
    return;
  }
  Unbind();
}

Texture2D::Texture2D(GLenum internal_format,
                     int width,
                     int height,
                     int mipmap_levels)
    : target_(GL_TEXTURE_2D), width_(width), height_(height) {
  glGenTextures(1, &id_);
  GLenum result = glGetError();
  if (result != GL_NO_ERROR) {
    LOGF(ERROR) << "Failed to generate texture: " << GlGetErrorString(result);
    return;
  }

  glBindTexture(target_, id_);
  glTexStorage2D(target_, mipmap_levels, internal_format, width_, height_);
  result = glGetError();
  if (result != GL_NO_ERROR) {
    LOGF(ERROR) << "Failed to configure texture storage: "
                << GlGetErrorString(result);
    Invalidate();
    return;
  }
  glBindTexture(target_, 0);
}

Texture2D::Texture2D(Texture2D&& other) {
  *this = std::move(other);
}

Texture2D& Texture2D::operator=(Texture2D&& other) {
  if (this != &other) {
    Invalidate();
    target_ = other.target_;
    id_ = other.id_;
    width_ = other.width_;
    height_ = other.height_;

    other.target_ = 0;
    other.id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
  }
  return *this;
}

Texture2D::~Texture2D() {
  Invalidate();
}

void Texture2D::Bind() const {
  glBindTexture(target_, id_);
}

void Texture2D::Unbind() const {
  glBindTexture(target_, 0);
}

void Texture2D::Invalidate() {
  if (IsValid()) {
    glDeleteTextures(1, &id_);
    id_ = 0;
  }
}

}  // namespace cros
