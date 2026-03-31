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

#include <limits>

#include "layersurface.h"

namespace PlasmaZones {

namespace {
/// Minimum usable dimension (pixels) for a virtual screen available area.
/// If panel intersection leaves less than this, fall back to full virtual geometry.
constexpr int MinUsableScreenDimension = 100;

/// Squared edge distance from a point to a rectangle (0 if inside).
/// Uses qint64 to avoid overflow when squaring large pixel distances.
/// Uses exclusive-right semantics (x + width, y + height) to match
/// VirtualScreenDef::absoluteGeometry() which constructs QRects from
/// (left, top, width, height). This avoids dist=0 ties at boundary
/// pixels between adjacent virtual screens.
qint64 edgeDistance(const QRect& rect, const QPoint& point)
{
    const qint64 dx = qMax(0, qMax(rect.left() - point.x(), point.x() - (rect.x() + rect.width())));
    const qint64 dy = qMax(0, qMax(rect.top() - point.y(), point.y() - (rect.y() + rect.height())));
    return dx * dx + dy * dy;
}
} // anonymous namespace

// Global cache for available geometry (screen name -> geometry)
// Updated by sensor windows, read by actualAvailableGeometry()
static QHash<QString, QRect> s_availableGeometryCache;

// Global pointer to the active ScreenManager instance (for static method access)
static QPointer<ScreenManager> s_instance;

ScreenManager::ScreenManager(QObject* parent)
    : QObject(parent)
{
    Q_ASSERT_X(!s_instance, "ScreenManager", "Multiple ScreenManager instances created");
    s_instance = this;
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
    if (m_running) {
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

    // Invalidate EDID cache so a different monitor on this connector gets a fresh read.
    // The daemon also does this, but ScreenManager should be self-contained.
    Utils::invalidateEdidCache(screen->name());

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

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Screen Management
// ═══════════════════════════════════════════════════════════════════════════════

bool ScreenManager::setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config)
{
    if (config.isEmpty()) {
        if (!m_virtualConfigs.contains(physicalScreenId)) {
            return true;
        }
        m_virtualConfigs.remove(physicalScreenId);
        m_effectiveScreenIdsDirty = true;
        invalidateVirtualGeometryCache(physicalScreenId);
        Q_EMIT virtualScreensChanged(physicalScreenId);
        return true;
    }

    // Validate: physicalScreenId must match config
    if (config.physicalScreenId != physicalScreenId) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: config physicalScreenId" << config.physicalScreenId
                            << "does not match parameter" << physicalScreenId;
        return false;
    }

    // Validate: need at least 2 screens for a meaningful subdivision
    if (config.screens.size() < 2) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: need at least 2 screens for subdivision, got"
                            << config.screens.size();
        return false;
    }

    // Validate: all defs must pass isValid()
    for (const auto& def : config.screens) {
        if (!def.isValid()) {
            qCWarning(lcScreen) << "setVirtualScreenConfig: invalid VirtualScreenDef" << def.id
                                << "region:" << def.region;
            return false;
        }
    }

    // Validate: all def.id values are unique
    {
        QSet<QString> seenIds;
        for (const auto& def : config.screens) {
            if (seenIds.contains(def.id)) {
                qCWarning(lcScreen) << "setVirtualScreenConfig: duplicate def.id" << def.id;
                return false;
            }
            seenIds.insert(def.id);
        }
    }

    // Validate: all def.index values are unique
    {
        QSet<int> seenIndices;
        for (const auto& def : config.screens) {
            if (seenIndices.contains(def.index)) {
                qCWarning(lcScreen) << "setVirtualScreenConfig: duplicate def.index" << def.index;
                return false;
            }
            seenIndices.insert(def.index);
        }
    }

    // Validate: no two regions overlap (pairwise intersection check, tolerance-aware)
    for (int i = 0; i < config.screens.size(); ++i) {
        for (int j = i + 1; j < config.screens.size(); ++j) {
            QRectF intersection = config.screens[i].region.intersected(config.screens[j].region);
            if (intersection.width() > VirtualScreenDef::Tolerance
                && intersection.height() > VirtualScreenDef::Tolerance) {
                qCWarning(lcScreen) << "setVirtualScreenConfig: overlapping regions between" << config.screens[i].id
                                    << "and" << config.screens[j].id;
                return false;
            }
        }
    }

    // Validate: regions should approximately cover [0,1]x[0,1]
    qreal totalArea = 0.0;
    for (const auto& def : config.screens) {
        totalArea += def.region.width() * def.region.height();
    }
    if (totalArea < 0.99) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: insufficient coverage for" << physicalScreenId << "- total area"
                            << totalArea << "< 0.99, rejecting config";
        return false;
    }
    if (totalArea > 1.01) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: suspicious total area" << totalArea << "> 1.01 for"
                            << physicalScreenId;
    }

    if (m_virtualConfigs.value(physicalScreenId) == config) {
        return true;
    }

    m_virtualConfigs.insert(physicalScreenId, config);
    m_effectiveScreenIdsDirty = true;
    invalidateVirtualGeometryCache(physicalScreenId);
    Q_EMIT virtualScreensChanged(physicalScreenId);
    return true;
}

VirtualScreenConfig ScreenManager::virtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualConfigs.value(physicalScreenId);
}

QStringList ScreenManager::effectiveScreenIds() const
{
    if (!m_effectiveScreenIdsDirty) {
        return m_cachedEffectiveScreenIds;
    }

    QStringList result;

    for (auto* screen : m_trackedScreens) {
        QString physId = Utils::screenIdentifier(screen);
        auto it = m_virtualConfigs.constFind(physId);
        if (it != m_virtualConfigs.constEnd() && it->hasSubdivisions()) {
            for (const auto& vs : it->screens) {
                result.append(vs.id);
            }
        } else {
            result.append(physId);
        }
    }

    m_cachedEffectiveScreenIds = result;
    m_effectiveScreenIdsDirty = false;
    return result;
}

QStringList ScreenManager::virtualScreenIdsFor(const QString& physicalScreenId) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    if (it != m_virtualConfigs.constEnd() && it->hasSubdivisions()) {
        QStringList ids;
        for (const auto& vs : it->screens) {
            ids.append(vs.id);
        }
        return ids;
    }

    return {physicalScreenId};
}

QRect ScreenManager::screenGeometry(const QString& screenId) const
{
    if (VirtualScreenId::isVirtual(screenId)) {
        if (!m_virtualGeometryCache.contains(screenId)) {
            QString physId = VirtualScreenId::extractPhysicalId(screenId);
            rebuildVirtualGeometryCache(physId);
        }
        QRect cached = m_virtualGeometryCache.value(screenId);
        if (cached.isValid()) {
            return cached;
        }
    }

    // Physical screen — try tracked screens first, then fallback
    QScreen* screen = Utils::findScreenByIdOrName(screenId);
    return screen ? screen->geometry() : QRect();
}

QRect ScreenManager::screenAvailableGeometry(const QString& screenId) const
{
    if (VirtualScreenId::isVirtual(screenId)) {
        QRect vsGeom = screenGeometry(screenId);
        if (!vsGeom.isValid()) {
            return QRect();
        }

        // Get the physical screen's available geometry and intersect
        QString physId = VirtualScreenId::extractPhysicalId(screenId);
        QScreen* screen = Utils::findScreenByIdOrName(physId);
        if (!screen) {
            return vsGeom;
        }

        QRect physAvail = actualAvailableGeometry(screen);
        QRect result = vsGeom.intersected(physAvail);
        // If panel consumes most of the virtual screen, fall back to full virtual geometry
        // to avoid zero/unusable available areas
        if (!result.isValid() || result.width() < MinUsableScreenDimension
            || result.height() < MinUsableScreenDimension) {
            qCWarning(lcScreen) << "screenAvailableGeometry: panel leaves insufficient space in virtual screen"
                                << screenId << "- using full virtual geometry";
            return vsGeom;
        }
        return result;
    }

    // Physical screen
    QScreen* screen = Utils::findScreenByIdOrName(screenId);
    return screen ? actualAvailableGeometry(screen) : QRect();
}

QScreen* ScreenManager::physicalQScreenFor(const QString& screenId) const
{
    QString physId = VirtualScreenId::isVirtual(screenId) ? VirtualScreenId::extractPhysicalId(screenId) : screenId;

    return Utils::findScreenByIdOrName(physId);
}

bool ScreenManager::hasVirtualScreens(const QString& physicalScreenId) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    return it != m_virtualConfigs.constEnd() && it->hasSubdivisions();
}

VirtualScreenDef::PhysicalEdges ScreenManager::physicalEdgesFor(const QString& screenId) const
{
    // Physical screens: all edges are at the physical boundary
    if (!VirtualScreenId::isVirtual(screenId)) {
        return {true, true, true, true};
    }

    QString physId = VirtualScreenId::extractPhysicalId(screenId);
    auto it = m_virtualConfigs.constFind(physId);
    if (it == m_virtualConfigs.constEnd()) {
        return {true, true, true, true};
    }

    for (const auto& vs : it->screens) {
        if (vs.id == screenId) {
            return vs.physicalEdges();
        }
    }

    return {true, true, true, true};
}

QString ScreenManager::virtualScreenAt(const QPoint& globalPos, const QString& physicalScreenId) const
{
    QScreen* screen = Utils::findScreenByIdOrName(physicalScreenId);
    return virtualScreenAtWithScreen(globalPos, physicalScreenId, screen);
}

QString ScreenManager::virtualScreenAtWithScreen(const QPoint& globalPos, const QString& physicalScreenId,
                                                 QScreen* screen) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    if (it == m_virtualConfigs.constEnd() || !screen) {
        return {};
    }

    // Use exclusive-right semantics to match edgeDistance(): a point at x+width
    // or y+height belongs to the next virtual screen, not this one. QRect::contains()
    // is inclusive-right and would create boundary ambiguity.
    auto containsExclusive = [](const QRect& r, const QPoint& p) {
        return p.x() >= r.x() && p.x() < r.x() + r.width() && p.y() >= r.y() && p.y() < r.y() + r.height();
    };

    QRect physGeom = screen->geometry();
    for (const auto& vs : it->screens) {
        QRect absGeom = vs.absoluteGeometry(physGeom);
        if (containsExclusive(absGeom, globalPos)) {
            return vs.id;
        }
    }

    // Point falls in gap between virtual screens — find nearest by edge distance
    QString nearestId;
    qint64 minDist = std::numeric_limits<qint64>::max();
    for (const auto& vs : it->screens) {
        QRect absGeom = vs.absoluteGeometry(physGeom);
        qint64 dist = edgeDistance(absGeom, globalPos);
        if (dist < minDist) {
            minDist = dist;
            nearestId = vs.id;
        }
    }

    return nearestId;
}

QString ScreenManager::effectiveScreenAt(const QPoint& globalPos) const
{
    for (auto* screen : m_trackedScreens) {
        if (!screen->geometry().contains(globalPos)) {
            continue;
        }

        QString physId = Utils::screenIdentifier(screen);
        if (hasVirtualScreens(physId)) {
            QString vsId = virtualScreenAtWithScreen(globalPos, physId, screen);
            if (!vsId.isEmpty()) {
                return vsId;
            }
        }

        return physId;
    }

    return {};
}

void ScreenManager::invalidateVirtualGeometryCache(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        m_virtualGeometryCache.clear();
        return;
    }

    // Remove all cached entries belonging to this physical screen
    const QString prefix = physicalScreenId + VirtualScreenId::separator();
    auto it = m_virtualGeometryCache.begin();
    while (it != m_virtualGeometryCache.end()) {
        if (it.key().startsWith(prefix)) {
            it = m_virtualGeometryCache.erase(it);
        } else {
            ++it;
        }
    }
}

void ScreenManager::rebuildVirtualGeometryCache(const QString& physicalScreenId) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    if (it == m_virtualConfigs.constEnd()) {
        return;
    }

    QScreen* screen = Utils::findScreenByIdOrName(physicalScreenId);
    if (!screen) {
        return;
    }

    QRect physGeom = screen->geometry();
    for (const auto& vs : it->screens) {
        m_virtualGeometryCache.insert(vs.id, vs.absoluteGeometry(physGeom));
    }
}

} // namespace PlasmaZones
