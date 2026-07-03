// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h>

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVector2D>
#include <QVector4D>
#include <QtGlobal>

#include <array>
#include <chrono>

namespace PlasmaZones::ShaderInternal {

/// Splice `#define PLASMAZONES_KWIN` (plus the ARB explicit-location
/// extension enables) after the shader's `#version` directive, selecting the
/// classic-GL default-block branch of the shared uniform headers that
/// KWin::GLShader requires. Shared by the animation compile path
/// (shader_transitions.cpp, where it is defined) and the surface-pack compile
/// path (surface_compile.cpp); it has external linkage here rather than an
/// anonymous-namespace copy per TU because the kwin-effect builds as a Unity
/// (jumbo) target, where duplicate anonymous-namespace definitions collide.
/// Full behavioural notes (BOM strip, comment-aware #version scan, missing
/// #version synthesis) live at the definition.
QByteArray injectKwinDefineAfterVersion(const QString& source);

/// Monotonic milliseconds since steady_clock epoch. Used by time-based shader
/// transitions for elapsed-progress math. We deliberately avoid
/// QDateTime::currentMSecsSinceEpoch — wall-clock isn't monotonic, and an NTP
/// adjustment mid-transition can run elapsed backwards (or jump it past the
/// duration) and either freeze or prematurely tear down the shader leg.
/// std::chrono::steady_clock matches the clock the phosphor-animation-layer
/// SurfaceAnimator already uses for its motion ticks.
inline qint64 shaderClockNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/// Animation-shader anchor uniforms: where the captured window ("anchor")
/// sits inside the shader's render target ("FBO").
///
///   • Anchor extent (default): the FBO covers the anchor (frame). The
///     anchor sits at the origin and `resolution == anchorSize`.
///     EXCEPT on the kwin path the redirected FBO covers the EXPANDED
///     geometry (frame + decoration + shadow), so `resolution` is the
///     expanded size and `anchorPosInFbo` is the shadow inset. A shader
///     sampling the FBO directly would otherwise treat the shadow edge
///     as the window edge; shaders fold the FBO back into the window's
///     own [0,1] space via `anchorRemap`.
///   • Surface extent (`fboExtent: "surface"`): the shader paints over the
///     whole output, so `resolution` is the output size, `anchorSize`
///     stays the window size, and `anchorPosInFbo` locates the window's
///     top-left within the output.
struct AnchorUniforms
{
    QVector2D resolution; ///< iResolution
    QVector2D anchorSize; ///< iAnchorSize
    QVector2D anchorPosInFbo; ///< iAnchorPosInFbo
};

/// Compute the anchor uniforms for a paintWindow tick. Pure geometry:
/// `anchor` is the captured-window rect (frame), `texture` is the rect
/// the shader's vTexCoord [0,1] covers, both in logical pixels at 1:1
/// scale (quad-list space and screen space coincide on the kwin path).
/// `iResolution` is the texture size, `iAnchorSize` is the anchor size,
/// and `iAnchorPosInFbo` locates the anchor's top-left within the
/// texture in logical pixels. When the texture equals the anchor (the
/// daemon's anchor-extent path), the uniforms collapse to
/// `{anchor, anchor, 0}` and `anchorRemap` reduces to identity.
inline AnchorUniforms computeAnchorUniforms(const QRectF& anchor, const QRectF& texture)
{
    const QVector2D textureSize(static_cast<float>(qMax(texture.width(), 1.0)),
                                static_cast<float>(qMax(texture.height(), 1.0)));
    const QVector2D anchorSize(static_cast<float>(qMax(anchor.width(), 1.0)),
                               static_cast<float>(qMax(anchor.height(), 1.0)));
    const QVector2D anchorPos(static_cast<float>(anchor.x() - texture.x()),
                              static_cast<float>(anchor.y() - texture.y()));
    return {textureSize, anchorSize, anchorPos};
}

/// UV sub-rect of `inner` within `outer`, as `(x, y, width, height)` in
/// `outer`'s [0,1] space. Drives the `iAnchorRectInTexture` uniform:
/// `outer` is uTexture0's backing rect — on KWin always the redirected
/// expanded-geometry FBO (frame + decoration + shadow) — and `inner` is
/// the rect the shader's `surfaceColor()` argument addresses. Surface-
/// extent transitions pass `(frame, expanded)` so `surfaceColor()` maps
/// an anchor-[0,1] coordinate onto the frame's sub-region of the shadow-
/// padded texture; anchor-extent transitions pass `(expanded, expanded)`
/// for the `(0, 0, 1, 1)` identity. Widths/heights are clamped at 1.0 to
/// keep the divisions safe on a degenerate zero-size window.
inline QVector4D computeTextureSubRect(const QRectF& inner, const QRectF& outer)
{
    const float outerW = static_cast<float>(qMax(outer.width(), 1.0));
    const float outerH = static_cast<float>(qMax(outer.height(), 1.0));
    return QVector4D(static_cast<float>(inner.x() - outer.x()) / outerW,
                     static_cast<float>(inner.y() - outer.y()) / outerH, static_cast<float>(inner.width()) / outerW,
                     static_cast<float>(inner.height()) / outerH);
}

/// Pre-baked uniform / param key strings for the hot paths.
///
/// `customParams[0..7]` and `uTexture1..3` (and their wrap / svgSize
/// variants) are looked up dozens of times per shader transition install
/// and per paintWindow tick. Building those names with `QStringLiteral(...).arg(slot)`
/// allocates a fresh `QString` per call; pre-baking them as `const char*`
/// (for `uniformLocation` calls that take `const char*`) and `QLatin1String`
/// (for `QVariantMap::value` / `find` calls keyed by `QString`) eliminates
/// the per-frame allocations entirely. Sized to the contract budgets so a
/// future bump triggers a compile-time mismatch rather than a silent
/// out-of-range read.
inline constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots>
    kUserTextureSamplerNames = {"uTexture1", "uTexture2", "uTexture3"};
inline constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots>
    kUserTextureWrapKeys = {"uTexture1_wrap", "uTexture2_wrap", "uTexture3_wrap"};
inline constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots>
    kITextureResolutionKeys = {"iTextureResolution[0]", "iTextureResolution[1]", "iTextureResolution[2]"};
inline constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams>
    kCustomParamsElementNames = {"customParams[0]", "customParams[1]", "customParams[2]", "customParams[3]",
                                 "customParams[4]", "customParams[5]", "customParams[6]", "customParams[7]"};
// Worst-case 16 slots — sized to `kMaxCustomColors`. If that ever rises
// the array literal must grow; the static_assert below pins the length.
inline constexpr std::array<const char*, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors>
    kCustomColorsElementNames = {"customColors[0]",  "customColors[1]",  "customColors[2]",  "customColors[3]",
                                 "customColors[4]",  "customColors[5]",  "customColors[6]",  "customColors[7]",
                                 "customColors[8]",  "customColors[9]",  "customColors[10]", "customColors[11]",
                                 "customColors[12]", "customColors[13]", "customColors[14]", "customColors[15]"};
static_assert(PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams == 8,
              "kCustomParamsElementNames literal must grow to match kMaxCustomParams");
static_assert(PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors == 16,
              "kCustomColorsElementNames literal must grow to match kMaxCustomColors");
static_assert(PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots == 3,
              "User-texture name arrays must grow to match kMaxUserTextureSlots");

} // namespace PlasmaZones::ShaderInternal
