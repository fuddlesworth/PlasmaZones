// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/AnimationShaderEffect.h> // shaderEffectAppliesToEventPath (peek suppression gate)
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/Registration.h>

#include <effect/effecthandler.h>
#include <core/output.h>
#include <virtualdesktops.h>
#include <workspace.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QEvent>
#include <QLocale>
#include <QLoggingCategory>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <QVarLengthArray>

#include "input_filter.h"
#include "tilinghandler/tilinghandler.h"
#include "compositor/compositorclock.h"
#include "handlers/dragtracker.h"
#include "compositor/compositorbridge.h"
#include "handlers/navigationhandler.h"
#include "handlers/screenchangehandler.h"
#include "handlers/snapassisthandler.h"
#include "handlers/snaphandler.h"
#include "compositor/stripviewanimator.h"
#include "compositor/scrolltabindicatorpainter.h"
#include "compositor/windowanimator.h"

namespace PlasmaZones {

// `lcEffect` is defined in plasmazoneseffect.cpp via Q_LOGGING_CATEGORY. Re-declare
// here so this TU can log under the same category without re-defining storage.

PlasmaZonesEffect::PlasmaZonesEffect()
    : OffscreenEffect()
    , m_tilingHandler(std::make_unique<TilingHandler>(this))
    , m_snapHandler(std::make_unique<SnapHandler>(this))
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
    , m_screenChangeHandler(std::make_unique<ScreenChangeHandler>(this))
    , m_snapAssistHandler(std::make_unique<SnapAssistHandler>(this))
    // Phase 3: per-output motion clocks drive every AnimatedValue in the
    // controller. One `CompositorClock` per `LogicalOutput` so mixed
    // refresh-rate displays (60 Hz + 144 Hz being the common case)
    // phase-lock independently instead of beating against a shared
    // process-wide clock. Populated below via effects->screens() and
    // maintained via the screenAdded/screenRemoved signals. The
    // fallback unbound clock covers: (a) the bootstrap window before
    // screens() populates, (b) windows whose `screen()` is null
    // mid-migration, (c) test paths that don't drive KWin::effects.
    , m_motionClockFallback(std::make_unique<CompositorClock>(nullptr))
    , m_windowAnimator(std::make_unique<WindowAnimator>())
    , m_stripViewAnimator(std::make_unique<StripViewAnimator>())
    , m_scrollTabPainter(std::make_unique<ScrollTabIndicatorPainter>())
    , m_shaderManager(this)
    , m_desktopTransition(this)
    , m_stripTransition(this)
    , m_dragTracker(std::make_unique<DragTracker>(this))
    , m_compositorBridge(std::make_unique<KWinCompositorBridge>(*this))
    , m_decorationManager(std::make_unique<DecorationManager>(*m_compositorBridge))
{
    PhosphorProtocol::registerWireTypes();

    // The compositor-drawn tab pills carry one translated string (the
    // untitled-tab placeholder) and this process is kwin_wayland, which
    // installs no PlasmaZones catalog of its own. Load the daemon's catalog
    // for the current locale from the shared data location; the translator
    // is parented to the effect, so an unload destroys it and ~QTranslator
    // calls removeTranslator itself (the parenting is what triggers that, not
    // what performs it). Same contexts
    // and lookup as the daemon's translation loader, minus the build-tree
    // search dirs that only make sense next to our own binaries.
    {
        auto* translator = new QTranslator(this);
        const QLocale locale;
        const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        for (const QString& dir : dataDirs) {
            if (translator->load(locale, QStringLiteral("plasmazones"), QStringLiteral("_"),
                                 dir + QStringLiteral("/plasmazones/translations"))) {
                QCoreApplication::installTranslator(translator);
                break;
            }
        }
    }

    // Latch compositor shutdown so the destructor can tell a runtime unload
    // (KCM toggle — restore the suppressed stock effects) from session
    // teardown (do NOT call loadEffect into the list KWin is unloading).
    // aboutToQuit fires when the event loop exits, before destructors run.
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        m_compositorShuttingDown = true;
    });

    // Decoration-manager wiring (veto + signal connections) lives with the
    // rest of the border/decoration code in decorations.cpp.
    setupDecorationManager();

    // Seed the decoration profile tree with the empty/neutral default so the
    // pre-fetch state is well-defined; the async `decorationProfileTreeJson`
    // fetch overwrites the whole tree on arrival. Borders are rule-owned and
    // render correctly before the fetch, so no placeholder tree is needed.
    seedDecorationTreeBaseline();

    // Constructor wiring decomposed into concern-scoped init methods (definitions
    // in lifecycle_wiring.cpp). Call order is load-bearing and matches the
    // original inline sequence — in particular initRenderingAndRegistries()
    // connects the screen signals before iterating the current screens().
    initRenderingAndRegistries();
    initTimers();
    connectDragTracker();
    connectWindowAndScreenSignals();
    connectDaemonSubscriptions();
    // Last, and it must stay last: the existing-window sweep inside can reach
    // code that expects the daemon subscriptions above to be wired.
    initExistingWindowsAndInput();
}

void PlasmaZonesEffect::clearDaemonCompositorState()
{
    qCInfo(lcEffect) << "Clearing daemon compositor drag/overlay state before effect shutdown";

    // Drop any in-flight desktop-switch transition and release its
    // active-fullscreen-effect claim. reset() is null-safe (it guards every
    // KWin::effects access), so it is fine to run here even though this can be
    // reached from the destructor before the KWin::effects teardown guards.
    m_desktopTransition.reset();
    // Same for the strip pass (no claim to release, but its capture textures
    // and compiled shaders free under the same context discipline).
    m_stripTransition.reset();
    // And the tab indicators: the override cursor must be handed back before
    // the effect goes (KWin would keep a pointing hand nothing owns). The
    // handler's clear is GL-free — it RETIRES the per-output textures rather
    // than deleting them — and releaseGl() right after is the GL-current
    // point that actually frees them (plus anything retired earlier), so
    // nothing leaks past the effect and nothing is deleted off-context.
    // Made current HERE rather than inherited from the transition resets
    // above, so reordering those cannot turn this into an off-context free.
    m_tilingHandler->clearScrollTabState();
    ensureGlContextCurrent();
    m_scrollTabPainter->releaseGl();

    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowDrag,
                                                QStringLiteral("clearForCompositorReconnect"));
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Overlay,
                                                QStringLiteral("hideOverlay"));
}

void PlasmaZonesEffect::syncStockEffectSuppression()
{
    if (!KWin::effects) {
        return;
    }
    // A pack owns an event only when it would actually RUN: animations
    // enabled, a pack resolved for the event's path, installed, AND applying
    // to the event's contract class. Resolution goes through
    // resolveShaderWithDefault, so a built-in per-event default would count
    // as ownership too — though none of the three current event paths
    // carries one (defaultShaderEffectIdForPath covers only the snap /
    // layout-switch geometry legs and the daemon overlay surfaces).
    // For the peek this mirrors the whole peek path's runnability gates, not
    // just the showingDesktopChanged handler's: the handler itself only
    // checks the animations toggle and a non-empty id, while the
    // isValid/appliesTo pair lives downstream in
    // DesktopTransitionManager::prepareTransitionPrototype. The gate has to
    // be the stricter of the two — a stale or inherited wrong-contract
    // override (or a pack assigned while the animations master toggle is
    // off) must not unload KWin's effects and then bail at the signal,
    // leaving the user with no animation at all.
    //
    // The gate covers ASSIGNMENT and CONTRACT, not COMPILE. Compilation happens
    // on the GL thread at paint time, so a pack whose metadata is valid but
    // whose .frag fails to compile still unloads the builtins here and then
    // draws nothing (compiledShader's null sentinel abandons the leg). That
    // leaves no animation for the event for as long as the pack stays assigned.
    // Accepted rather than plumbed: routing a paint-thread compile result back
    // into this predicate would make the suppression depend on frame timing,
    // and a bundled pack that fails to compile is a build-time bug the shader
    // validator catches, not a runtime state to design around.
    const auto packOwnsEvent = [this](const QString& path) {
        const PhosphorAnimationShaders::ShaderProfile profile =
            PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(), path);
        const QString effectId = profile.effectiveEffectId();
        if (effectId.isEmpty() || !m_windowAnimator->isEnabled()) {
            return false;
        }
        const PhosphorAnimationShaders::AnimationShaderEffect eff = m_shaderManager.shaderRegistry().effect(effectId);
        return eff.isValid() && PhosphorAnimationShaders::shaderEffectAppliesToEventPath(eff, path);
    };

    // Suppression groups — each KWin stock effect is owed unloading exactly
    // while OUR pack owns the event it animates (discussion #816: with both
    // running, two effects animate the same surface concurrently — stutter,
    // ghost trails, or a cancelled stock leg). The minimize/maximize stock
    // effects honor no grab role (unlike the open/close builtins, which the
    // WindowAddedGrabRole / WindowClosedGrabRole path already handles
    // per-window), so unloading is the only suppression that works — the same
    // conclusion the peek reached for windowaperture/eyeonscreen.
    //
    // TREE-resolved assignment only, deliberately: a per-app animation RULE
    // carrying a minimize/maximize shader cannot drive a global unload — it
    // would strip the stock animation from every other window for one app's
    // override. A rule-scoped pack therefore still double-animates its
    // matches; the tree (the global assignment surface) is the opt-in that
    // makes the exchange whole-session coherent.
    QStringList wanted;
    if (packOwnsEvent(PhosphorAnimation::ProfilePaths::DesktopPeek)) {
        wanted << QStringLiteral("windowaperture") << QStringLiteral("eyeonscreen");
    }
    if (packOwnsEvent(PhosphorAnimation::ProfilePaths::WindowMinimize)) {
        // Both stock minimize animations: KWin loads whichever the user
        // picked in the exclusive minimize-animations group, and unloading
        // the one that is not loaded is a recorded no-op.
        wanted << QStringLiteral("magiclamp") << QStringLiteral("squash");
    }
    if (packOwnsEvent(PhosphorAnimation::ProfilePaths::WindowMaximize)) {
        // KNOWN DIVERGENCE on scrolling screens, recorded rather than fixed.
        //
        // This unload is whole-session, but the maximize INTERCEPTION is
        // per-window: a maximize on a scroll-managed tile is claimed by the
        // strip's maximize-column verb, which skips the WindowMaximize morph
        // on purpose (window_connections.cpp, the interceptMaximizeRequest
        // arm) because the column's own batch transition already carries the
        // resize and installing a second one would supersede it.
        //
        // So on a scrolling screen a maximize plays the strip transition,
        // not the assigned pack, while stock maximize is gone session-wide.
        // The pack still plays everywhere else — tiling, snapping, floating
        // windows, and every non-scrolling screen — which is why the unload
        // is still the right trade.
        //
        // Narrowing it would need this predicate to know, per window, whether
        // the scrolling engine will claim that window's maximize. It cannot:
        // the answer depends on the window's screen, desktop and activity at
        // the moment of the request, and the suppression is decided once at
        // sync time for the whole session. Restoring stock here would be
        // worse than the divergence, since stock maximize would then fight
        // the strip batch on exactly the windows this arm exists to protect.
        wanted << QStringLiteral("maximize");
    }

    // Record then unload only what is actually loaded, so the restore path
    // re-loads exactly the user's own configuration and never force-loads an
    // effect they had disabled. Re-asserted every sync: a Desktop Effects KCM
    // apply re-loads scripts from kwinrc behind our back while the predicate
    // still holds, and this loop re-unloads them (the contains() check keeps
    // the record entry unique).
    // Snapshot the minimize pair's loaded state BEFORE the unload loop: the
    // sibling eviction below must judge "was the sibling genuinely loaded
    // this sync?" against pre-unload reality, not against a state this very
    // loop already mutated (processing magiclamp first would otherwise make
    // squash's check see the sibling as unloaded and evict a live record).
    const bool magiclampWasLoaded = KWin::effects->isEffectLoaded(QStringLiteral("magiclamp"));
    const bool squashWasLoaded = KWin::effects->isEffectLoaded(QStringLiteral("squash"));
    for (const QString& name : std::as_const(wanted)) {
        if (KWin::effects->isEffectLoaded(name)) {
            if (!m_suppressedStockEffects.contains(name)) {
                m_suppressedStockEffects.append(name);
            }
            // magiclamp/squash form the Desktop Effects KCM's exclusive
            // minimize group: recording one while its sibling is NOT loaded
            // means any sibling record is stale — the user switched picks
            // while suppressed (KWin's reconcile loaded the new pick behind
            // our back) and the old pick is now disabled in kwinrc. Drop it,
            // or the restore path would force-load BOTH minimize animations.
            // A sibling that IS loaded this sync (a hand-edited kwinrc
            // enabling both) keeps its record: both are genuinely owed
            // restoring, exactly as configured.
            if (name == QLatin1String("magiclamp") && !squashWasLoaded) {
                m_suppressedStockEffects.removeAll(QStringLiteral("squash"));
            } else if (name == QLatin1String("squash") && !magiclampWasLoaded) {
                m_suppressedStockEffects.removeAll(QStringLiteral("magiclamp"));
            }
            KWin::effects->unloadEffect(name);
        }
    }
    // Restore recorded names whose group predicate stopped holding. Keep
    // names whose re-load failed: the sync re-runs from every trigger site,
    // so a transient loader failure gets a free retry instead of permanently
    // dropping the restore obligation for the session. Already-loaded names
    // are satisfied as-is (a KCM reconcile can re-load them behind our back,
    // and loadEffect returns false for a loaded effect — treating that as a
    // failure would pin the name in the retry list forever, warning on every
    // sync for an effect that is in fact running).
    QStringList keep;
    for (const QString& name : std::as_const(m_suppressedStockEffects)) {
        if (wanted.contains(name)) {
            keep.append(name); // still suppressed; restore is owed later
            continue;
        }
        if (KWin::effects->isEffectLoaded(name)) {
            continue;
        }
        if (!KWin::effects->loadEffect(name)) {
            qCWarning(lcEffect) << "failed to restore stock effect" << name << "— will retry on next sync";
            keep.append(name);
        }
    }
    m_suppressedStockEffects = keep;
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    // Give KWin back the stock effects the suppression unloaded (show-desktop
    // scripts for the peek, magiclamp/squash/maximize for window packs) — a
    // runtime unload of THIS effect (KCM toggle) must not leave the user
    // with no minimize/maximize/show-desktop animation. First, while
    // everything is still alive.
    // Skipped during compositor shutdown (the aboutToQuit latch):
    // there the destructor runs from EffectsHandler's own unload-everything
    // sequence, and loadEffect would re-instantiate a script effect into the
    // list KWin is tearing down mid-iteration. Nothing is lost by skipping —
    // the next session loads the effects from kwinrc, which the suppression
    // never writes to.
    if (KWin::effects && !m_compositorShuttingDown) {
        // The latch is not what makes this safe against mid-iteration mutation
        // — the defer below is. What it buys is not queueing a load at all
        // during compositor shutdown: the destructor then runs from
        // EffectsHandler's own unload-everything sequence, and while the
        // stopped event loop means the lambda could never fire anyway, that is
        // a property of the shutdown path rather than a guarantee this code
        // should lean on.
        //
        // Deferred, detached (no `this` context — we are dying): this
        // destructor runs from KWin's unloadEffect, and if that unload
        // originated from a KCM-apply reconcile iterating the loaded-effects
        // list, a synchronous loadEffect here would mutate the container
        // mid-iteration — the same hazard reconfigure() defers around.
        const QStringList toRestore = m_suppressedStockEffects;
        QTimer::singleShot(0, [toRestore]() {
            if (!KWin::effects) {
                return;
            }
            // Restore UNCONDITIONALLY, even when a rapid unload→reload has
            // already constructed a new PlasmaZones instance. Skipping in that
            // case would lose the suppression record for good: the new
            // instance records only what is currently loaded (nothing), so a
            // later pack unassign could never bring the builtins back until a
            // KCM apply or relogin. The cost of restoring is bounded — the new
            // instance re-unloads during its daemon bringup (a few sequential
            // D-Bus round trips plus the registry rescan, sub-second in
            // practice; indefinitely if no daemon is running, in which case no
            // pack resolves either), a window where both animations could
            // race.
            for (const QString& name : toRestore) {
                // Already loaded (a KCM reconcile beat us to it) is satisfied,
                // not a failure — loadEffect returns false for a loaded effect.
                if (!KWin::effects->isEffectLoaded(name) && !KWin::effects->loadEffect(name)) {
                    qCWarning(lcEffect) << "failed to restore stock effect" << name;
                }
            }
        });
        m_suppressedStockEffects.clear();
    }

    // Sever the registry's `effectsChanged` connection BEFORE any shader or
    // cache teardown runs. (The suppressed-effect restore above is deliberately
    // ahead of it: it only reads a QStringList and posts a detached timer, so
    // it can neither re-enter the registry nor touch the caches this guards.)
    // The slot lambda touches `m_shaderManager.m_shaderTransitions`,
    // `m_shaderManager.m_shaderCache`, and `m_shaderManager.m_textureCache` — all
    // declared AFTER `m_animationShaderRegistry` in shadertransitionmanager.h,
    // so they destruct FIRST in C++ reverse-declaration order. The registry
    // destructs LAST, and any signal it (or its underlying file-watcher) emits
    // during its own member teardown would dispatch to the slot AFTER the
    // cache members are gone — UAF. Disconnect now while everything is still
    // alive.
    disconnect(&m_shaderManager.m_animationShaderRegistry, nullptr, this, nullptr);
    disconnect(&m_surfaceShaderRegistry, nullptr, this, nullptr);

    // Make the context current for the WHOLE destructor, member destruction included.
    //
    // The teardown calls below each make it current themselves — but only if they have
    // something to tear down. Unload the effect with no decorated windows and no live
    // transition (a KCM toggle on an idle desktop) and clearAllDecorations() iterates nothing,
    // the drain loop runs zero times, and nothing makes the context current at all. Then the
    // members go: m_compiledPacks, m_shaderCache, m_textureCache — glDeleteProgram,
    // glDeleteTextures, glDeleteFramebuffers, against whatever context happens to be current.
    // Making it current is sticky per-thread, so one call here covers the body and everything
    // the members destroy afterwards.
    ensureGlContextCurrent();

    // Stop the audio-spectrum provider (terminates its cava child process) and
    // sever its signal before teardown, so a late spectrumUpdated can't dispatch
    // to onEffectAudioSpectrum against half-destroyed members.
    if (m_audioProvider) {
        disconnect(m_audioProvider.get(), nullptr, this, nullptr);
        m_audioProvider->stop();
    }
    // Force the audio run gate off BEFORE the posted-event drain below. Teardown
    // (clearAllDecorations → removeWindowDecoration) posts fresh
    // scheduleEffectAudioSync MetaCalls, and the QEvent::MetaCall drain would run
    // syncEffectAudioState while decorations still exist — respawning cava moments
    // before member destruction kills it again. With the toggle forced off,
    // wantRun is false so the drained sync takes the harmless not-wanted branch.
    // Clear the spectrum size too so that branch's `wasLive` is false and it
    // requests no spurious full repaint during shutdown.
    m_enableAudioVisualizer = false;
    m_audioSpectrumSize = 0;

    // Drain the texture loader pool before any other teardown. A
    // worker that's mid-rasterise would otherwise post a queued
    // upload via QMetaObject::invokeMethod against `this` AFTER our
    // members start tearing down — UAF on the texture cache.
    // QThreadPool::waitForDone is called here BEFORE we let the
    // member's destructor run (which itself blocks on waitForDone)
    // so the pending invokeMethod posts are flushed against a still-
    // intact `this`.
    //
    // Distinct from the hot-reload path in the `effectsChanged`
    // lambda above: hot-reload bumps `m_shaderManager.m_textureCacheGeneration` (no
    // wait) so workers in flight can discard their queued upload
    // without blocking the compositor. Shutdown REQUIRES the wait —
    // the queued uploads need to be flushed against a live `this`,
    // not raced against member destruction.
    m_shaderManager.m_textureLoaderPool.waitForDone();
    m_shaderManager.m_textureLoadsInFlight.clear();

    // Clear daemon-side drag/overlay state while the effect is still alive,
    // but only after the destructor's local UAF-prevention guards have run.
    clearDaemonCompositorState();

    // Restore borderless and monocle-maximized windows so they recover properly.
    // Guard against compositor teardown — effects may outlive the stacking order.
    if (KWin::effects) {
        // Tiled tracking cleared first so the rebuild-on-restore handler
        // doesn't churn border items during the restore burst (see the
        // daemon-loss site above); restoreAll() covers every owner kind
        // including rule overrides.
        m_tilingHandler->clearTiledTracking();
        m_snapHandler->clearSnapTracking();
        m_decorationManager->restoreAll();
        // Windowed fullscreen FIRST, per claimReleaseOrder: this is the undo
        // obligation for the effect's own setFullScreen calls (without it an
        // unload strands every windowed-fullscreen client in KWin fullscreen
        // state with nothing left owning the flag), and BOTH maximize restores
        // below skip a window that still holds fullscreen, so it has to land
        // ahead of them.
        m_tilingHandler->restoreAllWindowedFullscreen();
        m_tilingHandler->restoreAllMonocleMaximized();
        // And for the maximize bits mirrored for maximized columns, on the
        // same obligation. AFTER the fullscreen release, deliberately: the
        // column restore skips a window that still holds fullscreen, and on
        // X11 setFullScreen(false) above has already taken effect by the time
        // this runs, so a window holding both states THROUGH OUR OWN windowed
        // fullscreen gets a real restore rather than a skip. A window that
        // went fullscreen on its own request is not in that ledger and is
        // skipped on either platform. On Wayland the committed bit trails a
        // client round-trip, so the skip applies there too — which is why it
        // retains its ledger entry instead of dropping it.
        m_tilingHandler->restoreAllMaximizedToEdges();
        restoreAllRuleWindowLayers();
        clearAllDecorations();
    }

    if (m_keyboardGrabbed && KWin::effects) {
        // Symmetric with the `if (KWin::effects)` guard above: during
        // compositor teardown KWin::effects can be null, and an
        // unguarded deref here would crash even though we reached the
        // destructor body cleanly. The compositor's own teardown
        // releases the grab when KWin::effects is gone.
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }
    m_screenChangeHandler->stop();
    // We no longer reserve/unreserve edges; the daemon disables KWin snap via config.

    // Explicitly tear down every active shader transition before the
    // member-by-member destruction runs. `endShaderTransition` calls
    // `setShader(window, nullptr)` and `unredirect(window)` to release
    // KWin's offscreen state cleanly; relying on default destruction
    // would let the offscreen FBOs linger until KWin's effect-removed
    // bookkeeping ran. Iterates a snapshot because endShaderTransition
    // erases from `m_shaderManager.m_shaderTransitions` mid-loop.
    //
    // Guarded by `if (KWin::effects)` matching the clearAllDecorations /
    // ungrabKeyboard guards above: during compositor teardown the global
    // is null and `endShaderTransition` dereferences it (setShader,
    // unredirect, refWindow). The compositor's own teardown reclaims
    // the offscreen state when KWin::effects is gone.
    if (KWin::effects) {
        QVarLengthArray<KWin::EffectWindow*, 8> activeWindows;
        for (auto& [w, _] : m_shaderManager.shaderTransitions()) {
            activeWindows.push_back(w);
        }
        for (auto* w : activeWindows) {
            endShaderTransition(w);
        }
        // endShaderTransition queues each window's `unrefWindow` via
        // QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) to
        // avoid use-after-free in paintWindow's expiry fall-through. In
        // the destructor path that defer is unsafe in the opposite
        // direction: ~QObject runs after this body returns and discards
        // pending posted MetaCalls targeted at `this`, so the queued
        // unrefs would never fire and KWin's EffectWindow refcount
        // stays incremented for every close-grab transition active at
        // teardown — leaking the close grab. Drain the queue here, while
        // `this` is still fully constructed and the lambdas can safely
        // run. The lambdas only call `KWin::effects->unrefWindow(...)`
        // and don't touch our member state after the unref, so
        // synchronous dispatch from the dtor body is sound.
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
    // When KWin::effects is null (compositor teardown) the drain above is
    // skipped and any still-installed ShaderTransition is destroyed by
    // member destruction instead. Each entry's `visibleRef` dtor then
    // dereferences its stored EffectWindow* (`unrefVisible`) — there is no
    // way to neutralise an EffectWindowVisibleRef without touching the
    // window. This relies on KWin destroying effects before it destroys
    // windows, which is the same lifetime assumption KWin's own Magic
    // Lamp / Squash effects make: both hold visible refs in member
    // containers destroyed at effect destruction with no null-effects
    // special-casing.
}

} // namespace PlasmaZones
