// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/Registration.h>

#include <effect/effecthandler.h>
#include <core/output.h>
#include <virtualdesktops.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>
#include <QVarLengthArray>

#include "tilinghandler/tilinghandler.h"
#include "compositor/compositorclock.h"
#include "handlers/dragtracker.h"
#include "compositor/compositorbridge.h"
#include "handlers/navigationhandler.h"
#include "handlers/screenchangehandler.h"
#include "handlers/snapassisthandler.h"
#include "handlers/snaphandler.h"
#include "compositor/stripviewanimator.h"
#include "compositor/windowanimator.h"

namespace PlasmaZones {

// `lcEffect` is defined in plasmazoneseffect.cpp via Q_LOGGING_CATEGORY. Re-declare
// here so this TU can log under the same category without re-defining storage.

// Constructor wiring, decomposed from the PlasmaZonesEffect ctor along its
// original comment seams. Called from the ctor shell (lifecycle.cpp) in this
// exact order; the "connect signals FIRST, then iterate screens" ordering the
// clocks block documents is preserved within initRenderingAndRegistries().
void PlasmaZonesEffect::initRenderingAndRegistries()
{
    // Vertex snapping stays at KWin's default (Round) here and is toggled
    // per frame by prePaintScreen: None only while an animation or shader
    // transition is in flight, Round the rest of the time. None exists for
    // sub-pixel animation smoothness (KWin's Round quantises smooth
    // translates into 1px steps — visible judder at low velocities; MagicLamp
    // uses None for its quad deformation for the same reason). But the mode
    // is effect-global and every decorated window is PERMANENTLY redirected
    // (reconcileDecorationShader), so a blanket None resampled every static
    // decorated window on a half-pixel grid at fractional output scales —
    // steady-state blur on every 1.25x/1.5x desktop (discussion #868). The
    // per-frame toggle keeps both properties: crisp at rest, smooth in
    // motion.

    // Single-worker pool for off-loading user-texture loads. See the
    // header docstring for `m_shaderManager.m_textureLoaderPool` for the rationale —
    // serialised loads keep the dedupe cheap and avoid duplicate GPU
    // uploads if multiple shader transitions reference the same file
    // in quick succession.
    m_shaderManager.m_textureLoaderPool.setMaxThreadCount(1);

    // Populate per-output clocks from the currently-known output set.
    // Subsequent hotplug events land in onScreenAdded / onScreenRemoved.
    //
    // Order: connect signals FIRST, then iterate the current screens()
    // snapshot. A screen plugged in between those two steps would
    // otherwise be missed — the signal wouldn't have an attached slot
    // yet, and the loop would already have run. With the signals
    // connected first, the worst case is a duplicate `onScreenAdded`
    // call (once via signal, once via loop). `onScreenAdded` is
    // idempotent (re-insertion check against m_motionClocksByOutput)
    // so the duplicate is a no-op.
    if (KWin::effects) {
        connect(KWin::effects, &KWin::EffectsHandler::screenAdded, this, &PlasmaZonesEffect::onScreenAdded);
        connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, &PlasmaZonesEffect::onScreenRemoved);
        for (KWin::LogicalOutput* output : KWin::effects->screens()) {
            onScreenAdded(output);
        }
        // Seed the cursor cache with the live position so the first frame
        // after a fresh shader install with iMouse declared sees the real
        // cursor. The default-constructed QPointF(0, 0) would otherwise be
        // misinterpreted as INSIDE any window whose frame contains the
        // origin (i.e. all windows on the primary monitor with origin at
        // (0, 0)) for one frame, producing a false-positive hover spike
        // before prePaintScreen overwrites the cache on the next tick.
        m_shaderManager.m_cachedCursorGlobal = KWin::effects->cursorPos();
    }

    // Wire the fallback clock as the animator's default. The animator's
    // clockForHandle override resolves the per-output clock at
    // startAnimation time; the default kicks in only when a window has
    // no resolvable output (which is rare but real — XWayland
    // bootstrap, mid-migration with a null screen()).
    m_windowAnimator->setClock(m_motionClockFallback.get());
    m_windowAnimator->setOutputClockResolver([this](KWin::LogicalOutput* output) -> PhosphorAnimation::IMotionClock* {
        return clockForOutput(output);
    });
    // The strip view rides the same per-output clocks. No fallback clock, and
    // that asymmetry with the window animator is deliberate: a window with no
    // resolvable output still has to animate somewhere, but a view offset
    // belongs to an OUTPUT by definition, so an unresolvable one has nothing
    // to slide and the batch's geometry stands on its own.
    //
    // So this is a raw map lookup rather than clockForOutput(), which falls
    // back for an unmapped output and would make the sentence above false.
    // Two things depend on the miss really being a miss: applyBatchDelta's
    // no-clock branch, which leaves the view at rest through a hotplug race
    // and is otherwise unreachable, and onScreenRemoved's reap, which matches
    // by clock pointer and cannot find a leg that bound to the fallback.
    m_stripViewAnimator->setOutputClockResolver(
        [this](KWin::LogicalOutput* output) -> PhosphorAnimation::IMotionClock* {
            if (!output) {
                return nullptr;
            }
            const auto it = m_motionClocksByOutput.find(output);
            return it == m_motionClocksByOutput.end() ? nullptr : it->second.get();
        });
    m_stripViewAnimator->setRepaintRequest([](KWin::LogicalOutput* output) {
        if (!output || !KWin::effects) {
            return;
        }
        // The whole output, not a swept per-window bound: every column on the
        // strip is moving, so there is no smaller honest region. This is the
        // cost the plan accepted for rigid motion, and it is bounded by the
        // leg's duration.
        KWin::effects->addRepaint(KWin::Region(output->geometry()));
    });
    m_windowAnimator->setOnAnimationCompleteCallback([this](KWin::EffectWindow* w) {
        // Only tear down ANIMATOR-DRIVEN shader transitions
        // (durationMs == 0; the leg rides m_windowAnimator's timeline).
        // Time-based transitions (durationMs > 0; window.open / close /
        // focus / etc.) have their own QTimer teardown scheduled by
        // tryBeginShaderForEvent — without this guard, a window.snapIn
        // transition that's been superseded by another window.* event leaves
        // the original animator running its geometry tween, and that
        // animator's eventual completion would prematurely kill the
        // successor (whose own QTimer hasn't fired yet).
        const auto* st = m_shaderManager.findTransition(w);
        if (!st || st->durationMs > 0) {
            return;
        }
        endShaderTransition(w);
    });
    connect(&m_shaderManager.m_animationShaderRegistry,
            &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this, [this]() {
                // Make the GL context current FIRST. This fires from the registry's file
                // watcher, between frames, where the compositor's context is not current
                // — and everything below is GL: endShaderTransition hands the redirect
                // back (KWin destroys the offscreen framebuffer), and the two cache
                // clears destroy GLShaders and GLTextures, i.e. glDeleteProgram and
                // glDeleteTextures. Its SIBLING handler (the surface registry's, below)
                // has done this from the start and explains why.
                //
                // It is NOT enough that endShaderTransition makes the context current
                // itself: it only runs from the drain loop, once per LIVE transition, and
                // the ordinary hot-reload (a user saves a .glsl, no window mid-animation)
                // drains nothing at all. The cache clears below then run against no
                // context. An earlier pass removed this call on exactly that reasoning and
                // left this paragraph standing over the hole.
                ensureGlContextCurrent();

                // Drain UNCONDITIONALLY. Gating the drain on the context bought nothing —
                // and worse, the residual self-heal below would notice the undrained
                // transitions, log a CRITICAL, and then drain them anyway, which made the
                // gate a false-alarm generator rather than a safety measure. Bailing
                // outright, which this first did, was worse still: it left the caches
                // populated, so a hot-reloaded pack kept rendering from its OLD compiled
                // shader for the rest of the session.
                QVarLengthArray<KWin::EffectWindow*, 8> windows;
                for (auto& [w, _] : m_shaderManager.shaderTransitions())
                    windows.push_back(w);
                for (auto* w : windows)
                    endShaderTransition(w);
                // Release-build pair for the contract: every transition entry
                // MUST drain through endShaderTransition before we clear the
                // shader and texture caches. A residual entry holds raw
                // non-owning pointers into BOTH — `ShaderTransition::cached`
                // into m_shaderCache and `ShaderTransition::userTextures` into
                // m_textureCache — so clearing either while an entry survives
                // would let the next paintWindow on that window deref freed GL
                // objects. Self-heal in production by re-running
                // endShaderTransition for the residual entries — same handler
                // the loop above uses — so a future refactor that adds an
                // early-return to endShaderTransition can't crash the
                // compositor.
                if (!m_shaderManager.empty()) {
                    qCCritical(lcEffect) << "shader manager not drained before cache clear; re-draining"
                                         << m_shaderManager.shaderTransitions().size() << "residual transitions";
                    QVarLengthArray<KWin::EffectWindow*, 8> residual;
                    for (auto& [w, _] : m_shaderManager.shaderTransitions())
                        residual.push_back(w);
                    for (auto* w : residual)
                        endShaderTransition(w);
                }
                // And if the re-drain ALSO failed to empty the map, hold both
                // caches. Detecting the residue and then freeing under it is
                // strictly worse than the stale render it was trying to
                // prevent: a use-after-free in paintWindow is a compositor
                // crash, while retained caches only mean the surviving
                // transitions keep rendering from their pre-reload programs
                // and textures. The next effectsChanged with a drained manager
                // clears both, so the staleness is bounded by the following
                // reload rather than the session.
                const bool drained = m_shaderManager.empty();
                Q_ASSERT(drained);
                if (drained) {
                    m_shaderManager.m_shaderCache.clear();
                }
                // Drop the texture cache too — a hot-reload that swaps a
                // texture file behind the same metadata.json path needs
                // a fresh upload to pick up the new contents. The cache
                // is keyed by absolute path; without this clear a
                // file-content change with no path change would never
                // refresh.
                //
                // Bump the cache generation rather than draining the
                // loader pool synchronously. `waitForDone()` on the GL
                // thread would block the compositor for tens of ms when
                // a worker is mid-rasterise of a 1024x1024 SVG (the
                // worst case for `loadUserTextureImage`). Workers
                // already in flight will complete their CPU rasterise,
                // but their queued GL upload checks the generation
                // captured at submission time against
                // `m_shaderManager.m_textureCacheGeneration` and discards if mismatched
                // — so no stale (pre-reload) bytes can re-populate the
                // cleared cache. Clear immediately so the next
                // `beginShaderTransition` hits the synchronous fallback
                // path and uploads fresh content.
                //
                // The generation bump and the in-flight set are unconditional:
                // neither frees anything a residual transition points at, and
                // retiring the in-flight loads is what keeps pre-reload bytes
                // out of the cache whether or not the cache itself is cleared.
                // The cache clear rides the same `drained` gate as the shader
                // cache above, for the userTextures pointers.
                ++m_shaderManager.m_textureCacheGeneration;
                m_shaderManager.m_textureLoadsInFlight.clear();
                if (drained) {
                    m_shaderManager.m_textureCache.clear();
                }
                // Desktop-switch packs are served by the SAME AnimationShaderRegistry
                // as the per-window effects, so a reloaded `desktop.switch` pack must
                // invalidate the DesktopTransitionManager's parallel compiled-shader
                // cache too — otherwise the next switch renders with the stale shader.
                m_desktopTransition.invalidateShaderCache();
                // …and the strip pass's, for the same reason (a reloaded
                // `scrolling.view` pack must recompile on the next scroll).
                m_stripTransition.invalidateShaderCache();
                // A pack reload can flip a pack's `audio` metadata flag, which
                // feeds the cava run gate via hasAudioReactiveAnimation().
                scheduleEffectAudioSync();
                // The registry commit is also the FIRST moment (bringup) and
                // the ONLY moment (pack install/uninstall) a suppression-owning
                // pack's validity (peek / minimize / maximize) can change
                // without a tree edit: the profile
                // tree arrives on an EARLIER D-Bus reply than the registry
                // scan, so the tree-load sync at loadShaderProfileFromDbus
                // resolves against an empty registry on session start, and
                // deleting the assigned pack from disk never touches the tree
                // at all. Re-run the suppression sync here so KWin's stock
                // effects are unloaded exactly when our pack
                // becomes runnable and restored the moment it stops being.
                syncStockEffectSuppression();
            });

    // Surface shader pack hot-reload: when a data/surface pack changes on disk,
    // drop EVERY compiled surface pack so the next paint recompiles each
    // referenced pack against the new source, and repaint so decorated windows
    // pick it up. Also drop the per-window multipass FBO state: a recompiled pack
    // whose buffer-pass COUNT changed would otherwise under-render, because the
    // composite path's chainBufferTex realloc keys on the chain pack-id list (and
    // size), not on each pack's buffer-pass count — only clearing it here forces
    // the next paint to reallocate against the new pass count. The next
    // compiledPack() call recompiles lazily per pack id.
    connect(&m_surfaceShaderRegistry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this, [this]() {
        // This fires from the registry's file watcher between frames, where the
        // compositor's GL context is NOT current. m_compiledPacks owns GLShaders
        // and m_surfaceMultipass owns GLTextures, so their destruction issues
        // glDelete* calls that want a current context (the same discipline
        // compiledPack()/surfacePresentShader() apply for off-paint callers).
        // ensureGlContextCurrent() is that one shared make-current; its only
        // false case is compositor teardown (!KWin::effects), where GL is being
        // torn down and the driver reclaims the objects regardless, so the
        // clears are safe either way. The sibling animation-registry handler
        // above uses the same helper.
        ensureGlContextCurrent();
        m_compiledPacks.clear();
        m_packBufferScaleCache.clear(); // caches the multiplier-folded product; rides the compile cache's lifetime
        m_anyCompiledPackReadsCursor = false; // re-derived as packs recompile
        m_opacityTintFallbackWarned = false; // re-arm the capture-fallback warning with the fresh compiles
        m_surfaceMultipass.clear();
        // Repaint whenever there is a compositor, NOT only when the context went current: a
        // repaint is not GL work. Gating it on the make-current result meant a transient
        // failure dropped the caches but never asked the screen to redraw, so the reloaded
        // packs would not appear until something incidental damaged the scene.
        if (KWin::effects) {
            KWin::effects->addRepaintFull();
        }
        // A pack edit can also change the registry metadata that
        // updateWindowDecoration SNAPSHOTS onto each WindowDecoration
        // (outerPadding, needsBackdrop, chainInteriorOpaque) — without a
        // re-resolve every open window keeps rendering with the old values.
        // The decoration-profile-tree loader in daemon_settings.cpp does the
        // same after its cache clears, for the same reason.
        updateAllDecorations();
        // A pack reload can flip a decoration pack's `audio` metadata flag,
        // which feeds the cava run gate via hasAudioReactiveDecoration() —
        // mirror the animation registry's effectsChanged handler above.
        scheduleEffectAudioSync();
    });
}

void PlasmaZonesEffect::initTimers()
{
    // Frame-geometry shadow flush timer. Debounces per-window
    // windowFrameGeometryChanged signals and pushes the latest geometry to
    // the daemon at ~20Hz so daemon-local shortcut handlers (float toggle,
    // etc.) have fresh geometry without a round-trip. Single-shot timer
    // re-armed on each incoming change — the flush fires at most one D-Bus
    // call per window per 50ms window regardless of how many pixels moved.
    m_frameGeometryFlushTimer = new QTimer(this);
    m_frameGeometryFlushTimer->setSingleShot(true);
    m_frameGeometryFlushTimer->setInterval(50);
    connect(m_frameGeometryFlushTimer, &QTimer::timeout, this, &PlasmaZonesEffect::flushPendingFrameGeometry);

    // Rules.rulesChanged debounce. See slotRulesChanged: the
    // daemon emits one signal per per-rule mutation, so without coalescing a
    // 50-rule batch edit fires 50 full-ruleset fetches + parses. 50ms matches
    // the frame-geometry flush above — single edits feel instant, bursts
    // collapse to a single fetch at the trailing edge.
    m_animationRulesRefreshDebounce.setSingleShot(true);
    m_animationRulesRefreshDebounce.setInterval(50);
    connect(&m_animationRulesRefreshDebounce, &QTimer::timeout, this, &PlasmaZonesEffect::loadRuleAnimationsFromDbus);
}

void PlasmaZonesEffect::connectWindowAndScreenSignals()
{
    // KWin::effects derefs here are deliberately unguarded: an Effect is
    // only ever constructed by a live EffectsHandler, so at ctor-wiring
    // time the pointer cannot be null. initRenderingAndRegistries' guard is
    // the historical outlier, kept because its screen loop doubles as a
    // no-compositor unit-test path.
    // Connect to window lifecycle signals
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &PlasmaZonesEffect::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &PlasmaZonesEffect::slotWindowClosed);

    // Panel lifecycle drives KWin's work area: a panel added, removed, or
    // resized changes the strut-excluded clientArea. Route panel windows to
    // the screen-change handler so it re-pushes the authoritative work area
    // to the daemon. Covers docks AND unmovable layer-shell surfaces (a
    // third-party shell's exclusive-zone panel is not isDock() to KWin);
    // trackDockWindow / onWindowClosed no-op for every other window.
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, m_screenChangeHandler.get(),
            &ScreenChangeHandler::trackDockWindow);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, m_screenChangeHandler.get(),
            &ScreenChangeHandler::onWindowClosed);
    // Panels mapped before the effect loaded never fire windowAdded — hook the
    // already-present panels now so a later resize of one still re-reports.
    // Skip close-grabbed dying windows: other effects' close animations can
    // hold deleted windows in the stacking order across an effect (re)load.
    for (KWin::EffectWindow* existing : KWin::effects->stackingOrder()) {
        if (!existing || existing->isDeleted()) {
            continue;
        }
        m_screenChangeHandler->trackDockWindow(existing);
    }
    // clientArea(MaximizeArea) is queried for the current virtual desktop, so a
    // panel that reserves space on only one desktop changes the work area when
    // the user switches desktops — re-push so the daemon tracks per-desktop
    // struts too.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, m_screenChangeHandler.get(),
            &ScreenChangeHandler::scheduleClientAreaReport);

    // Border overlays are built only for current-desktop windows (markWindowSnapped
    // and updateAllDecorations both gate on isOnCurrentDesktop), so the overlay for a
    // window snapped while on another desktop isn't created until that desktop
    // becomes current. Rebuild on every desktop switch so those borders appear
    // without waiting for the window to be re-activated.
    //
    // Known one-frame cost: a desktop switch usually moves each screen's resolved
    // active layout too, and this sweep re-folds appearance against the
    // active-layout entries the PREVIOUS desktop resolved — the daemon's
    // setActiveLayouts push lands after, and its change edge re-folds the
    // affected windows. So an ActiveLayout-scoped border / tint / opacity can
    // show the outgoing desktop's value for the frames between the switch and the
    // push. Not corrected here: the effect cannot resolve the daemon's
    // assignment cascade, so there is no local value to fold instead.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this, [this]() {
        updateAllDecorations();
    });

    // Per-output virtual desktops (Plasma 6.7 "switch desktops independently for
    // each screen"): report each output's current desktop so the daemon keys its
    // per-screen desktop map off real per-output switches instead of KWin's global
    // current — which flips merely on cursor movement between monitors on
    // different desktops (#648). This signal does NOT fire on cursor movement,
    // only on an actual desktop change for an output, so it is the deterministic
    // source. `output == nullptr` is a global all-output switch (per-output mode
    // off); fan out to every screen so the daemon has one code path.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this,
            [this](KWin::VirtualDesktop*, KWin::VirtualDesktop* newDesktop, KWin::EffectWindow*,
                   KWin::LogicalOutput* output) {
                if (!newDesktop) {
                    return;
                }
                if (output) {
                    reportScreenDesktop(output, static_cast<int>(newDesktop->x11DesktopNumber()));
                    return;
                }
                resyncAllScreenDesktops();
            });

    // Runtime desktop create / destroy (the dynamic-workspaces feature drives
    // both). KWin's desktop NUMBERS are positional: adding or removing a
    // desktop renumbers x11DesktopNumber() for every desktop after the
    // insertion point, but desktopChanged fires only for outputs whose current
    // VirtualDesktop OBJECT changed. Every unaffected screen therefore keeps a
    // number that now names a different desktop, on both sides — the effect's
    // own dedup cache and the daemon's per-screen map. The daemon names the
    // effect as the fixer for exactly this
    // (VirtualDesktopManager::reportScreenDesktop's contract), so re-push every
    // screen's fresh number. Deferred and coalesced: see
    // scheduleScreenDesktopResync for why the numbering cannot be read from
    // inside the signal.
    connect(KWin::effects, &KWin::EffectsHandler::desktopAdded, this, [this](KWin::VirtualDesktop*) {
        scheduleScreenDesktopResync();
    });

    // Live per-output-desktops mode. The bringup report samples this once, but
    // the compositor property is writable at runtime (System Settings, or the
    // daemon's own consented kwinrc write followed by a reconfigure). The
    // daemon's self-heal arm keys off a reported `false` to re-apply that
    // write, so without this connection the one scenario it exists for stays
    // invisible until the next daemon restart. The daemon change-gates the
    // value, so a repeat costs nothing. The connection is made once (this
    // wiring runs once per effect) and torn down with the effect by QObject's
    // receiver-side auto-disconnect.
    if (auto* vdm = KWin::VirtualDesktopManager::self()) {
        connect(vdm, &KWin::VirtualDesktopManager::perOutputVirtualDesktopsChanged, this, [this] {
            reportPerOutputDesktopsMode();
        });
    }

    // The strip view spring is per-OUTPUT while scroll state is
    // per-(screen, desktop, activity), so a desktop switch orphans any
    // residual offset: it belongs to the desktop being LEFT, and carrying
    // it across paints the incoming desktop's columns at it. Dropping is
    // also what decides the switch blend's outgoing capture, though not in
    // the direction an earlier note claimed: that capture is deferred to the
    // next paintScreen, by which time forgetOutput has already run, so the
    // drop guarantees an UNSHIFTED outgoing capture rather than avoiding a
    // frozen shifted one. Drop the spring and
    // the strip shader pass for the switched output(s); the incoming
    // desktop's own scroll state re-seeds a fresh accumulation on its next
    // batch. forgetOutput fires no repaint of its own, so damage each
    // dropped output — with a transition pack assigned the blend repaints
    // everything anyway, but a pack-less switch relies on this.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this,
            [this](KWin::VirtualDesktop* oldDesktop, KWin::VirtualDesktop* newDesktop, KWin::EffectWindow*,
                   KWin::LogicalOutput* output) {
                if (!oldDesktop || !newDesktop || oldDesktop == newDesktop) {
                    return;
                }
                const auto dropFor = [this](KWin::LogicalOutput* out) {
                    if (!out) {
                        return;
                    }
                    m_stripTransition.outputRemoved(out);
                    m_stripViewAnimator->forgetOutput(out);
                    KWin::effects->addRepaint(out->geometry());
                };
                if (output) {
                    dropFor(output);
                    return;
                }
                const auto outputs = KWin::effects->screens();
                for (KWin::LogicalOutput* out : outputs) {
                    dropFor(out);
                }
            });

    // The ACTIVITY twin of the desktop-switch drop above: scroll state is
    // per-(screen, desktop, activity), so an activity switch orphans a
    // residual offset exactly the same way, and without this the offset
    // crossed activities and painted the incoming activity's columns shifted
    // for one leg. Activities are never per-output, so every output drops.
    connect(KWin::effects, &KWin::EffectsHandler::currentActivityChanged, this,
            [this, lastActivity = KWin::effects->currentActivity()](const QString& newActivity) mutable {
                // Same-value guard, the equivalent of the desktop twin's
                // oldDesktop == newDesktop bail (this signal carries only the
                // new id, so the previous one is cached in the connection,
                // seeded from the activity current at connect time so even a
                // session-start echo of it is caught). Purely defensive: a
                // re-announced current activity (an activity-manager
                // reconnect, a session-start echo) must not kill a live view
                // leg on every output for a switch that never happened, and
                // a genuine A->B->A round trip still drops on both hops
                // because the cache follows each one.
                if (!newActivity.isEmpty() && newActivity == lastActivity) {
                    return;
                }
                lastActivity = newActivity;
                const auto outputs = KWin::effects->screens();
                for (KWin::LogicalOutput* out : outputs) {
                    if (!out) {
                        continue;
                    }
                    m_stripTransition.outputRemoved(out);
                    m_stripViewAnimator->forgetOutput(out);
                    KWin::effects->addRepaint(out->geometry());
                }
            });

    // Full-screen desktop-switch TRANSITION (separate from the daemon-reporting
    // connection above). Resolve the `desktop.switch` shader from the profile
    // tree; when one is assigned, run the two-desktop blend. An empty resolve
    // (default state / user picked None) is a no-op, so KWin's normal switch —
    // or its built-in Slide — proceeds untouched.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this,
            [this](KWin::VirtualDesktop* oldDesktop, KWin::VirtualDesktop* newDesktop, KWin::EffectWindow*,
                   KWin::LogicalOutput* output) {
                if (!oldDesktop || !newDesktop) {
                    return;
                }
                // Skip the blend for a switch the effect issued itself to
                // satisfy a daemon setScreenDesktopRequested. The owner-wins
                // snap-back is a corrective bounce the user did not ask for,
                // and blending it costs two output-sized GLTexture captures per
                // output on top of the switch that provoked it. The daemon
                // reporting, decoration, strip and client-area arms all keep
                // firing — those are correctness arms, this one is decoration.
                if (m_programmaticDesktopSwitch) {
                    return;
                }
                // Honour the global animations master toggle, exactly as the two
                // per-window shader paths do (beginShaderTransition /
                // tryBeginShaderForEvent). `animationsEnabled` drives
                // m_windowAnimator->setEnabled(); a user who turns all animations
                // off must not still get a full-screen desktop-switch blend.
                if (!m_windowAnimator->isEnabled()) {
                    return;
                }
                const PhosphorAnimationShaders::ShaderProfile profile =
                    PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(),
                                                                       PhosphorAnimation::ProfilePaths::DesktopSwitch);
                const QString effectId = profile.effectiveEffectId();
                if (effectId.isEmpty()) {
                    return;
                }
                // Per-event motion profile (curve + duration) for the desktop
                // switch in ONE walk via the shared SSOT: global animator profile
                // → `desktop` → `desktop.switch` motion-tree overrides. The base
                // is the global animator profile, so with no override the switch
                // inherits the master animation duration + curve (the global
                // slider retimes it), and BOTH duration and curve come from the
                // same base. desktop.switch is a windowless event (no per-window
                // rule scope), so pass an empty WindowQuery — the rule layer is
                // then skipped. paintOutput eases iTime through `.curve` so the
                // node's curve shapes the switch.
                const PhosphorAnimation::Profile eventMotion = resolveEventMotionProfile(
                    PhosphorAnimation::ProfilePaths::DesktopSwitch, PhosphorRules::WindowQuery{}, QString());
                const int durationMs = qRound(eventMotion.effectiveDuration());
                m_desktopTransition.begin(oldDesktop, newDesktop, output, effectId, profile.effectiveParameters(),
                                          durationMs, eventMotion.curve);
            });

    // Full-screen show-desktop PEEK transition, the sibling of the desktop
    // switch above. Resolve the `desktop.peek` shader; when one is assigned,
    // run the windows-scene / bare-desktop blend (hide leg on true, show-back
    // leg on false). One node drives both legs over the same endpoints; the
    // show leg reverses the blend rather than swapping the captures, so an
    // asymmetric pack retraces its own motion (see paintOutput). An empty
    // resolve is a no-op, so KWin's default show-desktop behaviour proceeds
    // untouched.
    // While a pack IS assigned, KWin's windowaperture / eyeonscreen script
    // effects are unloaded (syncStockEffectSuppression) — they ignore
    // the fullscreen claim, and left loaded they would leak their transforms
    // into the peek captures.
    connect(KWin::effects, &KWin::EffectsHandler::showingDesktopChanged, this, [this](bool showing) {
        // Honour the global animations master toggle, exactly as the
        // desktop-switch handler above does — folded into the resolve so the
        // empty-id call below still happens.
        QString effectId;
        PhosphorAnimationShaders::ShaderProfile profile;
        if (m_windowAnimator->isEnabled()) {
            profile = PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(),
                                                                         PhosphorAnimation::ProfilePaths::DesktopPeek);
            effectId = profile.effectiveEffectId();
        }
        if (effectId.isEmpty()) {
            // No pack runnable for THIS toggle — but beginPeek must still see
            // the signal: its empty-id contract reaps any live peek leg, so a
            // hide leg begun while a pack WAS assigned (since unassigned, or
            // animations since disabled) cannot keep blending against the
            // reversed toggle or poison the bare-desktop cache.
            m_desktopTransition.beginPeek(showing, QString(), QVariantMap(), 0, nullptr);
            return;
        }
        // Same one-walk motion resolve as desktop.switch: global animator
        // profile → `desktop` → `desktop.peek` motion-tree overrides. Peek is
        // a windowless event, so the empty WindowQuery skips the rule layer.
        const PhosphorAnimation::Profile eventMotion = resolveEventMotionProfile(
            PhosphorAnimation::ProfilePaths::DesktopPeek, PhosphorRules::WindowQuery{}, QString());
        m_desktopTransition.beginPeek(showing, effectId, profile.effectiveParameters(),
                                      qRound(eventMotion.effectiveDuration()), eventMotion.curve);
    });

    // Reap any live desktop transition whose OUTGOING desktop is removed from the
    // pager mid-switch: it captured a raw VirtualDesktop* in begin() that the
    // deferred captureDesktop() still COMPARES (isOnDesktop) up to the
    // transition's duration later. It never derefs it, so this is not a crash
    // guard — a freed pointer must simply not linger and be matched against a
    // live desktop.
    connect(KWin::effects, &KWin::EffectsHandler::desktopRemoved, this, [this](KWin::VirtualDesktop* desktop) {
        m_desktopTransition.desktopRemoved(desktop);
        // Removal renumbers every surviving desktop after it, silently
        // invalidating both the effect's dedup cache and the daemon's
        // per-screen map for every screen KWin did not switch. Twin of the
        // desktopAdded arm above; see it for the full rationale.
        scheduleScreenDesktopResync();
    });

    // Belt-and-suspenders: windowClosed removes animations, but if a deferred
    // timer re-adds one between windowClosed and windowDeleted, the Item tree
    // will be torn down while an animation entry still references the window.
    // Purge here to prevent SIGSEGV in the per-frame damage sweep
    // (scheduleRepaints → expandedPadding → expandedGeometry).
    // Also clean up caches that slotWindowClosed may have already cleared —
    // QHash::take/remove on missing keys is a no-op, so this is safe.
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, [this](KWin::EffectWindow* w) {
        endShaderTransition(w);
        m_windowAnimator->removeAnimation(w);
        if (m_idCaches.windowIdCache.contains(w)) {
            const QString cachedId = m_idCaches.windowIdCache.take(w);
            m_idCaches.windowIdReverse.remove(cachedId);
            // Free the border entry AND its multipass FBO targets keyed by this
            // window id. Normally removeWindowDecoration (run from slotWindowClosed)
            // already cleared both; the explicit call here is defence-in-depth
            // for a window deleted without a preceding close. Pass the window
            // pointer so the GL release (setShader(nullptr) + unredirect) can
            // still run — findWindowById returns null post-delete. Critically
            // this also drops the m_windowDecorations entry, so a delete-without-
            // close can't strand it and keep isActive() pinned true.
            removeWindowDecoration(cachedId, w);
            // Belt-and-suspenders for the not-expected case of a multipass entry
            // without a border entry (removeWindowDecoration's no-border early-return
            // would otherwise skip the FBO cleanup).
            //
            // Its own make-current, not one borrowed from the endShaderTransition above:
            // this destroys GLTextures and GLFramebuffers, we are off the paint cycle, and
            // the day that neighbour grows an early return this would silently become
            // glDelete* against no context. The window is gone, so the transition guard
            // releaseSurfaceState applies has nothing left to protect — erase directly.
            if (m_surfaceMultipass.contains(cachedId)) {
                ensureGlContextCurrent();
                m_surfaceMultipass.erase(cachedId);
            }
            // Mirror the m_pendingFrameGeometry cleanup that
            // slotWindowClosed runs (window_lifecycle.cpp). A
            // windowFrameGeometryChanged emission between
            // slotWindowClosed and windowDeleted (possible for
            // windows held alive via WindowClosedGrabRole) would
            // re-insert into the pending map; without this belt-
            // and-suspenders cleanup the entry would leak for the
            // rest of the session. Keyed by `cachedId` (composite
            // appId|uuid) which is the same key the pending map
            // uses on the push side.
            m_pendingFrameGeometry.remove(cachedId);
            // Same belt-and-suspenders as m_frameOpacityCache below: a closing
            // decorated window keeps painting under its close animation, and
            // pushBorderUniforms re-creates the m_focusFade entry via operator[]
            // on every such frame AFTER slotWindowClosed already scrubbed it. So
            // the slotWindowClosed removal alone is not enough; drop it here too
            // (keyed by the frozen cachedId) or the entry leaks for the session.
            m_focusFade.remove(cachedId);
            // Same delete-without-close defence for the layer snapshot: the
            // normal removal lives in slotWindowClosed, which a window deleted
            // without a preceding close never reaches. No restore is possible
            // (the window is gone); this only keeps the map bounded.
            m_ruleWindowLayerSnapshots.remove(cachedId);
            // And for the parked-column paint hint, whose normal removal is
            // also close-path (window_lifecycle) or daemon-teardown. A
            // stranded entry is never READ back (the paint-side probes key
            // on a LIVE window's id), so this is purely bounding the map.
            m_scrollVisualDelta.remove(cachedId);
            // Windowed-fullscreen membership keeps the same backstop pairing
            // (slotWindowClosed removes it first in every ordering KWin
            // provides; this bounds the map if that ever changes). The
            // keep-flag snapshot rides along.
            m_windowedFullscreenWindows.remove(cachedId);
            m_windowedFsLayerSnapshots.remove(cachedId);
            m_lastReportedMinSize.remove(cachedId);
            m_scrollCommandedRects.remove(cachedId);
            m_scrollOfferedColumn.remove(cachedId);
        }
        m_trackedScreenPerWindow.remove(w);
        // The corpse's frozen strip displacement dies with it. This is THE
        // remover, not a backstop: the entry exists precisely so the corpse
        // paints displaced until this moment, and the pointer keying makes
        // erasing here mandatory address-reuse safety besides (a reused
        // EffectWindow address inheriting a dead corpse's offset would draw a
        // brand-new window a pan away).
        m_scrollCorpseFreeze.remove(w);
        // Desktop-set stamp, same raw-pointer keying and the same two reasons
        // as the tracked screen above: keep the hash bounded, and stop a reused
        // address from inheriting a dead window's desktop set (which would make
        // the arrival arm misread the new window's first desktop edit).
        m_trackedDesktopsPerWindow.remove(w);
        // Wired-window guard. The connections themselves die with the window, so
        // this is address-reuse safety, not connection hygiene: a stale entry
        // would make setupWindowConnections REFUSE to wire a new window that
        // reused a dead one's address, which fails silent and total.
        m_wiredWindows.remove(w);
        m_restoreSuppress.remove(w);
        // Pending deferred geometry replay. The connection itself dies with the
        // window, so this is not a dangling-connection guard — it is the same
        // address-reuse safety as the stamps below: a stale handle here would
        // make the next defer for a recycled address disconnect a replay that
        // belongs to the new window.
        m_deferredGeometryReplay.remove(w);
        // Spurious-minimize-pair stamp — raw-pointer-keyed like its
        // siblings below, so erase here both to stay bounded and so a
        // reused address can't inherit a stale stamp that would swallow
        // the new window's first genuine un-minimize animation.
        m_minimizeShaderStamp.remove(w);
        // Drop per-window shader-event bookkeeping. m_lastFocusShaderWindow is
        // a QPointer that auto-nulls on destroy, so it's already cleaned up;
        // m_shaderManager.m_lastFullyMaximized is a raw-pointer-keyed QHash so we explicitly
        // erase here to keep it bounded across long sessions.
        m_shaderManager.m_lastFullyMaximized.remove(w);
        m_lastPushedCaption.remove(w);
        // Sibling raw-pointer-keyed hashes — the maximize morph's departure
        // rect and the deferred-install entry. Same bounded-across-long-
        // sessions rationale as above, plus address-reuse safety for the
        // pending entry (a stale entry at a reused address would fire a
        // bogus morph on the new window's first resize).
        m_shaderManager.m_preMaximizeFrame.remove(w);
        m_shaderManager.m_pendingMaximizeMorph.remove(w);
        // Drop the queued-expiry guard for this raw pointer. KWin reuses
        // EffectWindow heap addresses freely, so a stale entry surviving
        // past windowDeleted would cause the next window allocated at the
        // same address to skip its first expiry queue (paint_pipeline.cpp's
        // `m_pendingShaderExpiryEnd.contains(w)` check would see the stale
        // entry and decline to insert a fresh one), leaking that window's
        // first lifecycle-event teardown. endShaderTransition above also
        // removes this entry as defence-in-depth, but if a teardown ran
        // earlier in the session and the queued lambda was still pending
        // when windowDeleted fires, the lambda's safeWindow QPointer
        // catches deletion — the bare set entry then needs explicit
        // cleanup here.
        m_shaderManager.m_pendingShaderExpiryEnd.remove(w);
        // Drop the per-frame SetOpacity cache entry for this window. The cache
        // is normally cleared at postPaintScreen, but a window deleted
        // mid-frame leaves a stale raw-pointer key; KWin reuses EffectWindow
        // heap addresses, so a stale entry surviving until postPaintScreen
        // could be read by a paintWindow call that landed at the same
        // address.
        m_shaderManager.m_frameOpacityCache.remove(w);
    });

    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &PlasmaZonesEffect::slotWindowActivated);

    // Update the daemon's primary screen override when KDE Display Settings change
    if (auto* ws = KWin::Workspace::self()) {
        connect(ws, &KWin::Workspace::outputOrderChanged, this, [this]() {
            auto* workspace = KWin::Workspace::self();
            if (workspace && m_daemonGate.serviceRegistered) {
                const auto outputs = workspace->outputOrder();
                if (!outputs.isEmpty()) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                        {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
                }
            }
        });
    }

    // mouseChanged is the only reliable way to get modifier state in a KWin effect on Wayland;
    // QGuiApplication::queryKeyboardModifiers() doesn't work since effects run in the compositor.
    connect(KWin::effects, &KWin::EffectsHandler::mouseChanged, this, &PlasmaZonesEffect::slotMouseChanged);

    // Connect to screen geometry changes for keepWindowsInZonesOnResolutionChange feature
    // In KWin 6, use virtualScreenGeometryChanged (not per-screen signal)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenGeometryChanged);

    // Discussion #527 follow-up: latch the screen-change flag the instant KWin
    // tells us an output appeared or disappeared. KWin fires screenAdded /
    // screenRemoved BEFORE the per-window outputChanged signals it emits for
    // windows it reassigns as part of the layout change, so this beats the
    // race where outputChanged would reach the autotile-delegation guard in
    // window_connections.cpp without isScreenChangeInProgress() set — and
    // when KWin shifts a remaining monitor's x-offset on the second add
    // (DPMS wake of a dual-monitor setup), oldScreenStillConnected returns
    // true and is no help on its own. slotScreenLayoutChanged sets the same
    // pending flag + debounce that virtualScreenGeometryChanged eventually
    // would, so the existing settle path is unchanged once it catches up.
    connect(KWin::effects, &KWin::EffectsHandler::screenAdded, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenLayoutChanged);
    connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenLayoutChanged);
    // Invalidate screen ID cache and refresh virtual screen definitions on screen changes
    // (connector names may be reassigned, physical screen geometry changes invalidate
    // virtual screen absolute geometry)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, this, [this]() {
        m_idCaches.screenIdCache.clear();
        m_idCaches.connectedPhysicalIdsValid = false;
        m_lastEffectiveScreenId.clear();
        // A rotation or mode change keeps the same connector and EDID id, so
        // no per-window outputChanged fires — yet ScreenOrientation is a
        // matchable rule field stamped live from the output geometry, and
        // every verdict cache keys on (windowId, ruleSet revision) only.
        // Drop the caches and sweep the borders so orientation-scoped rules
        // re-resolve, mirroring the daemon-ready re-seed pattern.
        invalidateAllRuleCaches();
        scheduleBorderSweep();
    });
}

} // namespace PlasmaZones
