// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h>

#include <QRectF>
#include <QVector2D>
#include <QtGlobal>

#include <array>
#include <chrono>

namespace PlasmaZones::ShaderInternal {

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
///   • Anchor extent (default): the render target covers the window 1:1,
///     so `resolution == anchorSize` and the anchor sits at the origin.
///   • Surface extent (`fboExtent: "surface"`): the shader paints over the
///     whole output, so `resolution` is the output size, `anchorSize`
///     stays the window size, and `anchorPosInFbo` locates the window's
///     top-left within the output. Surface-extent shaders read these (via
///     `anchorRemap`) to composite the window into its sub-region.
struct AnchorUniforms
{
    QVector2D resolution; ///< iResolution
    QVector2D anchorSize; ///< iAnchorSize
    QVector2D anchorPosInFbo; ///< iAnchorPosInFbo
};

/// Compute the anchor uniforms for a paintWindow tick. Pure geometry:
/// `windowFrame` and `outputGeometry` are logical-pixel rects at 1:1
/// scale (quad-list space and screen space coincide on the kwin path).
/// See `AnchorUniforms` for the two extent modes.
inline AnchorUniforms computeAnchorUniforms(const QRectF& windowFrame, const QRectF& outputGeometry, bool surfaceExtent)
{
    const QVector2D windowSize(static_cast<float>(windowFrame.width()), static_cast<float>(windowFrame.height()));
    if (!surfaceExtent) {
        return {windowSize, windowSize, QVector2D(0.0f, 0.0f)};
    }
    const QVector2D outputSize(static_cast<float>(outputGeometry.width()), static_cast<float>(outputGeometry.height()));
    const QVector2D anchorPos(static_cast<float>(windowFrame.x() - outputGeometry.x()),
                              static_cast<float>(windowFrame.y() - outputGeometry.y()));
    return {outputSize, windowSize, anchorPos};
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
