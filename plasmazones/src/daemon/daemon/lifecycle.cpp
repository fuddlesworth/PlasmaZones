// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The Daemon start/stop lifecycle. stop() is one long, ORDERED teardown whose
// clear-before-reset contracts reference each other in sequence, so it stays
// whole here rather than split by subsystem. The bridge watchdog's timeout arm
// lives in bridge_watchdog.cpp and the plasma-workspace.target probe in
// plasma_workspace.cpp.

#include "daemon/daemon.h"
#include "helpers.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QScreen>
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusError>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <array>

#include <PhosphorServiceIdle/IdleService.h>
#include <PhosphorShaders/ShaderPresetStore.h>
#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/QtQuickClockManager.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/enginefactory.h"
#include "daemon/controllers/contextresolverwiring.h"
#include "daemon/rendering/zoneentryscaffold.h"
#include "daemon/rendering/zoneshadernoderhi.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSource.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorScreens/DBusScreenAdaptor.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorRules/RuleStoreWatcher.h>

#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/settingsconfigstore.h"
#include "config/settings.h"
#include "core/types/baselinecleanup.h"
#include "core/types/constants.h"
#include "core/resolve/crosssurfaceresolver.h"
#include "core/resolve/animationbootstrap.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/geometryutils.h"
#include "core/utils/utils.h"
#include "core/platform/logging.h"
#include "core/interfaces/shaderregistry.h"
#include "common/screenidresolver.h"
#include "common/layoutbundlebuilder.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/overlayadaptor.h"
#include "dbus/zonedetectionadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/autotileadaptor/autotileadaptor.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/ruleadaptor.h"

namespace PlasmaZones {

void Daemon::start()
{
    if (m_running) {
        return;
    }

    // Reset the shutdown latch — stop() sets it true and nothing else clears
    // it, so a stop()→start() cycle (tests, programmatic restart) would
    // permanently silence every shutdown-guarded code path
    // (warnCompositorBridgeMissing, late-arrival reply guards, OSD
    // suppression in daemon/osd.cpp) on the second run. m_aboutToQuitConnected
    // already contemplates this cycle to avoid stacking the aboutToQuit
    // handler; this is the matching reset on the value side.
    m_shuttingDown = false;

    // Re-arm the idle service. stop() tears it down (its Wayland notification object and
    // every connection around it), and setupIdleService is otherwise only reached from
    // init(), which start() does not re-run — so an in-process restart came back up with
    // the idle state permanently stale. Guarded on !m_idleService so the ordinary
    // init()-then-start() path does not build it twice.
    //
    // Note what this does NOT fix: stop() also unregisters the D-Bus object and the
    // service name, and re-registering them lives in init(), not here, so a restarted
    // daemon has no bus presence and nothing it publishes reaches the effect regardless.
    // The same asymmetry covers the autotile shortcuts: their grabs survive stop() while
    // their handler connections died with the engine, and initializeAutotile() re-runs
    // from start() below but wires handlers only when m_autotileEngine exists, which
    // after a no-init stop() it does not. With no bus presence nothing they trigger
    // reaches anyone, so wiring them here would repair a limb of a cycle that is
    // degraded by design. The re-arm exists so the daemon's own state is consistent
    // after the cycle, not because the cycle restores service.
    if (!m_idleService) {
        setupIdleService();
    }

    // Re-publish the QML static defaults. stop() nulls all three
    // (`PhosphorCurve::setDefaultRegistry(nullptr)` etc.) to prevent
    // borrowed-pointer UAF during teardown; without this re-publish,
    // a stop()→start() cycle (tests, programmatic restart) leaves QML
    // resolving against nullptr defaults — every
    // `PhosphorMotionAnimation { profile: … }` and every clock-driven
    // animated-value lookup silently fails until the next ctor runs.
    // The setters are idempotent: storing the same pointer the ctor
    // installed is a no-op on the first start() of a fresh daemon.
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(&m_curveRegistry);
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&m_profileRegistry);
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(m_clockManager.get());

    // Suppress OSDs once Qt begins shutdown (SIGTERM, programmatic quit).
    // Connected once — m_aboutToQuitConnected prevents stacking on stop()→start().
    if (qGuiApp && !m_aboutToQuitConnected) {
        connect(qGuiApp, &QGuiApplication::aboutToQuit, this, [this]() {
            m_shuttingDown = true;
        });
        m_aboutToQuitConnected = true;
    }

    // Detect phantom plasma-restore sessions via systemd's user bus.
    // See queryPlasmaWorkspaceState() for the full rationale.
    queryPlasmaWorkspaceState();

    connectScreenSignals();
    connectDesktopActivity();

    // Register global shortcuts via ShortcutManager.
    // setDefaultShortcut stores defaults synchronously (fast, no key grabbing),
    // then key grabs are activated via async D-Bus calls so the event loop
    // stays responsive for Wayland protocol events during login.
    // Unguarded deref: m_shortcutManager is ctor-owned and never reset
    // (stop()'s guard is defensive teardown symmetry, not a live invariant).
    m_shortcutManager->registerShortcuts();
    connectShortcutSignals();
    connectScrollingShortcuts();
    initializeAutotile();
    initializeUnifiedController();
    connectLayoutSignals();
    connectOverlaySignals();

    // Initial layout resolution: set the active layout from per-desktop assignments.
    // Must run after connectLayoutSignals() (which sets up autotile screens and filter)
    // and after connectDesktopActivity() (which sets current desktop/activity).
    // PhosphorWorkspaces::VirtualDesktopManager and PhosphorWorkspaces::ActivityManager no longer resolve layouts —
    // this is the single code path that understands autotile vs snapping mode.
    syncModeFromAssignments();

    finalizeStartup();

    // Migrate window screen assignments from physical to virtual IDs, over the
    // placements the adaptor's constructor loaded. Nothing reloads the store
    // after this, so the migration is not discarded (F479).
    migrateStartupScreenAssignments();

    // Intentionally last: the algorithmChanged handler (signals.cpp) and showDesktopSwitchOsd
    // (osd.cpp) both gate on !m_running to suppress OSD/feedback during startup, while layouts
    // and algorithms are being assigned, and KWin/Plasma can deliver
    // desktop/activity-change signals during the same window. Setting m_running before
    // finalizeStartup() returns would let those handlers fire and double-queue (or leak past)
    // the startup OSD that finalizeStartup() is responsible for.
    m_running = true;
    // NOTE: daemonReady() is emitted by finalizeStartup() — do NOT emit again here.

    // Arm the compositor-bridge registration watchdog. See
    // BRIDGE_WATCHDOG_TIMEOUT_MS for the grace-period rationale. Skip if the
    // effect already registered during init().
    if (m_compositorBridge && !m_compositorBridge->isBridgeRegistered()) {
        m_bridgeWatchdogTimer.start(BRIDGE_WATCHDOG_TIMEOUT_MS);
    }
}

void Daemon::stop()
{
    m_shuttingDown = true;

    // Cancel any pending debounced gap-resnap so it can't fire mid-teardown
    // (the engine is cleared below; a late fire would be a wasted no-op).
    m_gapResnapTimer.stop();

    // The bridge watchdog is double-guarded (m_shuttingDown + registered
    // re-check) so a late fire is harmless, but every other piece of this
    // teardown severs explicitly rather than relying on an invariant.
    m_bridgeWatchdogTimer.stop();

    // The preview-notify debounce and the resnap-suppression watchdog are
    // both restartable from paths that outlive the teardown of what they
    // touch (a queued preview push, a resnap feedback in flight), so they
    // are severed here with the other timers rather than left to fire into
    // an unregistered adaptor.
    m_previewNotifyTimer.stop();

    // Both wired in init (init_adaptors.cpp), so init-origin teardown that
    // belongs on this side of the m_running gate: the geometry-reapply
    // debounce, and the algorithm loader whose file watcher must not fire
    // into the teardown.
    m_reapplyGeometriesTimer.stop();
    if (m_scriptedAlgorithmLoader) {
        m_scriptedAlgorithmLoader->disconnect();
    }

    // The per-screen scrolling-OSD settle timers accumulate one per screen
    // id ever seen; sweep them all here so a stop() leaves no pending
    // strip-preview fire and no per-screen residue.
    reapScrollingOsdSettleTimers();

    // The systemd PropertiesChanged subscription is a bus-level slot-table
    // entry keyed on `this`, not a QObject connection, so nothing in the
    // per-sender sweep below reaches it. Left in place it survives the stop
    // as a stale entry, and queryPlasmaWorkspaceState's own disconnect-first
    // on the next start() only covers it while the resolved unit path is
    // unchanged. Drop it here, with the path it was registered against.
    if (!m_plasmaWorkspaceTargetPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.systemd1"), m_plasmaWorkspaceTargetPath,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
            SLOT(onPlasmaWorkspaceTargetPropertiesChanged(QString, QVariantMap, QStringList)));
        m_plasmaWorkspaceTargetPath.clear();
    }

    // Null the drag adaptor's borrowed pointers ABOVE the m_running gate, for the same
    // reason as the provider lambdas and QML statics below: both are wired from init()
    // (init_adaptors.cpp / init_engines.cpp), which runs before start(), so an
    // init-without-start teardown (test fixture, early-fail init, double-stop) would
    // otherwise reach member destruction with the adaptor still holding a pointer to the
    // about-to-die ShortcutManager / AutotileEngine. Both setters are null-safe and
    // idempotent, so running this on an already-stopped daemon costs nothing.
    if (m_windowDragAdaptor) {
        // Cancel a live preview BEFORE dropping the engine borrows. The
        // adaptor reaches the engines only through those two pointers, so
        // nulling them first strands any preview: the engine keeps the window
        // detached with its restoration state held, and nothing left can
        // commit or cancel it. A stop() mid-drag is reachable from a settings
        // reload or a shutdown that races a drag.
        m_windowDragAdaptor->cancelDragInsertPreviews();
        m_windowDragAdaptor->setAutotileEngine(nullptr);
        m_windowDragAdaptor->setScrollEngine(nullptr);
        m_windowDragAdaptor->setShortcutRegistrar(nullptr);
    }

    // Drop the layout-manager provider lambdas FIRST, before the m_running
    // gate. They capture `this` and dereference m_settings, which is declared
    // after m_layoutManager and so is destroyed BEFORE it; a cascade query
    // during ~LayoutRegistry that hit a still-installed lambda would
    // dereference freed memory. The providers are installed in init(), which
    // runs before m_running is set in start(), so clearing them must not be
    // gated or the init-without-start paths (test fixtures, early-fail
    // constructors, double-stop) keep the UAF. Clearing is idempotent.
    if (m_layoutManager) {
        m_layoutManager->setDefaultLayoutIdProvider({});
        m_layoutManager->setDefaultAutotileAlgorithmProvider({});
        m_layoutManager->setTiledWindowCountProvider({});
        m_layoutManager->setScreenOrientationProvider({});
        m_layoutManager->setColorSchemeProvider({});
        m_layoutManager->setCurrentVirtualDesktopProvider({});
        m_layoutManager->setSnappingPreferredProvider({});
        m_layoutManager->setDefaultAssignmentSuppressedProvider({});
        m_layoutManager->setDefaultScrollingTemplateProvider({});
        m_layoutManager->setScrollingTemplateStore(nullptr);
    }

    // Null the QML static registry / manager pointers BEFORE the m_running
    // gate. These three statics are published unconditionally from
    // `setupAnimationProfiles()` in the ctor — which runs before `init()`
    // or `start()`. A Daemon constructed but never started (test fixtures,
    // early-fail init paths) still has them pinned to the about-to-die
    // members, so the clear must run on every teardown path, not just the
    // post-start one. Same "borrowed-pointer + late member destruction"
    // window as the provider lambdas above. The setDefault*(nullptr)
    // calls are unconditionally null-safe.
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(nullptr);
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(nullptr);

    // Animation-loader teardown, ALSO above the m_running gate for the same ctor-origin reason as
    // the statics it pairs with: setupAnimationProfiles runs from the ctor, so an
    // init-without-start teardown reaches the member destructors with the curve loader (and its
    // QFileSystemWatcher) still live — exactly the construct-without-start fixture the reset
    // comment below names. NOTE the deliberate asymmetry this creates for a stop() → start()
    // cycle: nothing rebuilds the loader, nor the m_ruleStoreWatcher reset beside it below (both
    // are ctor-only), so live reload of `plasmazones/curves` and of rules.json does not survive
    // the cycle — the curve seeds and the low-precedence tag DO survive so inheritance keeps
    // resolving, rules.json is still re-read by an explicit load() (D-Bus reloadRules), and a
    // restarted daemon has no bus presence anyway (see the partition-shedding rationale above).
    m_rawJsonProfiles.clear();

    // Stop the publish coalescing trampoline before resetting the loaders — the timer is a member
    // QTimer, so its `timeout` slot would otherwise still fire on the next event-loop tick after
    // m_settings (its data source) has been destroyed.
    m_animationPublishTimer.stop();
    m_animationPublishPending = false;

    // Reset the curve loader and the rules.json watcher explicitly so the
    // QFileSystemWatcher inside each is torn down NOW, before any shutdown step
    // can spin the event loop. Otherwise they destruct at the end of the ~Daemon
    // body, leaving a window where a stale path-change signal fires into a
    // half-destroyed object (or reloads m_ruleStore while its rulesChanged
    // subscribers are being detached). Seen in daemon tests.
    m_curveLoader.reset();
    m_ruleStoreWatcher.reset();

    // Idle wiring, ALSO before the m_running gate, for the same reason as the two
    // blocks above: setupIdleService() runs from init(), which precedes start(), so
    // an init-without-start teardown (test fixtures, early-fail init, double-stop)
    // reaches the member destructors with the idle service still live. ~Daemon calls
    // stop(), so resetting the service HERE (unconditionally, above the gate) means the
    // member destructor never has to. Every call below is null-safe and idempotent, so
    // running it on the already-stopped path costs nothing.
    //
    // The debounce timer goes with it: a pending fire would land in refreshIdleStages
    // after teardown. It early-returns on a null m_idleService, so this is belt and
    // braces — but every other piece of this wiring is severed explicitly rather than
    // left to an invariant, and this is the last piece.
    m_idleStagesRefreshTimer.stop();
    // Sever the idled/resumed lambdas BEFORE the reset. They are the two connections
    // idle.cpp makes with m_idleService as SENDER (not in m_idleConnections, which holds
    // only the settings/bridge/timer connections keyed on other senders), so
    // teardownIdleConnections below does not touch them — and it runs after this reset
    // anyway. Destroying the service walks ~IdleStateMachine, which can emit resumed()
    // while IdleService's own QObject connections are still live (QObject severs them only
    // in ~QObject, after member destruction), firing the daemon lambda mid-teardown. That
    // publishes sessionIdleNow() — a spurious sessionIdleChanged(false) D-Bus broadcast on
    // shutdown when the seat was idle. Disconnecting first makes the teardown emit nothing.
    if (m_idleService) {
        m_idleService->disconnect(this);
    }
    m_idleService.reset();
    // The next run starts from a fresh effect that assumes an active session. Leaving this
    // true would make the re-armed service's first publish look redundant and swallow it.
    m_publishedSessionIdle = false;
    // And the arm-retry budget, for the same reason its two neighbours here are reset: the
    // race it covers is a STARTUP race, so a restarted daemon needs its full budget. Spent
    // in run 1, it would otherwise send run 2 straight to the give-up branch on the very
    // first attempt.
    m_idleArmRetriesLeft = kIdleArmRetries;
    // The idle service's own signals die with it, but the connections idle.cpp made whose
    // sender OUTLIVES it are still live: the two settings signals, the debounce timer (a
    // value member), and the bridgeRegistered push when a compositor bridge exists (that
    // one is conditional, so it is three or four). A settings write between stop() and
    // ~Daemon would still run their lambdas. Sever exactly those.
    //
    // NOT a blanket disconnect(m_settings.get(), nullptr, this, nullptr). That severs
    // EVERY m_settings→this connection: the eight-signal gap-resnap sweep, the
    // batch settingsChanged handler, the adjacent-threshold handler, the
    // animation-profile republish, the system colour-scheme re-resolve, the two
    // idle-timeout handlers this function is actually here for, and the
    // snapping/autotile/scrolling enable deltas. No count is quoted on purpose —
    // the list grows, and a stale number reads as an audit of a set nobody
    // re-counted. Most of them are made in the constructor or init(), which
    // start() does NOT re-run. A stop()→start() cycle (which this daemon supports
    // deliberately, and says so in three places) would come back up with them
    // silently gone.
    teardownIdleConnections();

    // Unregister D-Bus object path and service to prevent late calls during shutdown
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QString(PhosphorProtocol::Service::ObjectPath));
    bus.unregisterService(QString(PhosphorProtocol::Service::Name));

    // Sever the remaining raw-pointer adaptors from the unique_ptr members
    // they borrow. ~QObject destroys these adaptors AFTER all unique_ptr
    // members have already run their destructors, so without detach the
    // adaptors would see dangling pointers during the destruction window —
    // and the SettingsAdaptor dtor's save-on-teardown would deref a freed
    // Settings object. Each adaptor's detach() is null-safe + idempotent.
    //
    // WHY ONLY THESE FOUR: SettingsAdaptor has the confirmed dtor-UAF
    // (debounced save timer flush). ShaderAdaptor + ControlAdaptor have
    // non-trivial signal wiring + cached state that benefits from
    // explicit teardown for the same "queued D-Bus call lands during
    // destruction window" defense-in-depth. RuleAdaptor borrows
    // m_ruleStore (a unique_ptr) and m_settings; without detach
    // its slot bodies could deref freed memory during the window after
    // ~Daemon's body returns — that is when the unique_ptr members
    // (including m_ruleStore) run their destructors, and the
    // raw-Qt-parented RuleAdaptor only runs its own destructor
    // *after* that, as part of QObject child cleanup.
    //
    // The other eleven raw-Qt-parented adaptors (LayoutAdaptor,
    // OverlayAdaptor, ZoneDetectionAdaptor, WindowTrackingAdaptor,
    // DBusScreenAdaptor, WindowDragAdaptor, CompositorBridgeAdaptor,
    // SnapAdaptor, TilingAdaptor, AutotileAdaptor, ScrollingAdaptor) all
    // ship destructors that don't
    // deref any borrowed pointer — they are `= default` or empty-body
    // (no member access); DBusScreenAdaptor ships an empty out-of-line
    // body. The substantive
    // safety claim is "no borrowed-pointer deref runs in any of their
    // destructors" — confirmed by inspecting each header + cpp pair,
    // not header alone. QDBusConnection::unregisterObject (invoked above) blocks new
    // method dispatch to them before we begin tearing down, and Qt's
    // sender-destruction auto-disconnect cleans up signal wiring when the
    // borrowed sender (m_layoutManager, etc.) is destroyed during member
    // destruction. Adding detach() to those eleven would require null-guarding
    // every slot body (they currently rely on the "borrowed pointer is
    // always valid" invariant), which is a larger refactor than the
    // defense-in-depth buys. If a future adaptor grows a dtor body that
    // derefs a borrowed member, add detach() to it AND wire the call here
    // — same pattern as these four.
    if (m_settingsAdaptor) {
        m_settingsAdaptor->detach();
    }
    if (m_shaderAdaptor) {
        m_shaderAdaptor->detach();
    }
    if (m_controlAdaptor) {
        m_controlAdaptor->detach();
    }
    if (m_ruleAdaptor) {
        m_ruleAdaptor->detach();
    }

    // Shader registries + preset store + warm-bake pool: torn down ABOVE the
    // !m_running gate because they are ctor/init-origin (setupAnimationShaderEffects
    // / setupSurfaceShaderEffects / setupShaderPresets / setupShaderWarmBakes all
    // run from init(), before start() sets m_running). An init-without-start
    // teardown (a failed init, or a double-stop) must still null the
    // OverlayService's borrows and run these destructors, or ~OverlayService is left
    // holding three dangling pointers — two registries and the preset registry —
    // the same reverse-destruction hazard the adaptor detaches above guard against.
    m_shaderBakePool.clear();
    m_shaderBakePool.waitForDone(500);
    // Reap the warm-bake QFutureWatchers with their host: a bake discarded by
    // the clear() above never fires finished, so its watcher (whose finished
    // handler is also its deleteLater) would otherwise survive every
    // stop() → init() cycle. setupShaderWarmBakes creates a fresh host.
    m_bakeWatcherHost.reset();
    if (m_overlayService) {
        m_overlayService->setAnimationShaderRegistry(nullptr);
    }
    m_animationShaderRegistry.reset();
    // Reset the surface registry here too so its QFileSystemWatcher and the
    // effectsChanged → warm-bake connection (captured by value into the init()
    // lambda, targeting `this`) are torn down before the event loop can spin
    // during shutdown. Null the overlay service's borrow FIRST (Stage d wired
    // the OSD decoration consumer), mirroring the animation registry above.
    if (m_overlayService) {
        m_overlayService->setSurfaceShaderRegistry(nullptr);
    }
    m_surfaceShaderRegistry.reset();
    // Same order for the preset store, so no queued presetsChanged reaches a
    // dangling registry while the event loop spins during shutdown.
    if (m_overlayService) {
        m_overlayService->setPresetRegistry(nullptr);
    }
    // Both connections to the ctor-owned m_shaderRegistry, severed before the store
    // goes. That registry is not reset here, keeps its watcher running, and stop() runs
    // with the event loop alive, so a later shadersChanged would reach the preset sync
    // (publishing into the store reset below) and the zone warm-bake (parenting a
    // watcher to an already-reset host).
    disconnect(m_overlayPresetSyncConnection);
    m_overlayPresetSyncConnection = {};
    // Belt and braces: these two die with the registries reset above, but the handles
    // must be cleared or setupShaderPresets' own sever reads a stale one next init.
    disconnect(m_animationPresetSyncConnection);
    m_animationPresetSyncConnection = {};
    disconnect(m_surfacePresetSyncConnection);
    m_surfacePresetSyncConnection = {};
    disconnect(m_zoneWarmBakeConnection);
    m_zoneWarmBakeConnection = {};
    m_presetStore.reset();
    // Clear the warm-bake dedup so a stop() -> init() cycle re-warms every pack (the
    // registries are rebuilt, so a remembered fingerprint would wrongly suppress the
    // fresh bake) and the hash does not grow unbounded across cycles.
    m_scheduledBakeFingerprints.clear();

    // initEnginesAndWiring lends the resolver to OverlayService before
    // start() marks the daemon running. Detach it above the running gate so a
    // failed init cannot leave ~OverlayService pumping deferred deletes with a
    // resolver that reverse member destruction has already freed.
    if (m_overlayService) {
        m_overlayService->setContextResolver(nullptr);
    }

    // The borrow-severing blocks below are ALSO init-origin (initEnginesAndWiring wires
    // them, and init() can complete without start() ever running), so they must sit
    // ABOVE the running gate: on an init-without-start teardown (failed init, ~Daemon
    // after init()) the captured `this` / raw-member closures would otherwise stay
    // installed while member destruction frees what they deref. Each is a null-safe
    // idempotent clear, so running them on a never-inited daemon is a no-op.

    // The shutdown save runs FIRST, while every borrow below is still wired:
    // its re-capture reads the engines, predicates and context resolver, and
    // with those severed it captured nothing. Running-path only; its guard
    // blocks the later saves this teardown schedules, and nothing below
    // mutates placement.
    if (m_running && m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->saveStateOnShutdown();
    }

    // Clear adaptor engine pointers BEFORE destroying the engines. Adaptors are Qt
    // children of the daemon (destroyed later); a D-Bus call arriving between engine
    // destruction and adaptor destruction would otherwise access freed memory. After
    // clearing, ensureEngine() returns false.
    if (m_tilingAdaptor) {
        m_tilingAdaptor->clearEngine();
    }
    if (m_autotileAdaptor) {
        m_autotileAdaptor->clearEngine();
    }
    if (m_scrollingAdaptor) {
        m_scrollingAdaptor->clearEngine();
    }
    if (m_snapAdaptor) {
        m_snapAdaptor->clearEngine();
    }

    // Null the WindowTrackingAdaptor's engine borrows for the same reason.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setEngines(nullptr, nullptr, nullptr);
    }

    // Clear the late-bound WTS float / mode callbacks that capture `this`, so
    // the "every `this`-capturing predicate is cleared" contract stays grep-discoverable.
    if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
        auto* wts = m_windowTrackingAdaptor->service();
        wts->setEngineFloatResolver({});
        wts->setEngineFloatWriter({});
        wts->setEngineFloatLister({});
        wts->setAutotileModePredicate({});
        wts->setEngineTiledPredicate({});
        wts->setModeEngineIdResolver({});
        // Deliberately NOT cleared here: setSnapStateResolver and setSnapEngine
        // store only QPointer(snapEngine), which self-nulls on destruction.
    }
    // NOTE: the strip-state provider is deliberately NOT cleared here either:
    // it captures only a QPointer, and clearing it makes saveState SKIP the
    // strips write. It is cleared just before m_scrollEngine.reset().
    // The `this`-capturing closures on the overlay service. Each is safe today
    // (every lambda re-resolves its dependency off the Daemon and null-checks
    // it), but leaving any of them installed breaks the grep-discoverable
    // clear-before-teardown contract the whole block exists for — so a new
    // provider joins this list rather than relying on its own null check.
    if (m_overlayService) {
        m_overlayService->setScrollZonesProvider({});
        // Same contract: the layouts-provided resolver captures `this` and
        // reads the router, which is reset before the engines below.
        m_overlayService->setLayoutSupportResolver({});
        // Same contract and the same reason: it captures `this` and reads the
        // router, which is reset before the engines below.
        m_overlayService->setAutotileActiveResolver({});
        m_overlayService->setDragInsertSelectorResolver({});
        m_overlayService->setStripCardsProvider({});
        m_overlayService->setStripAxisProvider({});
        // Symmetric clear of the late-bound drag-exclusion id: a stop() that
        // lands mid-drag never reaches the adaptor's resetDragState, and the
        // overlay service outlives this teardown.
        m_overlayService->setActiveDragWindowId({});
    }
    // Same clear-before-teardown contract as the overlay block above: the
    // picker's axis provider captures `this` and reads m_scrollEngine, which
    // is reset below.
    if (m_unifiedLayoutController) {
        m_unifiedLayoutController->setStripAxisProvider({});
    }

    // Drop the D-Bus borrowers' non-owning resolver / router / WTA pointers.
    // Explicit symmetric clear across all three borrowers — SnapAdaptor's
    // resolver is also nulled defensively by clearEngine() above, but doing
    // it here too keeps the teardown contract grep-discoverable and survives
    // a future refactor of clearEngine() that might stop touching the
    // resolver pointer.
    if (m_snapAdaptor) {
        m_snapAdaptor->setContextResolver(nullptr);
    }
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setContextResolver(nullptr);
    }
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setContextResolver(nullptr);
        // m_screenModeRouter is destroyed on the running path below; null its
        // WTA borrow before that reset so any D-Bus call landing in the gap
        // between this teardown and the bus unregister can't deref
        // a freed router pointer. SnapAdaptor's clearEngine() does
        // the symmetric clear (snapadaptor.cpp).
        m_windowTrackingAdaptor->setScreenModeRouter(nullptr);
    }
    if (m_tilingAdaptor) {
        // Sever the tiling adaptor's post-construction borrow of the WTA (wired
        // in init() so the tiling open path can resolve RouteToScreen /
        // RouteToDesktop rules). The tiling open path no-ops on a null WTA, so
        // a D-Bus open landing in the teardown gap can't drive routing against
        // half-torn-down state. Symmetric with the resolver / router clears above
        // and honours the shutdown-nullptr contract documented in tilingadaptor.h.
        m_tilingAdaptor->setWindowTrackingAdaptor(nullptr);
        // Same contract for the registry subscription: a metadata push landing
        // in the teardown gap must not walk engines that are being reset.
        m_tilingAdaptor->setWindowRegistry(nullptr);
    }

    // Sever SnapEngine's borrow of m_excludeRuleSet (a daemon-owned value
    // member) BEFORE m_snapEngine.reset(). Declaration order currently
    // guarantees lifetime, but a future reorder or ownership move could
    // silently introduce a dangling pointer through isAppIdExcluded if
    // a late shutdown call landed; the explicit clear here makes the
    // teardown contract grep-discoverable and survives that refactor.
    // `m_snapEngine` is base-typed `PlacementEngineBase*`; the setter
    // lives on the concrete `SnapEngine`. qobject_cast mirrors the
    // concreteAutotile narrowing a few lines below.
    if (auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get())) {
        concreteSnap->setExcludeRuleSet(nullptr);
        // The live-mode resolver captures `this` and consults the router,
        // which is destroyed BEFORE the engines (declaration order) — the
        // closure null-checks the router, but clearing it here keeps the
        // teardown grep-discoverable like every other late-bound borrow.
        concreteSnap->setLiveModeResolver({});
        // The window-registry borrow belongs here too. Member order means the
        // registry outlives the engines, so nothing can deref it in the
        // teardown gap; this is the grep-discoverable contract, and it matches
        // the clear the tiling adaptor's identically-named borrow gets above.
        concreteSnap->setWindowRegistry(nullptr);
        // The navigation-state provider and cross-surface resolver: raw borrows too.
        concreteSnap->setNavigationStateProvider(nullptr);
        concreteSnap->setCrossSurfaceResolver(nullptr);
        // QPointer-only delegate; cleared for symmetry with the scroll engine.
        concreteSnap->setPersistenceDelegate({}, {});
    }

    // Likewise sever WindowTrackingAdaptor's borrow of m_ruleStore (used by
    // its restore-position evaluator) before the store is destroyed. Same
    // grep-discoverable teardown contract as the SnapEngine exclude borrow above.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setRuleStore(nullptr);
        // The zone-detection borrow belongs in this block too, and was the one
        // late-bound pointer missing from it. Nothing can deref it in the
        // teardown gap today (its single use is at wiring time, never on a
        // D-Bus-reachable path), so this is the grep-discoverable contract
        // rather than a live fix.
        m_windowTrackingAdaptor->setZoneDetectionAdaptor(nullptr);
        // And the registry borrow (the setter drops its recorded connections).
        m_windowTrackingAdaptor->setWindowRegistry(nullptr);
    }

    // Clear the autotile context-gap provider, which captures `this` (Daemon, via
    // m_layoutManager / currentDesktopForScreen / currentActivity). No live deref
    // can occur today — m_autotileEngine is destroyed on the running path below
    // while `this` is still alive — but clearing it keeps the "every
    // `this`-capturing closure is cleared before teardown" contract complete and
    // grep-discoverable, exactly like the SnapEngine exclude-rule borrow above.
    // `m_autotileEngine` is base-typed `PlacementEngineBase*`;
    // setContextGapProvider lives on the concrete engine.
    if (auto* concreteAutotile = qobject_cast<PhosphorTileEngine::AutotileEngine*>(m_autotileEngine.get())) {
        concreteAutotile->setContextGapProvider({});
        // The cross-surface resolver borrow all three engines took (enginefactory.cpp).
        concreteAutotile->setCrossSurfaceResolver(nullptr);
        concreteAutotile->setPersistenceDelegate({}, {});
    }
    // Scroll twin of the clear above: every closure below captures Daemon `this`
    // (init_engines.cpp) under the same clear-before-destroy contract.
    if (auto* concreteScroll = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get())) {
        concreteScroll->setContextGapProvider({});
        concreteScroll->setScrollingModeResolver({});
        concreteScroll->setCrossSurfaceResolver(nullptr);
        concreteScroll->setPersistenceDelegate({}, {});
    }

    // Everything ABOVE this gate is init/ctor-origin teardown that must run on
    // an init-without-start path (a failed init, a double-stop): the adaptor
    // detaches, the D-Bus unregister, the loader resets, the QML-static
    // null-outs, the shader-registry teardown, and the borrow-severing clears
    // just above — all of which either sever a BORROWED pointer a member
    // destructor would otherwise deref, or are idempotent no-ops. Everything
    // BELOW is start()-origin (the persistent sender reconnects, the state
    // save, and the engine/resolver member RESETS established during a running
    // session); running those without a prior start() could touch half-wired
    // state. Hence the gate sits here.
    if (!m_running) {
        return;
    }

    // start() reconnects these persistent senders on every run
    // (connectScreenSignals / connectDesktopActivity / connectShortcutSignals /
    // connectOverlaySignals / initializeAutotile); sever them here or a
    // stop()→start() cycle stacks duplicate lambda connections and every
    // shortcut / screen / desktop / overlay signal dispatches its handler
    // twice on the second run. Scoped per-sender, NOT a blanket
    // settings-style disconnect, for the reason documented above
    // teardownIdleConnections(): every connection these senders hold on
    // `this` is made in per-start code, so severing them is exactly undone
    // by the next start(). This sweep covers the connectScreenSignals /
    // connectDesktopActivity / connectShortcutSignals connections in
    // start.cpp; the connectLayoutSignals / connectOverlaySignals connections
    // (whose sender m_layoutManager is mixed and must not be blanket-severed)
    // are instead tracked in m_restartScopedConnections and cleared at the
    // top of connectLayoutSignals(), and the WTA-to-drag-adaptor fan-out uses
    // Qt::UniqueConnection. The two schemes are complementary, not
    // alternatives — this one owns the persistent-sender sweep, that one owns
    // the mixed-sender and non-daemon-receiver connections.
    if (m_shortcutManager) {
        m_shortcutManager->disconnect(this);
    }
    if (m_screenManager) {
        m_screenManager->disconnect(this);
    }
    if (m_virtualDesktopManager) {
        m_virtualDesktopManager->disconnect(this);
    }
    if (m_activityManager) {
        m_activityManager->disconnect(this);
    }
    if (m_overlayService) {
        m_overlayService->disconnect(this);
    }
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->disconnect(this);
    }
    // The restart-scoped handles too. connectLayoutSignals() drops them at its
    // own top on the NEXT start(), which is enough to prevent stacking, but it
    // leaves them live for the whole stopped interval — and their senders
    // (m_layoutManager, m_scrollingTemplateStore) are ctor-owned and keep
    // emitting, so a store mutation between stop() and the next start() would
    // run updateEngineScreens on a stopped daemon. Severing here makes the
    // teardown symmetric with every other per-start list; the clear in
    // connectLayoutSignals stays, because init() can re-run without a stop().
    for (const QMetaObject::Connection& conn : std::as_const(m_restartScopedConnections)) {
        disconnect(conn);
    }
    m_restartScopedConnections.clear();

    // Per-session change-gate state: a stale tiled-count entry would make the
    // first placementChanged of the next init/start cycle read as "unchanged"
    // and silently skip its save trigger.
    m_lastTiledCountByScreen.clear();
    // Sibling latch, same per-session shape (its queued single-shot also
    // gates on m_shuttingDown, so this is symmetry rather than a live fix).
    m_reconcileAssignmentsPending = false;
    // Its colour-scheme twin, same shape and same reasoning.
    m_colorSchemeRefreshPending = false;
    // Per-session restore staging: entries computed against the pre-stop
    // window set must not feed a post-restart KCM apply with dead geometry.
    m_pendingSnapFloatRestores.clear();
    // The derived engine sets are only read while the recompute latch is held,
    // and the next cycle's first recompute rewrites them before any read — but
    // they are per-session change-gate state like the two above, and leaving
    // them out was an asymmetry in a block whose whole purpose is that reset.
    m_derivedAutotileScreens.clear();
    m_derivedScrollingScreens.clear();
    // And the latch pair that GUARDS those two derived sets. Resetting the
    // state a latch protects while leaving the latch itself set is the half
    // that bites: a stop() taken with the recompute in progress (a nested
    // signal reaching here mid-pass) would come back with the daemon believing
    // a recompute is still running, and every updateEngineScreens of the next
    // session would set the queued flag and return without ever recomputing.
    m_updateEngineScreensInProgress = false;
    m_updateEngineScreensQueued = false;
    // Same shape again: the assignment snapshot is replaced wholesale by the
    // next diffActiveAssignments, but the announce map beside it is advanced
    // only when a card is shown, so a template announced before the stop would
    // otherwise still count as "already seen" a session later.
    m_activeAssignmentByScreen.clear();
    m_lastAnnouncedTemplateByScreen.clear();
    // The per-screen scroll caps the scrolling pass resolves. Same per-session
    // shape as the derived sets above: rewritten by the next pass before any
    // read, but this hash is what decides whether a placementChanged arriving
    // between the stop and that pass walks the strip at all, so a dead
    // session's caps must not be what answers that.
    m_scrollFfmMaxScrollPercent.clear();
    // The mode-transition focus seed. Pass-scoped by design — the next
    // updateEngineScreens clears it at the top of its capture phase before any
    // read — so this is symmetry rather than a live fix, in a block whose whole
    // purpose is that symmetry. It matters more now that one of its three write
    // arms is fed from adaptor state rather than from a departing engine: that
    // arm's source outlives the stop, so leaving a seed behind here would carry
    // a dead session's focus into the next one's first flip.
    m_transitionFocusSeed.clear();
    // m_lastEngineOrders is the one per-context cache this block deliberately
    // LEAVES ALONE. It is not change-gate state: it holds the window order
    // captured when a context left a tiling engine, and a stop() does not close
    // the windows, so the order it records is still the right seed when the
    // same context re-enters after the restart. Clearing it would make every
    // mode re-entry in the new session fall back to compositor announce order.
    // Its own prunes keep it bounded: a closed window drops out of every order
    // (pruneEngineOrdersForWindow), and a removed desktop or activity drops its
    // contexts (pruneContextMapsForDesktop / pruneContextMapsForActivities).
    // Screen ids are pruned on a virtual-screen RECONFIGURE
    // (pruneEngineOrdersForRemovedScreens, which keeps the new VS id set plus
    // the bare physical id), and an output that goes away loses its orders in
    // retireOutputPlacements: its windows are parked by the engines instead,
    // and a replayed order would tile windows that have moved on.

    // Per-session OSD gate. The screen-removal cooldown deadline can still be
    // in the future, and carried into the next start() it swallows the first
    // OSD of the new session.
    m_screensSettlingUntil = {};

    // Release the shortcut grabs and the Portal session with the connections:
    // registerShortcuts() on the next start() lazily recreates the registry
    // and backend, and starting from an empty entry table avoids the second
    // run's re-registration warning.
    if (m_shortcutManager) {
        m_shortcutManager->unregisterShortcuts();
    }

    // stop() deliberately does NOT unregister the settings-driven entries
    // (`Global`, …), shed the shell-family-seed partition, or clear the
    // low-precedence owner tag. `m_profileRegistry` is a value member, so it
    // dies with the Daemon and no later Daemon can inherit its contents —
    // there is no shared registry to leave polluted. The only way another
    // consumer ever reached it was the QML static default pointer, and that is
    // nulled above, before this gate.
    //
    // Shedding them here would be actively wrong. All three are re-established
    // only from `setupAnimationProfiles()`, which runs from the CONSTRUCTOR:
    // neither `init()` nor `start()` calls it. A stop()→start() cycle on the
    // same instance is supported (see the re-publish of the three QML statics
    // and the idle re-arm in `start()`), and shedding them would bring it back
    // with an empty seed store: pass 1 of `resolveWithInheritance` would find
    // nothing and every shell `PhosphorMotionAnimation { profile: … }` would
    // resolve against library defaults. Leaving them in place is what makes the
    // cycle come back whole. The config-backed timing partition survives for
    // the same reason and by the same route — `installMotionProfileTree` is
    // also reached only from the ctor-origin setup.
    //
    // `m_curveLoader` is reset ABOVE the m_running gate so its destructor runs
    // NOW (issuing its own `clearOwner(ownerTag)` and tearing down the
    // QFileSystemWatcher) rather than in the `~Daemon` body, where it would
    // fire path-change signals into a half-destroyed object. What it owns is
    // CURVES, not profiles, so its teardown costs the cycle live curve reload
    // and nothing else — and it is the only sender of `curvesChanged`, so after
    // a cycle nothing re-parses the timing tree against a reloaded registry
    // either. The raw-JSON snapshot is cleared alongside because it caches
    // curve-resolved profiles, not because any destructor drops matching
    // entries. (The clears and resets themselves run ABOVE the m_running gate,
    // hoisted next to the QML-static null-outs they pair with — the loaders
    // are ctor-origin, so an init-without-start teardown needs them too.)

    // Stop pending timers to prevent callbacks during shutdown
    m_geometryUpdateTimer.stop();
    m_geometryUpdatePending = false;
    m_geometryDeferralClock.invalidate();

    // Hide the zone overlay AND the three Escape-consuming modal slots. The
    // shortcut grabs those modals dismiss on are released just above
    // (unregisterShortcuts), so a slot left showing has no way out; on a
    // stop() → start() cycle the next session comes up with a stale picker
    // or cheatsheet floating over it.
    hideOverlay();
    if (m_overlayService) {
        m_overlayService->hideLayoutPicker();
        m_overlayService->hideSnapAssist();
        m_overlayService->hideCheatsheet();
        // The zone selector has the same no-way-out problem: OverlayService
        // outlives stop(), the drag adaptor's engine borrows are already
        // nulled and the bus unregisters below, so no drag-end can ever hide
        // a selector left showing — and the latched m_zoneSelectorVisible
        // would make showZoneSelector's entry guard refuse every show for
        // the whole next session.
        m_overlayService->hideZoneSelector();
        // hideZoneSelector deliberately keeps the zone triple for the
        // drag-end snap path; no drag-end follows a stop(), so clear both
        // selection families explicitly.
        m_overlayService->clearSelectedZone();
        // The scroll drop-indicator overrides have the same no-way-out problem
        // as the modals above: the departing-screen loop that ordinarily
        // clears them keys on the engine's active set, which is empty from
        // here on, so stale paint would survive a stop()/start() cycle.
        m_overlayService->clearAllScrollDropIndicatorOverrides();
    }

    // Save state. The window-tracking state was saved at the top of this
    // function, before the engine borrows were severed.
    m_layoutManager->saveLayouts();
    m_layoutManager->saveAssignments();
    m_settings->save();

    // Autotile per-window restore state is included in WTA's saveStateOnShutdown()
    // (run at the top of stop(), with the engines still wired). No separate save.
    //
    // Do NOT call setAutotileScreens({}) here — it emits windowsReleased
    // which clears WTS floating state and restarts the save timer, potentially
    // overwriting the correct WTS state saved above. The engine is destroyed
    // immediately after, so cleanup is unnecessary.

    // The engine borrows, WTS callbacks, and resolver/router/WTA borrower
    // pointers were all severed ABOVE the running gate (they are init-origin);
    // from here on only the member RESETS remain. Order: the resolver and its
    // three adapters die first, then the underlying router /
    // VirtualDesktopManager / ActivityManager / Settings can safely reset().
    m_contextResolver.reset();
    m_settingsGateAdapter.reset();
    m_screenModeAdapter.reset();
    m_workspaceStateAdapter.reset();

    // Destroy the router. Engines below outlive it so any in-flight
    // navigatorForShortcut path completes with the engine pointers it
    // already captured before the router went away.
    m_screenModeRouter.reset();

    // Destroy engines now (during stop(), before Qt child destruction order).
    // Their exclude-rule / rule-store / context-gap borrows were severed
    // above the running gate.
    m_snapEngine.reset();
    m_autotileEngine.reset();
    // Drop the strip-state provider FIRST, and only now: once the engine is
    // gone the closure answers an EMPTY blob, and saveload turns an empty
    // blob into deleteKey(scrollStrips), so any later save would WIPE the
    // user's persisted strip structure. Clearing it makes saveState skip the
    // strips write instead, which is why it cannot happen any earlier — the
    // shutdown save above still needs a live engine to answer.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setScrollStripStateProvider({});
    }
    m_scrollEngine.reset();

    // All three engines borrowed m_crossSurfaceResolver (injected at construction).
    // They are destroyed immediately above, so the borrow is already dead;
    // reset the resolver here too so the teardown order is explicit and
    // grep-discoverable — matching the exclude-rule / window-rule borrow
    // severing above — and survives a future member-declaration reorder.
    m_crossSurfaceResolver.reset();

    // Provider lambdas already cleared at the top of stop() (before the
    // m_running gate) so this point requires no further teardown.

    m_running = false;
}

} // namespace PlasmaZones
