// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/Manager.h"

#include "PhosphorScreens/IConfigStore.h"
#include "PhosphorScreens/IPanelSource.h"
#include "PhosphorScreens/ScreenIdentity.h"
#include "screenslogging.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <PhosphorShell/LayerSurface.h>

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWindow>

using PhosphorShell::LayerSurface;

namespace Phosphor::Screens {

namespace {
/// Build a stable identifier for a screen — preferring `QScreen::name()`
/// (the connector). Mirrors the daemon's per-process screen identifier so
/// log messages line up.
QString screenLabel(QScreen* screen)
{
    return screen ? screen->name() : QStringLiteral("(null screen)");
}
} // namespace

ScreenManager::ScreenManager(ScreenManagerConfig cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
{
}

ScreenManager::~ScreenManager()
{
    stop();
}

void ScreenManager::start()
{
    if (m_running) {
        return;
    }
    m_running = true;

    connect(qApp, &QGuiApplication::screenAdded, this, &ScreenManager::onScreenAdded);
    connect(qApp, &QGuiApplication::screenRemoved, this, &ScreenManager::onScreenRemoved);

    if (m_cfg.panelSource) {
        connect(m_cfg.panelSource, &IPanelSource::panelOffsetsChanged, this, &ScreenManager::onPanelOffsetsChanged);
        connect(m_cfg.panelSource, &IPanelSource::requeryCompleted, this, &ScreenManager::onPanelRequeryCompleted);
        m_cfg.panelSource->start();
    } else {
        // No panel source — emit panelGeometryReady on the next event-loop
        // tick so listeners that gate on it don't hang. The bit is set
        // inside the deferred lambda so callers invoking
        // isPanelGeometryReady() BEFORE the tick fires (in tests especially)
        // see the false state they expect.
        //
        // Guarded by m_running so a stop() between this schedule and the
        // tick firing doesn't surface a stale readiness signal to listeners
        // that already tore down their wiring.
        QTimer::singleShot(0, this, [this]() {
            if (m_running && !m_panelGeometryReadyEmitted) {
                m_panelGeometryReadyEmitted = true;
                Q_EMIT panelGeometryReady();
            }
        });
    }

    if (m_cfg.configStore) {
        connect(m_cfg.configStore, &IConfigStore::changed, this, &ScreenManager::onConfigStoreChanged);
        // Initial cache population.
        refreshVirtualConfigs(m_cfg.configStore->loadAll());
    }

    for (auto* screen : QGuiApplication::screens()) {
        if (!m_trackedScreens.contains(screen)) {
            connectScreenSignals(screen);
            m_trackedScreens.append(screen);
            if (m_cfg.useGeometrySensors) {
                createGeometrySensor(screen);
            }
        }
    }
}

void ScreenManager::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;

    if (m_cfg.panelSource) {
        m_cfg.panelSource->stop();
        disconnect(m_cfg.panelSource, nullptr, this, nullptr);
    }
    if (m_cfg.configStore) {
        disconnect(m_cfg.configStore, nullptr, this, nullptr);
    }

    while (!m_geometrySensors.isEmpty()) {
        destroyGeometrySensor(m_geometrySensors.begin().key());
    }

    for (auto* screen : m_trackedScreens) {
        disconnectScreenSignals(screen);
    }
    m_trackedScreens.clear();

    disconnect(qApp, &QGuiApplication::screenAdded, this, nullptr);
    disconnect(qApp, &QGuiApplication::screenRemoved, this, nullptr);

    m_availableGeometryCache.clear();
    // Invalidate derived caches so a post-stop effectiveScreenIds() /
    // screenGeometry(vsId) call doesn't return pre-stop IDs or regions.
    // m_virtualConfigs stays — the IConfigStore is authoritative and
    // clearing it would drop legitimate pending state between stop/start
    // cycles. m_panelGeometryReadyEmitted stays for the same reason (a
    // restart shouldn't re-fire the one-shot unless the panel source
    // actually re-transitions to ready).
    m_cachedEffectiveScreenIds.clear();
    m_effectiveScreenIdsDirty = true;
    m_virtualGeometryCache.clear();
}

QVector<QScreen*> ScreenManager::screens() const
{
    if (!qApp) {
        qCCritical(lcPhosphorScreens) << "screens() called before QGuiApplication initialized";
        return {};
    }
    const auto screenList = QGuiApplication::screens();
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

    auto* sensor = new QWindow();
    sensor->setScreen(screen);
    sensor->setFlag(Qt::FramelessWindowHint);
    sensor->setFlag(Qt::BypassWindowManagerHint);
    sensor->setObjectName(QStringLiteral("GeometrySensor-%1").arg(screen->name()));

    auto* layerSurface = LayerSurface::get(sensor);
    if (!layerSurface) {
        qCWarning(lcPhosphorScreens) << "Failed to create layer surface for sensor on" << screenLabel(screen);
        delete sensor;
        return;
    }

    LayerSurface::BatchGuard guard(layerSurface);
    layerSurface->setScreen(screen);
    layerSurface->setLayer(LayerSurface::LayerBackground);
    layerSurface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityNone);
    layerSurface->setAnchors(LayerSurface::AnchorAll);
    layerSurface->setExclusiveZone(0);
    layerSurface->setScope(QStringLiteral("phosphor-screens-sensor-%1").arg(screen->name()));

    connect(sensor, &QWindow::widthChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });
    connect(sensor, &QWindow::heightChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });
    connect(sensor, &QWindow::xChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });
    connect(sensor, &QWindow::yChanged, this, [this, screen]() {
        onSensorGeometryChanged(screen);
    });

    m_geometrySensors.insert(screen, sensor);

    sensor->setGeometry(screen->geometry());
    sensor->show();

    // Kick the panel source so it queries (or re-queries) for the new screen.
    if (m_cfg.panelSource) {
        m_cfg.panelSource->requestRequery();
    }
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
        m_availableGeometryCache.remove(screen->name());
    }
}

void ScreenManager::calculateAvailableGeometry(QScreen* screen)
{
    if (!screen) {
        return;
    }

    const QRect screenGeom = screen->geometry();
    const QString connectorName = screen->name();

    qCDebug(lcPhosphorScreens) << "calculateAvailableGeometry: screen=" << connectorName << "geometry=" << screenGeom;

    // Panel offsets via the injected source (zero offsets if no source).
    IPanelSource::Offsets panel =
        m_cfg.panelSource ? m_cfg.panelSource->currentOffsets(screen) : IPanelSource::Offsets{};
    const bool hasPanelData = !panel.isZero();

    // Layer-shell sensor — real-time available SIZE from the compositor.
    // Sensor position is unreliable on Wayland; only its size matters.
    QRect sensorGeom;
    bool hasSensorData = false;
    if (auto sensor = m_geometrySensors.value(screen); sensor && sensor->isVisible()) {
        sensorGeom = sensor->geometry();
        hasSensorData = sensorGeom.isValid() && sensorGeom.width() > 0 && sensorGeom.height() > 0;
    }

    int availX = screenGeom.x() + panel.left;
    int availY = screenGeom.y() + panel.top;
    int finalWidth = 0;
    int finalHeight = 0;

    const int dbusWidth = screenGeom.width() - panel.left - panel.right;
    const int dbusHeight = screenGeom.height() - panel.top - panel.bottom;

    if (hasSensorData && hasPanelData) {
        // Both sources — use the smaller size to handle floating panels
        // (sensor full screen, D-Bus knows panel) AND panel-edit cases
        // (sensor accurate, D-Bus stale).
        finalWidth = qMin(sensorGeom.width(), dbusWidth);
        finalHeight = qMin(sensorGeom.height(), dbusHeight);
        if (qAbs(dbusHeight - sensorGeom.height()) > 5 || qAbs(dbusWidth - sensorGeom.width()) > 5) {
            qCDebug(lcPhosphorScreens) << "D-Bus vs Sensor mismatch."
                                       << "D-Bus:" << dbusWidth << "x" << dbusHeight << "Sensor:" << sensorGeom.width()
                                       << "x" << sensorGeom.height() << "Using:" << finalWidth << "x" << finalHeight;
        }
    } else if (hasSensorData) {
        const bool panelReady = m_cfg.panelSource && m_cfg.panelSource->ready();
        if (panelReady && (sensorGeom.width() < screenGeom.width() || sensorGeom.height() < screenGeom.height())) {
            qCDebug(lcPhosphorScreens) << "Sensor for" << connectorName << "reports" << sensorGeom.size()
                                       << "but panel source found no panels on this screen."
                                       << "Using full screen geometry instead.";
            finalWidth = screenGeom.width();
            finalHeight = screenGeom.height();
        } else {
            finalWidth = sensorGeom.width();
            finalHeight = sensorGeom.height();
        }
    } else if (hasPanelData) {
        finalWidth = dbusWidth;
        finalHeight = dbusHeight;
    } else {
        availX = screenGeom.x();
        availY = screenGeom.y();
        finalWidth = screenGeom.width();
        finalHeight = screenGeom.height();
    }

    const QRect availGeom(availX, availY, finalWidth, finalHeight);
    const QString screenKey = screen->name();
    const QRect oldGeom = m_availableGeometryCache.value(screenKey);

    if (availGeom == oldGeom) {
        return;
    }

    const QString source = hasSensorData ? QStringLiteral("sensor")
                                         : (hasPanelData ? QStringLiteral("D-Bus") : QStringLiteral("fallback"));
    qCInfo(lcPhosphorScreens) << "calculateAvailableGeometry: screen=" << screenKey << "screenGeom=" << screenGeom
                              << "available=" << availGeom << "source=" << source;

    m_availableGeometryCache.insert(screenKey, availGeom);
    Q_EMIT availableGeometryChanged(screen, availGeom);
}

void ScreenManager::onSensorGeometryChanged(QScreen* screen)
{
    if (!screen) {
        return;
    }
    auto sensor = m_geometrySensors.value(screen);
    if (!sensor) {
        return;
    }
    const QRect sensorGeom = sensor->geometry();
    qCDebug(lcPhosphorScreens) << "onSensorGeometryChanged: screen=" << screen->name()
                               << "sensorGeometry=" << sensorGeom << "screenGeometry=" << screen->geometry();
    if (!sensorGeom.isValid() || sensorGeom.width() <= 0 || sensorGeom.height() <= 0) {
        return;
    }
    // Sensor moved — re-query panels (handles panels added/removed/resized).
    if (m_cfg.panelSource) {
        m_cfg.panelSource->requestRequery();
    } else {
        calculateAvailableGeometry(screen);
    }
}

void ScreenManager::onPanelOffsetsChanged(QScreen* screen)
{
    calculateAvailableGeometry(screen);

    // First-ready transition — emit panelGeometryReady exactly once.
    if (!m_panelGeometryReadyEmitted && m_cfg.panelSource && m_cfg.panelSource->ready()) {
        m_panelGeometryReadyEmitted = true;
        qCInfo(lcPhosphorScreens) << "panelGeometryReady";
        Q_EMIT panelGeometryReady();
    }
}

void ScreenManager::onPanelRequeryCompleted()
{
    Q_EMIT delayedPanelRequeryCompleted();
}

void ScreenManager::onConfigStoreChanged()
{
    if (m_cfg.configStore) {
        refreshVirtualConfigs(m_cfg.configStore->loadAll());
    }
}

QRect ScreenManager::actualAvailableGeometry(QScreen* screen) const
{
    if (!screen) {
        return QRect();
    }
    const QString screenKey = screen->name();
    if (m_availableGeometryCache.contains(screenKey)) {
        const QRect cached = m_availableGeometryCache.value(screenKey);
        qCDebug(lcPhosphorScreens) << "actualAvailableGeometry: screen=" << screenKey << "cached=" << cached;
        return cached;
    }
    // No cached value — fall back to Qt's availableGeometry, with full
    // screen geometry as a final fallback. Caches the Qt fallback so
    // subsequent calls don't re-evaluate the same branch on cold start.
    const QRect availGeom = screen->availableGeometry();
    const QRect screenGeom = screen->geometry();
    qCInfo(lcPhosphorScreens) << "actualAvailableGeometry: screen=" << screenKey
                              << "no cache, fallback qtAvail=" << availGeom << "fullScreen=" << screenGeom;
    if (availGeom != screenGeom && availGeom.isValid()) {
        m_availableGeometryCache.insert(screenKey, availGeom);
        return availGeom;
    }
    return screenGeom;
}

bool ScreenManager::isPanelGeometryReady() const
{
    // Tracks the first @ref panelGeometryReady emission, not the panel
    // source's own `ready()` flag. This keeps the answer consistent
    // with "has the `panelGeometryReady` signal fired yet?" which is
    // what every consumer actually cares about.
    //
    // In particular it stays false for a freshly-constructed
    // ScreenManager that hasn't had `start()` called, even when no
    // panel source is configured — tests rely on that to simulate
    // the "waiting for panel geometry" state without running a real
    // Plasma shell.
    return m_panelGeometryReadyEmitted;
}

void ScreenManager::scheduleDelayedPanelRequery(int delayMs)
{
    if (!m_cfg.panelSource) {
        return;
    }
    // Forward the caller's delay verbatim — the panel source's contract
    // treats delayMs<=0 as "immediate" (matches
    // IPanelSource::requestRequery), so this wrapper preserves that
    // semantic instead of silently dropping the call. Callers that truly
    // want a delay pass a positive value.
    m_cfg.panelSource->requestRequery(delayMs);
}

void ScreenManager::onScreenAdded(QScreen* screen)
{
    if (!screen || m_trackedScreens.contains(screen)) {
        return;
    }
    connectScreenSignals(screen);
    m_trackedScreens.append(screen);
    m_effectiveScreenIdsDirty = true;
    if (m_cfg.useGeometrySensors) {
        createGeometrySensor(screen);
    }
    Q_EMIT screenAdded(screen);
}

void ScreenManager::onScreenRemoved(QScreen* screen)
{
    if (!screen) {
        return;
    }
    // Use the EDID-aware identifier here — the QScreen* will be
    // destroyed shortly, so we capture the stable ID for cache
    // invalidation while the pointer is still valid.
    const QString physId = ScreenIdentity::identifierFor(screen);
    invalidateVirtualGeometryCache(physId);
    destroyGeometrySensor(screen);
    disconnectScreenSignals(screen);
    m_trackedScreens.removeAll(screen);
    // Do NOT clear m_virtualConfigs[physId] — the host's IConfigStore is the
    // authoritative source. Stale entries for unconnected screens are filtered
    // by effectiveScreenIds().

    // Drop EDID-cache entries pinned to this connector so a different
    // monitor on the same port gets fresh identifier resolution next time.
    ScreenIdentity::invalidateEdidCache(screen->name());
    m_effectiveScreenIdsDirty = true;

    Q_EMIT screenRemoved(screen);
}

void ScreenManager::onScreenGeometryChanged(const QRect& geometry)
{
    auto* screen = qobject_cast<QScreen*>(sender());
    if (!screen) {
        return;
    }
    const QString physId = ScreenIdentity::identifierFor(screen);
    if (!physId.isEmpty()) {
        invalidateVirtualGeometryCache(physId);
    } else {
        invalidateVirtualGeometryCache();
    }
    m_effectiveScreenIdsDirty = true;
    Q_EMIT screenGeometryChanged(screen, geometry);
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

} // namespace Phosphor::Screens
