// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/Curve.h>

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVariant>
#include <QVector2D>
#include <QVector4D>
#include <QtGlobal>

#include <array>
#include <chrono>

#include <epoxy/gl.h>

namespace PlasmaZones::ShaderInternal {

/// Save/restore the global GL state the offscreen decoration folds perturb —
/// blend enable + separate blend funcs, viewport, clear colour, active texture
/// unit. The folds (renderSurfaceChainComposite, which inlines the buffer
/// passes, plus the snapshot captures) run offscreen passes plus a nested
/// effects->drawWindow immediately BEFORE KWin's on-screen draw of the same
/// frame, and KWin's convention is that an effect hands blend/viewport back
/// the way it found them; without this guard the first frame after a fold
/// inherits whatever state the fold's last inner draw left (a disabled
/// GL_BLEND renders the animation shader's premultiplied output as OPAQUE
/// BLACK across the whole quad — the close-animation black-flash class).
/// Header-inline so both consuming TUs share one definition under the Unity
/// build.
class ScopedGlState
{
public:
    ScopedGlState()
    {
        m_blendEnabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_SRC_RGB, &m_blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &m_blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &m_blendDstAlpha);
        glGetIntegerv(GL_VIEWPORT, m_viewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, m_clearColor);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
        // OffscreenData::paint toggles GL_SCISSOR_TEST — the nested
        // effects->drawWindow inside a fold can flip it, so it belongs in
        // the snapshot alongside blend.
        m_scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_SCISSOR_BOX, m_scissorBox);
    }
    ~ScopedGlState()
    {
        if (m_blendEnabled) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        glBlendFuncSeparate(static_cast<GLenum>(m_blendSrcRgb), static_cast<GLenum>(m_blendDstRgb),
                            static_cast<GLenum>(m_blendSrcAlpha), static_cast<GLenum>(m_blendDstAlpha));
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
        glActiveTexture(static_cast<GLenum>(m_activeTexture));
        if (m_scissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        glScissor(m_scissorBox[0], m_scissorBox[1], m_scissorBox[2], m_scissorBox[3]);
    }
    ScopedGlState(const ScopedGlState&) = delete;
    ScopedGlState& operator=(const ScopedGlState&) = delete;

private:
    GLboolean m_blendEnabled = GL_FALSE;
    GLint m_blendSrcRgb = GL_ONE;
    GLint m_blendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint m_blendSrcAlpha = GL_ONE;
    GLint m_blendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint m_viewport[4] = {0, 0, 0, 0};
    GLfloat m_clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLint m_activeTexture = GL_TEXTURE0;
    GLboolean m_scissorEnabled = GL_FALSE;
    GLint m_scissorBox[4] = {0, 0, 0, 0};
};

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

} // namespace PlasmaZones::ShaderInternal

namespace PhosphorSurfaceShaders {
struct SurfaceShaderEffect;
}

namespace PlasmaZones {
struct SurfaceParamValues; // types.h

namespace ShaderInternal {

/// Translate a surface pack's declared parameter defaults merged with a
/// DecorationProfile's per-pack friendly overrides into contract-slot uniform
/// values (SurfaceParamValues, types.h). Defined in surface_compile.cpp;
/// external linkage for the same Unity-build reason as
/// injectKwinDefineAfterVersion. Used by compiledPack() for the baseline bake
/// and by updateWindowDecoration() for the per-window values.
SurfaceParamValues resolveSurfaceParamValues(const PhosphorSurfaceShaders::SurfaceShaderEffect& eff,
                                             const QVariantMap& friendlyOverrides);

} // namespace ShaderInternal
} // namespace PlasmaZones

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

/// The lifetime (ms) a shader transition should run for, given the motion
/// cascade's resolved @p nominalMs and its timing @p curve.
///
/// A STATEFUL (spring) curve derives its own timeline from its physics rather
/// than the duration slider — mirroring the settings UI's "Spring mode derives
/// its own duration" — so it runs for the spring's analytical settle time and
/// rings out under the paint pass's per-frame step() instead of being cut at
/// the easing duration.
///
/// Every result is clamped into the animation envelope. Both callers now feed an
/// already-clamped nominal (resolveEventMotionProfile bounds the motion cascade's
/// duration at the source), so for a STATELESS curve this qBound is idempotent.
/// It stays load-bearing for the SPRING path, whose settleTime() is derived from
/// the curve's physics and is bounded by nothing upstream — and it keeps the
/// helper correct for any future caller that hands it a raw tree duration.
///
/// Consequence at parity across both callers: a spring whose settleTime() exceeds
/// the max is CUT, and for a very soft one that means cut before it visibly
/// starts, not merely mid-ring. `omega=0.1, zeta=0.1` settles in 530 s, floored to
/// Spring::MaxSettleSeconds (30 s) and then clamped to 2 s here — at which point
/// the integrated value is still ~0.02, so the transition tears down with iTime
/// near ZERO and the window snaps. The clamp is doing its job (it is the only
/// thing between such a profile and a pinned repaint), but a pack author tuning a
/// soft spring should know the envelope, not the physics, is what they hit.
inline int resolveTransitionLifetimeMs(int nominalMs, const PhosphorAnimation::Curve* curve)
{
    const int lifetime = (curve && curve->isStateful()) ? qRound(curve->settleTime() * 1000.0) : nominalMs;
    return qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, lifetime,
                  PhosphorAnimation::Limits::MaxAnimationDurationMs);
}

/// The iTime clamp POLICY, in one place.
///
/// An overshooting curve (underdamped spring, back / elastic ease) passes through
/// RAW — the overshoot is the curve, and the geometry animator bounces past the
/// target on the same pick, so flattening it here would make the shader and the
/// geometry disagree. Every other curve is clamped, where an out-of-range value
/// is a bug rather than the intent.
///
/// Both progress sources must route through this: `easeProgress` (the time-driven
/// branch) and `paintWindow`'s animator-driven branch. They are two call sites of
/// one policy, and when the policy lived in two places only one of them got
/// updated — the animator branch kept clamping, which flattened the bounce for
/// exactly the `window.movement.*` events whose geometry visibly bounces.
inline qreal clampProgressForCurve(qreal value, const PhosphorAnimation::Curve* curve)
{
    return (curve && curve->overshoots()) ? value : qBound(0.0, value, 1.0);
}

/// Ease @p linear through @p curve. Shared by the per-window transition paint
/// and the desktop switch, which otherwise carried this logic (and its dt cap)
/// twice.
///
/// A null curve is linear. A stateless curve evaluates @p linear directly. A
/// STATEFUL (spring) curve integrates @p state toward 1 by the inter-frame dt
/// and returns the integrated value, ignoring @p linear.
///
/// OVERSHOOT: a curve that reports `overshoots()` (an underdamped spring, a
/// back / elastic ease) returns its value UNCLAMPED, so iTime can exceed 1 — and
/// dip below 0 — for those curves. That is deliberate: the overshoot IS the
/// curve, and clamping it flattens a bouncy pick into a plateau at 1.0 while the
/// geometry animator (`AnimatedValue::advance`, which likewise does not clamp)
/// bounces past the target. Flattening here would make the shader and the
/// geometry disagree about the same curve. Non-overshooting curves stay clamped,
/// where an out-of-range value is a bug rather than the intent.
///
/// Packs must therefore treat iTime as UNBOUNDED for these curves —
/// `AnimationShaderContract::kITime` tells authors to clamp defensively, and a
/// pack that samples a texture or lerps a rect on iTime without clamping will
/// extrapolate past its endpoint on an overshooting pick.
///
/// @p stepCurve owns the SINGLE per-frame integrator step: the paint pass passes
/// true; a predictor that must not advance the integrator (the backdrop capture)
/// passes false and reads the last stepped value.
///
/// The dt is capped at Limits::MaxShaderTimeDeltaSeconds. Spring::step is
/// semi-implicit Euler, so an unbounded step diverges: a suspend/resume or a
/// compositor stall would hand it a multi-second dt in one frame and blow the
/// velocity up. The cap BOUNDS THE BLAST RADIUS; it does not make the integrator
/// unconditionally stable (Spring admits omega up to 200, and strict stability
/// wants dt < 1/(5*omega), far below 100 ms). Substepping would be the real fix
/// if a pack ever needs a stiff spring here. @p lastPaintTimeMs < 0 is the
/// "no prior paint" sentinel and yields dt = 0.
inline qreal easeProgress(const PhosphorAnimation::Curve* curve, PhosphorAnimation::CurveState& state,
                          qint64 lastPaintTimeMs, qint64 nowMs, qreal linear, bool stepCurve)
{
    if (!curve) {
        return linear;
    }
    if (!curve->isStateful()) {
        return clampProgressForCurve(curve->evaluate(linear), curve);
    }
    if (stepCurve) {
        const qreal dt = lastPaintTimeMs < 0
            ? 0.0
            : qBound(0.0, qreal(nowMs - lastPaintTimeMs) / 1000.0,
                     static_cast<qreal>(PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds));
        curve->step(dt, state, 1.0);
    }
    return clampProgressForCurve(state.value, curve);
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

/// Texture unit for the audio-spectrum sampler (uAudioSpectrum) in the surface
/// composite fold. The fold's main + buffer passes bind unit 0 (uTexture0),
/// 1..4 (iChannel0..3), and 5 (uBackdrop); unit 6 is the first free unit.
///
/// The present passthrough in decoration_render.cpp (`kSurfaceChannelBaseUnit`)
/// reuses a unit in this same range, but the two never collide: the fold
/// completes and unbinds unit 6 before drawWindow's present pass runs, so they
/// occupy their units in disjoint phases regardless of the exact numbers.
/// Shared here (not file-local) so the fold (surfacelayers.cpp) and the bind
/// helper (surface_audio.cpp) agree on one value. If audio ever moves into the
/// present phase, revisit this overlap.
inline constexpr int kSurfaceAudioUnit = 6;
static_assert(kSurfaceAudioUnit > 5,
              "kSurfaceAudioUnit must clear the fold's units 0..5 (uTexture0/iChannel/backdrop)");

/// First texture unit for SURFACE pack user textures (uTexture1..3) in the
/// composite fold's main pass: units 7..9, clear of the fold's units 0..5 and
/// the audio unit 6. The animation paintWindow path also binds audio at unit 6
/// (bindSurfaceAudio) in its own phase; its user textures live at units 1..3,
/// so the two paths' maps never meet on the same draw.
inline constexpr int kSurfaceUserTextureBaseUnit = 7;
static_assert(kSurfaceUserTextureBaseUnit > kSurfaceAudioUnit,
              "surface user textures must sit above the audio unit — the fold binds both in the same pass");

/// Surface decoration texture units shared by the animation-layer paths in
/// paint_pipeline.cpp and the present rebind in decoration_render.cpp. All are
/// offset from kMaxUserTextureSlots (N, = 3 today) so the animation
/// user-texture slots 0..N-1 never collide with them:
///   kOldSnapshotUnit        (N+1) — the morph cross-fade's old-window snapshot
///   kSurfaceLayerUnit       (N+2) — the composited surface layer bound for the
///                                   animation shader / transition rebind
///   kSurfaceChannelBaseUnit (N+3) — the present passthrough's final composite
///                                   slot (shares unit 6 with kSurfaceAudioUnit
///                                   in the disjoint present phase, see above)
/// Centralized here (not re-derived per call site) so the fold, the animation
/// path, and the present rebind can't drift apart if the contract's slot count
/// changes.
inline constexpr int kOldSnapshotUnit = 1 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
inline constexpr int kSurfaceLayerUnit = 2 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
inline constexpr int kSurfaceChannelBaseUnit =
    3 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
static_assert(kOldSnapshotUnit < kSurfaceLayerUnit && kSurfaceLayerUnit < kSurfaceChannelBaseUnit,
              "surface decoration units must stay ordered and distinct");
// Pin the relationship to the audio unit: today kSurfaceChannelBaseUnit == 6 ==
// kSurfaceAudioUnit (the documented disjoint-phase overlap), while the layer /
// old-snapshot units sit below it. A kMaxUserTextureSlots bump that pushes
// kSurfaceLayerUnit onto the audio unit (N=4 → both 6) would silently collide
// mid-fold, so fail at compile time instead.
static_assert(kSurfaceLayerUnit != kSurfaceAudioUnit && kSurfaceChannelBaseUnit >= kSurfaceAudioUnit,
              "surface layer unit must not collide with the audio unit, and the present slot must "
              "stay at or above it — a kMaxUserTextureSlots bump needs the audio/present unit map revisited");

} // namespace PlasmaZones::ShaderInternal
