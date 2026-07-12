// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// @file surface_types.h
/// Compiled-pack and per-window state for SURFACE (decoration) shaders.
///
/// Split out of types.h, which held two concerns that never referenced each
/// other: the surface/decoration fold (here) and the window animation
/// transitions (transition_types.h). Nothing in this header depends on
/// PhosphorAnimation, and nothing in transition_types.h depends on
/// PhosphorSurface — the seam is real, not cosmetic. types.h remains as an
/// umbrella that includes both, so existing consumers are unaffected.

#include <PhosphorSurface/SurfaceShaderContract.h>

#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QColor>
#include <QHash>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector4D>

#include <array>
#include <memory>
#include <vector>

namespace KWin {
class Item;
}

namespace PlasmaZones {

/// One compiled buffer pass of a multipass SURFACE pack (run in the
/// composite fold). Each buffer.frag is a fullscreen-quad fragment that
/// samples the captured window surface (uTexture0) plus any prior buffer outputs
/// (iChannel0..N-1) and writes into its own FBO; the main effect.frag then
/// samples the final buffer output(s) as iChannel0..3. Compiled in
/// compiledPack() right after the main pack shader, cleared (fail-closed) if any
/// buffer pass fails to compile so the pack degrades to single-pass. The vector
/// of these is shared by every decorated window — the per-window FBO targets
/// live in SurfaceMultipassState.
struct CompiledSurfaceBufferPass
{
    std::unique_ptr<KWin::GLShader> shader;
    int uTexture0Loc = -1; ///< the captured surface (bound to GL_TEXTURE0)
    int uTimeLoc = -1; ///< iTime — continuous seconds (-1 for a static buffer pass)
    int uSurfaceSizeLoc = -1; ///< uSurfaceSize — the composite canvas extent, device px
    int uScaleLoc = -1; ///< uSurfaceScale — logical-to-device scale (blur radii etc.)
    /// Backdrop sampling (needsBackdrop packs): sampler + valid-rect + gate
    /// locations; -1 for the common pass that never references the scene
    /// behind the window.
    int uBackdropLoc = -1;
    int uBackdropRectLoc = -1;
    int uHasBackdropLoc = -1;
    /// Audio-spectrum locations (surface_audio.glsl): the bar-count int and the
    /// spectrum sampler. -1 for a buffer pass that never reads the CAVA spectrum
    /// (the linker drops both). A pass with iAudioSpectrumSizeLoc >= 0 is the
    /// signal that this buffer pass reacts to audio.
    int iAudioSpectrumSizeLoc = -1;
    int uAudioSpectrumLoc = -1;
    /// iChannel0..3 sampler locations — prior buffer outputs feeding this pass.
    std::array<int, 4> iChannelLoc{{-1, -1, -1, -1}};
    /// iChannelResolution[0..3] element locations (the .xy pixel size of each).
    std::array<int, 4> iChannelResolutionLoc{{-1, -1, -1, -1}};
    /// Pack-declared parameter slot locations (reuse the main pass's values).
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> customParamsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> a;
        a.fill(-1);
        return a;
    }();
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> customColorsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> a;
        a.fill(-1);
        return a;
    }();
};

/// Resolved customParams / customColors uniform VALUES for one surface pack —
/// the pack's declared parameter defaults merged with a DecorationProfile's
/// per-pack overrides, translated to contract slots. Stored per (window, pack)
/// on WindowDecoration::packParamValues so windows on different surface paths
/// (window.tiled vs window.snapped vs window.floating) can carry different
/// override values for the SAME pack; the pack's compiled program is
/// value-independent, so only these arrays vary per window.
struct SurfaceParamValues
{
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> params{};
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> colors{};
};

/// Compiled state for ONE surface shader pack (e.g. "border", "glow"), keyed by
/// pack id in PlasmaZonesEffect::m_compiledPacks. Holds everything the old single
/// borderShader() global produced for the one selected pack — the main MapTexture
/// GLShader, its contract uniform locations, the pack-declared customParams /
/// customColors locations + resolved-default VALUES, the main-pass iChannel
/// locations, and the compiled multipass buffer passes — now parameterised per
/// pack id so per-window decoration chains can each render their own base pack.
///
/// Compiled on first use (compiledPack), cached for the effect's lifetime, and
/// fail-closed: a pack whose compile fails latches `compileFailed = true` and
/// `shader == nullptr`, so subsequent lookups return it without re-attempting the
/// compile every frame. The whole map is cleared on a SurfaceShaderRegistry
/// hot-reload (effectsChanged) so the next paint recompiles against fresh source.
///
/// The param VALUES baked here at first-compile time are the BASELINE fallback
/// only (the profile that triggered the compile). The render paths prefer the
/// per-window values updateWindowDecoration resolves into
/// WindowDecoration::packParamValues, so windows on different surface paths can
/// carry different overrides for the same pack; these baked arrays are used
/// only when a window has no per-window entry for the pack.
struct CompiledSurfacePack
{
    std::unique_ptr<KWin::GLShader> shader;
    bool compileFailed = false; ///< latch a failed compile so we don't retry every frame

    // Contract uniform locations (1:1 with PhosphorSurfaceShaders::SurfaceShaderContract).
    int uSurfaceSizeLoc = -1; ///< uSurfaceSize — uTexture0 extent, device px
    int uFrameTopLeftLoc = -1; ///< uSurfaceFrameTopLeft — frame top-left within the texture, device px
    int uFrameSizeLoc = -1; ///< uSurfaceFrameSize — frame size excluding shadows, device px
    int uScaleLoc = -1; ///< uSurfaceScale — logical-to-device pixel scale
    int uFocusedLoc = -1; ///< uSurfaceFocused — 1.0 focused / 0.0 unfocused
    int uOpacityLoc = -1; ///< uSurfaceOpacity — rule-resolved window opacity (handlesOpacity packs)
    /// The pack's `"handlesOpacity"` METADATA flag, cached at compile time.
    ///
    /// Gates the uSurfaceOpacity push, so that push and the present pass's
    /// suppression (`WindowDecoration::chainHandlesOpacity`) share ONE authority. Gate
    /// the push on the linker (`uOpacityLoc >= 0`) instead and a pack that samples
    /// uSurfaceOpacity — directly, or via the shared `surfaceSlabOpen()` helper —
    /// without declaring the flag gets the rule alpha applied twice: once folded,
    /// once by the present pass. That is the single-apply invariant the
    /// iWindowOpacity bug already broke once. Such a pack now gets 1.0 pushed and
    /// the present pass modulates, as intended.
    bool handlesOpacity = false;
    int uTimeLoc = -1; ///< iTime — continuous seconds; -1 ⟺ static pack (drives the repaint gate)
    /// uTexture0 — the input-surface sampler (unit 0). Every decorated window
    /// (one-pack chains included) folds through renderSurfaceChainComposite,
    /// which runs the main pass as a fullscreen FBO pass, binds the running
    /// composite to unit 0 itself, and sets this explicitly. (The only
    /// remaining OffscreenData::paint auto-bind of unit 0 belongs to the
    /// animation-transition path's CachedShader, a different struct.)
    int uTexture0Loc = -1;
    /// Backdrop sampling (needsBackdrop packs, main pass): sampler +
    /// valid-rect + gate locations; -1 for packs that never reference the
    /// scene behind the window.
    int uBackdropLoc = -1;
    int uBackdropRectLoc = -1;
    int uHasBackdropLoc = -1;
    /// Audio-spectrum locations (surface_audio.glsl): iAudioSpectrumSize (bar
    /// count int) + the uAudioSpectrum sampler. -1 for a pack that never reads
    /// the CAVA spectrum (the linker drops both — the common case). A pack with
    /// iAudioSpectrumSizeLoc >= 0 is audio-reactive: it drives the run gate
    /// (syncEffectAudioState) and the per-frame repaint gate (windowSurfaceAnimates).
    int iAudioSpectrumSizeLoc = -1;
    int uAudioSpectrumLoc = -1;
    /// iMouse — cursor in the surface texture's top-down device-px space,
    /// (-1, -1) off-canvas. -1 for packs that never read the cursor (the
    /// common case); pushBorderUniforms then skips the push entirely.
    int iMouseLoc = -1;

    /// MAIN-pass iChannel0..3 sampler + iChannelResolution[0..3] element
    /// locations. -1 when the linker dropped the uniform (single-pass pack).
    std::array<int, 4> iChannelLoc{{-1, -1, -1, -1}};
    std::array<int, 4> iChannelResolutionLoc{{-1, -1, -1, -1}};

    /// User-declared image textures (metadata `textures`): sampler +
    /// iTextureResolution[N] element locations, plus the textures themselves,
    /// loaded once at compile time (decorations are persistent, so the
    /// pack-lifetime cache is the natural owner — freed with the shader on
    /// cache clear, where the GL context discipline already applies). Slot N
    /// feeds uTexture<N+1>. A slot with no loadable file stays null and the
    /// fold skips its bind (the sampler then reads transparent black).
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots> userTextureLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots> a;
        a.fill(-1);
        return a;
    }();
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots> iTextureResolutionLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots> a;
        a.fill(-1);
        return a;
    }();
    std::array<std::unique_ptr<KWin::GLTexture>, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots>
        userTextures;

    /// Pack-declared parameter uniform locations + resolved-default values.
    /// float/int/bool params pack into customParams[N], colours into
    /// customColors[N]. The border pack declares borderWidth / cornerRadius /
    /// useSystemAccent (customParams[0]) and active/inactive colours
    /// (customColors[0..1]) — pushBorderUniforms overrides those slots with
    /// the per-window rule appearance. Slots a pack does not reference
    /// resolve to -1 and push nothing.
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> customParamsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> a;
        a.fill(-1);
        return a;
    }();
    std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> customColorsLoc = []() {
        std::array<int, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> a;
        a.fill(-1);
        return a;
    }();
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomParams> customParamsValues{};
    std::array<QVector4D, PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors> customColorsValues{};

    /// Compiled multipass buffer passes (run in the composite fold). Empty for
    /// a single-pass pack (the border). Cleared fail-closed if any pass fails
    /// to compile (the pack then renders single-pass). The per-window FBO
    /// targets live in m_surfaceMultipass.
    std::vector<CompiledSurfaceBufferPass> bufferPasses;
};

/// Per-window FBO state for the decoration composite fold
/// (renderSurfaceChainComposite): the ping-pong composite pair, the per-pack
/// buffer-pass textures (chainBufferTex), and the backdrop capture. Keyed by
/// getWindowId(w) in m_surfaceMultipass; freed by removeWindowDecoration.
struct SurfaceMultipassState
{
    // ── Multi-pack chain compositing path (renderSurfaceChainComposite) ──────
    // The two composite textures ping-pong as the chain is folded pack-by-pack;
    // `finalSlot` names the slot holding the last fold (presented by drawWindow
    // through the passthrough shader). `chainBufferTex[k]` caches pack k's
    // buffer-pass outputs so an ANIMATED pack (future iTime support) does not
    // reallocate its scratch textures every frame. All are sized for
    // `compositeSize` / `chainKey` and rebuilt only when the window size or the
    // resolved chain changes.
    std::array<std::unique_ptr<KWin::GLTexture>, 2> compositeTex;
    std::vector<std::vector<std::unique_ptr<KWin::GLTexture>>> chainBufferTex;
    QStringList chainKey; ///< the chain `chainBufferTex` was allocated for
    QSize compositeSize; ///< full textureSize the composite targets were allocated for
    int finalSlot = 0; ///< which compositeTex slot holds the final fold
    /// Did the LAST fold actually apply the window's rule alpha?
    ///
    /// Reports what the fold DID, not what the metadata promised.
    /// `WindowDecoration::chainHandlesOpacity` is folded from registry metadata
    /// alone, with no reference to whether the pack's GLSL compiled — so a
    /// handlesOpacity pack that FAILS to compile is skipped by the fold (nothing
    /// applies uSurfaceOpacity) while both consumers still stand down on the
    /// metadata, and the window renders FULLY OPAQUE, silently dropping the user's
    /// SetOpacity rule. Fail-open on a rule they set.
    ///
    /// Reading this instead means a compiled-but-broken chain falls back to
    /// present-pass modulation, which is exactly the "no pack owns the alpha"
    /// regime. chainHandlesOpacity stays for the uses that legitimately need the
    /// metadata BEFORE any fold has run.
    bool handledOpacity = false;
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

/// Per-window border + rounded corners, rendered by sampling the redirected
/// window through an offscreen MapTexture fragment shader (the KDE-Rounded-
/// Corners / shapecorners technique) rather than a scene-graph
/// `OutlinedBorderItem`.
///
/// A scene-graph child item composites against KWin's own decoration frame +
/// drop shadow, so on server-side-decorated windows an outline looks "inset".
/// Sampling the redirected texture runs over the decoration with KWin's own MVP,
/// so the outline is flush and the SAME path rounds the corners.
///
/// The shader evaluates one analytic rounded-rect signed-distance field over the
/// window FRAME and does TWO things from it, IDENTICALLY for decorated and
/// borderless windows: it clips the window content to the INNER rounded rect
/// (inset by the border width) and lays the outline band OVER the background just
/// outside it — so the content sits inside the border and a translucent border
/// blends with the desktop, not the content. It runs over the COMPOSITED
/// redirected texture, so it rounds the outer frame corners (titlebar included)
/// without ever clipping an individual client subsurface. No drop shadow is drawn
/// (KWin does not render one into this texture).
///
/// The border APPEARANCE (width / radius / colour) is no longer host state on
/// this struct: it is the resolved pack's own declared PARAMETERS, baked into the
/// CompiledSurfacePack's customParams/customColors at compile time and pushed by
/// pushBorderUniforms. This struct now only records WHICH pack chain renders and
/// the per-window hide-titlebar choice; the appearance lives with the pack.
struct WindowDecoration
{
    /// True when THIS border owns the window's OffscreenEffect redirect +
    /// border shader slot. False while an animation transition has taken over
    /// the slot (the animation path's begin/end coordinates the handover) —
    /// the per-frame uniform push and the transition-end re-apply both consult
    /// this so the border path never fights the transition lifecycle.
    bool shaderApplied = false;

    /// The resolved decoration shader-pack chain for this window
    /// (DecorationProfile::effectiveChain()), e.g. {"border"} or {"border",
    /// "glow"}. The idle present path composites the FULL chain
    /// (renderSurfaceChainComposite folds chain[1..] over the base); only the
    /// animation surface-layer path renders chain[0] (basePackId) alone.
    QStringList chain;

    /// The base pack id to render — chain.value(0), defaulting to "border".
    /// The render path (drawWindow / pushBorderUniforms / renderSurfaceChainComposite)
    /// looks this up in m_compiledPacks to get the CompiledSurfacePack instead
    /// of the old single global border shader.
    QString basePackId;

    /// True when the base "border" pack in `chain` is driven by the window
    /// rules (the rule-backed appearance path) rather than a user decoration
    /// pack. When set, the fields below carry the per-window border appearance
    /// resolved from the rules (resolveWindowAppearance); pushBorderUniforms
    /// pushes them in place of the "border" pack's shared metadata defaults so
    /// each window can show a different width / radius / colour. The shader picks
    /// activeColor vs inactiveColor by uSurfaceFocused. False for a pure
    /// user-pack chain, where the pack's own baked param defaults apply.
    bool ruleBorder = false;
    int ruleBorderWidth = 0;
    int ruleBorderRadius = 0;
    QColor ruleBorderActiveColor;
    QColor ruleBorderInactiveColor;

    /// Transparent OUTER MARGIN (logical px) the chain's packs need around
    /// the window to draw into — the max over each pack's resolved
    /// `paddingParam` value (e.g. the glow pack's glowSize). When > 0 the
    /// window takes the padded composite path: the capture canvas is
    /// inflated by this margin on every side, the packs run over the padded
    /// canvas, and apply() presents the composite on a matching padded quad
    /// (prePaintWindow marks the window transformed so KWin doesn't clip).
    /// 0 = the classic path, byte-for-byte unchanged.
    int outerPadding = 0;

    /// True when any pack in the chain declares `"needsBackdrop": true`
    /// (frost / glass). Forces the composite-fold path, has paintWindow
    /// capture the scene behind the window each frame (uBackdrop), and
    /// drives continuous repaints (the backdrop changes without any damage
    /// landing on this window).
    bool needsBackdrop = false;

    /// The window's rule-resolved opacity (SetOpacity), 1.0 when no rule
    /// applies. Custom MapTexture redirect shaders IGNORE
    /// WindowPaintData::opacity (KWin only applies it through its own
    /// default shader's modulation), so a decorated window would render
    /// fully opaque no matter the rule. When < 1.0 the window routes
    /// through the composite fold, whose window CAPTURE applies the dim
    /// (the nested draw uses KWin's default modulating shader) — content
    /// dims once, decoration packs stack over it at full strength, and a
    /// frost pack gets the translucency it fills. Kept fresh by
    /// updateWindowDecoration, which re-runs on every trigger that can change
    /// a rule verdict (focus, snap state, rule/config edits).
    double ruleOpacity = 1.0;

    /// True when any chain pack declares `"handlesOpacity": true`. The
    /// present pass then pushes uOpacity = 1.0 (no final modulation) and the
    /// declaring pack applies uSurfaceOpacity itself.
    bool chainHandlesOpacity = false;

    /// Damage bookkeeping for padded chains across window moves/resizes:
    /// KWin damages the window's own old/new rects on a geometry change, but
    /// not the margin band OUTSIDE them, so the glow would trail during a
    /// drag. updateWindowDecoration connects windowFrameGeometryChanged for
    /// padded windows and damages lastPaddedGeo ∪ the new padded rect at
    /// screen level; removeWindowDecoration disconnects.
    QRectF lastPaddedGeo;
    QMetaObject::Connection paddedGeoConnection;

    /// Texcoord-handedness cache for the padded present quad built in
    /// apply() — same rationale as ShaderTransition::handednessCached: the
    /// convention comes from KWin's own window quad and doesn't shift, so
    /// derive it once from the first source quad instead of per frame.
    bool presentHandednessCached = false;
    double uAtLeft = 0.0;
    double uAtRight = 1.0;
    double vAtTop = 0.0;
    double vAtBottom = 1.0;

    /// Per-window resolved param values for each pack in `chain`, keyed by
    /// pack id — the window's DecorationProfile overrides translated to
    /// contract slots (ShaderInternal::resolveSurfaceParamValues). Filled by
    /// updateWindowDecoration alongside the chain itself, so it refreshes on
    /// exactly the same triggers (tree change, tile/snap membership change).
    /// The render paths prefer these over the CompiledSurfacePack's baked
    /// baseline values; a missing key falls back to the baked ones.
    QHash<QString, SurfaceParamValues> packParamValues;
};

} // namespace PlasmaZones
