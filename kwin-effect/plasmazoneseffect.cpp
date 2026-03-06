// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <algorithm>
#include <QBuffer>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QtMath>
#include <QPixmap>
#include <QPointer>
#include <window.h>
#include <workspace.h>
#include <core/output.h> // For Output::name() for multi-monitor support

#include "autotilehandler.h"
#include "autotileborderrenderer.h"
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

// NavigateDirectivePrefix moved to navigationhandler.cpp to avoid redefinition

// ═══════════════════════════════════════════════════════════════════════════════
// Template helpers
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Template to ensure a D-Bus interface is initialized and valid
 * @tparam InterfacePtr unique_ptr<QDBusInterface> type
 * @param interface Reference to the interface pointer to ensure
 * @param interfaceName D-Bus interface name (e.g., DBus::Interface::WindowDrag)
 * @param logName Human-readable name for logging
 *
 * Replaces the duplicate ensure*Interface() methods with a single template.
 *
 * IMPORTANT: QDBusInterface's constructor performs synchronous D-Bus introspection,
 * blocking the calling thread until the target service responds. To prevent compositor
 * hangs during login, the serviceRegistered flag is kept false until the daemon emits
 * its daemonReady D-Bus signal (end of Daemon::start()), confirming it can process
 * messages. This ensures all calls during the startup window bail out here, avoiding
 * synchronous introspection while the daemon is still initializing.
 */
template<typename InterfacePtr>
static void ensureInterface(InterfacePtr& interface, const QString& interfaceName, const char* logName,
                            bool serviceRegistered)
{
    if (interface && interface->isValid()) {
        return;
    }

    // Fast pre-check: use the cached service registration state (updated via
    // QDBusServiceWatcher signals) instead of calling isServiceRegistered() which
    // is a synchronous D-Bus call that blocks the compositor thread.
    if (!serviceRegistered) {
        qCDebug(lcEffect) << "Skipping" << logName << "interface - service not registered";
        return;
    }

    interface = std::make_unique<QDBusInterface>(DBus::ServiceName, DBus::ObjectPath, interfaceName,
                                                 QDBusConnection::sessionBus());

    if (!interface->isValid()) {
        qCWarning(lcEffect) << "Cannot connect to" << logName << "interface -" << interface->lastError().message();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Method Implementations
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::fireAndForgetDBusCall(const QString& interface, const QString& method, const QVariantList& args,
                                              const QString& logContext)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, interface, method);
    for (const QVariant& arg : args) {
        msg << arg;
    }
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    const QString ctx = logContext.isEmpty() ? method : logContext;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [ctx](QDBusPendingCallWatcher* w) {
        if (w->isError()) {
            qCWarning(lcEffect) << ctx << "D-Bus call failed:" << w->error().message();
        }
        w->deleteLater();
    });
}

QString PlasmaZonesEffect::iconToDataUrl(const QIcon& icon, int size)
{
    if (icon.isNull()) {
        return QString();
    }
    QPixmap pix = icon.pixmap(size, size);
    if (pix.isNull()) {
        return QString();
    }
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    if (!pix.toImage().save(&buffer, "PNG")) {
        return QString();
    }
    return QStringLiteral("data:image/png;base64,") + QString::fromUtf8(ba.toBase64());
}

QRect PlasmaZonesEffect::parseZoneGeometry(const QString& json) const
{
    if (json.isEmpty()) {
        return QRect();
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcEffect) << "Failed to parse zone geometry:" << parseError.errorString();
        return QRect();
    }

    QJsonObject obj = doc.object();
    return QRect(obj[QLatin1String("x")].toInt(), obj[QLatin1String("y")].toInt(), obj[QLatin1String("width")].toInt(),
                 obj[QLatin1String("height")].toInt());
}

void PlasmaZonesEffect::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                                    const QRectF& preCapturedGeometry)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!ensureWindowTrackingReady("ensure pre-snap geometry")) {
        return;
    }

    QPointer<KWin::EffectWindow> safeWindow = w;
    QString capturedWindowId = windowId;
    QRectF capturedGeom = preCapturedGeometry;

    QDBusPendingCall pendingCall = m_windowTrackingInterface->asyncCall(QStringLiteral("hasPreSnapGeometry"), windowId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedGeom](QDBusPendingCallWatcher* watcher) {
                watcher->deleteLater();

                QDBusPendingReply<bool> reply = *watcher;
                bool hasGeometry = reply.isValid() && reply.value();

                if (!hasGeometry && m_windowTrackingInterface && m_windowTrackingInterface->isValid()) {
                    // Use pre-captured geometry if provided, otherwise read from window
                    QRectF geom =
                        capturedGeom.isValid() ? capturedGeom : (safeWindow ? safeWindow->frameGeometry() : QRectF());
                    if (geom.width() > 0 && geom.height() > 0) {
                        m_windowTrackingInterface->asyncCall(QStringLiteral("storePreSnapGeometry"), capturedWindowId,
                                                             static_cast<int>(geom.x()), static_cast<int>(geom.y()),
                                                             static_cast<int>(geom.width()),
                                                             static_cast<int>(geom.height()));
                        qCInfo(lcEffect) << "Stored pre-snap geometry for window" << capturedWindowId;
                    }
                }
            });
}

QHash<QString, KWin::EffectWindow*> PlasmaZonesEffect::buildWindowMap(bool filterHandleable) const
{
    QHash<QString, KWin::EffectWindow*> windowMap;
    const auto windows = KWin::effects->stackingOrder();
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
    , m_borderRenderer(std::make_unique<AutotileBorderRenderer>())
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
    , m_screenChangeHandler(std::make_unique<ScreenChangeHandler>(this))
    , m_snapAssistHandler(std::make_unique<SnapAssistHandler>(this))
    , m_windowAnimator(std::make_unique<WindowAnimator>(this))
    , m_dragTracker(std::make_unique<DragTracker>(this))
{
    // Connect DragTracker signals
    //
    // Performance optimization: keyboard grab and D-Bus dragMoved calls are deferred
    // until an activation trigger is detected. This eliminates 60Hz D-Bus traffic and
    // keyboard grab/ungrab overhead for non-zone window drags (discussion #167).
    connect(m_dragTracker.get(), &DragTracker::dragStarted, this,
            [this](KWin::EffectWindow* w, const QString& windowId, const QRectF& geometry) {
                qCDebug(lcEffect) << "Window move started -" << w->windowClass()
                                  << "current modifiers:" << static_cast<int>(m_currentModifiers);
                // On autotile screens, don't show manual zone overlay or grab keyboard.
                // The drag proceeds freely; floatWindow is called on drag end.
                // Capture this decision so dragStopped uses the same state — prevents
                // a race where m_autotileScreens changes mid-drag (async D-Bus signal)
                // and leaves the popup visible with no snap.
                if (m_autotileHandler->isAutotileScreen(getWindowScreenName(w))) {
                    m_dragBypassedForAutotile = true;
                    return;
                }
                m_dragBypassedForAutotile = false;
                m_dragActivationDetected = false;
                m_dragStartedSent = false;
                m_pendingDragWindowId = windowId;
                m_pendingDragGeometry = geometry;

                // Check if zones are needed right now. If so, send dragStarted
                // immediately and grab keyboard. Otherwise defer until activation
                // is detected mid-drag (or skip entirely if user never activates).
                //
                // When triggers haven't loaded yet (!m_triggersLoaded), stay
                // permissive — send immediately to avoid masking trigger issues (#175).
                if (detectActivationAndGrab() || m_cachedZoneSelectorEnabled || !m_triggersLoaded) {
                    sendDeferredDragStarted();
                }
                // Grab keyboard to intercept Escape before KWin's MoveResizeFilter.
                // Without this, Escape cancels the interactive move AND the overlay.
                // With the grab, Escape only dismisses the overlay while the drag continues.
                if (!m_keyboardGrabbed) {
                    KWin::effects->grabKeyboard(this);
                    m_keyboardGrabbed = true;
                }
            });
    connect(m_dragTracker.get(), &DragTracker::dragMoved, this,
            [this](const QString& windowId, const QPointF& cursorPos) {
                // Gate D-Bus calls: if no activation trigger is held, toggle mode is off,
                // and zone selector is disabled, skip the D-Bus call entirely. This
                // eliminates 60Hz D-Bus traffic during non-zone drags.
                //
                // When triggers haven't loaded yet, stay permissive (#175).
                if (!detectActivationAndGrab() && !m_cachedZoneSelectorEnabled && m_triggersLoaded) {
                    return;
                }
                // Ensure dragStarted was sent before any dragMoved
                sendDeferredDragStarted();
                callDragMoved(windowId, cursorPos, m_currentModifiers, static_cast<int>(m_currentMouseButtons));
            });
    connect(m_dragTracker.get(), &DragTracker::dragStopped, this,
            [this](KWin::EffectWindow* w, const QString& windowId, bool cancelled) {
                // Release keyboard grab before handling drag end
                if (m_keyboardGrabbed) {
                    KWin::effects->ungrabKeyboard();
                    m_keyboardGrabbed = false;
                }
                // Use the captured autotile state from drag start (not live m_autotileScreens)
                // to ensure consistent behavior even if autotile screens changed mid-drag.
                if (m_dragBypassedForAutotile) {
                    if (!cancelled) {
                        fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("floatWindow"), {windowId},
                                              QStringLiteral("floatWindow"));
                        qCInfo(lcEffect) << "Autotile drag-to-float:" << windowId;
                    }
                    return;
                }
                m_dragActivationDetected = false;

                if (!m_dragStartedSent) {
                    // Drag ended without ever activating zones — no D-Bus state to clean up
                    m_pendingDragWindowId.clear();
                    m_pendingDragGeometry = QRectF();
                    return;
                }
                m_dragStartedSent = false;
                m_pendingDragWindowId.clear();
                m_pendingDragGeometry = QRectF();

                if (cancelled) {
                    // Drag was cancelled externally (e.g. window went fullscreen).
                    // Tell the daemon to cancel rather than snap to the hovered zone.
                    callCancelSnap();
                } else {
                    callDragStopped(w, windowId);
                }
            });

    // Connect to window lifecycle signals
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &PlasmaZonesEffect::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &PlasmaZonesEffect::slotWindowClosed);

    // Belt-and-suspenders: windowClosed removes animations, but if a deferred
    // timer re-adds one between windowClosed and windowDeleted, the Item tree
    // will be torn down while an animation entry still references the window.
    // Purge here to prevent SIGSEGV in animationBounds → expandedGeometry.
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, [this](KWin::EffectWindow* w) {
        m_windowAnimator->removeAnimation(w);
    });

    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &PlasmaZonesEffect::slotWindowActivated);

    // Repaint border region when stacking order changes (popup/menu open/close,
    // window raise/lower) so the border's scissor clipping is recalculated
    // against the new set of windows above the active window.
    connect(KWin::effects, &KWin::EffectsHandler::stackingOrderChanged, this, [this]() {
        if (!m_lastBorderRect.isEmpty()) {
            KWin::effects->addRepaint(m_lastBorderRect);
        }
    });

    // mouseChanged is the only reliable way to get modifier state in a KWin effect on Wayland;
    // QGuiApplication::queryKeyboardModifiers() doesn't work since effects run in the compositor.
    connect(KWin::effects, &KWin::EffectsHandler::mouseChanged, this, &PlasmaZonesEffect::slotMouseChanged);

    // Connect to screen geometry changes for keepWindowsInZonesOnResolutionChange feature
    // In KWin 6, use virtualScreenGeometryChanged (not per-screen signal)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenGeometryChanged);

    // Connect to daemon's settingsChanged D-Bus signal
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                          QStringLiteral("settingsChanged"), this, SLOT(slotSettingsChanged()));
    qCInfo(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";

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
    // After a daemon restart, m_lastCursorScreenName is still valid in the effect
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

        // Restore borderless and monocle-maximized windows — daemon state is gone
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        m_lastBorderRect = QRect();

        KWin::effects->addRepaintFull();
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon service registered — waiting for daemonReady signal";

        // DO NOT set m_daemonServiceRegistered = true here.
        // The daemon registers its D-Bus service name in init(), BEFORE start()
        // runs heavy initialization and BEFORE the event loop begins. If we set
        // the flag now, window lifecycle events (slotWindowAdded → updateWindowStickyState,
        // slotWindowActivated, slotMouseChanged, etc.) would call ensureInterface()
        // which creates QDBusInterface synchronously — its constructor performs D-Bus
        // introspection that blocks until the daemon responds. Since the daemon can't
        // process messages yet, KWin freezes until D-Bus timeout (~25s).
        //
        // Instead, keep m_daemonServiceRegistered false until the daemon's own
        // daemonReady signal fires (end of Daemon::start()), confirming it can
        // handle D-Bus requests. slotDaemonReady() sets the flag and re-pushes state.

        // Reset stale D-Bus interfaces from the previous daemon instance.
        // Since m_daemonServiceRegistered remains false, ensureInterface() will
        // skip recreation, preventing synchronous introspection during startup.
        m_windowTrackingInterface.reset();
        m_overlayInterface.reset();
        m_settingsInterface.reset();

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

    // Seed m_lastCursorScreenName with the compositor's active screen. This ensures
    // the daemon has a valid cursor screen even if no mouse movement occurs after login.
    // slotMouseChanged will overwrite this as soon as the cursor moves.
    //
    // The actual D-Bus push to the daemon happens in slotDaemonReady(), which fires
    // either from the async Introspect callback above (daemon already running) or
    // from the daemonReady D-Bus signal (daemon starts later). We do NOT push here
    // to avoid synchronous QDBusInterface creation on the compositor thread.
    auto* initialScreen = KWin::effects->activeScreen();
    if (initialScreen) {
        m_lastCursorScreenName = initialScreen->name();
    }

    qCInfo(lcEffect) << "Initialized - C++ effect with D-Bus support and mouseChanged connection";
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    // Restore borderless and monocle-maximized windows so they recover properly.
    // Guard against compositor teardown — effects may outlive the stacking order.
    if (KWin::effects) {
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        m_lastBorderRect = QRect();
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
    // Note: PlasmaZones daemon requires Wayland with LayerShellQt
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
    return m_dragTracker->isDragging() || m_windowAnimator->hasActiveAnimations() || !m_lastBorderRect.isEmpty();
}

void PlasmaZonesEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (e->type() == QEvent::KeyPress && e->key() == Qt::Key_Escape && m_dragTracker->isDragging()) {
        // The keyboard grab ensures this runs before KWin's MoveResizeFilter,
        // so Escape never reaches the interactive move handler. The daemon
        // hides the overlay and sets snapCancelled; the drag continues as
        // a plain window move without zone snapping.
        qCInfo(lcEffect) << "Escape pressed during drag — dismissing overlay, continuing drag";
        callCancelSnap();
    }
    // All other keys are silently consumed by the grab. Modifier state is
    // unaffected because mouseChanged reads xkb state directly.
}

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // Sync floating state for this window from daemon
    // This ensures windows that were floating when closed remain floating when reopened
    // Use full windowId so daemon can do per-instance lookup with appId fallback
    QString windowId = getWindowId(w);
    m_navigationHandler->syncFloatingStateForWindow(windowId);

    // Notify autotile daemon about the new window
    m_autotileHandler->notifyWindowAdded(w);

    // Check if we should auto-snap new windows
    // Skip on autotile screens - the autotile engine handles window placement
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    if (!m_autotileHandler->isAutotileScreen(getWindowScreenName(w)) && shouldAutoSnapWindow(w) && !w->isMinimized()) {
        // Don't auto-snap if there's already another window of the same class
        // with a different PID. This prevents unwanted snapping when another app
        // spawns a window (e.g., Cachy Update spawning a Ghostty terminal).
        if (hasOtherWindowOfClassWithDifferentPid(w)) {
            qCDebug(lcEffect) << "Skipping auto-snap for" << w->windowClass()
                              << "- another window of same class exists with different PID";
            return;
        }

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

    // NOTE: Don't clear floating state here - it should persist across window close/reopen
    // The daemon preserves floating state (keyed by appId) so the window stays floating
    // when reopened. The effect's local cache will be synced in slotWindowAdded().

    m_windowAnimator->removeAnimation(w);

    const QString closedWindowId = getWindowId(w);
    const QString closedScreenName = getWindowScreenName(w);

    // Notify autotile handler for cleanup (tracking sets + autotile D-Bus)
    m_autotileHandler->onWindowClosed(closedWindowId, closedScreenName);

    // Notify general daemon for cleanup
    notifyWindowClosed(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated
    notifyWindowActivated(w);

    // Repaint so the active-only border moves with focus.
    // Only repaint when the old or new active window is borderless.
    const int bw = m_autotileHandler->borderWidth();
    if (bw > 0 && m_autotileHandler->borderColor().alpha() > 0) {
        const bool hadBorder = !m_lastBorderRect.isEmpty();
        const bool newBorderless = w && m_autotileHandler->isBorderlessWindow(getWindowId(w));
        if (hadBorder || newBorderless) {
            // When the new window is borderless but m_lastBorderRect was cleared
            // (e.g. focus returned from Spectacle/OSD), pre-set it so isActive()
            // returns true and KWin calls our paintScreen to draw the border.
            if (newBorderless && m_lastBorderRect.isEmpty()) {
                m_lastBorderRect = w->frameGeometry().toAlignedRect().adjusted(-bw, -bw, bw, bw);
            }
            KWin::effects->addRepaintFull();
        }
    }
}

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);
    });

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

    // C2: Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMaximizedStateChanged);

    // M1: Track when a monocle-maximized window goes fullscreen
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFullScreenChanged);

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMinimizedChanged);
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
            // This includes activation button press/release — the daemon shows/hides
            // the overlay based on whether the activation trigger is currently held,
            // matching keyboard modifier behavior (hold to show, release to hide,
            // re-press to show again).
            //
            // Skip on autotile screens — no zone overlay to update, and calling
            // detectActivationAndGrab() would wastefully grab the keyboard and
            // sendDeferredDragStarted() would send a D-Bus call the daemon can't use.
            //
            // Gating: same logic as dragMoved lambda — skip if no activation
            // detected and no reason to send (avoids D-Bus traffic for non-zone drags).
            if (!m_dragBypassedForAutotile) {
                // When triggers haven't loaded yet, stay permissive (#175).
                if (detectActivationAndGrab() || m_cachedZoneSelectorEnabled || !m_triggersLoaded) {
                    sendDeferredDragStarted();
                    callDragMoved(m_dragTracker->draggedWindowId(), pos, m_currentModifiers,
                                  static_cast<int>(m_currentMouseButtons));
                }
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
    // Only send a D-Bus call when the cursor actually crosses to a different monitor,
    // not on every pixel move. This gives the daemon accurate cursor-based screen info
    // on Wayland where QCursor::pos() is unreliable for background processes.
    auto* output = KWin::effects->screenAt(pos.toPoint());
    if (output) {
        QString screenName = output->name();
        if (screenName != m_lastCursorScreenName) {
            m_lastCursorScreenName = screenName;
            if (ensureWindowTrackingReady("report cursor screen")) {
                m_windowTrackingInterface->asyncCall(QStringLiteral("cursorScreenChanged"), screenName);
            }
        }
    }
}

void PlasmaZonesEffect::applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                                  const std::function<void()>& onComplete)
{
    if (count <= 0) {
        if (onComplete) {
            onComplete();
        }
        return;
    }

    const int staggerMs = m_cachedAnimationStaggerInterval;
    const bool stagger = (m_cachedAnimationSequenceMode == 1) && (count > 1) && (staggerMs > 0);

    if (stagger) {
        // Apply first item immediately (no event loop delay), schedule rest.
        // Stagger branch requires count > 1, so the loop always runs.
        applyFn(0);
        for (int i = 1; i < count; ++i) {
            const int delay = i * staggerMs;
            const bool isLast = (i == count - 1);
            QTimer::singleShot(delay, this, [applyFn, onComplete, i, isLast]() {
                applyFn(i);
                if (isLast && onComplete) {
                    onComplete();
                }
            });
        }
    } else {
        for (int i = 0; i < count; ++i) {
            applyFn(i);
        }
        if (onComplete) {
            onComplete();
        }
    }
}

void PlasmaZonesEffect::slotDaemonReady()
{
    if (m_daemonServiceRegistered) {
        return; // Already ready — idempotent guard
    }

    m_daemonServiceRegistered = true;
    qCInfo(lcEffect) << "Daemon ready — re-pushing state";

    // CRITICAL: Do NOT call ensureWindowTrackingReady() or any method that
    // creates QDBusInterface here. The daemonReady signal is emitted at the
    // end of Daemon::start(), BEFORE app.exec() starts the event loop
    // (main.cpp:94 vs 102). QDBusInterface's constructor performs synchronous
    // D-Bus Introspect — if the daemon can't process messages yet, KWin
    // blocks for ~25s (D-Bus timeout), freezing the compositor on login.
    //
    // Instead, use QDBusMessage::createMethodCall + asyncCall for all
    // immediate state pushes. QDBusInterface will be created lazily on the
    // first user-initiated action (window drag, activation, etc.), by which
    // time the daemon's event loop is guaranteed to be running.

    // Re-push cursor screen
    if (!m_lastCursorScreenName.isEmpty()) {
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                              {m_lastCursorScreenName}, QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen:" << m_lastCursorScreenName;
    }

    // Re-notify active window (gives daemon lastActiveScreenName)
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (activeWindow && shouldHandleWindow(activeWindow)) {
        QString windowId = getWindowId(activeWindow);
        QString screenName = getWindowScreenName(activeWindow);
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowActivated"),
                              {windowId, screenName}, QStringLiteral("windowActivated"));
        qCDebug(lcEffect) << "Re-notified active window:" << windowId << "on" << screenName;

        // Also notify autotile engine of focus
        if (m_autotileHandler->isAutotileScreen(screenName)) {
            fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("notifyWindowFocused"),
                                  {windowId, screenName}, QStringLiteral("notifyWindowFocused"));
        }
    }

    // Re-sync floating windows (async, no QDBusInterface needed)
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QStringList> reply = *w;
            if (reply.isValid()) {
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
    connectNavigationSignals();

    // Delegate autotile re-initialization to handler
    m_autotileHandler->onDaemonReady();

    // Re-announce all existing windows on autotile screens
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && shouldHandleWindow(w)) {
            m_autotileHandler->notifyWindowAdded(w);
        }
    }

    // Restore snap state for non-autotile windows.
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
            QDBusPendingReply<QStringList> reply = *w;
            QSet<QString> trackedAppIds;
            if (reply.isValid()) {
                const QStringList trackedWindows = reply.value();
                for (const QString& windowId : trackedWindows) {
                    QString appId = extractAppId(windowId);
                    if (!appId.isEmpty()) {
                        trackedAppIds.insert(appId);
                    }
                }
            }

            const auto allWindows = KWin::effects->stackingOrder();
            int restored = 0;
            for (KWin::EffectWindow* window : allWindows) {
                if (!window || !shouldHandleWindow(window)) {
                    continue;
                }
                if (window->isMinimized()) {
                    continue;
                }
                // Skip autotile screens — they're handled by notifyWindowAdded above
                if (m_autotileHandler->isAutotileScreen(getWindowScreenName(window))) {
                    continue;
                }
                QString windowId = getWindowId(window);
                QString appId = extractAppId(windowId);
                if (trackedAppIds.contains(appId)) {
                    continue;
                }
                callResolveWindowRestore(window);
                ++restored;
            }
            if (restored > 0) {
                qCInfo(lcEffect) << "Triggered snap restore for" << restored << "untracked windows after daemon ready";
            }
        });
    }
}

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCInfo(lcEffect) << "Daemon signaled settingsChanged - reloading settings";
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
    if (!w) {
        return QString();
    }

    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }

    // App identity: prefer desktopFileName (most stable cross-session identifier)
    QString appId = window->desktopFileName();
    if (appId.isEmpty()) {
        // Fallback: normalize windowClass
        // X11: "resourceName resourceClass" -> extract resourceClass
        // Wayland: app_id as-is
        QString wc = w->windowClass();
        int spaceIdx = wc.indexOf(QLatin1Char(' '));
        appId = (spaceIdx > 0) ? wc.mid(spaceIdx + 1) : wc;
    }
    appId = appId.toLower();

    // Instance identity: KWin's internal UUID (unique within KWin session)
    QString instanceId = window->internalId().toString(QUuid::WithoutBraces);

    return appId + QLatin1Char('|') + instanceId;
}

bool PlasmaZonesEffect::shouldHandleWindow(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    // Never snap our own windows (daemon overlays, Snap Assist, editor)
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazones"), Qt::CaseInsensitive)) {
        return false;
    }

    // Exclude XDG desktop portal windows (file dialogs, color pickers, etc.)
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return false;
    }

    // Skip special windows
    if (w->isSpecialWindow()) {
        return false;
    }
    if (w->isDesktop()) {
        return false;
    }
    if (w->isDock()) {
        return false;
    }
    if (w->isFullScreen()) {
        return false;
    }
    // Skip windows that skip the switcher (tooltips, popups, etc.)
    if (w->isSkipSwitcher()) {
        return false;
    }

    // Skip transient/dialog windows if setting is enabled
    // This excludes dialogs, utilities, tooltips, notifications, menus, etc.
    if (m_excludeTransientWindows) {
        if (w->isDialog()) {
            return false;
        }
        if (w->isUtility()) {
            return false;
        }
        if (w->isSplash()) {
            return false;
        }
        if (w->isNotification()) {
            return false;
        }
        if (w->isOnScreenDisplay()) {
            return false;
        }
        if (w->isModal()) {
            return false;
        }
        if (w->isPopupWindow()) {
            return false;
        }
    }

    // Skip windows smaller than minimum size (if size thresholds are enabled)
    if (m_minimumWindowWidth > 0 || m_minimumWindowHeight > 0) {
        QRectF geometry = w->frameGeometry();
        if (m_minimumWindowWidth > 0 && geometry.width() < m_minimumWindowWidth) {
            return false;
        }
        if (m_minimumWindowHeight > 0 && geometry.height() < m_minimumWindowHeight) {
            return false;
        }
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
    return true;
}

bool PlasmaZonesEffect::shouldAutoSnapWindow(KWin::EffectWindow* w) const
{
    // First apply basic filter
    if (!shouldHandleWindow(w)) {
        return false;
    }

    // Only auto-snap normal windows (main application windows)
    // This single check excludes all non-normal window types:
    // dialogs, utilities, splash screens, notifications, OSD, menus, tooltips, etc.
    // Window types are mutually exclusive in KWin.
    if (!w->isNormalWindow()) {
        return false;
    }

    // Modal check is NOT redundant - isModal() is a property, not a window type.
    // A normal window CAN be modal (e.g., file chooser that was incorrectly typed).
    if (w->isModal()) {
        return false;
    }

    // Popup check handles edge cases where popups might be classified as normal
    if (w->isPopupWindow()) {
        return false;
    }

    return true;
}

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

void PlasmaZonesEffect::ensureWindowTrackingInterface()
{
    ensureInterface(m_windowTrackingInterface, DBus::Interface::WindowTracking, "WindowTracking",
                    m_daemonServiceRegistered);
}

bool PlasmaZonesEffect::ensureWindowTrackingReady(const char* methodName)
{
    ensureWindowTrackingInterface();
    if (!m_windowTrackingInterface || !m_windowTrackingInterface->isValid()) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- WindowTracking interface not available";
        return false;
    }
    return true;
}

bool PlasmaZonesEffect::ensureOverlayInterface(const char* methodName)
{
    ensureInterface(m_overlayInterface, DBus::Interface::Overlay, "Overlay", m_daemonServiceRegistered);
    if (!m_overlayInterface || !m_overlayInterface->isValid()) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- Overlay interface not available";
        return false;
    }
    return true;
}

void PlasmaZonesEffect::syncFloatingWindowsFromDaemon()
{
    // Delegate to NavigationHandler
    m_navigationHandler->syncFloatingWindowsFromDaemon();
}

// Template implementation for loadSettingAsync — must be in .cpp since
// PlasmaZonesEffect is not a header-only class (all callers are in this TU).
template<typename Fn>
void PlasmaZonesEffect::loadSettingAsync(const QString& name, Fn&& onValue)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                                      QStringLiteral("getSetting"));
    msg << name;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [name, onValue](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            QVariant value = reply.value();
            if (value.canConvert<QDBusVariant>()) {
                value = value.value<QDBusVariant>().variant();
            }
            onValue(value);
            qCDebug(lcEffect) << "Loaded" << name;
        }
    });
}

void PlasmaZonesEffect::loadCachedSettings()
{
    // Set sensible defaults — updated asynchronously when daemon responds.
    // Uses raw QDBusMessage (not QDBusInterface) to avoid synchronous introspection
    // that would block the compositor during login (see discussion #158).
    m_excludeTransientWindows = true;
    m_minimumWindowWidth = 200;
    m_minimumWindowHeight = 150;
    m_triggersLoaded = false; // Permissive until new triggers arrive (#175)

    loadSettingAsync(QStringLiteral("excludeTransientWindows"), [this](const QVariant& v) {
        m_excludeTransientWindows = v.toBool();
    });
    loadSettingAsync(QStringLiteral("minimumWindowWidth"), [this](const QVariant& v) {
        m_minimumWindowWidth = v.toInt();
    });
    loadSettingAsync(QStringLiteral("minimumWindowHeight"), [this](const QVariant& v) {
        m_minimumWindowHeight = v.toInt();
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
    loadSettingAsync(QStringLiteral("zoneSelectorEnabled"), [this](const QVariant& v) {
        m_cachedZoneSelectorEnabled = v.toBool();
    });

    // autotileHideTitleBars needs extra logic when toggled off — delegate to handler
    loadSettingAsync(QStringLiteral("autotileHideTitleBars"), [this](const QVariant& v) {
        m_autotileHandler->updateHideTitleBarsSetting(v.toBool());
        if (!v.toBool()) {
            m_lastBorderRect = QRect();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderWidth"), [this](const QVariant& v) {
        int bw = qBound(0, v.toInt(), 10);
        if (m_autotileHandler->borderWidth() != bw) {
            m_autotileHandler->setBorderWidth(bw);
            if (bw == 0) {
                m_lastBorderRect = QRect();
            }
            // Invalidate pending stagger timers that would use the old border width
            m_autotileHandler->invalidateStaggerGeneration();
            fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("retileAllScreens"), {},
                                  QStringLiteral("border width change retile"));
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setBorderColor(QColor(v.toString()));
    });

    // dragActivationTriggers has complex QDBusArgument deserialization — keep as special case
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath,
                                                          DBus::Interface::Settings, QStringLiteral("getSetting"));
        msg << QStringLiteral("dragActivationTriggers");
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QVariant> reply = *w;
            if (!reply.isValid()) {
                qCWarning(lcEffect) << "Failed to load dragActivationTriggers — gating remains permissive";
                return;
            }
            QVariant triggerVariant = reply.value();
            if (triggerVariant.canConvert<QDBusVariant>()) {
                triggerVariant = triggerVariant.value<QDBusVariant>().variant();
            }

            // D-Bus may deliver QVariantList-of-QVariantMap as QDBusArgument (#175)
            QVariantList triggerList;
            if (triggerVariant.canConvert<QDBusArgument>()) {
                const QDBusArgument arg = triggerVariant.value<QDBusArgument>();
                arg.beginArray();
                while (!arg.atEnd()) {
                    QVariant element;
                    arg >> element;
                    triggerList.append(element);
                }
                arg.endArray();
            } else {
                triggerList = triggerVariant.toList();
            }
            m_cachedDragActivationTriggers = triggerList;

            // Pre-parse to POD structs (avoids QVariant unboxing at ~30Hz)
            m_parsedTriggers.clear();
            m_parsedTriggers.reserve(triggerList.size());
            for (const auto& t : std::as_const(triggerList)) {
                QVariantMap map;
                if (t.canConvert<QDBusArgument>()) {
                    const QDBusArgument elemArg = t.value<QDBusArgument>();
                    elemArg >> map;
                } else {
                    map = t.toMap();
                }
                ParsedTrigger pt;
                pt.modifier = map.value(QStringLiteral("modifier"), 0).toInt();
                pt.mouseButton = map.value(QStringLiteral("mouseButton"), 0).toInt();
                m_parsedTriggers.append(pt);
            }

            qCDebug(lcEffect) << "Loaded dragActivationTriggers:" << m_parsedTriggers.size() << "triggers";
            bool anyValid =
                std::any_of(m_parsedTriggers.cbegin(), m_parsedTriggers.cend(), [](const ParsedTrigger& pt) {
                    return pt.modifier != 0 || pt.mouseButton != 0;
                });
            if (!m_parsedTriggers.isEmpty() && !anyValid) {
                qCWarning(lcEffect) << "All triggers have modifier=0 mouseButton=0"
                                    << "— possible deserialization issue";
            }
            m_triggersLoaded = true;
        });
    }

    qCDebug(lcEffect) << "Loading cached settings asynchronously, using defaults until loaded";
}

bool PlasmaZonesEffect::checkLocalModifier(int modifierSetting, Qt::KeyboardModifiers mods)
{
    const bool shiftHeld = mods.testFlag(Qt::ShiftModifier);
    const bool ctrlHeld = mods.testFlag(Qt::ControlModifier);
    const bool altHeld = mods.testFlag(Qt::AltModifier);
    const bool metaHeld = mods.testFlag(Qt::MetaModifier);

    switch (modifierSetting) {
    case 0:
        return false; // Disabled
    case 1:
        return shiftHeld; // Shift
    case 2:
        return ctrlHeld; // Ctrl
    case 3:
        return altHeld; // Alt
    case 4:
        return metaHeld; // Meta
    case 5:
        return ctrlHeld && altHeld; // CtrlAlt
    case 6:
        return ctrlHeld && shiftHeld; // CtrlShift
    case 7:
        return altHeld && shiftHeld; // AltShift
    case 8:
        return true; // AlwaysActive
    case 9:
        return altHeld && metaHeld; // AltMeta
    case 10:
        return ctrlHeld && altHeld && metaHeld; // CtrlAltMeta
    default:
        return false;
    }
}

bool PlasmaZonesEffect::anyLocalTriggerHeld() const
{
    // Use pre-parsed triggers to avoid QVariant unboxing (~30x/sec during drag)
    for (const auto& t : m_parsedTriggers) {
        const bool modMatch = (t.modifier == 0) || checkLocalModifier(t.modifier, m_currentModifiers);
        const bool btnMatch = (t.mouseButton == 0) || (static_cast<int>(m_currentMouseButtons) & t.mouseButton) != 0;
        if (modMatch && btnMatch && (t.modifier != 0 || t.mouseButton != 0))
            return true;
    }
    return false;
}

bool PlasmaZonesEffect::detectActivationAndGrab()
{
    if (m_dragActivationDetected) {
        return true;
    }
    if (anyLocalTriggerHeld() || m_cachedToggleActivation) {
        m_dragActivationDetected = true;
        if (!m_keyboardGrabbed) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return true;
    }
    return false;
}

void PlasmaZonesEffect::sendDeferredDragStarted()
{
    if (m_dragStartedSent) {
        return;
    }
    m_dragStartedSent = true;
    callDragStarted(m_pendingDragWindowId, m_pendingDragGeometry);
}

void PlasmaZonesEffect::connectNavigationSignals()
{
    // Connect to WindowTracking navigation signals
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("moveWindowToZoneRequested"), this,
                                          SLOT(slotMoveWindowToZoneRequested(QString, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("focusWindowInZoneRequested"), this,
                                          SLOT(slotFocusWindowInZoneRequested(QString, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("restoreWindowRequested"), this,
                                          SLOT(slotRestoreWindowRequested()));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("toggleWindowFloatRequested"), this,
                                          SLOT(slotToggleWindowFloatRequested(bool)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("swapWindowsRequested"), this,
                                          SLOT(slotSwapWindowsRequested(QString, QString, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("rotateWindowsRequested"), this,
                                          SLOT(slotRotateWindowsRequested(bool, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("resnapToNewLayoutRequested"), this,
                                          SLOT(slotResnapToNewLayoutRequested(QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("cycleWindowsInZoneRequested"), this,
                                          SLOT(slotCycleWindowsInZoneRequested(QString, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("snapAllWindowsRequested"), this,
                                          SLOT(slotSnapAllWindowsRequested(QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("moveSpecificWindowToZoneRequested"), this,
                                          SLOT(slotMoveSpecificWindowToZoneRequested(QString, QString, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("pendingRestoresAvailable"), this,
                                          SLOT(slotPendingRestoresAvailable()));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("reapplyWindowGeometriesRequested"),
                                          m_screenChangeHandler.get(), SLOT(slotReapplyWindowGeometriesRequested()));

    // Connect to floating state changes to keep local cache in sync
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("windowFloatingChanged"), this,
                                          SLOT(slotWindowFloatingChanged(QString, bool)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("applyGeometryRequested"), this,
                                          SLOT(slotApplyGeometryRequested(QString, QString, QString, QString)));

    // Connect to Settings signal for window picker (KCM exclusion list helper)
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                          QStringLiteral("runningWindowsRequested"), this,
                                          SLOT(slotRunningWindowsRequested()));

    // Connect to WindowDrag signals for during-drag behavior
    // Note: zoneGeometryDuringDragChanged is emitted by daemon for overlay highlight; geometry is applied
    // only on release (dragStopped), not during drag, so the effect does not subscribe to it.
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                          QStringLiteral("restoreSizeDuringDragChanged"), this,
                                          SLOT(slotRestoreSizeDuringDrag(QString, int, int)));

    qCInfo(lcEffect) << "Connected to keyboard navigation D-Bus signals";
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

QString PlasmaZonesEffect::getWindowScreenName(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }

    // Get screen from EffectWindow - returns KWin::Output*
    auto* output = w->screen();
    if (output) {
        return output->name();
    }
    return QString();
}

void PlasmaZonesEffect::emitNavigationFeedback(bool success, const QString& action, const QString& reason,
                                               const QString& sourceZoneId, const QString& targetZoneId,
                                               const QString& screenName)
{
    // Call D-Bus method on daemon to report navigation feedback (can't emit signals on another service's interface)
    if (!ensureWindowTrackingReady("report navigation feedback")) {
        return;
    }
    m_windowTrackingInterface->asyncCall(QStringLiteral("reportNavigationFeedback"), success, action, reason,
                                         sourceZoneId, targetZoneId, screenName);
}

void PlasmaZonesEffect::slotMoveWindowToZoneRequested(const QString& targetZoneId, const QString& zoneGeometry)
{
    // Delegate to NavigationHandler
    m_navigationHandler->handleMoveWindowToZone(targetZoneId, zoneGeometry);
}

void PlasmaZonesEffect::slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId,
                                                              const QString& geometryJson)
{
    QRect geometry = parseZoneGeometry(geometryJson);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: invalid geometry" << geometryJson;
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
        QString appId = extractAppId(windowId);
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w) && extractAppId(getWindowId(w)) == appId) {
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

    ensurePreSnapGeometryStored(targetWindow, getWindowId(targetWindow));
    applySnapGeometry(targetWindow, geometry);

    QString screenName = getWindowScreenName(targetWindow);
    if (ensureWindowTrackingReady("snap assist windowSnapped")) {
        m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), getWindowId(targetWindow), zoneId,
                                             screenName);
        m_windowTrackingInterface->asyncCall(QStringLiteral("recordSnapIntent"), getWindowId(targetWindow), true);

        // Snap Assist continuation: if more empty zones and unsnapped windows remain, re-show
        m_snapAssistHandler->showContinuationIfNeeded(screenName);
    }
}

void PlasmaZonesEffect::slotFocusWindowInZoneRequested(const QString& targetZoneId, const QString& windowId)
{
    // Delegate to NavigationHandler
    m_navigationHandler->handleFocusWindowInZone(targetZoneId, windowId);
}

void PlasmaZonesEffect::slotRestoreWindowRequested()
{
    m_navigationHandler->handleRestoreWindow();
}

void PlasmaZonesEffect::slotToggleWindowFloatRequested(bool shouldFloat)
{
    Q_UNUSED(shouldFloat)
    KWin::EffectWindow* activeWindow = getValidActiveWindowOrFail(QStringLiteral("float"));
    if (!activeWindow) {
        return;
    }
    QString windowId = getWindowId(activeWindow);
    QString screenName = getWindowScreenName(activeWindow);

    // Route to the autotile engine if this window is autotile-managed
    if (m_autotileHandler->handleAutotileFloatToggle(activeWindow, windowId, screenName)) {
        return;
    }

    if (!ensureWindowTrackingReady("toggle float")) {
        return;
    }
    // Store the window's current geometry as pre-snap BEFORE the daemon processes the toggle.
    // If the daemon decides to unfloat (snap to zone), this ensures the floating position is
    // preserved as pre-snap for the next float toggle (float->unfloat->float cycle).
    // If the daemon decides to float instead, it clears pre-snap anyway, so this is harmless.
    // D-Bus calls on the same connection are processed in order, so storePreSnapGeometry
    // completes before toggleFloatForWindow.
    QRectF frameGeo = activeWindow->frameGeometry();
    m_windowTrackingInterface->asyncCall(QStringLiteral("storePreSnapGeometry"), windowId,
                                         static_cast<int>(frameGeo.x()), static_cast<int>(frameGeo.y()),
                                         static_cast<int>(frameGeo.width()), static_cast<int>(frameGeo.height()));
    m_windowTrackingInterface->asyncCall(QStringLiteral("toggleFloatForWindow"), windowId, screenName);
}

void PlasmaZonesEffect::slotApplyGeometryRequested(const QString& windowId, const QString& geometryJson,
                                                   const QString& zoneId, const QString& screenName)
{
    QRect geometry = parseZoneGeometry(geometryJson);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotApplyGeometryRequested: invalid geometry" << geometryJson;
        return;
    }
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: window not found" << windowId;
        return;
    }
    applySnapGeometry(w, geometry);
    if (!zoneId.isEmpty() && ensureWindowTrackingReady("apply geometry windowSnapped")) {
        m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), getWindowId(w), zoneId, screenName);
        m_windowTrackingInterface->asyncCall(QStringLiteral("recordSnapIntent"), getWindowId(w), true);
    }
}

void PlasmaZonesEffect::slotSwapWindowsRequested(const QString& targetZoneId, const QString& targetWindowId,
                                                 const QString& zoneGeometry)
{
    m_navigationHandler->handleSwapWindows(targetZoneId, targetWindowId, zoneGeometry);
}

void PlasmaZonesEffect::slotRotateWindowsRequested(bool clockwise, const QString& rotationData)
{
    m_navigationHandler->handleRotateWindows(clockwise, rotationData);
}

void PlasmaZonesEffect::slotResnapToNewLayoutRequested(const QString& resnapData)
{
    m_navigationHandler->handleResnapToNewLayout(resnapData);
}

void PlasmaZonesEffect::slotSnapAllWindowsRequested(const QString& screenName)
{
    qCInfo(lcEffect) << "Snap all windows requested for screen:" << screenName;

    if (!ensureWindowTrackingReady("snap all windows")) {
        return;
    }

    // Async fetch all snapped windows to filter already-snapped ones locally
    QDBusPendingCall snapCall = m_windowTrackingInterface->asyncCall(QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenName](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedAppIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedAppIds.insert(extractAppId(id));
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
            QString appId = extractAppId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

            if (getWindowScreenName(w) != screenName) {
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
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenName;
            emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"), QString(),
                                   QString(), screenName);
            return;
        }

        if (!ensureWindowTrackingReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall = m_windowTrackingInterface->asyncCall(QStringLiteral("calculateSnapAllWindows"),
                                                                         unsnappedWindowIds, screenName);
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenName](QDBusPendingCallWatcher* cw) {
            cw->deleteLater();

            QDBusPendingReply<QString> calcReply = *cw;
            if (calcReply.isError()) {
                qCWarning(lcEffect) << "calculateSnapAllWindows failed:" << calcReply.error().message();
                emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("calculation_error"),
                                       QString(), QString(), screenName);
                return;
            }

            QString snapData = calcReply.value();
            m_navigationHandler->handleSnapAllWindows(snapData, screenName);
        });
    });
}

void PlasmaZonesEffect::slotCycleWindowsInZoneRequested(const QString& directive, const QString& unused)
{
    m_navigationHandler->handleCycleWindowsInZone(directive, unused);
}

void PlasmaZonesEffect::slotPendingRestoresAvailable()
{
    qCInfo(lcEffect) << "Pending restores available - retrying restoration for all visible windows";

    if (!ensureWindowTrackingReady("pending restores")) {
        return;
    }

    // Use ASYNC batch call to get all tracked windows at once
    // This avoids N sync D-Bus calls (one per window) that could freeze compositor during startup
    QDBusPendingCall pendingCall = m_windowTrackingInterface->asyncCall(QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        QSet<QString> trackedAppIds;

        if (reply.isValid()) {
            // Extract app IDs from tracked windows for comparison
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                QString appId = extractAppId(windowId);
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
            QString appId = extractAppId(windowId);
            if (trackedAppIds.contains(appId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window);
        }
    });
}

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& windowId, bool isFloating)
{
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    // Uses full windowId for per-instance tracking (appId fallback in isWindowFloating).
    qCInfo(lcEffect) << "Floating state changed for" << windowId << "- isFloating:" << isFloating;
    m_navigationHandler->setWindowFloating(windowId, isFloating);
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

        // Deduplicate by windowClass (first seen = topmost due to reverse iteration)
        if (seenClasses.contains(windowClass)) {
            continue;
        }
        seenClasses.insert(windowClass);

        QString appName = deriveShortNameFromWindowClass(windowClass);
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
    ensureInterface(m_settingsInterface, DBus::Interface::Settings, "Settings", m_daemonServiceRegistered);
    if (m_settingsInterface && m_settingsInterface->isValid()) {
        m_settingsInterface->asyncCall(QStringLiteral("provideRunningWindows"), jsonString);
    } else {
        qCWarning(lcEffect) << "Cannot provide running windows - Settings interface not available";
    }
}

bool PlasmaZonesEffect::borderActivated(KWin::ElectricBorder border)
{
    Q_UNUSED(border)
    // We no longer reserve edges, so this callback won't be triggered by our effect.
    // The daemon handles disabling Quick Tile via KWin config.
    return false;
}

void PlasmaZonesEffect::callResolveWindowRestore(KWin::EffectWindow* window)
{
    if (!window) {
        return;
    }

    if (!ensureWindowTrackingReady("resolve window restore")) {
        return;
    }

    QString windowId = getWindowId(window);
    QString screenName = getWindowScreenName(window);
    bool sticky = isWindowSticky(window);

    QPointer<KWin::EffectWindow> safeWindow = window;

    // Single D-Bus call — daemon runs the full appRule → persisted → emptyZone → lastZone chain
    // skipAnimation=true: window is being restored to its snap position on startup/reopen,
    // so teleport directly instead of sliding from KWin's saved position.
    tryAsyncSnapCall(*m_windowTrackingInterface, QStringLiteral("resolveWindowRestore"), {windowId, screenName, sticky},
                     safeWindow, windowId, true, nullptr, nullptr, true);
}

void PlasmaZonesEffect::callDragStarted(const QString& windowId, const QRectF& geometry)
{
    updateWindowStickyState(m_dragTracker->draggedWindow());

    // Get window class info for exclusion filtering
    QString appName;
    QString windowClass;
    if (m_dragTracker->draggedWindow()) {
        windowClass = m_dragTracker->draggedWindow()->windowClass();
        appName = deriveShortNameFromWindowClass(windowClass);
        if (appName.isEmpty()) {
            appName = windowClass;
        }
    }

    // Use QDBusMessage::createMethodCall instead of QDBusInterface to avoid
    // synchronous D-Bus introspection. QDBusInterface's constructor blocks the
    // compositor thread (~25s timeout) if the daemon is registered but not yet
    // processing messages. QDBusMessage is purely local — no D-Bus communication
    // until asyncCall, which returns immediately.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("dragStarted"));
    msg << windowId << geometry.x() << geometry.y() << geometry.width() << geometry.height() << appName << windowClass
        << static_cast<int>(m_currentMouseButtons);
    QDBusConnection::sessionBus().asyncCall(msg);
}
bool PlasmaZonesEffect::isWindowSticky(KWin::EffectWindow* w) const
{
    return w && w->isOnAllDesktops();
}

void PlasmaZonesEffect::updateWindowStickyState(KWin::EffectWindow* w)
{
    if (!w || !ensureWindowTrackingReady("update sticky state")) {
        return;
    }

    QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }

    bool sticky = isWindowSticky(w);
    m_windowTrackingInterface->asyncCall(QStringLiteral("setWindowSticky"), windowId, sticky);
}

void PlasmaZonesEffect::callDragMoved(const QString& windowId, const QPointF& cursorPos, Qt::KeyboardModifiers mods,
                                      int mouseButtons)
{
    // Don't send manual zone drag updates when drag was started on an autotile screen.
    // Use captured flag (not live m_autotileScreens) for consistency with drag start/stop.
    if (m_dragBypassedForAutotile) {
        return;
    }

    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    // See callDragStarted() comment for rationale.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("dragMoved"));
    msg << windowId << static_cast<int>(cursorPos.x()) << static_cast<int>(cursorPos.y()) << static_cast<int>(mods)
        << mouseButtons;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PlasmaZonesEffect::callDragStopped(KWin::EffectWindow* window, const QString& windowId)
{
    // Cursor position at release (from last poll during drag) - daemon uses this for release screen
    QPointF cursorAtRelease = m_dragTracker->lastCursorPos();

    // Modifiers: m_currentModifiers is updated by slotMouseChanged. When drag ends via forceEnd (LMB
    // release), modifiers reflect the state at that moment. When drag ends via poll (isUserMove went
    // false), we use the last slotMouseChanged state; modifier released just before mouse may be stale.
    // This is acceptable for Snap Assist triggers - best-effort detection.

    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    // See callDragStarted() comment for rationale.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("dragStopped"));
    msg << windowId << static_cast<int>(cursorAtRelease.x()) << static_cast<int>(cursorAtRelease.y())
        << static_cast<int>(m_currentModifiers) << static_cast<int>(m_currentMouseButtons);
    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(msg);

    // Use QPointer to safely handle window destruction during async call
    QPointer<KWin::EffectWindow> safeWindow = window;

    // Create watcher to handle the reply
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                QDBusPendingReply<int, int, int, int, bool, QString, bool, bool, QString> reply = *w;
                if (reply.isError()) {
                    qCWarning(lcEffect) << "dragStopped call failed:" << reply.error().message();
                    return;
                }

                int snapX = reply.argumentAt<0>();
                int snapY = reply.argumentAt<1>();
                int snapWidth = reply.argumentAt<2>();
                int snapHeight = reply.argumentAt<3>();
                bool shouldSnap = reply.argumentAt<4>();
                QString releaseScreenName = reply.argumentAt<5>();
                bool restoreSizeOnly = reply.argumentAt<6>();
                bool snapAssistRequested = reply.argumentAt<7>();
                QString emptyZonesJson = reply.argumentAt<8>();

                qCInfo(lcEffect) << "dragStopped returned shouldSnap=" << shouldSnap
                                 << "releaseScreen=" << releaseScreenName << "restoreSizeOnly=" << restoreSizeOnly
                                 << "geometry=" << QRect(snapX, snapY, snapWidth, snapHeight);

                if (shouldSnap && safeWindow) {
                    // Final fullscreen check before applying geometry - window could have
                    // transitioned to fullscreen between drag stop and this point
                    if (safeWindow->isFullScreen()) {
                        qCDebug(lcEffect) << "Window is fullscreen at drag stop, skipping snap";
                    } else {
                        QRect snapGeometry;
                        bool shouldApply = true;
                        if (restoreSizeOnly) {
                            // Drag-to-unsnap: apply only pre-snap width/height, keep current position
                            QRectF frame = safeWindow->frameGeometry();
                            snapGeometry =
                                QRect(static_cast<int>(frame.x()), static_cast<int>(frame.y()), snapWidth, snapHeight);
                            // Skip if already restored during drag (slotRestoreSizeDuringDrag) to avoid redundant
                            // moveResize
                            if (qAbs(frame.width() - snapWidth) <= 1 && qAbs(frame.height() - snapHeight) <= 1) {
                                shouldApply = false;
                                qCDebug(lcEffect)
                                    << "Skip restore apply - already at correct size from during-drag restore";
                            }
                        } else {
                            snapGeometry = QRect(snapX, snapY, snapWidth, snapHeight);
                        }
                        if (shouldApply) {
                            // If the window is still in user-move state because only the
                            // activation mouse button is held (LMB already released),
                            // cancel KWin's interactive move so we can snap immediately.
                            // Without this, applySnapGeometry defers (100 ms retry) until
                            // ALL buttons are released, causing a noticeable delay when
                            // using a mouse button (e.g. RMB) for zone activation.
                            if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                                KWin::Window* kw = safeWindow->window();
                                if (kw) {
                                    qCDebug(lcEffect) << "Cancelling interactive move"
                                                         " (activation button held, LMB released)";
                                    kw->cancelInteractiveMoveResize();
                                }
                            }
                            applySnapGeometry(safeWindow, snapGeometry);
                        }
                    }
                }

                // Auto-fill: if window was dropped without snapping to a zone, try snapping to
                // the first empty zone on the release screen (where the user released the drag).
                // Use daemon-provided releaseScreenName (cursor position), not window's current
                // screen - after cross-screen drag the window may still report the old screen.
                if (!shouldSnap && safeWindow && !releaseScreenName.isEmpty()
                    && ensureWindowTrackingReady("auto-fill on drop")) {
                    bool sticky = isWindowSticky(safeWindow);
                    auto onSnapSuccess = [this](const QString&, const QString& snappedScreenName) {
                        m_snapAssistHandler->showContinuationIfNeeded(snappedScreenName);
                    };
                    tryAsyncSnapCall(*m_windowTrackingInterface, QStringLiteral("snapToEmptyZone"),
                                     {windowId, releaseScreenName, sticky}, safeWindow, windowId, true, nullptr,
                                     onSnapSuccess);
                }

                // Snap Assist: if daemon requested, build candidates (unsnapped only) and call showSnapAssist.
                // All D-Bus calls are async to prevent compositor freeze if daemon is busy with
                // overlay teardown / layout change (see discussion #158).
                if (snapAssistRequested && !emptyZonesJson.isEmpty() && !releaseScreenName.isEmpty()) {
                    m_snapAssistHandler->asyncShow(windowId, releaseScreenName, emptyZonesJson);
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

void PlasmaZonesEffect::tryAsyncSnapCall(QDBusAbstractInterface& iface, const QString& method,
                                         const QList<QVariant>& args, QPointer<KWin::EffectWindow> window,
                                         const QString& windowId, bool storePreSnap, std::function<void()> fallback,
                                         std::function<void(const QString&, const QString&)> onSnapSuccess,
                                         bool skipAnimation)
{
    QDBusPendingCall call = iface.asyncCallWithArgumentList(method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args,
             skipAnimation](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << method << "error:" << reply.error().message();
                    if (fallback)
                        fallback();
                    return;
                }
                if (reply.argumentAt<4>() && window) {
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                              reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap)
                        ensurePreSnapGeometryStored(window, windowId);
                    applySnapGeometry(window, geo, false, 20, skipAnimation);
                    // args[1] is screenName (e.g. for snapToEmptyZone, snapToLastZone)
                    if (onSnapSuccess && args.size() >= 2) {
                        onSnapSuccess(windowId, args[1].toString());
                    }
                    return;
                }
                if (fallback)
                    fallback();
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
                                          int retriesLeft, bool skipAnimation)
{
    if (!window) {
        qCWarning(lcEffect) << "Cannot apply geometry - window is null";
        return;
    }

    // Normalize so width/height are non-negative; reject invalid rects
    QRect geo = geometry.normalized();
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        qCWarning(lcEffect) << "Cannot apply geometry - geometry is invalid or empty:" << geometry;
        return;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546.
    if (window->isFullScreen()) {
        qCDebug(lcEffect) << "Skipping geometry change - window is fullscreen";
        return;
    }

    // For X11/XWayland windows, KWin constrains the frame size to align with
    // WM_SIZE_HINTS (size increments for terminals like Ghostty, Kitty, etc.).
    // Pre-compute the constrained size and center the window in its zone so the
    // gap is distributed evenly instead of all at the bottom-right.
    // This applies to all snap operations (zone snap, autotile, resnap, etc.).
    // Wayland-native clients negotiate size async (constrainFrameSize only
    // checks min/max, not char-cell grid), so they're handled by the deferred
    // check in centerUndersizedAutotileWindows().
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

    qCDebug(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geo;

    // Capture old frame before moveResize for repaint region
    const QRectF oldFrame = window->frameGeometry();

    // In KWin 6, we use the window's moveResize methods
    // When allowDuringDrag is false: defer if window is in user move/resize (snap on release)
    // When allowDuringDrag is true: apply immediately (FancyZones-style during drag)
    if (!allowDuringDrag && (window->isUserMove() || window->isUserResize())) {
        if (retriesLeft <= 0) {
            qCWarning(lcEffect) << "Giving up snap geometry — window still in user move after"
                                << "20 retries (2s). This may indicate a KWin state bug.";
            return;
        }
        qCDebug(lcEffect) << "Window still in user move/resize state, deferring geometry change"
                          << "(retries left:" << retriesLeft << ")";
        // Schedule the geometry change for when the move operation completes.
        // Use QPointer to safely handle window destruction during the timer delay.
        // This covers the brief race where forceEnd fired but KWin hasn't cleared
        // isUserMove yet (takes ~1 frame). The activation-button-held case is
        // handled earlier in callDragStopped via cancelInteractiveMoveResize.
        QPointer<KWin::EffectWindow> safeWindow = window;
        QTimer::singleShot(100, this, [this, safeWindow, geo, remaining = retriesLeft - 1, skipAnimation]() {
            if (safeWindow && !safeWindow->isDeleted() && !safeWindow->isFullScreen()) {
                applySnapGeometry(safeWindow, geo, false, remaining, skipAnimation);
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

QString PlasmaZonesEffect::extractAppId(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }
    int sep = windowId.indexOf(QLatin1Char('|'));
    return (sep > 0) ? windowId.left(sep) : windowId;
}

QString PlasmaZonesEffect::deriveShortNameFromWindowClass(const QString& windowClass)
{
    if (windowClass.isEmpty()) {
        return QString();
    }
    // Handle reverse-DNS: org.kde.dolphin -> dolphin
    int dotIdx = windowClass.lastIndexOf(QLatin1Char('.'));
    if (dotIdx >= 0 && dotIdx < windowClass.length() - 1) {
        return windowClass.mid(dotIdx + 1);
    }
    return windowClass;
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

void PlasmaZonesEffect::notifyWindowClosed(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    const QString windowId = getWindowId(w);

    if (!ensureWindowTrackingReady("notify windowClosed")) {
        return;
    }

    qCInfo(lcEffect) << "Notifying daemon: windowClosed" << windowId;
    m_windowTrackingInterface->asyncCall(QStringLiteral("windowClosed"), windowId);
}

void PlasmaZonesEffect::notifyWindowActivated(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w)) {
        return;
    }

    if (!ensureWindowTrackingReady("notify windowActivated")) {
        return;
    }

    QString windowId = getWindowId(w);
    QString screenName = getWindowScreenName(w);

    qCDebug(lcEffect) << "Notifying daemon: windowActivated" << windowId << "on screen" << screenName;
    m_windowTrackingInterface->asyncCall(QStringLiteral("windowActivated"), windowId, screenName);

    // R2 fix: Notify autotile engine of focus change with screen name so
    // m_windowToScreen is updated (also addresses R5: cross-screen detection)
    if (m_autotileHandler->isAutotileScreen(screenName)) {
        fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("notifyWindowFocused"), {windowId, screenName},
                              QStringLiteral("notifyWindowFocused"));
    }
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    // Single-pass lookup: check exact ID match and appId fallback.
    const QString targetAppId = extractAppId(windowId);
    KWin::EffectWindow* appMatch = nullptr;

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (wId == windowId) {
            return w; // Exact match — return immediately
        }
        if (!appMatch && extractAppId(wId) == targetAppId) {
            appMatch = w;
        }
    }

    return appMatch;
}

QVector<KWin::EffectWindow*> PlasmaZonesEffect::findAllWindowsById(const QString& windowId) const
{
    QVector<KWin::EffectWindow*> out;
    if (windowId.isEmpty()) {
        return out;
    }
    const QString targetAppId = extractAppId(windowId);
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
        if (extractAppId(wId) == targetAppId) {
            out.append(w);
        }
    }
    return out;
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

void PlasmaZonesEffect::paintScreen(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    int mask, const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
    QRect borderRect;
    const int bw = m_autotileHandler->borderWidth();
    const QColor bc = m_autotileHandler->borderColor();
    KWin::EffectWindow* active = KWin::effects->activeWindow();

    if (bw > 0 && bc.isValid() && bc.alpha() > 0 && active) {
        if (!active->isMinimized() && !active->isFullScreen() && active->isOnCurrentDesktop()
            && active->screen() == screen) {
            const QString wid = getWindowId(active);
            if (m_autotileHandler->isBorderlessWindow(wid) && !m_windowAnimator->hasAnimation(active)) {
                const QRectF frame = active->frameGeometry();
                borderRect = frame.toAlignedRect().adjusted(-bw, -bw, bw, bw);
            }
        }
    }

    KWin::effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);

    // Draw border after all windows, clipped against every window above the
    // active window in the stacking order.
    if (!borderRect.isEmpty()) {
        QRegion visibleRegion(borderRect);
        bool seenActive = false;

        const auto stack = KWin::effects->stackingOrder();
        for (auto* w : stack) {
            if (w == active) {
                seenActive = true;
                continue;
            }
            if (seenActive && w && !w->isMinimized() && w->isOnCurrentDesktop() && w->screen() == screen) {
                const QRect winRect = w->frameGeometry().toAlignedRect();
                if (visibleRegion.intersects(winRect)) {
                    qCDebug(lcEffect) << "BORDER CLIP: subtracting" << w->caption() << "type=" << int(w->windowType())
                                      << "geo=" << winRect;
                    visibleRegion -= winRect;
                }
            }
        }

        if (!visibleRegion.isEmpty()) {
            m_borderRenderer->drawBorders(renderTarget, viewport, {borderRect}, bw, bc, visibleRegion);
        }
    }

    if (m_lastBorderRect != borderRect) {
        if (!m_lastBorderRect.isEmpty()) {
            KWin::effects->addRepaint(m_lastBorderRect);
        }
        m_lastBorderRect = borderRect;
    }
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
