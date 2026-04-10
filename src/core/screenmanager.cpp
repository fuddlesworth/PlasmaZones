// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmanager.h"
#include "virtualscreen.h"
#include "platform.h"
#include "logging.h"
#include "utils.h"
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QRegularExpression>

#include "layersurface.h"

namespace PlasmaZones {

// Global cache for available geometry (screen name -> geometry)
// Updated by sensor windows, read by actualAvailableGeometry()
// Main-thread-only — no synchronization needed (Qt GUI thread constraint)
static QHash<QString, QRect> s_availableGeometryCache;

// Global pointer to the active ScreenManager instance (for static method access)
// Main-thread-only — no synchronization needed (Qt GUI thread constraint)
static QPointer<ScreenManager> s_instance;

ScreenManager::ScreenManager(QObject* parent)
    : QObject(parent)
{
    Q_ASSERT_X(!s_instance, "ScreenManager", "Multiple ScreenManager instances created");
    if (s_instance) {
        qCWarning(lcScreen) << "ScreenManager: singleton already exists, ignoring duplicate construction";
        return;
    }
    s_instance = this;
    m_valid = true;
    m_delayedPanelRequeryTimer.setSingleShot(true);
    connect(&m_delayedPanelRequeryTimer, &QTimer::timeout, this, [this]() {
        queryKdePlasmaPanels(true); // true = emit delayedPanelRequeryCompleted when done
    });
}

ScreenManager::~ScreenManager()
{
    stop();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool ScreenManager::init()
{
    // Placeholder — initialization logic moved to start(). Kept for API compatibility.
    return true;
}

void ScreenManager::start()
{
    if (m_running || !m_valid) {
        return;
    }

    m_running = true;

    // Connect to QGuiApplication signals
    connect(qApp, &QGuiApplication::screenAdded, this, &ScreenManager::onScreenAdded);
    connect(qApp, &QGuiApplication::screenRemoved, this, &ScreenManager::onScreenRemoved);

    // Watch for org.kde.plasmashell registration so we can query panels as soon as
    // Plasma Shell is available, instead of guessing with arbitrary timer delays.
    // If plasmashell is already registered, scheduleDbusQuery (called from
    // createGeometrySensor) handles it immediately.
    m_plasmaShellWatcher = new QDBusServiceWatcher(QStringLiteral("org.kde.plasmashell"), QDBusConnection::sessionBus(),
                                                   QDBusServiceWatcher::WatchForRegistration, this);
    connect(m_plasmaShellWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcScreen) << "Plasmashell: registered, querying panels";
        queryKdePlasmaPanels();
    });

    // Connect to existing screens and create geometry sensors
    for (auto* screen : QGuiApplication::screens()) {
        if (!m_trackedScreens.contains(screen)) {
            connectScreenSignals(screen);
            m_trackedScreens.append(screen);
            createGeometrySensor(screen);
        }
    }
}

void ScreenManager::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    m_delayedPanelRequeryTimer.stop();

    // Destroy all geometry sensors (use while-loop to avoid modifying hash during iteration)
    while (!m_geometrySensors.isEmpty()) {
        destroyGeometrySensor(m_geometrySensors.begin().key());
    }

    // Disconnect from all screens
    for (auto* screen : m_trackedScreens) {
        disconnectScreenSignals(screen);
    }
    m_trackedScreens.clear();

    // Disconnect from QGuiApplication
    disconnect(qApp, &QGuiApplication::screenAdded, this, nullptr);
    disconnect(qApp, &QGuiApplication::screenRemoved, this, nullptr);

    // Clear cache
    s_availableGeometryCache.clear();
}

QVector<QScreen*> ScreenManager::screens() const
{
    if (!qApp) {
        qCCritical(lcScreen) << "screens() called before QGuiApplication initialized";
        return {};
    }

    const auto screenList = QGuiApplication::screens();
    if (screenList.isEmpty()) {
        return {};
    }

    return QVector<QScreen*>(screenList.begin(), screenList.end());
}

QScreen* ScreenManager::primaryScreen() const
{
    return QGuiApplication::primaryScreen();
}

QScreen* ScreenManager::screenByName(const QString& name) const
{
    for (auto* screen : QGuiApplication::screens()) {
        if (screen->name() == name) {
            return screen;
        }
    }
    return nullptr;
}

void ScreenManager::createGeometrySensor(QScreen* screen)
{
    if (!screen || m_geometrySensors.contains(screen)) {
        return;
    }

    // Create the main sensor (anchored to all edges) for available area size
    auto* sensor = new QWindow();
    sensor->setScreen(screen);
    sensor->setFlag(Qt::FramelessWindowHint);
    sensor->setFlag(Qt::BypassWindowManagerHint);
    sensor->setObjectName(QStringLiteral("GeometrySensor-%1").arg(Utils::screenIdentifier(screen)));

    auto* layerSurface = LayerSurface::get(sensor);
    if (!layerSurface) {
        qCWarning(lcScreen) << "Failed to create layer surface for sensor";
        delete sensor;
        return;
    }

    // Batch all property changes into a single propertiesChanged() emission
    // so the QPA plugin only does one applyProperties()+commit round-trip.
    LayerSurface::BatchGuard guard(layerSurface);
    layerSurface->setScreen(screen);
    layerSurface->setLayer(LayerSurface::LayerBackground);
    layerSurface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityNone);
    layerSurface->setAnchors(LayerSurface::AnchorAll);
    layerSurface->setExclusiveZone(0);
    layerSurface->setScope(QStringLiteral("plasmazones-sensor-%1").arg(Utils::screenIdentifier(screen)));
    // Do NOT call sensor->setOpacity(0.0): on Wayland, Qt's QWaylandWindow does not implement
    // QWindow::setOpacity(), so it logs "This plugin does not support setting window opacity".
    // The sensor is in LayerBackground with no content; it does not need explicit transparency.

    connect(sensor, &QWindow::widthChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });
    connect(sensor, &QWindow::heightChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });
    // Also track position changes - panels can move without changing available area size
    connect(sensor, &QWindow::xChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });
    connect(sensor, &QWindow::yChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });

    m_geometrySensors.insert(screen, sensor);

    // Set geometry before show() to ensure the sensor lands on the correct Wayland output.
    // On Wayland, QWindow::setScreen() alone is insufficient — the compositor uses the
    // initial geometry to determine output binding. Without this, all sensors may end up
    // on the primary output and report its available geometry instead of their own.
    sensor->setGeometry(screen->geometry());
    sensor->show();

    // Query KDE Plasma for panel information via D-Bus (most accurate method)
    // Schedule initial query with debounce to coalesce multiple sensor creations
    scheduleDbusQuery();
}

void ScreenManager::destroyGeometrySensor(QScreen* screen)
{
    auto sensor = m_geometrySensors.take(screen);
    if (sensor) {
        sensor->disconnect();
        sensor->hide();
        sensor->deleteLater();
    }

    if (screen) {
        s_availableGeometryCache.remove(screen->name());
    }
}

void ScreenManager::calculateAvailableGeometry(QScreen* screen)
{
    if (!screen) {
        return;
    }

    QRect screenGeom = screen->geometry();
    QString connectorName = screen->name();

    qCDebug(lcScreen) << "calculateAvailableGeometry: screen=" << connectorName << "geometry=" << screenGeom;

    int topOffset = 0;
    int bottomOffset = 0;
    int leftOffset = 0;
    int rightOffset = 0;
    bool hasDbusData = false;

    // Look up panel offsets by screen name (populated by D-Bus query with geometry matching)
    if (m_panelOffsets.contains(connectorName)) {
        const ScreenPanelOffsets& offsets = m_panelOffsets.value(connectorName);
        topOffset = offsets.top;
        bottomOffset = offsets.bottom;
        leftOffset = offsets.left;
        rightOffset = offsets.right;
        hasDbusData = true;
    }

    // Get sensor geometry (real-time accurate available SIZE from compositor).
    // Sensor position is always (0,0) on Wayland, so it doesn't tell us where the available
    // area starts. Floating panels don't use exclusive zones, so the sensor can't detect
    // them; only D-Bus has that info.
    auto sensor = m_geometrySensors.value(screen);
    QRect sensorGeom;
    bool hasSensorData = false;
    if (sensor && sensor->isVisible()) {
        sensorGeom = sensor->geometry();
        hasSensorData = sensorGeom.isValid() && sensorGeom.width() > 0 && sensorGeom.height() > 0;
    }

    // Calculate final available geometry
    // Strategy:
    // - POSITION: Always use D-Bus offsets (sensor position is unreliable on Wayland)
    // - SIZE: Use the SMALLER of D-Bus or sensor (handles both floating panels and panel editing)
    //   - If sensor < D-Bus: panel is being resized (sensor is real-time accurate)
    //   - If sensor >= D-Bus: panels might be floating (D-Bus is the only source of truth)
    int availX, availY, finalWidth, finalHeight;

    // Position always comes from D-Bus panel offsets (or 0 if no D-Bus data)
    availX = screenGeom.x() + leftOffset;
    availY = screenGeom.y() + topOffset;

    // Calculate D-Bus-based size
    int dbusWidth = screenGeom.width() - leftOffset - rightOffset;
    int dbusHeight = screenGeom.height() - topOffset - bottomOffset;

    if (hasSensorData && hasDbusData) {
        // Both sources available - use the smaller size to handle all cases:
        // - Floating panels: sensor sees full screen, D-Bus has panel info → use D-Bus (smaller)
        // - Panel editing: D-Bus is stale, sensor is accurate → use sensor (smaller)
        // - Normal docked panels: both should match
        finalWidth = qMin(sensorGeom.width(), dbusWidth);
        finalHeight = qMin(sensorGeom.height(), dbusHeight);

        // Log when they differ significantly
        if (qAbs(dbusHeight - sensorGeom.height()) > 5 || qAbs(dbusWidth - sensorGeom.width()) > 5) {
            qCDebug(lcScreen) << "D-Bus vs Sensor mismatch."
                              << "D-Bus:" << dbusWidth << "x" << dbusHeight << "Sensor:" << sensorGeom.width() << "x"
                              << sensorGeom.height() << "Using:" << finalWidth << "x" << finalHeight;
        }
    } else if (hasSensorData) {
        // Only sensor data, no D-Bus panel info for this screen.
        if (m_panelGeometryReceived
            && (sensorGeom.width() < screenGeom.width() || sensorGeom.height() < screenGeom.height())) {
            // D-Bus query succeeded but found no panels on this screen,
            // yet sensor reports less than full screen. The sensor likely
            // landed on a different output (Wayland screen binding issue).
            qCDebug(lcScreen) << "Sensor for" << connectorName << "reports" << sensorGeom.size()
                              << "but D-Bus found no panels on this screen."
                              << "Using full screen geometry instead.";
            finalWidth = screenGeom.width();
            finalHeight = screenGeom.height();
        } else {
            finalWidth = sensorGeom.width();
            finalHeight = sensorGeom.height();
        }
    } else if (hasDbusData) {
        // Only D-Bus data - use it
        finalWidth = dbusWidth;
        finalHeight = dbusHeight;
    } else {
        // No data at all - use full screen geometry
        availX = screenGeom.x();
        availY = screenGeom.y();
        finalWidth = screenGeom.width();
        finalHeight = screenGeom.height();
    }

    QRect availGeom(availX, availY, finalWidth, finalHeight);

    // Check if geometry actually changed
    QString screenKey = screen->name();
    QRect oldGeom = s_availableGeometryCache.value(screenKey);

    if (availGeom == oldGeom) {
        return;
    }

    QString source =
        hasSensorData ? QStringLiteral("sensor") : (hasDbusData ? QStringLiteral("D-Bus") : QStringLiteral("fallback"));
    qCInfo(lcScreen) << "calculateAvailableGeometry: screen=" << screenKey << "screenGeom=" << screenGeom
                     << "available=" << availGeom << "source=" << source;

    // Update cache and emit signal
    s_availableGeometryCache.insert(screenKey, availGeom);
    Q_EMIT availableGeometryChanged(screen, availGeom);
}

void ScreenManager::onSensorGeometryChanged(QScreen* screen)
{
    // Main sensor geometry changed - re-query panels and recalculate
    if (!screen) {
        return;
    }

    auto sensor = m_geometrySensors.value(screen);
    if (!sensor) {
        return;
    }

    QRect sensorGeom = sensor->geometry();
    qCDebug(lcScreen) << "onSensorGeometryChanged: screen=" << screen->name() << "sensorGeometry=" << sensorGeom
                      << "screenGeometry=" << screen->geometry();

    if (!sensorGeom.isValid() || sensorGeom.width() <= 0 || sensorGeom.height() <= 0) {
        return;
    }

    // Re-query KDE Plasma panels via debounced D-Bus call
    // This handles panels being added/removed/resized
    scheduleDbusQuery();
}

QRect ScreenManager::actualAvailableGeometry(QScreen* screen)
{
    if (!screen) {
        return QRect();
    }

    QString screenKey = screen->name();

    // Check cache first (populated by sensor windows)
    if (s_availableGeometryCache.contains(screenKey)) {
        QRect cached = s_availableGeometryCache.value(screenKey);
        qCDebug(lcScreen) << "actualAvailableGeometry: screen=" << screenKey << "cached=" << cached
                          << "fullScreen=" << screen->geometry() << "qtAvail=" << screen->availableGeometry();
        return cached;
    }

    // Fallback: check if Qt's availableGeometry differs from geometry
    // This can work on some Wayland compositors before sensor data is available
    QRect availGeom = screen->availableGeometry();
    QRect screenGeom = screen->geometry();

    qCInfo(lcScreen) << "actualAvailableGeometry: screen=" << screenKey << "no cache, fallback qtAvail=" << availGeom
                     << "fullScreen=" << screenGeom
                     << "using=" << ((availGeom != screenGeom && availGeom.isValid()) ? "qtAvail" : "fullScreen");

    if (availGeom != screenGeom && availGeom.isValid()) {
        s_availableGeometryCache.insert(screenKey, availGeom);
        return availGeom;
    }

    // No sensor data and Qt doesn't know - return full screen
    return screenGeom;
}

bool ScreenManager::isPanelGeometryReady()
{
    return s_instance && s_instance->m_panelGeometryReceived;
}

ScreenManager* ScreenManager::instance()
{
    return s_instance;
}

QScreen* ScreenManager::resolvePhysicalScreen(const QString& screenId)
{
    auto* mgr = instance();
    QScreen* screen = mgr ? mgr->physicalQScreenFor(screenId) : nullptr;
    if (!screen) {
        screen = screenId.isEmpty() ? Utils::primaryScreen() : Utils::findScreenByIdOrName(screenId);
    }
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    return screen;
}

void ScreenManager::onScreenAdded(QScreen* screen)
{
    if (!screen || m_trackedScreens.contains(screen)) {
        return;
    }

    connectScreenSignals(screen);
    m_trackedScreens.append(screen);
    m_effectiveScreenIdsDirty = true;
    createGeometrySensor(screen);
    Q_EMIT screenAdded(screen);
}

void ScreenManager::onScreenRemoved(QScreen* screen)
{
    if (!screen) {
        return;
    }

    // Invalidate virtual geometry cache before the screen pointer becomes invalid
    QString physId = Utils::screenIdentifier(screen);
    invalidateVirtualGeometryCache(physId);

    destroyGeometrySensor(screen);
    disconnectScreenSignals(screen);
    m_trackedScreens.removeAll(screen);
    m_effectiveScreenIdsDirty = true;

    // Remove stale virtual screen config for the removed physical screen.
    // Keeping it would cause effectiveScreenIds() to enumerate virtual screen IDs
    // that no longer have a backing QScreen, leading to invalid geometry lookups.
    m_virtualConfigs.remove(physId);

    // Invalidate EDID cache so a different monitor on this connector gets a fresh read.
    // The daemon also does this, but ScreenManager should be self-contained.
    Utils::invalidateEdidCache(screen->name());

    // EDID invalidation may change screenIdentifier() results for subsequently
    // connected monitors on the same connector, so mark effective IDs dirty again
    // (the assignment above covers the current removal; this covers the EDID side-effect).
    m_effectiveScreenIdsDirty = true;

    Q_EMIT screenRemoved(screen);
}

void ScreenManager::onScreenGeometryChanged(const QRect& geometry)
{
    auto* screen = qobject_cast<QScreen*>(sender());
    if (screen) {
        // Invalidate virtual geometry caches only for the screen that changed
        const QString physId = Utils::screenIdentifier(screen);
        if (!physId.isEmpty()) {
            invalidateVirtualGeometryCache(physId);
        } else {
            invalidateVirtualGeometryCache();
        }
        m_effectiveScreenIdsDirty = true;

        // Screen geometry changed - the sensor window will be reconfigured by compositor
        // which will trigger onSensorGeometryChanged automatically
        Q_EMIT screenGeometryChanged(screen, geometry);
    }
}

void ScreenManager::connectScreenSignals(QScreen* screen)
{
    if (!screen) {
        return;
    }

    connect(screen, &QScreen::geometryChanged, this, &ScreenManager::onScreenGeometryChanged);
}

void ScreenManager::disconnectScreenSignals(QScreen* screen)
{
    if (!screen) {
        return;
    }

    disconnect(screen, &QScreen::geometryChanged, this, nullptr);
}

// Virtual screen management methods are in screenmanager/virtualscreens.cpp

} // namespace PlasmaZones
