// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/Manager.h"

#include "PhosphorScreens/IConfigStore.h"
#include "PhosphorScreens/IPanelSource.h"
#include "PhosphorScreens/IScreenProvider.h"
#include "PhosphorScreens/QtScreenProvider.h"
#include "screenslogging.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <PhosphorWayland/LayerSurface.h>

#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWindow>

using PhosphorWayland::LayerSurface;

namespace PhosphorScreens {

ScreenManager::ScreenManager(ScreenManagerConfig cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
    // Default to a live Qt-backed provider when the host didn't inject one.
    // Parented to `this`, so the manager owns it and it shares the manager's
    // lifetime — matching the "consumer owns an injected provider, manager
    // owns the default" split documented on ScreenManagerConfig.
    , m_screenProvider(m_cfg.screenProvider ? m_cfg.screenProvider : new QtScreenProvider(this))
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

    connect(m_screenProvider, &IScreenProvider::screenAdded, this, &ScreenManager::onProviderScreenAdded);
    connect(m_screenProvider, &IScreenProvider::screenRemoved, this, &ScreenManager::onProviderScreenRemoved);
    connect(m_screenProvider, &IScreenProvider::screenGeometryChanged, this,
            &ScreenManager::onProviderScreenGeometryChanged);

    // Populate m_trackedScreens BEFORE any signal-emitting step that follows
    // (panelSource->start, refreshVirtualConfigs). A synchronous IPanelSource
    // implementation — NoOpPanelSource is the canonical example — fans out
    // panelOffsetsChanged during start(); listeners that query
    // effectiveScreenIds() from that handler must observe a coherent tracked-
    // screen set, not the empty pre-populate state. The configStore path has
    // the same invariant for virtualScreensChanged.
    //
    // Geometry sensors are deliberately created AFTER panelSource->start()
    // below — createGeometrySensor kicks a requestRequery on the panel
    // source, which is a no-op if the source isn't running yet. Running it
    // post-start means the kick actually lands, instead of being silently
    // swallowed by PlasmaPanelSource::issueQuery's !m_running guard.
    syncTrackedScreens();

    // Initial population changes the effective screen set. Ensure the first
    // effectiveScreenIds() call after start() rebuilds instead of returning a
    // stale empty cache.
    m_effectiveScreenIdsDirty = true;

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

    // Geometry sensors last — the panel source is live now so the per-screen
    // requestRequery() kick inside createGeometrySensor actually lands. See
    // the ordering rationale on the syncTrackedScreens call above.
    if (m_cfg.useGeometrySensors) {
        for (const auto& screen : std::as_const(m_trackedScreens)) {
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

    if (m_cfg.panelSource) {
        m_cfg.panelSource->stop();
        // Explicit per-signal disconnects (mirroring the start() connects)
        // so a future signal added to IPanelSource doesn't get silently
        // torn down by a blanket `disconnect(source, nullptr, this, nullptr)`.
        disconnect(m_cfg.panelSource, &IPanelSource::panelOffsetsChanged, this, &ScreenManager::onPanelOffsetsChanged);
        disconnect(m_cfg.panelSource, &IPanelSource::requeryCompleted, this, &ScreenManager::onPanelRequeryCompleted);
    }
    if (m_cfg.configStore) {
        disconnect(m_cfg.configStore, &IConfigStore::changed, this, &ScreenManager::onConfigStoreChanged);
    }

    while (!m_geometrySensors.isEmpty()) {
        destroyGeometrySensor(m_geometrySensors.begin().key());
    }

    m_trackedScreens.clear();

    // Explicit per-signal disconnects, matching the start() connects.
    disconnect(m_screenProvider, &IScreenProvider::screenAdded, this, &ScreenManager::onProviderScreenAdded);
    disconnect(m_screenProvider, &IScreenProvider::screenRemoved, this, &ScreenManager::onProviderScreenRemoved);
    disconnect(m_screenProvider, &IScreenProvider::screenGeometryChanged, this,
               &ScreenManager::onProviderScreenGeometryChanged);

    m_availableGeometryCache.clear();
    // Invalidate derived caches so a post-stop effectiveScreenIds() /
    // screenGeometry(vsId) call doesn't return pre-stop IDs or regions.
    // m_virtualConfigs stays — the IConfigStore is authoritative and
    // clearing it would drop legitimate pending state between stop/start
    // cycles.
    //
    // m_panelGeometryReadyEmitted is a process-lifetime latch: once set to
    // true it stays that way across stop()/start() cycles for the life of
    // the object. This is deliberate and matches how consumers use the
    // `panelGeometryReady` signal — they subscribe once at startup, wait
    // for the first emission, then treat the answer as "panels have been
    // observed at least once in this session". The daemon is a single-shot
    // process so the latch is effectively permanent there; the behaviour
    // also keeps test determinism predictable (testPanelGeometryReadyFires
    // OnceEvenAcrossRestart in test_manager_lifecycle.cpp pins it). Long-
    // lived embeddings that truly need re-ready semantics should create a
    // fresh ScreenManager instead of cycling stop/start — cheap, no hidden
    // global state.
    m_cachedEffectiveScreenIds.clear();
    m_effectiveScreenIdsDirty = true;
    m_virtualGeometryCache.clear();

    // Drop compositor-reported overrides — the KWin effect re-pushes a fresh
    // clientArea snapshot for every screen when it re-registers the bridge,
    // so keeping stale rects across a stop/start cycle would only risk a
    // transient mismatch before that push lands.
    m_compositorAvailableGeometry.clear();
}

QVector<PhysicalScreen> ScreenManager::screens() const
{
    return m_screenProvider->screens();
}

PhysicalScreen ScreenManager::primaryScreen() const
{
    return m_screenProvider->primaryScreen();
}

PhysicalScreen ScreenManager::screenByName(const QString& name) const
{
    const auto all = m_screenProvider->screens();
    for (const auto& screen : all) {
        if (screen.name == name) {
            return screen;
        }
    }
    return {};
}

void ScreenManager::syncTrackedScreens()
{
    // The provider is the single source of truth for the connected-output
    // set. Every lifecycle slot resyncs through here rather than mutating
    // m_trackedScreens incrementally, so the tracked geometry/identifier
    // snapshots never drift from the provider's current view.
    m_trackedScreens = m_screenProvider->screens();
}

PhysicalScreen ScreenManager::trackedScreenByName(const QString& name) const
{
    for (const auto& screen : m_trackedScreens) {
        if (screen.name == name) {
            return screen;
        }
    }
    return {};
}

PhysicalScreen ScreenManager::trackedScreenFor(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        // Empty resolves to the primary output — mirrors the historical
        // ScreenIdentity::findByIdOrName contract callers depend on.
        return trackedScreenByName(m_screenProvider->primaryScreen().name);
    }
    // A physical screen ID is either the EDID-aware identifier or the bare
    // connector name; accept both. The disambiguated "base/CONNECTOR" form
    // matches the identifier branch exactly — it is what the provider
    // stamped onto PhysicalScreen::identifier.
    for (const auto& screen : m_trackedScreens) {
        if (screen.identifier == screenId || screen.name == screenId) {
            return screen;
        }
    }
    return {};
}

void ScreenManager::createGeometrySensor(const PhysicalScreen& screen)
{
    if (!screen.isValid() || m_geometrySensors.contains(screen.name)) {
        return;
    }
    // Layer-shell sensor windows need a real output to attach to. A
    // synthetic screen (FakeScreenProvider, no QScreen) gets none —
    // calculateAvailableGeometry falls back to the panel-source / Qt path
    // for it, which is exactly the behaviour a headless test wants.
    if (!screen.qscreen) {
        return;
    }

    auto* sensor = new QWindow();
    sensor->setScreen(screen.qscreen);
    sensor->setFlag(Qt::FramelessWindowHint);
    sensor->setFlag(Qt::BypassWindowManagerHint);
    sensor->setObjectName(QStringLiteral("GeometrySensor-%1").arg(screen.name));

    auto* layerSurface = LayerSurface::get(sensor);
    if (!layerSurface) {
        qCWarning(lcPhosphorScreens) << "Failed to create layer surface for sensor on" << screen.name;
        delete sensor;
        return;
    }

    LayerSurface::BatchGuard guard(layerSurface);
    layerSurface->setScreen(screen.qscreen);
    layerSurface->setLayer(LayerSurface::LayerBackground);
    layerSurface->setKeyboardInteractivity(LayerSurface::KeyboardInteractivityNone);
    layerSurface->setAnchors(LayerSurface::AnchorAll);
    layerSurface->setExclusiveZone(0);
    layerSurface->setScope(QStringLiteral("phosphor-screens-sensor-%1").arg(screen.name));

    const QString screenName = screen.name;
    connect(sensor, &QWindow::widthChanged, this, [this, screenName]() {
        onSensorGeometryChanged(screenName);
    });
    connect(sensor, &QWindow::heightChanged, this, [this, screenName]() {
        onSensorGeometryChanged(screenName);
    });
    connect(sensor, &QWindow::xChanged, this, [this, screenName]() {
        onSensorGeometryChanged(screenName);
    });
    connect(sensor, &QWindow::yChanged, this, [this, screenName]() {
        onSensorGeometryChanged(screenName);
    });

    m_geometrySensors.insert(screen.name, sensor);

    sensor->setGeometry(screen.geometry);
    sensor->show();

    // Kick the panel source so it queries (or re-queries) for the new screen.
    if (m_cfg.panelSource) {
        m_cfg.panelSource->requestRequery();
    }
}

void ScreenManager::destroyGeometrySensor(const QString& screenName)
{
    auto sensor = m_geometrySensors.take(screenName);
    if (sensor) {
        sensor->disconnect();
        sensor->hide();
        sensor->deleteLater();
    }
    m_availableGeometryCache.remove(screenName);
    // A disconnected screen's compositor override is meaningless — drop it so
    // a same-connector reconnect starts from the heuristic until the effect
    // re-pushes a fresh clientArea for the new output.
    m_compositorAvailableGeometry.remove(screenName);
}

void ScreenManager::calculateAvailableGeometry(const PhysicalScreen& screen)
{
    if (!screen.isValid()) {
        return;
    }

    const QRect screenGeom = screen.geometry;
    const QString screenKey = screen.name;

    qCDebug(lcPhosphorScreens) << "calculateAvailableGeometry: screen=" << screenKey << "geometry=" << screenGeom;

    // Two information sources with complementary authority:
    //
    //  1. Layer-shell sensor (AnchorAll, exclusiveZone=0). The compositor
    //     sizes it to fill the region not claimed by other exclusive-zone
    //     surfaces. Its *size* is authoritative — it reflects every panel
    //     actually reserving space on this output (Plasma panels, non-Plasma
    //     layer surfaces, auto-hide panels in their current state, etc.).
    //     Its *position* is NOT authoritative here because our layer-shell
    //     QPA plugin synthesizes QWindow::geometry() from anchors + the full
    //     screen rect, not the compositor's actual placement (layer-shell
    //     protocol has no server→client position event).
    //
    //  2. plasmashell D-Bus script eval. Tells us *which edge* each Plasma
    //     panel lives on plus a per-edge thickness. The thickness is NOT
    //     authoritative — Plasma 6 reports `floating=true` for panels that
    //     nonetheless reserve exclusive space, and the scripting API's
    //     `panel.geometry` is undefined in recent versions. This source
    //     contributes directional ratios only.
    //
    // Reconciliation: the sensor tells us the total reserved in each axis;
    // D-Bus ratios distribute that total across the two edges on that axis.
    // Truly-floating or hidden-autohide panels self-correct — the sensor
    // shows zero reservation, the scale factor collapses to zero, and their
    // D-Bus offsets have no effect.

    IPanelSource::Offsets panel =
        m_cfg.panelSource ? m_cfg.panelSource->currentOffsets(screen.qscreen) : IPanelSource::Offsets{};

    QRect sensorGeom;
    bool hasSensorData = false;
    if (auto sensor = m_geometrySensors.value(screen.name); sensor && sensor->isVisible()) {
        sensorGeom = sensor->geometry();
        hasSensorData = sensorGeom.isValid() && sensorGeom.width() > 0 && sensorGeom.height() > 0;
    }

    int effTop = 0;
    int effBottom = 0;
    int effLeft = 0;
    int effRight = 0;
    QString source;

    if (hasSensorData) {
        // Sensor authoritatively owns the total. Distribute per D-Bus ratios.
        const int vertReserved = qMax(0, screenGeom.height() - sensorGeom.height());
        const int horizReserved = qMax(0, screenGeom.width() - sensorGeom.width());
        const int dbusVert = panel.top + panel.bottom;
        const int dbusHoriz = panel.left + panel.right;

        if (dbusVert > 0) {
            // Split vertReserved in the ratio D-Bus claims for top:bottom.
            effTop = qRound(panel.top * double(vertReserved) / dbusVert);
            effBottom = vertReserved - effTop;
        } else if (vertReserved > 0) {
            // Sensor shows vertical reservation but D-Bus has no panels on
            // the top/bottom edges (panel source stale, absent, or non-
            // Plasma layer surfaces). We have the total but no direction
            // — attribute to the bottom as a safe default (matches the
            // single-dock common case). Zones will be slightly wrong for
            // top-only-panel setups in this degenerate state; this path is
            // transient unless panel detection is permanently broken.
            effTop = 0;
            effBottom = vertReserved;
        }
        if (dbusHoriz > 0) {
            effLeft = qRound(panel.left * double(horizReserved) / dbusHoriz);
            effRight = horizReserved - effLeft;
        } else if (horizReserved > 0) {
            effLeft = 0;
            effRight = horizReserved;
        }

        if (dbusVert > 0 || dbusHoriz > 0) {
            source = QStringLiteral("sensor+D-Bus");
            if (dbusVert > vertReserved || dbusHoriz > horizReserved) {
                qCDebug(lcPhosphorScreens)
                    << "D-Bus over-reports reservation on" << screenKey << "— D-Bus vert=" << dbusVert
                    << "sensor vert=" << vertReserved << "— scaling down (likely floating/autohide panels)";
            } else if (dbusVert < vertReserved || dbusHoriz < horizReserved) {
                qCDebug(lcPhosphorScreens)
                    << "D-Bus under-reports reservation on" << screenKey << "— D-Bus vert=" << dbusVert
                    << "sensor vert=" << vertReserved << "— non-Plasma layer surface suspected";
            }
        } else if (vertReserved > 0 || horizReserved > 0) {
            source = QStringLiteral("sensor-only");
        } else {
            source = QStringLiteral("sensor+no-panels");
        }
    } else if (!panel.isZero()) {
        // No sensor yet. Trust D-Bus directly until the compositor configures
        // the sensor (typically sub-frame).
        effTop = panel.top;
        effBottom = panel.bottom;
        effLeft = panel.left;
        effRight = panel.right;
        source = QStringLiteral("D-Bus");
    } else {
        source = QStringLiteral("fallback");
    }

    QRect availGeom(screenGeom.x() + effLeft, screenGeom.y() + effTop, screenGeom.width() - effLeft - effRight,
                    screenGeom.height() - effTop - effBottom);

    // Source 0 (highest priority): compositor-reported client area. When the
    // KWin effect has pushed a rect for this screen it overrides the sensor +
    // D-Bus heuristic computed above; the heuristic remains the fallback for
    // sessions where the effect is not loaded. See setCompositorAvailableGeometry.
    if (const auto compIt = m_compositorAvailableGeometry.constFind(screenKey);
        compIt != m_compositorAvailableGeometry.constEnd()) {
        // Clamp to the current screen rect — the compositor snapshot can lag a
        // screen resize by a frame, and an available rect spilling past the
        // output would corrupt every downstream relative-geometry calculation.
        const QRect clamped = compIt.value().intersected(screenGeom);
        // QRect::isValid() is the exact negation of isEmpty() — a valid rect
        // is non-empty by definition, so isValid() alone is the usable check.
        if (clamped.isValid()) {
            availGeom = clamped;
            source = QStringLiteral("compositor");
        }
    }

    const QRect oldGeom = m_availableGeometryCache.value(screenKey);
    if (availGeom == oldGeom) {
        return;
    }

    // Log effective insets derived from the final rect so the line is correct
    // regardless of which source won (the eff* locals reflect only the
    // heuristic path and would be stale under a compositor override).
    qCInfo(lcPhosphorScreens) << "calculateAvailableGeometry: screen=" << screenKey << "screenGeom=" << screenGeom
                              << "available=" << availGeom << "source=" << source
                              << "effOffsets L=" << (availGeom.x() - screenGeom.x())
                              << "T=" << (availGeom.y() - screenGeom.y())
                              << "R=" << (screenGeom.right() - availGeom.right())
                              << "B=" << (screenGeom.bottom() - availGeom.bottom());

    m_availableGeometryCache.insert(screenKey, availGeom);
    Q_EMIT availableGeometryChanged(screen, availGeom);
}

void ScreenManager::onSensorGeometryChanged(const QString& screenName)
{
    auto sensor = m_geometrySensors.value(screenName);
    if (!sensor) {
        return;
    }
    const PhysicalScreen screen = trackedScreenByName(screenName);
    if (!screen.isValid()) {
        return;
    }
    const QRect sensorGeom = sensor->geometry();
    qCDebug(lcPhosphorScreens) << "onSensorGeometryChanged: screen=" << screenName << "sensorGeometry=" << sensorGeom
                               << "screenGeometry=" << screen.geometry;
    if (!sensorGeom.isValid() || sensorGeom.width() <= 0 || sensorGeom.height() <= 0) {
        return;
    }
    // Kick the panel source so it picks up add/remove/resize of a panel that
    // might have caused the sensor reflow. But always recompute the
    // available-geometry cache for this screen — sensor SIZE is an
    // independent input to the final rect, and a requery that returns
    // identical offsets will NOT emit panelOffsetsChanged, leaving the cache
    // stale for sensor-only changes (e.g. floating-panel ↔ reserved-panel
    // flip).
    if (m_cfg.panelSource) {
        m_cfg.panelSource->requestRequery();
    }
    calculateAvailableGeometry(screen);
}

void ScreenManager::onPanelOffsetsChanged(QScreen* screen)
{
    // The panel source still identifies screens by QScreen* — resolve to the
    // tracked PhysicalScreen so the recompute runs against the provider's
    // geometry snapshot. An untracked screen (hotplug race) is skipped; the
    // panelGeometryReady latch below still advances.
    if (screen) {
        const PhysicalScreen tracked = trackedScreenByName(screen->name());
        if (tracked.isValid()) {
            calculateAvailableGeometry(tracked);
        }
    }

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

QRect ScreenManager::actualAvailableGeometry(const PhysicalScreen& screen) const
{
    PS_SCREEN_MANAGER_ASSERT_GUI_THREAD();
    if (!screen.isValid()) {
        return QRect();
    }
    const QString screenKey = screen.name;
    if (m_availableGeometryCache.contains(screenKey)) {
        // No qCDebug on cache hits — this is a hot read path called from
        // every overlay-window geometry update; per-call logging floods
        // logs when QT_LOGGING_RULES=*=true is set during debug. The
        // cache-miss path below stays informational (qCInfo) since it
        // only fires once per screen at first access.
        return m_availableGeometryCache.value(screenKey);
    }
    // No cached value — fall back to Qt's availableGeometry, with full
    // screen geometry as a final fallback. A synthetic screen (no QScreen)
    // has no Qt availableGeometry, so its full rect is the only answer.
    // Caches the fallback so subsequent calls don't re-evaluate the same
    // branch on cold start.
    const QRect screenGeom = screen.geometry;
    const QRect availGeom = screen.qscreen ? screen.qscreen->availableGeometry() : screenGeom;
    qCInfo(lcPhosphorScreens) << "actualAvailableGeometry: screen=" << screenKey
                              << "no cache, fallback qtAvail=" << availGeom << "fullScreen=" << screenGeom;
    if (availGeom != screenGeom && availGeom.isValid()) {
        m_availableGeometryCache.insert(screenKey, availGeom);
        return availGeom;
    }
    return screenGeom;
}

QRect ScreenManager::actualAvailableGeometry(QScreen* screen) const
{
    PS_SCREEN_MANAGER_ASSERT_GUI_THREAD();
    if (!screen) {
        return QRect();
    }
    // Resolve against the tracked set so the value-typed overload's cache
    // (keyed by connector name, populated for tracked screens) stays
    // consistent. A live QScreen the manager has not tracked yet — a
    // hotplug race — misses the tracked set; fall back to the QScreen's own
    // availableGeometry rather than surface an empty rect.
    const PhysicalScreen tracked = trackedScreenByName(screen->name());
    return tracked.isValid() ? actualAvailableGeometry(tracked) : screen->availableGeometry();
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

void ScreenManager::setCompositorAvailableGeometry(const QString& screenName, const QRect& available)
{
    if (screenName.isEmpty()) {
        return;
    }

    // An invalid/empty rect clears the override and reverts the screen to the
    // sensor + D-Bus heuristic. A valid rect records it as the authoritative
    // source. Both paths early-return when nothing actually changed so a
    // redundant push (the effect re-reports every screen on each trigger)
    // collapses to a no-op instead of churning availableGeometryChanged.
    // QRect::isValid() already implies a non-empty rect (isEmpty() == !isValid()),
    // so it covers every zero- or negative-size payload the effect can send.
    const bool valid = available.isValid();
    if (valid) {
        if (m_compositorAvailableGeometry.value(screenName) == available) {
            return;
        }
        // Stored unclamped on purpose: calculateAvailableGeometry re-intersects
        // it with the current screen rect on every recompute, so an override
        // that arrives before a screen-resize is processed self-corrects once
        // the new geometry lands — no need to re-push or re-clamp here.
        m_compositorAvailableGeometry.insert(screenName, available);
        qCInfo(lcPhosphorScreens) << "Compositor available geometry for" << screenName << "=" << available;
    } else {
        if (m_compositorAvailableGeometry.remove(screenName) == 0) {
            return;
        }
        qCInfo(lcPhosphorScreens) << "Compositor available geometry cleared for" << screenName;
    }

    // Recompute against the new authoritative source. calculateAvailableGeometry
    // diffs against m_availableGeometryCache and only emits availableGeometryChanged
    // on a real change, so the daemon's reapply path stays edge-triggered.
    const PhysicalScreen tracked = trackedScreenByName(screenName);
    if (tracked.isValid()) {
        calculateAvailableGeometry(tracked);
    }
}

void ScreenManager::propagateIdentifierDrift(const QHash<QString, QString>& oldIds)
{
    // Compare each still-tracked screen's prior identifier (captured before
    // the topology change, keyed by connector name) against its current one.
    // Any diff is a disambiguation flip — the persisted VS config keyed under
    // the old ID is now orphaned. Re-key the manager's own cache in place,
    // then emit screenIdentifierChanged so the host's IConfigStore can do
    // the same to its backing store. Both directions (bare→suffixed on add,
    // suffixed→bare on remove) flow through this single code path.
    for (const auto& screen : std::as_const(m_trackedScreens)) {
        const auto oldIt = oldIds.constFind(screen.name);
        if (oldIt == oldIds.constEnd()) {
            // Newly-added connector — no prior identifier to drift from.
            continue;
        }
        const QString oldId = oldIt.value();
        const QString newId = screen.identifier;
        if (oldId == newId || oldId.isEmpty() || newId.isEmpty()) {
            continue;
        }

        // Migrate the manager's cache: move any config at oldId → newId.
        // If nothing was cached at oldId, the signal still fires so an
        // external store that holds the only copy can migrate itself.
        auto cached = m_virtualConfigs.find(oldId);
        if (cached != m_virtualConfigs.end()) {
            VirtualScreenConfig cfg = cached.value();
            cfg.physicalScreenId = newId;
            for (auto& def : cfg.screens) {
                def.physicalScreenId = newId;
                def.id = PhosphorIdentity::VirtualScreenId::make(newId, def.index);
            }
            m_virtualConfigs.erase(cached);
            m_virtualConfigs.insert(newId, cfg);
            m_effectiveScreenIdsDirty = true;
            invalidateVirtualGeometryCache(oldId);
            invalidateVirtualGeometryCache(newId);
        }

        qCInfo(lcPhosphorScreens) << "Screen identifier drift:" << oldId << "→" << newId;
        Q_EMIT screenIdentifierChanged(oldId, newId);
    }
}

void ScreenManager::onProviderScreenAdded(const PhysicalScreen& screen)
{
    // Mutates the GUI-thread-only caches via syncTrackedScreens / sensor
    // creation — assert the IScreenProvider emitted on the GUI thread.
    PS_SCREEN_MANAGER_ASSERT_GUI_THREAD();
    if (!screen.isValid()) {
        return;
    }
    // Snapshot prior identifiers (keyed by connector name) BEFORE resyncing,
    // so a topology-driven disambiguation flip on the *existing* screens is
    // detectable. Scenario: a second same-model monitor joining promotes the
    // first monitor's ID from bare "Manuf:Model:Serial" to
    // "Manuf:Model:Serial/CONNECTOR". The provider recomputes identifiers on
    // add; propagateIdentifierDrift then migrates any persisted VS config
    // keyed on the old bare form so it does not silently orphan.
    QHash<QString, QString> oldIds;
    oldIds.reserve(m_trackedScreens.size());
    for (const auto& s : std::as_const(m_trackedScreens)) {
        oldIds.insert(s.name, s.identifier);
    }

    syncTrackedScreens();
    m_effectiveScreenIdsDirty = true;
    propagateIdentifierDrift(oldIds);

    if (m_cfg.useGeometrySensors) {
        const PhysicalScreen tracked = trackedScreenByName(screen.name);
        if (tracked.isValid()) {
            createGeometrySensor(tracked);
        }
    }
    Q_EMIT screenAdded(screen);
}

void ScreenManager::onProviderScreenRemoved(const PhysicalScreen& screen)
{
    // Mutates the GUI-thread-only caches (virtual-geometry cache, sensor map)
    // — assert the IScreenProvider emitted on the GUI thread.
    PS_SCREEN_MANAGER_ASSERT_GUI_THREAD();
    if (!screen.isValid()) {
        return;
    }
    // The removed screen's identifier is still valid on the signal payload.
    const QString physId = screen.identifier;
    if (!physId.isEmpty()) {
        invalidateVirtualGeometryCache(physId);
    } else {
        invalidateVirtualGeometryCache();
    }
    destroyGeometrySensor(screen.name);

    // Snapshot identifiers of the still-tracked screens (the removed one
    // included — it falls out below) before the resync. Removal can collapse
    // a disambiguated ID back to bare form on a surviving screen that
    // previously had a same-model sibling; propagateIdentifierDrift migrates
    // persisted configs in that direction too.
    QHash<QString, QString> oldIds;
    oldIds.reserve(m_trackedScreens.size());
    for (const auto& s : std::as_const(m_trackedScreens)) {
        oldIds.insert(s.name, s.identifier);
    }

    syncTrackedScreens();
    m_effectiveScreenIdsDirty = true;
    // Do NOT clear m_virtualConfigs[physId] — the host's IConfigStore is the
    // authoritative source. Stale entries for unconnected screens are
    // filtered by effectiveScreenIds().
    propagateIdentifierDrift(oldIds);

    Q_EMIT screenRemoved(screen);
}

void ScreenManager::onProviderScreenGeometryChanged(const PhysicalScreen& screen)
{
    // Mutates the GUI-thread-only caches (virtual-geometry + available-geometry)
    // — assert the IScreenProvider emitted on the GUI thread.
    PS_SCREEN_MANAGER_ASSERT_GUI_THREAD();
    if (!screen.isValid()) {
        return;
    }
    const QString physId = screen.identifier;
    if (!physId.isEmpty()) {
        invalidateVirtualGeometryCache(physId);
    } else {
        invalidateVirtualGeometryCache();
    }
    m_effectiveScreenIdsDirty = true;
    syncTrackedScreens();
    // Recompute available geometry against the new screen rect. The
    // available-geometry cache is screen-origin-relative (availGeom is
    // built from screenGeom.x()/y() in calculateAvailableGeometry), so a
    // moved or resized output must refresh it. Without this, an output
    // re-added at a transient (0,0) origin — DPMS wake, hotplug — keeps
    // its stale (0,0)-based available rect even after the output settles
    // to the real position, and every layout anchored to that screen
    // renders shifted to the desktop origin. Mirrors the recompute that
    // onSensorGeometryChanged and onPanelOffsetsChanged already perform.
    //
    // Recompute against the freshly-synced tracked snapshot rather than the
    // signal payload, matching onProviderScreenAdded's createGeometrySensor
    // call — the manager operates on its own tracked set, not raw payloads.
    calculateAvailableGeometry(trackedScreenByName(screen.name));
    Q_EMIT screenGeometryChanged(screen);
}

} // namespace PhosphorScreens
