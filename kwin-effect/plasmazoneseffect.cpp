// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <QBuffer>
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
#include <QPixmap>
#include <QPointer>

#include <window.h>
#include <core/output.h> // For Output::name() for multi-monitor support

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
 * hangs during login (when the daemon may be registered but not yet processing messages),
 * we first check if the service name is registered on the bus. This is a fast call to
 * the D-Bus daemon itself (always responsive, <1ms). If the service isn't registered,
 * we skip interface creation entirely, avoiding the blocking introspection.
 */
template<typename InterfacePtr>
static void ensureInterface(InterfacePtr& interface, const QString& interfaceName, const char* logName)
{
    if (interface && interface->isValid()) {
        return;
    }

    // Fast pre-check: ask the D-Bus daemon (not the target service) whether the service
    // name has an owner. This avoids the expensive QDBusInterface introspection call when
    // the daemon isn't running at all. The D-Bus daemon always responds immediately.
    auto* busIface = QDBusConnection::sessionBus().interface();
    if (!busIface || !busIface->isServiceRegistered(DBus::ServiceName).value()) {
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
    return QRect(obj[QLatin1String("x")].toInt(), obj[QLatin1String("y")].toInt(),
                 obj[QLatin1String("width")].toInt(), obj[QLatin1String("height")].toInt());
}

void PlasmaZonesEffect::dispatchAsyncStringReply(QDBusPendingCall call, std::function<void(const QString&)> callback)
{
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [callback](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (callback) {
            callback(reply.isValid() ? reply.value() : QString());
        }
    });
}

void PlasmaZonesEffect::queryZoneForWindowAsync(const QString& windowId, std::function<void(const QString&)> callback)
{
    if (!ensureWindowTrackingReady("query zone for window")) {
        if (callback) callback(QString());
        return;
    }
    dispatchAsyncStringReply(
        m_windowTrackingInterface->asyncCall(QStringLiteral("getZoneForWindow"), windowId),
        std::move(callback));
}

void PlasmaZonesEffect::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId, const QRectF& preCapturedGeometry)
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

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safeWindow, capturedWindowId, capturedGeom](QDBusPendingCallWatcher* watcher) {
        watcher->deleteLater();

        QDBusPendingReply<bool> reply = *watcher;
        bool hasGeometry = reply.isValid() && reply.value();

        if (!hasGeometry && m_windowTrackingInterface && m_windowTrackingInterface->isValid()) {
            // Use pre-captured geometry if provided, otherwise read from window
            QRectF geom = capturedGeom.isValid() ? capturedGeom
                                                  : (safeWindow ? safeWindow->frameGeometry() : QRectF());
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
            QString stableId = extractStableId(getWindowId(w));
            windowMap[stableId] = w;
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
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
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
                if (m_autotileScreens.contains(getWindowScreenName(w))) {
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
                if (detectActivationAndGrab() || m_cachedZoneSelectorEnabled) {
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
                if (!detectActivationAndGrab() && !m_cachedZoneSelectorEnabled) {
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
                        QDBusMessage msg = QDBusMessage::createMethodCall(
                            DBus::ServiceName, DBus::ObjectPath,
                            DBus::Interface::Autotile,
                            QStringLiteral("floatWindow"));
                        msg << windowId;
                        QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
                        auto *watcher = new QDBusPendingCallWatcher(pending, this);
                        connect(watcher, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *w) {
                            if (w->isError()) {
                                qCWarning(lcEffect) << "floatWindow D-Bus call failed:" << w->error().message();
                            }
                            w->deleteLater();
                        });
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

    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &PlasmaZonesEffect::slotWindowActivated);

    // mouseChanged is the only reliable way to get modifier state in a KWin effect on Wayland;
    // QGuiApplication::queryKeyboardModifiers() doesn't work since effects run in the compositor.
    connect(KWin::effects, &KWin::EffectsHandler::mouseChanged, this, &PlasmaZonesEffect::slotMouseChanged);

    // Connect to screen geometry changes for keepWindowsInZonesOnResolutionChange feature
    // In KWin 6, use virtualScreenGeometryChanged (not per-screen signal)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, this,
            &PlasmaZonesEffect::slotScreenGeometryChanged);

    // Connect to daemon's settingsChanged D-Bus signal
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                          QStringLiteral("settingsChanged"), this, SLOT(slotSettingsChanged()));
    qCInfo(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";

    // Connect to keyboard navigation D-Bus signals
    connectNavigationSignals();

    // Connect to autotile D-Bus signals
    connectAutotileSignals();
    loadAutotileSettings();

    // Watch for daemon D-Bus service (re)registration.
    // After a daemon restart, m_lastCursorScreenName is still valid in the effect
    // but the daemon's lastCursorScreenName/lastActiveScreenName are empty.
    // Without this, keyboard shortcuts (rotate, etc.) operate on all screens
    // because resolveShortcutScreen returns nullptr.
    //
    // Limitations: only watches for service *registration* (new daemon start).
    // If the daemon crashes mid-call, in-flight D-Bus calls will return errors
    // that individual callers handle via isValid()/isError() checks.
    // On Wayland, this watcher uses D-Bus monitoring (not X11 selection),
    // which works reliably across both sessions.
    auto* serviceWatcher = new QDBusServiceWatcher(
        DBus::ServiceName, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon service registered — scheduling state re-push";

        // Reset stale D-Bus interfaces so ensureInterface recreates them on next use
        // Note: WindowDrag interface uses QDBusMessage (no QDBusInterface to reset)
        m_windowTrackingInterface.reset();
        m_zoneDetectionInterface.reset();
        m_overlayInterface.reset();
        m_settingsInterface.reset();

        // Defer re-push by 2s to avoid blocking the compositor.
        // QDBusInterface constructor performs synchronous introspection. If we call
        // ensureWindowTrackingReady() immediately, the daemon may still be in its
        // start() method (event loop not yet running) and unable to respond,
        // causing KWin to freeze until the D-Bus timeout expires.
        QTimer::singleShot(2000, this, [this]() {
            qCInfo(lcEffect) << "Re-pushing state after daemon registration";

            // Re-push cursor screen
            if (!m_lastCursorScreenName.isEmpty()
                && ensureWindowTrackingReady("daemon re-register cursor screen")) {
                m_windowTrackingInterface->asyncCall(
                    QStringLiteral("cursorScreenChanged"), m_lastCursorScreenName);
                qCDebug(lcEffect) << "Re-sent cursor screen:" << m_lastCursorScreenName;
            }

            // Re-notify active window (gives daemon lastActiveScreenName)
            KWin::EffectWindow* activeWindow = getActiveWindow();
            if (activeWindow) {
                notifyWindowActivated(activeWindow);
            }

            // Re-sync floating state and settings from daemon
            syncFloatingWindowsFromDaemon();
            loadCachedSettings();
            loadAutotileSettings();

            // Clear stale window tracking — the new daemon has no knowledge of our windows
            m_notifiedWindows.clear();
            m_pendingCloses.clear();

            // Re-announce all existing windows on autotile screens
            const auto windows = KWin::effects->stackingOrder();
            for (KWin::EffectWindow* w : windows) {
                if (w && shouldHandleWindow(w)) {
                    notifyWindowAdded(w);
                }
            }
        });
    });

    // Sync floating window state from daemon's persisted state
    syncFloatingWindowsFromDaemon();

    // Load exclusion settings from daemon
    loadCachedSettings();

    // Setup screen geometry change debounce timer
    // This prevents rapid-fire updates from causing windows to be resnapped unnecessarily
    // when monitors are connected/disconnected or arrangement changes occur
    m_screenChangeDebounce.setSingleShot(true);
    m_screenChangeDebounce.setInterval(500); // 500ms debounce
    connect(&m_screenChangeDebounce, &QTimer::timeout, this, &PlasmaZonesEffect::applyScreenGeometryChange);

    // Store initial virtual screen geometry for comparison
    m_lastVirtualScreenGeometry = KWin::effects->virtualScreenGeometry();

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
    auto* initialScreen = KWin::effects->activeScreen();
    if (initialScreen) {
        m_lastCursorScreenName = initialScreen->name();
        // Defer the D-Bus call so the daemon has time to register its service
        QTimer::singleShot(500, this, [this, initialName = m_lastCursorScreenName]() {
            // Only send if no mouse movement has already updated the screen
            if (m_lastCursorScreenName == initialName
                && !initialName.isEmpty()
                && ensureWindowTrackingReady("initial cursor screen")) {
                m_windowTrackingInterface->asyncCall(QStringLiteral("cursorScreenChanged"), initialName);
                qCDebug(lcEffect) << "Sent initial cursor screen:" << initialName;
            }
        });
    }

    qCInfo(lcEffect) << "Initialized - C++ effect with D-Bus support and mouseChanged connection";
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    if (m_keyboardGrabbed) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }
    m_screenChangeDebounce.stop();
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
    return m_dragTracker->isDragging();
}

void PlasmaZonesEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (e->type() == QEvent::KeyPress && e->key() == Qt::Key_Escape
        && m_dragTracker->isDragging()) {
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
    // Use full windowId so daemon can do per-instance lookup with stableId fallback
    QString windowId = getWindowId(w);
    m_navigationHandler->syncFloatingStateForWindow(windowId);

    // Notify autotile daemon about the new window
    notifyWindowAdded(w);

    // Check if we should auto-snap new windows to last used zone
    // Skip on autotile screens - the autotile engine handles window placement
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    if (!m_autotileScreens.contains(getWindowScreenName(w)) && shouldAutoSnapWindow(w) && !w->isMinimized()) {
        // Don't auto-snap if there's already another window of the same class
        // with a different PID. This prevents unwanted snapping when another app
        // spawns a window (e.g., Cachy Update spawning a Ghostty terminal).
        if (hasOtherWindowOfClassWithDifferentPid(w)) {
            qCDebug(lcEffect) << "Skipping auto-snap for" << w->windowClass()
                              << "- another window of same class exists with different PID";
            return;
        }

        // Use QPointer to safely handle window destruction during the delay
        // Raw pointer capture would become dangling if window closes before timer fires
        QPointer<KWin::EffectWindow> safeWindow = w;
        QTimer::singleShot(100, this, [this, safeWindow]() {
            if (safeWindow && shouldAutoSnapWindow(safeWindow)) {
                // Re-check PID condition after delay (windows might have changed)
                if (hasOtherWindowOfClassWithDifferentPid(safeWindow)) {
                    qCDebug(lcEffect) << "Skipping auto-snap for" << safeWindow->windowClass()
                                      << "after delay - another window of same class exists with different PID";
                    return;
                }
                callSnapToLastZone(safeWindow);
            }
        });
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
    // The daemon preserves floating state (keyed by stableId) so the window stays floating
    // when reopened. The effect's local cache will be synced in slotWindowAdded().

    m_windowAnimator->removeAnimation(w);

    // Notify daemon for cleanup
    notifyWindowClosed(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated
    notifyWindowActivated(w);
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
        qCInfo(lcEffect) << "mouseChanged buttons:" << static_cast<int>(oldbuttons) << "->" << static_cast<int>(buttons);
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
                if (detectActivationAndGrab() || m_cachedZoneSelectorEnabled) {
                    sendDeferredDragStarted();
                    callDragMoved(m_dragTracker->draggedWindowId(), pos, m_currentModifiers, static_cast<int>(m_currentMouseButtons));
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

void PlasmaZonesEffect::slotScreenGeometryChanged()
{
    // Debounce screen geometry changes to prevent rapid-fire updates
    // The virtualScreenGeometryChanged signal can fire multiple times in quick succession
    // for various reasons:
    // - Monitor connect/disconnect
    // - Monitor arrangement changes in system settings
    // - Resolution changes
    // - KWin internal geometry recalculations
    //
    // Without debouncing, this causes windows to be resnapped at random/unexpected times.
    // We wait 500ms after the last signal before actually applying changes.

    QRect currentGeometry = KWin::effects->virtualScreenGeometry();
    qCInfo(lcEffect) << "virtualScreenGeometryChanged fired"
                      << "- current:" << currentGeometry << "- previous:" << m_lastVirtualScreenGeometry
                      << "- pending:" << m_pendingScreenChange;

    // Check if the geometry actually changed significantly
    if (currentGeometry == m_lastVirtualScreenGeometry && !m_pendingScreenChange) {
        qCDebug(lcEffect) << "Screen geometry unchanged, ignoring signal";
        return;
    }

    m_pendingScreenChange = true;
    m_screenChangeDebounce.start(); // Restart timer (debounce)
}

void PlasmaZonesEffect::applyScreenGeometryChange()
{
    if (!m_pendingScreenChange) {
        return;
    }

    QRect currentGeometry = KWin::effects->virtualScreenGeometry();
    qCInfo(lcEffect) << "Applying debounced screen geometry change"
                      << "- previous:" << m_lastVirtualScreenGeometry << "- current:" << currentGeometry;

    m_pendingScreenChange = false;

    // Only reposition windows when the virtual screen SIZE (resolution / monitor setup) changed.
    // When only position or internal state changes (e.g. exiting KDE panel edit mode), KWin
    // may still emit virtualScreenGeometryChanged; we must not move windows then or they jump.
    const QSize previousSize = m_lastVirtualScreenGeometry.size();
    const QSize currentSize = currentGeometry.size();
    if (previousSize == currentSize) {
        qCDebug(lcEffect) << "Virtual screen size unchanged, skipping window repositioning";
        m_lastVirtualScreenGeometry = currentGeometry;
        return;
    }

    m_lastVirtualScreenGeometry = currentGeometry;
    if (m_reapplyInProgress) {
        m_reapplyPending = true;
        return;
    }
    fetchAndApplyWindowGeometries();
}

void PlasmaZonesEffect::slotReapplyWindowGeometriesRequested()
{
    qCInfo(lcEffect) << "Daemon requested re-apply of window geometries (e.g. after panel editor close)";
    if (m_reapplyInProgress) {
        m_reapplyPending = true;
        return;
    }
    fetchAndApplyWindowGeometries();
}

void PlasmaZonesEffect::fetchAndApplyWindowGeometries()
{
    if (!ensureWindowTrackingReady("get updated window geometries")) {
        return;
    }
    m_reapplyInProgress = true;
    QDBusPendingCall pendingCall = m_windowTrackingInterface->asyncCall(QStringLiteral("getUpdatedWindowGeometries"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    QPointer<PlasmaZonesEffect> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [self](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (!self) {
            return;
        }
        self->m_reapplyInProgress = false;
        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcEffect) << "No window geometries to update";
        } else {
            self->applyWindowGeometriesFromJson(reply.value());
        }
        if (self->m_reapplyPending) {
            self->m_reapplyPending = false;
            QTimer::singleShot(0, self, [self]() {
                if (self) {
                    self->fetchAndApplyWindowGeometries();
                }
            });
        }
    });
}

void PlasmaZonesEffect::applyWindowGeometriesFromJson(const QString& geometriesJson)
{
    if (geometriesJson.isEmpty() || geometriesJson == QLatin1String("[]")) {
        qCDebug(lcEffect) << "Empty geometries list from daemon";
        return;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(geometriesJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcEffect) << "Failed to parse window geometries:" << parseError.errorString();
        return;
    }
    if (!doc.isArray()) {
        qCWarning(lcEffect) << "Window geometries root is not an array";
        return;
    }
    QJsonArray geometries = doc.array();
    qCInfo(lcEffect) << "Applying geometries to" << geometries.size() << "windows";

    // Single pass: map by full window ID (so multiple windows in same zone get correct geometry)
    // and by stableId for fallback (first window per stableId; daemon usually sends full ids).
    QHash<QString, KWin::EffectWindow*> windowByFullId;
    QHash<QString, KWin::EffectWindow*> windowByStableId;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || !shouldHandleWindow(w)) {
            continue;
        }
        QString fullId = getWindowId(w);
        QString stableId = extractStableId(fullId);
        windowByFullId.insert(fullId, w);
        if (!windowByStableId.contains(stableId)) {
            windowByStableId.insert(stableId, w);
        }
    }

    for (const QJsonValue& value : geometries) {
        if (!value.isObject()) {
            qCDebug(lcEffect) << "Skipping non-object geometry entry";
            continue;
        }
        QJsonObject obj = value.toObject();
        QString windowId = obj[QLatin1String("windowId")].toString();
        if (windowId.isEmpty()) {
            qCDebug(lcEffect) << "Skipping geometry entry with empty windowId";
            continue;
        }
        int width = obj[QLatin1String("width")].toInt();
        int height = obj[QLatin1String("height")].toInt();
        if (width <= 0 || height <= 0) {
            qCDebug(lcEffect) << "Skipping geometry entry with invalid size for" << windowId;
            continue;
        }
        int x = obj[QLatin1String("x")].toInt();
        int y = obj[QLatin1String("y")].toInt();

        KWin::EffectWindow* window = windowByFullId.value(windowId);
        if (!window) {
            window = windowByStableId.value(extractStableId(windowId));
        }
        if (window && shouldHandleWindow(window)) {
            // Skip windows on autotile screens — they are managed by the autotile engine
            const QString winScreenName = getWindowScreenName(window);
            if (m_autotileScreens.contains(winScreenName)) {
                qCDebug(lcEffect) << "Skipping autotile-managed window" << windowId << "on screen" << winScreenName;
                continue;
            }
            QRect newGeometry(x, y, width, height);
            QRectF currentWindowGeometry = window->frameGeometry();
            if (QRect(currentWindowGeometry.toRect()) != newGeometry) {
                qCInfo(lcEffect) << "Repositioning window" << windowId << "from" << currentWindowGeometry << "to"
                                  << newGeometry;
                applySnapGeometry(window, newGeometry);
            }
        }
    }
}

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCInfo(lcEffect) << "Daemon signaled settingsChanged - reloading settings";
    loadCachedSettings();
    loadAutotileSettings();
}

QString PlasmaZonesEffect::getWindowId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }

    // Create a stable identifier from window properties
    // Format: "windowClass:resourceName:internalId"
    QString windowClass = w->windowClass();
    QString resourceName = w->windowRole().isEmpty() ? windowClass : w->windowRole();
    QString internalId = QString::number(reinterpret_cast<quintptr>(w));

    return QStringLiteral("%1:%2:%3").arg(windowClass, resourceName, internalId);
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
    ensureInterface(m_windowTrackingInterface, DBus::Interface::WindowTracking, "WindowTracking");
}

void PlasmaZonesEffect::ensureZoneDetectionInterface()
{
    ensureInterface(m_zoneDetectionInterface, DBus::Interface::ZoneDetection, "ZoneDetection");
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
    ensureInterface(m_overlayInterface, DBus::Interface::Overlay, "Overlay");
    if (!m_overlayInterface || !m_overlayInterface->isValid()) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- Overlay interface not available";
        return false;
    }
    return true;
}

QJsonArray PlasmaZonesEffect::buildSnapAssistCandidates(const QString& excludeWindowId,
                                                        const QString& screenName,
                                                        const QSet<QString>& snappedWindowIds) const
{
    // Candidates: unsnapped windows (including floated — user can snap them to fill empty zones)
    QJsonArray candidates;
    const auto windows = KWin::effects->stackingOrder();

    for (KWin::EffectWindow* w : windows) {
        if (!w || !shouldHandleWindow(w) || w->isMinimized() || !w->isOnCurrentDesktop()
            || !w->isOnCurrentActivity()) {
            continue;
        }

        QString windowId = getWindowId(w);
        if (windowId == excludeWindowId) {
            continue; // Exclude the just-snapped window (exact match)
        }
        // Check snapped set by both full ID (exact) and stable ID (for daemon-stored IDs
        // whose pointer address may differ from the current EffectWindow pointer)
        if (snappedWindowIds.contains(windowId)) {
            continue; // Exact match — this window is snapped
        }
        QString stableId = extractStableId(windowId);
        bool snappedByStableId = false;
        for (const QString& snappedId : snappedWindowIds) {
            if (extractStableId(snappedId) == stableId) {
                // Stable ID matches — but only exclude if there's a single window with this
                // stable ID. If multiple windows share the stable ID (same app), don't exclude
                // based on stable ID alone since only one of them is actually snapped.
                snappedByStableId = true;
                break;
            }
        }
        if (snappedByStableId) {
            // Count how many live windows share this stable ID
            int sameStableCount = 0;
            for (KWin::EffectWindow* other : windows) {
                if (other && shouldHandleWindow(other)
                    && extractStableId(getWindowId(other)) == stableId) {
                    ++sameStableCount;
                }
            }
            // If only one window has this stable ID, the stable ID match is unambiguous
            if (sameStableCount <= 1) {
                continue;
            }
            // Multiple windows share this stable ID — don't exclude, the full-ID check
            // above already handled the exact match case
        }

        QString winScreenName = getWindowScreenName(w);
        if (!screenName.isEmpty() && winScreenName != screenName) {
            continue; // Same screen only
        }

        QString windowClass = w->windowClass();
        QString iconName = deriveShortNameFromWindowClass(windowClass);
        if (iconName.isEmpty()) {
            iconName = QStringLiteral("application-x-executable");
        }

        QJsonObject obj;
        obj[QLatin1String("windowId")] = windowId;
        obj[QLatin1String("kwinHandle")] = w->internalId().toString();
        obj[QLatin1String("icon")] = iconName;
        obj[QLatin1String("caption")] = w->caption();

        // Use EffectWindow::icon() for proper app icon (KWin resolves from .desktop)
        QIcon winIcon = w->icon();
        if (!winIcon.isNull()) {
            QPixmap pix = winIcon.pixmap(64, 64);
            if (!pix.isNull()) {
                QByteArray ba;
                QBuffer buffer(&ba);
                buffer.open(QIODevice::WriteOnly);
                if (pix.toImage().save(&buffer, "PNG")) {
                    QString dataUrl = QStringLiteral("data:image/png;base64,")
                        + QString::fromUtf8(ba.toBase64());
                    obj[QLatin1String("iconPng")] = dataUrl;
                }
            }
        }

        candidates.append(obj);
    }
    return candidates;
}

void PlasmaZonesEffect::syncFloatingWindowsFromDaemon()
{
    // Delegate to NavigationHandler
    m_navigationHandler->syncFloatingWindowsFromDaemon();
}

void PlasmaZonesEffect::loadCachedSettings()
{
    // Set sensible defaults immediately - don't block compositor startup waiting for daemon
    // These will be updated asynchronously when the daemon responds
    m_excludeTransientWindows = true;
    m_minimumWindowWidth = 200;
    m_minimumWindowHeight = 150;
    m_snapAssistEnabled = false;

    // Use QDBusMessage + QDBusConnection::asyncCall instead of QDBusInterface to avoid
    // synchronous D-Bus introspection that blocks the compositor thread.
    // QDBusInterface's constructor sends an Introspect call and blocks until the target
    // service replies. During login, the daemon may have registered its D-Bus name
    // (via systemd/autostart) but not yet be processing messages, causing the
    // introspection to block for up to the D-Bus timeout (25s). This hangs KWin,
    // delaying all autostart applications. QDBusMessage::createMethodCall is purely
    // local (no D-Bus communication), and asyncCall returns immediately.
    // If the daemon isn't running, the async calls simply fail and defaults are used.

    // Helper: create a fully-async D-Bus call to getSetting without QDBusInterface
    auto makeSettingCall = [](const QString& settingName) -> QDBusPendingCall {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
            QStringLiteral("getSetting"));
        msg << settingName;
        return QDBusConnection::sessionBus().asyncCall(msg);
    };

    // Helper: extract setting value from D-Bus reply (handles both QDBusVariant and plain QVariant)
    auto extractVariant = [](const QDBusPendingReply<QVariant>& reply) -> QVariant {
        QVariant value = reply.value();
        if (value.canConvert<QDBusVariant>()) {
            return value.value<QDBusVariant>().variant();
        }
        return value;
    };

    // Load excludeTransientWindows
    auto* excludeWatcher = new QDBusPendingCallWatcher(
        makeSettingCall(QStringLiteral("excludeTransientWindows")), this);
    connect(excludeWatcher, &QDBusPendingCallWatcher::finished, this, [this, extractVariant](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            m_excludeTransientWindows = extractVariant(reply).toBool();
            qCDebug(lcEffect) << "Loaded excludeTransientWindows:" << m_excludeTransientWindows;
        }
    });

    // Load minimumWindowWidth
    auto* widthWatcher = new QDBusPendingCallWatcher(
        makeSettingCall(QStringLiteral("minimumWindowWidth")), this);
    connect(widthWatcher, &QDBusPendingCallWatcher::finished, this, [this, extractVariant](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            m_minimumWindowWidth = extractVariant(reply).toInt();
            qCDebug(lcEffect) << "Loaded minimumWindowWidth:" << m_minimumWindowWidth;
        }
    });

    // Load minimumWindowHeight
    auto* heightWatcher = new QDBusPendingCallWatcher(
        makeSettingCall(QStringLiteral("minimumWindowHeight")), this);
    connect(heightWatcher, &QDBusPendingCallWatcher::finished, this, [this, extractVariant](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            m_minimumWindowHeight = extractVariant(reply).toInt();
            qCDebug(lcEffect) << "Loaded minimumWindowHeight:" << m_minimumWindowHeight;
        }
    });

    // Load snapAssistEnabled (for Snap Assist continuation gating)
    auto* snapAssistWatcher = new QDBusPendingCallWatcher(
        makeSettingCall(QStringLiteral("snapAssistEnabled")), this);
    connect(snapAssistWatcher, &QDBusPendingCallWatcher::finished, this, [this, extractVariant](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            m_snapAssistEnabled = extractVariant(reply).toBool();
            qCDebug(lcEffect) << "Loaded snapAssistEnabled:" << m_snapAssistEnabled;
        }
    });

    // Load dragActivationTriggers (for local trigger gating — avoids 60Hz D-Bus during non-zone drags)
    auto* triggersWatcher = new QDBusPendingCallWatcher(
        makeSettingCall(QStringLiteral("dragActivationTriggers")), this);
    connect(triggersWatcher, &QDBusPendingCallWatcher::finished, this, [this, extractVariant](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            m_cachedDragActivationTriggers = extractVariant(reply).toList();
            // Pre-parse to POD structs so anyLocalTriggerHeld() avoids QVariant
            // unboxing on every call (~30x/sec during drag).
            m_parsedTriggers.clear();
            m_parsedTriggers.reserve(m_cachedDragActivationTriggers.size());
            for (const auto& t : std::as_const(m_cachedDragActivationTriggers)) {
                const auto map = t.toMap();
                ParsedTrigger pt;
                pt.modifier = map.value(QStringLiteral("modifier"), 0).toInt();
                pt.mouseButton = map.value(QStringLiteral("mouseButton"), 0).toInt();
                m_parsedTriggers.append(pt);
            }
            qCDebug(lcEffect) << "Loaded dragActivationTriggers:" << m_parsedTriggers.size() << "triggers";
        }
    });

    // Load toggleActivation
    auto* toggleWatcher = new QDBusPendingCallWatcher(
        makeSettingCall(QStringLiteral("toggleActivation")), this);
    connect(toggleWatcher, &QDBusPendingCallWatcher::finished, this, [this, extractVariant](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            m_cachedToggleActivation = extractVariant(reply).toBool();
            qCDebug(lcEffect) << "Loaded toggleActivation:" << m_cachedToggleActivation;
        }
    });

    // Load zoneSelectorEnabled
    auto* zoneSelectorWatcher = new QDBusPendingCallWatcher(
        makeSettingCall(QStringLiteral("zoneSelectorEnabled")), this);
    connect(zoneSelectorWatcher, &QDBusPendingCallWatcher::finished, this, [this, extractVariant](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            m_cachedZoneSelectorEnabled = extractVariant(reply).toBool();
            qCDebug(lcEffect) << "Loaded zoneSelectorEnabled:" << m_cachedZoneSelectorEnabled;
        }
    });

    qCDebug(lcEffect) << "Loading cached settings asynchronously, using defaults until loaded";
}

bool PlasmaZonesEffect::checkLocalModifier(int modifierSetting, Qt::KeyboardModifiers mods)
{
    const bool shiftHeld = mods.testFlag(Qt::ShiftModifier);
    const bool ctrlHeld = mods.testFlag(Qt::ControlModifier);
    const bool altHeld = mods.testFlag(Qt::AltModifier);
    const bool metaHeld = mods.testFlag(Qt::MetaModifier);

    switch (modifierSetting) {
    case 0:  return false;                    // Disabled
    case 1:  return shiftHeld;                // Shift
    case 2:  return ctrlHeld;                 // Ctrl
    case 3:  return altHeld;                  // Alt
    case 4:  return metaHeld;                 // Meta
    case 5:  return ctrlHeld && altHeld;      // CtrlAlt
    case 6:  return ctrlHeld && shiftHeld;    // CtrlShift
    case 7:  return altHeld && shiftHeld;     // AltShift
    case 8:  return true;                     // AlwaysActive
    case 9:  return altHeld && metaHeld;      // AltMeta
    case 10: return ctrlHeld && altHeld && metaHeld; // CtrlAltMeta
    default: return false;
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
                                          QStringLiteral("reapplyWindowGeometriesRequested"), this,
                                          SLOT(slotReapplyWindowGeometriesRequested()));

    // Connect to floating state changes to keep local cache in sync
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("windowFloatingChanged"), this,
                                          SLOT(slotWindowFloatingChanged(QString, bool)));

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

void PlasmaZonesEffect::queryAdjacentZoneAsync(const QString& currentZoneId, const QString& direction, std::function<void(const QString&)> callback)
{
    ensureZoneDetectionInterface();
    if (!m_zoneDetectionInterface || !m_zoneDetectionInterface->isValid()) {
        if (callback) callback(QString());
        return;
    }
    dispatchAsyncStringReply(
        m_zoneDetectionInterface->asyncCall(QStringLiteral("getAdjacentZone"), currentZoneId, direction),
        std::move(callback));
}

void PlasmaZonesEffect::queryFirstZoneInDirectionAsync(const QString& direction, const QString& screenName, std::function<void(const QString&)> callback)
{
    ensureZoneDetectionInterface();
    if (!m_zoneDetectionInterface || !m_zoneDetectionInterface->isValid()) {
        if (callback) callback(QString());
        return;
    }
    dispatchAsyncStringReply(
        m_zoneDetectionInterface->asyncCall(QStringLiteral("getFirstZoneInDirection"), direction, screenName),
        std::move(callback));
}

void PlasmaZonesEffect::queryZoneGeometryForScreenAsync(const QString& zoneId, const QString& screenName, std::function<void(const QString&)> callback)
{
    if (!ensureWindowTrackingReady("query zone geometry")) {
        if (callback) callback(QString());
        return;
    }
    dispatchAsyncStringReply(
        m_windowTrackingInterface->asyncCall(QStringLiteral("getZoneGeometryForScreen"), zoneId, screenName),
        std::move(callback));
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

    // Match by exact full window ID (includes pointer address) to distinguish
    // multiple windows of the same application. Fall back to stable ID only if
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
        QString stableId = extractStableId(windowId);
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w) && extractStableId(getWindowId(w)) == stableId) {
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
        showSnapAssistContinuationIfNeeded(screenName);
    }
}

void PlasmaZonesEffect::showSnapAssistContinuationIfNeeded(const QString& screenName)
{
    if (screenName.isEmpty() || !m_snapAssistEnabled || !ensureWindowTrackingReady("snap assist continuation")) {
        return;
    }
    QDBusPendingCall emptyCall =
        m_windowTrackingInterface->asyncCall(QStringLiteral("getEmptyZonesJson"), screenName);
    auto* watcher = new QDBusPendingCallWatcher(emptyCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, screenName](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<QString> reply = *w;
                if (!reply.isValid() || reply.value().isEmpty()
                    || reply.value() == QLatin1String("[]")) {
                    return;
                }
                asyncShowSnapAssist(QString(), screenName, reply.value());
            });
}

void PlasmaZonesEffect::asyncShowSnapAssist(const QString& excludeWindowId, const QString& screenName,
                                             const QString& emptyZonesJson)
{
    if (!ensureWindowTrackingReady("snap assist snapped windows")) {
        return;
    }
    QDBusPendingCall snapCall =
        m_windowTrackingInterface->asyncCall(QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);
    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, excludeWindowId, screenName, emptyZonesJson](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<QStringList> reply = *w;
                QSet<QString> snappedWindowIds;
                if (reply.isValid()) {
                    for (const QString& id : reply.value()) {
                        snappedWindowIds.insert(id);
                    }
                }
                QJsonArray candidates = buildSnapAssistCandidates(excludeWindowId, screenName, snappedWindowIds);
                if (candidates.isEmpty() || !ensureOverlayInterface("snap assist show")) {
                    return;
                }
                m_overlayInterface->asyncCall(QStringLiteral("showSnapAssist"), screenName,
                                              emptyZonesJson,
                                              QString::fromUtf8(
                                                  QJsonDocument(candidates).toJson(QJsonDocument::Compact)));
                qCInfo(lcEffect) << "Snap Assist shown with" << candidates.size() << "candidates";
            });
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
    m_navigationHandler->handleToggleWindowFloat(shouldFloat);
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

    // Async fetch all snapped windows to filter already-snapped ones locally,
    // replacing the previous per-window sync queryZoneForWindow loop
    QDBusPendingCall snapCall = m_windowTrackingInterface->asyncCall(QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, screenName](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedStableIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedStableIds.insert(extractStableId(id));
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
            QString stableId = extractStableId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

            if (getWindowScreenName(w) != screenName) {
                qCDebug(lcEffect) << "snap-all: skipping window on different screen" << stableId;
                continue;
            }

            if (w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                qCDebug(lcEffect) << "snap-all: skipping minimized/other-desktop window" << stableId;
                continue;
            }

            // Full ID match first (distinguishes multi-instance apps),
            // stable ID fallback for single-instance apps across restarts
            if (snappedFullIds.contains(windowId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window" << stableId;
                continue;
            }
            if (!hasOtherWindowOfClassWithDifferentPid(w) && snappedStableIds.contains(stableId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window (stable match)" << stableId;
                continue;
            }

            unsnappedWindowIds.append(windowId);
        }

        qCDebug(lcEffect) << "snap-all: found" << unsnappedWindowIds.size() << "unsnapped windows to snap";

        if (unsnappedWindowIds.isEmpty()) {
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenName;
            emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"),
                                   QString(), QString(), screenName);
            return;
        }

        if (!ensureWindowTrackingReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall = m_windowTrackingInterface->asyncCall(
            QStringLiteral("calculateSnapAllWindows"), unsnappedWindowIds, screenName);
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, screenName](QDBusPendingCallWatcher* cw) {
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
        QSet<QString> trackedStableIds;

        if (reply.isValid()) {
            // Extract stable IDs from tracked windows for comparison
            // Window IDs include pointer addresses which change, but stable IDs persist
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                QString stableId = extractStableId(windowId);
                if (!stableId.isEmpty()) {
                    trackedStableIds.insert(stableId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedStableIds.size() << "tracked windows from daemon";
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
            QString stableId = extractStableId(windowId);
            if (trackedStableIds.contains(stableId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callSnapToLastZone(window);
        }
    });
}

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& windowId, bool isFloating)
{
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    // Uses full windowId for per-instance tracking (stableId fallback in isWindowFloating).
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
        if (w->isSpecialWindow() || w->isDesktop() || w->isDock()
            || w->isSkipSwitcher() || w->isNotification()
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
        obj[QStringLiteral("windowClass")] = windowClass;
        obj[QStringLiteral("appName")] = appName;
        obj[QStringLiteral("caption")] = w->caption();
        windowArray.append(obj);
    }

    QString jsonString = QString::fromUtf8(QJsonDocument(windowArray).toJson(QJsonDocument::Compact));
    qCDebug(lcEffect) << "Providing" << windowArray.size() << "running windows to daemon";

    // Send result back to daemon via D-Bus
    ensureInterface(m_settingsInterface, DBus::Interface::Settings, "Settings");
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

void PlasmaZonesEffect::callSnapToLastZone(KWin::EffectWindow* window)
{
    if (!window) {
        return;
    }

    if (!ensureWindowTrackingReady("snap to last zone")) {
        return;
    }

    QString windowId = getWindowId(window);
    QString screenName = getWindowScreenName(window);
    bool sticky = isWindowSticky(window);

    // Use QPointer to safely handle window destruction during async calls
    QPointer<KWin::EffectWindow> safeWindow = window;

    // Priority chain (built bottom-up so each step's fallback is the next):
    //
    // screenName strategy: Steps 1-2 (app rules, session restore) use the screenName
    // captured at call time — app rules should match the screen where the window opened,
    // and persisted zones are stored against the original screen. Steps 3-4 (auto-assign,
    // last zone) re-query getWindowScreenName(safeWindow) live because the window may have
    // been moved between async steps and these features should target the current screen.

    // FOURTH: Snap to last zone (final fallback)
    auto tryLastZone = [this, safeWindow, windowId, sticky]() {
        if (!safeWindow || !m_windowTrackingInterface || !m_windowTrackingInterface->isValid()) return;
        QString screen = getWindowScreenName(safeWindow);
        tryAsyncSnapCall(*m_windowTrackingInterface, QStringLiteral("snapToLastZone"),
                          {windowId, screen, sticky}, safeWindow, windowId, true, nullptr);
    };

    // THIRD: Auto-assign to empty zone
    auto tryEmptyZone = [this, safeWindow, windowId, sticky, tryLastZone]() {
        if (!safeWindow || !m_windowTrackingInterface || !m_windowTrackingInterface->isValid()) return;
        QString screen = getWindowScreenName(safeWindow);
        tryAsyncSnapCall(*m_windowTrackingInterface, QStringLiteral("snapToEmptyZone"),
                          {windowId, screen, sticky}, safeWindow, windowId, true, tryLastZone);
    };

    // SECOND: Restore from persisted zone (uses captured screenName — persisted zone matches open-time screen)
    auto tryRestore = [this, safeWindow, windowId, screenName, sticky, tryEmptyZone]() {
        if (!safeWindow || !m_windowTrackingInterface || !m_windowTrackingInterface->isValid()) return;
        tryAsyncSnapCall(*m_windowTrackingInterface, QStringLiteral("restoreToPersistedZone"),
                          {windowId, screenName, sticky}, safeWindow, windowId, true, tryEmptyZone);
    };

    // FIRST: App rules (highest priority — uses captured screenName for open-time screen matching)
    tryAsyncSnapCall(*m_windowTrackingInterface, QStringLiteral("snapToAppRule"),
                      {windowId, screenName, sticky}, safeWindow, windowId, true, tryRestore);
}

void PlasmaZonesEffect::callDragStarted(const QString& windowId, const QRectF& geometry)
{
    updateWindowStickyState(m_dragTracker->draggedWindow());

    // Get window class info for exclusion filtering
    QString appName;
    QString windowClass;
    if (m_dragTracker->draggedWindow()) {
        windowClass = m_dragTracker->draggedWindow()->windowClass();
        // Derive short app name from window class for exclusion matching
        appName = windowClass;
        int spaceIdx = windowClass.indexOf(QLatin1Char(' '));
        if (spaceIdx > 0) {
            appName = windowClass.left(spaceIdx);
        } else {
            int dotIdx = windowClass.lastIndexOf(QLatin1Char('.'));
            if (dotIdx >= 0 && dotIdx < windowClass.length() - 1) {
                appName = windowClass.mid(dotIdx + 1);
            }
        }
    }

    // Use QDBusMessage::createMethodCall instead of QDBusInterface to avoid
    // synchronous D-Bus introspection. QDBusInterface's constructor blocks the
    // compositor thread (~25s timeout) if the daemon is registered but not yet
    // processing messages. QDBusMessage is purely local — no D-Bus communication
    // until asyncCall, which returns immediately.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
        QStringLiteral("dragStarted"));
    msg << windowId << geometry.x() << geometry.y() << geometry.width()
        << geometry.height() << appName << windowClass << static_cast<int>(m_currentMouseButtons);
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

void PlasmaZonesEffect::callDragMoved(const QString& windowId, const QPointF& cursorPos, Qt::KeyboardModifiers mods, int mouseButtons)
{
    // Don't send manual zone drag updates when drag was started on an autotile screen.
    // Use captured flag (not live m_autotileScreens) for consistency with drag start/stop.
    if (m_dragBypassedForAutotile) {
        return;
    }

    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    // See callDragStarted() comment for rationale.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
        QStringLiteral("dragMoved"));
    msg << windowId << static_cast<int>(cursorPos.x())
        << static_cast<int>(cursorPos.y()) << static_cast<int>(mods) << mouseButtons;
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
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
        QStringLiteral("dragStopped"));
    msg << windowId << static_cast<int>(cursorAtRelease.x())
        << static_cast<int>(cursorAtRelease.y()) << static_cast<int>(m_currentModifiers)
        << static_cast<int>(m_currentMouseButtons);
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
                          << "releaseScreen=" << releaseScreenName
                          << "restoreSizeOnly=" << restoreSizeOnly
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
                    snapGeometry = QRect(static_cast<int>(frame.x()), static_cast<int>(frame.y()),
                                         snapWidth, snapHeight);
                    // Skip if already restored during drag (slotRestoreSizeDuringDrag) to avoid redundant moveResize
                    if (qAbs(frame.width() - snapWidth) <= 1 && qAbs(frame.height() - snapHeight) <= 1) {
                        shouldApply = false;
                        qCDebug(lcEffect) << "Skip restore apply - already at correct size from during-drag restore";
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
                    if (safeWindow->isUserMove()
                        && !(m_currentMouseButtons & Qt::LeftButton)) {
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
        if (!shouldSnap && safeWindow && !releaseScreenName.isEmpty() && ensureWindowTrackingReady("auto-fill on drop")) {
            bool sticky = isWindowSticky(safeWindow);
            auto onSnapSuccess = [this](const QString&, const QString& snappedScreenName) {
                showSnapAssistContinuationIfNeeded(snappedScreenName);
            };
            tryAsyncSnapCall(*m_windowTrackingInterface, QStringLiteral("snapToEmptyZone"),
                              {windowId, releaseScreenName, sticky}, safeWindow, windowId, true, nullptr,
                              onSnapSuccess);
        }

        // Snap Assist: if daemon requested, build candidates (unsnapped only) and call showSnapAssist.
        // All D-Bus calls are async to prevent compositor freeze if daemon is busy with
        // overlay teardown / layout change (see discussion #158).
        if (snapAssistRequested && !emptyZonesJson.isEmpty() && !releaseScreenName.isEmpty()) {
            asyncShowSnapAssist(windowId, releaseScreenName, emptyZonesJson);
        }
    });
}

void PlasmaZonesEffect::callCancelSnap()
{
    qCInfo(lcEffect) << "Calling cancelSnap (drag cancelled by Escape or external event)";
    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
        QStringLiteral("cancelSnap"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PlasmaZonesEffect::tryAsyncSnapCall(QDBusAbstractInterface& iface, const QString& method,
                                          const QList<QVariant>& args,
                                          QPointer<KWin::EffectWindow> window, const QString& windowId,
                                          bool storePreSnap, std::function<void()> fallback,
                                          std::function<void(const QString&, const QString&)> onSnapSuccess)
{
    QDBusPendingCall call = iface.asyncCallWithArgumentList(method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args](
                QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << method << "error:" << reply.error().message();
                    if (fallback) fallback();
                    return;
                }
                if (reply.argumentAt<4>() && window) {
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(),
                             reply.argumentAt<2>(), reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap) ensurePreSnapGeometryStored(window, windowId);
                    applySnapGeometry(window, geo);
                    // args[1] is screenName (e.g. for snapToEmptyZone, snapToLastZone)
                    if (onSnapSuccess && args.size() >= 2) {
                        onSnapSuccess(windowId, args[1].toString());
                    }
                    return;
                }
                if (fallback) fallback();
            });
}

void PlasmaZonesEffect::applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag, int retriesLeft)
{
    if (!window) {
        qCWarning(lcEffect) << "Cannot apply geometry - window is null";
        return;
    }

    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "Cannot apply geometry - geometry is invalid";
        return;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546.
    if (window->isFullScreen()) {
        qCDebug(lcEffect) << "Skipping geometry change - window is fullscreen";
        return;
    }

    qCDebug(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geometry;

    // In KWin 6, we use the window's moveResize methods
    // When allowDuringDrag is false: defer if window is in user move/resize (snap on release)
    // When allowDuringDrag is true: apply immediately (FancyZones-style during drag)
    if (!allowDuringDrag && (window->isUserMove() || window->isUserResize())) {
        if (retriesLeft <= 0) {
            qCWarning(lcEffect) << "Giving up snap geometry — window still in user move after max retries";
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
        QTimer::singleShot(100, this, [this, safeWindow, geometry, retriesLeft]() {
            if (safeWindow && !safeWindow->isFullScreen()) {
                applySnapGeometry(safeWindow, geometry, false, retriesLeft - 1);
            }
        });
        return;
    }

    // KWin 6: EffectWindow exposes window() which returns the underlying Window*
    // Window has moveResize(const QRectF &geometry) method
    KWin::Window* kwinWindow = window->window();
    if (kwinWindow) {
        qCDebug(lcEffect) << "Using Window::moveResize() directly";
        kwinWindow->moveResize(QRectF(geometry));
    } else {
        qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
    }
}

QString PlasmaZonesEffect::extractStableId(const QString& windowId)
{
    // Window ID format: "windowClass:resourceName:pointerAddress"
    // Stable ID: "windowClass:resourceName" (without pointer address)
    // This allows matching windows across KWin restarts since only the pointer changes

    // Find the last colon (separates pointer address from the rest)
    int lastColon = windowId.lastIndexOf(QLatin1Char(':'));
    if (lastColon <= 0) {
        // No colon found or only one part - return as-is
        return windowId;
    }

    // Check if what's after the last colon looks like a pointer address (all digits)
    QString potentialPointer = windowId.mid(lastColon + 1);
    bool isPointer = !potentialPointer.isEmpty();
    for (const QChar& c : potentialPointer) {
        if (!c.isDigit()) {
            isPointer = false;
            break;
        }
    }

    if (isPointer) {
        // Strip the pointer address
        return windowId.left(lastColon);
    }

    // Not a pointer format, return as-is
    return windowId;
}

QString PlasmaZonesEffect::deriveShortNameFromWindowClass(const QString& windowClass)
{
    if (windowClass.isEmpty()) {
        return QString();
    }
    int spaceIdx = windowClass.indexOf(QLatin1Char(' '));
    if (spaceIdx > 0) {
        return windowClass.left(spaceIdx);
    }
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

    // Extract all window info upfront — the EffectWindow may be partially
    // destroyed during slotWindowClosed, so read everything before any early return.
    const QString windowId = getWindowId(w);
    const QString screenName = getWindowScreenName(w);

    // If we haven't notified the daemon about this window yet, record the close
    // so we can suppress the open if it arrives late (D-Bus ordering race)
    if (!m_notifiedWindows.contains(windowId) && m_autotileScreens.contains(screenName)) {
        m_pendingCloses.insert(windowId);
    }

    // Remove from autotile tracking set so re-opened windows get re-notified.
    // This must happen regardless of whether the WindowTracking interface is up.
    m_notifiedWindows.remove(windowId);

    // Notify autotile daemon (uses its own D-Bus path, independent of WindowTracking)
    if (m_autotileScreens.contains(screenName)) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName,
            DBus::ObjectPath,
            DBus::Interface::Autotile,
            QStringLiteral("windowClosed"));
        msg << windowId;

        QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
        auto *watcher = new QDBusPendingCallWatcher(pending, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [windowId](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcEffect) << "windowClosed D-Bus call failed for" << windowId << ":" << w->error().message();
            }
        });
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenName;
    }

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
    if (m_autotileScreens.contains(screenName)) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath,
            DBus::Interface::Autotile,
            QStringLiteral("notifyWindowFocused"));
        msg << windowId << screenName;
        QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
        auto *watcher = new QDBusPendingCallWatcher(pending, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [windowId](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcEffect) << "notifyWindowFocused D-Bus call failed for" << windowId << ":" << w->error().message();
            }
        });
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Autotile integration
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::notifyWindowAdded(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w)) {
        return;
    }

    const QString windowId = getWindowId(w);

    // Window was already closed before we could notify open — skip (D-Bus ordering race)
    if (m_pendingCloses.remove(windowId)) {
        return;
    }

    if (m_notifiedWindows.contains(windowId)) {
        return;
    }
    m_notifiedWindows.insert(windowId);

    // Include the screen name so the daemon knows which monitor layout to use
    QString screenName = getWindowScreenName(w);

    // Only notify autotile daemon for windows on autotile screens
    if (m_autotileScreens.contains(screenName)) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName,
            DBus::ObjectPath,
            DBus::Interface::Autotile,
            QStringLiteral("windowOpened"));
        msg << windowId;
        msg << screenName;

        QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
        auto *watcher = new QDBusPendingCallWatcher(pending, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcEffect) << "windowOpened D-Bus call failed for" << windowId << ":" << w->error().message();
                m_notifiedWindows.remove(windowId);
            }
        });
        qCDebug(lcEffect) << "Notified autotile: windowOpened" << windowId << "on screen" << screenName;
    }
}

void PlasmaZonesEffect::connectAutotileSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    bus.connect(
        DBus::ServiceName,
        DBus::ObjectPath,
        DBus::Interface::Autotile,
        QStringLiteral("windowTileRequested"),
        this,
        SLOT(slotAutotileWindowRequested(QString, int, int, int, int)));

    bus.connect(
        DBus::ServiceName,
        DBus::ObjectPath,
        DBus::Interface::Autotile,
        QStringLiteral("focusWindowRequested"),
        this,
        SLOT(slotAutotileFocusWindowRequested(QString)));

    bus.connect(
        DBus::ServiceName,
        DBus::ObjectPath,
        DBus::Interface::Autotile,
        QStringLiteral("monocleVisibilityChanged"),
        this,
        SLOT(slotMonocleVisibilityChanged(QString, QStringList)));

    bus.connect(
        DBus::ServiceName,
        DBus::ObjectPath,
        DBus::Interface::Autotile,
        QStringLiteral("enabledChanged"),
        this,
        SLOT(slotAutotileEnabledChanged(bool)));

    bus.connect(
        DBus::ServiceName,
        DBus::ObjectPath,
        DBus::Interface::Autotile,
        QStringLiteral("autotileScreensChanged"),
        this,
        SLOT(slotAutotileScreensChanged(QStringList)));

    qCInfo(lcEffect) << "Connected to autotile D-Bus signals";
}

void PlasmaZonesEffect::loadAutotileSettings()
{
    // Query initial autotile screen set from daemon asynchronously.
    // After this, we track changes via the autotileScreensChanged D-Bus signal.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBus::ServiceName, DBus::ObjectPath,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg << DBus::Interface::Autotile << QStringLiteral("autotileScreens");

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isValid()) {
            QStringList screens = reply.value().variant().toStringList();
            // All screens in the initial load are "added" (old set was empty or stale)
            const QSet<QString> added(screens.begin(), screens.end());
            m_autotileScreens = added;
            qCInfo(lcEffect) << "Loaded autotile screens:" << m_autotileScreens;

            // Save pre-autotile geometries and re-notify windows on added screens
            // (they may have been skipped because m_autotileScreens was empty at startup)
            if (!added.isEmpty()) {
                const auto windows = KWin::effects->stackingOrder();
                for (KWin::EffectWindow* win : windows) {
                    if (win && shouldHandleWindow(win)) {
                        const QString screenName = getWindowScreenName(win);
                        if (added.contains(screenName)) {
                            const QString windowId = getWindowId(win);
                            m_preAutotileGeometries[screenName][windowId] = win->frameGeometry();
                            m_notifiedWindows.remove(windowId); // Allow re-notification
                            notifyWindowAdded(win);
                        }
                    }
                }
            }
        } else {
            qCDebug(lcEffect) << "Could not query autotile screens - daemon may not be running";
        }
    });
}

void PlasmaZonesEffect::slotAutotileEnabledChanged(bool enabled)
{
    // enabledChanged is still emitted for backward compat; the real state
    // is tracked via autotileScreensChanged. Just log for diagnostics.
    qCInfo(lcEffect) << "Autotile enabled state changed:" << enabled;
}

void PlasmaZonesEffect::slotAutotileScreensChanged(const QStringList& screenNames)
{
    const QSet<QString> newScreens(screenNames.begin(), screenNames.end());
    const QSet<QString> removed = m_autotileScreens - newScreens;
    const QSet<QString> added = newScreens - m_autotileScreens;

    // Single pass: handle both removed and added screens in one stacking order scan
    const auto windows = KWin::effects->stackingOrder();

    if (!removed.isEmpty()) {
        // Clear notified windows for screens that left autotile so they
        // get re-notified if the screen is later re-added.
        QSet<QString> windowsOnRemovedScreens;
        for (KWin::EffectWindow* w : windows) {
            if (w && removed.contains(getWindowScreenName(w))) {
                windowsOnRemovedScreens.insert(getWindowId(w));
            }
        }
        m_notifiedWindows -= windowsOnRemovedScreens;

        // Restore pre-autotile geometries for windows on removed screens.
        // This covers windows that weren't snapped to zones before autotile
        // (the daemon's resnapCurrentAssignments handles zone-snapped windows).
        for (const QString& screenName : removed) {
            const auto savedIt = m_preAutotileGeometries.constFind(screenName);
            if (savedIt == m_preAutotileGeometries.constEnd()) {
                continue;
            }
            const QHash<QString, QRectF>& savedGeometries = savedIt.value();
            for (KWin::EffectWindow* w : windows) {
                if (!w || !shouldHandleWindow(w) || getWindowScreenName(w) != screenName) {
                    continue;
                }
                const QString windowId = getWindowId(w);
                const auto geoIt = savedGeometries.constFind(windowId);
                if (geoIt == savedGeometries.constEnd()) {
                    continue;
                }
                const QRectF& savedGeo = geoIt.value();
                if (savedGeo.isValid() && QRectF(w->frameGeometry()) != savedGeo) {
                    qCInfo(lcEffect) << "Restoring pre-autotile geometry for" << windowId
                                     << "from" << w->frameGeometry() << "to" << savedGeo;
                    applySnapGeometry(w, savedGeo.toRect());
                }
            }
            m_preAutotileGeometries.remove(screenName);
        }
    }

    // Update m_autotileScreens BEFORE the added-screens loop so that
    // notifyWindowAdded()'s m_autotileScreens.contains() check sees the
    // new screens. Without this, windows on newly-added autotile screens
    // would silently skip the windowOpened D-Bus call.
    m_autotileScreens = newScreens;

    // Save pre-autotile geometries for windows on newly added screens
    if (!added.isEmpty()) {
        for (KWin::EffectWindow* w : windows) {
            if (!w || !shouldHandleWindow(w)) {
                continue;
            }
            const QString screenName = getWindowScreenName(w);
            if (!added.contains(screenName)) {
                continue;
            }
            const QString windowId = getWindowId(w);
            m_preAutotileGeometries[screenName][windowId] = w->frameGeometry();

            // Re-notify windows on added screens (they may have been skipped
            // because m_autotileScreens was empty at startup)
            m_notifiedWindows.remove(windowId);
            notifyWindowAdded(w);
        }
        qCInfo(lcEffect) << "Saved pre-autotile geometries for screens:" << added;
    }

    qCInfo(lcEffect) << "Autotile screens changed:" << m_autotileScreens;
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    // Single-pass lookup: check both exact ID and stable ID (minus pointer suffix)
    // in one scan of the stacking order, avoiding a second O(n) fallback pass.
    const QString targetStableId = extractStableId(windowId);
    KWin::EffectWindow* stableMatch = nullptr;

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (wId == windowId) {
            return w; // Exact match — return immediately
        }
        if (!stableMatch && !targetStableId.isEmpty() && extractStableId(wId) == targetStableId) {
            stableMatch = w; // Remember first stable match, keep scanning for exact
        }
    }

    return stableMatch;
}

void PlasmaZonesEffect::slotAutotileWindowRequested(const QString& windowId, int x, int y, int width, int height)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        // R7 fix: Upgrade from debug to warning — a tile request for a missing
        // window indicates a tracking inconsistency between daemon and effect
        qCWarning(lcEffect) << "Autotile: window not found for tile request:" << windowId;
        return;
    }

    QRect targetGeometry(x, y, width, height);
    applyAutotileGeometry(w, targetGeometry);
}

void PlasmaZonesEffect::slotAutotileFocusWindowRequested(const QString& windowId)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "Autotile: window not found for focus request:" << windowId;
        return;
    }

    KWin::effects->activateWindow(w);
}

void PlasmaZonesEffect::slotMonocleVisibilityChanged(const QString& focusedWindowId,
                                                       const QStringList& windowsToHide)
{
    // Unminimize the focused window
    KWin::EffectWindow* focusedW = findWindowById(focusedWindowId);
    if (focusedW && shouldHandleWindow(focusedW)) {
        KWin::Window* kw = focusedW->window();
        if (kw && focusedW->isMinimized()) {
            kw->setMinimized(false);
            qCDebug(lcEffect) << "Monocle: unminimized focused window" << focusedWindowId;
        }
    }

    // Minimize all other tiled windows
    for (const QString& windowId : windowsToHide) {
        KWin::EffectWindow* w = findWindowById(windowId);
        if (!w || !shouldHandleWindow(w)) {
            continue;
        }
        KWin::Window* kw = w->window();
        if (kw && !w->isMinimized()) {
            kw->setMinimized(true);
            qCDebug(lcEffect) << "Monocle: minimized window" << windowId;
        }
    }
}

void PlasmaZonesEffect::applyAutotileGeometry(KWin::EffectWindow* w, const QRect& geometry)
{
    if (!w || geometry.isEmpty()) {
        return;
    }

    // Reuse the existing applySnapGeometry infrastructure which handles:
    // - Fullscreen window safety checks
    // - Deferred application when window is in user move/resize
    // - Direct KWin::Window::moveResize() call
    applySnapGeometry(w, geometry);
}

void PlasmaZonesEffect::prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                                        std::chrono::milliseconds presentTime)
{
    if (m_windowAnimator->hasAnimation(w)) {
        // Mark window as transformed so paintWindow gets called
        data.setTransformed();
    }

    KWin::effects->prePaintWindow(view, w, data, presentTime);

    // Post-paint logic: animation completion and repaint requests (KWin has no postPaintWindow)
    if (w && m_windowAnimator->hasAnimation(w)) {
        if (m_windowAnimator->isAnimationComplete(w)) {
            // Animation finished - apply final geometry and clean up
            QRect finalGeometry = m_windowAnimator->finalGeometry(w);
            m_windowAnimator->removeAnimation(w);

            qCDebug(lcEffect) << "Window animation complete, applying final geometry:" << finalGeometry;
            applySnapGeometry(w, finalGeometry);
        } else {
            // Animation still running - request another repaint
            w->addRepaintFull();
        }
    }
}

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget,
                                     const KWin::RenderViewport& viewport,
                                     KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                     KWin::WindowPaintData& data)
{
    // Apply animation transform if window is being animated
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
