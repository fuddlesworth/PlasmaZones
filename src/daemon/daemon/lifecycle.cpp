// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QScreen>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusError>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <array>

#include <PhosphorServiceIdle/IdleService.h>
#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>
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
#include "daemon/rendering/surfaceshaderitem.h"
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
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>

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
#include "phosphor_i18n.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/overlayadaptor.h"
#include "dbus/zonedetectionadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/autotileadaptor/autotileadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/ruleadaptor.h"

namespace PlasmaZones {

namespace {
// Grace period (ms) for the KWin effect to register as a compositor bridge
// after daemon startup. Comfortably longer than a healthy effect takes to
// register (sub-second once KWin and the daemon's D-Bus name are both up),
// even when the daemon starts before KWin during login — so a timeout means
// a genuine failure, not a race.
constexpr int BRIDGE_WATCHDOG_TIMEOUT_MS = 20000;

// Locate the installed PlasmaZones KWin effect plugin and read the KWin
// version embedded in its plugin interface ID. The KWin effect is a compiled
// C++ plugin; KWin bakes its exact version into the IID it accepts
// (EffectPluginFactory_iid = "org.kde.kwin.EffectPluginFactory" + the KWin
// version string), and silently rejects any plugin built against a different
// KWin. metaData() reads only the static metadata section — no dlopen — so it
// works even on the version-mismatched plugin KWin itself refuses to load.
// `installed` is set to whether the plugin file was found at all. Returns the
// KWin version the effect was built against, or empty when the plugin is
// missing or its IID is not a recognizable KWin effect IID.
QString probeEffectKWinVersion(bool& installed)
{
    static const QLatin1String iidPrefix("org.kde.kwin.EffectPluginFactory");
    const QString effectRelPath = QStringLiteral("kwin/effects/plugins/kwin_effect_plasmazones.so");

    installed = false;
    const QStringList libraryPaths = QCoreApplication::libraryPaths();
    for (const QString& base : libraryPaths) {
        const QString candidate = base + QLatin1Char('/') + effectRelPath;
        if (!QFile::exists(candidate)) {
            continue;
        }
        installed = true;
        const QString iid = QPluginLoader(candidate).metaData().value(QLatin1String("IID")).toString();
        return iid.startsWith(iidPrefix) ? iid.mid(iidPrefix.size()) : QString();
    }
    return QString();
}
} // anonymous namespace

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
    // Note what this does NOT fix: stop() also unregisters the D-Bus object and the service
    // name, and re-registering them lives in init(), not here. A restarted daemon therefore
    // has no bus presence, so nothing it publishes reaches the effect regardless. The
    // re-arm exists so the daemon's own state is consistent after the cycle (which the
    // repairs below also do), not because the cycle fully restores service.
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
    m_shortcutManager->registerShortcuts();
    connectShortcutSignals();
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

    // Migrate window screen assignments from physical to virtual IDs.
    // Must run AFTER finalizeStartup() which loads WTA state — otherwise
    // the migration finds no windows to migrate.
    migrateStartupScreenAssignments();

    // Intentionally last: the algorithmChanged handler (signals.cpp) and showDesktopSwitchOsd
    // (osd.cpp) both gate on !m_running to suppress OSD/feedback during startup. finalizeStartup()
    // calls m_autotileEngine->loadState() which synchronously emits algorithmChanged, and
    // KWin/Plasma can deliver desktop/activity-change signals during the same window. Setting
    // m_running before finalizeStartup() returns would let those handlers fire and double-queue
    // (or leak past) the startup OSD that finalizeStartup() is responsible for.
    m_running = true;
    // NOTE: daemonReady() is emitted by finalizeStartup() — do NOT emit again here.

    // Arm the compositor-bridge registration watchdog. See
    // BRIDGE_WATCHDOG_TIMEOUT_MS for the grace-period rationale. Skip if the
    // effect already registered during init().
    if (m_compositorBridge && !m_compositorBridge->isBridgeRegistered()) {
        m_bridgeWatchdogTimer.start(BRIDGE_WATCHDOG_TIMEOUT_MS);
    }
}

void Daemon::warnCompositorBridgeMissing()
{
    // Stay silent during shutdown. The watchdog may still be armed when the
    // session ends, and a warning/notification raised on the way out is just
    // noise — mirrors the OSD suppression gated on m_running/m_shuttingDown.
    if (m_shuttingDown) {
        return;
    }

    // Re-check: the watchdog is stopped on bridgeRegistered, but a registration
    // landing in the same event-loop turn as the timeout could still reach
    // here. Treat a registered bridge as success and stay silent.
    if (!m_compositorBridge || m_compositorBridge->isBridgeRegistered()) {
        return;
    }

    // Inspect the installed effect plugin (synchronous, cheap). The most common
    // silent failure is a stale effect build whose IID no longer matches the
    // running KWin, so KWin's effect loader rejects it without surfacing an
    // error and the effect never registers.
    bool effectInstalled = false;
    const QString effectKWinVersion = probeEffectKWinVersion(effectInstalled);

    if (!effectInstalled) {
        emitBridgeMissingWarning(
            PhosphorI18n::tr("The PlasmaZones KWin effect plugin is not installed where KWin can find it. "
                             "Reinstall PlasmaZones."));
        return;
    }
    if (effectKWinVersion.isEmpty()) {
        // Plugin present but its IID is not a recognizable KWin effect IID —
        // nothing specific to report, fall back to the generic guidance.
        emitBridgeMissingWarning(QString());
        return;
    }

    // Compare the effect's build-time KWin version against the running KWin.
    // supportInformation() is the only reliable D-Bus source for KWin's
    // version; query it asynchronously so this degraded startup path never
    // blocks the daemon's event loop (mirrors the fire-and-forget notification
    // call in emitBridgeMissingWarning).
    QDBusMessage req =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                       QStringLiteral("org.kde.KWin"), QStringLiteral("supportInformation"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(req, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, effectKWinVersion](QDBusPendingCallWatcher* call) {
                call->deleteLater();

                // The 3s round-trip widens the window in which a late effect
                // registration can land; stay silent if shutdown began or the
                // bridge registered after all.
                if (m_shuttingDown || (m_compositorBridge && m_compositorBridge->isBridgeRegistered())) {
                    return;
                }

                QString diagnosis;
                const QDBusPendingReply<QString> reply = *call;
                if (!reply.isError()) {
                    const QRegularExpressionMatch match =
                        QRegularExpression(QStringLiteral("KWin version:\\s*(\\S+)")).match(reply.value());
                    if (match.hasMatch()) {
                        const QString runningKWinVersion = match.captured(1);
                        if (runningKWinVersion != effectKWinVersion) {
                            diagnosis = PhosphorI18n::tr(
                                            "The PlasmaZones KWin effect was built for KWin %1 but "
                                            "KWin %2 is running, so KWin will not load it. Rebuild and "
                                            "reinstall PlasmaZones against the running KWin.")
                                            .arg(effectKWinVersion, runningKWinVersion);
                        }
                    }
                }
                emitBridgeMissingWarning(diagnosis);
            });
}

void Daemon::emitBridgeMissingWarning(const QString& diagnosis)
{
    if (diagnosis.isEmpty()) {
        qCWarning(lcDaemon) << "Compositor bridge did not register within" << (BRIDGE_WATCHDOG_TIMEOUT_MS / 1000)
                            << "s of startup — the PlasmaZones KWin effect is not running or"
                            << "failed to register. Window dragging, keyboard shortcuts, and"
                            << "snapping will not work. Enable the PlasmaZones effect in System"
                            << "Settings > Desktop Effects, then restart the Plasma session so"
                            << "KWin loads it.";
    } else {
        qCWarning(lcDaemon) << "Compositor bridge did not register within" << (BRIDGE_WATCHDOG_TIMEOUT_MS / 1000)
                            << "s of startup — window control is dead." << diagnosis;
    }

    const QString body = diagnosis.isEmpty()
        ? PhosphorI18n::tr(
              "The PlasmaZones KWin effect has not registered with the daemon, so window "
              "dragging and shortcuts will not work. Make sure it is enabled in System "
              "Settings > Desktop Effects, then restart the Plasma session.")
        : diagnosis;

    // Raise a desktop notification via the freedesktop spec so the user sees
    // the problem without having to read the journal. A direct method call
    // (rather than QDBusInterface) keeps this off the main thread's critical
    // path: QDBusInterface's constructor does a blocking Introspect round-trip,
    // whereas createMethodCall + asyncCall is genuinely fire-and-forget. A
    // missing notification server just makes the async call error out, which
    // is fine. Mirrors the createMethodCall pattern used elsewhere in daemon.
    QDBusMessage notify = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    notify << QStringLiteral("PlasmaZones") // app_name
           << 0u // replaces_id
           << QStringLiteral("plasmazones") // app_icon
           << PhosphorI18n::tr("PlasmaZones: window manager integration inactive") // summary
           << body // body
           << QStringList() // actions
           << QVariantMap() // hints
           << -1; // timeout (server default)
    QDBusConnection::sessionBus().asyncCall(notify);
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

    // Null the drag adaptor's borrowed pointers ABOVE the m_running gate, for
    // the same reason as the provider lambdas and QML statics below: both are
    // wired from init() (init_adaptors.cpp / init_engines.cpp), which runs
    // before start(), so an init-without-start teardown (test fixture,
    // early-fail init, double-stop) would otherwise reach member destruction
    // with the adaptor still holding a pointer to the about-to-die
    // ShortcutManager / AutotileEngine. Both setters are null-safe and
    // idempotent, so running this on an already-stopped daemon costs nothing.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setAutotileEngine(nullptr);
        m_windowDragAdaptor->setShortcutRegistrar(nullptr);
    }

    // Drop the layout-manager provider lambdas FIRST, before the m_running
    // gate. They capture `this` and dereference m_settings; m_settings is
    // declared after m_layoutManager, so reverse-order member destruction
    // tears m_settings down BEFORE m_layoutManager. Any cascade query
    // during ~LayoutRegistry that hits a still-installed lambda would
    // dereference freed memory.
    //
    // The m_running gate skips the rest of stop() on init-without-start
    // paths (test fixtures, early-fail constructors, double-stop). The
    // providers, however, are installed in init() — which runs before
    // m_running is set in start(). Clearing them must therefore not be
    // gated, otherwise the dangling-lambda UAF still reaches the
    // member-destruction window. clearing is null-safe and idempotent
    // (an already-cleared std::function clears to itself).
    if (m_layoutManager) {
        m_layoutManager->setDefaultLayoutIdProvider({});
        m_layoutManager->setDefaultAutotileAlgorithmProvider({});
        m_layoutManager->setTiledWindowCountProvider({});
        m_layoutManager->setScreenOrientationProvider({});
        m_layoutManager->setSnappingPreferredProvider({});
        m_layoutManager->setDefaultAssignmentSuppressedProvider({});
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
    // every m_settings→this connection — the gap-resnap sweep, the adjacent-threshold
    // handler, the snapping/autotile enable-delta, the animation-profile republish, all
    // eleven of them — and most are made in the constructor or init(), which start() does
    // NOT re-run. A stop()→start() cycle (which this daemon supports deliberately, and
    // says so in three places) would come back up with them silently gone.
    teardownIdleConnections();

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
    // by the next start(). Connections whose sender or receiver falls outside
    // the sweep (m_layoutManager is a mixed sender whose init_services.cpp
    // connections must survive; the WTA-to-drag-adaptor fan-out has a
    // non-daemon receiver; one m_settings connection is per-start) are
    // tracked in m_perStartConnections and severed individually below.
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
    for (const QMetaObject::Connection& conn : std::as_const(m_perStartConnections)) {
        disconnect(conn);
    }
    m_perStartConnections.clear();

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
    // with an empty seed partition and an empty low-precedence tag —
    // `resolveWithInheritance` degrades to a single-layer walk and every shell
    // `PhosphorMotionAnimation { profile: … }` resolves against nothing.
    // Leaving them in place is what makes the cycle come back whole.
    //
    // The one partition stop() still sheds is the loader-owned user-JSON
    // partition (tagged `kPlasmaZonesUserProfilesOwnerTag`), and only as a side
    // effect of the loader teardown below: `m_profileLoader` / `m_curveLoader`
    // are reset so their destructors run NOW (issuing their own
    // `clearOwner(ownerTag)` and tearing down the QFileSystemWatchers) rather
    // than in the `~Daemon` body, where they would fire path-change signals
    // into a half-destroyed object. The user-JSON entries are optional
    // overrides on top of the seeds, so losing them across a cycle only drops
    // the user's authored tweaks, not the shell's ability to resolve — and the
    // seeds that remain keep inheritance working. The raw-JSON snapshot is
    // cleared with them, since it mirrors exactly the entries those destructors
    // drop.
    m_rawJsonProfiles.clear();

    // Stop the publish coalescing trampoline before resetting the
    // loaders — the timer is a member QTimer, so its `timeout` slot
    // would otherwise still fire on the next event-loop tick after
    // m_settings (its data source) has been destroyed.
    m_animationPublishTimer.stop();
    m_animationPublishPending = false;

    // Reset the loaders explicitly so the QFileSystemWatcher inside
    // each is torn down NOW, before any other shutdown step has a
    // chance to spin the event loop. Without this, the unique_ptrs
    // would only destruct at the end of the ~Daemon body, leaving a
    // window where stale path-change signals could fire into a
    // half-destroyed object — visible in tests that re-construct the
    // daemon, and theoretically observable in production on a
    // configure-reload cycle. ProfileLoader's destructor issues its
    // own `clearOwner(kPlasmaZonesUserProfilesOwnerTag)` so the
    // per-daemon `m_profileRegistry` value member sheds those entries here.
    m_profileLoader.reset();
    m_curveLoader.reset();
    m_shaderBakePool.clear();
    m_shaderBakePool.waitForDone(500);
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

    // Stop pending timers to prevent callbacks during shutdown
    m_geometryUpdateTimer.stop();
    m_geometryUpdatePending = false;

    // Disconnect scripted algorithm loader to prevent file watcher events during teardown
    if (m_scriptedAlgorithmLoader) {
        m_scriptedAlgorithmLoader->disconnect();
    }

    // Hide overlay
    hideOverlay();

    // Save state
    m_layoutManager->saveLayouts();
    m_layoutManager->saveAssignments();
    m_settings->save();
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->saveStateOnShutdown();
    }

    m_reapplyGeometriesTimer.stop();

    // Autotile per-window restore state is included in WTA's saveStateOnShutdown()
    // above via the unified WindowPlacementStore (refreshOpenWindowPlacements
    // captures every open window's placement). No separate save needed.
    //
    // Do NOT call setAutotileScreens({}) here — it emits windowsReleased
    // which clears WTS floating state and restarts the save timer, potentially
    // overwriting the correct WTS state saved above. The engine is destroyed
    // immediately after, so cleanup is unnecessary.

    // Clear adaptor engine pointers BEFORE destroying the engines.
    // Adaptors are Qt children of the daemon (destroyed later); a D-Bus call
    // arriving between engine destruction and adaptor destruction would otherwise
    // access freed memory. After clearing, ensureEngine() returns false.
    if (m_autotileAdaptor) {
        m_autotileAdaptor->clearEngine();
    }
    if (m_snapAdaptor) {
        m_snapAdaptor->clearEngine();
    }

    // Null the WindowDragAdaptor's engine pointer for the same reason.
    // Clear engine references before destruction
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setEngines(nullptr, nullptr);
    }

    // Clear the late-bound WTS float / mode callbacks that capture `this` (Daemon,
    // via screenModeForWindow) — symmetric with the setShouldTrackPredicate /
    // setShouldRestorePredicate clears, so the "every `this`-capturing predicate is
    // cleared before teardown" contract stays grep-discoverable and survives a
    // future ownership/order refactor.
    if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
        auto* wts = m_windowTrackingAdaptor->service();
        wts->setEngineFloatResolver({});
        wts->setEngineFloatWriter({});
        wts->setEngineFloatLister({});
        wts->setAutotileModePredicate({});
        wts->setAutotileTiledPredicate({});
        // Deliberately NOT cleared here: the snap-state resolver (setSnapStateResolver)
        // and setSnapEngine both capture/store only QPointer(snapEngine), so they
        // self-null when the engine is destroyed — there is no `this`/raw-pointer
        // capture to invalidate, unlike the float callbacks above.
    }

    // Tear down the context-resolver triple before destroying the
    // services the adapters borrow from. Order: borrowers (D-Bus
    // adaptors) drop their non-owning resolver pointer first, then the
    // resolver and its three adapters die, then the underlying router
    // / VirtualDesktopManager / ActivityManager / Settings can safely
    // reset(). Without this, a queued D-Bus method that lands between
    // here and ~Daemon (or a shortcut-manager signal still alive on the
    // main thread) would deref an adapter whose backing service had
    // already been freed by the existing engine-pointer teardown below.
    //
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
        // m_screenModeRouter is destroyed below; null its WTA borrow
        // before that reset so any D-Bus call landing in the gap
        // between this teardown and the bus unregister can't deref
        // a freed router pointer. SnapAdaptor's clearEngine() does
        // the symmetric clear (snapadaptor.cpp).
        m_windowTrackingAdaptor->setScreenModeRouter(nullptr);
    }
    if (m_autotileAdaptor) {
        // Sever the autotile adaptor's post-construction borrow of the WTA (wired
        // in init() so the autotile open path can resolve RouteToScreen /
        // RouteToDesktop rules). The autotile open path no-ops on a null WTA, so
        // a D-Bus open landing in the teardown gap can't drive routing against
        // half-torn-down state. Symmetric with the resolver / router clears above
        // and honours the shutdown-nullptr contract documented in autotileadaptor.h.
        m_autotileAdaptor->setWindowTrackingAdaptor(nullptr);
    }
    m_contextResolver.reset();
    m_settingsGateAdapter.reset();
    m_screenModeAdapter.reset();
    m_workspaceStateAdapter.reset();

    // Destroy the router. Engines below outlive it so any in-flight
    // navigatorForShortcut path completes with the engine pointers it
    // already captured before the router went away.
    m_screenModeRouter.reset();

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
    }

    // Likewise sever WindowTrackingAdaptor's borrow of m_ruleStore (used by
    // its restore-position evaluator) before the store is destroyed. Same
    // grep-discoverable teardown contract as the SnapEngine exclude borrow above.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setRuleStore(nullptr);
    }

    // Clear the autotile context-gap provider, which captures `this` (Daemon, via
    // m_layoutManager / currentDesktopForScreen / currentActivity). No live deref
    // can occur today — m_autotileEngine is destroyed below while `this` is still
    // alive — but clearing it keeps the "every `this`-capturing closure is cleared
    // before teardown" contract complete and grep-discoverable, exactly like the
    // SnapEngine exclude-rule borrow above. `m_autotileEngine` is base-typed
    // `PlacementEngineBase*`; setContextGapProvider lives on the concrete engine.
    if (auto* concreteAutotile = qobject_cast<PhosphorTileEngine::AutotileEngine*>(m_autotileEngine.get())) {
        concreteAutotile->setContextGapProvider({});
    }

    // Destroy engines now (during stop(), before Qt child destruction order).
    m_snapEngine.reset();
    m_autotileEngine.reset();

    // Both engines borrowed m_crossSurfaceResolver (injected at construction).
    // They are destroyed immediately above, so the borrow is already dead;
    // reset the resolver here too so the teardown order is explicit and
    // grep-discoverable — matching the exclude-rule / window-rule borrow
    // severing above — and survives a future member-declaration reorder.
    m_crossSurfaceResolver.reset();

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
    // The other nine raw-Qt-parented adaptors (LayoutAdaptor,
    // OverlayAdaptor, ZoneDetectionAdaptor, WindowTrackingAdaptor,
    // DBusScreenAdaptor, WindowDragAdaptor, CompositorBridgeAdaptor,
    // SnapAdaptor, AutotileAdaptor) all ship destructors that don't
    // deref any borrowed pointer — most are `= default` / empty-body
    // (no member access), and the two outliers do only self-cleanup
    // on a Qt-child member: DBusScreenAdaptor ships an empty out-of-
    // line body, and WindowTrackingAdaptor's `~WindowTrackingAdaptor`
    // calls `m_service->setShouldTrackPredicate({})` on its Qt-child
    // m_service to clear a captured-this lambda before the child
    // tears down (see Pass-3 commit c4e3c5125). The substantive
    // safety claim is "no borrowed-pointer deref runs in any of their
    // destructors" — confirmed by inspecting each header + cpp pair,
    // not header alone. QDBusConnection::unregisterObject (invoked above) blocks new
    // method dispatch to them before we begin tearing down, and Qt's
    // sender-destruction auto-disconnect cleans up signal wiring when the
    // borrowed sender (m_layoutManager, etc.) is destroyed during member
    // destruction. Adding detach() to those nine would require null-guarding
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

    // Provider lambdas already cleared at the top of stop() (before the
    // m_running gate) so this point requires no further teardown.

    m_running = false;
}

void Daemon::queryPlasmaWorkspaceState()
{
    // Query the user-bus systemd for `plasma-workspace.target`'s ActiveState
    // to distinguish a real Plasma session from a phantom plasma-restore session.
    //
    // During user-logout → SDDM handoff, systemd may respawn the daemon into a
    // transient "phantom" state: a stray `kwin_wayland` from a fallback session-
    // restore mechanism briefly publishes a fresh `wayland-N` socket inside the
    // still-dying `user@.service`, and `Restart=on-failure` schedules a daemon
    // retry after Qt's wayland QPA aborts on the vanished `wl_display`. The
    // phantom daemon fires welcome OSDs against an output about to be unbound.
    //
    // Why this signal works: `plasma-workspace.target` is only flipped to `active`
    // by `startplasma-wayland`'s orchestration after SDDM hands off. The phantom
    // has no `startplasma-wayland` leader, so the target stays inactive — a signal
    // the phantom cannot fake. logind's `User.State` stays `active` whenever
    // `user@.service` is up (can't distinguish phantom from real), and the
    // wayland-socket existence probe passes during the phantom.
    //
    // Fail-open on all D-Bus errors: `m_plasmaWorkspaceActive` defaults to `true`,
    // so non-systemd setups and headless tests aren't accidentally silenced.
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (!sessionBus.isConnected()) {
        qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: session bus unavailable, leaving m_plasmaWorkspaceActive=true";
        return;
    }

    QDBusMessage subscribeMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("Subscribe"));
    auto* subscribeWatcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(subscribeMsg), this);
    connect(subscribeWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: Subscribe failed:" << reply.error().message()
                              << "— PropertiesChanged signals may not arrive";
        }
    });

    QDBusMessage getUnitMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("GetUnit"));
    getUnitMsg << QStringLiteral("plasma-workspace.target");
    auto* getUnitWatcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(getUnitMsg), this);
    connect(getUnitWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            qCInfo(lcDaemon) << "queryPlasmaWorkspaceState: GetUnit('plasma-workspace.target') failed:"
                             << reply.error().message() << "— leaving fail-open (target not loaded)";
            return;
        }
        m_plasmaWorkspaceTargetPath = reply.value().path();
        if (m_plasmaWorkspaceTargetPath.isEmpty()) {
            return;
        }
        fetchPlasmaWorkspaceActiveState();
    });
}

void Daemon::fetchPlasmaWorkspaceActiveState()
{
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), m_plasmaWorkspaceTargetPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.systemd1.Unit") << QStringLiteral("ActiveState");
    auto* watcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: ActiveState Get failed:" << reply.error().message();
            return;
        }
        const QString state = reply.value().toString();
        m_plasmaWorkspaceActive = (state == QLatin1String("active"));
        qCInfo(lcDaemon) << "plasma-workspace.target ActiveState at startup:" << state
                         << "plasmaWorkspaceActive=" << m_plasmaWorkspaceActive
                         << "path=" << m_plasmaWorkspaceTargetPath;

        QDBusConnection bus = QDBusConnection::sessionBus();
        const bool ok =
            bus.connect(QStringLiteral("org.freedesktop.systemd1"), m_plasmaWorkspaceTargetPath,
                        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                        SLOT(onPlasmaWorkspaceTargetPropertiesChanged(QString, QVariantMap, QStringList)));
        if (!ok) {
            qCWarning(lcDaemon) << "queryPlasmaWorkspaceState: failed to subscribe to Unit PropertiesChanged on"
                                << m_plasmaWorkspaceTargetPath;
        }
    });
}

void Daemon::onPlasmaWorkspaceTargetPropertiesChanged(const QString& interfaceName,
                                                      const QVariantMap& changedProperties,
                                                      const QStringList& /*invalidatedProperties*/)
{
    if (interfaceName != QLatin1String("org.freedesktop.systemd1.Unit")) {
        return;
    }
    const auto it = changedProperties.constFind(QStringLiteral("ActiveState"));
    if (it == changedProperties.constEnd()) {
        return;
    }
    const QString state = it->toString();
    const bool nowActive = (state == QLatin1String("active"));
    if (m_plasmaWorkspaceActive != nowActive) {
        qCInfo(lcDaemon) << "plasma-workspace.target state changed:" << state;
    }
    m_plasmaWorkspaceActive = nowActive;
}

} // namespace PlasmaZones
