// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../snapnavigationtargets.h"
#include "internal.h"
#include "../../snap/SnapEngine.h"
#include "../../core/logging.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include "../../core/virtualscreen.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>

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

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->moveFocusedInDirection(direction);
    }
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->focusInDirection(direction);
    }
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->pushFocusedToEmptyZone(screenId);
    }
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    if (m_snapEngine) {
        m_snapEngine->restoreFocusedWindow();
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
        m_snapEngine->swapFocusedInDirection(direction);
    }
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->moveFocusedToPosition(zoneNumber, screenId);
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
        m_snapEngine->cycleFocus(forward);
    }
}

void WindowTrackingAdaptor::resnapToNewLayout()
{
    if (m_snapEngine) {
        m_snapEngine->resnapToNewLayout();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper used by the remaining in-adaptor methods (resnapForVirtualScreenReconfigure
// and handleBatchedResnap) that operate on pre-built ZoneAssignmentEntry vectors
// and still own their own D-Bus signal emission. SnapEngine's navigation_actions.cpp
// has an equivalent helper in an anonymous namespace for the engine-owned methods.
// A future refactor can unify them behind a SnapEngine::applyResnapEntries public
// method; for now the duplication is bounded to these two call sites.
// ═══════════════════════════════════════════════════════════════════════════════
static bool processBatchEntries(WindowTrackingAdaptor* adaptor, const QVector<ZoneAssignmentEntry>& entries,
                                const QString& action)
{
    if (entries.isEmpty()) {
        return false;
    }
    for (const auto& entry : entries) {
        if (entry.targetZoneId == QLatin1String("__restore__")) {
            adaptor->windowUnsnapped(entry.windowId);
            adaptor->clearPreTileGeometry(entry.windowId);
        } else {
            QString screenId = entry.targetScreenId;
            QPoint center = entry.targetGeometry.center();
            auto* mgr = ScreenManager::instance();
            if (screenId.isEmpty() && mgr) {
                screenId = mgr->effectiveScreenAt(center);
            }
            if (screenId.isEmpty()) {
                for (QScreen* screen : QGuiApplication::screens()) {
                    if (screen->geometry().contains(center)) {
                        screenId = Utils::screenIdentifier(screen);
                        break;
                    }
                }
            }
            if (screenId.isEmpty()) {
                screenId = adaptor->lastCursorScreenName();
                if (screenId.isEmpty()) {
                    screenId = adaptor->lastActiveScreenName();
                }
            }
            if (entry.targetZoneIds.size() > 1) {
                adaptor->windowSnappedMultiZone(entry.windowId, entry.targetZoneIds, screenId);
            } else {
                adaptor->windowSnapped(entry.windowId, entry.targetZoneId, screenId);
            }
        }
    }
    WindowGeometryList geometries;
    geometries.reserve(entries.size());
    for (const ZoneAssignmentEntry& entry : entries) {
        geometries.append(WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry));
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

bool WindowTrackingAdaptor::validateDirection(const QString& direction, const QString& action)
{
    if (direction.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << action << "- empty direction";
        Q_EMIT navigationFeedback(false, action, QStringLiteral("invalid_direction"), QString(), QString(), QString());
        return false;
    }
    return true;
}

bool WindowTrackingAdaptor::isWindowExcluded(const QString& windowId, const QString& action)
{
    if (!m_settings) {
        return false;
    }

    // Current class, not first-seen — matches rules against the window's
    // live appId so mid-session mutations don't bypass exclusions on fresh
    // operations. (Per feedback_class_change_exclusion.md, existing snapped
    // state is NOT re-evaluated — only new decision points, like this one.)
    // m_service is constructed in the adaptor ctor and is never null here.
    const QString appId = m_service->currentAppIdFor(windowId);
    for (const QString& excluded : m_settings->excludedApplications()) {
        if (Utils::appIdMatches(appId, excluded)) {
            qCInfo(lcDbusWindow) << action << ":" << windowId << "excluded by app rule:" << excluded;
            Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(),
                                      m_lastActiveScreenId);
            return true;
        }
    }
    for (const QString& excluded : m_settings->excludedWindowClasses()) {
        if (Utils::appIdMatches(appId, excluded)) {
            qCInfo(lcDbusWindow) << action << ":" << windowId << "excluded by class rule:" << excluded;
            Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(),
                                      m_lastActiveScreenId);
            return true;
        }
    }
    return false;
}

} // namespace PlasmaZones
