// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorShaders/BaseUniforms.h>
#include <PhosphorShaders/IUniformExtension.h>

#include <QMutex>
#include <QMutexLocker>
#include <QSizeF>
#include <QVector4D>

#include <atomic>
#include <cstring>

namespace PhosphorAnimation {

/// IUniformExtension that appends animation-shader spatial uniforms after
/// `PhosphorShaders::BaseUniforms` in the UBO. Carries the surface-on-
/// screen rect (`iSurfaceScreenPos`), the captured card's pixel size
/// (`iAnchorSize`), its top-left within the FBO (`iAnchorPosInFbo`), and
/// its UV sub-rect within the captured texture (`iAnchorRectInTexture`)
/// — read by vertex shaders that map the FBO quad onto the card and by
/// fragment shaders that want card-space sampling and screen-relative
/// noise / edge fades.
///
/// Why an extension and not a BaseUniforms member: the zone-shader path
/// (`ZoneShaderItem` via `ZoneUniformExtension`) shares `BaseUniforms`
/// with the animation path. An earlier revision grew BaseUniforms to
/// 704 bytes to add these two fields directly; that change broke zone-
/// shader rendering on multi-VS setups in a way the team couldn't
/// reproduce in a controlled test (likely a transitively-included-
/// `common.glsl` cache-key gap in `shaderCacheKey`, but unconfirmed).
/// Keeping `BaseUniforms` at 672 bytes and routing animation-only
/// fields through this extension isolates the two pipelines: zone
/// shaders attach `ZoneUniformExtension` (4096 bytes after base) and
/// never observe these fields at all; animation shaders attach
/// `AnimationUniformExtension` (48 bytes after base) — `iSurfaceScreenPos`
/// at offset 672, `iAnchorSize` at 688, `iAnchorPosInFbo` at 696,
/// `iAnchorRectInTexture` at 704 — matching the trailing-field layout
/// `data/animations/shared/animation_uniforms.glsl`'s UBO branch
/// declares.
///
/// Cross-runtime parity: the kwin-effect path uses classic GL
/// `setUniform` lookups for these names (see `paint_shader_window.cpp`)
/// — independent of the UBO mechanism,
/// works without any extension. Author-side GLSL is identical across
/// runtimes (the `#ifdef PLASMAZONES_KWIN` branch of
/// animation_uniforms.glsl declares them as default-block uniforms;
/// the UBO branch declares them inside `AnimationUniforms { ... }`).
///
/// Ownership: created per-leg by `SurfaceAnimator::attachShaderToAnchor`
/// and installed on the plain `ShaderEffect` it creates via
/// `ShaderEffect::setUniformExtension(shared_ptr)`. The shared_ptr stays
/// alive in the animator's per-track storage, so consumer-side updates
/// (`syncShaderGeometryNow` calls `setISurfaceScreenPos` /
/// `setIAnchorSize`) reach the same instance the render thread reads.
///
/// Threading: `write` is called on the render thread during prepare();
/// `setI*` is called on the GUI thread from `syncShaderGeometryNow`.
/// `m_mutex` serializes both — sync phase normally blocks the render
/// thread but some Qt render loops can advance through prepare() before
/// the next sync fires, so the lock is required to prevent torn copies.
class PHOSPHORANIMATION_EXPORT AnimationUniformExtension : public PhosphorShaders::IUniformExtension
{
public:
    AnimationUniformExtension()
    {
        std::memset(&m_data, 0, sizeof(m_data));
        // iAnchorRectInTexture defaults to the (0, 0, 1, 1) identity —
        // a card-sized anchor with no capture margin. memset leaves it
        // (0, 0, 0, 0); a zero width/height would collapse every
        // surfaceColor sample to the texture's corner texel (and divide
        // animation.vert's remap by zero) before the first geometry
        // sync lands a real value.
        m_data.iAnchorRectInTexture[2] = 1.0f;
        m_data.iAnchorRectInTexture[3] = 1.0f;
    }

    int extensionSize() const override
    {
        return static_cast<int>(sizeof(m_data));
    }

    void write(char* buffer, int offset) const override
    {
        QMutexLocker lock(&m_mutex);
        std::memcpy(buffer + offset, &m_data, sizeof(m_data));
    }

    bool isDirty() const override
    {
        return m_dirty.load(std::memory_order_acquire);
    }

    void clearDirty() override
    {
        m_dirty.store(false, std::memory_order_release);
    }

    /// Animation shaders pair `iResolution` with logical-pixel
    /// extension fields (`iAnchorSize`, `iAnchorPosInFbo`,
    /// `iSurfaceScreenPos`) for UV / clip-space ratios, and use the
    /// vertex-stage `vTexCoord` instead of `gl_FragCoord` — so the
    /// legacy DPR-scaling of `iResolution` would mismatch units across
    /// the UBO and shrink the rendered card by 1/DPR (fly-in) or
    /// shift the anchor UV remap by the same factor (broken-glass,
    /// morph). Override to publish `iResolution` in logical pixels so
    /// every ratio in animation shaders is dimensionally consistent.
    bool requiresPhysicalResolution() const override
    {
        return false;
    }

    /// Surface origin in **logical** screen pixels (.xy) plus host
    /// screen dimensions (.zw). Pushed by SurfaceAnimator on every leg
    /// attach and on each anchor / window geometry signal. Logical-
    /// pixel units — the same unit `iResolution` carries on this path,
    /// because this extension's `requiresPhysicalResolution()` returns
    /// false and `shadereffect.cpp::syncCustomNode` therefore skips the
    /// DPR multiply. `.zw` mirrors the QQuickWindow's contentItem size
    /// and equals the wl_surface rect for screen-sized OSD / popup
    /// surfaces on the daemon path, so it is an equivalent screen-size
    /// source when a shader wants it independent of the FBO bounds.
    /// Initial value (0,0,0,0) — vertex shaders that read `.zw` for
    /// screen size MUST guard against zero (`max(.z, 1.0)`) to avoid
    /// NaN before the first push lands.
    void setISurfaceScreenPos(const QVector4D& pos)
    {
        QMutexLocker lock(&m_mutex);
        if (m_data.iSurfaceScreenPos[0] == pos.x() && m_data.iSurfaceScreenPos[1] == pos.y()
            && m_data.iSurfaceScreenPos[2] == pos.z() && m_data.iSurfaceScreenPos[3] == pos.w()) {
            return;
        }
        m_data.iSurfaceScreenPos[0] = static_cast<float>(pos.x());
        m_data.iSurfaceScreenPos[1] = static_cast<float>(pos.y());
        m_data.iSurfaceScreenPos[2] = static_cast<float>(pos.z());
        m_data.iSurfaceScreenPos[3] = static_cast<float>(pos.w());
        m_dirty.store(true, std::memory_order_release);
    }

    /// Anchor (card) pixel size in **logical** pixels. Decoupled from
    /// `iResolution` because Qt auto-resets iResolution to the shader
    /// item's bounds on any geometry event. For a `fboExtentKind:
    /// Surface` shader item that auto-reset would clobber any anchor-
    /// size override. Pushed alongside `iSurfaceScreenPos` from
    /// `syncShaderGeometryNow`. Logical-pixel units to keep magic-
    /// constant tuning that consumes this as a standalone pixel count
    /// stable across DPR settings (broken-glass's `uSize`, tv-glitch's
    /// row offsets). Ratios against `iSurfaceScreenPos.zw` or against
    /// `iResolution` are both unit-consistent here — the animation
    /// path publishes `iResolution` in logical pixels too (see
    /// `requiresPhysicalResolution()` above).
    void setIAnchorSize(const QSizeF& size)
    {
        QMutexLocker lock(&m_mutex);
        const float w = static_cast<float>(size.width());
        const float h = static_cast<float>(size.height());
        if (m_data.iAnchorSize[0] == w && m_data.iAnchorSize[1] == h) {
            return;
        }
        m_data.iAnchorSize[0] = w;
        m_data.iAnchorSize[1] = h;
        m_dirty.store(true, std::memory_order_release);
    }

    /// Anchor's top-left position inside the shader item's FBO, in
    /// **logical** pixels. Combined with `iAnchorSize` and the FBO
    /// size — both in the same logical unit — shaders can compute the
    /// anchor's UV region inside the FBO:
    /// ```
    /// vec2 anchorTopLeftUv = iAnchorPosInFbo / iResolution;
    /// vec2 anchorSizeUv    = iAnchorSize    / iResolution;
    /// vec2 anchorUv        = (vTexCoord - anchorTopLeftUv) / anchorSizeUv;
    /// ```
    /// This generalises the previous `customParams[7].x`-based ring-
    /// padding remap (morph, broken-glass) AND the surface-extent vertex
    /// remap (fly-in) onto a single math contract that works for any
    /// FBO size the runtime decides to allocate.
    ///
    /// Values per FBO-extent mode:
    ///   * Anchor extent, no padding: (0, 0). Anchor == FBO.
    ///   * Anchor extent with ring (e.g. anchor+0.5): (padW, padH) where
    ///     padW = anchor.width * pad, padH = anchor.height * pad.
    ///   * Surface extent: anchor's position relative to the FBO's
    ///     origin, which under the daemon's shaderItem-parented-to-
    ///     anchor->parentItem() convention equals `anchor->x()` /
    ///     `anchor->y()` (the shaderItem is at (0,0) in parent's coords
    ///     for surface extent, so anchor's local-to-parent coords ARE
    ///     anchor's local-to-FBO coords).
    void setIAnchorPosInFbo(const QPointF& pos)
    {
        QMutexLocker lock(&m_mutex);
        const float x = static_cast<float>(pos.x());
        const float y = static_cast<float>(pos.y());
        if (m_data.iAnchorPosInFbo[0] == x && m_data.iAnchorPosInFbo[1] == y) {
            return;
        }
        m_data.iAnchorPosInFbo[0] = x;
        m_data.iAnchorPosInFbo[1] = y;
        m_dirty.store(true, std::memory_order_release);
    }

    /// Card's UV sub-rect within `uTexture0`, as `(x, y, width, height)`
    /// in the captured texture's [0, 1] space. The daemon renders the
    /// shader anchor into `uTexture0`; when the anchor is larger than
    /// the visible card — a PopupFrame capture item that bundles the
    /// glow margin so the glow animates with the card — this rect is
    /// the card's region within that texture. `animation.vert` divides
    /// `texCoord` by it so anchor-extent shaders see `vTexCoord` as
    /// [0, 1] over the card (the glow margin falls outside [0, 1]), and
    /// `surfaceColor()` multiplies by it so card-space samples address
    /// the card region of the texture. A bare card-sized anchor carries
    /// the `(0, 0, 1, 1)` identity, which makes both folds a passthrough.
    /// Pushed by `syncShaderGeometryNow` alongside `iAnchorSize`. Mirrors
    /// the kwin-effect's same-named uniform (frame's sub-rect within
    /// KWin's shadow-padded redirected FBO).
    void setIAnchorRectInTexture(const QVector4D& rect)
    {
        QMutexLocker lock(&m_mutex);
        if (m_data.iAnchorRectInTexture[0] == rect.x() && m_data.iAnchorRectInTexture[1] == rect.y()
            && m_data.iAnchorRectInTexture[2] == rect.z() && m_data.iAnchorRectInTexture[3] == rect.w()) {
            return;
        }
        m_data.iAnchorRectInTexture[0] = static_cast<float>(rect.x());
        m_data.iAnchorRectInTexture[1] = static_cast<float>(rect.y());
        m_data.iAnchorRectInTexture[2] = static_cast<float>(rect.z());
        m_data.iAnchorRectInTexture[3] = static_cast<float>(rect.w());
        m_dirty.store(true, std::memory_order_release);
    }

private:
    /// Std140 layout — appended after BaseUniforms at offset
    /// `sizeof(PhosphorShaders::BaseUniforms)`. Mirror of the trailing
    /// fields in `data/animations/shared/animation_uniforms.glsl`'s UBO
    /// branch. `iSurfaceScreenPos` / `iAnchorSize` land at the same
    /// std140 offsets (672, 688) the pre-isolation in-BaseUniforms
    /// layout used; `iAnchorPosInFbo` (696) and `iAnchorRectInTexture`
    /// (704) extend the trailing region.
    struct alignas(16) AnimationExtensionData
    {
        float iSurfaceScreenPos[4]; // offset 0  (16 bytes), UBO offset 672 = 0 + sizeof(BaseUniforms)
        float iAnchorSize[2]; // offset 16 (8 bytes),  UBO offset 688
        float iAnchorPosInFbo[2]; // offset 24 (8 bytes),  UBO offset 696. Anchor's top-left
                                  //   position inside the shader item's FBO, in logical pixels.
                                  //   See setIAnchorPosInFbo's docstring for the per-extent math
                                  //   contract this enables.
        float iAnchorRectInTexture[4]; // offset 32 (16 bytes), UBO offset 704. Card's UV sub-rect
                                       //   within uTexture0. offset 32 is already 16-aligned, so
                                       //   std140 inserts no pad before it. See
                                       //   setIAnchorRectInTexture's docstring.
    };
    static_assert(sizeof(AnimationExtensionData) == 48,
                  "AnimationExtensionData must be exactly 48 bytes — std140 vec4-aligned, no trailing pad");
    static_assert(sizeof(PhosphorShaders::BaseUniforms) + sizeof(AnimationExtensionData) == 720,
                  "BaseUniforms + AnimationExtensionData must total 720 bytes — matches the trailing-field "
                  "layout declared in data/animations/shared/animation_uniforms.glsl");
    static_assert(sizeof(PhosphorShaders::BaseUniforms) == 672,
                  "BaseUniforms must remain at 672 bytes — growing it shifts ZoneUniformExtension's "
                  "zoneRects offset and breaks every zone shader compiled against `data/overlays/shared/"
                  "common.glsl`. AnimationUniformExtension exists precisely to avoid that growth.");

    AnimationExtensionData m_data;
    mutable QMutex m_mutex;
    std::atomic<bool> m_dirty{true};
};

} // namespace PhosphorAnimation
