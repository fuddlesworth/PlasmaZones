// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderContract.h> // kMaxCustomParams
#include <PhosphorAnimation/Curve.h> // per-event timing curve + CurveState

#include <QHash> // std::hash<QString> specialization (used by the unordered_map key below)
#include <QRectF>
#include <QString>
#include <QVariant> // QVariantMap
#include <QVector4D>

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>

namespace KWin {
class EffectWindow;
class GLShader;
class GLTexture;
class LogicalOutput;
class RenderTarget;
class RenderViewport;
class Region;
class VirtualDesktop;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/// Full-screen two-texture transitions: virtual-desktop switch + show-desktop peek.
///
/// Unlike the per-window shader transitions (ShaderTransitionManager, which runs
/// on the OffscreenEffect's redirected per-window quad), these are full-screen
/// TWO-texture blends: two per-output scene captures cross-faded with a
/// desktop-class transition shader (data/animations/<id>, appliesTo
/// ["desktop"], sampling uFromDesktop / uToDesktop via desktop_transition.glsl).
/// A desktop SWITCH blends the OUTGOING desktop into the INCOMING one; a
/// show-desktop PEEK blends the windows scene into the bare desktop, forwards
/// on the hide leg and backwards on the show leg, driven by the same shader
/// node (`desktop.peek`).
///
/// The effect owns one instance. On `desktopChanged` / `showingDesktopChanged`
/// the effect resolves the `desktop.switch` / `desktop.peek` shader from the
/// shader profile tree and calls begin() / beginPeek(). With no shader assigned
/// begin() returns immediately, while beginPeek() still reaps any live peek leg
/// (see its empty-id contract below). Either way KWin's normal behaviour
/// proceeds. When a transition does start the manager forces a full-screen paint
/// via the mask and routes paintScreen through paintOutput(), which draws the
/// blend instead of the live scene until progress reaches 1.
///
/// The fullscreen claim differs per kind. A desktop SWITCH claims
/// `setActiveFullScreenEffect(m_effect)` so KWin's built-in Slide bows out —
/// taken only when no other effect holds the screen, released only while KWin
/// still reports it as ours. A PEEK never touches the claim:
/// EffectsHandler::setActiveFullScreenEffect itself cancels show desktop on
/// every null↔non-null transition, so claiming from the showingDesktopChanged
/// handler would re-entrantly unhide every window (the "animation plays but
/// windows don't hide" bug). beginPeek instead bows out if another effect
/// already holds the screen. Suppressing KWin's show-desktop script effects
/// doesn't need the claim either (windowaperture / eyeonscreen never consult
/// it) — those are unloaded by syncShowDesktopEffectSuppression while a peek
/// pack is assigned, which also keeps the peek captures free of their
/// transforms.
class DesktopTransitionManager
{
public:
    explicit DesktopTransitionManager(PlasmaZonesEffect* effect);
    ~DesktopTransitionManager();

    DesktopTransitionManager(const DesktopTransitionManager&) = delete;
    DesktopTransitionManager& operator=(const DesktopTransitionManager&) = delete;

    /// Start a transition. @p output is the switched output, or nullptr for a
    /// global (all-output) switch — in which case every output transitions from
    /// @p from to @p to. Resolves nothing itself: the caller passes the already
    /// resolved @p effectId (empty → no-op), @p params (the profile's effective
    /// pack parameters, translated into the customParams/customColors pools),
    /// @p durationMs (the motion-cascade resolved duration; floored into the
    /// animation envelope, so even a 0 becomes a sane minimum) and the optional
    /// @p progressCurve (the node's timing curve; null → linear iTime). Every
    /// resolved lifetime is clamped into the animation envelope. Capture is
    /// deferred to the first paintOutput() for each output, where a live GL
    /// context exists.
    void begin(KWin::VirtualDesktop* from, KWin::VirtualDesktop* to, KWin::LogicalOutput* output,
               const QString& effectId, const QVariantMap& params, int durationMs,
               std::shared_ptr<const PhosphorAnimation::Curve> progressCurve = nullptr);

    /// Start a show-desktop peek transition on every output. @p showing follows
    /// EffectsHandler::showingDesktopChanged: true is the HIDE leg, false the
    /// SHOW leg. Inputs mirror begin() and are resolved by the caller from the
    /// `desktop.peek` node.
    ///
    /// Both legs share ONE endpoint pair — FROM = the windows scene, TO = the
    /// bare desktop — and differ only in the direction time runs: the hide leg
    /// drives progress 0 → 1, the show leg 1 → 0, so the show leg is literally
    /// the hide leg played backwards and an asymmetric pack retraces its own
    /// motion (see paintOutput's t computation for why swapping the textures
    /// with forward progress would NOT be equivalent). The show leg takes its
    /// TO from the bare-desktop texture the hide leg cached per output — that
    /// scene cannot be re-captured once the windows are back (visible windows
    /// render black through the per-window composite, see captureDesktop's
    /// incoming-desktop note) — and captures its FROM live, since KWin unhides
    /// the windows before the signal. Outputs with no usable cache entry are
    /// skipped and settle instantly.
    ///
    /// An EMPTY @p effectId is NOT a no-op: it reaps any live peek leg and drops
    /// the bare-desktop cache, then returns. Callers rely on this — the
    /// showingDesktopChanged handler calls beginPeek unconditionally, with an
    /// empty id when no pack is runnable, so that clearing the pack mid-peek
    /// tears the leg down instead of stranding it. The same reap runs on every
    /// other bail-out (a foreign fullscreen claim, a pack that failed to
    /// prepare).
    void beginPeek(bool showing, const QString& effectId, const QVariantMap& params, int durationMs,
                   std::shared_ptr<const PhosphorAnimation::Curve> progressCurve = nullptr);

    /// True while any output has a live transition. Feeds PlasmaZonesEffect::isActive()
    /// so KWin keeps the effect in the paint chain.
    bool isRunning() const
    {
        return !m_active.empty();
    }

    /// True while @p screen specifically has a live transition. prePaintScreen gates
    /// the full-screen PAINT_SCREEN_TRANSFORMED mask on this so a per-output switch
    /// (Plasma 6.7 per-output desktops) doesn't force every NON-transitioning output
    /// through the transformed-paint path for the transition's duration.
    bool isRunningForOutput(KWin::LogicalOutput* screen) const
    {
        return m_active.find(screen) != m_active.end();
    }

    /// Paint one output's transition. Returns true when the blend was drawn (the
    /// caller must then SKIP the normal scene paint for this output); false when
    /// this output has no live transition (or it just settled) and the normal
    /// KWin scene paint should proceed. Performs the deferred capture on the
    /// first call for an output, advances progress, and tears the output's
    /// transition down when it completes.
    bool paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport, int mask,
                     const KWin::Region& deviceRegion, KWin::LogicalOutput* screen);

    /// Schedule the next frame while any transition is live (called from
    /// postPaintScreen). No-op when idle.
    ///
    /// Also REAPS transitions whose lifetime has run out. paintOutput() settles an
    /// output only when that output is painted, so an output that stops being
    /// painted mid-transition (DPMS-asleep but still enabled, or one KWin has
    /// stopped rendering) would never settle while a sibling output kept painting:
    /// m_active would never empty, the active-fullscreen-effect claim would never
    /// release, and Slide/Overview/Cube would stay suppressed for the rest of the
    /// session. postPaintScreen runs regardless of which output painted, so the
    /// wall-clock reap here makes settling independent of being painted.
    void scheduleRepaints();

    /// Drop every compiled desktop-transition shader so the next switch recompiles
    /// against freshly reloaded pack source. Called from the AnimationShaderRegistry
    /// hot-reload handler: desktop packs are served by the SAME registry as the
    /// per-window effects, so editing a `desktop.switch` pack must invalidate this
    /// cache too (the per-window ShaderTransitionManager cache clears in the same
    /// handler). Makes the compositor context current to free the GLShaders.
    void invalidateShaderCache();

    /// Drop any live transition whose OUTGOING desktop @p desktop was just removed
    /// from the pager. begin() stores a raw VirtualDesktop* that the deferred
    /// captureDesktop() consumes later; a removed desktop leaves that pointer
    /// dangling, so reap the entry (releasing the fullscreen claim when the last
    /// one goes). Wired to EffectsHandler::desktopRemoved.
    void desktopRemoved(KWin::VirtualDesktop* desktop);

    /// Drop a removed output's live transition (if any). Called from the effect's
    /// screenRemoved handler: a disconnected LogicalOutput* left in m_active would
    /// dangle — paintOutput()/scheduleRepaints() deref the key, and the fullscreen
    /// claim would never release once its output vanished. Releases the claim when
    /// the last transition goes.
    void outputRemoved(KWin::LogicalOutput* screen);

    /// Drop all live transitions and release GL resources (effect teardown /
    /// compositor reset). Clears the active-fullscreen-effect claim.
    void reset();

private:
    /// What a transition blends. Switch reconstructs the outgoing desktop and
    /// captures the incoming live scene. Both peek legs share ONE endpoint
    /// pair and differ only in the direction time runs: FROM = the windows
    /// scene (reconstructed on the hide leg from the just-hidden windows held
    /// renderable, captured live on the show leg — they are back by then), TO
    /// = the bare desktop (captured live on the hide leg, replayed from the
    /// per-output cache on the show leg — it cannot be re-captured once the
    /// windows return).
    enum class Kind {
        Switch,
        PeekHide,
        PeekShow,
    };

    struct OutputTransition
    {
        Kind kind = Kind::Switch;
        // Outgoing desktop for Kind::Switch (captureDesktop derefs it deferred,
        // see desktopRemoved). Stays null for the peek kinds — they reference no
        // desktop, so desktop removal never reaps them.
        KWin::VirtualDesktop* from = nullptr;
        // shared_ptr, not unique_ptr: the peek hide leg shares its bare-desktop
        // capture with m_peekDesktopCache, which outlives the transition to feed
        // the show leg. The switch kind still holds the only reference.
        std::shared_ptr<KWin::GLTexture> fromTex;
        std::shared_ptr<KWin::GLTexture> toTex;
        QString effectId;
        // Resolved p_<name> values packed into customParams[] slots (metadata
        // defaults merged with the profile's overrides). Same value for every
        // output in one begin(); copied per-output for the paint upload.
        std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParams{};
        // Resolved color params packed into customColors[] slots as normalised
        // rgba, in declaration order — the color pool the per-window path also
        // uploads (see shader_transitions.cpp). A `"type":"color"` pack param's
        // `p_<name>` define resolves to `customColors[N]`, so without this a
        // desktop pack's colors read transparent black.
        std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColors{};
        // Actual switch direction for direction-aware packs: wrap-corrected
        // desktop-grid delta in cells (.xy, +x = one column right, +y = one
        // row down) and the same delta normalized (.zw). Zeros when the grid
        // positions could not be resolved. Computed once in begin(), uploaded
        // as `iSwitchDelta` (declared in desktop_transition.glsl).
        QVector4D switchDelta;
        qint64 startTimeMs = 0;
        int durationMs = 0;
        // Per-event timing curve for `desktop.switch`, resolved through the
        // cascade `global → desktop → desktop.switch` (an override on the bare
        // `desktop` node would apply, though the UI exposes only the leaf, so it
        // has no separate user-facing "All"). paintOutput eases the linear
        // `elapsed / durationMs` through it before uploading iTime, so the node's
        // curve (e.g. "Ease Out") shapes the switch exactly as it shapes the
        // per-window shader path. Null → linear iTime.
        std::shared_ptr<const PhosphorAnimation::Curve> progressCurve;
        // Integration state for a STATEFUL (spring) progressCurve; stepped toward
        // target 1 by the inter-frame dt in paintOutput. Unused for stateless.
        PhosphorAnimation::CurveState progressCurveState;
        // Previous paintOutput tick time (shader-clock ms) for the spring dt.
        // -1 = no prior paint (first step gets dt 0).
        qint64 lastPaintTimeMs = -1;
        // Monotonic paint counter uploaded as iFrame, so glitch-style desktop
        // packs get a per-frame stutter that is independent of the linear iTime
        // progress — matching the canonical animation contract's iFrame.
        int frameCount = 0;
        bool captured = false;
    };

    /// An array of @p N uniform locations, every slot "unset" (-1).
    template<std::size_t N>
    static constexpr std::array<int, N> makeUnsetLocations()
    {
        std::array<int, N> locs{};
        for (int& loc : locs) {
            loc = -1;
        }
        return locs;
    }

    /// Compiled desktop-transition shader + cached uniform locations. Keyed by
    /// effectId, mirroring ShaderTransitionManager's per-effect cache.
    struct CompiledDesktopShader
    {
        std::unique_ptr<KWin::GLShader> shader; // null == compile failed (sentinel, don't retry)
        int iFromDesktopLoc = -1;
        int iToDesktopLoc = -1;
        int iTimeLoc = -1;
        int iResolutionLoc = -1;
        int iFrameLoc = -1;
        int iSwitchDeltaLoc = -1;
        // customParams[slot] / customColors[slot] uniform locations (-1 when the
        // shader omits the slot — the GLSL compiler prunes unreferenced ones).
        // Filled with -1 rather than left value-initialised: `{}` would default
        // every slot to 0, which is a VALID location, so an early-out between the
        // cache emplace and the location loop would hand out slot 0 for every
        // pruned uniform. The scalar members above default to -1 for the same
        // reason; these have to agree with them.
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams> customParamsLoc =
            makeUnsetLocations<PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams>();
        std::array<int, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors> customColorsLoc =
            makeUnsetLocations<PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors>();
    };

    /// Render every window on @p desktop that intersects @p screen into a fresh
    /// output-sized FBO, bottom-to-top in stacking order (wallpaper included —
    /// it is an on-all-desktops window). Used for the OUTGOING desktop, which is
    /// no longer current so its windows aren't in the live scene and must be
    /// reconstructed through the per-window pipeline. Returns null on allocation
    /// failure.
    ///
    /// @p outputTarget and @p outputViewport are the ON-SCREEN target this
    /// capture will ultimately be blended into. They supply the capture's device
    /// size, internal format and colour space, so the blend is a pass-through
    /// rather than a conversion — see captureFormatFor.
    std::unique_ptr<KWin::GLTexture> captureDesktop(KWin::VirtualDesktop* desktop, KWin::LogicalOutput* screen,
                                                    const KWin::RenderTarget& outputTarget,
                                                    const KWin::RenderViewport& outputViewport);

    /// Capture the LIVE composited scene for @p screen into a fresh output-sized
    /// FBO via effects->paintScreen. Used for the INCOMING desktop: it is the
    /// current desktop after the switch, so its already-visible windows render
    /// black through drawWindow (they belong to the ongoing scene paint) — the
    /// scene composite is the correct, reliable source. Returns null on failure.
    ///
    /// @p outputTarget / @p outputViewport as for captureDesktop: both captures
    /// must land in the same size, format and colour space.
    std::unique_ptr<KWin::GLTexture> captureLiveScene(int mask, KWin::LogicalOutput* screen,
                                                      const KWin::RenderTarget& outputTarget,
                                                      const KWin::RenderViewport& outputViewport);

    /// The peek hide leg's FROM texture: the bare desktop with every
    /// show-desktop-hidden window composited back on top through the direct
    /// per-window path, the same way captureDesktop reconstructs the outgoing
    /// desktop's non-scene windows (the windows are already out of this frame's
    /// scene walk by the time showingDesktopChanged fires, so a live capture
    /// cannot include them; visibility refs cannot do the job either — a ref
    /// taken during paintOutput only affects the NEXT frame's scene walk).
    ///
    /// @p bareDesktop is the already-captured TO texture: it is blitted in as
    /// the base layer when the hardware supports framebuffer blits, so the
    /// bare-desktop scene is rendered once per peek, not twice. Pass null (or
    /// run on a no-blit GPU) and the base layer re-renders via
    /// effects->paintScreen instead — identical output, one extra scene pass.
    std::unique_ptr<KWin::GLTexture> capturePeekWindowsScene(KWin::GLTexture* bareDesktop, int mask,
                                                             KWin::LogicalOutput* screen,
                                                             const KWin::RenderTarget& outputTarget,
                                                             const KWin::RenderViewport& outputViewport);

    /// Composite every stacking-order window that passes @p includeWindow into
    /// the CURRENTLY PUSHED framebuffer, bottom-to-top, through the effect's
    /// own per-window pipeline in direct-drive mode (m_directPaintCapture).
    /// Shared tail of captureDesktop and capturePeekWindowsScene — both
    /// reconstruct windows that are NOT in this frame's scene walk; windows the
    /// live scene IS painting render black through this path, so filters must
    /// exclude them.
    void compositeWindowsInto(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                              const QRectF& logicalGeometry,
                              const std::function<bool(KWin::EffectWindow*)>& includeWindow);

    /// Shared resolve prologue of begin() and beginPeek(): validates the pack
    /// (installed + desktop-contract via shaderEffectAppliesToEventPath against
    /// @p eventPath) and fills @p proto's effectId, parameter pools, start time,
    /// clamped duration and curve. Returns false when the transition must not
    /// run (the caller bails and KWin's native behaviour proceeds).
    bool prepareTransitionPrototype(const QString& effectId, const QVariantMap& params, const QString& eventPath,
                                    int durationMs, std::shared_ptr<const PhosphorAnimation::Curve> progressCurve,
                                    OutputTransition* proto);

    /// Compile (or fetch from cache) the desktop-transition shader for @p effectId.
    /// NEVER null: returns the cache entry, whose `shader` is null when the id is
    /// unknown or compilation failed. Callers must check `->shader`. Caching the
    /// failure as a non-null sentinel is what keeps a broken pack from recompiling
    /// every frame. Implemented in desktoptransitionshader.cpp (the assembly part).
    CompiledDesktopShader* compiledShader(const QString& effectId);

    /// Drop every live PEEK transition (kind-guarded; switches keep running),
    /// repainting each affected output. Called by every beginPeek bail-out so
    /// a stranded hide leg never keeps blending against a reversed toggle.
    void reapPeekTransitions();

    /// Free an output's transition and, when the last one goes, release the
    /// active-fullscreen-effect claim.
    ///
    /// This and the other teardown members (releaseFullScreenClaimIfIdle,
    /// ensureGlContextCurrent, scheduleRepaints, outputRemoved, desktopRemoved,
    /// invalidateShaderCache, reset — the last five are declared in the public
    /// section above) are implemented in desktoptransitionteardown.cpp, the
    /// class's teardown part.
    void endOutput(KWin::LogicalOutput* screen);

    /// Release the active-fullscreen-effect claim once no transition is left.
    ///
    /// Releases only when KWin still reports US as the active full-screen effect.
    /// An unconditional `setActiveFullScreenEffect(nullptr)` would clear a claim
    /// that Overview or Cube took while our transition was in flight, unsuppressing
    /// every effect that was bowing out to THEM. The claim in begin() is guarded
    /// symmetrically: it only takes the screen when nobody else holds it.
    ///
    /// DEFERS while show-desktop is active, keeping both the claim and the flag.
    /// setActiveFullScreenEffect cancels show desktop in EITHER direction, so a
    /// switch that claimed before the user triggered show-desktop would, on
    /// settling, hide the windows and then spontaneously bring them back. begin()'s
    /// `!isShowingDesktop()` guard only covers TAKING the claim; this covers
    /// dropping it. The deferred release is retried by the peek path: every
    /// showingDesktopChanged runs beginPeek, whose every bail-out reaps and lands
    /// back here, and by the showing=false edge KWin has already cleared its own
    /// state, so the write is a no-op against it.
    ///
    /// @param force skip the show-desktop deferral. For teardown only (reset()),
    /// where a claim pointing at a dying effect must not outlive it — the cancel
    /// is moot when the compositor is dropping the effect anyway.
    void releaseFullScreenClaimIfIdle(bool force = false);

    /// Make the compositor GL context current so GLTexture/GLShader frees issue
    /// their glDelete* against a live context. Every off-paint-thread mutator that
    /// releases captured GL objects (begin's re-switch, output/desktop removal,
    /// shader-cache invalidation, reset) calls this first. A no-op at teardown
    /// (!effects), where the driver reclaims the objects regardless.
    void ensureGlContextCurrent();

    PlasmaZonesEffect* m_effect;
    // Move-only mapped values (unique_ptr GL handles) — std::unordered_map, not
    // QHash, which is copy-on-write and would instantiate a deleted copy ctor.
    std::unordered_map<KWin::LogicalOutput*, OutputTransition> m_active;
    std::unordered_map<QString, CompiledDesktopShader> m_shaderCache;
    // Per-output bare-desktop capture seeded by the peek HIDE leg and consumed
    // by the SHOW leg (which cannot re-capture it — the windows are visible
    // again by the time showingDesktopChanged(false) fires). Refreshed on every
    // hide leg; validated against the show leg's viewport size and target format
    // before use. Seeded only once BOTH of the hide leg's captures succeed, so
    // an entry always has a hide leg that actually blended behind it.
    // Drop sites: with its output (outputRemoved), on reset(), on every
    // beginPeek bail-out (reapPeekTransitions — a broken hide/show pairing means
    // the capture may no longer match the desktop's content), on a desktop
    // switch touching the output (begin() — the capture is of the outgoing
    // desktop), on consumption by the show leg (paintOutput), and when a
    // never-painted SHOW leg expires — whether the wall-clock reap catches it
    // (scheduleRepaints) or its own first-paint settle does (paintOutput; an
    // output asleep for the whole leg). Hide legs are exempt from both expiry
    // drops — their cache is the pending show-leg endpoint.
    std::unordered_map<KWin::LogicalOutput*, std::shared_ptr<KWin::GLTexture>> m_peekDesktopCache;
    bool m_fullScreenClaimed = false;
};

} // namespace PlasmaZones
