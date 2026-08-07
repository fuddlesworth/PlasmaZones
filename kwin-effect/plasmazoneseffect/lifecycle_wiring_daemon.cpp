// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "input_filter.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>

#include "tilinghandler/tilinghandler.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"

namespace PlasmaZones {

// `lcEffect` is defined in plasmazoneseffect.cpp via Q_LOGGING_CATEGORY. Re-declare
// here so this TU can log under the same category without re-defining storage.
Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// Daemon-facing half of the constructor wiring, split from
// lifecycle_wiring.cpp by concern (that file keeps the rendering/timer/
// drag/window wiring): the D-Bus subscription block and the
// service-(un)registration lifecycle it drives.

void PlasmaZonesEffect::connectDaemonSubscriptions()
{
    // Connect to daemon's settingsChanged D-Bus signal. A failed connect is
    // silent otherwise — check the return so a broken subscription is
    // debuggable instead of looking like a daemon that never emits.
    const bool settingsConnected =
        QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                              PhosphorProtocol::Service::Interface::Settings,
                                              QStringLiteral("settingsChanged"), this, SLOT(slotSettingsChanged()));
    if (settingsConnected) {
        qCInfo(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";
    } else {
        qCWarning(lcEffect) << "Failed to connect to daemon settingsChanged D-Bus signal";
    }

    // Which wl_surface carries each screen's scrolling tab indicators. The
    // paint path slides that surface with the strip, and the object id is the
    // only handle it can match on: every daemon overlay reports the same window
    // class, and a layer surface's scope is not exposed per window.
    const bool tabSurfaceConnected = QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("scrollTabSurfaceChanged"), this,
        SLOT(onScrollTabSurfaceChanged(QString, uint)));
    if (!tabSurfaceConnected) {
        qCWarning(lcEffect) << "Failed to connect to daemon scrollTabSurfaceChanged D-Bus signal"
                            << "— scrolling tab indicators will not ride the strip";
    }

    // Connect to virtual screen changes — daemon emits this when a physical screen's
    // virtual subdivisions are added, removed, or modified.
    const bool vsChangedConnected = QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Screen, QStringLiteral("virtualScreensChanged"), this,
        SLOT(onVirtualScreensChanged(QString)));
    if (vsChangedConnected) {
        qCInfo(lcEffect) << "Connected to daemon virtualScreensChanged D-Bus signal";
    } else {
        qCWarning(lcEffect) << "Failed to connect to daemon virtualScreensChanged D-Bus signal";
    }

    // Connect to per-event motion-profile-tree changes. The daemon emits
    // this (separate from settingsChanged) when a per-event animation
    // duration is edited, so per-event durations apply live instead of
    // only after a logout/login.
    const bool motionTreeConnected = QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Settings, QStringLiteral("motionProfileTreeChanged"), this,
        SLOT(slotMotionProfileTreeChanged()));
    if (motionTreeConnected) {
        qCInfo(lcEffect) << "Connected to daemon motionProfileTreeChanged D-Bus signal";
    } else {
        qCWarning(lcEffect) << "Failed to connect to daemon motionProfileTreeChanged D-Bus signal";
    }

    // Session idle. The daemon owns the detection (ext-idle-notify-v1 is a Wayland
    // CLIENT protocol, and this effect lives inside the compositor that serves it),
    // so the effect only ever sees the resolved boolean and pauses / resumes the
    // decoration chain on it.
    const bool idleConnected = QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Settings, QStringLiteral("sessionIdleChanged"), this,
        SLOT(slotSessionIdleChanged(bool)));
    if (idleConnected) {
        qCInfo(lcEffect) << "Connected to daemon sessionIdleChanged D-Bus signal";
    } else {
        qCWarning(lcEffect) << "Failed to connect to daemon sessionIdleChanged D-Bus signal";
    }

    // Connect to keyboard navigation D-Bus signals
    connectNavigationSignals();

    // Connect to autotile D-Bus signals
    m_tilingHandler->connectSignals();
    m_tilingHandler->loadSettings();

    // Verify daemon availability asynchronously to avoid blocking the compositor.
    // CRITICAL: Do NOT use synchronous isServiceRegistered() here. The daemon
    // registers its D-Bus service name in init() BEFORE start() runs heavy
    // initialization and BEFORE the event loop begins
    // (src/daemon/main.cpp: init() → start() → app.exec()).
    // During that window, isServiceRegistered() returns true but the daemon
    // can't process messages. Any synchronous QDBusInterface creation would
    // trigger Introspect, blocking KWin for up to the D-Bus timeout (~25s).
    //
    // Instead, send an async Introspect — if the daemon responds, it's fully
    // operational and we trigger slotDaemonReady(). If it can't respond (still
    // initializing), the call times out harmlessly and we wait for the
    // daemonReady D-Bus signal instead.
    {
        QDBusMessage introspect = QDBusMessage::createMethodCall(
            PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
            QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
        auto* watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(introspect, PhosphorProtocol::Service::DaemonReadyProbeTimeoutMs),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (reply.isValid() && !m_daemonGate.serviceRegistered) {
                // Daemon responded — it's fully operational.
                // Trigger the same ready flow as the daemonReady signal.
                slotDaemonReady();
            }
        });
    }

    // Connect to daemon's daemonReady signal — emitted at the end of Daemon::start()
    // after all initialization is complete and the daemon can process D-Bus messages.
    // This is the safe point to set m_daemonGate.serviceRegistered and create QDBusInterfaces.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::LayoutRegistry,
                                          QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));

    // Watch for daemon D-Bus service registration and unregistration.
    // After a daemon restart, m_lastCursorOutput is still valid in the effect
    // but the daemon's lastCursorScreenName/lastActiveScreenName are empty.
    // Without this, keyboard shortcuts (rotate, etc.) operate on all screens
    // because resolveShortcutScreen returns nullptr.
    //
    // On Wayland, this watcher uses D-Bus monitoring (not X11 selection),
    // which works reliably across both sessions.
    auto* serviceWatcher = new QDBusServiceWatcher(
        PhosphorProtocol::Service::Name, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon service unregistered";
        m_daemonGate.serviceRegistered = false;
        // Release the idle latch. m_sessionIdle is daemon-pushed state whose ONLY
        // route back to false is a sessionIdleChanged(false) broadcast — and a
        // restarted daemon arms a fresh ext-idle-notify-v1 notification on a seat
        // that is already active, which never produces an idle->active edge and so
        // never sends one. Left set, every decorated window's chain would stay
        // frozen for the rest of the session. Repaint them so a chain paused under
        // the latch is put back in the paint loop (a paused chain emits no damage
        // of its own).
        if (m_sessionIdle) {
            m_sessionIdle = false;
            repaintAllDecorations();
        }
        // Drop the virtual-screen readiness immediately. The defs from the
        // previous daemon cycle are now stale; without clearing the flag here,
        // the windowFrameGeometryChanged VS-crossing detector would keep
        // resolving against stale virtual-screen boundaries during the gap
        // between unregistration and the next daemon's fetch. continueDaemonReady
        // setup re-clears and refetches on bringup; this closes the gap before it.
        m_daemonGate.virtualScreensReady = false;
        // The stale floating-window set is dropped further down in this same
        // handler (clearAllFloatingState beside clearAllZoneState, paired with
        // the rule-cache invalidation) — no separate clear here.
        // Also clear the bridge-registration in-flight gate. Without
        // this, a daemon-restart racing the in-flight registerBridge
        // reply leaves the gate set: the new daemon's `daemonReady`
        // signal arrives, slotDaemonReady sees the gate true and
        // bails, and the gate only clears later when the stale call's
        // error reply arrives — by which time no further signal will
        // re-trigger slotDaemonReady. The effect would sit idle
        // indefinitely. Resetting here keeps the gate authoritative
        // across daemon restarts.
        m_daemonGate.bridgeRegistrationInFlight = false;
        // Retire the in-flight call along with the gate. Its reply is still
        // coming, and without this bump it would land after the NEW daemon's
        // registration has re-armed the gate and clear it out from under it.
        ++m_daemonGate.bridgeRegistrationGeneration;
        m_daemonGate.readyRestoresDone = false;
        m_daemonGate.readyWindowStateProcessed = false;
        m_snapHandler->clearRestoreCache();
        // Reset the rules-subscription gate so the next daemon's
        // `rulesChanged` broadcasts can be re-subscribed. Without this,
        // the daemonReady disconnect+reconnect dance below would re-wire
        // daemonReady against the new bus name but the rulesChanged
        // subscription guard would still latch and skip the re-subscribe
        // — silently dropping rule edits across daemon restarts.
        //
        // Disconnect the previous rulesChanged match rule BEFORE flipping
        // the gate. Qt does not deduplicate match rules (same pitfall the
        // daemonReady serviceRegistered handler addresses); without this
        // disconnect, every daemon restart accumulates one extra match
        // rule, and each rulesChanged emission then dispatches N times
        // to slotRulesChanged across N restarts. The debounce
        // collapses the work to a single fetch, but each dispatch still
        // pays D-Bus delivery + Qt slot invocation.
        QDBusConnection::sessionBus().disconnect(QString(PhosphorProtocol::Service::Name),
                                                 QString(PhosphorProtocol::Service::ObjectPath),
                                                 QString(PhosphorProtocol::Service::Interface::Rules),
                                                 QStringLiteral("rulesChanged"), this, SLOT(slotRulesChanged()));
        m_daemonGate.rulesSubscribed = false;
        // Release any pending first-frame open suppression. Without the
        // daemon there is no `resolveWindowRestore` reply coming and no
        // autotile reposition either, so the suppression entry would just
        // hold the window invisible until its 250ms deadline. Releasing
        // each entry through endRestoreSuppression also schedules the
        // per-window repaint so the windows become visible immediately
        // rather than at the next natural compositor cycle.
        const auto suppressedWindows = m_restoreSuppress.keys();
        for (KWin::EffectWindow* sw : suppressedWindows) {
            endRestoreSuppression(sw);
        }

        // Restore borderless and monocle-maximized windows — daemon state is
        // gone. Clear the handlers' tiled tracking FIRST: restoreAll() emits
        // windowDecorationRestored per window, and the rebuild-on-restore
        // handler would otherwise recreate a border item for every still-
        // tracked window only for clearAllDecorations() to destroy it moments
        // later. With tracking cleared, resolveSurfacePathFor resolves
        // mode-tracked windows to window.floating during the restore burst and
        // the handler drops their items. Windows matched by a still-live SetBorder rule
        // (the rule sets deliberately survive daemon loss, see below) can
        // still get an item recreated and immediately torn down by
        // clearAllDecorations() — bounded, invisible churn that is cheaper than
        // suppressing the handler across the burst.
        m_tilingHandler->clearTiledTracking();
        // The scrolling set is a dead session's Mode discriminator: keeping
        // it would stamp Mode "scrolling" into rule verdicts resolved
        // during the daemon-down interval (invalidateAllRuleCaches below
        // bakes the stale set in otherwise). The TEARDOWN variant, not the
        // live chokepoint: the live setter's scheduled border sweep would
        // re-create rule-matched decorations right after
        // clearAllDecorations below.
        m_tilingHandler->clearScrollingScreensForTeardown();
        m_snapHandler->clearSnapTracking();
        // Drop the zone / floating caches that feed the IsSnapped / Zone /
        // IsFloating rule-match fields. Unlike the exclusion / animation rule
        // sets (deliberately preserved below), these caches mirror per-window
        // PLACEMENT state owned by the now-dead daemon session. Keeping them
        // would let a `WHEN IsSnapped` / `Zone(...)` / `IsFloating` rule match
        // against stale state during the bringup race until the async
        // syncZonesFromDaemon / getFloatingWindows re-seed lands. Both are
        // authoritatively repopulated on daemon-ready.
        m_navigationHandler->clearAllZoneState();
        m_navigationHandler->clearAllFloatingState();
        // The placement caches above feed placement-scoped rule match inputs. A
        // SetOpacity rule keyed on IsSnapped/IsFloating/Zone caches its verdict
        // per (windowId, ruleSet revision) — neither moves here — so drop the
        // whole match cache; any decoration built after this resolves against
        // the cleared placement. The folded opacity itself reverts with the
        // decorations (clearAllDecorations below tears down the opacity-tint
        // layer along with the border), so no repaint or re-fold is needed here.
        // Also carries the window-layer sweep (see invalidateAllRuleCaches): a
        // `WHEN IsFloating` layer rule releases its keep-above here (snapshot
        // restore) instead of stranding it for the daemon-down interval.
        invalidateAllRuleCaches();
        m_decorationManager->restoreAll();
        m_tilingHandler->restoreAllMonocleMaximized();
        clearAllDecorations();
        // Deliberately do NOT clear `m_snappingExclusionRuleSet`,
        // `m_decorationExclusionRuleSet`, `m_animationExclusionRuleSet`, or
        // the shader manager's animation
        // rule set. Across a daemon restart the user's last-known rule set
        // remains authoritative — clearing here would briefly drop every
        // exclusion / animation override during the bringup race, flashing
        // un-filtered animations and unstyled snaps until the new daemon
        // replays its rulesChanged broadcast. The sets get refreshed once
        // the new daemon's `loadRuleAnimationsFromDbus` reply lands.
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon registered: waiting for daemonReady signal";

        // DO NOT set m_daemonGate.serviceRegistered = true here.
        // The daemon registers its D-Bus service name in init(), BEFORE start()
        // runs heavy initialization and BEFORE the event loop begins. Keep the
        // flag false until the daemon's own daemonReady signal fires (end of
        // Daemon::start()), confirming it can handle D-Bus requests.

        // Defensive reconnect of daemonReady. Subscriptions against a
        // WELL-KNOWN name survive daemon restarts (the bus re-resolves the
        // owner per match rule — daemon_bringup.cpp's connectNavigationSignals
        // note is the authoritative statement, and settingsChanged plus all
        // sixteen navigation signals rely on it without any re-wire), so
        // this refresh is belt-and-braces, not a requirement. Keep the
        // disconnect-first pairing (Qt doesn't deduplicate match rules) and
        // do NOT propagate the pattern to other signals.
        QDBusConnection::sessionBus().disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                                 PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                 QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
        QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                              PhosphorProtocol::Service::Interface::LayoutRegistry,
                                              QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
    });

    // NOTE: daemon state sync (floating windows, cached settings) is NOT done
    // here. m_daemonGate.serviceRegistered is false at this point (set only by
    // slotDaemonReady), so any ensureInterface() call would bail out immediately.
    // All daemon state sync is deferred to slotDaemonReady().

    // Connect to existing windows. Skip close-grabbed dying windows — wiring
    // per-window connections and seeding screen tracking for a window whose
    // close already happened would resurrect state nothing cleans up.
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted()) {
            continue;
        }
        setupWindowConnections(w);
    }

    // The daemon disables KWin's Quick Tile via kwriteconfig6. We don't reserve electric borders
    // here because that would turn on the edge effect visually; the daemon's config approach
    // is the right way to prevent Quick Tile from activating.

    // Seed m_lastCursorOutput with the compositor's active screen. This ensures
    // the daemon has a valid cursor screen even if no mouse movement occurs after login.
    // slotMouseChanged will overwrite this as soon as the cursor moves.
    //
    // The actual D-Bus push to the daemon happens in slotDaemonReady(), which fires
    // either from the async Introspect callback above (daemon already running) or
    // from the daemonReady D-Bus signal (daemon starts later). We do NOT push here
    // to avoid synchronous QDBusInterface creation on the compositor thread.
    auto* initialScreen = KWin::effects->activeScreen();
    if (initialScreen) {
        m_lastCursorOutput = initialScreen->name();
    }

    // Overhang input filter: keeps clicks off strip straddlers' clipped-away
    // overhangs (see input_filter.h). This wiring runs once per effect, so the
    // assignment stands on its own; the unique_ptr uninstalls the filter on
    // effect teardown.
    m_overhangInputFilter = std::make_unique<ScrollOverhangInputFilter>(this);

    qCInfo(lcEffect) << "initialized: C++ effect with D-Bus support and mouseChanged connection";
}

} // namespace PlasmaZones
