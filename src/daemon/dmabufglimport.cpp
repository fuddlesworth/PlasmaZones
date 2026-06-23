// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dmabufglimport.h"
#include "../core/logging.h"

// epoxy provides the EGL/GL entry points (eglCreateImageKHR,
// glEGLImageTargetTexture2DOES, the EGL_EXT_image_dma_buf_import tokens) and
// must precede any other GL/EGL include. This TU intentionally pulls in NO Qt
// GL headers so epoxy can't clash with them.
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include <vector>

namespace PlasmaZones {

// DrmFormatModInvalid is shared via dmabufthumbnail.h (PlasmaZones namespace).

GlDmabufImport importDmabufToGlTexture(const DmabufThumbnailDesc& desc)
{
    GlDmabufImport result;
    if (desc.fd < 0 || desc.width <= 0 || desc.height <= 0) {
        return result;
    }
    // stride/offset are uint32, but the EGL_LINUX_DMA_BUF attribute list takes
    // EGLint (signed 32-bit). A value >= 2^31 would narrow to a negative attrib
    // and hand EGL a garbage plane layout. The producer controls these
    // independently of width/height, so reject out-of-range values rather than
    // silently corrupt the import.
    if (desc.stride > 0x7fffffffu || desc.offset > 0x7fffffffu) {
        qCWarning(lcOverlay) << "dmabuf GL import: stride/offset exceed EGLint range";
        return result;
    }
    EGLDisplay dpy = eglGetCurrentDisplay();
    if (dpy == EGL_NO_DISPLAY) {
        qCWarning(lcOverlay) << "dmabuf GL import: no current EGL display";
        return result;
    }

    // EGL_LINUX_DMA_BUF_EXT consumes the DRM FourCC directly (not a VkFormat).
    std::vector<EGLint> attrs = {
        EGL_WIDTH,
        desc.width,
        EGL_HEIGHT,
        desc.height,
        EGL_LINUX_DRM_FOURCC_EXT,
        static_cast<EGLint>(desc.fourcc),
        EGL_DMA_BUF_PLANE0_FD_EXT,
        desc.fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,
        static_cast<EGLint>(desc.offset),
        EGL_DMA_BUF_PLANE0_PITCH_EXT,
        static_cast<EGLint>(desc.stride),
    };
    if (desc.modifier != DrmFormatModInvalid) {
        // Requires EGL_EXT_image_dma_buf_import_modifiers.
        attrs.push_back(EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT);
        attrs.push_back(static_cast<EGLint>(desc.modifier & 0xffffffffULL));
        attrs.push_back(EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT);
        attrs.push_back(static_cast<EGLint>(desc.modifier >> 32));
    }
    attrs.push_back(EGL_NONE);

    // No ownership of desc.fd is taken: the EGLImage references the buffer
    // independently, so the caller's fd stays valid/owned.
    const EGLImageKHR image = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                                static_cast<EGLClientBuffer>(nullptr), attrs.data());
    if (image == EGL_NO_IMAGE_KHR) {
        qCWarning(lcOverlay) << "dmabuf GL import: eglCreateImageKHR(LINUX_DMA_BUF) failed";
        return result;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Clear any stale GL error so the post-bind check reflects only this call,
    // then verify the EGLImage actually bound — a driver can reject the format
    // here, and returning ok=true with an unbacked texture would silently copy
    // garbage. Mirror the Vulkan path's per-step result checking.
    while (glGetError() != GL_NO_ERROR) { }
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, static_cast<GLeglImageOES>(image));
    const GLenum bindError = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);
    if (bindError != GL_NO_ERROR) {
        qCWarning(lcOverlay) << "dmabuf GL import: glEGLImageTargetTexture2DOES failed, GL error 0x"
                             << QString::number(bindError, 16);
        glDeleteTextures(1, &texture);
        eglDestroyImageKHR(dpy, image);
        return result;
    }

    result.ok = true;
    result.textureId = texture;
    result.release = [dpy, image, texture]() {
        GLuint t = texture;
        glDeleteTextures(1, &t);
        eglDestroyImageKHR(dpy, image);
    };
    return result;
}

} // namespace PlasmaZones
