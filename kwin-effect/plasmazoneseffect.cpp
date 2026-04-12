// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <dbus_helpers.h>
#include <dbus_types.h>
#include <screen_id.h>
#include <stagger_timer.h>
#include <window_id.h>

#include <algorithm>
#include <memory>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QScreen>
#include <QtMath>
#include <QPointer>
#include <window.h>
#include <workspace.h>
#include <core/output.h> // For Output::name() for multi-monitor support
#include <scene/windowitem.h>
#include <scene/surfaceitem.h>
#include <scene/outlinedborderitem.h>
#include <scene/borderoutline.h>

#include "autotilehandler.h"
#include "kwin_compositor_bridge.h"
#include "screenchangehandler.h"
#include "snapassisthandler.h"
#include "navigationhandler.h"
#include "windowanimator.h"
#include "dragtracker.h"
#include "dbus_constants.h"
// QGuiApplication::queryKeyboardModifiers() doesn't work in KWin effects on Wayland
// because the effect runs inside the compositor process. We use mouseChanged instead.

namespace PlasmaZones {

Q_LOGGING_CATEGORY(lcEffect, "plasmazones.effect", QtInfoMsg)

namespace {
// Duplicated from daemon's configkeys.h — effect cannot include daemon headers
constexpr QLatin1String TriggerModifierField("modifier");
constexpr QLatin1String TriggerMouseButtonField("mouseButton");
} // namespace

// NavigateDirectivePrefix moved to navigationhandler.cpp to avoid redefinition

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus helpers (all async — no QDBusInterface to avoid synchronous introspection)
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Method Implementations
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                                    const QRectF& preCapturedGeometry)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!isDaemonReady("ensure pre-snap geometry")) {
        return;
    }

    QPointer<KWin::EffectWindow> safeWindow = w;
    QString capturedWindowId = windowId;
    QRectF capturedGeom = preCapturedGeometry;

    QDBusPendingCall pendingCall =
        DBusHelpers::asyncCall(DBus::Interface::WindowTracking, QStringLiteral("hasPreTileGeometry"), {windowId});
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedGeom](QDBusPendingCallWatcher* watcher) {
                watcher->deleteLater();

                QDBusPendingReply<bool> reply = *watcher;
                bool hasGeometry = reply.isValid() && reply.value();

                if (!hasGeometry && m_daemonServiceRegistered) {
                    // Use pre-captured geometry if provided, otherwise read from window
                    QRectF geom =
                        capturedGeom.isValid() ? capturedGeom : (safeWindow ? safeWindow->frameGeometry() : QRectF());
                    if (geom.width() > 0 && geom.height() > 0) {
                        // Use virtual-screen-aware ID — getWindowScreenId() falls back to the
                        // physical ID when virtual screen defs haven't loaded yet, so it is
                        // safe to call unconditionally. Using it here ensures the stored
                        // screen ID always matches the ID used by later lookups.
                        QString screenId = safeWindow ? getWindowScreenId(safeWindow) : QString();
                        DBusHelpers::fireAndForget(
                            this, DBus::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
                            {capturedWindowId, static_cast<int>(geom.x()), static_cast<int>(geom.y()),
                             static_cast<int>(geom.width()), static_cast<int>(geom.height()), screenId, false},
                            QStringLiteral("storePreTileGeometry"));
                        qCInfo(lcEffect) << "Stored pre-tile geometry for window" << capturedWindowId;
                    }
                }
            });
}

QHash<QString, KWin::EffectWindow*> PlasmaZonesEffect::buildWindowMap(bool filterHandleable) const
{
    const auto windows = KWin::effects->stackingOrder();
    QHash<QString, KWin::EffectWindow*> windowMap;
    windowMap.reserve(windows.size());
    for (KWin::EffectWindow* w : windows) {
        if (w && (!filterHandleable || shouldHandleWindow(w))) {
            windowMap[getWindowId(w)] = w;
        }
    }
    return windowMap;
}

KWin::EffectWindow* PlasmaZonesEffect::getValidActiveWindowOrFail(const QString& action)
{
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for" << action;
        emitNavigationFeedback(false, action, QStringLiteral("no_window"));
        return nullptr;
    }
    return activeWindow;
}

bool PlasmaZonesEffect::isWindowFloating(const QString& windowId) const
{
    return m_navigationHandler->isWindowFloating(windowId);
}

PlasmaZonesEffect::PlasmaZonesEffect()
    : Effect()
    , m_autotileHandler(std::make_unique<AutotileHandler>(this))
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
    , m_screenChangeHandler(std::make_unique<ScreenChangeHandler>(this))
    , m_snapAssistHandler(std::make_unique<SnapAssistHandler>(this))
    , m_windowAnimator(std::make_unique<WindowAnimator>(this))
    , m_dragTracker(std::make_unique<DragTracker>(this))
    , m_compositorBridge(std::make_unique<KWinCompositorBridge>(this))
{
    PlasmaZones::registerDBusTypes();

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

    // Connect DragTracker signals
    //
    // Performance optimization: keyboard grab and D-Bus dragMoved calls are deferred
    // until an activation trigger is detected. This eliminates 60Hz D-Bus traffic and
    // keyboard grab/ungrab overhead for non-zone window drags (discussion #167).
    connect(m_dragTracker.get(), &DragTracker::dragStarted, this,
            [this](KWin::EffectWindow* w, const QString& windowId, const QRectF& geometry) {
                qCDebug(lcEffect) << "Window move started -" << w->windowClass()
                                  << "current modifiers:" << static_cast<int>(m_currentModifiers);

                // Fire beginDrag async to get a daemon-authoritative policy.
                // While the reply is pending, we
                // default m_currentDragPolicy to a conservative snap-path so
                // the worst case (stale effect cache would have said autotile
                // but daemon knows better, or vice-versa) is a brief overlay
                // flash rather than a dead drag. The reply handler flips the
                // bypass flag retroactively a few ms later if the daemon says
                // this is an autotile drag.
                //
                // This replaces the previous stale-cache read of
                // m_autotileHandler->isAutotileScreen() as the single source
                // of truth for drag-start routing — root cause of the
                // post-settings-reload dead-drag window found in #310 log
                // forensics.
                m_currentDragPolicy = DragPolicy{};
                m_currentDragPolicy.streamDragMoved = true;
                m_currentDragPolicy.showOverlay = true;
                m_currentDragPolicy.grabKeyboard = true;
                m_currentDragPolicy.captureGeometry = true;

                const QString startScreenId = getWindowScreenId(w);
                const QRect frame = geometry.toRect();
                QDBusMessage beginMsg = QDBusMessage::createMethodCall(
                    DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag, QStringLiteral("beginDrag"));
                beginMsg << windowId << frame.x() << frame.y() << frame.width() << frame.height() << startScreenId
                         << static_cast<int>(m_currentMouseButtons);
                QDBusPendingCall beginPending = QDBusConnection::sessionBus().asyncCall(beginMsg);
                auto* beginWatcher = new QDBusPendingCallWatcher(beginPending, this);
                QPointer<KWin::EffectWindow> safeW = w;
                const QString capturedWindowId = windowId;
                const QString capturedScreenId = startScreenId;
                connect(beginWatcher, &QDBusPendingCallWatcher::finished, this,
                        [this, safeW, capturedWindowId, capturedScreenId](QDBusPendingCallWatcher* bw) {
                            bw->deleteLater();
                            QDBusPendingReply<DragPolicy> reply = *bw;
                            if (!reply.isValid()) {
                                qCWarning(lcEffect) << "beginDrag reply invalid:" << reply.error().message();
                                return;
                            }
                            m_currentDragPolicy = reply.value();
                            qCInfo(lcEffect) << "beginDrag reply:" << capturedWindowId
                                             << "bypass=" << m_currentDragPolicy.bypassReason
                                             << "stream=" << m_currentDragPolicy.streamDragMoved
                                             << "immediateFloat=" << m_currentDragPolicy.immediateFloatOnStart;
                            // If the daemon confirms autotile, flip the effect
                            // state to bypass mode. Usually the effect-side
                            // fast path below already did this synchronously;
                            // this catches the stale-cache case where the fast
                            // path missed.
                            if (m_currentDragPolicy.bypassReason == QLatin1String("autotile_screen")) {
                                if (!m_dragBypassedForAutotile) {
                                    m_dragBypassedForAutotile = true;
                                    m_dragBypassScreenId = capturedScreenId;
                                    qCInfo(lcEffect)
                                        << "beginDrag: retroactive autotile bypass for" << capturedWindowId;
                                }
                                // Apply immediate float transition if the policy
                                // says so and the window wasn't already floated
                                // by the fast path. Using QPointer so we skip
                                // if the window was destroyed between drag-start
                                // and reply.
                                if (safeW && m_currentDragPolicy.immediateFloatOnStart
                                    && !isWindowFloating(capturedWindowId)
                                    && !m_dragFloatedWindowIds.contains(capturedWindowId)) {
                                    m_autotileHandler->handleDragToFloat(safeW, capturedWindowId, capturedScreenId,
                                                                         /*immediate=*/true);
                                    m_dragFloatedWindowIds.insert(capturedWindowId);
                                }
                            }
                        });

                // Fast path: the effect-side autotile cache is USUALLY correct.
                // We still consult it synchronously so the common case runs at
                // zero latency. The async beginDrag reply above runs as a
                // correction layer for the cases where the cache is stale
                // (post-settings-reload — the #310 scenario).
                if (m_autotileHandler->isAutotileScreen(startScreenId)) {
                    m_dragBypassedForAutotile = true;
                    m_dragBypassScreenId = startScreenId;
                    // If the window is currently autotile-tiled, restore its
                    // title bar and pre-autotile size NOW (synchronously, during
                    // the interactive move). This mirrors snap mode, where
                    // dragging a snapped window out of its zone visibly restores
                    // the free-floating size before release — without this, the
                    // user drags a borderless tile-sized window and only sees it
                    // become a floating window after they drop.
                    //
                    // Guarded on isTrackedWindow so we don't touch windows that
                    // are already floating (not in the autotile tree).
                    if (m_autotileHandler->isTrackedWindow(windowId) && !isWindowFloating(windowId)) {
                        m_autotileHandler->handleDragToFloat(w, windowId, m_dragBypassScreenId, /*immediate=*/true);
                        // Mark as drag-floated so the daemon's pre-tile geometry
                        // restore (applyGeometryForFloat, triggered by the
                        // setWindowFloatingForScreen call at drop) is skipped in
                        // slotApplyGeometryRequested — the window should stay
                        // where the user drops it, not snap back to a stored rect.
                        m_dragFloatedWindowIds.insert(windowId);
                    }
                    return;
                }
                m_dragBypassedForAutotile = false;
                m_dragActivationDetected = false;
                m_dragStartedSent = false;
                m_pendingDragWindowId = windowId;
                m_pendingDragGeometry = geometry;
                m_snapDragStartScreenId = getWindowScreenId(w);

                // beginDrag already initialized daemon-side snap-drag state
                // (called internally from the adaptor). The effect only needs
                // to decide whether to grab the keyboard for local Escape
                // handling.
                detectActivationAndGrab();
                // Grab keyboard to intercept Escape before KWin's MoveResizeFilter.
                // Without this, Escape cancels the interactive move AND the overlay.
                // With the grab, Escape only dismisses the overlay while the drag continues.
                if (!m_keyboardGrabbed) {
                    KWin::effects->grabKeyboard(this);
                    m_keyboardGrabbed = true;
                }
            });
    connect(
        m_dragTracker.get(), &DragTracker::dragMoved, this, [this](const QString& windowId, const QPointF& cursorPos) {
            // Cross-VS flip detection is daemon-owned. The
            // daemon's updateDragCursor handler computes policy at the
            // cursor position and emits dragPolicyChanged when it flips.
            // The effect reacts via slotDragPolicyChanged (see below).
            //
            // Here we only forward the cursor to the daemon as a
            // fire-and-forget call. The daemon-side dispatch handles
            // both the snap-path overlay updates and the cross-VS
            // detection in a single round trip.

            // In autotile bypass — skip snap zone processing locally;
            // the daemon's updateDragCursor still watches for a flip
            // BACK to snap mode.
            const bool bypassed =
                m_currentDragPolicy.bypassReason == QLatin1String("autotile_screen") || m_dragBypassedForAutotile;
            if (!bypassed) {
                // Gate D-Bus calls on activation trigger state so a drag
                // without any intent to use zones doesn't flood the bus
                // at 30Hz. This is a local input-event optimization; it
                // isn't policy and doesn't come from the daemon.
                if (!detectActivationAndGrab() && !m_cachedZoneSelectorEnabled && m_triggersLoaded) {
                    return;
                }
            }

            // Forward the cursor to the daemon. For snap drags, this
            // drives overlay/zone detection. For bypass drags, the
            // daemon watches the cursor for a cross-VS flip and emits
            // dragPolicyChanged when the policy changes.
            DBusHelpers::fireAndForget(this, DBus::Interface::WindowDrag, QStringLiteral("updateDragCursor"),
                                       {windowId, qRound(cursorPos.x()), qRound(cursorPos.y()),
                                        static_cast<int>(m_currentModifiers), static_cast<int>(m_currentMouseButtons)},
                                       QStringLiteral("updateDragCursor"));
        });
    connect(m_dragTracker.get(), &DragTracker::dragStopped, this,
            [this](KWin::EffectWindow* w, const QString& windowId, bool cancelled) {
                // Release keyboard grab before handling drag end
                if (m_keyboardGrabbed) {
                    KWin::effects->ungrabKeyboard();
                    m_keyboardGrabbed = false;
                }

                // Single entry point for drag-end dispatch. The
                // daemon owns the decision; callEndDrag sends endDrag and
                // the reply handler applies whatever DragOutcome comes back
                // (ApplySnap / ApplyFloat / RestoreSize / NoOp / etc.).
                //
                // The autotile branch special-casing that used to live here
                // is gone — cross-VS transitions were applied mid-drag by
                // slotDragPolicyChanged, and final drop-time actions are
                // encoded in the DragOutcome.
                callEndDrag(w, windowId, cancelled);

                // Clear drag state for the next session.
                m_currentDragPolicy = DragPolicy{};
                m_dragBypassedForAutotile = false;
                m_dragBypassScreenId.clear();
                m_snapDragStartScreenId.clear();
                m_dragActivationDetected = false;
                m_dragStartedSent = false;
                m_pendingDragWindowId.clear();
                m_pendingDragGeometry = QRectF();
            });

    // Connect to window lifecycle signals
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &PlasmaZonesEffect::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &PlasmaZonesEffect::slotWindowClosed);

    // Belt-and-suspenders: windowClosed removes animations, but if a deferred
    // timer re-adds one between windowClosed and windowDeleted, the Item tree
    // will be torn down while an animation entry still references the window.
    // Purge here to prevent SIGSEGV in animationBounds → expandedGeometry.
    // Also clean up caches that slotWindowClosed may have already cleared —
    // QHash::take/remove on missing keys is a no-op, so this is safe.
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, [this](KWin::EffectWindow* w) {
        m_windowAnimator->removeAnimation(w);
        if (m_windowIdCache.contains(w)) {
            const QString cachedId = m_windowIdCache.take(w);
            m_windowIdReverse.remove(cachedId);
        }
        m_trackedScreenPerWindow.remove(w);
    });

    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &PlasmaZonesEffect::slotWindowActivated);

    // Update the daemon's primary screen override when KDE Display Settings change
    if (auto* ws = KWin::Workspace::self()) {
        connect(ws, &KWin::Workspace::outputOrderChanged, this, [this]() {
            auto* workspace = KWin::Workspace::self();
            if (workspace && m_daemonServiceRegistered) {
                const auto outputs = workspace->outputOrder();
                if (!outputs.isEmpty()) {
                    DBusHelpers::fireAndForget(this, DBus::Interface::Screen,
                                               QStringLiteral("setPrimaryScreenFromKWin"), {outputs.first()->name()},
                                               QStringLiteral("setPrimaryScreenFromKWin"));
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
    // Invalidate screen ID cache and refresh virtual screen definitions on screen changes
    // (connector names may be reassigned, physical screen geometry changes invalidate
    // virtual screen absolute geometry)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, this, [this]() {
        m_screenIdCache.clear();
        m_lastEffectiveScreenId.clear();
    });

    // Connect to daemon's settingsChanged D-Bus signal
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                          QStringLiteral("settingsChanged"), this, SLOT(slotSettingsChanged()));
    qCInfo(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";

    // Connect to virtual screen changes — daemon emits this when a physical screen's
    // virtual subdivisions are added, removed, or modified.
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Screen,
                                          QStringLiteral("virtualScreensChanged"), this,
                                          SLOT(onVirtualScreensChanged(QString)));

    // Connect to keyboard navigation D-Bus signals
    connectNavigationSignals();

    // Connect to autotile D-Bus signals
    m_autotileHandler->connectSignals();
    m_autotileHandler->loadSettings();

    // Verify daemon availability asynchronously to avoid blocking the compositor.
    // CRITICAL: Do NOT use synchronous isServiceRegistered() here. The daemon
    // registers its D-Bus service name in init() BEFORE start() runs heavy
    // initialization and BEFORE the event loop begins (main.cpp:88→94→102).
    // During that window, isServiceRegistered() returns true but the daemon
    // can't process messages. Any synchronous QDBusInterface creation would
    // trigger Introspect, blocking KWin for up to the D-Bus timeout (~25s).
    //
    // Instead, send an async Introspect — if the daemon responds, it's fully
    // operational and we trigger slotDaemonReady(). If it can't respond (still
    // initializing), the call times out harmlessly and we wait for the
    // daemonReady D-Bus signal instead.
    {
        QDBusMessage introspect = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath,
                                                                 QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                                                 QStringLiteral("Introspect"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(introspect, 3000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (reply.isValid() && !m_daemonServiceRegistered) {
                // Daemon responded — it's fully operational.
                // Trigger the same ready flow as the daemonReady signal.
                slotDaemonReady();
            }
        });
    }

    // Connect to daemon's daemonReady signal — emitted at the end of Daemon::start()
    // after all initialization is complete and the daemon can process D-Bus messages.
    // This is the safe point to set m_daemonServiceRegistered and create QDBusInterfaces.
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::LayoutManager,
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
        DBus::ServiceName, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon service unregistered";
        m_daemonServiceRegistered = false;
        m_daemonReadyRestoresDone = false;
        m_daemonReadyWindowStateProcessed = false;
        m_snapRestoreCache.clear();

        // Restore borderless and monocle-maximized windows — daemon state is gone
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        clearAllBorders();
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon registered: waiting for daemonReady signal";

        // DO NOT set m_daemonServiceRegistered = true here.
        // The daemon registers its D-Bus service name in init(), BEFORE start()
        // runs heavy initialization and BEFORE the event loop begins. Keep the
        // flag false until the daemon's own daemonReady signal fires (end of
        // Daemon::start()), confirming it can handle D-Bus requests.

        // Reconnect daemonReady signal — Qt may cache the old daemon's unique bus
        // name in match rules, so refresh for the new daemon instance.
        // Disconnect first to prevent duplicate match rules (Qt doesn't deduplicate),
        // which would cause slotDaemonReady to fire twice on the same signal.
        QDBusConnection::sessionBus().disconnect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::LayoutManager,
                                                 QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
        QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::LayoutManager,
                                              QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
    });

    // NOTE: syncFloatingWindowsFromDaemon() and loadCachedSettings() are NOT
    // called here. m_daemonServiceRegistered is false at this point (set only by
    // slotDaemonReady), so any ensureInterface() call would bail out immediately.
    // All daemon state sync is deferred to slotDaemonReady().

    // Connect to existing windows
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
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

    qCInfo(lcEffect) << "initialized: C++ effect with D-Bus support and mouseChanged connection";
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    // Restore borderless and monocle-maximized windows so they recover properly.
    // Guard against compositor teardown — effects may outlive the stacking order.
    if (KWin::effects) {
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        clearAllBorders();
    }

    if (m_keyboardGrabbed) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }
    m_screenChangeHandler->stop();
    // We no longer reserve/unreserve edges; the daemon disables KWin snap via config.
}

bool PlasmaZonesEffect::supported()
{
    // This effect is a compositor plugin that works in KWin on Wayland
    // Note: PlasmaZones daemon requires Wayland with layer-shell support
    return true;
}

bool PlasmaZonesEffect::enabledByDefault()
{
    return true;
}

void PlasmaZonesEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    // Called when KWin wants effects to reload or when daemon notifies of settings change
    qCDebug(lcEffect) << "reconfigure() called";
}

bool PlasmaZonesEffect::isActive() const
{
    return m_dragTracker->isDragging() || m_windowAnimator->hasActiveAnimations();
}

void PlasmaZonesEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (e->type() == QEvent::KeyPress && e->key() == Qt::Key_Escape && m_dragTracker->isDragging()) {
        // The keyboard grab ensures this runs before KWin's MoveResizeFilter,
        // so Escape never reaches the interactive move handler. The daemon
        // hides the overlay and sets snapCancelled; the drag continues as
        // a plain window move without zone snapping.
        qCInfo(lcEffect) << "Drag escape: overlay hidden, drag continues";
        callCancelSnap();
    }
    // All other keys are silently consumed by the grab. Modifier state is
    // unaffected because mouseChanged reads xkb state directly.
}

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // Populate the daemon's WindowRegistry with this window's initial metadata.
    // Runs before any other daemon notification so consumers querying the
    // registry from their windowOpened handlers see a record (sessions 2+).
    pushWindowMetadata(w);

    // Sync floating state for this window from daemon
    // This ensures windows that were floating when closed remain floating when reopened
    // Use full windowId so daemon can do per-instance lookup with appId fallback
    QString windowId = getWindowId(w);
    m_navigationHandler->syncFloatingStateForWindow(windowId);

    bool onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));

    // Check if this window is a candidate for snap restore
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    bool canSnapRestore =
        shouldHandleWindow(w) && isTileableWindow(w) && !w->isMinimized() && !hasOtherWindowOfClassWithDifferentPid(w);

    // Instant snap restore: if we have a cached zone geometry for this app,
    // teleport the window immediately — no D-Bus round-trip, no visible flash.
    // The async callResolveWindowRestore still runs to register the zone assignment
    // in the daemon; this just eliminates the visual lag.
    if (canSnapRestore && !m_snapRestoreCache.isEmpty()) {
        QString appId = WindowIdUtils::extractAppId(windowId);
        auto cacheIt = m_snapRestoreCache.find(appId);
        if (cacheIt != m_snapRestoreCache.end()) {
            QRect cachedGeo = cacheIt.value();
            if (cachedGeo.isValid()) {
                qCInfo(lcEffect) << "Instant snap restore for" << appId << "to:" << cachedGeo;
                applySnapGeometry(w, cachedGeo, false, /*skipAnimation=*/true);
                m_snapRestoreCache.erase(cacheIt);
                // Re-evaluate screen after teleport — the window may have moved
                // off the autotile screen, so update for the autotile decision below.
                onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));
            }
        }
    }

    if (onAutotileScreen && canSnapRestore) {
        // Window landed on an autotile screen, but may have a pending snap restore
        // to a non-autotile screen. KWin's session restore places windows at their
        // saved geometry, which may be a pre-snap floating position in the autotile
        // screen's area — even though the window was snapped in the snap screen
        // before logout. Try snap restore FIRST: if it moves the window off the
        // autotile screen, we avoid the autotile add→float→remove→resnap dance
        // that causes visible flickering and repeated resizing.
        QPointer<KWin::EffectWindow> safeW = w;
        callResolveWindowRestore(w, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // Snap restore either moved the window to a snap screen (no-op for
            // autotile) or didn't apply (window genuinely belongs on autotile).
            m_autotileHandler->notifyWindowAdded(safeW);
        });
        return;
    }

    // Standard path: notify autotile first, then try snap restore
    m_autotileHandler->notifyWindowAdded(w);

    if (!onAutotileScreen && canSnapRestore) {
        callResolveWindowRestore(w);
    }
}

void PlasmaZonesEffect::slotWindowClosed(KWin::EffectWindow* w)
{
    // Release keyboard grab if the dragged window was closed
    if (m_keyboardGrabbed && m_dragTracker->draggedWindow() == w) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }

    // Delegate to helpers
    m_dragTracker->handleWindowClosed(w);

    // Clear floating state — floating is runtime-only and resets on window close.
    // The daemon clears its side in windowClosed().
    m_navigationHandler->setWindowFloating(getWindowId(w), false);

    m_windowAnimator->removeAnimation(w);

    const QString closedWindowId = getWindowId(w);
    const QString closedScreenId = getWindowScreenId(w);

    // Clean up snap-mode minimize tracking
    m_minimizeFloatedWindows.remove(closedWindowId);
    m_dragFloatedWindowIds.remove(closedWindowId);

    // Notify autotile handler for cleanup (tracking sets + autotile D-Bus)
    m_autotileHandler->onWindowClosed(closedWindowId, closedScreenId);

    // Remove the window's border item (parent WindowItem is being destroyed anyway,
    // but clean up our tracking hash to avoid stale entries).
    removeWindowBorder(closedWindowId);

    // Drop the effect-local app-id cache entry. closedWindowId is the bare
    // instance id under the new wire format, which is exactly the cache key.
    m_appIdByInstance.remove(closedWindowId);

    // Notify general daemon for cleanup
    notifyWindowClosed(w);

    // Clean up caches AFTER all consumers that call getWindowId(w).
    // The windowDeleted handler does final cleanup, but removing here
    // prevents re-insertion by any late calls.
    m_windowIdCache.remove(w);
    m_windowIdReverse.remove(closedWindowId);
    m_trackedScreenPerWindow.remove(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated
    notifyWindowActivated(w);

    // Recreate all borders so the active window gets the active color
    // and inactive windows get the inactive color.  A full recreate is
    // used instead of in-place setOutline() because the latter may not
    // trigger a scene-graph repaint in all KWin versions.
    updateAllBorders();
}

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);

        // When a window is moved to a different desktop (e.g., "Move to Desktop 2"),
        // treat it as removed from the current desktop's tiling. The normal desktop-
        // switch flow will pick it up when the user switches to the target desktop.
        if (window && !window->isOnCurrentDesktop() && !window->isOnAllDesktops()) {
            const QString windowId = getWindowId(window);
            const QString screenId = getWindowScreenId(window);
            if (m_autotileHandler->isAutotileScreen(screenId)) {
                // Save pre-autotile geometry before onWindowClosed clears it.
                // When the window is re-added on the target desktop, this preserved
                // geometry is used instead of the current (tiled) frame position.
                m_autotileHandler->savePreAutotileForDesktopMove(windowId, screenId);

                // Restore title bar before removing from tiling — onWindowClosed
                // only clears tracking, it doesn't call setNoBorder(false) since
                // it's also used for truly closing windows.
                if (m_autotileHandler->isBorderlessWindow(windowId)) {
                    KWin::Window* kw = window->window();
                    if (kw) {
                        kw->setNoBorder(false);
                    }
                }
                m_autotileHandler->onWindowClosed(windowId, screenId);
                removeWindowBorder(windowId);
                qCInfo(lcEffect) << "Window moved off current desktop, removed from autotile:" << windowId;
            }
        }
    });

    // Detect when a window moves between monitors (e.g., "Move to Screen Right").
    // KWin::Window::outputChanged fires once when the window's output property changes.
    // Transfer the window from the old screen's autotile state to the new screen's state,
    // and unsnap any snapped window that crosses screens.
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Track the window's screen ID so we can detect cross-screen moves for snapping windows
        // (not tracked by the autotile handler's m_notifiedWindowScreens).
        m_trackedScreenPerWindow[w] = getWindowScreenId(w);
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Delegate autotile handling (autotile→autotile, autotile→snapping, etc.)
            // This must run even during drag so the autotile engine removes the
            // window from the old screen's tiling state immediately.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-screen moves: notify the daemon which
            // decides whether to unsnap based on its own state. If the daemon just
            // assigned this window to the new screen (restore/resnap/snap assist),
            // the stored screen matches and no unsnap occurs. If the user moved
            // the window via "Move to Screen" shortcut, the stored screen differs
            // and the daemon unsnaps.
            // Skip during drag: the drag system owns snap state transitions
            // (float, unsnap, size restore, pre-tile cleanup) and handles them
            // in dragStopped() with richer context.
            // Skip when the old screen disappeared (monitor standby/disconnect):
            // KWin reassigns orphaned windows to remaining outputs, firing
            // outputChanged even though the window didn't actually move. The
            // ScreenChangeHandler will resnap windows after the debounce settles.
            // Also skip during an active screen geometry change (debounce in flight).
            bool oldScreenStillConnected = false;
            for (const auto* output : KWin::effects->screens()) {
                if (outputScreenId(output) == oldScreenId) {
                    oldScreenStillConnected = true;
                    break;
                }
            }
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId
                && !m_autotileHandler->isAutotileScreen(oldScreenId)
                && !m_autotileHandler->isAutotileScreen(newScreenId) && !m_dragTracker->isDragging()
                && oldScreenStillConnected && !m_screenChangeHandler->isScreenChangeInProgress()) {
                const QString windowId = getWindowId(safeW);
                DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                                           {windowId, newScreenId}, QStringLiteral("cross-screen move"));
            }
        });
        // Virtual screen boundary detection: KWin's outputChanged only fires when
        // the physical monitor changes. Moving a window between virtual screens on the
        // same physical monitor (e.g., A/vs:0 → A/vs:1) is invisible to outputChanged.
        // Detect these crossings via frameGeometryChanged, using the same trackedScreen
        // state as the outputChanged handler above.
        // (The autotile handler has its own detection in slotWindowFrameGeometryChanged;
        // this covers snapping-mode windows which autotile doesn't track.)
        //
        // VS crossing detection uses VirtualScreenId::isVirtualScreenCrossing()
        // (shared/virtualscreenid.h) — the same predicate used by autotilehandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_virtualScreensReady) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Skip during drag — the drag system owns state transitions.
            // Autotile drag handles VS transfers in dragStopped (line 262-285).
            // Snapping drag handles cross-screen unsnap in dragStopped via daemon.
            if (m_dragTracker->isDragging()) {
                return;
            }

            // Skip VS detection for autotile-tracked windows — the autotile
            // handler's slotWindowFrameGeometryChanged owns VS crossing for
            // windows it already tracks (m_notifiedWindows). Only untracked
            // windows (snapping-mode entering an autotile VS) need delegation.
            const QString windowId = getWindowId(safeW);
            if (m_autotileHandler->isTrackedWindow(windowId)) {
                return;
            }

            // Delegate autotile handling for untracked cross-VS transitions
            // (snapping→autotile). The autotile handler's own detection only
            // covers windows it already tracks.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-VS moves: notify the daemon
            if (!m_autotileHandler->isAutotileScreen(oldScreenId) && !m_autotileHandler->isAutotileScreen(newScreenId)
                && !m_screenChangeHandler->isScreenChangeInProgress()) {
                DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                                           {windowId, newScreenId}, QStringLiteral("virtual screen crossing"));
            }
        });

        // Clean up the tracked screen entry when the window is destroyed
        connect(safeW, &QObject::destroyed, this, [this, safeW]() {
            m_trackedScreenPerWindow.remove(safeW);
        });

        // Metadata mutations: KWin fires these when an app swaps its class or
        // desktop file after the surface is already mapped. Electron/CEF apps
        // (Emby, some Discord forks) do this mid-session and silently break any
        // daemon state keyed to the first-seen class. Push the latest metadata
        // to the WindowRegistry so consumers query the current value.
        //
        // Per feedback_class_change_exclusion.md: the registry only updates its
        // record. It does NOT retroactively unsnap, re-snap, or re-evaluate
        // rules — that would surprise users. Committed state stays committed.
        auto pushLatest = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW);
            }
        };
        connect(kw, &KWin::Window::windowClassChanged, this, pushLatest);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, pushLatest);
        connect(kw, &KWin::Window::captionChanged, this, pushLatest);
    }

    // Detect drag start/end via KWin's per-window signals instead of polling.
    // windowStartUserMovedResized fires once when an interactive move (or resize) begins;
    // windowFinishUserMovedResized fires once when it ends (button release, Escape, etc.).
    // This eliminates the poll timer that previously scanned the full stacking order at
    // 32ms intervals during drag — a significant source of compositor-thread overhead.
    //
    // NOTE: windowFrameGeometryChanged / windowStepUserMovedResized are intentionally NOT
    // connected for drag tracking. They fire on every pixel of movement, which would flood
    // D-Bus. Cursor position updates are handled event-driven via slotMouseChanged →
    // DragTracker::updateCursorPosition(), throttled to ~30Hz.
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowStartMoveResize(window);
    });
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowFinishMoveResize(window);
    });

    // Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMaximizedStateChanged);

    // Track when a monocle-maximized window goes fullscreen
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFullScreenChanged);

    // Autotile: center undersized Wayland windows as soon as they commit constrained size
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFrameGeometryChanged);

    // Frame-geometry shadow: push the latest geometry to the daemon so
    // daemon-local shortcut handlers (float toggle, etc.) can read fresh
    // geometry without round-tripping. Debounced at ~50ms per window via
    // m_frameGeometryFlushTimer so rapid move/resize sequences collapse
    // into at most one D-Bus push.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, w]() {
        if (!w || !shouldHandleWindow(w)) {
            return;
        }
        const QString windowId = getWindowId(w);
        if (windowId.isEmpty()) {
            return;
        }
        const QRect geo = w->frameGeometry().toRect();
        if (geo.width() <= 0 || geo.height() <= 0) {
            return;
        }
        m_pendingFrameGeometry[windowId] = geo;
        if (!m_frameGeometryFlushTimer->isActive()) {
            m_frameGeometryFlushTimer->start();
        }
    });

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMinimizedChanged);

    // Snap mode: track minimize/unminimize to float/unfloat snapped windows
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);
}

void PlasmaZonesEffect::slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                                         Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                                         Qt::KeyboardModifiers oldmodifiers)
{
    Q_UNUSED(oldpos)
    Q_UNUSED(oldmodifiers)

    const bool modifiersChanged = (m_currentModifiers != modifiers);
    const bool buttonsChanged = (oldbuttons != buttons);

    if (buttonsChanged && m_dragTracker->isDragging()) {
        qCInfo(lcEffect) << "mouseChanged buttons:" << static_cast<int>(oldbuttons) << "->"
                         << static_cast<int>(buttons);
    }

    if (modifiersChanged) {
        m_currentModifiers = modifiers;
        qCDebug(lcEffect) << "Modifiers changed to" << static_cast<int>(modifiers);
    }
    m_currentMouseButtons = buttons;

    if (m_dragTracker->isDragging()) {
        if ((oldbuttons & Qt::LeftButton) && !(buttons & Qt::LeftButton)) {
            // Primary button released = drag is over. Force-end regardless of whether
            // other buttons (e.g. right-click for zone activation) are still held.
            //
            // KWin keeps isUserMove() true while any button is held, so
            // windowFinishUserMovedResized wouldn't fire until ALL buttons are
            // released. forceEnd() gives immediate snap response on LMB release.
            //
            // After forceEnd, applySnapGeometry will defer (retry every 100 ms)
            // until isUserMove() clears when the remaining buttons are released.
            m_dragTracker->forceEnd(pos);
        } else if (modifiersChanged || buttonsChanged) {
            // Push modifier/button changes to daemon during drag immediately.
            // This includes activation button press/release — the daemon's
            // lazy snap-drag activation uses these modifiers to decide when
            // to promote a pending drag to active (first tick with trigger
            // held) and when to hide the overlay (trigger released).
            //
            // For bypass (autotile) drags, modifier changes must also flow
            // so the daemon's autotile drag-insert rising-edge detection
            // (hold and toggle modes) can fire without requiring cursor
            // motion. Without this, tapping the trigger while stationary
            // was silently dropped.
            //
            // The daemon's updateDragCursor is cheap for pending drags
            // (returns early without running dragMoved), so the rapid fire
            // of modifier-change events during a drag no longer causes the
            // overlay destroy/create churn that prompted discussion #310's
            // sibling regression.
            const bool bypassed =
                m_currentDragPolicy.bypassReason == QLatin1String("autotile_screen") || m_dragBypassedForAutotile;
            const bool shouldForward =
                bypassed || detectActivationAndGrab() || m_cachedZoneSelectorEnabled || !m_triggersLoaded;
            if (shouldForward) {
                DBusHelpers::fireAndForget(this, DBus::Interface::WindowDrag, QStringLiteral("updateDragCursor"),
                                           {m_dragTracker->draggedWindowId(), qRound(pos.x()), qRound(pos.y()),
                                            static_cast<int>(m_currentModifiers),
                                            static_cast<int>(m_currentMouseButtons)},
                                           QStringLiteral("updateDragCursor - modifier/button change"));
            }
        } else {
            // Position-only change: drive cursor tracking through DragTracker's
            // event-driven path. This eliminates QTimer jitter from the compositor
            // frame path — updates arrive at input-device cadence (throttled to
            // ~30Hz inside DragTracker to avoid D-Bus flooding).
            m_dragTracker->updateCursorPosition(pos);
        }
    }

    // Track which screen the cursor is on for shortcut screen detection.
    // Only send a D-Bus call when the cursor actually crosses to a different monitor
    // (or virtual screen), not on every pixel move. This gives the daemon accurate
    // cursor-based screen info on Wayland where QCursor::pos() is unreliable for
    // background processes.
    const QPoint roundedPos(qRound(pos.x()), qRound(pos.y()));
    auto* output = KWin::effects->screenAt(roundedPos);
    QString connectorName;
    QString effectiveScreenId;
    if (output) {
        connectorName = output->name();
        // Resolve to virtual screen ID if subdivisions exist
        effectiveScreenId = resolveEffectiveScreenId(roundedPos, output);
        if (effectiveScreenId != m_lastEffectiveScreenId) {
            m_lastEffectiveScreenId = effectiveScreenId;
            m_lastCursorOutput = connectorName;
            if (m_daemonServiceRegistered) {
                DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                                           {effectiveScreenId});
            }
        }
    }

    // Focus follows mouse: activate autotile window under cursor when not dragging.
    // Reuse effectiveScreenId computed above to avoid redundant resolveEffectiveScreenId call.
    if (!m_dragTracker->isDragging() && output) {
        m_autotileHandler->handleCursorMoved(pos, effectiveScreenId);
    }
}

void PlasmaZonesEffect::applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                                  const std::function<void()>& onComplete)
{
    PlasmaZones::applyStaggeredOrImmediate(this, count, m_cachedAnimationSequenceMode, m_cachedAnimationStaggerInterval,
                                           applyFn, onComplete);
}

void PlasmaZonesEffect::slotDaemonReady()
{
    if (m_daemonServiceRegistered) {
        return; // Already ready — idempotent guard
    }

    m_daemonServiceRegistered = true;
    qCInfo(lcEffect) << "daemon ready: re-pushing state";

    // All D-Bus calls use QDBusMessage::createMethodCall + asyncCall (no QDBusInterface)
    // to avoid synchronous D-Bus introspection that blocks the compositor thread.

    // Push KWin's output-order primary screen to the daemon so getPrimaryScreen()
    // reflects KDE Display Settings rather than QGuiApplication::primaryScreen().
    auto* ws = KWin::Workspace::self();
    if (ws) {
        const auto outputs = ws->outputOrder();
        if (!outputs.isEmpty()) {
            DBusHelpers::fireAndForget(this, DBus::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                                       {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
        }
    }

    // Re-push cursor screen — use the cached effective screen ID (which includes
    // virtual screen IDs like "A/vs:0") so the daemon's shortcut handler resolves
    // to the correct virtual screen, not the physical monitor.
    // m_lastEffectiveScreenId was set during the last processCursorPosition() call
    // via resolveEffectiveScreenId(), so it already has the correct virtual ID.
    if (!m_lastEffectiveScreenId.isEmpty()) {
        DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                                   {m_lastEffectiveScreenId}, QStringLiteral("cursorScreenChanged"));
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
        DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                                   {cursorScreenId}, QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen (physical fallback):" << cursorScreenId;
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
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QStringList> reply = *w;
            if (reply.isValid()) {
                m_navigationHandler->clearAllFloatingState();
                QStringList floatingIds = reply.value();
                for (const QString& id : floatingIds) {
                    m_navigationHandler->setWindowFloating(id, true);
                }
                qCDebug(lcEffect) << "Synced" << floatingIds.size() << "floating windows from daemon";
            }
        });
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

    // Re-announce all existing windows on autotile screens in one batch D-Bus
    // call instead of per-window windowOpened round-trips.
    const auto windows = KWin::effects->stackingOrder();
    m_autotileHandler->notifyWindowsAddedBatch(windows);

    // Report all live window IDs to the daemon so it can prune stale
    // entries from KConfig (windows that were snapped but no longer exist).
    {
        QStringList aliveWindowIds;
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w)) {
                aliveWindowIds.append(getWindowId(w));
            }
        }
        DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("pruneStaleWindows"),
                                   {QVariant::fromValue(aliveWindowIds)}, QStringLiteral("pruneStaleWindows"));
    }

    // Fetch pre-computed pending restore geometries so slotWindowAdded can
    // teleport windows to their zone immediately (no D-Bus round-trip flash).
    // Fire-and-forget: the cache is populated asynchronously. Windows that open
    // before the reply arrives fall back to the normal async restore path.
    {
        QDBusMessage geoMsg =
            QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                           QStringLiteral("getPendingRestoreGeometries"));
        auto* geoWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(geoMsg), this);
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
            m_snapRestoreCache.clear();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                QJsonObject geo = it.value().toObject();
                int x = geo[QLatin1String("x")].toInt();
                int y = geo[QLatin1String("y")].toInt();
                int w = geo[QLatin1String("width")].toInt();
                int h = geo[QLatin1String("height")].toInt();
                if (w > 0 && h > 0) {
                    m_snapRestoreCache.insert(it.key(), QRect(x, y, w, h));
                }
            }
            qCDebug(lcEffect) << "Cached" << m_snapRestoreCache.size() << "pending restore geometries";
        });
    }

    // Restore snap state for all untracked windows.
    // pendingRestoresAvailable may have fired BEFORE daemonReady, causing
    // slotPendingRestoresAvailable to bail out (m_daemonServiceRegistered was false).
    // Now that the daemon is confirmed ready, retry the restore flow using raw
    // QDBusMessage (no QDBusInterface) to avoid synchronous introspection.
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            // Guard: prevent slotPendingRestoresAvailable from double-processing
            // the same windows. Set inside the callback so that if this D-Bus call
            // fails, the flag stays false and slotPendingRestoresAvailable can
            // still function as a fallback.
            m_daemonReadyRestoresDone = true;

            QDBusPendingReply<QStringList> reply = *w;
            QSet<QString> trackedAppIds;
            if (reply.isValid()) {
                const QStringList trackedWindows = reply.value();
                for (const QString& windowId : trackedWindows) {
                    QString appId = WindowIdUtils::extractAppId(windowId);
                    if (!appId.isEmpty()) {
                        trackedAppIds.insert(appId);
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
                if (!window || !shouldHandleWindow(window)) {
                    continue;
                }
                if (window->isMinimized()) {
                    continue;
                }
                QString appId = WindowIdUtils::extractAppId(getWindowId(window));
                if (trackedAppIds.contains(appId)) {
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
                // applySnapGeometry, we know a moveResize happened.
                QRectF geoBefore = safeWindow->frameGeometry();

                callResolveWindowRestore(
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

QString PlasmaZonesEffect::getWindowId(KWin::EffectWindow* w) const
{
    // windowId IS the instance id. The daemon's runtime primary key is this
    // opaque, compositor-supplied string. It's stable for the window's
    // lifetime regardless of class mutations, so every map/set keyed by
    // windowId inside the daemon is immune to Electron/CEF apps swapping
    // their WM_CLASS after the surface is mapped.
    //
    // App class is looked up separately — via getWindowAppId() here in the
    // effect, and via WindowRegistry in the daemon. Both read the live
    // value rather than trusting a frozen first-seen string.
    //
    // Populates m_appIdByInstance as a side-effect so appIdForInstance()
    // can answer later D-Bus calls without walking the stacking order.
    if (!w) {
        return QString();
    }

    // Cache hit: window IDs never change during a window's lifetime
    auto cacheIt = m_windowIdCache.constFind(w);
    if (cacheIt != m_windowIdCache.constEnd()) {
        return cacheIt.value();
    }

    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    const QString instanceId = window->internalId().toString(QUuid::WithoutBraces);
    const QString appId = getWindowAppId(w);
    if (!appId.isEmpty()) {
        const_cast<PlasmaZonesEffect*>(this)->m_appIdByInstance.insert(instanceId, appId);
    }
    const QString result = appId + QLatin1Char('|') + instanceId;
    m_windowIdCache.insert(w, result);
    m_windowIdReverse.insert(result, const_cast<KWin::EffectWindow*>(w));
    return result;
}

QString PlasmaZonesEffect::getWindowInstanceId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    return window->internalId().toString(QUuid::WithoutBraces);
}

QString PlasmaZonesEffect::getWindowAppId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    // Prefer desktopFileName (stable cross-session identifier when available).
    QString appId = window->desktopFileName();
    if (appId.isEmpty()) {
        // Fallback: normalize windowClass
        //   X11: "resourceName resourceClass" → extract resourceClass
        //   Wayland: app_id as-is
        QString wc = w->windowClass();
        int spaceIdx = wc.indexOf(QLatin1Char(' '));
        appId = (spaceIdx > 0) ? wc.mid(spaceIdx + 1) : wc;
    }
    return appId.toLower();
}

void PlasmaZonesEffect::pushWindowMetadata(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const QString instanceId = getWindowInstanceId(w);
    if (instanceId.isEmpty()) {
        return;
    }

    const QString appId = getWindowAppId(w);
    KWin::Window* window = w->window();
    const QString desktopFile = window ? window->desktopFileName() : QString();
    const QString title = w->caption();

    // Fire-and-forget — the daemon side is idempotent.
    DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("setWindowMetadata"),
                               {instanceId, appId, desktopFile, title}, QStringLiteral("setWindowMetadata"));
}

void PlasmaZonesEffect::flushPendingFrameGeometry()
{
    if (m_pendingFrameGeometry.isEmpty()) {
        return;
    }
    // Move into a local so reentrancy from D-Bus (or later pushes) can't
    // disturb the iteration.
    const auto batch = std::exchange(m_pendingFrameGeometry, {});
    for (auto it = batch.constBegin(); it != batch.constEnd(); ++it) {
        const QRect& geo = it.value();
        DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("setFrameGeometry"),
                                   {it.key(), geo.x(), geo.y(), geo.width(), geo.height()},
                                   QStringLiteral("setFrameGeometry"));
    }
}

bool PlasmaZonesEffect::isPlasmaShellSurface(const QString& windowClass)
{
    // Substring match on "plasmashell" already subsumes "org.kde.plasmashell".
    // Listed classes are the layer-shell surfaces that leak into autotile
    // tracking on Wayland: notification containers, system tray popups, the
    // OSD, the emoji picker, and krunner. Case-insensitive because Wayland
    // appIds and X11 class names differ in casing conventions.
    return windowClass.contains(QLatin1String("plasmashell"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.plasma.emojier"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.plasma.notifications"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.krunner"), Qt::CaseInsensitive);
}

bool PlasmaZonesEffect::shouldHandleWindow(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    // Never snap our own overlay/editor windows (but allow the settings app)
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return false;
    }

    // Exclude XDG desktop portal windows (file dialogs, color pickers, etc.)
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return false;
    }

    // Plasma shell layer-shell surfaces — see isPlasmaShellSurface() for rationale.
    if (isPlasmaShellSurface(windowClass)) {
        return false;
    }

    // Check user-configured exclusion lists (needed for drag gating — daemon also enforces
    // for keyboard nav, but the effect must filter for drag operations and lifecycle reporting)
    if (!m_excludedApplications.isEmpty() || !m_excludedWindowClasses.isEmpty()) {
        KWin::Window* kw = w->window();
        const QString appName = kw ? kw->desktopFileName() : QString();
        for (const QString& excluded : m_excludedApplications) {
            if (!excluded.isEmpty() && appName.contains(excluded, Qt::CaseInsensitive)) {
                return false;
            }
        }
        for (const QString& excluded : m_excludedWindowClasses) {
            if (!excluded.isEmpty() && windowClass.contains(excluded, Qt::CaseInsensitive)) {
                return false;
            }
        }
    }

    // Skip special / non-manageable window types (inherently effect-side — KWin metadata)
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()) {
        return false;
    }

    // Skip transient/dialog windows unconditionally. Dialogs, utilities, tooltips,
    // notifications, etc. should never be zone-managed. User-configured exclusion
    // lists and minimum size checks are handled by the daemon.
    if (w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow()) {
        return false;
    }

    return true;
}

bool PlasmaZonesEffect::isTileableWindow(KWin::EffectWindow* w) const
{
    // Reject menus, popups, tooltips, modals, and transient children.
    // Electron apps (Vesktop, VS Code, Discord) create separate windows
    // for context menus and dropdowns that pass shouldHandleWindow() but
    // must never enter the autotile tree.
    if (!w->isNormalWindow() || w->isModal() || w->isPopupWindow() || w->isDropdownMenu() || w->isPopupMenu()
        || w->isTooltip() || w->isMenu() || w->transientFor()) {
        return false;
    }
    // Reject keep-above windows — overlay/utility tools (Spectacle, color
    // pickers, screen rulers, etc.) set keep-above and should not enter the
    // autotile tree or receive auto-focus. Without this guard, opening
    // Spectacle while focusNewWindows is enabled disrupts the tiled layout.
    if (w->keepAbove()) {
        return false;
    }
    return true;
}

// shouldAutoSnapWindow removed — equivalent to shouldHandleWindow + isTileableWindow.
// Call sites use isTileableWindow directly (stricter than shouldHandleWindow alone).

bool PlasmaZonesEffect::hasOtherWindowOfClassWithDifferentPid(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    QString windowClass = w->windowClass();
    pid_t windowPid = w->pid();

    // Check all existing windows for same class but different PID
    // This detects when another app (e.g., Cachy Update) spawns a window
    // of a class that the user has previously snapped (e.g., Ghostty)
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* other : windows) {
        if (other == w) {
            continue; // Skip self
        }
        if (!shouldHandleWindow(other)) {
            continue; // Skip non-managed windows
        }
        if (other->windowClass() == windowClass && other->pid() != windowPid) {
            // Found another window of the same class with different PID
            // This means the new window was likely spawned by a different app
            return true;
        }
    }

    return false;
}

bool PlasmaZonesEffect::isDaemonReady(const char* methodName) const
{
    if (!m_daemonServiceRegistered) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- daemon not ready";
        return false;
    }
    return true;
}

void PlasmaZonesEffect::syncFloatingWindowsFromDaemon()
{
    // Delegate to NavigationHandler
    m_navigationHandler->syncFloatingWindowsFromDaemon();
}

// Template implementation for loadSettingAsync — delegates to shared helper.
template<typename Fn>
void PlasmaZonesEffect::loadSettingAsync(const QString& name, Fn&& onValue)
{
    DBusHelpers::loadSettingAsync(this, name, std::forward<Fn>(onValue));
}

void PlasmaZonesEffect::loadCachedSettings()
{
    // Uses raw QDBusMessage (not QDBusInterface) to avoid synchronous introspection
    // that would block the compositor during login (see discussion #158).
    //
    // Transient exclusion and min-size are handled by the daemon. Exclusion lists are
    // cached here for drag-operation gating (shouldHandleWindow).
    m_triggersLoaded = false; // Permissive until new triggers arrive (#175)

    loadSettingAsync(QStringLiteral("excludedApplications"), [this](const QVariant& v) {
        m_excludedApplications = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("excludedWindowClasses"), [this](const QVariant& v) {
        m_excludedWindowClasses = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("minimumWindowWidth"), [this](const QVariant& v) {
        m_cachedMinWindowWidth = v.toInt();
    });
    loadSettingAsync(QStringLiteral("minimumWindowHeight"), [this](const QVariant& v) {
        m_cachedMinWindowHeight = v.toInt();
    });
    loadSettingAsync(QStringLiteral("snapAssistEnabled"), [this](const QVariant& v) {
        m_snapAssistHandler->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationsEnabled"), [this](const QVariant& v) {
        m_windowAnimator->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationDuration"), [this](const QVariant& v) {
        const int d = qBound(50, v.toInt(), 500);
        m_windowAnimator->setDuration(d);
        m_cachedAnimationDuration = d;
    });
    loadSettingAsync(QStringLiteral("animationEasingCurve"), [this](const QVariant& v) {
        m_windowAnimator->setEasingCurve(EasingCurve::fromString(v.toString()));
    });
    loadSettingAsync(QStringLiteral("animationMinDistance"), [this](const QVariant& v) {
        m_windowAnimator->setMinDistance(qBound(0, v.toInt(), 200));
    });
    loadSettingAsync(QStringLiteral("animationSequenceMode"), [this](const QVariant& v) {
        m_cachedAnimationSequenceMode = qBound(0, v.toInt(), 1);
    });
    loadSettingAsync(QStringLiteral("animationStaggerInterval"), [this](const QVariant& v) {
        m_cachedAnimationStaggerInterval = qBound(10, v.toInt(), 200);
    });
    loadSettingAsync(QStringLiteral("toggleActivation"), [this](const QVariant& v) {
        m_cachedToggleActivation = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragInsertToggle"), [this](const QVariant& v) {
        m_cachedAutotileDragInsertToggle = v.toBool();
    });
    loadSettingAsync(QStringLiteral("zoneSelectorEnabled"), [this](const QVariant& v) {
        m_cachedZoneSelectorEnabled = v.toBool();
    });

    // autotileHideTitleBars needs extra logic when toggled off — delegate to handler
    loadSettingAsync(QStringLiteral("autotileHideTitleBars"), [this](const QVariant& v) {
        m_autotileHandler->updateHideTitleBarsSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileShowBorder"), [this](const QVariant& v) {
        m_autotileHandler->updateShowBorderSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileBorderWidth"), [this](const QVariant& v) {
        int bw = qBound(0, v.toInt(), 10);
        if (m_autotileHandler->borderWidth() != bw) {
            m_autotileHandler->setBorderWidth(bw);
            // Invalidate pending stagger timers that would use the old border width
            m_autotileHandler->invalidateStaggerGeneration();
            DBusHelpers::fireAndForget(this, DBus::Interface::Autotile, QStringLiteral("retileAllScreens"), {},
                                       QStringLiteral("border width change retile"));
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderRadius"), [this](const QVariant& v) {
        int br = qBound(0, v.toInt(), 20);
        if (m_autotileHandler->borderRadius() != br) {
            m_autotileHandler->setBorderRadius(br);
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileInactiveBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setInactiveBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileFocusFollowsMouse"), [this](const QVariant& v) {
        m_autotileHandler->setFocusFollowsMouse(v.toBool());
    });

    // dragActivationTriggers — uses shared TriggerParser for QDBusArgument deserialization
    {
        DBusHelpers::loadSettingAsync(this, QStringLiteral("dragActivationTriggers"), [this](const QVariant& v) {
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
    if (anyLocalTriggerHeld() || m_cachedToggleActivation || m_cachedAutotileDragInsertToggle) {
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
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking, QStringLiteral("applyGeometryRequested"),
        this, SLOT(slotApplyGeometryRequested(QString, int, int, int, int, QString, QString, bool)));

    // Daemon-driven focus/cycle: daemon resolves target window and emits activateWindowRequested
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("activateWindowRequested"), this,
                                          SLOT(slotActivateWindowRequested(QString)));

    // Float toggle is entirely daemon-local: the daemon reads the active
    // window from its own shadow, calls toggleFloatForWindow internally, and
    // emits applyGeometryRequested to paint the outcome. The effect no longer
    // participates in the decision.

    // Daemon-driven batch operations (rotate, resnap emit applyGeometriesBatch)
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("applyGeometriesBatch"), this,
                                          SLOT(slotApplyGeometriesBatch(PlasmaZones::WindowGeometryList, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("raiseWindowsRequested"), this,
                                          SLOT(slotRaiseWindowsRequested(QStringList)));

    // Snap-all: daemon triggers effect to collect candidates
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("snapAllWindowsRequested"), this,
                                          SLOT(slotSnapAllWindowsRequested(QString)));

    // Move specific window (Snap Assist selection)
    QDBusConnection::sessionBus().connect(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
        QStringLiteral("moveSpecificWindowToZoneRequested"), this,
        SLOT(slotMoveSpecificWindowToZoneRequested(QString, QString, int, int, int, int)));

    // Pending restores on daemon startup
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("pendingRestoresAvailable"), this,
                                          SLOT(slotPendingRestoresAvailable()));

    // Screen geometry reapply
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("reapplyWindowGeometriesRequested"),
                                          m_screenChangeHandler.get(), SLOT(slotReapplyWindowGeometriesRequested()));

    // Floating state sync
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("windowFloatingChanged"), this,
                                          SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    // Settings: window picker for KCM exclusion list
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                          QStringLiteral("runningWindowsRequested"), this,
                                          SLOT(slotRunningWindowsRequested()));

    // WindowDrag: during-drag size restore
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                          QStringLiteral("restoreSizeDuringDragChanged"), this,
                                          SLOT(slotRestoreSizeDuringDrag(QString, int, int)));

    // WindowDrag: cross-VS policy flip. Daemon detects the cursor crossing
    // a virtual-screen boundary that changes autotile↔snap routing and
    // emits this signal so the effect can apply the transition locally
    // (handleDragToFloat, onWindowClosed, overlay cancel, etc.).
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                          QStringLiteral("dragPolicyChanged"), this,
                                          SLOT(slotDragPolicyChanged(QString, PlasmaZones::DragPolicy)));

    qCInfo(lcEffect) << "Connected to navigation D-Bus signals";
}

KWin::EffectWindow* PlasmaZonesEffect::getActiveWindow() const
{
    // Prefer KWin's active (focused) window when it is manageable and on current desktop
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    if (active && active->isOnCurrentActivity() && active->isOnCurrentDesktop() && !active->isMinimized()
        && shouldHandleWindow(active)) {
        return active;
    }
    // Fallback: topmost manageable window on current desktop (e.g. when activeWindow() is
    // null or refers to a dialog/utility we don't handle)
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (w && w->isOnCurrentActivity() && w->isOnCurrentDesktop() && !w->isMinimized() && shouldHandleWindow(w)) {
            return w;
        }
    }
    return nullptr;
}

QString PlasmaZonesEffect::outputScreenId(const KWin::LogicalOutput* output) const
{
    if (!output) {
        return QString();
    }
    const QString connectorName = output->name();

    // Cache: screen IDs are stable for the lifetime of an output. Caching avoids
    // repeated QGuiApplication::screens() iteration and sysfs reads (~30Hz during drag).
    // Invalidated on screen add/remove (m_screenIdCache cleared by screen change handler).
    auto it = m_screenIdCache.constFind(connectorName);
    if (it != m_screenIdCache.constEnd()) {
        return *it;
    }

    // Build a screen ID that exactly matches the daemon's Utils::screenIdentifier().
    // Uses shared ScreenIdUtils (compositor-common) for hex normalization and sysfs EDID
    // fallback, ensuring byte-identical output across daemon and compositor processes.
    //
    // Try QScreen::serialNumber() first (same source as daemon), then sysfs fallback.
    QString serialNumber;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            serialNumber = screen->serialNumber();
            break;
        }
    }

    const QString baseId =
        ScreenIdUtils::buildScreenBaseId(output->manufacturer(), output->model(), serialNumber, connectorName);

    // Disambiguate identical monitors: if another screen produces the same base ID,
    // append "/ConnectorName" to make each unique. Mirrors daemon's screenIdentifier().
    bool hasDuplicate = false;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() != connectorName
            && ScreenIdUtils::buildScreenBaseId(screen->manufacturer(), screen->model(), screen->serialNumber(),
                                                screen->name())
                == baseId) {
            hasDuplicate = true;
            break;
        }
    }

    QString result = hasDuplicate ? baseId + QLatin1Char('/') + connectorName : baseId;
    m_screenIdCache.insert(connectorName, result);
    return result;
}

QString PlasmaZonesEffect::getWindowScreenId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    const QPointF c = w->frameGeometry().center();
    return resolveEffectiveScreenId(QPoint(qRound(c.x()), qRound(c.y())), w->screen());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Screen Support
// ═══════════════════════════════════════════════════════════════════════════════

QString PlasmaZonesEffect::resolveEffectiveScreenId(const QPoint& pos, const KWin::LogicalOutput* output) const
{
    const QString physId = outputScreenId(output);
    if (physId.isEmpty()) {
        return physId;
    }

    // Check if this physical screen has virtual subdivisions
    auto it = m_virtualScreenDefs.constFind(physId);
    if (it == m_virtualScreenDefs.constEnd() || it->isEmpty()) {
        return physId; // No subdivisions, return physical ID
    }

    // Find which virtual screen contains the point.
    // Use exclusive-right/bottom semantics to match the daemon's containment check.
    // QRect::contains() uses inclusive-right, which causes boundary-pixel mismatches
    // between effect and daemon for abutting virtual screens.
    for (const auto& vs : *it) {
        const QRect& r = vs.geometry;
        if (pos.x() >= r.x() && pos.x() < r.x() + r.width() && pos.y() >= r.y() && pos.y() < r.y() + r.height()) {
            return vs.id;
        }
    }

    // Fallback: pick nearest virtual screen (covers rounding gaps)
    QString nearestVsId;
    int minDist = INT_MAX;
    for (const auto& vs : *it) {
        // Manhattan distance from point to nearest edge of the rect
        int dx = 0;
        int dy = 0;
        // Use exclusive-right/bottom (x + width, y + height) to match the
        // primary containment check above.  QRect::right()/bottom() return
        // inclusive values (x + width - 1), which would be off by 1px.
        const int exRight = vs.geometry.x() + vs.geometry.width();
        const int exBottom = vs.geometry.y() + vs.geometry.height();
        if (pos.x() < vs.geometry.left()) {
            dx = vs.geometry.left() - pos.x();
        } else if (pos.x() >= exRight) {
            dx = pos.x() - exRight;
        }
        if (pos.y() < vs.geometry.top()) {
            dy = vs.geometry.top() - pos.y();
        } else if (pos.y() >= exBottom) {
            dy = pos.y() - exBottom;
        }
        int dist = dx + dy;
        if (dist < minDist) {
            minDist = dist;
            nearestVsId = vs.id;
        }
    }
    if (!nearestVsId.isEmpty()) {
        return nearestVsId;
    }
    // Ultimate fallback (should never reach here)
    qCWarning(lcEffect) << "resolveEffectiveScreenId: no virtual screens found for" << physId;
    return physId;
}

void PlasmaZonesEffect::fetchVirtualScreenConfig(const QString& physicalScreenId, uint64_t generation)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Screen,
                                                      QStringLiteral("getVirtualScreenConfig"));
    msg << physicalScreenId;

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    QPointer<PlasmaZonesEffect> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [self, physicalScreenId, generation](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (!self)
                    return;
                // Helper lambda: decrement pending counter and fire deferred processing when all done.
                // Only participates in the startup gate if generation != 0 (issued by fetchAllVirtualScreenConfigs)
                // and the generation matches the current one (not stale from a prior fetch cycle).
                // Captures self by value (QPointer copy) to avoid dangling reference.
                auto countdownVsGate = [self, generation]() {
                    if (generation == 0 || !self || self->m_vsConfigGeneration != generation) {
                        return;
                    }
                    if (self->m_pendingVsConfigReplies > 0 && --self->m_pendingVsConfigReplies == 0) {
                        self->m_virtualScreensReady = true;
                        if (self->m_daemonServiceRegistered) {
                            self->processDaemonReadyWindowState();
                        }
                    }
                };

                QDBusPendingReply<QString> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << "fetchVirtualScreenConfig: no virtual screens for" << physicalScreenId
                                      << reply.error().message();
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                    countdownVsGate();
                    return;
                }

                const QString json = reply.value();
                QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
                if (!doc.isObject()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                    countdownVsGate();
                    return;
                }

                QJsonArray screens = doc.object().value(QLatin1String("screens")).toArray();

                // Look up the physical output geometry ONCE rather than per VS definition (O(N) vs O(N*M))
                QRect physGeom;
                const auto outputs = KWin::effects->screens();
                for (const auto* out : outputs) {
                    if (self->outputScreenId(out) == physicalScreenId) {
                        physGeom = out->geometry();
                        break;
                    }
                }

                if (!physGeom.isValid()) {
                    qCWarning(lcEffect) << "Physical output" << physicalScreenId
                                        << "not found (hot-unplug?) — skipping VS config update;"
                                        << "will re-fetch on reconnect";
                }

                QVector<EffectVirtualScreenDef> defs;
                for (const QJsonValue& val : screens) {
                    QJsonObject obj = val.toObject();
                    QJsonObject region = obj.value(QLatin1String("region")).toObject();

                    EffectVirtualScreenDef def;
                    def.id = obj.value(QLatin1String("id")).toString();

                    // Compute absolute geometry from fractional region within physical screen
                    if (physGeom.isValid()) {
                        qreal rx = region.value(QLatin1String("x")).toDouble();
                        qreal ry = region.value(QLatin1String("y")).toDouble();
                        qreal rw = region.value(QLatin1String("width")).toDouble();
                        qreal rh = region.value(QLatin1String("height")).toDouble();
                        // Edge-consistent rounding: compute edges then derive width/height
                        // to avoid 1px gaps between abutting virtual screens
                        int left = physGeom.x() + qRound(rx * physGeom.width());
                        int top = physGeom.y() + qRound(ry * physGeom.height());
                        int right = physGeom.x() + qRound((rx + rw) * physGeom.width());
                        int bottom = physGeom.y() + qRound((ry + rh) * physGeom.height());
                        def.geometry = QRect(left, top, right - left, bottom - top);
                    }

                    if (def.geometry.isValid() && !def.id.isEmpty()) {
                        defs.append(def);
                    }
                }

                if (defs.isEmpty()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                } else {
                    qCInfo(lcEffect) << "Loaded" << defs.size() << "virtual screens for" << physicalScreenId;
                    self->m_virtualScreenDefs.insert(physicalScreenId, defs);
                }

                // Re-resolve tracked screen IDs so stale virtual screen IDs
                // are replaced with IDs from the updated boundaries.
                for (auto it = self->m_trackedScreenPerWindow.begin(); it != self->m_trackedScreenPerWindow.end();
                     ++it) {
                    auto* window = it.key();
                    if (!window || window->isDeleted()) {
                        continue;
                    }
                    {
                        const QPointF cf = window->frameGeometry().center();
                        const QPoint center(qRound(cf.x()), qRound(cf.y()));
                        const QString newScreenId = self->resolveEffectiveScreenId(center, window->screen());
                        if (!newScreenId.isEmpty()) {
                            it.value() = newScreenId;
                            // Also update the autotile handler's notified screen map
                            // so slotWindowFrameGeometryChanged does not compare against
                            // the stale pre-config-change screen ID.
                            const QString windowId = self->getWindowId(window);
                            self->m_autotileHandler->updateNotifiedScreen(windowId, newScreenId);
                        }
                    }
                }

                countdownVsGate();

                // For live VS config changes (generation=0), re-enable VS crossing
                // detection now that boundary definitions are updated.
                // countdownVsGate skips for generation=0, so m_virtualScreensReady
                // must be restored here. For startup fetches (generation>0),
                // countdownVsGate already sets it when all screens are processed.
                if (generation == 0) {
                    self->m_virtualScreensReady = true;
                }
            });
}

void PlasmaZonesEffect::fetchAllVirtualScreenConfigs()
{
    const auto outputs = KWin::effects->screens();

    // Collect physical screen IDs in a single pass to avoid count/iterate race
    // (a screen removed between two loops would cause count and calls to diverge)
    QStringList physIds;
    for (const auto* output : outputs) {
        const QString physId = outputScreenId(output);
        if (!physId.isEmpty()) {
            physIds.append(physId);
        }
    }

    physIds.removeDuplicates();

    // Prune stale m_virtualScreenDefs entries for physical screens that are no
    // longer connected. Without this, resolveEffectiveScreenId could match against
    // geometry from a disconnected monitor.
    const QSet<QString> currentPhysIds(physIds.begin(), physIds.end());
    for (auto it = m_virtualScreenDefs.begin(); it != m_virtualScreenDefs.end();) {
        if (!currentPhysIds.contains(it.key()))
            it = m_virtualScreenDefs.erase(it);
        else
            ++it;
    }

    if (physIds.isEmpty()) {
        // No physical screens to query — gate opens immediately
        m_virtualScreensReady = true;
        m_pendingVsConfigReplies = 0;
        if (m_daemonServiceRegistered) {
            processDaemonReadyWindowState();
        }
        return;
    }

    // Bump generation so stale callbacks from prior fetches are ignored
    const uint64_t generation = ++m_vsConfigGeneration;
    m_pendingVsConfigReplies = physIds.size();
    m_virtualScreensReady = false;

    for (const QString& physId : physIds) {
        fetchVirtualScreenConfig(physId, generation);
    }
}

void PlasmaZonesEffect::onVirtualScreensChanged(const QString& physicalScreenId)
{
    qCInfo(lcEffect) << "Virtual screens changed for" << physicalScreenId;
    m_screenIdCache.clear();
    m_lastEffectiveScreenId.clear();
    // Temporarily disable VS-aware crossing detection while the async fetch is in-flight.
    // Without this, slotWindowFrameGeometryChanged uses stale boundary definitions from the
    // old config, potentially causing spurious VS crossing events during the D-Bus round-trip.
    m_virtualScreensReady = false;
    fetchVirtualScreenConfig(physicalScreenId); // generation=0, won't participate in startup gate
}

void PlasmaZonesEffect::emitNavigationFeedback(bool success, const QString& action, const QString& reason,
                                               const QString& sourceZoneId, const QString& targetZoneId,
                                               const QString& screenId)
{
    // Call D-Bus method on daemon to report navigation feedback (can't emit signals on another service's interface)
    if (!isDaemonReady("report navigation feedback")) {
        return;
    }
    DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("reportNavigationFeedback"),
                               {success, action, reason, sourceZoneId, targetZoneId, screenId});
}

void PlasmaZonesEffect::slotActivateWindowRequested(const QString& windowId)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (w) {
        KWin::effects->activateWindow(w);
    } else {
        qCDebug(lcEffect) << "slotActivateWindowRequested: window not found" << windowId;
    }
}

void PlasmaZonesEffect::slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x,
                                                              int y, int width, int height)
{
    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: invalid geometry" << geometry;
        return;
    }

    // Match by exact full window ID (appId|uuid) to distinguish
    // multiple windows of the same application. Fall back to appId only if
    // the exact match fails (e.g. window was recreated between candidate build
    // and selection).
    KWin::EffectWindow* targetWindow = nullptr;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && shouldHandleWindow(w) && getWindowId(w) == windowId) {
            targetWindow = w;
            break;
        }
    }
    if (!targetWindow) {
        QString appId = WindowIdUtils::extractAppId(windowId);
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w) && WindowIdUtils::extractAppId(getWindowId(w)) == appId) {
                targetWindow = w;
                break;
            }
        }
    }

    if (!targetWindow) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: window not found" << windowId;
        emitNavigationFeedback(false, QStringLiteral("snap_assist"), QStringLiteral("window_not_found"));
        return;
    }

    // Capture geometry BEFORE applySnapGeometry resizes the window. The async D-Bus
    // callback in ensurePreSnapGeometryStored would read frameGeometry() after the
    // resize, corrupting the pre-tile entry with zone dimensions.
    ensurePreSnapGeometryStored(targetWindow, getWindowId(targetWindow), targetWindow->frameGeometry());
    applySnapGeometry(targetWindow, geometry);

    // Derive screen from the applied geometry center. Use resolveEffectiveScreenId
    // to get the virtual screen ID (not just the physical output).
    QPoint geoCenter = geometry.center();
    const auto* output = KWin::effects->screenAt(geoCenter);
    QString screenId = output ? resolveEffectiveScreenId(geoCenter, output) : getWindowScreenId(targetWindow);

    if (isDaemonReady("snap assist windowSnapped")) {
        DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("windowSnapped"),
                                   {getWindowId(targetWindow), zoneId, screenId});
        DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("recordSnapIntent"),
                                   {getWindowId(targetWindow), true});

        // Snap Assist continuation: only for manual-mode screens.
        // Autotile screens manage their own window placement; showing snap assist
        // after an autotile resnap is incorrect (the daemon silently ignores the
        // selection anyway via the isAutotileScreen guard in signals.cpp).
        if (!m_autotileHandler->isAutotileScreen(screenId)) {
            m_snapAssistHandler->showContinuationIfNeeded(screenId);
        }
    }
}

// slotToggleWindowFloatRequested removed — the daemon now handles float-toggle
// locally against its active-window + frame-geometry shadow and emits
// applyGeometryRequested directly. See WindowTrackingAdaptor::toggleWindowFloat.

void PlasmaZonesEffect::slotApplyGeometryRequested(const QString& windowId, int x, int y, int width, int height,
                                                   const QString& zoneId, const QString& screenId, bool sizeOnly)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: window not found" << windowId;
        return;
    }

    // Check for size-only restore (drag-out unsnap without activation trigger).
    // The daemon sets sizeOnly=true to restore pre-snap width/height while keeping
    // the window at its current drop position.
    if (sizeOnly) {
        if (width > 0 && height > 0) {
            QRectF currentFrame = w->frameGeometry();
            QRect sizeOnlyGeo(qRound(currentFrame.x()), qRound(currentFrame.y()), width, height);
            qCInfo(lcEffect) << "slotApplyGeometryRequested: size-only restore for" << windowId << width << "x"
                             << height;
            applySnapGeometry(w, sizeOnlyGeo);
        }
        return;
    }

    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotApplyGeometryRequested: invalid geometry" << geometry;
        return;
    }
    // Skip float-restore geometry on minimized windows: when a snapped window is minimized
    // we float it (to free the zone slot), but applying the pre-tile geometry while minimized
    // would poison what KWin restores to on unminimize, causing a visible flash of the
    // pre-snap geometry before the unfloat re-snaps to the zone.
    if (w->isMinimized() && zoneId.isEmpty()) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: skipping float-restore geometry on minimized window:"
                          << windowId;
        return;
    }
    // Skip float-restore geometry for drag-to-float: when the user drags a window
    // off the autotile layout, the daemon restores pre-autotile geometry. But the
    // user expects the window to stay where they dropped it, not snap back.
    if (zoneId.isEmpty() && m_dragFloatedWindowIds.remove(windowId)) {
        qCInfo(lcEffect) << "slotApplyGeometryRequested: skipping float-restore for drag-floated window:" << windowId;
        return;
    }
    qCInfo(lcEffect) << "slotApplyGeometryRequested:" << windowId << "geo:" << geometry << "zoneId:" << zoneId
                     << "screen:" << screenId << "floating:" << isWindowFloating(windowId)
                     << "currentFrame:" << w->frameGeometry();
    // Store pre-snap geometry before first snap (idempotent — skips if already stored).
    // The daemon handles windowSnapped/recordSnapIntent internally, but only the effect
    // knows the window's current frame geometry for pre-tile storage.
    if (!zoneId.isEmpty()) {
        // Capture frame geometry synchronously BEFORE applySnapGeometry moves the window.
        // ensurePreSnapGeometryStored is async (D-Bus hasPreTileGeometry check) — without
        // pre-capturing, the callback would read the post-move geometry instead of the
        // original free-floating position.
        ensurePreSnapGeometryStored(w, getWindowId(w), w->frameGeometry());
    }

    applySnapGeometry(w, geometry);
    // Note: windowSnapped/recordSnapIntent are NOT called here. For daemon-driven
    // navigation, the daemon handles zone bookkeeping internally before emitting
    // applyGeometryRequested. For legacy callers (autotile float restore via
    // applyGeometryForFloat), zoneId is empty so no snap confirmation is needed.
}

void PlasmaZonesEffect::slotApplyGeometriesBatch(const WindowGeometryList& geometries, const QString& action)
{
    qCInfo(lcEffect) << "applyGeometriesBatch:" << action;

    if (geometries.isEmpty()) {
        return;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = buildWindowMap();

    struct PendingApply
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
    };
    QVector<PendingApply> pending;

    for (const auto& entry : geometries) {
        if (entry.windowId.isEmpty() || entry.width <= 0 || entry.height <= 0) {
            continue;
        }

        // Exact match first, appId fallback for single-instance apps
        KWin::EffectWindow* window = windowMap.value(entry.windowId);
        if (!window) {
            QString appId = WindowIdUtils::extractAppId(entry.windowId);
            KWin::EffectWindow* candidate = nullptr;
            int matchCount = 0;
            for (auto it = windowMap.constBegin(); it != windowMap.constEnd(); ++it) {
                if (WindowIdUtils::extractAppId(it.key()) == appId) {
                    candidate = it.value();
                    if (++matchCount > 1)
                        break;
                }
            }
            if (matchCount == 1) {
                window = candidate;
            }
        }

        if (!window) {
            continue;
        }

        PendingApply p;
        p.window = QPointer<KWin::EffectWindow>(window);
        p.geometry = entry.toRect();
        pending.append(p);
    }

    if (pending.isEmpty()) {
        return;
    }

    // Note: ensurePreSnapGeometryStored is NOT called here. Batch operations (rotate, resnap)
    // move windows between zones — their pre-tile geometry is already stored from the original
    // snap. The daemon's processBatchEntries calls clearPreTileGeometry only for __restore__
    // entries (overflow windows). Calling ensurePreSnapGeometryStored here would race with
    // the daemon's clearPreTileGeometry and store the zone geometry as pre-tile, corrupting
    // the restore path on subsequent mode transitions.

    // Capture stacking order before applying geometries (moveResize raises on Wayland)
    const auto allWindows = KWin::effects->stackingOrder();
    QVector<QPointer<KWin::EffectWindow>> savedStack;
    for (KWin::EffectWindow* w : allWindows) {
        savedStack.append(QPointer<KWin::EffectWindow>(w));
    }

    applyStaggeredOrImmediate(
        pending.size(),
        [this, pending](int i) {
            const auto& p = pending[i];
            if (p.window) {
                applySnapGeometry(p.window, p.geometry);
            }
        },
        [this, savedStack, action]() {
            // Restore z-order after all geometries applied
            auto* ws = KWin::Workspace::self();
            if (ws) {
                for (const auto& wPtr : savedStack) {
                    if (wPtr && !wPtr->isDeleted()) {
                        KWin::Window* kw = wPtr->window();
                        if (kw) {
                            ws->raiseWindow(kw);
                        }
                    }
                }
            }
            // Show snap assist after resnap if applicable
            if (action == QLatin1String("resnap") && m_snapAssistHandler->isEnabled()) {
                KWin::EffectWindow* activeWin = getActiveWindow();
                QString activeScreenId = activeWin ? getWindowScreenId(activeWin) : QString();
                if (!activeScreenId.isEmpty() && !m_autotileHandler->isAutotileScreen(activeScreenId)) {
                    m_snapAssistHandler->showContinuationIfNeeded(activeScreenId);
                }
            }
        });
}

void PlasmaZonesEffect::slotRaiseWindowsRequested(const QStringList& windowIds)
{
    auto* ws = KWin::Workspace::self();
    if (!ws) {
        return;
    }

    for (const QString& windowId : windowIds) {
        KWin::EffectWindow* w = findWindowById(windowId);
        if (w && !w->isDeleted()) {
            KWin::Window* kw = w->window();
            if (kw) {
                ws->raiseWindow(kw);
            }
        }
    }
}

void PlasmaZonesEffect::slotSnapAllWindowsRequested(const QString& screenId)
{
    qCInfo(lcEffect) << "Snap all windows requested for screen:" << screenId;

    if (!isDaemonReady("snap all windows")) {
        return;
    }

    // Async fetch all snapped windows to filter already-snapped ones locally
    QDBusPendingCall snapCall =
        DBusHelpers::asyncCall(DBus::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedAppIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedAppIds.insert(WindowIdUtils::extractAppId(id));
            }
        }

        // Collect unsnapped, non-floating windows on this screen in stacking order
        // (bottom-to-top) so lower windows get lower-numbered zones deterministically
        QStringList unsnappedWindowIds;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            if (!w || !shouldHandleWindow(w)) {
                continue;
            }

            QString windowId = getWindowId(w);
            QString appId = WindowIdUtils::extractAppId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

            // Always use EDID-based screen ID for comparison
            QString winScreen = getWindowScreenId(w);
            if (winScreen != screenId) {
                qCDebug(lcEffect) << "snap-all: skipping window on different screen" << appId;
                continue;
            }

            if (w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                qCDebug(lcEffect) << "snap-all: skipping minimized/other-desktop window" << appId;
                continue;
            }

            // Full ID match first (distinguishes multi-instance apps),
            // appId fallback for single-instance apps
            if (snappedFullIds.contains(windowId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window" << appId;
                continue;
            }
            if (!hasOtherWindowOfClassWithDifferentPid(w) && snappedAppIds.contains(appId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window (appId match)" << appId;
                continue;
            }

            unsnappedWindowIds.append(windowId);
        }

        qCDebug(lcEffect) << "snap-all: found" << unsnappedWindowIds.size() << "unsnapped windows to snap";

        if (unsnappedWindowIds.isEmpty()) {
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenId;
            emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"), QString(),
                                   QString(), screenId);
            return;
        }

        if (!isDaemonReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall =
            DBusHelpers::asyncCall(DBus::Interface::WindowTracking, QStringLiteral("calculateSnapAllWindows"),
                                   {QVariant::fromValue(unsnappedWindowIds), screenId});
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* cw) {
            cw->deleteLater();

            QDBusPendingReply<SnapAllResultList> calcReply = *cw;
            if (calcReply.isError()) {
                qCWarning(lcEffect) << "calculateSnapAllWindows failed:" << calcReply.error().message();
                emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("calculation_error"),
                                       QString(), QString(), screenId);
                return;
            }

            SnapAllResultList snapResults = calcReply.value();

            // Build WindowGeometryList for the batch geometry path
            WindowGeometryList snapGeometries;
            snapGeometries.reserve(snapResults.size());
            for (const auto& r : snapResults) {
                snapGeometries.append(r.toGeometryEntry());
            }
            slotApplyGeometriesBatch(snapGeometries, QStringLiteral("snap_all"));

            // Confirm snap assignments with daemon
            if (isDaemonReady("snap-all confirmation")) {
                SnapConfirmationList confirmEntries;
                for (const auto& r : snapResults) {
                    SnapConfirmationEntry entry;
                    entry.windowId = r.windowId;
                    entry.zoneId = r.targetZoneId;
                    entry.screenId = screenId;
                    entry.isRestore = false;
                    confirmEntries.append(entry);
                }
                if (!confirmEntries.isEmpty()) {
                    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath,
                                                                      DBus::Interface::WindowTracking,
                                                                      QStringLiteral("windowsSnappedBatch"));
                    msg << QVariant::fromValue(confirmEntries);
                    auto* batchWatcher =
                        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
                    connect(batchWatcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher* bw) {
                        if (bw->isError()) {
                            qCWarning(lcEffect) << "windowsSnappedBatch D-Bus call failed:" << bw->error().message();
                        }
                        bw->deleteLater();
                    });
                }
            }
        });
    });
}

void PlasmaZonesEffect::slotPendingRestoresAvailable()
{
    // If slotDaemonReady already dispatched snap restores for this daemon
    // session, skip — both signals fire during restart, and the second round
    // of moveResize() calls would disrupt the stacking order that the first
    // round carefully preserves via activateWindow(previouslyActive).
    if (m_daemonReadyRestoresDone) {
        qCInfo(lcEffect) << "Pending restores: already handled by slotDaemonReady, skipping";
        return;
    }

    qCInfo(lcEffect) << "Pending restores: retrying restoration for all visible windows";

    if (!isDaemonReady("pending restores")) {
        return;
    }

    // Use ASYNC batch call to get all tracked windows at once
    QDBusPendingCall pendingCall =
        DBusHelpers::asyncCall(DBus::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        QSet<QString> trackedAppIds;

        if (reply.isValid()) {
            // Extract app IDs from tracked windows for comparison
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                QString appId = WindowIdUtils::extractAppId(windowId);
                if (!appId.isEmpty()) {
                    trackedAppIds.insert(appId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedAppIds.size() << "tracked windows from daemon";
        } else {
            qCWarning(lcEffect) << "Failed to get tracked windows:" << reply.error().message();
            // Continue anyway - will try to restore all windows (daemon will handle duplicates)
        }

        // Now iterate through all visible windows and restore untracked ones
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* window : windows) {
            if (!window || !shouldHandleWindow(window)) {
                continue;
            }

            // Skip minimized or invisible windows
            if (window->isMinimized() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }

            // Check if this window is already tracked using local set lookup (O(1))
            QString windowId = getWindowId(window);
            QString appId = WindowIdUtils::extractAppId(windowId);
            if (trackedAppIds.contains(appId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window);
        }
    });
}

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    Q_UNUSED(screenId)
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    // Uses full windowId for per-instance tracking (appId fallback in isWindowFloating).
    qCInfo(lcEffect) << "Floating state changed for" << windowId << "- isFloating:" << isFloating;
    m_navigationHandler->setWindowFloating(windowId, isFloating);
    // When a window is unfloated (tiled/snapped), clear the drag-float skip flag.
    // Without this, a subsequent float toggle's geometry restore would be skipped
    // because m_dragFloatedWindowIds still has the entry from the original drag.
    if (!isFloating) {
        m_dragFloatedWindowIds.remove(windowId);
    }
}

void PlasmaZonesEffect::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w) || !isTileableWindow(w)) {
        return;
    }
    const QString windowId = getWindowId(w);
    const QString screenId = getWindowScreenId(w);

    // Autotile handler handles its own screens — only handle snap-mode here
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        return;
    }

    const bool minimized = w->isMinimized();

    if (minimized) {
        if (isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Snap: minimized already-floating window, skipping float:" << windowId;
            return;
        }
        m_minimizeFloatedWindows.insert(windowId);
    } else {
        if (!m_minimizeFloatedWindows.remove(windowId)) {
            qCDebug(lcEffect) << "Snap: unminimized window was not minimize-floated, skipping unfloat:" << windowId;
            return;
        }
    }

    qCInfo(lcEffect) << "Snap: window" << (minimized ? "minimized, floating:" : "unminimized, unfloating:") << windowId
                     << "on" << screenId;

    if (m_daemonServiceRegistered) {
        DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("setWindowFloatingForScreen"),
                                   {windowId, screenId, minimized}, QStringLiteral("setWindowFloatingForScreen"));
    }
}

void PlasmaZonesEffect::slotRunningWindowsRequested()
{
    qCInfo(lcEffect) << "Running windows requested by KCM";

    QJsonArray windowArray;
    QSet<QString> seenClasses;

    // Iterate in reverse (top-to-bottom) so deduplication keeps the topmost
    // window's caption per class, which is more useful to the user
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (!w) {
            continue;
        }

        // Include all normal, non-special windows (relaxed filter for the picker)
        if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isSkipSwitcher() || w->isNotification()
            || w->isOnScreenDisplay() || w->isPopupWindow()) {
            continue;
        }

        QString windowClass = w->windowClass();
        if (windowClass.isEmpty()) {
            continue;
        }

        // Normalize X11 "resourceName resourceClass" to just resourceClass,
        // matching the format used by getWindowId() for app rule matching.
        int spaceIdx = windowClass.indexOf(QLatin1Char(' '));
        if (spaceIdx > 0) {
            windowClass = windowClass.mid(spaceIdx + 1);
        }

        // Deduplicate by windowClass (first seen = topmost due to reverse iteration)
        if (seenClasses.contains(windowClass)) {
            continue;
        }
        seenClasses.insert(windowClass);

        QString appName = WindowIdUtils::deriveShortName(windowClass);
        if (appName.isEmpty()) {
            appName = windowClass;
        }

        QJsonObject obj;
        obj[QLatin1String("windowClass")] = windowClass;
        obj[QLatin1String("appName")] = appName;
        obj[QLatin1String("caption")] = w->caption();
        windowArray.append(obj);
    }

    QString jsonString = QString::fromUtf8(QJsonDocument(windowArray).toJson(QJsonDocument::Compact));
    qCDebug(lcEffect) << "Providing" << windowArray.size() << "running windows to daemon";

    // Send result back to daemon via D-Bus
    if (m_daemonServiceRegistered) {
        DBusHelpers::fireAndForget(this, DBus::Interface::Settings, QStringLiteral("provideRunningWindows"),
                                   {jsonString}, QStringLiteral("provideRunningWindows"));
    } else {
        qCWarning(lcEffect) << "provideRunningWindows: daemon not ready";
    }
}

bool PlasmaZonesEffect::borderActivated(KWin::ElectricBorder border)
{
    Q_UNUSED(border)
    // We no longer reserve edges, so this callback won't be triggered by our effect.
    // The daemon handles disabling Quick Tile via KWin config.
    return false;
}

void PlasmaZonesEffect::callResolveWindowRestore(KWin::EffectWindow* window, std::function<void()> onComplete)
{
    if (!window) {
        if (onComplete)
            onComplete();
        return;
    }

    if (!isDaemonReady("resolve window restore")) {
        if (onComplete)
            onComplete();
        return;
    }

    QString windowId = getWindowId(window);
    QString screenId = getWindowScreenId(window);
    bool sticky = isWindowSticky(window);

    QPointer<KWin::EffectWindow> safeWindow = window;

    // Single D-Bus call — daemon runs the full appRule → persisted → emptyZone → lastZone chain
    // skipAnimation=true: window is being restored to its snap position on startup/reopen,
    // so teleport directly instead of sliding from KWin's saved position.
    // storePreSnap=false: the window is already at its snap/zone position (from before
    // daemon restart or from KWin session restore), so its current frameGeometry is the
    // zone geometry — NOT the free-floating geometry. Storing it as pre-tile would cause
    // float toggle to restore to the zone geometry instead of the original free-floating position.
    tryAsyncSnapCall(DBus::Interface::WindowTracking, QStringLiteral("resolveWindowRestore"),
                     {windowId, screenId, sticky}, safeWindow, windowId, false, nullptr, nullptr,
                     /*skipAnimation=*/true, onComplete);
}

// The kwin-effect no longer calls the legacy dragStarted D-Bus method;
// beginDrag sets up snap-path state internally on the daemon side, so
// there's only one code path into the drag state machine.
bool PlasmaZonesEffect::isWindowSticky(KWin::EffectWindow* w) const
{
    return w && w->isOnAllDesktops();
}

void PlasmaZonesEffect::updateWindowStickyState(KWin::EffectWindow* w)
{
    if (!w || !m_daemonServiceRegistered) {
        return;
    }

    QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }

    bool sticky = isWindowSticky(w);
    // Use fire-and-forget instead of QDBusInterface to avoid synchronous D-Bus
    // introspection. slotWindowAdded → updateWindowStickyState fires for every
    // window during login; QDBusInterface creation blocks the compositor thread
    // for ~25s if the daemon hasn't entered app.exec() yet (daemonReady is
    // emitted before the event loop starts).
    DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("setWindowSticky"),
                               {windowId, sticky}, QStringLiteral("setWindowSticky"));
}

// The dragMoved lambda sends updateDragCursor directly via
// DBusHelpers::fireAndForget. Single entry point for hot-path cursor updates.

void PlasmaZonesEffect::callEndDrag(KWin::EffectWindow* window, const QString& windowId, bool cancelled)
{
    // Single entry point for drag-end dispatch.
    // Sends endDrag, receives a DragOutcome, and applies exactly the
    // action the daemon decided. Replaces callDragStopped (whose reply
    // shape was a 9-tuple of out-params) with a typed struct.
    QPointF cursorAtRelease = m_dragTracker->lastCursorPos();

    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("endDrag"));
    msg << windowId << static_cast<int>(cursorAtRelease.x()) << static_cast<int>(cursorAtRelease.y())
        << static_cast<int>(m_currentModifiers) << static_cast<int>(m_currentMouseButtons) << cancelled;
    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(msg);

    QPointer<KWin::EffectWindow> safeWindow = window;
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this, safeWindow, windowId](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<DragOutcome> reply = *w;
            if (reply.isError()) {
                qCWarning(lcEffect) << "endDrag call failed:" << reply.error().message();
                return;
            }
            const DragOutcome outcome = reply.value();
            qCInfo(lcEffect) << "endDrag outcome:" << windowId << "action=" << outcome.action
                             << "screen=" << outcome.targetScreenId << "geo=" << outcome.toRect()
                             << "snapAssist=" << outcome.requestSnapAssist;

            switch (outcome.action) {
            case DragOutcome::NoOp:
            case DragOutcome::CancelSnap:
            case DragOutcome::NotifyDragOutUnsnap:
                // Daemon handled any internal cleanup. Nothing for the
                // effect to paint.
                break;

            case DragOutcome::ApplyFloat: {
                // Autotile bypass drag ended — float the window at its
                // current screen. The plugin-side compositor work
                // (handleDragToFloat, setWindowFloatingForScreen) was
                // previously inlined in the dragStopped lambda; now it
                // fires here off the daemon's authoritative answer.
                //
                // Cross-VS transitions that happened mid-drag were
                // applied by slotDragPolicyChanged at the moment of
                // crossing, so by the time we get here the autotile
                // handler has the right tracking state.
                if (!safeWindow) {
                    break;
                }
                const QString dropScreenId = getWindowScreenId(safeWindow);
                if (dropScreenId.isEmpty()) {
                    break;
                }
                m_autotileHandler->handleDragToFloat(safeWindow, windowId, dropScreenId);
                m_dragFloatedWindowIds.insert(windowId);
                DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking,
                                           QStringLiteral("setWindowFloatingForScreen"), {windowId, dropScreenId, true},
                                           QStringLiteral("setWindowFloatingForScreen - endDrag ApplyFloat"));
                qCInfo(lcEffect) << "endDrag ApplyFloat:" << windowId << "on" << dropScreenId;
                break;
            }

            case DragOutcome::ApplySnap: {
                if (!safeWindow || safeWindow->isFullScreen()) {
                    break;
                }
                const QRect snapGeometry = outcome.toRect();
                // If the window is still in user-move state because only
                // the activation mouse button is held (LMB already
                // released), cancel KWin's interactive move so we can
                // snap immediately. Without this, applySnapGeometry
                // defers (100ms retry) until ALL buttons are released —
                // noticeable delay when using a mouse button (RMB) for
                // zone activation.
                if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                    if (KWin::Window* kw = safeWindow->window()) {
                        kw->cancelInteractiveMoveResize();
                    }
                }
                applySnapGeometry(safeWindow, snapGeometry);
                break;
            }

            case DragOutcome::RestoreSize: {
                if (!safeWindow || safeWindow->isFullScreen()) {
                    break;
                }
                // Drag-to-unsnap: apply pre-snap width/height at current
                // position. Skip if slotRestoreSizeDuringDrag already
                // applied during the drag (size within 1px).
                QRectF frame = safeWindow->frameGeometry();
                const QRect geo(static_cast<int>(frame.x()), static_cast<int>(frame.y()), outcome.width,
                                outcome.height);
                if (qAbs(frame.width() - outcome.width) <= 1 && qAbs(frame.height() - outcome.height) <= 1) {
                    qCDebug(lcEffect) << "endDrag RestoreSize: already at correct size, skipping";
                    break;
                }
                if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                    if (KWin::Window* kw = safeWindow->window()) {
                        kw->cancelInteractiveMoveResize();
                    }
                }
                applySnapGeometry(safeWindow, geo);
                break;
            }
            }

            // Auto-fill: if window was dropped without snapping to a
            // zone and wasn't floated, try the first empty zone on the
            // release screen. Daemon-provided targetScreenId wins over
            // window's current screen (cross-screen drags).
            const bool applied = outcome.action == DragOutcome::ApplySnap || outcome.action == DragOutcome::ApplyFloat;
            if (!applied && safeWindow && !outcome.targetScreenId.isEmpty() && isDaemonReady("auto-fill on drop")) {
                const bool sticky = isWindowSticky(safeWindow);
                auto onSnapSuccess = [this](const QString&, const QString& snappedScreenId) {
                    m_snapAssistHandler->showContinuationIfNeeded(snappedScreenId);
                };
                tryAsyncSnapCall(DBus::Interface::WindowTracking, QStringLiteral("snapToEmptyZone"),
                                 {windowId, outcome.targetScreenId, sticky}, safeWindow, windowId, true, nullptr,
                                 onSnapSuccess);
            }

            // Snap Assist: show the window picker if the daemon
            // requested it. asyncShow is non-blocking.
            if (outcome.requestSnapAssist && !outcome.emptyZones.isEmpty() && !outcome.targetScreenId.isEmpty()) {
                m_snapAssistHandler->asyncShow(windowId, outcome.targetScreenId, outcome.emptyZones);
            }
        });
}

void PlasmaZonesEffect::callCancelSnap()
{
    qCInfo(lcEffect) << "Calling cancelSnap (drag cancelled by Escape or external event)";
    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("cancelSnap"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PlasmaZonesEffect::tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                                         QPointer<KWin::EffectWindow> window, const QString& windowId,
                                         bool storePreSnap, std::function<void()> fallback,
                                         std::function<void(const QString&, const QString&)> onSnapSuccess,
                                         bool skipAnimation, std::function<void()> onComplete)
{
    QDBusPendingCall call = DBusHelpers::asyncCall(interface, method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args, skipAnimation,
             onComplete](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << method << "error:" << reply.error().message();
                    if (fallback)
                        fallback();
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (reply.argumentAt<4>() && window) {
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                              reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap)
                        ensurePreSnapGeometryStored(window, windowId, window ? window->frameGeometry() : QRectF());
                    applySnapGeometry(window, geo, false, skipAnimation);
                    // args[1] is screenId (e.g. for snapToEmptyZone, snapToLastZone)
                    if (onSnapSuccess && args.size() >= 2) {
                        onSnapSuccess(windowId, args[1].toString());
                    }
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (fallback)
                    fallback();
                if (onComplete)
                    onComplete();
            });
}

void PlasmaZonesEffect::repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo)
{
    window->addRepaintFull();
    if (oldFrame.isValid()) {
        KWin::effects->addRepaint(oldFrame.toAlignedRect());
    }
    KWin::effects->addRepaint(newGeo);
}

void PlasmaZonesEffect::applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag,
                                          bool skipAnimation)
{
    if (!window) {
        qCWarning(lcEffect) << "applyGeometry: window is null";
        return;
    }

    // Normalize so width/height are non-negative; reject invalid rects
    QRect geo = geometry.normalized();
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        qCWarning(lcEffect) << "applyGeometry: invalid or empty geometry:" << geometry;
        return;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546.
    if (window->isFullScreen()) {
        qCDebug(lcEffect) << "applyGeometry: window is fullscreen, skipping";
        return;
    }

    // For X11/XWayland windows, KWin constrains the frame size to align with
    // WM_SIZE_HINTS (size increments for terminals like Ghostty, Kitty, etc.).
    // Pre-compute the constrained size and center the window in its zone so the
    // gap is distributed evenly instead of all at the bottom-right.
    // This applies to all snap operations (zone snap, autotile, resnap, etc.).
    // Wayland-native clients negotiate size async (constrainFrameSize only
    // checks min/max, not char-cell grid), so they're handled by the deferred
    // check in slotWindowFrameGeometryChanged().
    if (window->isX11Client()) {
        KWin::Window* kw = window->window();
        if (kw) {
            const QSizeF constrained = kw->constrainFrameSize(QSizeF(geo.size()));
            const int cw = qRound(constrained.width());
            const int ch = qRound(constrained.height());
            if (cw < geo.width() || ch < geo.height()) {
                // Clamp to non-negative: if min-size exceeds the zone in one
                // dimension, don't shift the window beyond the zone's edge.
                const int dx = qMax(0, geo.width() - cw) / 2;
                const int dy = qMax(0, geo.height() - ch) / 2;
                geo = QRect(geo.x() + dx, geo.y() + dy, cw, ch);
                qCDebug(lcEffect) << "Pre-centered X11 window with size constraints:"
                                  << "zone=" << geometry.size() << "constrained=" << constrained << "adjusted=" << geo;
            }
        }
    }

    // Skip no-op: if window is already at the target geometry, calling
    // moveResize() is redundant and can have subtle stacking side effects
    // on some KWin versions (e.g. during daemon restart double-processing).
    if (QRectF(geo) == window->frameGeometry()) {
        qCDebug(lcEffect) << "moveResize: window already at target geometry, skipping:" << geo;
        return;
    }

    qCDebug(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geo;

    // Capture old frame before moveResize for repaint region
    const QRectF oldFrame = window->frameGeometry();

    // In KWin 6, we use the window's moveResize methods
    // When allowDuringDrag is false: defer if window is in user move/resize (snap on release)
    // When allowDuringDrag is true: apply immediately (snap-on-hover during drag)
    if (!allowDuringDrag && (window->isUserMove() || window->isUserResize())) {
        qCDebug(lcEffect) << "Window in user move/resize, deferring geometry via windowFinishUserMovedResized";
        QPointer<KWin::EffectWindow> safeWindow = window;
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(window, &KWin::EffectWindow::windowFinishUserMovedResized, this,
                        [this, safeWindow, geo, skipAnimation, conn](KWin::EffectWindow*) {
                            disconnect(*conn);
                            if (safeWindow && !safeWindow->isDeleted() && !safeWindow->isFullScreen()) {
                                applySnapGeometry(safeWindow, geo, false, skipAnimation);
                            }
                        });
        return;
    }

    // Animation: moveResize to the final geometry immediately, then morph
    // the window visually from its old position/size to the new one using
    // translate + scale in paintWindow(). This follows the standard KDE
    // effect pattern — effects are visual overlays, never per-frame moveResize.
    QPointF animStartPos;
    QSizeF animStartSize;
    if (!skipAnimation && !allowDuringDrag && m_windowAnimator->isEnabled()) {
        if (m_windowAnimator->hasAnimation(window)) {
            if (m_windowAnimator->isAnimatingToTarget(window, geo)) {
                return; // Already animating to this target
            }
            // Capture current visual state before changing anything (mid-flight redirect)
            animStartPos = m_windowAnimator->currentVisualPosition(window);
            animStartSize = m_windowAnimator->currentVisualSize(window);
            m_windowAnimator->removeAnimation(window);
        } else {
            animStartPos = oldFrame.topLeft();
            animStartSize = oldFrame.size();
        }

        // Apply final geometry immediately — client starts re-rendering at new size
        KWin::Window* kw = window->window();
        if (kw) {
            kw->moveResize(QRectF(geo));
        }

        // Start animation from old visual state to new geometry
        m_windowAnimator->startAnimation(window, animStartPos, animStartSize, geo);

        repaintSnapRegions(window, oldFrame, geo);
        return;
    }

    // No animation path (disabled, during drag, etc.): apply moveResize directly.
    if (m_windowAnimator->hasAnimation(window)) {
        m_windowAnimator->removeAnimation(window);
    }

    KWin::Window* kwinWindow = window->window();
    if (kwinWindow) {
        qCInfo(lcEffect) << "moveResize: QRect=" << geo << "-> QRectF=" << QRectF(geo);
        kwinWindow->moveResize(QRectF(geo));

        repaintSnapRegions(window, oldFrame, geo);
    } else {
        qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
    }
}

void PlasmaZonesEffect::slotRestoreSizeDuringDrag(const QString& windowId, int width, int height)
{
    // Restore pre-snap size when cursor leaves zone during drag. The window may have been
    // snapped when the drag started (at zone size); when the user drags out of all zones,
    // we restore to floated state immediately so they see the window return to original size.
    // This complements the release path (dragStopped) which also handles restore.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        return;
    }

    KWin::EffectWindow* window = m_dragTracker->draggedWindow();
    if (!window || !shouldHandleWindow(window)) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    // Restore-size-only: keep current position, apply pre-snap width/height
    QRectF frame = window->frameGeometry();
    QRect geometry(static_cast<int>(frame.x()), static_cast<int>(frame.y()), width, height);

    qCDebug(lcEffect) << "Restoring size during drag:" << windowId << geometry;
    applySnapGeometry(window, geometry, true);
}

void PlasmaZonesEffect::slotDragPolicyChanged(const QString& windowId, const DragPolicy& newPolicy)
{
    // Daemon-owned cross-VS flip. The daemon's updateDragCursor
    // handler computed policy at the current cursor position and found it
    // different from the policy in force — tell us so we can apply the
    // compositor-level transition. Replaces the effect-side cross-VS flip
    // loop in the dragMoved lambda that walked KWin::effects->screens()
    // with a stale m_autotileScreens cache.
    //
    // Guards: this slot only acts if we're actively tracking the drag for
    // this windowId. Stray signals (daemon restart, out-of-order delivery)
    // are ignored.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        qCDebug(lcEffect) << "slotDragPolicyChanged: drag no longer active for" << windowId;
        return;
    }

    const QString oldReason = m_currentDragPolicy.bypassReason;
    const QString newReason = newPolicy.bypassReason;
    if (oldReason == newReason) {
        // Same reason but different screenId (autotile→autotile cross-VS):
        // update the captured screen so endDrag's ApplyFloat uses the right one.
        m_currentDragPolicy = newPolicy;
        if (newReason == QLatin1String("autotile_screen")) {
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    qCInfo(lcEffect) << "slotDragPolicyChanged:" << windowId << oldReason << "->" << newReason
                     << "screen=" << newPolicy.screenId;

    m_currentDragPolicy = newPolicy;

    KWin::EffectWindow* dragW = m_dragTracker->draggedWindow();

    if (newReason == QLatin1String("autotile_screen")) {
        // Snap → autotile (or context-disabled → autotile). Cancel any
        // active snap overlay, enter bypass mode. Mirrors the old
        // effect-side flip block's "snap→autotile" branch, but driven by
        // daemon truth rather than an effect-cached screen set.
        if (!m_dragBypassedForAutotile) {
            callCancelSnap();
            m_dragBypassedForAutotile = true;
            m_dragBypassScreenId = newPolicy.screenId;
            m_dragStartedSent = false;
            m_pendingDragWindowId.clear();
            m_pendingDragGeometry = QRectF();
            m_snapDragStartScreenId.clear();
        } else {
            // Already in bypass but on a different autotile screen — just
            // update the captured screen id.
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    if (oldReason == QLatin1String("autotile_screen")) {
        // Autotile → snap (or autotile → context-disabled). Drop the
        // bypass flag and initialize snap-drag state as if the drag just
        // started on this snap screen. Remove the window from autotile
        // tracking so slotWindowFrameGeometryChanged doesn't fight the
        // snap geometry on subsequent geometry changes.
        //
        // Do NOT call handleDragToFloat here: the mid-drag schedule would
        // race against the zone snap at drop, making the window jump after
        // the user lets go. onWindowClosed alone clears the tracking state.
        if (dragW) {
            m_autotileHandler->onWindowClosed(windowId, m_dragBypassScreenId);
        }
        m_dragBypassedForAutotile = false;
        m_dragActivationDetected = false;
        m_dragStartedSent = false;
        m_pendingDragWindowId = windowId;
        m_pendingDragGeometry = dragW ? dragW->frameGeometry() : QRectF();
        m_snapDragStartScreenId = newPolicy.screenId;
        if (!m_keyboardGrabbed) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return;
    }

    // Other transitions (snap ↔ context_disabled / snapping_disabled):
    // no compositor-level work needed. The daemon will return a NoOp at
    // endDrag for disabled paths.
}

void PlasmaZonesEffect::notifyWindowClosed(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    const QString windowId = getWindowId(w);

    if (!isDaemonReady("notify windowClosed")) {
        return;
    }

    qCInfo(lcEffect) << "Notifying daemon: windowClosed" << windowId;
    DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("windowClosed"), {windowId});
}

void PlasmaZonesEffect::notifyWindowActivated(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    // Skip non-manageable window types but NOT user-excluded apps — the daemon
    // must always know which window is active so that keyboard shortcuts can
    // correctly skip excluded windows instead of operating on a stale
    // m_lastActiveWindowId.
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return;
    }
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return;
    }
    // Plasma shell surfaces — independent filter chain from shouldHandleWindow()
    // because notifyWindowActivated() intentionally skips user-exclusion lists
    // (the daemon still needs focus updates for excluded apps). The plasmashell
    // rejection must apply in both chains; see isPlasmaShellSurface().
    if (isPlasmaShellSurface(windowClass)) {
        return;
    }
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()
        || w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow()) {
        return;
    }

    if (!isDaemonReady("notify windowActivated")) {
        return;
    }

    QString windowId = getWindowId(w);
    QString screenId = getWindowScreenId(w);

    qCDebug(lcEffect) << "Notifying daemon: windowActivated" << windowId << "on screen" << screenId;
    DBusHelpers::fireAndForget(this, DBus::Interface::WindowTracking, QStringLiteral("windowActivated"),
                               {windowId, screenId});

    // Notify autotile engine of focus change so m_windowToScreen is updated
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        DBusHelpers::fireAndForget(this, DBus::Interface::Autotile, QStringLiteral("notifyWindowFocused"),
                                   {windowId, screenId}, QStringLiteral("notifyWindowFocused"));
    }
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    // O(1) exact match via reverse cache
    auto it = m_windowIdReverse.constFind(windowId);
    if (it != m_windowIdReverse.constEnd() && it.value() && !it.value()->isDeleted()) {
        return it.value();
    }

    // Fallback: appId-based fuzzy match (for cross-session restore where
    // the UUID portion changed but the appId is the same)
    const QString targetAppId = WindowIdUtils::extractAppId(windowId);
    KWin::EffectWindow* appMatch = nullptr;
    int matchCount = 0;

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (WindowIdUtils::extractAppId(wId) == targetAppId) {
            appMatch = w;
            ++matchCount;
        }
    }
    // Only return the fuzzy match if it's unambiguous — two Firefox windows
    // with different UUIDs would otherwise pick an arbitrary one and silently
    // misroute daemon requests.
    return matchCount == 1 ? appMatch : nullptr;
}

QVector<KWin::EffectWindow*> PlasmaZonesEffect::findAllWindowsById(const QString& windowId) const
{
    // Instance ids are unique — "all windows for a given id" is at most one
    // window. findAllWindowsById exists as an API seam for the (historical)
    // case where callers wanted every instance of an app class matching a
    // given composite; that semantic now lives on the daemon's
    // WindowRegistry::instancesWithAppId() + per-instance lookups. The
    // single-instance behavior here is the only case that remains.
    QVector<KWin::EffectWindow*> out;
    if (windowId.isEmpty()) {
        return out;
    }
    const QString targetAppId = WindowIdUtils::extractAppId(windowId);
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (wId == windowId) {
            // Exact match — discard any appId matches accumulated from earlier
            // windows in the stacking order. Without this clear, a second instance
            // of the same app (same appId) triggers the disambiguation path in
            // slotWindowsTileRequested, which can assign the wrong EffectWindow to
            // the tile entry — leaving the new window untiled.
            return {w};
        }
        if (WindowIdUtils::extractAppId(wId) == targetAppId) {
            out.append(w);
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-window borders (native OutlinedBorderItem)
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId)
{
    auto it = m_windowBorders.find(windowId);
    if (it == m_windowBorders.end()) {
        return;
    }
    WindowBorder& wb = it.value();
    if (wb.clippedSurface) {
        wb.clippedSurface->setBorderRadius(wb.savedSurfaceRadius);
    }
    // QPointer: item may already be null if Qt parent-child ownership destroyed it
    delete wb.item.data();
    QObject::disconnect(wb.geometryConnection);
    m_windowBorders.erase(it);
}

void PlasmaZonesEffect::clearAllBorders()
{
    while (!m_windowBorders.isEmpty()) {
        removeWindowBorder(m_windowBorders.begin().key());
    }
}

void PlasmaZonesEffect::updateWindowBorder(const QString& windowId, KWin::EffectWindow* w)
{
    // Remove existing border for this window first
    removeWindowBorder(windowId);

    const int bw = m_autotileHandler->borderWidth();
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    if (!m_autotileHandler->shouldShowBorderForWindow(windowId)) {
        return;
    }

    // Choose color: active for focused window, inactive for others
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    const bool isFocused = (w == active);
    const QColor bc = isFocused ? m_autotileHandler->borderColor() : m_autotileHandler->inactiveBorderColor();
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    // The OutlinedBorderItem draws the border OUTSIDE the innerRect, but the
    // parent WindowItem clips children to the window frame.  Inset the innerRect
    // by borderWidth so the border draws fully inside the frame (no clipping).
    const QRectF frame = w->frameGeometry();
    const KWin::RectF innerRect(bw, bw, frame.width() - 2.0 * bw, frame.height() - 2.0 * bw);
    const int br = m_autotileHandler->borderRadius();
    const KWin::BorderOutline outline(bw, bc, KWin::BorderRadius(br));

    KWin::WindowItem* windowItem = w->windowItem();
    if (!windowItem) {
        return;
    }

    WindowBorder wb;
    wb.item = new KWin::OutlinedBorderItem(innerRect, outline, windowItem);

    // For borderless windows, clip the SurfaceItem corners to match the border radius.
    if (br > 0 && m_autotileHandler->isBorderlessWindow(windowId)) {
        KWin::SurfaceItem* surface = windowItem->surfaceItem();
        if (surface) {
            wb.savedSurfaceRadius = surface->borderRadius();
            surface->setBorderRadius(KWin::BorderRadius(br));
            wb.clippedSurface = surface;
        }
    }

    // Keep the border in sync when the window resizes or moves.
    const QString wid = windowId; // capture by value
    wb.geometryConnection =
        connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
                [this, wid, bw](KWin::EffectWindow* ew, const QRectF& /*oldGeo*/) {
                    auto it = m_windowBorders.find(wid);
                    if (it != m_windowBorders.end() && it->item) {
                        const QRectF f = ew->frameGeometry();
                        it->item->setInnerRect(KWin::RectF(bw, bw, f.width() - 2.0 * bw, f.height() - 2.0 * bw));
                    }
                });

    m_windowBorders.insert(windowId, wb);
}

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    const int bw = m_autotileHandler->borderWidth();
    if (bw <= 0) {
        return;
    }

    // Iterate all effect windows and create borders for tiled ones
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted() || !w->isOnCurrentDesktop()) {
            continue;
        }
        const QString wid = getWindowId(w);
        if (m_autotileHandler->shouldShowBorderForWindow(wid)) {
            updateWindowBorder(wid, w);
        }
    }
}

void PlasmaZonesEffect::prePaintScreen(KWin::ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    // Update animation progress from presentTime and clean up completed ones
    m_windowAnimator->advanceAnimations(presentTime);

    if (m_windowAnimator->hasActiveAnimations()) {
        // Windows have translation transforms that move them outside their
        // frame geometry bounds — force full compositing mode.
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }

    KWin::effects->prePaintScreen(data, presentTime);
}

void PlasmaZonesEffect::postPaintScreen()
{
    // Schedule targeted repaints for active animations instead of full-screen
    m_windowAnimator->scheduleRepaints();
    KWin::effects->postPaintScreen();
}

void PlasmaZonesEffect::prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                                       std::chrono::milliseconds presentTime)
{
    if (w && m_windowAnimator->hasAnimation(w)) {
        // Mark as transformed so paintWindow applies our translate+scale
        data.setTransformed();
    }

    KWin::effects->prePaintWindow(view, w, data, presentTime);
}

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                    KWin::WindowPaintData& data)
{
    m_windowAnimator->applyTransform(w, data);
    KWin::effects->paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

} // namespace PlasmaZones

// KWin Effect Factory - creates the plugin
#include <effect/effect.h>

namespace KWin {

KWIN_EFFECT_FACTORY_SUPPORTED(PlasmaZones::PlasmaZonesEffect, "metadata.json",
                              return PlasmaZones::PlasmaZonesEffect::supported();)

} // namespace KWin

// MOC include - REQUIRED for the Q_OBJECT in KWIN_EFFECT_FACTORY_SUPPORTED
#include "plasmazoneseffect.moc"
