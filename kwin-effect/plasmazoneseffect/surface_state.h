// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Per-window GL state for the decoration composite fold, split out of types.h to
// keep that header under the 800-line limit. Consumed by surfacelayers.cpp (which
// folds), surface_backdrop.cpp (which fills backdropTex), decoration_render.cpp and
// paint_pipeline.cpp (which present compositeTex[finalSlot]).

#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>

#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QVector4D>

#include <array>
#include <memory>
#include <vector>

namespace PlasmaZones {

/// Per-window GL state for the decoration composite fold
/// (renderSurfaceChainComposite): the raw window capture, the cached static-prefix
/// fold, the ping-pong composite pair, the per-pack buffer-pass textures
/// (chainBufferTex), the backdrop capture, and a framebuffer pooled beside every
/// one of those textures. Keyed by getWindowId(w) in m_surfaceMultipass; freed by
/// removeWindowDecoration (a decoration REFRESH keeps it — see its keepSurfaceState
/// parameter) and by the windowDeleted backstop.
struct SurfaceMultipassState
{
    // ── Multi-pack chain compositing path (renderSurfaceChainComposite) ──────
    // The two composite textures ping-pong as the chain is folded pack-by-pack;
    // `finalSlot` names the slot holding the last fold (presented by drawWindow
    // through the passthrough shader). `chainBufferTex[k]` caches pack k's
    // buffer-pass outputs so an animated pack does not reallocate its scratch
    // textures every frame. All are sized for `compositeSize` / `chainKey` and
    // rebuilt only when the window size or the resolved chain changes.
    std::array<std::unique_ptr<KWin::GLTexture>, 2> compositeTex;
    std::vector<std::vector<std::unique_ptr<KWin::GLTexture>>> chainBufferTex;
    /// Framebuffers wrapping the textures above, cached for the same lifetime.
    /// KWin's GLFramebuffer ctor is glGenFramebuffers + attach +
    /// glCheckFramebufferStatus and its dtor is glDeleteFramebuffers; building
    /// them on the stack per pass meant a 4-pack chain churned one gen/check/
    /// delete cycle per pass per window per frame. The textures they attach are
    /// already pooled here, so the FBOs are pooled in lockstep and rebuilt only
    /// where the textures are (size or chain change). KWin's own OffscreenData
    /// caches its GLFramebuffer beside its texture for exactly this reason.
    std::array<std::unique_ptr<KWin::GLFramebuffer>, 2> compositeFbo;
    std::vector<std::vector<std::unique_ptr<KWin::GLFramebuffer>>> chainBufferFbo;

    /// The raw window capture, held in its OWN texture rather than in
    /// compositeTex[0], so the pack fold's ping-pong cannot clobber it. (The one
    /// exception is the degenerate chain where NO pack compiles: with nothing to
    /// fold there is no ping-pong to clobber it, so the capture goes straight into
    /// compositeTex[0] and is presented from there — finalSlot must name a composite
    /// slot, and 0 is the only one written.)
    ///
    /// The capture step calls KWin::effects->drawWindow(), which re-enters the
    /// whole draw chain — by far the most expensive step of the fold. Its only
    /// input is the window's own content, so it stays valid until the window
    /// damages. `captureValid` is cleared by the windowDamaged connection (and
    /// by any realloc here), which lets an ANIMATED chain keep folding its
    /// iTime packs every frame over a capture taken once. A window that damages
    /// every frame (video, terminal) re-captures every frame exactly as before.
    std::unique_ptr<KWin::GLTexture> captureTex;
    std::unique_ptr<KWin::GLFramebuffer> captureFbo;
    bool captureValid = false;

    /// The composite after folding the chain's leading run of STATIC packs (see
    /// packIsStatic) over the capture. Those packs are a pure function of the
    /// capture and their parameters, so while the capture holds, their fold holds
    /// too and the animated packs downstream can run straight off this instead.
    ///
    /// Cacheability is a property of the PREFIX, not of a pack on its own: each
    /// pack folds over the running composite, so the first time-varying pack makes
    /// every pack after it time-varying as well, however simple those are. Hence
    /// the leading run, and hence `prefixPackCount` — the number of packs this
    /// texture has folded, which must match the chain's current static run for the
    /// cache to be reusable.
    ///
    /// Invalidated with the capture (it is downstream of it), and by any chain or
    /// size change, which already rebuild everything here.
    std::unique_ptr<KWin::GLTexture> prefixTex;
    std::unique_ptr<KWin::GLFramebuffer> prefixFbo;
    bool prefixValid = false;
    int prefixPackCount = -1;

    /// The whole chain folded, for a chain with NO animated pack in it.
    ///
    /// The static-prefix cache above only pays when something dynamic follows the
    /// run, so it deliberately does not engage when every pack is static — but that
    /// left the all-static chain re-folding every single pack on every paintWindow,
    /// and paintWindow fires whenever ANYTHING on screen damages a region
    /// overlapping this window, not only when the window's own content changes. A
    /// bordered window on a busy desktop was paying its full chain fold for a
    /// composite that could not possibly have changed.
    ///
    /// Such a composite is a pure function of the capture, so it holds exactly as
    /// long as the capture does. Cleared with captureValid, and by any chain, size,
    /// scale or parameter change.
    bool compositeValid = false;

    QStringList chainKey; ///< the chain `chainBufferTex` was allocated for
    QSize compositeSize; ///< full textureSize the composite targets were allocated for
    /// The captureScale the targets were built at. Normally implied by
    /// compositeSize, but NOT when the kMaxSurfaceDim cap is active: past the cap
    /// the texture is pinned to the cap on its long axis for ANY input scale, so a
    /// huge window moving between outputs of different scale changes captureScale —
    /// and therefore uSurfaceScale, which packs multiply their logical-px border
    /// widths and corner radii by — with an unchanged compositeSize. Keyed here so
    /// that case still invalidates the caches instead of freezing the static prefix
    /// at the old scale.
    qreal captureScaleKey = 0.0;
    int finalSlot = 0; ///< which compositeTex slot holds the final fold
    /// The logical rect the composite canvas covers (expanded geometry
    /// inflated by the chain's outer padding, captured when the fold ran).
    /// The layer-rect remap and the padded quads read THIS instead of
    /// recomputing from live geometry, so a CLOSING (deleted) window — whose
    /// frozen composite is reused and whose live geometry may drift — keeps
    /// its decoration aligned to the texture that actually exists.
    QRectF canvasGeo;

    /// Backdrop capture for needsBackdrop chains: the scene behind the
    /// window blitted from the live render target over the SAME padded
    /// canvas as the composite (texel-aligned — a pack samples both with one
    /// uv). Reallocated on size change; freed with the rest of this state in
    /// removeWindowDecoration, and NEVER sampled on the deleted/close path (the
    /// fold doesn't run there; the frozen composite carries the last-alive
    /// frost baked in).
    std::unique_ptr<KWin::GLTexture> backdropTex;
    /// Framebuffer over backdropTex, cached for the texture's lifetime — the
    /// capture blit runs every frame for a needsBackdrop chain, so building it
    /// on the stack there churned a gen/check/delete per window per frame.
    std::unique_ptr<KWin::GLFramebuffer> backdropFbo;
    QSize backdropSize;
    /// Valid sub-rect of backdropTex in TOP-DOWN normalized coords (xy=min,
    /// zw=size) — the part actually blitted (canvas ∩ output). Zero-size
    /// means "no capture this frame" and pushes uHasBackdrop = 0.
    QVector4D backdropRect;

    /// Multi-output capture accumulation: paintWindow runs per OUTPUT, so a
    /// canvas straddling two outputs is captured once per output per frame,
    /// EACH blitting only its own slice into the shared texture. The first
    /// capture of a frame clears the texture; same-frame captures accumulate
    /// (their dest sub-rects are disjoint) and backdropRect grows to the
    /// UNION, so a window mid-move across a monitor boundary gets a complete
    /// backdrop instead of whichever slice painted last (winner-takes-all
    /// left most of the pane clamped and visibly killed the blur during
    /// cross-monitor animations).
    qint64 backdropFrameMs = -1;

    /// When the composite last folded (shader clock, ms). Rate-limits the
    /// backdrop-driven forced repaints in postPaintScreen to ~30fps, the
    /// better-blur-dx model: between refolds the present blit reuses the
    /// existing composite (which IS the cache), so frost over a video costs
    /// a fold every ~33ms instead of every vsync. Damage to the window
    /// itself still refolds immediately (its paint runs regardless).
    qint64 lastFoldMs = -1;
};

} // namespace PlasmaZones
