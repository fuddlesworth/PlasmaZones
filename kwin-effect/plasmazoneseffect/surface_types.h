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

#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QColor>
#include <QHash>
#include <QPointF>
#include <QRect>
#include <QSize>
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

    /// Comparable so updateWindowDecoration can ask whether a re-resolve actually
    /// MOVED a window's pack parameters. It usually has not (a focus change re-runs
    /// the whole resolve and lands on the same values), and the cached static-prefix
    /// fold that bakes them only has to be dropped when they really changed.
    bool operator==(const SurfaceParamValues&) const = default;
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
/// fail-closed: a pack whose compile fails caches an entry with a NULL shader, and
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

    // Contract uniform locations (1:1 with PhosphorSurfaceShaders::SurfaceShaderContract).
    int uSurfaceSizeLoc = -1; ///< uSurfaceSize — uTexture0 extent, device px
    int uFrameTopLeftLoc = -1; ///< uSurfaceFrameTopLeft — frame top-left within the texture, device px
    int uFrameSizeLoc = -1; ///< uSurfaceFrameSize — frame size excluding shadows, device px
    int uScaleLoc = -1; ///< uSurfaceScale — logical-to-device pixel scale
    int uFocusedLoc = -1; ///< uSurfaceFocused — 1.0 focused / 0.0 unfocused
    int uOpacityLoc = -1; ///< uSurfaceOpacity — LEGACY, pushed as a constant 1.0 (retired rule feed)
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
    /// iAudioSpectrumSizeLoc >= 0 is audio-reactive at the shader level: it drives
    /// the per-frame repaint gate (windowSurfaceAnimates) and the bind. The cava
    /// run gate is driven SEPARATELY by the pack's `audio` metadata (see
    /// hasAudioReactiveDecoration), which resolves before a pack's first compile
    /// and so cannot key on this linked location.
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
    /// customColors[N]. The built-in "border" pack declares borderWidth /
    /// cornerRadius / useSystemAccent (customParams[0]) and active/inactive
    /// colours (customColors[0..1]); its per-window rule/config appearance
    /// rides WindowDecoration::packParamValues, routed by param id like any
    /// other pack override — pushBorderUniforms has no border-specific slot
    /// writes. Slots a pack does not reference resolve to -1 and push nothing.
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

/// The cursor, when it is not over a window's canvas. Far outside any real screen, so a
/// never-folded state can never compare equal to a live pointer.
///
/// ONE definition, shared by the two that must agree EXACTLY: the fold keys its cache on
/// it and the repaint driver decides whether to drive on it. (The shader is handed its own
/// (-1, -1) "cursor absent" sentinel instead — a canvas-relative value, not a global one —
/// so it is deliberately not this constant.)
inline constexpr QPointF kCursorOutside(-1.0e9, -1.0e9);

/// What one fold decided to reuse, computed up front by planSurfaceFold().
///
/// The fold's cache decisions used to be interleaved with the folding itself, in pieces
/// that had to agree with each other and periodically did not. They live here now, and
/// the fold only executes them.
struct SurfaceFoldPlan
{
    /// May the chain animate at all right now (Decorations.Performance)? False means a
    /// stopped clock, silence, and no cursor — which is what makes it cacheable.
    bool mayAnimate = true;
    /// The clock this fold pushes as iTime — the window's OWN, which stops while it is
    /// not animating. Never a raw shared-clock read. See SurfaceMultipassState.
    float foldTime = 0.0f;
    /// Can the window capture be reused across frames? False under a live transition,
    /// which re-captures every frame.
    bool captureCacheable = true;
    /// Which texture the capture belongs in THIS fold — compositeTex[0] when no pack
    /// compiles (nothing folds, so the capture is the composite), captureTex otherwise.
    /// A chain can cross that line at runtime, and a capture cached on the wrong side of
    /// it is not a capture at all.
    bool captureInComposite = false;

    int foldablePacks = 0; ///< packs in the chain that compiled and therefore draw
    int staticPrefix = 0; ///< chain INDEX where the cacheable head ends
    int lastStaticDraw = -1; ///< the last of those (where the prefix cache is written)
    bool usePrefix = false; ///< is the static-prefix cache worth keeping for this chain?
    bool allStatic = false; ///< nothing in the chain varies per frame: cache it entire

    /// The STATE this fold bakes in, and therefore the key its cache is valid under.
    float foldFocus = 0.0f;
    float foldOpacity = 1.0f;
    QPointF foldCursor = kCursorOutside;
};

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
    /// WHICH texture the valid capture is sitting in.
    ///
    /// It has two possible homes: captureTex normally, or compositeTex[0] for the
    /// degenerate chain where no pack compiled (nothing folds, so the capture is the
    /// final composite and is presented straight from there). captureValid alone says
    /// only that A capture is current, not where — and a chain can cross the
    /// "does any pack compile" line at runtime, when the decoration tree changes or a
    /// broken pack is fixed and recompiles. Skipping the capture then leaves the fold
    /// reading captureTex, which in that case was allocated and never written: the
    /// window renders garbage until it happens to damage. The mirror case presents the
    /// last folded composite as though it were the raw window.
    bool captureInComposite = false;

    /// The composite after folding the chain's leading run of STATIC packs (those for
    /// which packVariesPerFrame is false) over the capture. Those packs are a pure
    /// function of the
    /// capture and their parameters, so while the capture holds, their fold holds
    /// too and the animated packs downstream can run straight off this instead.
    ///
    /// Cacheability is a property of the PREFIX, not of a pack on its own: each
    /// pack folds over the running composite, so the first time-varying pack makes
    /// every pack after it time-varying as well, however simple those are. Hence
    /// the leading run, and hence `prefixChainEnd` — the chain INDEX at which the run
    /// this texture folded ended, which must match the chain's current static run for
    /// the cache to be reusable.
    ///
    /// An INDEX, not a count, and the difference is the one that already produced a bug
    /// once: a pack in the run that fails to compile is skipped, so the number of packs
    /// actually folded (`staticFoldable`) is SMALLER than the index the run reaches
    /// (`staticPrefix`). This stores and is compared against the latter. The field was
    /// called prefixPackCount, which invited exactly the count-vs-index mix-up the
    /// surface_capture comparison warns about.
    ///
    /// Invalidated with the capture (it is downstream of it), and by any chain or
    /// size change, which already rebuild everything here.
    ///
    /// Allocated LAZILY, and only for a chain that has a cacheable run followed by a
    /// per-frame pack. Most chains do not — the default ["border"] has no per-frame
    /// pack at all — so an eager allocation was a full-canvas RGBA8 per decorated
    /// window that was never written and never read. Null whenever the chain has no
    /// use for it.
    std::unique_ptr<KWin::GLTexture> prefixTex;
    std::unique_ptr<KWin::GLFramebuffer> prefixFbo;
    bool prefixValid = false;
    int prefixChainEnd = -1;

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

    /// The STATE the cached fold above was baked with.
    ///
    /// Focus and rule-opacity are the two inputs a pack reads that are constant
    /// between events rather than different every frame, so they are cache KEYS, not
    /// disqualifiers (see packVariesPerFrame). Recording them is what lets the
    /// default `border` chain cache at all: that pack mixes its active and inactive
    /// colours on uSurfaceFocused, so treating focus as per-frame disqualified the
    /// most common decorated window on the desktop from both caches.
    ///
    /// A settled window folds once and holds. It re-folds across a focus cross-fade
    /// (the ramp moves every frame, and the border genuinely changes colour) and on
    /// a rule-opacity change, then settles again — the ramp clamps to exactly 0.0 /
    /// 1.0 at its ends, so it terminates rather than jittering the cache forever.
    ///
    /// Sentinels chosen so a never-folded state can never compare equal to a real
    /// value: focus is a clamped 0..1 and opacity a clamped 0..1.
    float foldedFocus = -1.0f;
    float foldedOpacity = -1.0f;
    /// The cursor the cached fold baked in, in global logical coords — a cache KEY, like
    /// focus and opacity, for the chains that read iMouse. The sentinel is far outside
    /// any real screen, so a never-folded state can never compare equal to a live cursor.
    /// A hover pack therefore re-folds when the pointer MOVES and holds still otherwise,
    /// instead of re-folding at vsync forever with the pointer parked.
    QPointF foldedCursor = kCursorOutside;

    /// The window's own animation clock, which STOPS whenever the window is not being
    /// folded and RESUMES where it stopped.
    ///
    /// Not the shared clock. That one runs on regardless, so a window that was not
    /// folding for ten minutes would resume by jumping its iTime ten minutes forward in
    /// a single frame, and every periodic pack (anything on sin(iTime)) would pop to an
    /// unrelated phase. Two things stop a window folding, and BOTH have to be accounted
    /// or the jump comes back through whichever one was missed:
    ///
    ///   Decorations.Performance paused it (an idle session, or an unfocused window
    ///   under "animate the focused window only"). Tracked by pausedAtMs.
    ///
    ///   Nothing painted it at all — minimized, on another desktop, fully occluded.
    ///   Nothing tells us that happened, so it is inferred from the GAP since the last
    ///   fold. This is the far more common case, and it is why the accounting cannot
    ///   simply hang off the pause gate.
    ///
    ///   pausedAtMs    the shared clock when a GATED pause began; negative when running
    ///   timeOffsetMs  total time this window has not been animating, subtracted from the
    ///                 shared clock to give its own
    ///
    /// INTEGER MILLISECONDS, not float seconds. The shared clock counts from an epoch that
    /// grows with uptime, and a float's resolution decays with magnitude: after a week the
    /// ULP is ~62 ms, four frames, so every sin(iTime) pack would visibly step. Worse, the
    /// value we actually want is the DIFFERENCE of two large near-equal numbers, which is
    /// exactly where float cancellation bites. Integers represent it exactly, and the cast
    /// to float happens once, at the uniform, on a value the offset has already brought
    /// back down to a small number of seconds.
    qint64 pausedAtMs = -1;
    qint64 timeOffsetMs = 0;

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

    /// Every output that has blitted into the CURRENT accumulation generation.
    ///
    /// This, and NOT a clock, is what separates one generation from the next. A canvas
    /// straddling several outputs is blitted once per output, and those slices must UNION
    /// into one valid rect — but a blit from an output ALREADY in this generation is the next
    /// frame for that output and must RESTART the rect, or a window that has moved keeps
    /// claiming canvas it no longer captures. Outputs have independent frame clocks, so no
    /// clock can tell those two cases apart.
    ///
    /// A SET, not "the last output that blitted". With two outputs both covering the canvas
    /// the blits alternate A, B, A, B — so "different from the last one" is true every single
    /// time, the restart never fires, and the rect only ever grows. That is the same
    /// never-expiring generation the wall-clock version had, which is what this replaced.
    QList<QRectF> backdropGenerationOutputs;

    /// A backdrop repaint has been ASKED FOR and has not yet produced a fold.
    ///
    /// The backdrop driver is the one repaint source with no damage behind it: the scene
    /// moves, this window does not, so nothing else will schedule the refold. It asks for
    /// one every ~33ms. But a repaint is a REQUEST, not a promise — KWin declines to paint
    /// a window that is fully occluded by an opaque one above it, so the fold never runs,
    /// lastFoldMs never advances, and a clock-only test says "due" again on the very next
    /// frame. That is a full repaint every vsync, forever, and the desktop can never idle.
    ///
    /// So the driver arms this when it asks, and only a fold that ACTUALLY FOLDS disarms
    /// it. An unpainted window costs exactly ONE wasted repaint instead of one per frame; a
    /// window that is really being painted behaves exactly as before, because its fold
    /// clears the flag on the very frame the repaint lands.
    ///
    /// The cached-composite early return deliberately does NOT clear it. Reaching that
    /// return proves nothing in the chain varies per frame, so no refold can change a pixel
    /// and the driver must stop asking. Clearing it there turned this one-shot into a
    /// permanent 30Hz wake-up for any window whose needsBackdrop metadata outlives its
    /// shader's actual use of uBackdrop.
    bool backdropRepaintPending = false;

    /// A hover repaint has been ASKED FOR and has not yet produced a fold. Exactly the same
    /// hazard as backdropRepaintPending, on exactly the same reasoning: the hover driver
    /// fires on every pointer-motion event and converges by comparing foldedCursor, which
    /// only a FOLD can advance. An occluded window is never painted, so it never folds, so
    /// its foldedCursor can never converge on the live pointer — and it would take a full
    /// repaint request per motion event, forever, on a window nobody can see.
    bool hoverRepaintPending = false;

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
/// pushBorderUniforms. This struct records WHICH pack chain renders and the per-window
/// values that feed it; the appearance lives with the pack. (Title-bar hiding is not here
/// at all — it is persistent window state, owned by DecorationManager.)
struct WindowDecoration
{
    /// True when THIS border owns the window's OffscreenEffect redirect +
    /// border shader slot. False while an animation transition has taken over
    /// the slot (the animation path's begin/end coordinates the handover) —
    /// the per-frame uniform push and the transition-end re-apply both consult
    /// this so the border path never fights the transition lifecycle.
    bool shaderApplied = false;

    /// The resolved decoration shader-pack chain for this window: either the
    /// built-in {"border"} base (easy mode — no user packs anywhere on the
    /// window's surface path) or the user's own pack chain, e.g. {"glow",
    /// "border-sweep"} (custom mode — any user pack suppresses the plain
    /// border outright, see updateWindowDecoration). The idle present path
    /// composites the FULL chain (renderSurfaceChainComposite folds
    /// chain[1..] over the base); only the animation surface-layer path
    /// renders chain[0] (basePackId) alone.
    QStringList chain;

    /// The base pack id to render — chain.value(0), defaulting to "border".
    /// The render path (drawWindow / pushBorderUniforms / renderSurfaceChainComposite)
    /// looks this up in m_compiledPacks to get the CompiledSurfacePack instead
    /// of the old single global border shader.
    QString basePackId;

    /// Transparent OUTER MARGIN (logical px) the chain's packs need around
    /// the window to draw into — the max over each pack's resolved
    /// `paddingParam` value (e.g. the glow pack's glowSize). When > 0 the
    /// window takes the padded composite path: the capture canvas is
    /// inflated by this margin on every side, the packs run over the padded
    /// canvas, and apply() presents the composite on a matching padded quad
    /// (prePaintWindow marks the window transformed so KWin doesn't clip).
    /// 0 = the classic path, byte-for-byte unchanged.
    int outerPadding = 0;

    /// True when any pack in the chain declares `"needsBackdrop": true` (frost / glass).
    /// Forces the composite-fold path, has paintWindow capture the scene behind the
    /// window each frame (uBackdrop), and drives continuous repaints (the backdrop
    /// changes without any damage landing on this window).
    ///
    /// METADATA, and it can over-report: a pack may declare the flag while its compiled
    /// GLSL never actually links uBackdrop / uHasBackdrop / uBackdropRect. The FOLD reads
    /// the linked uniforms instead (packVariesPerFrame) and will happily cache such a
    /// chain, while this flag keeps the ~30fps backdrop refold driver running. The fold
    /// stamps lastFoldMs on its cached-composite early return precisely so that driver
    /// rate-limits instead of firing every vsync — see the note there. The metadata is
    /// still the right gate for the CAPTURE (the scene must be captured before any pack
    /// can be asked whether it sampled it), so the two are not redundant.
    bool needsBackdrop = false;

    /// True when the chain carries the plain opacity-tint layer, whose
    /// opacity param is the resolved config + SetOpacity fold — the chain
    /// BAKES the window's opacity into its composite. Sole runtime consumer
    /// is the transition iWindowOpacity push: 1.0 when the fold's composite
    /// is what the transition samples, the foldedOpacity fallback otherwise
    /// (refined by the per-frame rule cache when one is populated).
    /// SetOpacity has no other application path: custom chains configure
    /// their own dimming through pack params (frost/glass contentOpacity).
    bool chainBakesOpacity = false;

    /// The effective opacity folded into the opacity-tint layer's `opacity`
    /// param (config default, SetOpacity rule winning); 1.0 when the layer is
    /// off. Two direct consumers, both fallbacks for paths where the fold's
    /// composite is not what reaches the screen: the fold's failed-compile
    /// fallback (the opacity-tint pack has no compiled shader, so the window
    /// CAPTURE dims by this value under KWin's default modulating shader),
    /// and the transition iWindowOpacity push on the bare-uTexture0 fallback
    /// of an opacity-baking chain (paintWindow). Single-apply holds on both —
    /// they fire only when the pack that owns the value did not run. Every
    /// other path reads the value through packParamValues like any pack
    /// param.
    double foldedOpacity = 1.0;

    /// Damage bookkeeping for padded chains across window moves/resizes:
    /// KWin damages the window's own old/new rects on a geometry change, but
    /// not the margin band OUTSIDE them, so the glow would trail during a
    /// drag. updateWindowDecoration connects windowFrameGeometryChanged for
    /// padded windows and damages lastPaddedGeo ∪ the new padded rect at
    /// screen level; removeWindowDecoration disconnects.
    QRectF lastPaddedGeo;
    QMetaObject::Connection paddedGeoConnection;
    /// Clears SurfaceMultipassState::captureValid when the window's own content
    /// changes, so the fold re-captures. Connected for EVERY decorated window
    /// (not just padded ones); disconnected by removeWindowDecoration.
    QMetaObject::Connection damageConnection;

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
