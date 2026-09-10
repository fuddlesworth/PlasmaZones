// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorPointer/PointerHistory.h>
#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>
#include <PhosphorPointer/PointerUniformExtension.h>

#include <PhosphorSurface/DecorationProfile.h>

#include <QHash> // std::hash<QString> specialization for the unordered_map key below
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector4D>
#include <Qt>

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace KWin {
class GLFramebuffer;
class GLShader;
class GLTexture;
class LogicalOutput;
class RenderTarget;
class RenderViewport;
}

namespace PlasmaZones {

/// The POINTER decoration pass: a screen-space chain of pointer shader packs
/// (data/pointer, PhosphorPointerShaders) drawn over the composited scene of
/// the one output the pointer is on.
///
/// Shape, in one line: a user-ordered chain of packs, each a full-contract
/// fragment shader, composited premultiplied source-over the finished frame,
/// alive only while the pointer has recent motion or a recent button event.
///
/// LIVENESS is entirely event-driven and lives in PointerHistory. A pack
/// declares `trailSeconds`; the pass keeps requesting repaints for that long
/// after the last motion or button event and then goes silent. There is no
/// timer and no spring: the only wake-ups are slotMouseChanged (notePointer)
/// and, while live, the pass's own per-frame repaint request.
///
/// COST RULE. A chain with no live layer must cost nothing per frame. Every
/// entry point early-returns on `m_engaged`, a cached verdict rebuilt only
/// when the enable flag, the profile or the registry changes: with the
/// feature off, an empty chain, or a chain whose every layer is disabled or
/// unresolvable, notePointer writes no history, isLive() is false so the
/// effect is not held in the paint chain by us, scheduleRepaints requests
/// nothing and paintOutput allocates and draws nothing.
///
/// COORDINATE SPACE. The contract's canvas is ONE output in device px,
/// top-down, origin at that output's top-left (pointer_uniforms.glsl). So the
/// history is kept in that space, not in global logical coordinates, and it
/// is RESET when the pointer crosses to another output: a trail has no
/// meaning across a canvas boundary, and keeping the samples would hand the
/// next frame positions measured against the old output's origin and scale.
/// It also means the per-output buffer targets below can be keyed by pack id
/// alone — only one output is ever the live canvas.
///
/// TEXCOORD ORIENTATION. `pointerPixel()` in pointer_lib.glsl reconstructs
/// top-down canvas px from vTexCoord as `vec2(uv.x, 1.0 - uv.y)` under
/// PLASMAZONES_KWIN — it expects BOTTOM-UP texcoords, the render target's own
/// orientation. That is the opposite of TransitionPass::drawOutputQuad, which
/// deliberately pins TOP-DOWN uv for the transition packs' Y-flipping
/// samplers. Hence this pass draws its own quad (drawDamageQuad) rather than
/// reusing that helper; feeding it the transition orientation would mirror
/// every pack vertically.
///
/// DAMAGE. The pass never repaints the whole output. PointerHistory::
/// damageRect gives the bounding box of the live trail samples plus the press
/// and release points, inflated by the chain's largest resolved `reach`
/// (logical px scaled to device). That rect drives both the repaint request
/// and the quad, so a pack's fragment stage runs over a small band around the
/// pointer instead of the whole screen. The quad IS the clip — no scissor is
/// taken, which keeps the pass out of the raw-device-region-into-GL-scissor
/// class of bug and stays correct on a rotated output, because the quad's
/// device-space corners are projected by viewport.projectionMatrix() exactly
/// like every other screen-level draw in this tree.
///
/// CURSOR ARBITRATION. A pack with `layer: above` paints OVER the cursor, so
/// the pass hides KWin's cursor while such a layer is live and re-draws the
/// sprite itself as the frame's last act (TransitionPass::drawSceneCursor,
/// shared with the strip pass). Two rules keep that from fighting the strip
/// pass, which hides the cursor for the same reason:
///   • hideCursorForPass() refuses when KWin already reports the cursor
///     hidden, so whoever asked first keeps it and draws it.
///   • releaseCursorHideForForeignPaint() hands it back when a desktop
///     transition or a strip leg is about to take this output's frame. Those
///     passes replace the whole paint, this pass never reaches paintOutput
///     for that output, and nothing would draw the cursor otherwise. The
///     effect's paintScreen calls it BEFORE those passes run, so the strip
///     pass's own hide can then succeed.
/// A `layer: below` chain never touches cursor visibility at all: it paints
/// under KWin's cursor (a hardware cursor plane included) by construction.
class PointerDecorationPass
{
public:
    /// Takes no back-pointer to the effect, unlike the transition managers:
    /// this pass reads everything it needs from `KWin::effects` and its own
    /// state, and never re-enters the effect's paint hooks.
    PointerDecorationPass();
    ~PointerDecorationPass();

    PointerDecorationPass(const PointerDecorationPass&) = delete;
    PointerDecorationPass& operator=(const PointerDecorationPass&) = delete;

    /// The pack registry, so the effect can wire its `effectsChanged`
    /// hot-reload signal (the search paths are populated lazily on first use).
    PhosphorPointerShaders::PointerShaderRegistry& registry()
    {
        return m_registry;
    }

    /// The decoration profile resolved at the `pointer` path of the effect's
    /// DecorationProfileTree. Its enabled chain, its per-pack parameters and
    /// its disabled-pack set are the whole configuration of this pass: there
    /// is no master switch, exactly as for every other decoration surface, so
    /// "on" means "the resolved chain has at least one enabled layer".
    /// Re-derives the engaged-chain cache; a no-op when the profile is
    /// unchanged, so a settings broadcast that touched something else does not
    /// restart a live chain.
    void setProfile(const PhosphorSurfaceShaders::DecorationProfile& profile);

    /// The outputs the effect's fullscreen gate
    /// (Decorations.Performance.SuppressWhileFullscreen) currently covers. The
    /// pass draws no chain over one of them, and — the part that matters — asks
    /// for no frames while the pointer is on one: a suppressed pass that still
    /// requested a repaint per frame and drew nothing would keep exactly the
    /// cost the setting exists to remove. Empty means the gate is off or no
    /// output is fullscreen, which is the common case and the fast path.
    ///
    /// Pushed rather than queried, so the per-frame path never walks the
    /// stacking order. Entering suppression drops the trail and hands back any
    /// cursor hide, the same tidy-up an emptied chain does: a pointer that
    /// walked onto a game must not reappear mid-trail when it leaves.
    void setSuppressedOutputs(const QSet<KWin::LogicalOutput*>& outputs);

    /// The pointer moved and/or its buttons changed. Called from
    /// PlasmaZonesEffect::slotMouseChanged, the only cursor-motion signal the
    /// effect gets. Positions are GLOBAL LOGICAL px; the pass resolves the
    /// output under @p pos and converts to that output's device-px canvas.
    /// Requests the repaint for this event itself, so a pointer moving over a
    /// hardware cursor plane (which damages nothing) still gets frames.
    void notePointer(const QPointF& pos, const QPointF& oldPos, Qt::MouseButtons buttons, Qt::MouseButtons oldButtons);
    /// Ask the pointer's current output to repaint the trail it still shows,
    /// before the history is reset because the pointer is moving to @p next.
    void repaintStaleTrail(KWin::LogicalOutput* next, qint64 nowMs);

    /// True while the chain is engaged AND the history is inside the longest
    /// `trailSeconds` of it. ORed into PlasmaZonesEffect::isActive(): without
    /// it the effect leaves the paint chain and paintOutput never runs, so
    /// the repaints scheduleRepaints() asks for would paint no decoration.
    bool isLive() const;

    /// True while THIS pass holds the compositor's cursor hidden. ORed into
    /// isActive() separately from isLive() for the same reason the strip pass
    /// does it: liveness can go false between the last painted frame and the
    /// next chain build, and an effect dropped from the chain with the cursor
    /// still hidden has no hook left to show it again.
    bool holdsCursorHide() const
    {
        return m_cursorHidden;
    }

    /// Draw the chain over @p screen's finished frame. Called from
    /// PlasmaZonesEffect::paintScreen AFTER `KWin::effects->paintScreen`, on
    /// the normal path only (a desktop transition or a strip leg replaces the
    /// output's paint and returns before this). A no-op for every output but
    /// the pointer's, and for a chain that is not live.
    void paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                     KWin::LogicalOutput* screen);

    /// Keep a live chain ticking: one repaint of the damage rect on the
    /// pointer's output per frame. Called from postPaintScreen. When the
    /// chain has just gone quiet this is also where a cursor hide taken by an
    /// `above` layer is released, covering the case where the pointer's
    /// output stopped painting entirely.
    void scheduleRepaints();

    /// Give the cursor back because ANOTHER pass is taking @p screen's frame
    /// (a desktop transition, a strip leg). See the class note.
    void releaseCursorHideForForeignPaint(KWin::LogicalOutput* screen);

    /// Drop every compiled pack so the next live frame recompiles against
    /// freshly reloaded source. Called from the registry's `effectsChanged`
    /// handler; also re-derives the engaged-chain cache, since a reload can
    /// add or remove the pack ids the chain names.
    void invalidateShaderCache();

    /// Drop a removed output's state. The history is keyed to one output's
    /// canvas, so an output going away invalidates it wholesale.
    void outputRemoved(KWin::LogicalOutput* screen);

    /// Drop the sampled trail because the canvas it was measured against has
    /// moved under it.
    ///
    /// Samples are stored as device pixels relative to the output's origin, so
    /// a resolution, scale or virtual-layout change leaves every one of them
    /// describing a canvas that no longer exists — the trail draws at the wrong
    /// offset and the wrong size until the ring ages out. The output itself is
    /// unchanged, so `outputRemoved` does not apply and the identity check in
    /// `notePointer` cannot see it either.
    void outputGeometryChanged();

    /// Drop all state and release GL resources (effect teardown / compositor
    /// reset). Null-safe against a torn-down `KWin::effects`.
    void reset();

private:
    /// One resolved chain entry: the metadata and the parameter overrides the
    /// draw needs, snapshotted when the chain is derived so the per-frame path
    /// makes no registry lookups.
    struct EngagedLayer
    {
        QString effectId;
        PhosphorPointerShaders::PointerShaderEffect effect;
        QVariantMap parameters;
        /// `resolvedReach(parameters)`, logical px. Pushed to the pack as
        /// `uPointerFlags.y` (scaled) so it can bound itself to the same
        /// number the damage rect is built from.
        double reachLogical = 0.0;
    };

    /// An array of @p N uniform locations, every slot "unset" (-1). `{}` would
    /// zero them, and 0 is a VALID uniform location.
    template<std::size_t N>
    static constexpr std::array<int, N> makeUnsetLocations()
    {
        std::array<int, N> locs{};
        for (int& loc : locs) {
            loc = -1;
        }
        return locs;
    }

    /// Cached uniform locations shared by the main pass and every buffer
    /// pass. A pack that never references a uniform links without it and the
    /// slot stays -1, so the push is skipped and an unused contract member
    /// costs nothing per frame.
    struct PointerUniformLocations
    {
        int iTime = -1;
        int iResolution = -1;
        int iMouse = -1;
        int uPointerVelocity = -1;
        int uPointerPress = -1;
        int uPointerRelease = -1;
        int uPointerState = -1;
        int uCursorRect = -1;
        int uPointerFlags = -1;
        int uCursorSprite = -1;
        std::array<int, PhosphorPointerShaders::PointerShaderContract::kMaxTrailPoints> uPointerTrail =
            makeUnsetLocations<PhosphorPointerShaders::PointerShaderContract::kMaxTrailPoints>();
        std::array<int, PhosphorPointerShaders::PointerShaderContract::kMaxCustomParams> customParams =
            makeUnsetLocations<PhosphorPointerShaders::PointerShaderContract::kMaxCustomParams>();
        std::array<int, PhosphorPointerShaders::PointerShaderContract::kMaxCustomColors> customColors =
            makeUnsetLocations<PhosphorPointerShaders::PointerShaderContract::kMaxCustomColors>();
        std::array<int, PhosphorPointerShaders::PointerShaderContract::kMaxUserTextureSlots> userTextures =
            makeUnsetLocations<PhosphorPointerShaders::PointerShaderContract::kMaxUserTextureSlots>();
        std::array<int, PhosphorPointerShaders::PointerShaderContract::kMaxUserTextureSlots> iTextureResolution =
            makeUnsetLocations<PhosphorPointerShaders::PointerShaderContract::kMaxUserTextureSlots>();
        std::array<int, 4> iChannel = makeUnsetLocations<4>();
        std::array<int, 4> iChannelResolution = makeUnsetLocations<4>();
    };

    /// One compiled multipass buffer stage of a pack. Buffer stages never
    /// receive the cursor sprite: uCursorSprite is not bound for them and
    /// uPointerFlags.x is 0 there, even for a `needsCursor` pack.
    struct CompiledBufferPass
    {
        std::unique_ptr<KWin::GLShader> shader;
        PointerUniformLocations loc;
    };

    /// A compiled pack: the main-pass shader, its uniform locations, the
    /// resolved parameter slot pools, the uploaded user textures and the
    /// compiled buffer stages. `shader == nullptr` on a cached entry is the
    /// FAILURE LATCH — the id is unknown or the compile failed, and the pack
    /// is skipped until the registry reloads rather than re-attempted every
    /// frame.
    struct CompiledPointerPack
    {
        std::unique_ptr<KWin::GLShader> shader;
        PointerUniformLocations loc;
        std::array<QVector4D, PhosphorPointerShaders::PointerShaderContract::kMaxCustomParams> customParams{};
        std::array<QVector4D, PhosphorPointerShaders::PointerShaderContract::kMaxCustomColors> customColors{};
        std::array<std::unique_ptr<KWin::GLTexture>,
                   PhosphorPointerShaders::PointerShaderContract::kMaxUserTextureSlots>
            userTextures;
        std::vector<CompiledBufferPass> bufferPasses;
        /// Ping-pong buffer targets, one pair per compiled buffer stage. Slot
        /// `bufferFront` holds the LAST frame's output (what `bufferFeedback`
        /// reads); the other is written this frame, and the two swap after the
        /// draw. Sized to the output's device size times the pack's clamped
        /// `bufferScale`, revalidated every frame and reallocated on a change.
        /// Empty for a pack that declares no buffer stages, which is every
        /// bundled pack — a single-pass chain allocates no FBO at all.
        std::vector<std::array<std::unique_ptr<KWin::GLTexture>, 2>> bufferTex;
        std::vector<std::array<std::unique_ptr<KWin::GLFramebuffer>, 2>> bufferFbo;
        QSize bufferSize;
        int bufferFront = 0;
        /// Set when the targets for `bufferSize` could not be allocated. The
        /// stages are then abandoned rather than retried (and re-warned)
        /// every frame, the same discipline the strip pass applies to its
        /// capture. Cleared when the wanted size changes, and gone with the
        /// entry when the cache is dropped (invalidateShaderCache/releaseGl).
        bool bufferAllocFailed = false;
    };

    /// Set by resetHistory(), consumed by runBufferPasses on the next live
    /// frame, which clears both slots of every pair. Kept on the pass rather
    /// than the pack because the packs outlive a reset in the cache.
    bool m_bufferFeedbackStale = false;

    // ── pointerdecorationshader.cpp ─────────────────────────────────────────

    /// Populate the registry's XDG search paths, once. Same order as
    /// PlasmaZonesEffect::ensureSurfaceRegistryPaths: ascending priority with
    /// the writable user dir LAST, because the scan strategy reverse-iterates
    /// with first-wins and a user override must win over the bundled id.
    void ensureRegistryPaths();

    /// Compile (or fetch) the pack for @p layer. NEVER null: the returned
    /// entry's `shader` is null when the pack is unknown or failed to compile.
    /// Returns nullptr only when no GL context could be made current, which
    /// deliberately caches nothing so the next frame retries.
    CompiledPointerPack* compiledPack(const EngagedLayer& layer);

    /// Resolve the uniform locations shared by every stage of a pack onto
    /// @p out. Called for the main shader and each buffer shader. A sampler
    /// the source references but @p eff does not declare (a uTextureN slot
    /// past its `textures`, uCursorSprite without `needsCursor`) is warned
    /// about and left at -1, so it is never bound and never fed unit 0.
    static void cacheUniformLocations(KWin::GLShader* shader, const PhosphorPointerShaders::PointerShaderEffect& eff,
                                      PointerUniformLocations& out);

    // ── pointerdecorationpaint.cpp ──────────────────────────────────────────

    /// Push the frame contract uniforms (everything but the sampler bindings)
    /// onto the currently bound @p shader.
    static void pushFrameUniforms(KWin::GLShader* shader, const PointerUniformLocations& loc,
                                  const CompiledPointerPack& pack,
                                  const PhosphorPointerShaders::PointerFrameState& state, const QSize& deviceSize,
                                  const QRectF& cursorRect, double timeSeconds, bool hasCursorSprite,
                                  float reachDevicePx);

    /// Run @p pack's buffer stages into its ping-pong targets for this frame,
    /// leaving each stage's fresh output ready to bind as iChannelN on the
    /// main pass. Returns false when the targets cannot be allocated, which
    /// makes the caller skip the layer for this frame rather than draw it
    /// with unbound channels.
    bool runBufferPasses(CompiledPointerPack& pack, const EngagedLayer& layer,
                         const PhosphorPointerShaders::PointerFrameState& state, const QSize& deviceSize,
                         const QRectF& cursorRect, double timeSeconds);

    /// Bind @p pack's user textures and buffer-stage outputs, starting at
    /// texture unit @p firstUnit. Returns the next free unit.
    static int bindPackTextures(KWin::GLShader* shader, const PointerUniformLocations& loc,
                                const CompiledPointerPack& pack, int firstUnit);
    /// Undo bindPackTextures: unbind units [@p firstUnit, @p endUnit) and
    /// leave unit 0 active. ScopedGlState restores the active-unit ENUM, not
    /// the bindings, so a name still bound when a later reset deletes it
    /// would survive as a dangling reference.
    static void unbindUnits(int firstUnit, int endUnit);

    /// Draw a quad covering @p deviceRect of the output, in the
    /// RenderViewport's device coordinate space, with BOTTOM-UP texcoords
    /// (see the class note on texcoord orientation). The caller must already
    /// have uploaded viewport.projectionMatrix() as the bound shader's MVP.
    /// @p deviceSize is the canvas the texcoords are normalised against and
    /// MUST be the size pushed as iResolution, since a pack reconstructs
    /// canvas px as uv * iResolution.
    static void drawDamageQuad(const KWin::RenderViewport& viewport, const QRectF& deviceRect, const QSize& deviceSize);
    /// Draw a unit NDC quad for a buffer stage (FBO to FBO, no projection).
    static void drawFullscreenQuad();

    /// Upload (or reuse) the compositor's cursor sprite for a `needsCursor`
    /// pack. Returns null when there is no cursor image, in which case
    /// uPointerFlags.x is pushed as 0.
    KWin::GLTexture* cursorSpriteTexture();

    /// Is @p screen covered by the effect's fullscreen gate? Inline and header-
    /// resident because all three TUs of this pass consult it: every liveness,
    /// damage and draw path funnels through this one expression, so no two of
    /// them can disagree about whether the pass is suppressed.
    bool suppressedOn(KWin::LogicalOutput* screen) const
    {
        return screen && m_suppressedOutputs.contains(screen);
    }

    // ── pointerdecorationpass.cpp ───────────────────────────────────────────

    /// Rebuild m_engaged / m_engagedLayers / m_maxReachLogical /
    /// m_maxTrailSeconds from the enable flag, the profile and the registry.
    /// The one place the cost rule's verdict is decided.
    void rebuildChain();

    /// This frame's damage in @p screen's device-px canvas, already clipped
    /// to the output. Empty when nothing is live. Not const: it records the
    /// sprite rect it saw (m_lastSpriteCanvasRect) for the next call's union.
    QRectF damageDeviceRect(KWin::LogicalOutput* screen, qint64 nowMs, bool ignoreSuppression = false);
    /// The same rect in GLOBAL LOGICAL px, the space addRepaint speaks.
    QRectF damageLogicalRect(KWin::LogicalOutput* screen, qint64 nowMs, bool ignoreSuppression = false);
    /// The rect the trail currently on screen occupies, for a caller about to
    /// drop it. Must be taken BEFORE the state that describes it is torn down.
    QRectF staleTrailRect();
    /// Drop the sampled history AND mark the multipass feedback canvas stale.
    /// The two belong together: a feedback pack's buffer holds the burst the
    /// history just discarded, and runBufferPasses only reallocates (and so
    /// only clears) when the target SIZE changes. An output crossing between
    /// two same-size outputs, or an un-suppress on the same one, would
    /// otherwise let the previous burst's glow bleed into the next one's
    /// first frames. Every reset goes through here so no future path can
    /// clear one without the other.
    void resetHistory();
    void repaintStale(const QRectF& stale) const;

    /// The cursor sprite's rect in @p screen's device-px canvas, hotspot
    /// applied. A null rect when the compositor reports no cursor image.
    QRectF cursorCanvasRect(KWin::LogicalOutput* screen) const;

    /// Take the compositor's cursor hide, if an `above` layer is engaged and
    /// the pointer is on @p screen. A no-op when something else already hides
    /// the cursor, so the pass never resurrects a cursor another effect (or
    /// the strip pass) wanted gone. Returns true only when THIS call took the
    /// hide, which is the frame paintOutput must not blit the sprite on.
    bool hideCursorForPass(KWin::LogicalOutput* screen);
    /// Give the hide back once no live `above` chain covers the pointer.
    void updateCursorHiding();
    bool cursorOnOutput(KWin::LogicalOutput* screen) const;

    /// Make the compositor GL context current before freeing GL objects off
    /// the paint thread. False only at compositor teardown, where the driver
    /// reclaims the objects regardless.
    bool ensureGlContextCurrent();

    /// Free every compiled pack and the cursor sprite under a made-current
    /// context.
    void releaseGl();

    PhosphorPointerShaders::PointerShaderRegistry m_registry;
    bool m_registryPathsAdded = false;

    PhosphorSurfaceShaders::DecorationProfile m_profile;

    /// The cost-rule verdict and its derived budgets. See rebuildChain().
    bool m_engaged = false;
    std::vector<EngagedLayer> m_engagedLayers;
    bool m_anyAboveLayer = false;

    /// Outputs the fullscreen gate covers. See setSuppressedOutputs. Compared
    /// only, never dereferenced, and outputRemoved does not need to prune it:
    /// the effect refreshes the set on the same signal.
    QSet<KWin::LogicalOutput*> m_suppressedOutputs;
    double m_maxReachLogical = 0.0;
    double m_maxTrailSeconds = 0.0;

    /// The output the history's canvas belongs to. Changing it resets the
    /// history (see the class note on coordinate space).
    KWin::LogicalOutput* m_output = nullptr;
    PhosphorPointerShaders::PointerHistory m_history;
    /// The cursor sprite rect the last damage computation saw, in m_output's
    /// canvas, unioned into the next one so a sprite that changed shape
    /// without a pointer event still gets its old band repainted.
    QRectF m_lastSpriteCanvasRect;
    /// Steady-clock origin of the pass's `iTime`, taken on the first live
    /// frame of a burst so a time-driven pack starts at zero rather than at
    /// the compositor's uptime. Cleared when the chain goes quiet.
    qint64 m_timeOriginMs = 0;
    bool m_hasTimeOrigin = false;

    // Move-only mapped values (unique_ptr GL handles) — std::unordered_map,
    // not QHash, which is copy-on-write and would instantiate a deleted copy
    // ctor.
    std::unordered_map<QString, CompiledPointerPack> m_packCache;

    /// The uploaded cursor sprite, keyed on the source QImage's cacheKey so a
    /// theme or shape change re-uploads and a stationary cursor does not.
    std::unique_ptr<KWin::GLTexture> m_cursorSprite;
    qint64 m_cursorSpriteKey = 0;

    /// True while THIS pass holds a hideCursor() on the compositor (the call
    /// is refcounted, so the flag keeps show/hide balanced).
    bool m_cursorHidden = false;
};

} // namespace PlasmaZones
