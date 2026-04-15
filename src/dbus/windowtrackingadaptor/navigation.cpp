// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../../config/settings.h"
#include "../../core/inavigationactions.h"
#include "../../core/logging.h"
#include "../../core/layoutmanager.h"
#include "../../core/screenmanager.h"
#include "../../core/screenmoderouter.h"
#include "../../core/utils.h"
#include "../../core/virtualscreen.h"
#include "../../core/windowtrackingservice.h"
#include "../../snap/SnapEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation forwarders.
//
// Every method in this range used to contain its own target-resolve +
// bookkeeping + signal-emission logic. The logic has moved into SnapEngine
// (src/snap/snapengine/navigation_actions.cpp) — this adaptor now acts as
// the thin D-Bus facade it was always supposed to be, forwarding
// shortcut-driven navigation requests to the engine that owns the
// behaviour.
//
// Methods that still contain real logic here are the ones tied to
// WindowTrackingAdaptor-specific state (toggleWindowFloat reads the
// frame-geometry shadow), JSON deserialization (handleBatchedResnap),
// or pure signal passthroughs (requestMoveSpecificWindowToZone,
// reportNavigationFeedback).
// ═══════════════════════════════════════════════════════════════════════════════

// D-Bus entry points: external callers (typically the KWin effect) invoke
// these without an explicit NavigationContext. The engine methods take a
// NavigationContext and fall back to WTA's last-active shadow when both
// fields are empty — matching the historical behaviour where these slots
// read the shadow directly.

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->moveFocusedInDirection(direction, NavigationContext{});
    }
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->focusInDirection(direction, NavigationContext{});
    }
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->pushFocusedToEmptyZone(NavigationContext{QString(), screenId});
    }
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    if (m_snapEngine) {
        m_snapEngine->restoreFocusedWindow(NavigationContext{});
    }
}

// Note: WindowTrackingAdaptor::toggleWindowFloat() (zero-arg shortcut
// handler) used to live here. It was the daemon-local "Meta-F" float-
// toggle path — read active window from the shadow, capture pre-tile
// from the frame-geometry shadow when already floating, route to the
// correct engine via toggleFloatForWindow.
//
// That logic now lives in SnapEngine::toggleFocusedFloat (in
// snapengine/navigation_actions.cpp). The shortcut handler in
// daemon/navigation.cpp::handleFloat dispatches through
// ScreenModeRouter::navigatorFor(screenId) → INavigationActions →
// toggleFocusedFloat(), which routes to either the autotile engine's
// toggleFocusedWindowFloat() or SnapEngine's toggleFocusedFloat() via
// the adapters. No circular bounce through WTA any more.

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->swapFocusedInDirection(direction, NavigationContext{});
    }
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->moveFocusedToPosition(zoneNumber, NavigationContext{QString(), screenId});
    }
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->rotateWindowsInLayout(clockwise, screenId);
    }
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    if (m_snapEngine) {
        m_snapEngine->cycleFocus(forward, NavigationContext{});
    }
}

void WindowTrackingAdaptor::resnapToNewLayout()
{
    if (m_snapEngine) {
        m_snapEngine->resnapToNewLayout();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Batch helper used by in-adaptor methods that operate on pre-built
// ZoneAssignmentEntry vectors (resnapForVirtualScreenReconfigure,
// handleBatchedResnap). Thin wrapper over
// WindowTrackingService::applyBatchAssignments — the loop and per-entry
// screen-resolution logic live there, and SnapEngine's navigation_actions.cpp
// uses the same WTS method, eliminating the historical two-copy duplication.
// ═══════════════════════════════════════════════════════════════════════════════
static bool processBatchEntries(WindowTrackingAdaptor* adaptor, const QVector<ZoneAssignmentEntry>& entries,
                                const QString& action)
{
    WindowTrackingService* service = adaptor ? adaptor->service() : nullptr;
    if (!service) {
        return false;
    }

    WindowGeometryList geometries = service->applyBatchAssignments(
        entries, WindowTrackingService::SnapIntent::UserInitiated, [adaptor]() -> QString {
            const QString cursor = adaptor->lastCursorScreenName();
            return cursor.isEmpty() ? adaptor->lastActiveScreenName() : cursor;
        });

    if (geometries.isEmpty()) {
        return false;
    }
    Q_EMIT adaptor->applyGeometriesBatch(geometries, action);
    return true;
}

// Build the effective list of snap-mode screens this resnap should target.
// Empty filter → all currently-known effective screens. Virtual screen
// filter → exactly that VS. Physical screen filter → every VS child of
// that physical monitor. The router is consulted once per screen to drop
// autotile-mode screens (engines own placement there, not snap).
QStringList WindowTrackingAdaptor::resolveSnapModeScreensForResnap(const QString& screenFilter) const
{
    QStringList candidates;
    if (!screenFilter.isEmpty()) {
        if (VirtualScreenId::isVirtual(screenFilter)) {
            candidates.append(screenFilter);
        } else if (auto* mgr = ScreenManager::instance()) {
            candidates = mgr->virtualScreenIdsFor(screenFilter);
        } else {
            candidates.append(screenFilter);
        }
    } else if (auto* mgr = ScreenManager::instance()) {
        candidates = mgr->effectiveScreenIds();
    }

    if (!m_screenModeRouter) {
        // No router wired yet — return the candidates unfiltered. Early
        // startup only; operational paths always have a router.
        return candidates;
    }
    return m_screenModeRouter->partitionByMode(candidates).snap;
}

void WindowTrackingAdaptor::resnapCurrentAssignments(const QString& screenFilter)
{
    if (m_snapEngine) {
        m_snapEngine->resnapCurrentAssignments(screenFilter);
    }
}

void WindowTrackingAdaptor::resnapForVirtualScreenReconfigure(const QString& physicalScreenId)
{
    qCDebug(lcDbusWindow) << "resnapForVirtualScreenReconfigure: physId=" << physicalScreenId;

    const QStringList snapScreens = resolveSnapModeScreensForResnap(physicalScreenId);
    QVector<ZoneAssignmentEntry> entries;
    for (const QString& sid : snapScreens) {
        entries.append(m_service->calculateResnapFromCurrentAssignments(sid));
    }

    if (entries.isEmpty()) {
        return;
    }

    // Tagged "vs_reconfigure" so the kwin-effect does NOT fire snap-assist
    // continuation — no user-initiated snap happened here, windows are just
    // following their VS's new geometry after a swap/rotate/split edit.
    processBatchEntries(this, entries, QStringLiteral("vs_reconfigure"));
}

void WindowTrackingAdaptor::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "resnapFromAutotileOrder: count=" << autotileWindowOrder.size() << "screen=" << screenId;

    if (m_snapEngine) {
        // Use SnapEngine's fallback logic (autotile order → current assignments)
        QVector<ZoneAssignmentEntry> entries =
            m_snapEngine->calculateResnapEntriesFromAutotileOrder(autotileWindowOrder, screenId);
        if (!entries.isEmpty()) {
            processBatchEntries(this, entries, QStringLiteral("resnap"));
        }
    }
}

void WindowTrackingAdaptor::snapAllWindows(const QString& screenId)
{
    qCDebug(lcDbusWindow) << "snapAllWindows: screen=" << screenId;
    if (m_snapEngine) {
        m_snapEngine->snapAllWindows(screenId);
    }
}

void WindowTrackingAdaptor::requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId,
                                                            const QRect& geometry)
{
    qCDebug(lcDbusWindow) << "requestMoveSpecificWindowToZone: window=" << windowId << "zone=" << zoneId;
    Q_EMIT moveSpecificWindowToZoneRequested(windowId, zoneId, geometry.x(), geometry.y(), geometry.width(),
                                             geometry.height());
}

SnapAllResultList WindowTrackingAdaptor::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows: count=" << windowIds.size() << "screen=" << screenId;
    if (m_snapEngine) {
        return m_snapEngine->calculateSnapAllWindows(windowIds, screenId);
    }
    return {};
}

void WindowTrackingAdaptor::handleBatchedResnap(const QString& resnapData)
{
    qCDebug(lcDbusWindow) << "handleBatchedResnap: processing batched resnap from SnapEngine";

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(resnapData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcDbusWindow) << "handleBatchedResnap: invalid JSON:" << parseError.errorString();
        return;
    }

    // Deserialize ZoneAssignmentEntry array
    QVector<ZoneAssignmentEntry> entries;
    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        ZoneAssignmentEntry entry;
        entry.windowId = obj.value(QLatin1String("windowId")).toString();
        entry.targetZoneId = obj.value(QLatin1String("targetZoneId")).toString();
        entry.sourceZoneId = obj.value(QLatin1String("sourceZoneId")).toString();
        // Deserialize multi-zone IDs (for zone-spanning windows)
        const QJsonArray zoneIdsArr = obj.value(QLatin1String("targetZoneIds")).toArray();
        if (!zoneIdsArr.isEmpty()) {
            for (const QJsonValue& v : zoneIdsArr)
                entry.targetZoneIds.append(v.toString());
        }
        entry.targetGeometry =
            QRect(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                  obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());
        if (!entry.windowId.isEmpty() && !entry.targetZoneId.isEmpty()) {
            entries.append(entry);
        }
    }

    processBatchEntries(this, entries, QStringLiteral("resnap"));
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                                     const QString& sourceZoneId, const QString& targetZoneId,
                                                     const QString& screenId)
{
    qCDebug(lcDbusWindow) << "Navigation feedback: success=" << success << "action=" << action << "reason=" << reason
                          << "sourceZone=" << sourceZoneId << "targetZone=" << targetZoneId << "screen=" << screenId;
    Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
}

} // namespace PlasmaZones
