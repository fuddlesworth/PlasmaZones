// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenmanager.h"
#include "platform.h"
#include "logging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QRegularExpression>

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#include <LayerShellQt/Shell>
#endif

namespace PlasmaZones {

// Global cache for available geometry (screen name -> geometry)
// Updated by sensor windows, read by actualAvailableGeometry()
static QHash<QString, QRect> s_availableGeometryCache;

// Global pointer to the active ScreenManager instance (for static method access)
static QPointer<ScreenManager> s_instance;

ScreenManager::ScreenManager(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
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
    return true;
}

void ScreenManager::start()
{
    if (m_running) {
        return;
    }

    m_running = true;

    // Connect to QGuiApplication signals
    connect(qApp, &QGuiApplication::screenAdded, this, &ScreenManager::onScreenAdded);
    connect(qApp, &QGuiApplication::screenRemoved, this, &ScreenManager::onScreenRemoved);

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

    // Destroy all geometry sensors
    for (auto* screen : m_geometrySensors.keys()) {
        destroyGeometrySensor(screen);
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
        qCWarning(lcScreen) << "screens() called before QGuiApplication initialized";
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

#ifdef HAVE_LAYER_SHELL
    if (!Platform::isWayland() || !Platform::hasLayerShell()) {
        // On X11, Qt's availableGeometry() works correctly
        // Just cache it directly
        s_availableGeometryCache.insert(screen->name(), screen->availableGeometry());
        return;
    }

    // Create the main sensor (anchored to all edges) for available area size
    auto* sensor = new QWindow();
    sensor->setScreen(screen);
    sensor->setFlag(Qt::FramelessWindowHint);
    sensor->setFlag(Qt::BypassWindowManagerHint);
    sensor->setObjectName(QStringLiteral("GeometrySensor-%1").arg(screen->name()));

    auto* layerWindow = LayerShellQt::Window::get(sensor);
    if (!layerWindow) {
        qCWarning(lcScreen) << "Failed to get LayerShellQt window handle for sensor";
        delete sensor;
        return;
    }

    layerWindow->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
    layerWindow->setLayer(LayerShellQt::Window::LayerBackground);
    layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layerWindow->setAnchors(
        LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                      | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    layerWindow->setExclusiveZone(0);
    layerWindow->setScope(QStringLiteral("plasmazones-sensor-%1").arg(screen->name()));
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
    sensor->show();

    // Query KDE Plasma for panel information via D-Bus (most accurate method)
    // Schedule initial query with debounce to coalesce multiple sensor creations
    scheduleDbusQuery();

#else
    // No LayerShellQt - use Qt's availableGeometry
    s_availableGeometryCache.insert(screen->name(), screen->availableGeometry());
#endif
}

void ScreenManager::destroyGeometrySensor(QScreen* screen)
{
    if (auto* sensor = m_geometrySensors.take(screen)) {
        sensor->disconnect();
        sensor->hide();
        sensor->deleteLater();
    }

    if (screen) {
        s_availableGeometryCache.remove(screen->name());
    }
}

#ifdef HAVE_LAYER_SHELL
void ScreenManager::scheduleDbusQuery()
{
    if (m_dbusQueryPending) {
        return;
    }

    m_dbusQueryPending = true;
    // Use a longer delay during startup to allow Plasma Shell to fully initialize
    // This prevents blocking calls when Plasma isn't ready yet
    QTimer::singleShot(250, this, [this]() {
        m_dbusQueryPending = false;
        // queryKdePlasmaPanels now handles recalculation in its async callback
        queryKdePlasmaPanels();
    });
}

void ScreenManager::calculateAvailableGeometry(QScreen* screen)
{
    if (!screen) {
        return;
    }

    QRect screenGeom = screen->geometry();

    // Get panel offsets from KDE Plasma D-Bus query
    // Map screen to index: find this screen's position in QGuiApplication::screens()
    // Plasma and Qt can use different screen orderings on multi-monitor setups.
    int screenIndex = -1;
    const auto screenList = QGuiApplication::screens();
    for (int i = 0; i < screenList.size(); ++i) {
        if (screenList[i] == screen) {
            screenIndex = i;
            break;
        }
    }

    qCDebug(lcScreen) << "calculateAvailableGeometry - Qt screen index mapping:" << screen->name() << "-> index"
                      << screenIndex << "(total screens:" << screenList.size() << ")";

    int topOffset = 0;
    int bottomOffset = 0;
    int leftOffset = 0;
    int rightOffset = 0;
    bool hasDbusData = false;

    if (screenIndex >= 0 && m_panelOffsets.contains(screenIndex)) {
        const ScreenPanelOffsets& offsets = m_panelOffsets.value(screenIndex);
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
    auto* sensor = m_geometrySensors.value(screen);
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
        // Only sensor data - use it (no D-Bus available)
        finalWidth = sensorGeom.width();
        finalHeight = sensorGeom.height();
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
    qCDebug(lcScreen) << "calculateAvailableGeometry:" << screenKey << "screen:" << screenGeom
                      << "available:" << availGeom << "source:" << source;

    // Update cache and emit signal
    s_availableGeometryCache.insert(screenKey, availGeom);
    Q_EMIT availableGeometryChanged(screen, availGeom);
}

void ScreenManager::queryKdePlasmaPanels()
{
    // Query KDE Plasma via D-Bus for panel information (ASYNC to avoid blocking)
    QDBusInterface* plasmaShell =
        new QDBusInterface(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                           QStringLiteral("org.kde.PlasmaShell"), QDBusConnection::sessionBus(), this);

    if (!plasmaShell->isValid()) {
        delete plasmaShell;
        // No Plasma shell - just recalculate with what we have
        for (auto* screen : m_trackedScreens) {
            calculateAvailableGeometry(screen);
        }
        return;
    }

    // JavaScript to get panel information from Plasma Shell
    // p.height is the panel thickness (perpendicular dimension) in Plasma's API
    // p.location is one of: "top", "bottom", "left", "right"
    // p.screen is the screen index (0-based)
    QString script = QStringLiteral(R"(
        panels().forEach(function(p,i){
            var thickness = Math.abs(p.height);
            print("PANEL:" + p.screen + ":" + p.location + ":" + thickness + "\n");
        });
    )");

    // Use ASYNC call to avoid blocking the main thread during startup
    QDBusPendingCall pendingCall = plasmaShell->asyncCall(QStringLiteral("evaluateScript"), script);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, plasmaShell](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<QString> reply = *w;

        // Clear existing panel offsets before parsing new data
        m_panelOffsets.clear();

        if (reply.isValid()) {
            QString output = reply.value();
            qCDebug(lcScreen) << "queryKdePlasmaPanels D-Bus reply:" << output;

            // Parse the output: PANEL:screenIndex:location:thickness
            static QRegularExpression panelRegex(QStringLiteral("PANEL:(\\d+):(\\w+):(\\d+)"));
            QRegularExpressionMatchIterator it = panelRegex.globalMatch(output);

            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                int screenIndex = match.captured(1).toInt();
                QString location = match.captured(2);
                int thickness = match.captured(3).toInt();

                qCDebug(lcScreen) << "  Parsed panel: screen" << screenIndex << location << "thickness=" << thickness;

                if (!m_panelOffsets.contains(screenIndex)) {
                    m_panelOffsets.insert(screenIndex, ScreenPanelOffsets{});
                }

                ScreenPanelOffsets& offsets = m_panelOffsets[screenIndex];
                if (location == QLatin1String("top")) {
                    offsets.top = thickness;
                } else if (location == QLatin1String("bottom")) {
                    offsets.bottom = thickness;
                } else if (location == QLatin1String("left")) {
                    offsets.left = thickness;
                } else if (location == QLatin1String("right")) {
                    offsets.right = thickness;
                }
            }
        } else {
            qCWarning(lcScreen) << "queryKdePlasmaPanels D-Bus query failed:" << reply.error().message();
        }

        // Log final panel offsets
        for (auto it = m_panelOffsets.constBegin(); it != m_panelOffsets.constEnd(); ++it) {
            qCDebug(lcScreen) << "  Screen" << it.key() << "panel offsets:"
                              << "T" << it.value().top << "B" << it.value().bottom << "L" << it.value().left << "R"
                              << it.value().right;
        }

        // Now recalculate geometry for all screens with updated panel data
        for (auto* screen : m_trackedScreens) {
            calculateAvailableGeometry(screen);
        }

        // Cleanup
        delete plasmaShell;
        w->deleteLater();
    });
}

#else
void ScreenManager::scheduleDbusQuery()
{
}
void ScreenManager::queryKdePlasmaPanels()
{
}
void ScreenManager::calculateAvailableGeometry(QScreen* screen)
{
    if (screen) {
        s_availableGeometryCache.insert(screen->name(), screen->availableGeometry());
    }
}
#endif

void ScreenManager::onSensorGeometryChanged(QScreen* screen)
{
    // Main sensor geometry changed - re-query panels and recalculate
    if (!screen) {
        return;
    }

    auto* sensor = m_geometrySensors.value(screen);
    if (!sensor) {
        return;
    }

    QRect sensorGeom = sensor->geometry();
    qCDebug(lcScreen) << "onSensorGeometryChanged:" << screen->name() << "sensorGeometry:" << sensorGeom
                      << "screenGeometry:" << screen->geometry();

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
        return s_availableGeometryCache.value(screenKey);
    }

    // Fallback: check if Qt's availableGeometry differs from geometry
    // This works on X11 and some Wayland compositors
    QRect availGeom = screen->availableGeometry();
    QRect screenGeom = screen->geometry();

    if (availGeom != screenGeom && availGeom.isValid()) {
        s_availableGeometryCache.insert(screenKey, availGeom);
        return availGeom;
    }

    // No sensor data and Qt doesn't know - return full screen
    return screenGeom;
}

void ScreenManager::onScreenAdded(QScreen* screen)
{
    if (!screen || m_trackedScreens.contains(screen)) {
        return;
    }

    connectScreenSignals(screen);
    m_trackedScreens.append(screen);
    createGeometrySensor(screen);
    Q_EMIT screenAdded(screen);
}

void ScreenManager::onScreenRemoved(QScreen* screen)
{
    if (!screen) {
        return;
    }

    destroyGeometrySensor(screen);
    disconnectScreenSignals(screen);
    m_trackedScreens.removeAll(screen);
    Q_EMIT screenRemoved(screen);
}

void ScreenManager::onScreenGeometryChanged(const QRect& geometry)
{
    auto* screen = qobject_cast<QScreen*>(sender());
    if (screen) {
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

} // namespace PlasmaZones
