// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileservice.h"
#include "interfaces.h"
#include "layout.h"
#include "zone.h"
#include "layoutmanager.h"
#include "windowtrackingservice.h"
#include "geometryutils.h"
#include "screenmanager.h"
#include "utils.h"
#include "logging.h"
#include <QJsonObject>
#include <QScreen>
#include <algorithm>

namespace PlasmaZones {

AutoTileService::AutoTileService(LayoutManager* layoutManager,
                                 WindowTrackingService* windowTracking,
                                 ISettings* settings,
                                 QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracking(windowTracking)
    , m_settings(settings)
{
    // Debounce timer for window close/minimize events
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(50); // 50ms — fast enough to feel responsive
    connect(&m_debounceTimer, &QTimer::timeout, this, &AutoTileService::processPendingRegenerations);
}

AutoTileService::~AutoTileService() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// #108 — Window Lifecycle Hooks
// ═══════════════════════════════════════════════════════════════════════════════

AutoTileResult AutoTileService::handleWindowOpened(const QString& windowId, const QString& screenName)
{
    if (windowId.isEmpty() || screenName.isEmpty() || !m_layoutManager || !m_windowTracking || !m_settings) {
        return {};
    }

    if (!resolveDynamicLayout(screenName)) {
        return {};
    }

    // Check if window is floating — floating windows don't participate in auto-tile
    if (m_windowTracking->isWindowFloating(windowId)) {
        qCDebug(lcCore) << "AutoTile: window" << windowId << "is floating, skipping";
        return {};
    }

    // Add to tiled windows for this screen
    QStringList& tiledList = m_tiledWindows[screenName];
    if (!tiledList.contains(windowId)) {
        // Check newWindowAsMaster setting
        if (m_settings->newWindowAsMaster()) {
            tiledList.prepend(windowId);
            m_masterWindows[screenName] = windowId;
        } else {
            tiledList.append(windowId);
        }
        m_windowScreens[windowId] = screenName;
    }

    // Set master if none yet (EDGE-6: check value is non-empty, not just key existence)
    if (m_masterWindows.value(screenName).isEmpty() && !tiledList.isEmpty()) {
        m_masterWindows[screenName] = tiledList.first();
    }

    qCInfo(lcCore) << "AutoTile: window opened" << windowId << "on" << screenName
                   << "tiled count:" << tiledList.size();

    return regenerateForScreen(screenName);
}

void AutoTileService::handleWindowClosed(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    QString screenName = m_windowScreens.take(windowId);
    if (screenName.isEmpty()) {
        return; // Window wasn't tracked by auto-tile
    }

    // Remove from tiled windows
    QStringList& tiledList = m_tiledWindows[screenName];
    tiledList.removeAll(windowId);
    m_minimizedWindows.remove(windowId);

    // Update master if needed (EDGE-6: remove key entirely when list is empty)
    if (m_masterWindows.value(screenName) == windowId) {
        if (tiledList.isEmpty()) {
            m_masterWindows.remove(screenName);
        } else {
            m_masterWindows[screenName] = tiledList.first();
        }
    }

    qCInfo(lcCore) << "AutoTile: window closed" << windowId << "on" << screenName
                   << "tiled count:" << tiledList.size();

    // Debounce the regeneration
    scheduleRegeneration(screenName);
}

void AutoTileService::handleWindowMinimized(const QString& windowId, bool minimized)
{
    if (windowId.isEmpty()) {
        return;
    }

    QString screenName = m_windowScreens.value(windowId);
    if (screenName.isEmpty()) {
        return;
    }

    // Check setting: whether minimized windows count toward zone count
    if (!m_settings || m_settings->countMinimizedWindows()) {
        return; // Minimized windows still occupy zones, no regen needed
    }

    if (minimized) {
        m_minimizedWindows.insert(windowId);

        // If the minimized window is master, promote the next visible window
        // so orderedTiledWindows() returns a consistent master-first list
        if (m_masterWindows.value(screenName) == windowId) {
            const QStringList& tiledList = m_tiledWindows.value(screenName);
            QString newMaster;
            for (const QString& wId : tiledList) {
                if (wId != windowId && !m_minimizedWindows.contains(wId)) {
                    newMaster = wId;
                    break;
                }
            }
            if (!newMaster.isEmpty()) {
                m_masterWindows[screenName] = newMaster;
            }
            // If all windows are minimized, leave master as-is — it will be restored
        }
    } else {
        m_minimizedWindows.remove(windowId);
    }

    qCInfo(lcCore) << "AutoTile: window" << (minimized ? "minimized" : "restored")
                   << windowId << "on" << screenName;

    scheduleRegeneration(screenName);
}

void AutoTileService::handleLayoutChanged(const QString& screenName)
{
    if (screenName.isEmpty() || !m_layoutManager) {
        return;
    }

    Layout* layout = resolveDynamicLayout(screenName);
    if (!layout) {
        // Layout is no longer Dynamic — clean up all tracking for this screen
        QStringList windowsToRemove;
        for (auto it = m_windowScreens.constBegin(); it != m_windowScreens.constEnd(); ++it) {
            if (it.value() == screenName) {
                windowsToRemove.append(it.key());
            }
        }
        for (const QString& wId : windowsToRemove) {
            m_windowScreens.remove(wId);
            m_minimizedWindows.remove(wId);
        }
        m_tiledWindows.remove(screenName);
        m_masterWindows.remove(screenName);
        return;
    }

    qCInfo(lcCore) << "AutoTile: layout changed on" << screenName
                   << "algorithm:" << layout->algorithmId();

    regenerateAndEmit(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// #106 — Master Window
// ═══════════════════════════════════════════════════════════════════════════════

QString AutoTileService::masterWindowId(const QString& screenName) const
{
    return m_masterWindows.value(screenName);
}

void AutoTileService::promoteMasterWindow(const QString& windowId, const QString& screenName)
{
    if (windowId.isEmpty() || screenName.isEmpty()) {
        return;
    }

    QStringList& tiledList = m_tiledWindows[screenName];
    if (!tiledList.contains(windowId)) {
        qCDebug(lcCore) << "AutoTile: promote failed — window" << windowId << "not tiled on" << screenName;
        return;
    }

    QString currentMaster = m_masterWindows.value(screenName);
    if (windowId == currentMaster) {
        qCDebug(lcCore) << "AutoTile: window" << windowId << "is already master";
        return;
    }

    // Swap positions: promoted window goes to index 0, old master takes promoted's position
    int promotedIdx = tiledList.indexOf(windowId);
    int masterIdx = tiledList.indexOf(currentMaster);

    if (promotedIdx >= 0 && masterIdx >= 0) {
        tiledList.swapItemsAt(promotedIdx, masterIdx);
    } else if (promotedIdx >= 0) {
        tiledList.removeAt(promotedIdx);
        tiledList.prepend(windowId);
    }

    m_masterWindows[screenName] = windowId;

    qCInfo(lcCore) << "AutoTile: promoted" << windowId << "to master on" << screenName;

    regenerateAndEmit(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// #107 — Master Ratio Resize
// ═══════════════════════════════════════════════════════════════════════════════

void AutoTileService::adjustMasterRatio(const QString& screenName, qreal delta)
{
    Layout* layout = resolveDynamicLayout(screenName);
    if (!layout) {
        return;
    }

    qreal newRatio = qBound(0.1, layout->masterRatio() + delta, 0.9);
    if (qFuzzyCompare(newRatio, layout->masterRatio())) {
        return;
    }

    layout->setMasterRatio(newRatio);

    qCInfo(lcCore) << "AutoTile: master ratio adjusted to" << newRatio << "on" << screenName;

    regenerateAndEmit(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════════════════════

bool AutoTileService::isScreenDynamic(const QString& screenName) const
{
    return resolveDynamicLayout(screenName) != nullptr;
}

int AutoTileService::tiledWindowCount(const QString& screenName) const
{
    const QStringList& tiledList = m_tiledWindows.value(screenName);
    if (!m_settings || m_settings->countMinimizedWindows()) {
        return tiledList.size();
    }

    // Exclude minimized windows from count
    int count = 0;
    for (const QString& windowId : tiledList) {
        if (!m_minimizedWindows.contains(windowId)) {
            ++count;
        }
    }
    return count;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private: Helpers
// ═══════════════════════════════════════════════════════════════════════════════

Layout* AutoTileService::resolveDynamicLayout(const QString& screenName) const
{
    if (screenName.isEmpty() || !m_layoutManager) {
        return nullptr;
    }
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenName);
    return (layout && layout->category() == LayoutCategory::Dynamic) ? layout : nullptr;
}

void AutoTileService::regenerateAndEmit(const QString& screenName)
{
    AutoTileResult result = regenerateForScreen(screenName);
    if (result.handled) {
        Q_EMIT geometriesChanged(screenName, assignmentsToJson(result.assignments));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private: Core Regeneration
// ═══════════════════════════════════════════════════════════════════════════════

AutoTileResult AutoTileService::regenerateForScreen(const QString& screenName)
{
    Layout* layout = resolveDynamicLayout(screenName);
    if (!layout) {
        return {};
    }

    // Get ordered tiled windows (master first)
    QStringList orderedWindows = orderedTiledWindows(screenName);
    int windowCount = orderedWindows.size();

    if (windowCount == 0) {
        // No tiled windows — clear zones
        layout->regenerateZones(0);
        return {true, {}};
    }

    // Regenerate zones for the current window count
    layout->regenerateZones(windowCount);

    // Recalculate zone geometries for the screen
    QScreen* screen = Utils::findScreenByName(screenName);
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        qCWarning(lcCore) << "AutoTile: no screen found for" << screenName;
        return {};
    }
    layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(screen));

    // Get zones sorted by zone number
    QVector<Zone*> zones = layout->zones();
    std::sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
        return a->zoneNumber() < b->zoneNumber();
    });

    if (zones.size() != windowCount) {
        qCWarning(lcCore) << "AutoTile: zone count" << zones.size()
                          << "!= window count" << windowCount << "on" << screenName;
    }

    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);

    // Map ordered[i] -> zones[i]
    AutoTileResult result;
    result.handled = true;

    for (int i = 0; i < qMin(orderedWindows.size(), zones.size()); ++i) {
        const QString& windowId = orderedWindows[i];
        Zone* zone = zones[i];
        QString zoneId = zone->id().toString();

        QRectF geoF = GeometryUtils::getZoneGeometryWithGaps(
            zone, screen, zonePadding, outerGap, true);
        QRect geo = geoF.toRect();

        if (geo.isValid()) {
            WindowAssignment assignment;
            assignment.windowId = windowId;
            assignment.zoneId = zoneId;
            assignment.geometry = geo;
            result.assignments.append(assignment);

            // Update WindowTrackingService assignments
            if (m_windowTracking) {
                int virtualDesktop = 0; // auto-tile doesn't restrict to desktops
                m_windowTracking->assignWindowToZone(windowId, zoneId, screenName, virtualDesktop);
            }
        }
    }

    qCInfo(lcCore) << "AutoTile: regenerated" << zones.size() << "zones for"
                   << orderedWindows.size() << "windows on" << screenName;

    return result;
}

QStringList AutoTileService::orderedTiledWindows(const QString& screenName) const
{
    const QStringList& tiledList = m_tiledWindows.value(screenName);
    if (tiledList.isEmpty()) {
        return {};
    }

    bool countMinimized = !m_settings || m_settings->countMinimizedWindows();

    // Filter out minimized windows if not counting them
    QStringList ordered;
    for (const QString& windowId : tiledList) {
        if (!countMinimized && m_minimizedWindows.contains(windowId)) {
            continue;
        }
        ordered.append(windowId);
    }

    // Master window is always first in the tiled list (maintained by prepend/swap)
    // So the order is already: [master, window2, window3, ...]
    return ordered;
}

void AutoTileService::scheduleRegeneration(const QString& screenName)
{
    m_pendingScreens.insert(screenName);
    m_debounceTimer.start(); // Restart the 50ms timer
}

void AutoTileService::processPendingRegenerations()
{
    QSet<QString> screens = std::move(m_pendingScreens);
    m_pendingScreens.clear();

    for (const QString& screenName : screens) {
        regenerateAndEmit(screenName);
    }
}

QJsonArray AutoTileService::assignmentsToJson(const QVector<WindowAssignment>& assignments) const
{
    QJsonArray array;
    for (const WindowAssignment& a : assignments) {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = a.windowId;
        obj[QLatin1String("zoneId")] = a.zoneId;
        obj[QLatin1String("x")] = a.geometry.x();
        obj[QLatin1String("y")] = a.geometry.y();
        obj[QLatin1String("w")] = a.geometry.width();
        obj[QLatin1String("h")] = a.geometry.height();
        array.append(obj);
    }
    return array;
}

} // namespace PlasmaZones
