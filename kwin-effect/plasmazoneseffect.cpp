// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
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

// Navigation directive prefix constant - used for keyboard navigation commands
static const QString NavigateDirectivePrefix = QStringLiteral("navigate:");

// ═══════════════════════════════════════════════════════════════════════════════
// Template helpers to reduce code duplication (DRY principle)
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

/**
 * @brief Load a setting from D-Bus with proper QDBusVariant unwrapping
 * @tparam T The type to convert the setting to
 * @param settingsInterface Reference to the settings D-Bus interface
 * @param key The setting key name
 * @param defaultValue Default value if the setting can't be loaded
 * @return The setting value or defaultValue on failure
 *
 * Consolidates the repeated pattern of calling getSetting, checking the reply,
 * and unwrapping QDBusVariant if needed.
 */
template<typename T>
static T loadDBusSetting(QDBusInterface& settingsInterface, const QString& key, T defaultValue)
{
    if (!settingsInterface.isValid()) {
        return defaultValue;
    }

    QDBusMessage reply = settingsInterface.call(QStringLiteral("getSetting"), key);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return defaultValue;
    }

    QVariant value = reply.arguments().at(0);
    if (value.canConvert<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant().value<T>();
    }
    return value.value<T>();
}

PlasmaZonesEffect::PlasmaZonesEffect()
    : Effect()
{
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
    return m_draggedWindow != nullptr;
}

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // Phase 2.1: Notify daemon for autotiling (before auto-snap logic)
    notifyWindowAdded(w);

    // Check if we should auto-snap new windows to last used zone
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    if (shouldAutoSnapWindow(w) && !w->isMinimized()) {
        // Use QPointer to safely handle window destruction during the delay
        // Raw pointer capture would become dangling if window closes before timer fires
        QPointer<KWin::EffectWindow> safeWindow = w;
        QTimer::singleShot(100, this, [this, safeWindow]() {
            if (safeWindow && shouldAutoSnapWindow(safeWindow)) {
                callSnapToLastZone(safeWindow);
            }
        });
    }
}

void PlasmaZonesEffect::slotWindowClosed(KWin::EffectWindow* w)
{
    if (w == m_draggedWindow) {
        // Window being dragged was closed
        m_draggedWindow = nullptr;
        m_draggedWindowId.clear();
    }

    // Clean up floating window tracking using stable ID
    QString windowId = getWindowId(w);
    QString stableId = extractStableId(windowId);
    m_floatingWindows.remove(stableId);

    // Phase 2.1: Notify daemon for autotiling and cleanup
    notifyWindowClosed(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Phase 2.1: Notify daemon of window activation for autotiling focus tracking
    // Filtering is handled internally by notifyWindowActivated (SRP)
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
                if (window == m_draggedWindow) {
                    // Skip if window has gone fullscreen - don't track fullscreen windows
                    // This prevents sending drag updates during fullscreen transitions
                    // which could interfere with games entering fullscreen mode
                    if (window->isFullScreen()) {
                        qCDebug(lcEffect) << "Window went fullscreen during drag, stopping tracking";
                        m_draggedWindow = nullptr;
                        m_draggedWindowId.clear();
                        return;
                    }
                    // Window geometry changed during drag - send update
                    // Use tracked modifiers from mouseChanged signal
                    QPointF cursorPos = KWin::effects->cursorPos();
                    callDragMoved(m_draggedWindowId, cursorPos, m_currentModifiers);
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
        if (m_draggedWindow) {
            QPointF cursorPos = KWin::effects->cursorPos();
            callDragMoved(m_draggedWindowId, cursorPos, m_currentModifiers);
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

    QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getUpdatedWindowGeometries"));

    if (msg.type() != QDBusMessage::ReplyMessage || msg.arguments().isEmpty()) {
        qCDebug(lcEffect) << "No window geometries to update";
        return;
    }

    QString geometriesJson = msg.arguments().at(0).toString();
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

    // Build a map of windowId -> EffectWindow for faster lookup
    QHash<QString, KWin::EffectWindow*> windowMap;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w) {
            windowMap[getWindowId(w)] = w;
        }
    }

    // Apply new geometries to windows
    for (const QJsonValue& value : geometries) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject obj = value.toObject();
        QString windowId = obj[QStringLiteral("windowId")].toString();
        int x = obj[QStringLiteral("x")].toInt();
        int y = obj[QStringLiteral("y")].toInt();
        int width = obj[QStringLiteral("width")].toInt();
        int height = obj[QStringLiteral("height")].toInt();

        KWin::EffectWindow* window = windowMap.value(windowId);
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
}

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCDebug(lcEffect) << "Daemon signaled settingsChanged - reloading exclusion settings";
    loadExclusionSettings();
}

void PlasmaZonesEffect::pollWindowMoves()
{
    // Check all windows for user move state
    const auto windows = KWin::effects->stackingOrder();

    KWin::EffectWindow* movingWindow = nullptr;

    for (KWin::EffectWindow* w : windows) {
        if (w && w->isUserMove() && !w->isUserResize() && shouldHandleWindow(w)) {
            movingWindow = w;
            break;
        }
    }

    // Detect start of drag
    if (movingWindow && !m_draggedWindow) {
        m_draggedWindow = movingWindow;
        m_draggedWindowId = getWindowId(movingWindow);
        m_lastCursorPos = KWin::effects->cursorPos();

        // Note: Screen edges are now reserved preemptively at effect initialization
        // to prevent the race condition where Quick Tile triggers before drag detection

        qCDebug(lcEffect) << "Window move started -" << movingWindow->windowClass()
                          << "current modifiers:" << static_cast<int>(m_currentModifiers);
        callDragStarted(m_draggedWindowId, movingWindow->frameGeometry());
    }
    // Detect ongoing drag - send position updates
    else if (movingWindow && movingWindow == m_draggedWindow) {
        // Re-check if window has gone fullscreen during drag (e.g., game entered fullscreen)
        // If so, stop tracking immediately to prevent interference with fullscreen mode
        if (movingWindow->isFullScreen()) {
            qCDebug(lcEffect) << "Window went fullscreen mid-drag, stopping tracking";
            m_draggedWindow = nullptr;
            m_draggedWindowId.clear();
            return;
        }
        QPointF cursorPos = KWin::effects->cursorPos();
        if (cursorPos != m_lastCursorPos) {
            m_lastCursorPos = cursorPos;
            // Use tracked modifiers from mouseChanged signal instead of QGuiApplication
            // QGuiApplication::queryKeyboardModifiers() does NOT work in KWin effects on Wayland
            callDragMoved(m_draggedWindowId, cursorPos, m_currentModifiers);
        }
    }
    // Detect end of drag
    else if (!movingWindow && m_draggedWindow) {
        qCDebug(lcEffect) << "Window move finished";
        // Save the window pointer before clearing - we need it to apply the snap geometry
        KWin::EffectWindow* windowToSnap = m_draggedWindow;
        QString windowIdToSnap = m_draggedWindowId;

        // Clear state first to prevent re-entry issues
        m_draggedWindow = nullptr;
        m_draggedWindowId.clear();

        // Call dragStopped with the window so we can apply snap geometry
        callDragStopped(windowToSnap, windowIdToSnap);
    }
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
    // Sync floating window state from daemon's persisted state on startup
    // Note: The daemon now stores floating windows by STABLE ID (without pointer address)
    // so we can directly use those IDs for comparison
    if (!ensureWindowTrackingReady("sync floating windows")) {
        return;
    }

    QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getFloatingWindows"));
    if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
        QStringList floatingWindows = msg.arguments().at(0).toStringList();
        for (const QString& stableId : floatingWindows) {
            // Store by stable ID (daemon already returns stable IDs)
            m_floatingWindows.insert(stableId);
        }
        if (!floatingWindows.isEmpty()) {
            qCDebug(lcEffect) << "Synced" << floatingWindows.size() << "floating windows from daemon (stable IDs)";
        }
    }
}

void PlasmaZonesEffect::loadExclusionSettings()
{
    // Query exclusion settings from daemon via D-Bus
    QDBusInterface settingsInterface(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                     QDBusConnection::sessionBus());

    if (!settingsInterface.isValid()) {
        qCDebug(lcEffect) << "Cannot load exclusion settings - daemon not available yet";
        return;
    }

    // Load all exclusion settings using the template helper
    m_excludeTransientWindows = loadDBusSetting<bool>(settingsInterface, QStringLiteral("excludeTransientWindows"), true);
    m_minimumWindowWidth = loadDBusSetting<int>(settingsInterface, QStringLiteral("minimumWindowWidth"), 200);
    m_minimumWindowHeight = loadDBusSetting<int>(settingsInterface, QStringLiteral("minimumWindowHeight"), 150);

    qCDebug(lcEffect) << "Loaded exclusion settings: excludeTransient=" << m_excludeTransientWindows
                      << "minWidth=" << m_minimumWindowWidth << "minHeight=" << m_minimumWindowHeight;
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

void PlasmaZonesEffect::emitNavigationFeedback(bool success, const QString& action, const QString& reason)
{
    // Call D-Bus method on daemon to report navigation feedback (can't emit signals on another service's interface)
    if (!ensureWindowTrackingReady("report navigation feedback")) {
        return;
    }
    m_windowTrackingInterface->asyncCall(QStringLiteral("reportNavigationFeedback"), success, action, reason);
}

void PlasmaZonesEffect::slotMoveWindowToZoneRequested(const QString& targetZoneId, const QString& zoneGeometry)
{
    qCDebug(lcEffect) << "Move window to zone requested -" << targetZoneId;

    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for move";
        emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_window"));
        return;
    }

    QString windowId = getWindowId(activeWindow);
    QString stableId = extractStableId(windowId);

    // Check if window is floating - skip if so (use stable ID for comparison)
    if (m_floatingWindows.contains(stableId)) {
        qCDebug(lcEffect) << "Window is floating, skipping move";
        emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("window_floating"));
        return;
    }

    // Check if this is a navigation directive (navigate:left, navigate:right, etc.)
    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());
        qCDebug(lcEffect) << "Navigation direction:" << direction;

        // Get current zone for this window
        if (!ensureWindowTrackingReady("get zone for window")) {
            return;
        }

        QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getZoneForWindow"), windowId);

        QString currentZoneId;
        if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
            currentZoneId = msg.arguments().at(0).toString();
        }

        QString targetZone;
        QString screenName = getWindowScreenName(activeWindow);

        if (currentZoneId.isEmpty()) {
            // Window not snapped - snap directly to the edge zone in the direction pressed
            // This is more intuitive: pressing "left" snaps to leftmost, "right" to rightmost, etc.
            qCDebug(lcEffect) << "Window not snapped, finding first zone in direction" << direction;

            // Store current geometry as pre-snap geometry
            QRectF currentGeom = activeWindow->frameGeometry();
            m_windowTrackingInterface->asyncCall(QStringLiteral("storePreSnapGeometry"), windowId,
                                                 static_cast<int>(currentGeom.x()), static_cast<int>(currentGeom.y()),
                                                 static_cast<int>(currentGeom.width()),
                                                 static_cast<int>(currentGeom.height()));

            // Find the edge zone in the direction pressed
            targetZone = queryFirstZoneInDirection(direction);
            if (targetZone.isEmpty()) {
                qCDebug(lcEffect) << "No zones available for navigation";
                emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"));
                return;
            }
            qCDebug(lcEffect) << "Found first zone in direction" << direction << ":" << targetZone;
        } else {
            // Window is already snapped - navigate to adjacent zone
            targetZone = queryAdjacentZone(currentZoneId, direction);
            if (targetZone.isEmpty()) {
                qCDebug(lcEffect) << "No adjacent zone in direction" << direction;
                emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"));
                return;
            }
        }

        // Get geometry for target zone (use window's screen for multi-monitor support)
        QString geometryJson = queryZoneGeometryForScreen(targetZone, screenName);
        if (geometryJson.isEmpty()) {
            qCDebug(lcEffect) << "Could not get geometry for zone" << targetZone;
            return;
        }

        // Parse geometry JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(geometryJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcEffect) << "Failed to parse zone geometry:" << parseError.errorString();
            return;
        }

        QJsonObject obj = doc.object();
        QRect geometry(obj[QStringLiteral("x")].toInt(), obj[QStringLiteral("y")].toInt(),
                       obj[QStringLiteral("width")].toInt(), obj[QStringLiteral("height")].toInt());

        qCDebug(lcEffect) << "Moving window to zone" << targetZone << "geometry:" << geometry;

        // Apply geometry
        applySnapGeometry(activeWindow, geometry);

        // Notify daemon of the snap
        m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);

        // Emit success feedback
        emitNavigationFeedback(true, QStringLiteral("move"));

    } else if (!targetZoneId.isEmpty()) {
        // Direct zone ID - need to get geometry for active window's screen (multi-monitor support)
        // The daemon may have provided geometry based on primary screen, but we need to use
        // the geometry for the screen where the active window is located
        QString geometryJson = zoneGeometry;

        // Always recalculate geometry for the active window's screen to ensure multi-monitor support
        // This fixes the "push to empty zone" feature on secondary monitors
        QString screenName = getWindowScreenName(activeWindow);
        if (!screenName.isEmpty()) {
            QString screenGeometry = queryZoneGeometryForScreen(targetZoneId, screenName);
            if (!screenGeometry.isEmpty()) {
                geometryJson = screenGeometry;
                qCDebug(lcEffect) << "Using geometry for window's screen" << screenName;
            }
        }

        // Fall back to provided geometry or fetch from primary screen
        if (geometryJson.isEmpty()) {
            geometryJson = queryZoneGeometry(targetZoneId);
        }

        if (geometryJson.isEmpty()) {
            qCDebug(lcEffect) << "Could not get geometry for zone" << targetZoneId;
            emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(geometryJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcEffect) << "Failed to parse zone geometry:" << parseError.errorString();
            return;
        }

        QJsonObject obj = doc.object();
        QRect geometry(obj[QStringLiteral("x")].toInt(), obj[QStringLiteral("y")].toInt(),
                       obj[QStringLiteral("width")].toInt(), obj[QStringLiteral("height")].toInt());

        qCDebug(lcEffect) << "Moving window to zone" << targetZoneId << "geometry:" << geometry << "on screen"
                          << screenName;

        // Store pre-snap geometry if not already tracked (for unsnapped window moving to zone)
        ensureWindowTrackingInterface();
        if (m_windowTrackingInterface && m_windowTrackingInterface->isValid()) {
            QDBusMessage checkMsg = m_windowTrackingInterface->call(QStringLiteral("hasPreSnapGeometry"), windowId);
            bool hasGeometry = false;
            if (checkMsg.type() == QDBusMessage::ReplyMessage && !checkMsg.arguments().isEmpty()) {
                hasGeometry = checkMsg.arguments().at(0).toBool();
            }

            if (!hasGeometry) {
                QRectF currentGeom = activeWindow->frameGeometry();
                m_windowTrackingInterface->asyncCall(
                    QStringLiteral("storePreSnapGeometry"), windowId, static_cast<int>(currentGeom.x()),
                    static_cast<int>(currentGeom.y()), static_cast<int>(currentGeom.width()),
                    static_cast<int>(currentGeom.height()));
            }
        }

        applySnapGeometry(activeWindow, geometry);

        // Notify daemon
        if (m_windowTrackingInterface && m_windowTrackingInterface->isValid()) {
            m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZoneId);
        }

        // Emit success feedback AFTER the window is moved (not from the daemon)
        emitNavigationFeedback(true, QStringLiteral("push"));
    }
}

void PlasmaZonesEffect::slotFocusWindowInZoneRequested(const QString& targetZoneId, const QString& windowId)
{
    Q_UNUSED(windowId) // We'll find the window ourselves
    qCDebug(lcEffect) << "Focus window in zone requested -" << targetZoneId;

    if (targetZoneId.isEmpty()) {
        return;
    }

    // Check if this is a navigation directive
    QString actualZoneId = targetZoneId;
    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());

        // Get current active window's zone
        KWin::EffectWindow* activeWindow = getActiveWindow();
        if (!activeWindow) {
            return;
        }

        QString activeWindowId = getWindowId(activeWindow);
        if (!ensureWindowTrackingReady("get zone for window")) {
            return;
        }

        QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getZoneForWindow"), activeWindowId);

        QString currentZoneId;
        if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
            currentZoneId = msg.arguments().at(0).toString();
        }

        if (currentZoneId.isEmpty()) {
            // Window not snapped - can't navigate focus from unsnapped window
            // Focus navigation requires knowing the current zone to find adjacent
            qCDebug(lcEffect) << "Focus navigation requires snapped window";
            emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"));
            return;
        }

        actualZoneId = queryAdjacentZone(currentZoneId, direction);
        if (actualZoneId.isEmpty()) {
            qCDebug(lcEffect) << "No adjacent zone in direction" << direction;
            emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"));
            return;
        }
    }

    // Get windows in the target zone
    if (!ensureWindowTrackingReady("get windows in zone")) {
        return;
    }

    QDBusMessage zoneReply = m_windowTrackingInterface->call(QStringLiteral("getWindowsInZone"), actualZoneId);

    QStringList windowsInZone;
    if (zoneReply.type() == QDBusMessage::ReplyMessage && !zoneReply.arguments().isEmpty()) {
        windowsInZone = zoneReply.arguments().at(0).toStringList();
    }

    if (windowsInZone.isEmpty()) {
        qCDebug(lcEffect) << "No windows in zone" << actualZoneId;
        emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"));
        return;
    }

    // Find the first matching window and activate it
    QString targetWindowId = windowsInZone.first();
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && getWindowId(w) == targetWindowId) {
            qCDebug(lcEffect) << "Activating window" << targetWindowId;
            KWin::effects->activateWindow(w);
            emitNavigationFeedback(true, QStringLiteral("focus"));
            return;
        }
    }

    // Window not found in effect's window list
    emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("window_not_found"));
}

void PlasmaZonesEffect::slotRestoreWindowRequested()
{
    qCDebug(lcEffect) << "Restore window requested";

    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for restore";
        emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_window"));
        return;
    }

    QString windowId = getWindowId(activeWindow);

    // Get pre-snap geometry from daemon
    if (!ensureWindowTrackingReady("get pre-snap geometry")) {
        emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("dbus_error"));
        return;
    }

    // Call getValidatedPreSnapGeometry for safety
    QDBusMessage reply = m_windowTrackingInterface->call(QStringLiteral("getValidatedPreSnapGeometry"), windowId);

    if (reply.type() != QDBusMessage::ReplyMessage) {
        qCDebug(lcEffect) << "Failed to get pre-snap geometry";
        emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_geometry"));
        return;
    }

    // Parse out parameters: found (bool), x, y, width, height
    // Qt D-Bus adaptor return order: return value FIRST, then output parameters
    if (reply.arguments().size() < 5) {
        emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("invalid_response"));
        return;
    }

    bool found = reply.arguments().at(0).toBool();
    int x = reply.arguments().at(1).toInt();
    int y = reply.arguments().at(2).toInt();
    int width = reply.arguments().at(3).toInt();
    int height = reply.arguments().at(4).toInt();

    if (!found || width <= 0 || height <= 0) {
        qCDebug(lcEffect) << "No valid pre-snap geometry found";
        emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"));
        return;
    }

    QRect geometry(x, y, width, height);
    qCDebug(lcEffect) << "Restoring window to" << geometry;

    applySnapGeometry(activeWindow, geometry);

    // Notify daemon to clear pre-snap geometry and tracking
    m_windowTrackingInterface->asyncCall(QStringLiteral("windowUnsnapped"), windowId);
    m_windowTrackingInterface->asyncCall(QStringLiteral("clearPreSnapGeometry"), windowId);

    emitNavigationFeedback(true, QStringLiteral("restore"));
}

void PlasmaZonesEffect::slotToggleWindowFloatRequested(bool shouldFloat)
{
    Q_UNUSED(shouldFloat) // We determine the new state ourselves by toggling
    qCDebug(lcEffect) << "Toggle float requested";

    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for float toggle";
        emitNavigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_window"));
        return;
    }

    QString windowId = getWindowId(activeWindow);
    QString stableId = extractStableId(windowId);

    // Check current float state and TOGGLE it (use stable ID for comparison)
    bool isCurrentlyFloating = m_floatingWindows.contains(stableId);
    bool newFloatState = !isCurrentlyFloating;

    qCDebug(lcEffect) << "Window" << stableId << "currently floating:" << isCurrentlyFloating
                      << "-> new state:" << newFloatState;

    ensureWindowTrackingInterface();

    if (newFloatState) {
        // Float the window (store by stable ID)
        m_floatingWindows.insert(stableId);
        qCDebug(lcEffect) << "Window" << stableId << "is now floating";

        // Restore to original size when floating
        if (m_windowTrackingInterface && m_windowTrackingInterface->isValid()) {
            QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getValidatedPreSnapGeometry"), windowId);

            // Qt D-Bus adaptor return order: return value FIRST, then output parameters
            if (msg.type() == QDBusMessage::ReplyMessage && msg.arguments().size() >= 5) {
                bool found = msg.arguments().at(0).toBool();
                int x = msg.arguments().at(1).toInt();
                int y = msg.arguments().at(2).toInt();
                int width = msg.arguments().at(3).toInt();
                int height = msg.arguments().at(4).toInt();

                if (found && width > 0 && height > 0) {
                    QRect geometry(x, y, width, height);
                    qCDebug(lcEffect) << "Restoring float window to original size" << geometry;
                    applySnapGeometry(activeWindow, geometry);
                }
            }

            // Unsnap for float (saves zone for restore-on-unfloat; no-op if never snapped)
            m_windowTrackingInterface->asyncCall(QStringLiteral("windowUnsnappedForFloat"), windowId);
            m_windowTrackingInterface->asyncCall(QStringLiteral("setWindowFloating"), windowId, true);
        }
    } else {
        // Unfloat the window (remove by stable ID)
        m_floatingWindows.remove(stableId);
        qCDebug(lcEffect) << "Window" << stableId << "is no longer floating";

        // Sync float state with daemon
        if (m_windowTrackingInterface && m_windowTrackingInterface->isValid()) {
            m_windowTrackingInterface->asyncCall(QStringLiteral("setWindowFloating"), windowId, false);

            // Restore to previous zone if it was snapped before being floated
            QDBusMessage preMsg = m_windowTrackingInterface->call(QStringLiteral("getPreFloatZone"), windowId);
            if (preMsg.type() == QDBusMessage::ReplyMessage && preMsg.arguments().size() >= 2
                && preMsg.arguments().at(0).toBool()) {
                QString zoneId = preMsg.arguments().at(1).toString();
                if (!zoneId.isEmpty()) {
                    QString screenName = getWindowScreenName(activeWindow);
                    QString geometryJson = queryZoneGeometryForScreen(zoneId, screenName);
                    if (!geometryJson.isEmpty()) {
                        QJsonParseError parseError;
                        QJsonDocument doc = QJsonDocument::fromJson(geometryJson.toUtf8(), &parseError);
                        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                            QJsonObject obj = doc.object();
                            QRect geometry(obj[QStringLiteral("x")].toInt(), obj[QStringLiteral("y")].toInt(),
                                           obj[QStringLiteral("width")].toInt(), obj[QStringLiteral("height")].toInt());
                            qCDebug(lcEffect) << "Restoring unfloated window to zone" << zoneId;
                            applySnapGeometry(activeWindow, geometry);
                            m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, zoneId);
                        }
                    }
                    m_windowTrackingInterface->asyncCall(QStringLiteral("clearPreFloatZone"), windowId);
                }
            }
        }
    }

    // Emit success feedback with the action result
    QString actionResult = newFloatState ? QStringLiteral("floated") : QStringLiteral("unfloated");
    emitNavigationFeedback(true, QStringLiteral("float"), actionResult);
}

void PlasmaZonesEffect::slotSwapWindowsRequested(const QString& targetZoneId, const QString& targetWindowId,
                                                  const QString& zoneGeometry)
{
    Q_UNUSED(targetWindowId)
    Q_UNUSED(zoneGeometry)
    qCDebug(lcEffect) << "Swap windows requested -" << targetZoneId;

    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for swap";
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_window"));
        return;
    }

    QString windowId = getWindowId(activeWindow);
    QString stableId = extractStableId(windowId);

    // Check if window is floating - skip if so
    if (m_floatingWindows.contains(stableId)) {
        qCDebug(lcEffect) << "Window is floating, skipping swap";
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("window_floating"));
        return;
    }

    // Check if this is a swap directive (swap:left, swap:right, etc.)
    static const QString SwapDirectivePrefix = QStringLiteral("swap:");
    if (!targetZoneId.startsWith(SwapDirectivePrefix)) {
        qCWarning(lcEffect) << "Invalid swap directive:" << targetZoneId;
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_directive"));
        return;
    }

    QString direction = targetZoneId.mid(SwapDirectivePrefix.length());
    qCDebug(lcEffect) << "Swap direction:" << direction;

    // Get current zone for this window
    if (!ensureWindowTrackingReady("get zone for swap")) {
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"));
        return;
    }

    QDBusMessage msg = m_windowTrackingInterface->call(QStringLiteral("getZoneForWindow"), windowId);

    QString currentZoneId;
    if (msg.type() == QDBusMessage::ReplyMessage && !msg.arguments().isEmpty()) {
        currentZoneId = msg.arguments().at(0).toString();
    }

    if (currentZoneId.isEmpty()) {
        // Window not snapped - can't swap
        qCDebug(lcEffect) << "Active window not snapped to a zone, cannot swap";
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"));
        return;
    }

    // Find adjacent zone
    QString targetZone = queryAdjacentZone(currentZoneId, direction);
    if (targetZone.isEmpty()) {
        qCDebug(lcEffect) << "No adjacent zone in direction" << direction;
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"));
        return;
    }

    QString screenName = getWindowScreenName(activeWindow);

    // Get windows in target zone
    QDBusMessage windowsMsg = m_windowTrackingInterface->call(QStringLiteral("getWindowsInZone"), targetZone);
    QStringList windowsInTargetZone;
    if (windowsMsg.type() == QDBusMessage::ReplyMessage && !windowsMsg.arguments().isEmpty()) {
        windowsInTargetZone = windowsMsg.arguments().at(0).toStringList();
    }

    // Get geometry for both zones
    QString targetZoneGeometry = queryZoneGeometryForScreen(targetZone, screenName);
    QString currentZoneGeometry = queryZoneGeometryForScreen(currentZoneId, screenName);

    if (targetZoneGeometry.isEmpty() || currentZoneGeometry.isEmpty()) {
        qCDebug(lcEffect) << "Could not get zone geometries for swap";
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"));
        return;
    }

    // Parse target zone geometry
    QJsonParseError parseError;
    QJsonDocument targetDoc = QJsonDocument::fromJson(targetZoneGeometry.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !targetDoc.isObject()) {
        qCWarning(lcEffect) << "Failed to parse target zone geometry:" << parseError.errorString();
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("parse_error"));
        return;
    }
    QJsonObject targetObj = targetDoc.object();
    QRect targetGeom(targetObj[QStringLiteral("x")].toInt(), targetObj[QStringLiteral("y")].toInt(),
                     targetObj[QStringLiteral("width")].toInt(), targetObj[QStringLiteral("height")].toInt());

    // Parse current zone geometry
    QJsonDocument currentDoc = QJsonDocument::fromJson(currentZoneGeometry.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !currentDoc.isObject()) {
        qCWarning(lcEffect) << "Failed to parse current zone geometry:" << parseError.errorString();
        emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("parse_error"));
        return;
    }
    QJsonObject currentObj = currentDoc.object();
    QRect currentGeom(currentObj[QStringLiteral("x")].toInt(), currentObj[QStringLiteral("y")].toInt(),
                      currentObj[QStringLiteral("width")].toInt(), currentObj[QStringLiteral("height")].toInt());

    if (windowsInTargetZone.isEmpty()) {
        // Target zone is empty - just move the active window there (like regular move)
        qCDebug(lcEffect) << "Target zone is empty, performing regular move";
        applySnapGeometry(activeWindow, targetGeom);
        m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
        emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("moved_to_empty"));
    } else {
        // Target zone has a window - perform the swap
        QString targetWindowIdToSwap = windowsInTargetZone.first();
        qCDebug(lcEffect) << "Swapping with window" << targetWindowIdToSwap;

        // Find the target window in the effect's window list
        KWin::EffectWindow* targetWindow = nullptr;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            if (w && getWindowId(w) == targetWindowIdToSwap) {
                targetWindow = w;
                break;
            }
        }

        if (!targetWindow) {
            qCDebug(lcEffect) << "Target window not found in effect window list";
            // Just move the active window anyway
            applySnapGeometry(activeWindow, targetGeom);
            m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
            emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_not_found"));
            return;
        }

        // Validate target window - skip excluded windows
        if (!shouldHandleWindow(targetWindow)) {
            qCDebug(lcEffect) << "Target window is excluded, performing regular move";
            applySnapGeometry(activeWindow, targetGeom);
            m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
            emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_excluded"));
            return;
        }

        // Check if target window is floating - skip if so
        QString targetStableId = extractStableId(targetWindowIdToSwap);
        if (m_floatingWindows.contains(targetStableId)) {
            qCDebug(lcEffect) << "Target window is floating, performing regular move";
            applySnapGeometry(activeWindow, targetGeom);
            m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
            emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_floating"));
            return;
        }

        // Swap the windows: each gets the other's zone geometry
        qCDebug(lcEffect) << "Swapping windows between zones" << currentZoneId << "and" << targetZone;

        // Ensure both windows have pre-snap geometry preserved
        // This handles edge cases where windows might have been placed in zones
        // via external means without pre-snap geometry being stored
        auto ensurePreSnapGeometry = [this](KWin::EffectWindow* w, const QString& wId) {
            QDBusMessage checkMsg = m_windowTrackingInterface->call(QStringLiteral("hasPreSnapGeometry"), wId);
            bool hasGeometry = false;
            if (checkMsg.type() == QDBusMessage::ReplyMessage && !checkMsg.arguments().isEmpty()) {
                hasGeometry = checkMsg.arguments().at(0).toBool();
            }
            if (!hasGeometry) {
                QRectF currentGeom = w->frameGeometry();
                m_windowTrackingInterface->asyncCall(
                    QStringLiteral("storePreSnapGeometry"), wId, static_cast<int>(currentGeom.x()),
                    static_cast<int>(currentGeom.y()), static_cast<int>(currentGeom.width()),
                    static_cast<int>(currentGeom.height()));
                qCDebug(lcEffect) << "Stored pre-snap geometry for window" << wId << "before swap";
            }
        };

        ensurePreSnapGeometry(activeWindow, windowId);
        ensurePreSnapGeometry(targetWindow, targetWindowIdToSwap);

        // Move active window to target zone
        applySnapGeometry(activeWindow, targetGeom);
        m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);

        // Move target window to current zone
        applySnapGeometry(targetWindow, currentGeom);
        m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), targetWindowIdToSwap, currentZoneId);

        emitNavigationFeedback(true, QStringLiteral("swap"));
    }
}

void PlasmaZonesEffect::slotRotateWindowsRequested(bool clockwise, const QString& rotationData)
{
    qCDebug(lcEffect) << "Rotate windows requested, clockwise:" << clockwise;

    // Parse rotation data JSON array
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(rotationData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcEffect) << "Failed to parse rotation data:" << parseError.errorString();
        emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("parse_error"));
        return;
    }

    QJsonArray rotationArray = doc.array();
    if (rotationArray.isEmpty()) {
        qCDebug(lcEffect) << "No windows to rotate";
        emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_windows"));
        return;
    }

    if (!ensureWindowTrackingReady("rotate windows")) {
        emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("dbus_error"));
        return;
    }

    // Build a map of windowId -> EffectWindow* for efficient lookup
    QHash<QString, KWin::EffectWindow*> windowMap;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && shouldHandleWindow(w)) {
            windowMap[getWindowId(w)] = w;
        }
    }

    int successCount = 0;

    // Process each window rotation
    for (const QJsonValue& value : rotationArray) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject moveObj = value.toObject();
        QString windowId = moveObj[QStringLiteral("windowId")].toString();
        QString targetZoneId = moveObj[QStringLiteral("targetZoneId")].toString();
        int x = moveObj[QStringLiteral("x")].toInt();
        int y = moveObj[QStringLiteral("y")].toInt();
        int width = moveObj[QStringLiteral("width")].toInt();
        int height = moveObj[QStringLiteral("height")].toInt();

        if (windowId.isEmpty() || targetZoneId.isEmpty()) {
            continue;
        }

        // Find the window
        KWin::EffectWindow* window = windowMap.value(windowId);
        if (!window) {
            qCDebug(lcEffect) << "Window not found for rotation:" << windowId;
            continue;
        }

        // Check if window is floating - skip if so (double-check, daemon should filter these)
        QString stableId = extractStableId(windowId);
        if (m_floatingWindows.contains(stableId)) {
            qCDebug(lcEffect) << "Window is floating, skipping rotation:" << windowId;
            continue;
        }

        // Ensure pre-snap geometry is preserved before rotation
        // This is important so windows can be restored to original size later
        QDBusMessage checkMsg = m_windowTrackingInterface->call(QStringLiteral("hasPreSnapGeometry"), windowId);
        bool hasGeometry = false;
        if (checkMsg.type() == QDBusMessage::ReplyMessage && !checkMsg.arguments().isEmpty()) {
            hasGeometry = checkMsg.arguments().at(0).toBool();
        }
        if (!hasGeometry) {
            QRectF currentGeom = window->frameGeometry();
            m_windowTrackingInterface->asyncCall(QStringLiteral("storePreSnapGeometry"), windowId,
                                                 static_cast<int>(currentGeom.x()),
                                                 static_cast<int>(currentGeom.y()),
                                                 static_cast<int>(currentGeom.width()),
                                                 static_cast<int>(currentGeom.height()));
            qCDebug(lcEffect) << "Stored pre-snap geometry for window" << windowId << "before rotation";
        }

        // Apply the new geometry
        QRect newGeom(x, y, width, height);
        applySnapGeometry(window, newGeom);

        // Update the window-zone assignment
        m_windowTrackingInterface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZoneId);

        qCDebug(lcEffect) << "Rotated window" << windowId << "to zone" << targetZoneId;
        ++successCount;
    }

    if (successCount > 0) {
        qCInfo(lcEffect) << "Rotated" << successCount << "windows" << (clockwise ? "clockwise" : "counterclockwise");
        emitNavigationFeedback(true, QStringLiteral("rotate"));
    } else {
        emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"));
    }
}

void PlasmaZonesEffect::slotCycleWindowsInZoneRequested(const QString& directive, const QString& unused)
{
    Q_UNUSED(unused)
    qCDebug(lcEffect) << "Cycle windows in zone requested -" << directive;

    // Parse directive: "cycle:forward" or "cycle:backward"
    static const QString CycleDirectivePrefix = QStringLiteral("cycle:");
    if (!directive.startsWith(CycleDirectivePrefix)) {
        qCWarning(lcEffect) << "Invalid cycle directive:" << directive;
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("invalid_directive"));
        return;
    }

    QString direction = directive.mid(CycleDirectivePrefix.length());

    // Explicitly validate direction - reject unknown values
    bool forward;
    if (direction == QStringLiteral("forward")) {
        forward = true;
    } else if (direction == QStringLiteral("backward")) {
        forward = false;
    } else {
        qCWarning(lcEffect) << "Unknown cycle direction:" << direction;
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("invalid_direction"));
        return;
    }
    qCDebug(lcEffect) << "Cycle direction:" << direction << "forward:" << forward;

    // Get the active window
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for cycle";
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("no_window"));
        return;
    }

    QString windowId = getWindowId(activeWindow);

    // Get the zone for the active window
    if (!ensureWindowTrackingReady("get zone for cycle")) {
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("dbus_error"));
        return;
    }

    QDBusMessage zoneMsg = m_windowTrackingInterface->call(QStringLiteral("getZoneForWindow"), windowId);
    QString currentZoneId;
    if (zoneMsg.type() == QDBusMessage::ReplyMessage && !zoneMsg.arguments().isEmpty()) {
        currentZoneId = zoneMsg.arguments().at(0).toString();
    }

    if (currentZoneId.isEmpty()) {
        qCDebug(lcEffect) << "Active window not snapped to any zone";
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"));
        return;
    }

    // Get all windows in this zone from daemon (unordered set from QHash)
    QDBusMessage windowsMsg = m_windowTrackingInterface->call(QStringLiteral("getWindowsInZone"), currentZoneId);
    QStringList windowIdsInZone;
    if (windowsMsg.type() == QDBusMessage::ReplyMessage && !windowsMsg.arguments().isEmpty()) {
        windowIdsInZone = windowsMsg.arguments().at(0).toStringList();
    }

    if (windowIdsInZone.size() < 2) {
        qCDebug(lcEffect) << "Only one window in zone, nothing to cycle";
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"));
        return;
    }

    // Build a z-order sorted list of windows in this zone
    // KWin's stackingOrder() returns windows from bottom to top, giving us a deterministic
    // cycling order based on window stacking rather than arbitrary hash iteration order.
    // This provides a more intuitive user experience where cycling follows visual layering.
    QSet<QString> zoneWindowSet(windowIdsInZone.begin(), windowIdsInZone.end());
    QVector<KWin::EffectWindow*> sortedWindowsInZone;

    const auto stackingOrder = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : stackingOrder) {
        if (w && zoneWindowSet.contains(getWindowId(w))) {
            sortedWindowsInZone.append(w);
        }
    }

    if (sortedWindowsInZone.size() < 2) {
        // Some windows from daemon might have been closed
        qCDebug(lcEffect) << "Less than 2 windows found in stacking order for zone";
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"));
        return;
    }

    // Find the current window's index in the sorted list
    int currentIndex = -1;
    for (int i = 0; i < sortedWindowsInZone.size(); ++i) {
        if (sortedWindowsInZone[i] == activeWindow) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex < 0) {
        qCDebug(lcEffect) << "Active window not found in zone's sorted window list";
        emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("window_stacking_mismatch"));
        return;
    }

    // Calculate the next window index (wrap around)
    int nextIndex;
    if (forward) {
        nextIndex = (currentIndex + 1) % sortedWindowsInZone.size();
    } else {
        nextIndex = (currentIndex - 1 + sortedWindowsInZone.size()) % sortedWindowsInZone.size();
    }

    KWin::EffectWindow* targetWindow = sortedWindowsInZone.at(nextIndex);
    QString targetWindowId = getWindowId(targetWindow);
    qCDebug(lcEffect) << "Cycling from window" << currentIndex << "to" << nextIndex
                      << "- target:" << targetWindowId << "(z-order sorted)";

    // Activate the target window
    qCDebug(lcEffect) << "Activating window" << targetWindowId;
    KWin::effects->activateWindow(targetWindow);
    emitNavigationFeedback(true, QStringLiteral("cycle"));
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
    updateWindowStickyState(m_draggedWindow);
    ensureDBusInterface();
    if (!m_dbusInterface || !m_dbusInterface->isValid()) {
        return;
    }

    // Get window class info for exclusion filtering
    QString appName;
    QString windowClass;
    if (m_draggedWindow) {
        windowClass = m_draggedWindow->windowClass();
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
    // Consistent filtering with notifyWindowAdded (SRP: filtering responsibility in notify method)
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

} // namespace PlasmaZones

// KWin Effect Factory - creates the plugin
#include <effect/effect.h>

namespace KWin {

KWIN_EFFECT_FACTORY_SUPPORTED(PlasmaZones::PlasmaZonesEffect, "metadata.json",
                              return PlasmaZones::PlasmaZonesEffect::supported();)

} // namespace KWin

// MOC include - REQUIRED for the Q_OBJECT in KWIN_EFFECT_FACTORY_SUPPORTED
#include "plasmazoneseffect.moc"
