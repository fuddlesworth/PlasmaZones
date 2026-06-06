// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorLayer/ISurfaceAnimator.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <memory>

namespace PhosphorAnimation {
class PhosphorProfileRegistry;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PhosphorLayer {
class Role;
class Surface;
}

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorAnimationLayer {

/**
 * @brief Concrete `PhosphorLayer::ISurfaceAnimator` driving show/hide via
 *        phosphor-animation Profiles.
 *
 * Phase 5 of the phosphor-animation roadmap. Replaces every overlay's
 * hand-rolled QML `ParallelAnimation { id: showAnimation }` block with a
 * single library-driven runtime that resolves curve + duration from
 * `PhosphorProfileRegistry`. Live profile reloads (drop a JSON, see it
 * apply on next show) flow through the registry's existing watcher path —
 * the animator re-resolves the path on every `beginShow` / `beginHide`
 * and captures the resolved Profile **by value** into the underlying
 * `MotionSpec`. In-flight animations therefore keep the Profile they
 * started with through to completion; reloads apply to the *next*
 * dispatch, not to a currently-running tick. (Verified by the
 * `profile_reload_during_flight_does_not_affect_inflight` regression
 * test in `pal_test_surface_animator`.)
 *
 * ## Per-role configuration
 *
 * The animator carries a default `Config` plus a per-`Role` override map.
 * Consumers register configs at construction:
 *
 * @code
 *     SurfaceAnimator anim(registry);
 *     anim.registerConfigForRole(PhosphorRoles::LayoutOsd,
 *         {.showProfile  = QStringLiteral("osd.show"),
 *          .showScaleProfile = QStringLiteral("osd.pop"),
 *          .showScaleFrom = 0.8,
 *          .hideProfile  = QStringLiteral("osd.hide"),
 *          .hideScaleTo  = 0.9});
 * @endcode
 *
 * Each surface's role is read from `surface->config().role` at dispatch
 * time. Surfaces whose role has no registered override use `defaultConfig`.
 *
 * ## Animations
 *
 * `beginShow` / `beginHide` drive the QQuickItem's `opacity` property
 * (always, 0↔1) and optionally its `scale` property (when a scale-profile
 * is configured). Both run in parallel against a shared steady-clock so
 * frame timing aligns. Each property animation is a
 * `PhosphorAnimation::AnimatedValue<qreal>` ticked at ~60 Hz by an internal
 * QTimer (decoupled from `QQuickWindow::beforeRendering` so offscreen QPA
 * tests work).
 *
 * ## Cancellation & supersession
 *
 * `cancel(surface)` stops in-flight animations for that surface only.
 * The Phase-5.1 dispatch in Surface calls `cancel` on every
 * Hidden→Shown / Shown→Hidden transition that supersedes a still-
 * running animation, so the SurfaceAnimator does not have to detect
 * overlap itself — the phosphor-layer side guarantees a clean cancel
 * point before each `beginShow` / `beginHide`.
 *
 * ## Re-entrancy contract
 *
 * The animator's internal driver invokes a track's `onComplete`
 * synchronously from inside its own tick loop. Consumers MUST NOT
 * delete the SurfaceAnimator (or anything that owns it) from inside
 * an `onComplete` callback — the tick loop continues iterating over
 * the track map after invocation, and a destroyed animator strands
 * the loop on freed memory. Deleting the *Surface* whose animation
 * just completed is safe; the library cancels the animator's tracking
 * entry on Surface destruction. If a callback genuinely needs to
 * tear down the animator, defer the deletion via `QTimer::singleShot(0, ...)`
 * or a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
 *
 * ## Lifetime
 *
 * Heap-allocated by the consumer (typically the daemon's OverlayService)
 * and passed to `SurfaceFactory::Deps::animator`. Must outlive every
 * Surface created from that factory; the destructor cancels any
 * still-tracked surface state but does NOT cascade-delete the surfaces
 * themselves.
 *
 * ## Thread safety
 *
 * GUI-thread only. Every method touches QQuickItem / QHash state without
 * synchronisation; the underlying `AnimatedValue<T>` runtime asserts the
 * same. Construction and `registerConfigForRole` are typically called once
 * during process / OverlayService startup but must still run on the GUI
 * thread to satisfy the QHash invariant.
 */
class PHOSPHORANIMATION_EXPORT SurfaceAnimator : public PhosphorLayer::ISurfaceAnimator
{
public:
    /**
     * @brief Per-role profile + scale tuning bundle.
     *
     * `showProfile` / `hideProfile` are *Profile path strings* (e.g.
     * "osd.show", "popup"). They're resolved live through the
     * registry on every dispatch so a settings-time profile reload
     * affects the next animation.
     *
     * `showScaleProfile` left empty disables the scale leg of the show
     * animation — opacity-only fade-in. Set non-empty to enable a parallel
     * scale animation from `showScaleFrom` → 1.0 driven by that profile's
     * curve / duration. The same convention applies to hide:
     * `hideScaleTo == 1.0` (the default) disables the hide-side scale
     * animation.
     *
     * Profile resolution failures (typo, missing JSON) fall back to the
     * library default Profile (150 ms OutCubic) — same fallback as
     * `PhosphorMotionAnimation` in the QML bindings. The fallback is
     * surfaced at `qCWarning` so a typo doesn't silently degrade the
     * animation; the QML-side `check-animation-profiles.py` build-time
     * lint catches QML references but cannot inspect C++ literals, so
     * the runtime warning is the backstop for `setupSurfaceAnimator`-
     * style registrations.
     *
     * @note **Pre-1.0 ABI.** Config is a plain aggregate exposed across
     * the DSO boundary. Adding or reordering fields between releases is
     * a binary-incompatible change until the library reaches 1.0
     * (SOVERSION 0 signals this). Consumers that aggregate-init by
     * position must rebuild against each release; prefer named-member
     * init (`Config{.showProfile = ...}`) for forward compatibility.
     */
    struct Config
    {
        QString showProfile; ///< Path resolved via registry, e.g. "osd.show"
        QString hideProfile; ///< Path resolved via registry, e.g. "osd.hide"
        QString showScaleProfile; ///< Optional. Empty → no scale animation on show.
        QString hideScaleProfile; ///< Optional. Empty → no scale animation on hide.
        qreal showScaleFrom = 1.0; ///< Initial scale value when showScaleProfile is set.
        qreal hideScaleTo = 1.0; ///< Final scale value when hideScaleProfile is set.
        /// Phase 6: shader transition effect ids. Looked up from
        /// AnimationShaderRegistry. Empty → no shader transition.
        QString showShaderEffectId;
        QString hideShaderEffectId;
        /// Optional profile paths for the shader time curve. When empty,
        /// the shader leg reuses the opacity profile's curve and duration.
        QString showShaderProfile;
        QString hideShaderProfile;
        /// Per-event shader parameter overrides forwarded to
        /// `PhosphorRendering::ShaderEffect::setShaderParams` on the
        /// shader leg. Keys are the effect's declared parameter ids
        /// (e.g. `direction`, `grain`). Resolved by the consumer from
        /// `ShaderProfile::effectiveParameters()` at config-register
        /// time; the animator forwards them verbatim. Empty → shader
        /// runs with its declared defaults.
        QVariantMap showShaderParameters;
        QVariantMap hideShaderParameters;
    };

    /// Construct against an explicit registry with caller-supplied
    /// defaults. The animator takes the registry by reference; the
    /// registry must outlive the animator. Composition roots (daemon /
    /// editor / settings) own one `PhosphorProfileRegistry` instance and
    /// thread it through; tests construct a per-fixture registry and
    /// pass it here. There is no default-singleton fallback — every
    /// caller injects their own dependency.
    explicit SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry, Config defaults);

    /// Convenience: registry-only ctor with empty default config.
    /// Equivalent to `SurfaceAnimator(registry, Config{})` but spelled
    /// out as a separate overload so unity-build paths don't get
    /// confused by a default-arg `Config{}` on the primary ctor (the
    /// previous shape `Config defaults = Config{}` triggered a parse
    /// ambiguity in the unity TU between the value-initialised default
    /// arg and a function-declarator interpretation).
    explicit SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry);

    /// Full constructor with shader registry for Phase 6 transition
    /// effects. Same DI contract as the registry-only ctors — caller
    /// owns the AnimationShaderRegistry and threads it through; pass
    /// `nullptr` to disable shader transitions. (No singleton fallback;
    /// see `SurfaceAnimator(registry, defaults)` above for the
    /// rationale.)
    SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry,
                    PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults);

    ~SurfaceAnimator() override;
    SurfaceAnimator(const SurfaceAnimator&) = delete;
    SurfaceAnimator& operator=(const SurfaceAnimator&) = delete;

    /// Override the configuration for one Role. The role's `scopePrefix`
    /// is used as the registration key. Lookup is longest-prefix-match
    /// (with `'-'` boundary) against the surface's `role.scopePrefix`,
    /// so consumers can register a config against a stable base role
    /// (e.g. `PhosphorRoles::LayoutOsd` with prefix `"phosphor-layout-osd"`)
    /// while the surfaces themselves carry per-instance roles derived via
    /// `withScopePrefix("phosphor-layout-osd-{screenId}-{gen}")` for
    /// compositor scope uniqueness. Without prefix matching, the unique
    /// per-instance suffix would make every lookup miss and silently fall
    /// back to the default config.
    void registerConfigForRole(const PhosphorLayer::Role& role, Config cfg);

    /// Read-only config lookup. Returns the registered config for @p role
    /// if one was set, otherwise `defaultConfig`.
    Config configForRole(const PhosphorLayer::Role& role) const;

    /// Mutable access to the default (unregistered-role fallback). Useful
    /// when the consumer wants to bind every overlay's default behaviour
    /// without enumerating roles.
    void setDefaultConfig(Config cfg);
    Config defaultConfig() const;

    /// Install the animation shader registry for Phase 6 shader transitions.
    /// May be called after construction (the daemon creates the registry
    /// after the overlay service). Null disables shader transitions.
    void setAnimationShaderRegistry(PhosphorAnimationShaders::AnimationShaderRegistry* registry);

    /// @name Dynamic shader uniforms
    /// @brief Cross-runtime feature parity with overlay shaders for the
    /// per-frame uniforms that don't come from the per-effect static
    /// metadata. Most are auto-driven by the animator and need no
    /// consumer wiring:
    ///
    ///   • `iTimeDelta` — real-time seconds between SurfaceAnimator
    ///     driver ticks (matches overlay's wall-clock convention).
    ///   • `iFrame` — per-leg frame counter, resets to 0 on each fresh
    ///     attach; post-incremented per push so the first frame reports 0.
    ///   • `iMouse` — driven by a `QQuickHoverHandler` installed on each
    ///     attached shader item, mirroring the overlay path's
    ///     `MouseArea { hoverEnabled }` / `HoverHandler` pattern. The
    ///     handler reports cursor position in shader-item-local pixels
    ///     (matches `iResolution`); when the cursor leaves the shader's
    ///     bounding box, `iMouse` is set to `(-1, -1)` — same off-region
    ///     sentinel the overlay path uses.
    ///   • `iDate` — auto-populated by `ShaderNodeRhi`'s scene-data sync,
    ///     throttled to 1 Hz refresh.
    ///   • `iTimeHi` — auto-computed wrap counterpart of `iTime` by
    ///     `ShaderNodeRhi`'s std140 split. Always 0 on this path since
    ///     `iTime` stays in [0,1] and never wraps. Animation shader
    ///     authors should NOT read `iTimeHi`; the field exists only to
    ///     keep the std140 layout aligned with the overlay UBO so a
    ///     single `effect.frag` source compiles against either runtime.
    ///
    /// The single uniform that NEEDS consumer wiring is
    /// `setAudioSpectrum` below — there is no centralised QML producer
    /// for CAVA data the way there is for hover events.
    /// @{

    /// Push the latest CAVA / audio-spectrum sample to every active
    /// animation shader item. Mirrors the overlay path's
    /// `OverlayService::onAudioSpectrumUpdated` → `audioSpectrum` QML
    /// property write — animation shaders programmatically attached by
    /// `SurfaceAnimator` have no QML scene to bind through, so the
    /// consumer (typically the daemon's `OverlayService`) wires its
    /// `IAudioSpectrumProvider::spectrumUpdated` signal here. The
    /// spectrum is also re-pushed at attach time so a shader that
    /// installs mid-stream sees the latest data on its first frame
    /// instead of zero-initialised silence.
    ///
    /// Pass an empty vector to clear (e.g. when audio visualization is
    /// disabled). Called at the audio framerate (typically 30-60 Hz);
    /// `ShaderEffect::setAudioSpectrum` no-ops on identity so an
    /// unchanged spectrum costs nothing.
    void setAudioSpectrum(const QVector<float>& spectrum);
    /// @}

    /// @name ISurfaceAnimator
    /// @{
    void beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) override;
    void beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete) override;
    void cancel(PhosphorLayer::Surface* surface) override;
    /// @}

    /// Role-override show/hide. The surface's own role is used for
    /// layer-shell-protocol concerns (anchors, kbd, layer); the
    /// `configRole` argument is used for animation-config resolution
    /// — replacing the implicit `configFor(surface->config().role)`
    /// lookup the no-arg variants do. Required by the unified
    /// `PassiveOverlayShell` pattern: one shell wl_surface hosts
    /// multiple per-content slots (OSD, zone-selector, snap-assist,
    /// picker), each animated with their own per-content motion +
    /// shader profiles. Without role override they'd all share the
    /// shell's role-config, which would homogenise transitions across
    /// content types.
    ///
    /// Pre-shell, every content had its own Surface and the no-arg
    /// variant resolved the right config from the surface's role
    /// directly. Post-shell, the surface's role is the shell's
    /// (PassiveShell) and we need to override per-content per-show.
    void beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem, const PhosphorLayer::Role& configRole,
                   CompletionCallback onComplete);
    void beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem, const PhosphorLayer::Role& configRole,
                   CompletionCallback onComplete);

    /// Global animation enable/disable. When disabled, `beginShow` and
    /// `beginHide` snap the rootItem to the target opacity (1.0 / 0.0)
    /// and fire the completion callback synchronously — no opacity
    /// tween, no shader leg, no scale leg. Mirrors the kwin-effect's
    /// global gate: a single `animationsEnabled` setting kills every
    /// animation on both runtimes when toggled off.
    void setEnabled(bool enabled);
    bool isEnabled() const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorAnimationLayer
