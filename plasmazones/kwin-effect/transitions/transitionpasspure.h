// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <epoxy/gl.h>

/// The parts of TransitionPass that are pure data with no KWin type in their
/// signature, header-only so the unit tests can include them straight from
/// the effect tree (the same shape as StripMotionSampler). Everything that
/// takes a RenderTarget or touches GL lives in transitionpasshelpers.h and
/// calls through to here.
namespace PlasmaZones {

namespace TransitionPass {

/// The alpha-capable capture format for an on-screen target whose own
/// internal format is @p targetFormat. Any float or wide (10-bit and up)
/// target maps to GL_RGBA16F so an HDR intermediate loses no headroom,
/// everything else to GL_RGBA8. GL_RGB10_A2 is on the wide list on purpose:
/// it is what KWin hands a 10-bit SDR output, and its 2-bit alpha cannot
/// carry coverage. The promotion doubles the capture's bandwidth on such an
/// output (16 bits per channel instead of 8) for one full-screen texture, one
/// full-screen copy and one alpha-only clear per frame of a scroll leg,
/// which is accepted so a 10-bit desktop keeps its precision through the
/// pass rather than banding for the length of every scroll.
inline GLenum alphaCaptureFormatForInternalFormat(GLenum targetFormat)
{
    switch (targetFormat) {
    case GL_RGBA16F:
    case GL_RGB16F:
    case GL_RGBA32F:
    case GL_RGB32F:
    case GL_R11F_G11F_B10F:
    case GL_RGB10_A2:
    case GL_RGBA16:
    case GL_RGB16:
        return GL_RGBA16F;
    default:
        return GL_RGBA8;
    }
}

} // namespace TransitionPass

} // namespace PlasmaZones
