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

#include <LayerShellQt/Window>
#include <LayerShellQt/Shell>

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
    if (auto* sensor = m_geometrySensors.take(screen)) {
        sensor->disconnect();
        sensor->hide();
        sensor->deleteLater();
    }

    if (screen) {
        s_availableGeometryCache.remove(screen->name());
    }
}

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
    QString screenName = screen->name();

    qCDebug(lcScreen) << "calculateAvailableGeometry screen=" << screenName
                      << "geometry=" << screenGeom;

    int topOffset = 0;
    int bottomOffset = 0;
    int leftOffset = 0;
    int rightOffset = 0;
    bool hasDbusData = false;

    // Look up panel offsets by screen name (populated by D-Bus query with geometry matching)
    if (m_panelOffsets.contains(screenName)) {
        const ScreenPanelOffsets& offsets = m_panelOffsets.value(screenName);
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
        // Only sensor data, no D-Bus panel info for this screen.
        if (m_panelGeometryReceived
            && (sensorGeom.width() < screenGeom.width() || sensorGeom.height() < screenGeom.height())) {
            // D-Bus query succeeded but found no panels on this screen,
            // yet sensor reports less than full screen. The sensor likely
            // landed on a different output (Wayland screen binding issue).
            qCDebug(lcScreen) << "Sensor for" << screenName << "reports" << sensorGeom.size()
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
    qCInfo(lcScreen) << "calculateAvailableGeometry screen= " << screenKey << " screenGeom= " << screenGeom
                      << " available= " << availGeom << " source= " << source;

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
        // Still emit panelGeometryReady so components don't hang waiting
        if (!m_panelGeometryReceived) {
            m_panelGeometryReceived = true;
            qCInfo(lcScreen) << "Panel geometry ready (no Plasma shell) - emitting signal";
            Q_EMIT panelGeometryReady();
        }
        return;
    }

    // JavaScript to get panel information from Plasma Shell
    // We query the panel's actual geometry to calculate the real offset from the screen edge,
    // which includes both thickness and any floating gap the theme defines.
    // p.height is the panel thickness (perpendicular dimension) in Plasma's API
    // p.location is one of: "top", "bottom", "left", "right"
    // p.screen is the screen index (0-based) — NOTE: Plasma's screen ordering can differ
    //   from Qt's QGuiApplication::screens() ordering on multi-monitor setups.
    //   We include the screen geometry in the output so we can match by geometry.
    // p.floating is a boolean indicating if the panel is in floating mode (Plasma 6)
    // p.hiding indicates auto-hide mode, one of ("none", "autohide", "dodgewindows", "windowsgobelow")
    // screenGeometry(screenIndex) returns the screen's full geometry
    QString script = QStringLiteral(R"(
        panels().forEach(function(p,i){
            var thickness = Math.abs(p.height);
            var floating = p.floating ? 1 : 0;
            var hiding = p.hiding;
            var sg = screenGeometry(p.screen);
            var loc = p.location;
            var pg = p.geometry;
            // Calculate the actual offset from the screen edge based on panel geometry
            // This includes both the panel thickness AND any floating gap
            var offset = thickness;
            if (pg && sg) {
                if (loc === "top") {
                    offset = (pg.y + pg.height) - sg.y;
                } else if (loc === "bottom") {
                    offset = (sg.y + sg.height) - pg.y;
                } else if (loc === "left") {
                    offset = (pg.x + pg.width) - sg.x;
                } else if (loc === "right") {
                    offset = (sg.x + sg.width) - pg.x;
                }
            }
            // Include screen geometry so we can match by geometry instead of index
            // (Plasma and Qt can have different screen orderings)
            var sgStr = sg ? (sg.x + "," + sg.y + "," + sg.width + "," + sg.height) : "";
            print("PANEL:" + p.screen + ":" + loc + ":" + hiding + ":" + offset + ":" + floating + ":" + sgStr + "\n");
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
            qCDebug(lcScreen) << "queryKdePlasmaPanels D-Bus reply= " << output;

            // Parse: PANEL:plasmaIndex:location:hiding:offset:floating:x,y,w,h
            // Match panels to Qt screens by geometry (Plasma and Qt can have different screen orderings)
            static QRegularExpression panelRegex(
                QStringLiteral("PANEL:(\\d+):(\\w+):(\\w+):(\\d+)(?::(\\d+))?(?::(\\d+),(\\d+),(\\d+),(\\d+))?"));
            QRegularExpressionMatchIterator it = panelRegex.globalMatch(output);

            const auto qtScreens = QGuiApplication::screens();

            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                int plasmaIndex = match.captured(1).toInt();
                QString location = match.captured(2);
                QString hiding = match.captured(3);
                int totalOffset = match.captured(4).toInt();
                bool isFloating = !match.captured(5).isEmpty() && match.captured(5).toInt() != 0;

                // Find the Qt screen matching this Plasma screen's geometry
                QString screenName;
                if (!match.captured(6).isEmpty()) {
                    QRect plasmaGeom(match.captured(6).toInt(), match.captured(7).toInt(),
                                     match.captured(8).toInt(), match.captured(9).toInt());
                    for (auto* qs : qtScreens) {
                        if (qs->geometry() == plasmaGeom) {
                            screenName = qs->name();
                            break;
                        }
                    }
                }

                if (screenName.isEmpty()) {
                    qCWarning(lcScreen) << "  Could not match Plasma screen" << plasmaIndex
                                        << "to any Qt screen by geometry — skipping panel";
                    continue;
                }

                qCDebug(lcScreen) << "  Parsed panel screen=" << screenName << "(plasma idx" << plasmaIndex << ")"
                                  << " location=" << location << " offset=" << totalOffset
                                  << " floating=" << isFloating << " hiding=" << hiding;

                bool panelAutoHides = (hiding == QLatin1String("autohide") 
                    || hiding == QLatin1String("dodgewindows") || hiding == QLatin1String("windowsgobelow"));

                if (!panelAutoHides) {
                    totalOffset = 0;
                }

                if (!m_panelOffsets.contains(screenName)) {
                    m_panelOffsets.insert(screenName, ScreenPanelOffsets{});
                }

                ScreenPanelOffsets& offsets = m_panelOffsets[screenName];
                if (location == QLatin1String("top")) {
                    offsets.top = totalOffset;
                } else if (location == QLatin1String("bottom")) {
                    offsets.bottom = totalOffset;
                } else if (location == QLatin1String("left")) {
                    offsets.left = totalOffset;
                } else if (location == QLatin1String("right")) {
                    offsets.right = totalOffset;
                }
            }
        } else {
            qCWarning(lcScreen) << "queryKdePlasmaPanels D-Bus query failed:" << reply.error().message();
        }

        // Log final panel offsets
        for (auto it = m_panelOffsets.constBegin(); it != m_panelOffsets.constEnd(); ++it) {
            qCInfo(lcScreen) << "  Screen" << it.key() << "panel offsets T=" << it.value().top
                              << "B=" << it.value().bottom << "L=" << it.value().left
                              << "R=" << it.value().right;
        }

        // Now recalculate geometry for all screens with updated panel data
        for (auto* screen : m_trackedScreens) {
            calculateAvailableGeometry(screen);
        }

        // Emit panelGeometryReady on first successful query
        if (!m_panelGeometryReceived) {
            m_panelGeometryReceived = true;
            qCInfo(lcScreen) << "Panel geometry ready - emitting signal";
            Q_EMIT panelGeometryReady();
        }

        // Cleanup
        delete plasmaShell;
        w->deleteLater();
    });
}

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
    qCDebug(lcScreen) << "onSensorGeometryChanged screen= " << screen->name() << " sensorGeometry= " << sensorGeom
                      << " screenGeometry= " << screen->geometry();

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
    // This can work on some Wayland compositors before sensor data is available
    QRect availGeom = screen->availableGeometry();
    QRect screenGeom = screen->geometry();

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
