// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "navigationhandler.h"
#include "windowanimator.h"
#include "dragtracker.h"
#include "dbus_constants.h"

#include <window.h>
#include <effect/effectwindow.h>
#include <effect/globals.h> // For ElectricBorder enum
#include <core/output.h> // For Output::name() for multi-monitor support

#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
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
 */
template<typename InterfacePtr>
static void ensureInterface(InterfacePtr& interface, const QString& interfaceName, const char* logName)
{
    if (interface && interface->isValid()) {
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
    return QRect(obj[QStringLiteral("x")].toInt(), obj[QStringLiteral("y")].toInt(),
                 obj[QStringLiteral("width")].toInt(), obj[QStringLiteral("height")].toInt());
}

QString PlasmaZonesEffect::queryZoneForWindow(const QString& windowId)
{
    if (!ensureWindowTrackingReady("query zone for window")) {
        return QString();
    }

    // NOTE: This remains synchronous because it's used in user-triggered navigation
    // handlers that need the result immediately. The latency is acceptable since
    // it only blocks during user actions, not during startup.
    // For startup paths, use async alternatives with callbacks.
    QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getZoneForWindow"), windowId);
    if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
        return msg.arguments().at(0).toString();
    }
    return QString();
}

void PlasmaZonesEffect::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!ensureWindowTrackingReady("ensure pre-snap geometry")) {
        return;
    }

    // Use ASYNC D-Bus call to check if geometry exists, then store if not
    // Use QPointer to safely handle window destruction during async call
    QPointer<KWin::EffectWindow> safeWindow = w;
    QString capturedWindowId = windowId;

    QDBusPendingCall pendingCall = m_windowTrackingInterface->asyncCall(QStringLiteral("hasPreSnapGeometry"), windowId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safeWindow, capturedWindowId](QDBusPendingCallWatcher* watcher) {
        watcher->deleteLater();

        QDBusPendingReply<bool> reply = *watcher;
        bool hasGeometry = reply.isValid() && reply.value();

        if (!hasGeometry && safeWindow && m_windowTrackingInterface && m_windowTrackingInterface->isValid()) {
            QRectF currentGeom = safeWindow->frameGeometry();
            m_windowTrackingInterface->asyncCall(QStringLiteral("storePreSnapGeometry"), capturedWindowId,
                                                 static_cast<int>(currentGeom.x()), static_cast<int>(currentGeom.y()),
                                                 static_cast<int>(currentGeom.width()),
                                                 static_cast<int>(currentGeom.height()));
            qCDebug(lcEffect) << "Stored pre-snap geometry for window" << capturedWindowId;
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

bool PlasmaZonesEffect::isWindowFloating(const QString& stableId) const
{
    return m_navigationHandler->isWindowFloating(stableId);
}

PlasmaZonesEffect::PlasmaZonesEffect()
    : Effect()
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
    , m_windowAnimator(std::make_unique<WindowAnimator>(this))
    , m_dragTracker(std::make_unique<DragTracker>(this))
{
    // Connect DragTracker signals
    connect(m_dragTracker.get(), &DragTracker::dragStarted, this,
            [this](KWin::EffectWindow* w, const QString& windowId, const QRectF& geometry) {
                qCDebug(lcEffect) << "Window move started -" << w->windowClass()
                                  << "current modifiers:" << static_cast<int>(m_currentModifiers);
                callDragStarted(windowId, geometry);
            });
    connect(m_dragTracker.get(), &DragTracker::dragMoved, this,
            [this](const QString& windowId, const QPointF& cursorPos) {
                callDragMoved(windowId, cursorPos, m_currentModifiers);
            });
    connect(m_dragTracker.get(), &DragTracker::dragStopped, this,
            [this](KWin::EffectWindow* w, const QString& windowId) {
                callDragStopped(w, windowId);
            });

    // Connect to window lifecycle signals
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &PlasmaZonesEffect::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &PlasmaZonesEffect::slotWindowClosed);

    // Phase 2.1: Connect to window activation for autotiling focus tracking
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
    qCDebug(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";

    // Connect to keyboard navigation D-Bus signals (Phase 1)
    connectNavigationSignals();

    // Connect to autotile D-Bus signals (Phase 2.3)
    connectAutotileSignals();

    // Load autotile settings from daemon (Phase 2.3)
    loadAutotileSettings();

    // Sync floating window state from daemon's persisted state
    syncFloatingWindowsFromDaemon();

    // Load exclusion settings from daemon
    loadExclusionSettings();

    // Setup polling timer for detecting window moves
    // In KWin 6, we poll windows to check isUserMove() state
    m_pollTimer.setInterval(50); // 20 Hz update rate
    connect(&m_pollTimer, &QTimer::timeout, this, &PlasmaZonesEffect::pollWindowMoves);

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

    // Start polling
    m_pollTimer.start();

    // The daemon disables KWin's Quick Tile via kwriteconfig6. We don't reserve electric borders
    // here because that would turn on the edge effect visually; the daemon's config approach
    // is the right way to prevent Quick Tile from activating.

    qCDebug(lcEffect) << "Initialized - C++ effect with D-Bus support and mouseChanged connection";
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    m_pollTimer.stop();
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

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // Phase 2.1: Notify daemon for autotiling (before auto-snap logic)
    notifyWindowAdded(w);

    // Sync floating state for this window from daemon
    // This ensures windows that were floating when closed remain floating when reopened
    QString windowId = getWindowId(w);
    QString stableId = extractStableId(windowId);
    m_navigationHandler->syncFloatingStateForWindow(stableId);

    // Check if we should auto-snap new windows to last used zone
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    if (shouldAutoSnapWindow(w) && !w->isMinimized()) {
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
    // Delegate to helpers
    m_dragTracker->handleWindowClosed(w);

    // NOTE: Don't clear floating state here - it should persist across window close/reopen
    // The daemon preserves floating state (keyed by stableId) so the window stays floating
    // when reopened. The effect's local cache will be synced in slotWindowAdded().

    // Clean up any pending autotile animations
    m_windowAnimator->removeAnimation(w);

    // Phase 2.1: Notify daemon for autotiling and cleanup
    notifyWindowClosed(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Phase 2.1: Notify daemon of window activation for autotiling focus tracking
    // Filtering handled by notifyWindowActivated
    notifyWindowActivated(w);
}

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);
    });

    // Connect to geometry changes for this window
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
            [this, w](KWin::EffectWindow* window, const QRectF& oldGeometry) {
                Q_UNUSED(oldGeometry)
                if (window == m_dragTracker->draggedWindow()) {
                    // Skip if window has gone fullscreen - don't track fullscreen windows
                    // This prevents sending drag updates during fullscreen transitions
                    // which could interfere with games entering fullscreen mode
                    if (window->isFullScreen()) {
                        qCDebug(lcEffect) << "Window went fullscreen during drag, stopping tracking";
                        m_dragTracker->reset();
                        return;
                    }
                    // Window geometry changed during drag - send update
                    // Use tracked modifiers from mouseChanged signal
                    QPointF cursorPos = KWin::effects->cursorPos();
                    callDragMoved(m_dragTracker->draggedWindowId(), cursorPos, m_currentModifiers);
                }
            });
}

void PlasmaZonesEffect::slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                                         Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                                         Qt::KeyboardModifiers oldmodifiers)
{
    Q_UNUSED(pos)
    Q_UNUSED(oldpos)
    Q_UNUSED(buttons)
    Q_UNUSED(oldbuttons)
    Q_UNUSED(oldmodifiers)

    // Track the current keyboard modifiers from KWin's input system
    // This is called whenever the mouse moves or modifiers change
    if (m_currentModifiers != modifiers) {
        m_currentModifiers = modifiers;
        qCDebug(lcEffect) << "Modifiers changed to" << static_cast<int>(modifiers);

        // If we're currently dragging, send an update with the new modifiers
        if (m_dragTracker->isDragging()) {
            QPointF cursorPos = KWin::effects->cursorPos();
            callDragMoved(m_dragTracker->draggedWindowId(), cursorPos, m_currentModifiers);
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
    qCDebug(lcEffect) << "virtualScreenGeometryChanged fired"
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
    qCDebug(lcEffect) << "Applying debounced screen geometry change"
                      << "- previous:" << m_lastVirtualScreenGeometry << "- current:" << currentGeometry;

    m_pendingScreenChange = false;
    m_lastVirtualScreenGeometry = currentGeometry;

    // Call daemon to get updated window geometries
    if (!ensureWindowTrackingReady("get updated window geometries")) {
        return;
    }

    // Use ASYNC D-Bus call to avoid blocking the compositor thread
    QDBusPendingCall pendingCall = m_windowTrackingInterface->asyncCall(QStringLiteral("getUpdatedWindowGeometries"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcEffect) << "No window geometries to update";
            return;
        }

        QString geometriesJson = reply.value();
        if (geometriesJson.isEmpty() || geometriesJson == QStringLiteral("[]")) {
            qCDebug(lcEffect) << "Empty geometries list from daemon";
            return;
        }

        // Parse JSON array of window geometries
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(geometriesJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            qCWarning(lcEffect) << "Failed to parse window geometries:" << parseError.errorString();
            return;
        }

        QJsonArray geometries = doc.array();
        qCDebug(lcEffect) << "Repositioning" << geometries.size() << "windows after resolution change";

        // Build a map of stableId -> EffectWindow for faster lookup
        // Using stable IDs ensures windows can be found after session restore
        // Note: We don't filter handleable here since we want ALL windows for geometry updates
        QHash<QString, KWin::EffectWindow*> windowMap = buildWindowMap(false);

        // Apply new geometries to windows
        for (const QJsonValue& value : geometries) {
            if (!value.isObject()) {
                continue;
            }

            QJsonObject obj = value.toObject();
            QString windowId = obj[QStringLiteral("windowId")].toString();
            QString stableId = extractStableId(windowId);
            int x = obj[QStringLiteral("x")].toInt();
            int y = obj[QStringLiteral("y")].toInt();
            int width = obj[QStringLiteral("width")].toInt();
            int height = obj[QStringLiteral("height")].toInt();

            KWin::EffectWindow* window = windowMap.value(stableId);
            if (window && shouldHandleWindow(window)) {
                QRect newGeometry(x, y, width, height);

                // Only apply if the geometry actually changed
                QRectF currentWindowGeometry = window->frameGeometry();
                if (QRect(currentWindowGeometry.toRect()) != newGeometry) {
                    qCDebug(lcEffect) << "Repositioning window" << windowId << "from" << currentWindowGeometry << "to"
                                      << newGeometry;
                    applySnapGeometry(window, newGeometry);
                } else {
                    qCDebug(lcEffect) << "Window" << windowId << "already at target geometry, skipping";
                }
            }
        }
    });
}

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCDebug(lcEffect) << "Daemon signaled settingsChanged - reloading settings";
    loadExclusionSettings();
    loadAutotileSettings();
}

void PlasmaZonesEffect::pollWindowMoves()
{
    // Delegate to DragTracker
    m_dragTracker->pollWindowMoves();
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

void PlasmaZonesEffect::ensureDBusInterface()
{
    ensureInterface(m_dbusInterface, DBus::Interface::WindowDrag, "WindowDrag");
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

void PlasmaZonesEffect::syncFloatingWindowsFromDaemon()
{
    // Delegate to NavigationHandler
    m_navigationHandler->syncFloatingWindowsFromDaemon();
}

void PlasmaZonesEffect::loadExclusionSettings()
{
    // Set sensible defaults immediately - don't block compositor startup waiting for daemon
    // These will be updated asynchronously when the daemon responds
    m_excludeTransientWindows = true;
    m_minimumWindowWidth = 200;
    m_minimumWindowHeight = 150;

    // Create interface for async calls
    auto* settingsInterface = new QDBusInterface(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                                  QDBusConnection::sessionBus(), this);

    if (!settingsInterface->isValid()) {
        qCDebug(lcEffect) << "Cannot load exclusion settings - daemon not available yet, using defaults";
        settingsInterface->deleteLater();
        return;
    }

    // Load settings asynchronously to avoid blocking KWin startup
    // Each setting is loaded independently so partial failures don't block others

    // Load excludeTransientWindows
    QDBusPendingCall excludeCall = settingsInterface->asyncCall(QStringLiteral("getSetting"), QStringLiteral("excludeTransientWindows"));
    auto* excludeWatcher = new QDBusPendingCallWatcher(excludeCall, this);
    connect(excludeWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            QVariant value = reply.value();
            if (value.canConvert<QDBusVariant>()) {
                m_excludeTransientWindows = value.value<QDBusVariant>().variant().toBool();
            } else {
                m_excludeTransientWindows = value.toBool();
            }
            qCDebug(lcEffect) << "Loaded excludeTransientWindows:" << m_excludeTransientWindows;
        }
    });

    // Load minimumWindowWidth
    QDBusPendingCall widthCall = settingsInterface->asyncCall(QStringLiteral("getSetting"), QStringLiteral("minimumWindowWidth"));
    auto* widthWatcher = new QDBusPendingCallWatcher(widthCall, this);
    connect(widthWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            QVariant value = reply.value();
            if (value.canConvert<QDBusVariant>()) {
                m_minimumWindowWidth = value.value<QDBusVariant>().variant().toInt();
            } else {
                m_minimumWindowWidth = value.toInt();
            }
            qCDebug(lcEffect) << "Loaded minimumWindowWidth:" << m_minimumWindowWidth;
        }
    });

    // Load minimumWindowHeight
    QDBusPendingCall heightCall = settingsInterface->asyncCall(QStringLiteral("getSetting"), QStringLiteral("minimumWindowHeight"));
    auto* heightWatcher = new QDBusPendingCallWatcher(heightCall, this);
    connect(heightWatcher, &QDBusPendingCallWatcher::finished, this, [this, settingsInterface](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            QVariant value = reply.value();
            if (value.canConvert<QDBusVariant>()) {
                m_minimumWindowHeight = value.value<QDBusVariant>().variant().toInt();
            } else {
                m_minimumWindowHeight = value.toInt();
            }
            qCDebug(lcEffect) << "Loaded minimumWindowHeight:" << m_minimumWindowHeight;
        }
        // Clean up the interface after the last setting is loaded
        settingsInterface->deleteLater();
    });

    qCDebug(lcEffect) << "Loading exclusion settings asynchronously, using defaults until loaded";
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
                                          QStringLiteral("cycleWindowsInZoneRequested"), this,
                                          SLOT(slotCycleWindowsInZoneRequested(QString, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("pendingRestoresAvailable"), this,
                                          SLOT(slotPendingRestoresAvailable()));

    // Connect to floating state changes to keep local cache in sync
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("windowFloatingChanged"), this,
                                          SLOT(slotWindowFloatingChanged(QString, bool)));

    qCDebug(lcEffect) << "Connected to keyboard navigation D-Bus signals";
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

QString PlasmaZonesEffect::queryAdjacentZone(const QString& currentZoneId, const QString& direction)
{
    ensureZoneDetectionInterface();
    if (!m_zoneDetectionInterface || !m_zoneDetectionInterface->isValid()) {
        return QString();
    }

    // NOTE: This remains synchronous because it's used in user-triggered navigation
    // handlers that need the result immediately for decision making.
    // The latency is acceptable since it only blocks during keyboard navigation,
    // not during startup or window lifecycle events.
    QDBusMessage msg = m_zoneDetectionInterface->call(QStringLiteral("getAdjacentZone"), currentZoneId, direction);

    if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
        return msg.arguments().at(0).toString();
    }
    return QString();
}

QString PlasmaZonesEffect::queryFirstZoneInDirection(const QString& direction)
{
    ensureZoneDetectionInterface();
    if (!m_zoneDetectionInterface || !m_zoneDetectionInterface->isValid()) {
        return QString();
    }

    // NOTE: This remains synchronous because it's used in user-triggered navigation
    // handlers that need the result immediately for decision making.
    // The latency is acceptable since it only blocks during keyboard navigation,
    // not during startup or window lifecycle events.
    QDBusMessage msg = m_zoneDetectionInterface->call(QStringLiteral("getFirstZoneInDirection"), direction);

    if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
        return msg.arguments().at(0).toString();
    }
    return QString();
}

QString PlasmaZonesEffect::queryZoneGeometry(const QString& zoneId)
{
    return queryZoneGeometryForScreen(zoneId, QString());
}

QString PlasmaZonesEffect::queryZoneGeometryForScreen(const QString& zoneId, const QString& screenName)
{
    if (!ensureWindowTrackingReady("query zone geometry")) {
        return QString();
    }

    // NOTE: This remains synchronous because it's used in user-triggered navigation
    // handlers that need geometry data immediately for window placement.
    // The latency is acceptable since it only blocks during keyboard navigation,
    // not during startup or window lifecycle events.
    // Use the screen-aware version for multi-monitor support
    QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getZoneGeometryForScreen"), zoneId, screenName);

    if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
        return msg.arguments().at(0).toString();
    }
    return QString();
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

void PlasmaZonesEffect::slotCycleWindowsInZoneRequested(const QString& directive, const QString& unused)
{
    m_navigationHandler->handleCycleWindowsInZone(directive, unused);
}

void PlasmaZonesEffect::slotPendingRestoresAvailable()
{
    qCDebug(lcEffect) << "Pending restores available - retrying restoration for all visible windows";

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

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& stableId, bool isFloating)
{
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    qCDebug(lcEffect) << "Floating state changed for" << stableId << "- isFloating:" << isFloating;
    m_navigationHandler->setWindowFloating(stableId, isFloating);
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

    // FIRST: Try to restore to persisted zone from previous session
    // This takes priority over "snap to last zone" to ensure windows return
    // to their original positions after relog, not to the last-used zone.
    QDBusPendingCall restoreCall =
        m_windowTrackingInterface->asyncCall(QStringLiteral("restoreToPersistedZone"), windowId, screenName, sticky);

    QDBusPendingCallWatcher* restoreWatcher = new QDBusPendingCallWatcher(restoreCall, this);
    connect(restoreWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, sticky](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << "restoreToPersistedZone error:" << reply.error().message();
                    // Fall through to snapToLastZone
                } else {
                    int snapX = reply.argumentAt<0>();
                    int snapY = reply.argumentAt<1>();
                    int snapWidth = reply.argumentAt<2>();
                    int snapHeight = reply.argumentAt<3>();
                    bool shouldRestore = reply.argumentAt<4>();

                    if (shouldRestore && safeWindow) {
                        QRect snapGeometry(snapX, snapY, snapWidth, snapHeight);
                        qCDebug(lcEffect)
                            << "Restoring window" << windowId << "to persisted zone geometry:" << snapGeometry;
                        applySnapGeometry(safeWindow, snapGeometry);
                        return; // Successfully restored, don't fall back to snapToLastZone
                    }
                }

                // FALLBACK: No persisted zone found, try "snap to last zone" feature
                if (!safeWindow || !m_windowTrackingInterface || !m_windowTrackingInterface->isValid()) {
                    return;
                }

                // BUG FIX: Pass the window's current screen name to prevent cross-monitor snapping
                // Without this, windows on monitor 2 would snap to zones on monitor 1
                QString windowScreenName = getWindowScreenName(safeWindow);
                QDBusPendingCall snapCall = m_windowTrackingInterface->asyncCall(QStringLiteral("snapToLastZone"),
                                                                                 windowId, windowScreenName, sticky);

                QDBusPendingCallWatcher* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);
                connect(snapWatcher, &QDBusPendingCallWatcher::finished, this,
                        [this, safeWindow, windowId](QDBusPendingCallWatcher* sw) {
                            sw->deleteLater();

                            QDBusPendingReply<int, int, int, int, bool> snapReply = *sw;
                            if (snapReply.isError()) {
                                qCDebug(lcEffect) << "snapToLastZone not applied:" << snapReply.error().message();
                                return;
                            }

                            int snapX = snapReply.argumentAt<0>();
                            int snapY = snapReply.argumentAt<1>();
                            int snapWidth = snapReply.argumentAt<2>();
                            int snapHeight = snapReply.argumentAt<3>();
                            bool shouldSnap = snapReply.argumentAt<4>();

                            if (shouldSnap && safeWindow) {
                                QRect snapGeometry(snapX, snapY, snapWidth, snapHeight);
                                qCDebug(lcEffect) << "Auto-snapping new window" << windowId
                                                  << "to last zone geometry:" << snapGeometry;
                                applySnapGeometry(safeWindow, snapGeometry);
                            }
                        });
            });
}

void PlasmaZonesEffect::callDragStarted(const QString& windowId, const QRectF& geometry)
{
    updateWindowStickyState(m_dragTracker->draggedWindow());
    ensureDBusInterface();
    if (!m_dbusInterface || !m_dbusInterface->isValid()) {
        return;
    }

    // Get window class info for exclusion filtering
    QString appName;
    QString windowClass;
    if (m_dragTracker->draggedWindow()) {
        windowClass = m_dragTracker->draggedWindow()->windowClass();
        // Use windowClass as appName too - KWin effect doesn't have separate appName
        appName = windowClass;
    }

    // D-Bus interface expects doubles for geometry + strings for app info (sddddss signature)
    m_dbusInterface->asyncCall(QStringLiteral("dragStarted"), windowId, geometry.x(), geometry.y(), geometry.width(),
                               geometry.height(), appName, windowClass);
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

void PlasmaZonesEffect::callDragMoved(const QString& windowId, const QPointF& cursorPos, Qt::KeyboardModifiers mods)
{
    ensureDBusInterface();
    if (!m_dbusInterface || !m_dbusInterface->isValid()) {
        return;
    }

    // D-Bus interface expects ints for cursor position and modifiers (siii signature)
    m_dbusInterface->asyncCall(QStringLiteral("dragMoved"), windowId, static_cast<int>(cursorPos.x()),
                               static_cast<int>(cursorPos.y()), static_cast<int>(mods));
}

void PlasmaZonesEffect::callDragStopped(KWin::EffectWindow* window, const QString& windowId)
{
    ensureDBusInterface();
    if (!m_dbusInterface || !m_dbusInterface->isValid()) {
        return;
    }

    // Make ASYNC call to get snap geometry - prevents UI freeze if daemon is slow
    // D-Bus signature: dragStopped(s) -> (iiiib)
    // The method has out parameters: snapX, snapY, snapWidth, snapHeight, shouldSnap
    QDBusPendingCall pendingCall = m_dbusInterface->asyncCall(QStringLiteral("dragStopped"), windowId);

    // Use QPointer to safely handle window destruction during async call
    QPointer<KWin::EffectWindow> safeWindow = window;

    // Create watcher to handle the reply
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safeWindow](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<int, int, int, int, bool> reply = *w;
        if (reply.isError()) {
            qCWarning(lcEffect) << "dragStopped call failed:" << reply.error().message();
            return;
        }

        int snapX = reply.argumentAt<0>();
        int snapY = reply.argumentAt<1>();
        int snapWidth = reply.argumentAt<2>();
        int snapHeight = reply.argumentAt<3>();
        bool shouldSnap = reply.argumentAt<4>();

        qCDebug(lcEffect) << "dragStopped returned shouldSnap=" << shouldSnap
                          << "geometry=" << QRect(snapX, snapY, snapWidth, snapHeight);

        if (shouldSnap && safeWindow) {
            // Final fullscreen check before applying geometry - window could have
            // transitioned to fullscreen between drag stop and this point
            if (safeWindow->isFullScreen()) {
                qCDebug(lcEffect) << "Window is fullscreen at drag stop, skipping snap";
            } else {
                QRect snapGeometry(snapX, snapY, snapWidth, snapHeight);
                applySnapGeometry(safeWindow, snapGeometry);
            }
        }
    });
}

void PlasmaZonesEffect::applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry)
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
    // First, we need to check if the window is in a valid state
    if (window->isUserMove() || window->isUserResize()) {
        qCDebug(lcEffect) << "Window still in user move/resize state, deferring geometry change";
        // Schedule the geometry change for next frame when the move operation is complete
        // Use QPointer to safely handle window destruction during the timer delay
        QPointer<KWin::EffectWindow> safeWindow = window;
        QTimer::singleShot(100, this, [this, safeWindow, geometry]() {
            // Re-check all conditions including fullscreen - window state can change during delay
            // QPointer automatically becomes null if the window is destroyed
            if (safeWindow && !safeWindow->isUserMove() && !safeWindow->isUserResize() && !safeWindow->isFullScreen()) {
                applySnapGeometry(safeWindow, geometry);
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

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 2.1: Window Event Notifications for Autotiling
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::notifyWindowAdded(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w)) {
        return;
    }

    if (!ensureWindowTrackingReady("notify windowAdded")) {
        return;
    }

    QString windowId = getWindowId(w);
    QString screenName = getWindowScreenName(w);

    // Track this window so we only send windowClosed for windows we've notified about
    m_notifiedWindows.insert(windowId);

    qCDebug(lcEffect) << "Notifying daemon: windowAdded" << windowId << "on screen" << screenName;
    m_windowTrackingInterface->asyncCall(QStringLiteral("windowAdded"), windowId, screenName);
}

void PlasmaZonesEffect::notifyWindowClosed(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    QString windowId = getWindowId(w);

    // Only notify for windows we previously notified via windowAdded
    // This avoids unnecessary D-Bus calls for special windows (tooltips, popups, etc.)
    if (!m_notifiedWindows.remove(windowId)) {
        return;
    }

    if (!ensureWindowTrackingReady("notify windowClosed")) {
        return;
    }

    qCDebug(lcEffect) << "Notifying daemon: windowClosed" << windowId;
    m_windowTrackingInterface->asyncCall(QStringLiteral("windowClosed"), windowId);
}

void PlasmaZonesEffect::notifyWindowActivated(KWin::EffectWindow* w)
{
    // Consistent filtering with notifyWindowAdded
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
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 2.3: Autotile Window Geometry Application with Animation
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::connectAutotileSignals()
{
    // Connect to Autotile D-Bus signals for window geometry and focus requests
    QDBusConnection::sessionBus().connect(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile,
        QStringLiteral("windowTileRequested"), this,
        SLOT(slotAutotileWindowRequested(QString, int, int, int, int)));

    QDBusConnection::sessionBus().connect(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile,
        QStringLiteral("focusWindowRequested"), this,
        SLOT(slotAutotileFocusWindowRequested(QString)));

    // Connect to enabledChanged to register existing windows when autotiling is enabled
    QDBusConnection::sessionBus().connect(
        DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile,
        QStringLiteral("enabledChanged"), this,
        SLOT(slotAutotileEnabledChanged(bool)));

    qCDebug(lcEffect) << "Connected to autotile D-Bus signals";
}

void PlasmaZonesEffect::slotAutotileEnabledChanged(bool enabled)
{
    qCDebug(lcEffect) << "Autotile enabled changed:" << enabled;

    if (enabled) {
        // When autotiling is enabled, notify daemon about all existing windows
        // so they can be tiled immediately
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            if (shouldHandleWindow(w) && !w->isMinimized()) {
                notifyWindowAdded(w);
            }
        }
        qCDebug(lcEffect) << "Notified daemon about" << windows.size() << "existing windows for autotiling";
    }
}

void PlasmaZonesEffect::loadAutotileSettings()
{
    // Set sensible defaults immediately - don't block compositor startup waiting for daemon
    m_windowAnimator->setAnimationsEnabled(true);
    m_windowAnimator->setAnimationDuration(150);

    // Create interface for async calls
    auto* settingsInterface = new QDBusInterface(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                                  QDBusConnection::sessionBus(), this);

    if (!settingsInterface->isValid()) {
        qCDebug(lcEffect) << "Cannot load autotile settings - daemon not available yet, using defaults";
        settingsInterface->deleteLater();
        return;
    }

    // Load settings asynchronously to avoid blocking KWin startup

    // Load autotileAnimationsEnabled
    QDBusPendingCall animEnabledCall = settingsInterface->asyncCall(QStringLiteral("getSetting"), QStringLiteral("autotileAnimationsEnabled"));
    auto* animEnabledWatcher = new QDBusPendingCallWatcher(animEnabledCall, this);
    connect(animEnabledWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            QVariant value = reply.value();
            bool enabled;
            if (value.canConvert<QDBusVariant>()) {
                enabled = value.value<QDBusVariant>().variant().toBool();
            } else {
                enabled = value.toBool();
            }
            m_windowAnimator->setAnimationsEnabled(enabled);
            qCDebug(lcEffect) << "Loaded autotileAnimationsEnabled:" << enabled;
        }
    });

    // Load autotileAnimationDuration
    QDBusPendingCall animDurationCall = settingsInterface->asyncCall(QStringLiteral("getSetting"), QStringLiteral("autotileAnimationDuration"));
    auto* animDurationWatcher = new QDBusPendingCallWatcher(animDurationCall, this);
    connect(animDurationWatcher, &QDBusPendingCallWatcher::finished, this, [this, settingsInterface](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            QVariant value = reply.value();
            int duration;
            if (value.canConvert<QDBusVariant>()) {
                duration = value.value<QDBusVariant>().variant().toInt();
            } else {
                duration = value.toInt();
            }
            m_windowAnimator->setAnimationDuration(static_cast<qreal>(duration));
            qCDebug(lcEffect) << "Loaded autotileAnimationDuration:" << duration << "ms";
        }
        // Clean up the interface after the last setting is loaded
        settingsInterface->deleteLater();
    });

    qCDebug(lcEffect) << "Loading autotile settings asynchronously, using defaults until loaded";
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && getWindowId(w) == windowId) {
            return w;
        }
    }
    return nullptr;
}

void PlasmaZonesEffect::slotAutotileWindowRequested(const QString& windowId, int x, int y, int width, int height)
{
    qCDebug(lcEffect) << "Autotile window requested:" << windowId << "geometry:" << QRect(x, y, width, height);

    KWin::EffectWindow* window = findWindowById(windowId);
    if (!window) {
        qCDebug(lcEffect) << "Window not found for autotile:" << windowId;
        return;
    }

    // Skip excluded windows (fullscreen, special, etc.)
    if (!shouldHandleWindow(window)) {
        qCDebug(lcEffect) << "Window excluded from autotile:" << windowId;
        return;
    }

    QRect geometry(x, y, width, height);
    applyAutotileGeometry(window, geometry, m_windowAnimator->animationsEnabled());
}

void PlasmaZonesEffect::slotAutotileFocusWindowRequested(const QString& windowId)
{
    qCDebug(lcEffect) << "Autotile focus requested:" << windowId;

    KWin::EffectWindow* window = findWindowById(windowId);
    if (!window) {
        qCDebug(lcEffect) << "Window not found for focus:" << windowId;
        return;
    }

    // Activate the window
    KWin::effects->activateWindow(window);
}

void PlasmaZonesEffect::applyAutotileGeometry(KWin::EffectWindow* window, const QRect& geometry, bool animate)
{
    if (!window || !geometry.isValid()) {
        return;
    }

    // Don't apply geometry during user interaction - would interfere with drag/resize
    if (window->isUserMove() || window->isUserResize()) {
        qCDebug(lcEffect) << "Window in user operation, skipping autotile";
        return;
    }

    // Don't animate fullscreen windows
    if (window->isFullScreen()) {
        qCDebug(lcEffect) << "Skipping autotile for fullscreen window";
        return;
    }

    QRectF currentGeometry = window->frameGeometry();

    // If not animating or geometry is the same, apply directly
    if (!animate || currentGeometry.toRect() == geometry) {
        m_windowAnimator->removeAnimation(window);
        applySnapGeometry(window, geometry);
        return;
    }

    // Check if already animating to the same target geometry (avoid jitter from rapid signals)
    if (m_windowAnimator->isAnimatingToTarget(window, geometry)) {
        qCDebug(lcEffect) << "Already animating to target geometry, ignoring duplicate";
        return;
    }

    // If already animating to different target, redirect the animation
    if (m_windowAnimator->hasAnimation(window)) {
        m_windowAnimator->redirectAnimation(window, geometry);
        return;
    }

    // Start new animation
    if (m_windowAnimator->startAnimation(window, currentGeometry, geometry)) {
        qCDebug(lcEffect) << "Started autotile animation for window from" << currentGeometry << "to" << geometry;
    }
}

void PlasmaZonesEffect::prePaintWindow(KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                                        std::chrono::milliseconds presentTime)
{
    if (m_windowAnimator->hasAnimation(w)) {
        // Mark window as transformed so paintWindow gets called
        data.setTransformed();
    }

    KWin::effects->prePaintWindow(w, data, presentTime);
}

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget,
                                     const KWin::RenderViewport& viewport,
                                     KWin::EffectWindow* w, int mask, QRegion region,
                                     KWin::WindowPaintData& data)
{
    // Apply animation transform if window is being animated
    m_windowAnimator->applyTransform(w, data);

    KWin::effects->paintWindow(renderTarget, viewport, w, mask, region, data);
}

void PlasmaZonesEffect::postPaintWindow(KWin::EffectWindow* w)
{
    // Safety check - window could be destroyed during paint cycle
    if (!w) {
        KWin::effects->postPaintWindow(w);
        return;
    }

    if (m_windowAnimator->hasAnimation(w)) {
        if (m_windowAnimator->isAnimationComplete(w)) {
            // Animation finished - apply final geometry and clean up
            QRect finalGeometry = m_windowAnimator->finalGeometry(w);
            m_windowAnimator->removeAnimation(w);

            qCDebug(lcEffect) << "Autotile animation complete, applying final geometry:" << finalGeometry;
            applySnapGeometry(w, finalGeometry);
        } else {
            // Animation still running - request another repaint
            w->addRepaintFull();
        }
    }

    KWin::effects->postPaintWindow(w);
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
