// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h> // kMaxCustomParams / kMaxCustomColors

#include <QVariant> // QVariantMap
#include <QVector4D>

#include <epoxy/gl.h>

#include <array>
#include <memory>

class QSize;

namespace KWin {
class GLTexture;
class RenderTarget;
class RenderViewport;
}

namespace PhosphorAnimationShaders {
struct AnimationShaderEffect;
}

namespace PlasmaZones {

/// Helpers shared by the SCREEN-LEVEL transition passes — the desktop
/// switch/peek blend (DesktopTransitionManager) and the scrolling strip pass
/// (StripTransitionManager). Both capture per-output scenes into
/// output-sized FBOs and draw one full-screen quad through a pack shader, so
/// the capture-format, texture-allocation, quad and pack-parameter plumbing
/// is identical by construction. Lifted here rather than duplicated; the
/// per-window transition path (ShaderTransitionManager) shares none of this
/// shape and stays separate.
namespace TransitionPass {

/// Capture textures inherit the ON-SCREEN target's internal format instead of
/// hardcoding GL_RGBA8. On an HDR/wide-gamut output the target is a float
/// format whose values are scene-referred against the display's peak
/// luminance, so 8-bit sRGB values written verbatim land dim and desaturated —
/// a desktop switch flashed the wrong brightness. Inheriting the target's
/// format is the idiom KWin's own blur and screen-transform use. Every format
/// KWin maps a DRM format to carries alpha, so this never silently drops it.
///
/// Reached through framebuffer() rather than RenderTarget::texture(): that
/// accessor dereferences the framebuffer unconditionally, and it is null on an
/// image-backed target. PlasmaZonesEffect::supported() now requires OpenGL
/// compositing so that cannot happen, but this stays honest rather than
/// resting on a guarantee made in another file.
GLenum captureFormatFor(const KWin::RenderTarget& outputTarget);

/// Allocate a capture texture of @p deviceSize in @p internalFormat (LINEAR
/// filter, CLAMP_TO_EDGE) — the shared preamble of every per-output capture.
/// Returns null when the size is empty or GL allocation fails.
std::unique_ptr<KWin::GLTexture> allocateOutputTexture(const QSize& deviceSize, GLenum internalFormat);

/// Draw a full-screen quad in the RenderViewport's DEVICE coordinate space
/// (logical pixels × scale, y-down), projected by viewport.projectionMatrix()
/// which the caller must have already uploaded as the bound shader's MVP.
///
/// Texcoords are pinned to SCREEN corners, so `uv` stays TOP-DOWN (uv.y == 0
/// at the top of the output) whatever the output transform is. That is the
/// space the transition GLSL modules' Y-flipping samplers
/// (getFromColor/getToColor, getStripColor) undo the capture FBO's Y-up
/// origin against — so the fragment stage and the packs need no change.
///
/// The pairing matters: emitting clip-space directly happened to give the
/// same top-down uv only because the default target transform is FlipY.
/// Re-deriving it from screen corners is what keeps that true once the
/// projection is applied.
void drawOutputQuad(const KWin::RenderViewport& viewport);

/// The GLSL vertex stage shared by the screen-level passes. This is what
/// drawOutputQuad's vertices feed into. Positions arrive in the
/// RenderViewport's device coordinate space and are projected by KWin's own
/// matrix, which encodes RenderTarget::transform() (the output rotation/flip,
/// combined with the buffer's FlipY) and the render offset. Emitting
/// clip-space directly is only equivalent when the transform is exactly FlipY
/// and the offset is zero — the default, unrotated configuration; on a
/// rotated output the target framebuffer is panel-oriented while the captures
/// are logical-oriented, so a pass that skipped the projection painted
/// unrotated and stretched. Callers splice their KWin define in themselves
/// (ShaderInternal::injectKwinDefineAfterVersion).
const char* outputQuadVertexSource();

/// Resolve p_<name> parameter values into the customParams[] / customColors[]
/// slot pools. translateAnimationParams fills the metadata defaults when the
/// profile carries no override — WITHOUT this the shaders run at
/// customParams == 0 (slide has no direction, dissolve no speckle scale,
/// etc.) and appear broken. Color params land as normalised rgba, exactly as
/// the per-window transition path uploads them (see shader_transitions.cpp);
/// translateAnimationParams coerces every color to a valid QColor (default →
/// Qt::transparent), so the isValid guard is defence-in-depth against a
/// caller that bypasses the registry encoder.
void translatePackParams(
    const PhosphorAnimationShaders::AnimationShaderEffect& eff, const QVariantMap& params,
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams>& customParams,
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors>& customColors);

} // namespace TransitionPass

} // namespace PlasmaZones
