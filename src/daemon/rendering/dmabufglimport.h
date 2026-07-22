// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/types/dmabufthumbnail.h"

#include <cstdint>
#include <functional>

namespace PlasmaZones {

/**
 * @brief Result of importing a dma-buf into a GL texture (OpenGL RHI fallback).
 *
 * @c textureId is a GL_TEXTURE_2D name bound to the imported dma-buf via
 * EGL_EXT_image_dma_buf_import; it is wrapped as a QRhiTexture transfer source.
 * @c release frees the GL texture + EGLImage and MUST be called with the GL
 * context current (the render thread, in the owning QSGTexture's destructor).
 */
struct GlDmabufImport
{
    bool ok = false;
    uint32_t textureId = 0;
    std::function<void()> release;
};

/**
 * @brief Import a single-plane dma-buf into a GL_TEXTURE_2D via EGL.
 *
 * Must be called on the scene-graph render thread with the OpenGL-RHI context
 * current (the import resolves the daemon's GL backend via
 * eglGetCurrentDisplay(); the EGLImage itself is created with EGL_NO_CONTEXT).
 * Does not take ownership of @c desc.fd (EGL references the buffer
 * independently; the caller still owns/closes the fd). Returns {ok=false} on
 * any failure. Isolated in its own TU so the EGL/GL (epoxy) headers never mix
 * with the Vulkan + Qt RHI headers in dmabuftextureprovider.cpp.
 */
GlDmabufImport importDmabufToGlTexture(const DmabufThumbnailDesc& desc);

} // namespace PlasmaZones
