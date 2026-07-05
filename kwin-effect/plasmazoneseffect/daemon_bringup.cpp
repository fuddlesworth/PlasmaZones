// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "../autotilehandler.h"
#include "../navigationhandler.h"
#include "../screenchangehandler.h"
#include "../snapassisthandler.h"
#include "../snaphandler.h"
#include "../windowanimator.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>

#include <effect/effecthandler.h>
#include <core/output.h>
#include <virtualdesktops.h>
#include <window.h>
#include <workspace.h>

#include <QColor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <memory>
#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Duplicated from daemon's configkeys.h — effect cannot include daemon headers
constexpr QLatin1String TriggerModifierField("modifier");
constexpr QLatin1String TriggerMouseButtonField("mouseButton");
} // namespace

void PlasmaZonesEffect::slotDaemonReady()
{
    if (m_daemonServiceRegistered) {
        return; // Already ready — idempotent guard
    }
    if (m_bridgeRegistrationInFlight) {
        // A registerBridge async call is already pending. The Introspect-
        // probe path (effect ctor, lifecycle.cpp) and the daemonReady D-Bus
        // signal can both fire slotDaemonReady before the FIRST registerBridge reply
        // sets m_daemonServiceRegistered. Without this gate, a daemon
        // racing its own readiness signal against an Introspect probe
        // would receive TWO registerBridge calls in flight, then both
        // replies would call continueDaemonReadySetup() — duplicate state
        // re-push. Idempotent? Mostly. Worth the fragility? No.
        return;
    }
    m_bridgeRegistrationInFlight = true;

    qCInfo(lcEffect) << "daemon ready: registering bridge before re-pushing state";

    // Register the compositor bridge with the daemon, passing our protocol
    // version so the daemon can reject us if we're too old. The daemon returns
    // its own API version and a session ID; "REJECTED" means version mismatch.
    //
    // All post-registration work (state re-push, virtual screen fetch, etc.) is
    // deferred into the reply callback so that on REJECTED / protocol mismatch
    // we never send a single stateful call to an incompatible daemon. Any such
    // call would either fail noisily or risk silent marshalling mismatches —
    // the very failure mode this PR is designed to prevent.
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(
            PhosphorProtocol::Service::Interface::CompositorBridge, QStringLiteral("registerBridge"),
            {QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
             QStringList{QStringLiteral("borderless"), QStringLiteral("animation")}}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        // Clear the in-flight flag on EVERY return path (success, error,
        // rejection, version mismatch) so a subsequent slotDaemonReady
        // can retry. m_daemonServiceRegistered remains the long-lived
        // success gate; m_bridgeRegistrationInFlight only covers the
        // narrow window between the call leaving and its reply arriving.
        m_bridgeRegistrationInFlight = false;
        QDBusPendingReply<PhosphorProtocol::BridgeRegistrationResult> reply = *w;
        if (reply.isError()) {
            qCWarning(lcEffect) << "registerBridge call failed:" << reply.error().message()
                                << "— effect remains idle until the daemon signals ready again.";
            return;
        }
        PhosphorProtocol::BridgeRegistrationResult result = reply.value();
        if (const QString err = result.validationError(); !err.isEmpty()) {
            qCWarning(lcEffect) << "registerBridge reply rejected:" << err
                                << "— effect remains idle until the daemon signals ready again.";
            return;
        }
        if (result.sessionId == QLatin1String("REJECTED")) {
            // REJECTED covers any invalid registration (the daemon also
            // rejects an empty compositorName); this caller always sends a
            // non-empty name, so for it the only reachable cause is a
            // protocol-version mismatch — diagnose that.
            qCCritical(lcEffect) << "Daemon REJECTED this effect's registration: daemon apiVersion="
                                 << result.apiVersion << "but this effect speaks"
                                 << PhosphorProtocol::Service::ApiVersion
                                 << "— a version mismatch; update the effect to match the daemon.";
            return;
        }
        int daemonVersion = result.apiVersion.toInt();
        if (daemonVersion < PhosphorProtocol::Service::MinPeerApiVersion) {
            qCCritical(lcEffect) << "Daemon apiVersion" << daemonVersion << "is below this effect's minimum"
                                 << PhosphorProtocol::Service::MinPeerApiVersion
                                 << "— update the daemon to match the effect.";
            return;
        }
        qCInfo(lcEffect) << "Bridge registered: daemon apiVersion=" << result.apiVersion
                         << "session=" << result.sessionId;
        m_daemonServiceRegistered = true;
        continueDaemonReadySetup();
    });
}

void PlasmaZonesEffect::continueDaemonReadySetup()
{
    // All D-Bus calls use QDBusMessage::createMethodCall + asyncCall (no QDBusInterface)
    // to avoid synchronous D-Bus introspection that blocks the compositor thread.

    // Re-push metadata for every live window. KWin's class/desktop/caption
    // change signals fired during session restore are swallowed by
    // pushWindowMetadata's m_daemonServiceRegistered gate, so the daemon's
    // WindowRegistry is empty when the bridge first comes up. Walk the
    // stacking order once and re-emit setWindowMetadata so any consumer
    // querying the registry from a subsequent windowOpened / settings-load
    // handler sees a populated record. Safe to send before virtual screens
    // / pending restores arrive — setWindowMetadata is registry-only and has
    // no dependency on screen identity.
    for (KWin::EffectWindow* w : KWin::effects->stackingOrder()) {
        // Skip close-grabbed dying windows: pushing their metadata would
        // resurrect registry records for windows whose windowClosed the
        // fresh daemon will never see.
        if (!w || w->isDeleted()) {
            continue;
        }
        pushWindowMetadata(w);
    }

    // Drop the snap-assist capture's "we recently posted this handle" set —
    // the daemon's bounded LRU is empty after a fresh registration (whether
    // first-start or restart), so any handle the kwin-effect would otherwise
    // skip on assumption-of-residence must be re-captured. Without this
    // reset, windows the user snap-assists toward shortly after a daemon
    // restart could silently fall back to icons until the set is rebuilt.
    if (m_snapAssistHandler) {
        m_snapAssistHandler->resetRecentlyPostedThumbnails();
    }

    // Push KWin's output-order primary screen to the daemon so getPrimaryScreen()
    // reflects KDE Display Settings rather than QGuiApplication::primaryScreen().
    auto* ws = KWin::Workspace::self();
    if (ws) {
        const auto outputs = ws->outputOrder();
        if (!outputs.isEmpty()) {
            PhosphorProtocol::ClientHelpers::fireAndForget(
                this, PhosphorProtocol::Service::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
        }
    }

    // Push KWin's authoritative per-screen work area (clientArea/MaximizeArea)
    // now that the bridge is up — see ScreenChangeHandler::scheduleClientAreaReport.
    if (m_screenChangeHandler) {
        m_screenChangeHandler->scheduleClientAreaReport();
    }

    // Re-push cursor screen — use the cached effective screen ID (which includes
    // virtual screen IDs like "A/vs:0") so the daemon's shortcut handler resolves
    // to the correct virtual screen, not the physical monitor.
    // m_lastEffectiveScreenId was set during the last processCursorPosition() call
    // via resolveEffectiveScreenId(), so it already has the correct virtual ID.
    if (!m_lastEffectiveScreenId.isEmpty()) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("cursorScreenChanged"), {m_lastEffectiveScreenId},
                                                       QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen:" << m_lastEffectiveScreenId;
    } else if (!m_lastCursorOutput.isEmpty()) {
        // Fallback: no effective ID cached yet (cursor hasn't moved since startup).
        // Resolve physical ID from connector name.
        QString cursorScreenId;
        for (const auto* output : KWin::effects->screens()) {
            if (output->name() == m_lastCursorOutput) {
                cursorScreenId = outputScreenId(output);
                break;
            }
        }
        if (cursorScreenId.isEmpty()) {
            cursorScreenId = m_lastCursorOutput;
        }
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("cursorScreenChanged"), {cursorScreenId},
                                                       QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen (physical fallback):" << cursorScreenId;
    }

    // Re-sync each output's current virtual desktop (Plasma 6.7 per-output virtual
    // desktops, #648). The daemon may have just (re)registered with an empty
    // per-screen map, so push the authoritative value for every screen — bypassing
    // reportScreenDesktop's dedup — and refresh the dedup cache to match.
    for (auto* output : KWin::effects->screens()) {
        auto* vd = KWin::effects->currentDesktop(output);
        if (!vd) {
            continue;
        }
        const QString screenId = outputScreenId(output);
        const int desktop = static_cast<int>(vd->x11DesktopNumber());
        m_lastScreenDesktop.insert(screenId, desktop);
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("screenDesktopChanged"), {screenId, desktop});
        qCDebug(lcEffect) << "Re-sent screen desktop:" << screenId << "->" << desktop;
    }

    // Re-notify active window (gives daemon lastActiveScreenName).
    // Use notifyWindowActivated which bypasses user exclusion lists — the daemon
    // must always know which window is active for correct shortcut handling.
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (activeWindow) {
        notifyWindowActivated(activeWindow);
    }

    // Fetch virtual screen definitions from daemon — needed before any screen ID
    // resolution so that getWindowScreenId() and cursor tracking return virtual
    // screen IDs when subdivisions are configured.
    // Clear ready flag immediately to close the race window where stale virtual
    // screen state from the previous daemon cycle is used before the new fetch
    // completes.
    m_virtualScreensReady = false;
    fetchAllVirtualScreenConfigs();

    // Re-sync floating windows (async, no QDBusInterface needed).
    // MUST clear the local set first — after daemon restart, the daemon's float state
    // is empty (ephemeral). Without clearing, stale entries from the previous daemon
    // session would persist in the effect, causing isWindowFloating() to return true
    // for windows that are no longer floating.
    {
        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getFloatingWindows")),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QStringList> reply = *w;
            // Clear the stale local float set unconditionally. This reply lands
            // during daemon bringup, so the freshly-registered daemon's float
            // state is authoritative (and empty on a fresh start). An invalid
            // reply still means the previous session's entries must be dropped —
            // retaining them would leave isWindowFloating() returning true for
            // windows that are no longer floating.
            m_navigationHandler->clearAllFloatingState();
            if (reply.isValid()) {
                // Bulk re-seed via the direct-write path (no per-window rule
                // invalidation) — the shared invalidation below drops every
                // placement-scoped verdict once, mirroring syncZonesFromDaemon.
                m_navigationHandler->seedFloatingWindows(reply.value());
            }
            // The clear (and any re-seed) changed the IsFloating match input;
            // drop the stale placement-scoped verdicts so a `WHEN isFloating`
            // rule re-resolves against the fresh state on the next frame
            // (mirrors the daemon-loss invalidation). Runs on the invalid-reply
            // path too — the unconditional clear above changed state there as
            // well, and skipping it would leave a cached "floating" verdict
            // pinned to a window that is no longer floating.
            invalidateAllRuleCaches();
        });
    }

    // Repopulate the snap-zone cache from the daemon's authoritative state so
    // windows snapped before this effect / daemon started are matchable by the
    // IsSnapped / Zone rule fields without waiting for their next state change.
    m_navigationHandler->syncZonesFromDaemon();

    // One-shot Rules subscription. The daemon emits rulesChanged per
    // per-rule mutation; slotRulesChanged debounces via a 50ms timer to
    // collapse batch edits into a single full-ruleset refetch. Subscribed here
    // (not in loadCachedSettings) because loadCachedSettings re-runs on every
    // settingsChanged broadcast, and QDBusConnection::connect accepts duplicate
    // subscriptions silently — re-subscribing on each broadcast would grow the
    // connection set unbounded across the effect's lifetime.
    if (!m_rulesSubscribed) {
        const bool ok = QDBusConnection::sessionBus().connect(
            QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
            QString(PhosphorProtocol::Service::Interface::Rules), QStringLiteral("rulesChanged"), this,
            SLOT(slotRulesChanged()));
        if (ok) {
            m_rulesSubscribed = true;
        } else {
            qCWarning(lcEffect) << "Failed to subscribe to Rules.rulesChanged — will retry on next bringup";
        }
    }

    // These already use QDBusMessage::createMethodCall (no QDBusInterface)
    loadCachedSettings();
    // Note: connectNavigationSignals() is NOT called here — it's already called
    // once in the constructor. D-Bus signal subscriptions persist across daemon
    // restarts. Calling it again would create duplicate connections, causing
    // handlers (e.g., toggleWindowFloat) to fire twice per signal.

    // Window state processing (autotile init, snap restore, etc.) depends on
    // virtual screen definitions being loaded for correct screen ID resolution.
    // Deferred to processDaemonReadyWindowState(), called by fetchAllVirtualScreenConfigs
    // once all async D-Bus replies have arrived.
}

void PlasmaZonesEffect::processDaemonReadyWindowState()
{
    if (m_daemonReadyWindowStateProcessed) {
        return;
    }
    m_daemonReadyWindowStateProcessed = true;

    // Delegate autotile re-initialization to handler.
    // Snapshot the active window so the autotile raise loop can re-activate it
    // after putting all tiled windows on top (which would bury non-tiled windows
    // like the KCM settings panel). Only set if the active window is NOT on an
    // autotile screen — autotile screens handle their own focus via
    // m_pendingAutotileFocusWindowId in the onComplete callback.
    KWin::EffectWindow* activeWin = KWin::effects->activeWindow();
    if (activeWin && !m_autotileHandler->isAutotileScreen(getWindowScreenId(activeWin))) {
        m_autotileHandler->setPendingReactivateWindow(activeWin);
    }
    m_autotileHandler->onDaemonReady();

    // Window re-announcement is NOT done here: onDaemonReady's loadSettings
    // queries the new daemon's authoritative autotile screen set and its
    // reply batches notifyWindowsAddedBatch(resetNotified=true). Announcing
    // here too would double-send AND use the pre-restart screen set, leaking
    // tracked-but-untiled entries for screens the new daemon doesn't autotile.
    const auto windows = KWin::effects->stackingOrder();

    // Report all live window IDs to the daemon so it can prune stale
    // entries from KConfig (windows that were snapped but no longer exist).
    {
        QStringList aliveWindowIds;
        for (KWin::EffectWindow* w : windows) {
            // !isDeleted: a close-grabbed dying window is NOT alive — listing
            // it would shield its stale persisted snap entry from the prune.
            if (w && !w->isDeleted() && shouldHandleWindow(w)) {
                aliveWindowIds.append(getWindowId(w));
            }
        }
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("pruneStaleWindows"),
            {QVariant::fromValue(aliveWindowIds)}, QStringLiteral("pruneStaleWindows"));
    }

    // Fetch pre-computed pending restore geometries so slotWindowAdded can
    // teleport windows to their zone immediately (no D-Bus round-trip flash).
    // Fire-and-forget: the cache is populated asynchronously. Windows that open
    // before the reply arrives fall back to the normal async restore path.
    {
        auto* geoWatcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getPendingRestoreGeometries")),
            this);
        connect(geoWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) {
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
            if (!doc.isObject()) {
                return;
            }
            QJsonObject obj = doc.object();
            m_snapHandler->clearRestoreCache();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                QJsonObject geo = it.value().toObject();
                // gw/gh, not w/h — `w` would shadow the lambda's watcher
                // parameter above.
                const int gx = geo[QLatin1String("x")].toInt();
                const int gy = geo[QLatin1String("y")].toInt();
                const int gw = geo[QLatin1String("width")].toInt();
                const int gh = geo[QLatin1String("height")].toInt();
                QString savedScreen = geo[QLatin1String("screenId")].toString();
                if (gw > 0 && gh > 0) {
                    m_snapHandler->cacheRestore(it.key(), CachedSnapRestore{QRect(gx, gy, gw, gh), savedScreen});
                }
            }
            qCDebug(lcEffect) << "Cached" << m_snapHandler->restoreCacheSize() << "pending restore geometries";
        });
    }

    // Restore snap state for all untracked windows.
    // pendingRestoresAvailable may have fired BEFORE daemonReady, causing
    // slotPendingRestoresAvailable to bail out (m_daemonServiceRegistered was false).
    // Now that the daemon is confirmed ready, retry the restore flow using raw
    // QDBusMessage (no QDBusInterface) to avoid synchronous introspection.
    {
        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getSnappedWindows")),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            QDBusPendingReply<QStringList> reply = *w;
            if (!reply.isValid()) {
                // Leave m_daemonReadyRestoresDone false: `finished` fires for
                // ERROR replies too, and latching the guard on a failed
                // getSnappedWindows would permanently disable the
                // slotPendingRestoresAvailable fallback for the session.
                qCWarning(lcEffect) << "getSnappedWindows failed at daemon-ready:" << reply.error().message()
                                    << "— deferring restores to pendingRestoresAvailable";
                return;
            }
            // Guard: prevent slotPendingRestoresAvailable from double-processing
            // the same windows. Set only on a VALID reply so a failed call
            // keeps the fallback alive.
            m_daemonReadyRestoresDone = true;

            // Re-drive per-window chrome (snap border / hidden title bar,
            // autotile border) for windows the daemon already considers managed.
            QSet<QString> trackedWindowIds;
            {
                // On daemon loss the effect cleared its window-appearance state
                // (DecorationManager::restoreAll + the handlers' tiled-tracking
                // clears) and restored every title bar; already-tracked windows are NOT in
                // the untracked-restore set below, so their chrome would never come
                // back without this. Daemon-driven and engine-common: the daemon
                // re-emits each engine's placement geometry, which routes through the
                // normal snap-commit / tile-request paths. Fired only on a VALID
                // reply — that proves the daemon's placement state is populated. The
                // windows are already in their zones, so nothing moves.
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("reapplyWindowAppearance"), {}, QStringLiteral("reapplyWindowAppearance"));

                const QStringList trackedWindows = reply.value();
                for (const QString& windowId : trackedWindows) {
                    if (!windowId.isEmpty()) {
                        trackedWindowIds.insert(windowId);
                    }
                }
            }

            // Snapshot the current stacking order before snap restores.
            // moveResize() on KWin 6 / Wayland implicitly raises the target
            // window. After all restores complete, we re-raise windows in
            // their original order — same pattern as the autotile handler's
            // onComplete raise loop in tiling.cpp.
            const auto allWindows = KWin::effects->stackingOrder();
            QVector<QPointer<KWin::EffectWindow>> savedStackingOrder;
            for (KWin::EffectWindow* w : allWindows) {
                savedStackingOrder.append(QPointer<KWin::EffectWindow>(w));
            }

            // Collect windows that need snap restoration (untracked).
            // Don't skip windows on autotile screens: KWin session restore may
            // place a window in the autotile screen's area even though it was
            // snapped in the snap screen before logout. The daemon's pending
            // restore entry knows the correct screen; if it returns a snap
            // geometry, the window moves off the autotile screen and the
            // autotile handler detects the departure via VS crossing detection.
            // Use QPointer for lifetime safety in case a window is destroyed
            // between collection and the dispatch loop below.
            QVector<QPointer<KWin::EffectWindow>> toRestore;
            for (KWin::EffectWindow* window : allWindows) {
                // !isDeleted: same deleted-window hygiene as the metadata
                // push and aliveWindowIds walks — getWindowId on a
                // close-grabbed dying window re-pollutes the id caches.
                if (!window || window->isDeleted() || !shouldHandleWindow(window)) {
                    continue;
                }
                if (window->isMinimized()) {
                    continue;
                }
                // Skip only if THIS window is already tracked (exact id). Deduping by
                // appId would skip an untracked window whose app has another tracked
                // window — stranding e.g. a multi-window terminal's window that raced
                // startup. The daemon tracks restored windows by live id, matching
                // getWindowId(). (Mirrors SnapHandler::slotPendingRestoresAvailable.)
                if (trackedWindowIds.contains(getWindowId(window))) {
                    continue;
                }
                toRestore.append(QPointer<KWin::EffectWindow>(window));
            }

            if (toRestore.isEmpty()) {
                qCDebug(lcEffect) << "No untracked windows need snap restore after daemon ready";
                return;
            }

            qCInfo(lcEffect) << "Triggered snap restore for" << toRestore.size()
                             << "untracked windows after daemon ready";

            // Track how many windows actually moved (moveResize was called).
            // If none moved, skip the stacking restoration — no disruption occurred.
            auto pending = std::make_shared<int>(toRestore.size());
            auto movedCount = std::make_shared<int>(0);

            for (const auto& safeWindow : toRestore) {
                if (!safeWindow || safeWindow->isDeleted()) {
                    // Window destroyed between collection and dispatch — count
                    // it as done so the pending counter still reaches zero.
                    if (--(*pending) == 0) {
                        qCDebug(lcEffect) << "Stacking restore: all targets gone, skipping";
                    }
                    continue;
                }
                // Snapshot geometry before the async call; if it changes after
                // applyWindowGeometry, we know a moveResize happened.
                QRectF geoBefore = safeWindow->frameGeometry();

                m_snapHandler->callResolveWindowRestore(
                    safeWindow.data(), [pending, movedCount, safeWindow, geoBefore, savedStackingOrder]() {
                        // Detect whether moveResize actually fired by comparing geometry.
                        if (safeWindow && !safeWindow->isDeleted() && safeWindow->frameGeometry() != geoBefore) {
                            ++(*movedCount);
                        }

                        if (--(*pending) > 0) {
                            return;
                        }

                        // All snap restores done.
                        if (*movedCount == 0) {
                            qCDebug(lcEffect) << "Stacking restore: all windows at target geometry, skipping";
                            return;
                        }

                        // Re-raise windows in original order (bottom-to-top).
                        auto* ws = KWin::Workspace::self();
                        if (!ws) {
                            return;
                        }
                        for (const auto& wPtr : savedStackingOrder) {
                            if (wPtr && !wPtr->isDeleted()) {
                                KWin::Window* kw = wPtr->window();
                                if (kw) {
                                    ws->raiseWindow(kw);
                                }
                            }
                        }
                    });
            }
        });
    }
}

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCInfo(lcEffect) << "settingsChanged: reloading settings";
    loadCachedSettings();
    // Note: loadAutotileSettings() is intentionally NOT called here.
    // Autotile screen changes are tracked via the dedicated autotileScreensChanged
    // D-Bus signal (→ slotAutotileScreensChanged), which is authoritative.
    // Calling loadAutotileSettings on every settingsChanged causes redundant
    // full window re-notification (N D-Bus windowOpened calls + retile round)
    // on every algorithm/gap/setting change — the daemon already retiles and
    // emits windowsTiled directly for those changes.
}

// Template implementation for loadSettingAsync — delegates to shared helper.
template<typename Fn>
void PlasmaZonesEffect::loadSettingAsync(const QString& name, Fn&& onValue)
{
    PhosphorProtocol::ClientHelpers::loadSettingAsync(this, name, std::forward<Fn>(onValue));
}

void PlasmaZonesEffect::loadCachedSettings()
{
    // Uses raw QDBusMessage (not QDBusInterface) to avoid synchronous introspection
    // that would block the compositor during login (see discussion #158).
    //
    // Transient exclusion and min-size are handled by the daemon. Exclusion lists are
    // cached here for drag-operation gating (shouldHandleWindow).
    m_triggersLoaded = false; // Permissive until new triggers arrive (#175)

    // excludedApplications / excludedWindowClasses are GONE — the v4
    // migration folded those lists into the unified Rule store, and
    // the effect's drag-gate exclusion rule set is now derived from the
    // store-side Exclude rules pulled via Rules.rulesChanged →
    // loadRuleAnimationsFromDbus. No D-Bus settings fetch needed.
    // isValid + clamp: a failed/invalid reply would otherwise toInt() to 0
    // and silently disable the min-size gate the permissive member defaults
    // exist to protect across the startup race (same hardening as the
    // animation min-size loaders below).
    loadSettingAsync(QStringLiteral("minimumWindowWidth"), [this](const QVariant& v) {
        if (v.isValid()) {
            m_cachedMinWindowWidth = qMax(0, v.toInt());
        }
    });
    loadSettingAsync(QStringLiteral("minimumWindowHeight"), [this](const QVariant& v) {
        if (v.isValid()) {
            m_cachedMinWindowHeight = qMax(0, v.toInt());
        }
    });
    // System colours for window-border rules: the zone highlight / inactive
    // colours track the Plasma colour scheme (when "use system colours" is on the
    // daemon keeps them in sync), and they are what a border-colour `accent`
    // sentinel resolves to in updateWindowDecoration — highlight for the focused
    // (active) slot, inactive for the unfocused (inactive) slot, mirroring the
    // distinct active/inactive system border colours the per-mode appearance
    // settings used before they folded into rules. Both are re-fetched on every
    // settingsChanged, so an accent / colour-scheme change repaints accent-
    // following borders without a relog.
    loadSettingAsync(QStringLiteral("highlightColor"), [this](const QVariant& v) {
        const QColor c(v.toString());
        if (m_borderAccentColor != c) {
            m_borderAccentColor = c;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("inactiveColor"), [this](const QVariant& v) {
        const QColor c(v.toString());
        if (m_borderInactiveColor != c) {
            m_borderInactiveColor = c;
            scheduleBorderSweep();
        }
    });
    // Config-backed window-decoration appearance default. Each key updates one
    // slot of m_windowAppearanceDefault; a change re-sweeps every window so the
    // default border / hidden title bar reapplies live (mirroring the accent /
    // inactive colour loaders above). Re-fetched on every settingsChanged, so a
    // Window Appearance page edit takes effect without a relog. Guarded on an
    // actual value change to avoid a redundant full border rebuild per fetch.
    loadSettingAsync(QStringLiteral("showWindowDecoration"), [this](const QVariant& v) {
        const bool b = v.toBool();
        if (m_windowAppearanceDefault.showBorder != b) {
            m_windowAppearanceDefault.showBorder = b;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderScope"), [this](const QVariant& v) {
        const QString s = v.toString();
        if (m_windowAppearanceDefault.borderScope != s) {
            m_windowAppearanceDefault.borderScope = s;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderWidth"), [this](const QVariant& v) {
        const int i = v.toInt();
        if (m_windowAppearanceDefault.borderWidth != i) {
            m_windowAppearanceDefault.borderWidth = i;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderRadius"), [this](const QVariant& v) {
        const int i = v.toInt();
        if (m_windowAppearanceDefault.borderRadius != i) {
            m_windowAppearanceDefault.borderRadius = i;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderColorActive"), [this](const QVariant& v) {
        const QString s = v.toString();
        if (m_windowAppearanceDefault.activeColor != s) {
            m_windowAppearanceDefault.activeColor = s;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderColorInactive"), [this](const QVariant& v) {
        const QString s = v.toString();
        if (m_windowAppearanceDefault.inactiveColor != s) {
            m_windowAppearanceDefault.inactiveColor = s;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("hideWindowTitleBars"), [this](const QVariant& v) {
        const bool b = v.toBool();
        if (m_windowAppearanceDefault.hideTitleBar != b) {
            m_windowAppearanceDefault.hideTitleBar = b;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowTitleBarScope"), [this](const QVariant& v) {
        const QString s = v.toString();
        if (m_windowAppearanceDefault.titleBarScope != s) {
            m_windowAppearanceDefault.titleBarScope = s;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("snapAssistEnabled"), [this](const QVariant& v) {
        m_snapAssistHandler->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationsEnabled"), [this](const QVariant& v) {
        m_windowAnimator->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationDuration"), [this](const QVariant& v) {
        // Clamp against the canonical settings-UI bounds. The earlier
        // local 500ms cap silently clamped a 2000ms user setting down
        // to 500ms, making shader transitions like matrix run far
        // faster than the daemon path's identical setting (the daemon
        // honours the full 2000ms range via the same constants).
        const int d = qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, v.toInt(),
                             PhosphorAnimation::Limits::MaxAnimationDurationMs);
        m_windowAnimator->setDuration(d);
        m_cachedAnimationDuration = d;
    });
    loadSettingAsync(QStringLiteral("animationEasingCurve"), [this](const QVariant& v) {
        // Polymorphic curve parse — handles bare bezier, named easing,
        // and "spring:..." in one path so Spring can drive snap motion
        // end-to-end without a settings-side branch.
        m_windowAnimator->setCurve(m_curveRegistry.create(v.toString()));
    });
    loadSettingAsync(QStringLiteral("animationMinDistance"), [this](const QVariant& v) {
        m_windowAnimator->setMinDistance(qBound(0, v.toInt(), 200));
    });
    loadSettingAsync(QStringLiteral("animationSequenceMode"), [this](const QVariant& v) {
        m_cachedAnimationSequenceMode = qBound(0, v.toInt(), 1);
    });
    loadSettingAsync(QStringLiteral("animationStaggerInterval"), [this](const QVariant& v) {
        m_cachedAnimationStaggerInterval = qBound(PhosphorAnimation::Limits::MinAnimationStaggerIntervalMs, v.toInt(),
                                                  PhosphorAnimation::Limits::MaxAnimationStaggerIntervalMs);
    });

    // Animation window filtering — independent of the snapping/tiling
    // exclusions cached above. Used by `shouldAnimateWindow()` to gate
    // the animation cascade; rules whose match expression resolves for
    // the window override the filter at the resolver layer so a targeted
    // rule can re-enable animation for an otherwise-excluded app.
    loadSettingAsync(QStringLiteral("animationExcludeTransientWindows"), [this](const QVariant& v) {
        m_animationExcludeTransientWindows = v.toBool();
    });
    // Default true (exclude). Guard on isValid() so a failed reply
    // leaves the member at its `true` init rather than `toBool()`'s
    // false-on-invalid — otherwise a missed fetch would animate
    // notifications/OSDs until the next successful settings load.
    loadSettingAsync(QStringLiteral("animationExcludeNotificationsAndOsd"), [this](const QVariant& v) {
        if (v.isValid()) {
            m_animationExcludeNotificationsAndOsd = v.toBool();
        }
    });
    // Clamp on the effect side as a defence-in-depth — the daemon's
    // schema validator already bounds these to [0, 2000], but a
    // malformed reply (`toInt()` returning 0 on a non-int variant or
    // a negative value from an out-of-spec callsite) would otherwise
    // silently disable / invert the min-size gate. Kept symmetric with
    // `animationDuration`'s `qBound` clamp above.
    loadSettingAsync(QStringLiteral("animationMinimumWindowWidth"), [this](const QVariant& v) {
        m_animationMinWindowWidth = qBound(0, v.toInt(), 2000);
    });
    loadSettingAsync(QStringLiteral("animationMinimumWindowHeight"), [this](const QVariant& v) {
        m_animationMinWindowHeight = qBound(0, v.toInt(), 2000);
    });
    // animationExcludedApplications / animationExcludedWindowClasses are
    // GONE — the v4 migration folded those lists into the unified
    // Rule store as `ExcludeAnimations`-action rules, and
    // loadRuleAnimationsFromDbus's parse step rebuilds the effect's
    // m_animationExclusionRuleSet from the same rule-set push that drives
    // the OverrideAnimation* pipeline. No D-Bus settings fetch needed.

    loadShaderProfileFromDbus();
    loadMotionProfileTreeFromDbus();
    loadShaderRegistryFromDbus();
    // Unified Rule store — pull in any rules carrying an
    // OverrideAnimation* action. The subscription below refreshes whenever
    // the daemon broadcasts `rulesChanged`, so an edit in the settings UI
    // lands without restarting the effect.
    loadRuleAnimationsFromDbus();
    // Subscription to the daemon's rulesChanged broadcast is installed once from
    // continueDaemonReadySetup() — installing it here would re-subscribe on every
    // slotSettingsChanged callback (QDBusConnection::connect silently accepts
    // duplicates, so the connection set would grow unbounded over the effect's
    // lifetime).
    loadSettingAsync(QStringLiteral("toggleActivation"), [this](const QVariant& v) {
        m_cachedToggleActivation = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragInsertToggle"), [this](const QVariant& v) {
        m_cachedAutotileDragInsertToggle = v.toBool();
    });
    loadSettingAsync(QStringLiteral("zoneSpanToggleMode"), [this](const QVariant& v) {
        m_cachedZoneSpanToggleMode = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragBehavior"), [this](const QVariant& v) {
        // Clamp unknown values to the safe default (Float) rather than the
        // highest known value — an older effect build against a newer daemon
        // must not silently map e.g. a future `ReorderAcrossScreens=2` onto
        // the nearest mode it happens to recognize.
        const int raw = v.toInt();
        switch (raw) {
        case static_cast<int>(EffectAutotileDragBehavior::Float):
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
            break;
        case static_cast<int>(EffectAutotileDragBehavior::Reorder):
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Reorder;
            break;
        default:
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
            break;
        }
    });
    loadSettingAsync(QStringLiteral("zoneSelectorEnabled"), [this](const QVariant& v) {
        m_cachedZoneSelectorEnabled = v.toBool();
    });

    // Window border / title-bar appearance is pushed as unified config defaults
    // (the window-appearance loaders above). Each slot is resolved as that config
    // default, scope-gated, with per-window rule overrides layered on top inside
    // updateWindowDecoration / reconcileRuleHiddenTitleBar (resolveEffectiveWindowAppearance).

    // Per-surface decoration profile tree (Stage 2a): the SSOT for each surface's
    // USER shader-pack chain (e.g. glow) plus each pack's parameter overrides,
    // keyed by surface path (window.tiled / window.snapped / window.floating).
    // The appearance-owned "border" base pack is prepended by updateWindowDecoration
    // from the config/rule resolution above; this tree contributes the packs the
    // user chained on top. The autotile/snap BorderState is still maintained (it
    // drives MEMBERSHIP — which windows are tiled/snapped — and the daemon's
    // retile insets), but does not feed appearance.
    //
    // On change: drop every compiled pack (a chain edit may reference a new pack,
    // and per-pack param VALUES are baked at compile time so they must recompile)
    // and rebuild all borders against the new tree, then repaint.
    loadSettingAsync(
        QString(PhosphorProtocol::Service::SettingProperty::DecorationProfileTree), [this](const QVariant& v) {
            const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
            if (!doc.isObject()) {
                qCWarning(lcEffect) << "decorationProfileTreeJson is not a JSON object — keeping current tree";
                return;
            }
            PhosphorSurfaceShaders::DecorationProfileTree tree =
                PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object());
            if (tree == m_decorationTree) {
                return;
            }
            m_decorationTree = std::move(tree);
            // Per-pack param values are baked at first compile, so a tree change that
            // alters parameters[packId] requires a recompile of that pack — clear the
            // whole compiled-pack cache (it lazily recompiles on the next paint).
            m_compiledPacks.clear();
            updateAllDecorations();
            if (KWin::effects) {
                KWin::effects->addRepaintFull();
            }
        });

    loadSettingAsync(QStringLiteral("autotileFocusFollowsMouse"), [this](const QVariant& v) {
        m_autotileHandler->setFocusFollowsMouse(v.toBool());
    });

    loadSettingAsync(QStringLiteral("snappingFocusFollowsMouse"), [this](const QVariant& v) {
        m_snapHandler->setFocusFollowsMouse(v.toBool());
    });

    // dragActivationTriggers — uses shared TriggerParser for QDBusArgument deserialization
    {
        PhosphorProtocol::ClientHelpers::loadSettingAsync(
            this, QStringLiteral("dragActivationTriggers"), [this](const QVariant& v) {
                m_parsedTriggers = TriggerParser::parseTriggers(v, TriggerModifierField, TriggerMouseButtonField);

                qCDebug(lcEffect) << "Loaded dragActivationTriggers:" << m_parsedTriggers.size() << "triggers";
                bool anyValid =
                    std::any_of(m_parsedTriggers.cbegin(), m_parsedTriggers.cend(), [](const ParsedTrigger& pt) {
                        return pt.modifier != 0 || pt.mouseButton != 0;
                    });
                if (!m_parsedTriggers.isEmpty() && !anyValid) {
                    qCWarning(lcEffect) << "All triggers have modifier=0 mouseButton=0"
                                        << "- possible deserialization issue";
                }
                m_triggersLoaded = true;
            });
    }

    qCDebug(lcEffect) << "Loading cached settings asynchronously, using defaults until loaded";
}

bool PlasmaZonesEffect::anyLocalTriggerHeld() const
{
    return TriggerParser::anyTriggerHeld(m_parsedTriggers, m_currentModifiers, m_currentMouseButtons);
}

bool PlasmaZonesEffect::detectActivationAndGrab()
{
    if (m_dragActivationDetected) {
        return true;
    }
    // Autotile drag-insert toggle mode also forces activation so the daemon
    // receives dragMoved ticks for rising-edge detection even when the drag
    // started on a non-autotile screen and the user hasn't held any snap
    // trigger. Without this, the cross-to-autotile policy flip never fires
    // because the gate below (drag lambda, slotMouseChanged) swallows ticks.
    // Zone-span toggle mode (#563) forces activation for the same reason: the
    // daemon's span rising-edge latch needs the release→press ticks even when
    // no key is currently held (e.g. activation itself is toggled on and the
    // user tapped, then released, the activation trigger).
    if (anyLocalTriggerHeld() || m_cachedToggleActivation || m_cachedAutotileDragInsertToggle
        || m_cachedZoneSpanToggleMode) {
        m_dragActivationDetected = true;
        if (!m_keyboardGrabbed) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return true;
    }
    return false;
}

// beginDrag is called unconditionally at drag-start; there's no deferred
// "only send dragStarted when zones activate" path because the daemon
// always knows about the drag from the moment it begins.

void PlasmaZonesEffect::connectNavigationSignals()
{
    // Daemon-driven navigation: daemon computes geometry and emits applyGeometryRequested directly
    QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometryRequested"), this,
        SLOT(slotApplyGeometryRequested(QString, int, int, int, int, QString, QString, bool)));

    // Daemon-driven focus/cycle: daemon resolves target window and emits activateWindowRequested
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("activateWindowRequested"), this,
                                          SLOT(slotActivateWindowRequested(QString)));

    // Cross-desktop directional move: daemon re-keys its tiling state and asks
    // the effect to move the real KWin window to the target virtual desktop.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("windowDesktopMoveRequested"), this,
                                          SLOT(slotWindowDesktopMoveRequested(QString, int)));

    // Daemon-initiated cross-output move: the daemon already migrated its
    // tiling state and reflowed both outputs; record the window so the
    // autotile handler's reactive outputChanged path updates bookkeeping only
    // instead of re-issuing windowClosed/windowOpened (which would tear down
    // the daemon's placement and strand the source monitor's reflow).
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("windowOutputMoveExpected"), this,
                                          SLOT(slotWindowOutputMoveExpected(QString, QString)));

    // Float toggle is entirely daemon-local: the daemon reads the active
    // window from its own shadow, calls toggleFloatForWindow internally, and
    // emits applyGeometryRequested to paint the outcome. The effect no longer
    // participates in the decision.

    // Daemon-driven batch operations (rotate, resnap, vs_reconfigure emit
    // applyGeometriesBatch; effect-local snap_all calls the slot directly)
    QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("applyGeometriesBatch"), this,
        SLOT(slotApplyGeometriesBatch(PhosphorProtocol::WindowGeometryList, QString)));

    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("raiseWindowsRequested"), this,
                                          SLOT(slotRaiseWindowsRequested(QStringList)));

    // Snap-all: daemon triggers effect to collect candidates
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("snapAllWindowsRequested"), m_snapHandler.get(),
                                          SLOT(slotSnapAllWindowsRequested(QString)));

    // Move specific window (Snap Assist selection)
    QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("moveSpecificWindowToZoneRequested"),
        m_snapHandler.get(), SLOT(slotMoveSpecificWindowToZoneRequested(QString, QString, int, int, int, int)));

    // Pending restores on daemon startup
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("pendingRestoresAvailable"), m_snapHandler.get(),
                                          SLOT(slotPendingRestoresAvailable()));

    // Screen geometry reapply
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("reapplyWindowGeometriesRequested"),
                                          m_screenChangeHandler.get(), SLOT(slotReapplyWindowGeometriesRequested()));

    // Floating state sync
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("windowFloatingChanged"), this,
                                          SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    // Snap-zone state sync — feeds the effect-side zone cache the IsSnapped /
    // Zone rule-match fields read. Carries the per-window WindowStateEntry
    // (zoneId / changeType) on every snap / unsnap / float / screen-change.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowTracking,
                                          QStringLiteral("windowStateChanged"), this,
                                          SLOT(slotWindowStateChanged(QString, PhosphorProtocol::WindowStateEntry)));

    // Settings: window picker for KCM exclusion list
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::Settings,
                                          QStringLiteral("runningWindowsRequested"), this,
                                          SLOT(slotRunningWindowsRequested()));

    // WindowDrag: during-drag size restore
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowDrag,
                                          QStringLiteral("restoreSizeDuringDragChanged"), this,
                                          SLOT(slotRestoreSizeDuringDrag(QString, int, int)));

    // WindowDrag: cross-VS policy flip. Daemon detects the cursor crossing
    // a virtual-screen boundary that changes autotile↔snap routing and
    // emits this signal so the effect can apply the transition locally
    // (handleDragToFloat, onWindowClosed, overlay cancel, etc.).
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowDrag,
                                          QStringLiteral("dragPolicyChanged"), this,
                                          SLOT(slotDragPolicyChanged(QString, PhosphorProtocol::DragPolicy)));

    // WindowDrag: snap assist (delivered asynchronously, separate from the
    // fast endDrag reply). The daemon schedules the empty-zone-list compute
    // after endDrag returns, so the compositor is unblocked first.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::WindowDrag,
                                          QStringLiteral("snapAssistReady"), m_snapHandler.get(),
                                          SLOT(slotSnapAssistReady(QString, QString, PhosphorProtocol::EmptyZoneList)));

    qCInfo(lcEffect) << "Connected to navigation D-Bus signals";
}

} // namespace PlasmaZones
