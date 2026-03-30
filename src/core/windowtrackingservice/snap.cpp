// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Auto-snap logic and snap tracking helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../interfaces.h"
#include "../layout.h"
#include "../zone.h"
#include "../layoutmanager.h"
#include "../virtualdesktopmanager.h"
#include "../virtualscreen.h"
#include "../utils.h"
#include "../screenmanager.h"
#include "../logging.h"
#include <QScreen>
#include <QSet>
#include <QUuid>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-Snap Logic
// ═══════════════════════════════════════════════════════════════════════════════

SnapResult WindowTrackingService::calculateSnapToAppRule(const QString& windowId, const QString& windowScreenName,
                                                         bool isSticky) const
{
    // Check if window was floating - floating windows should NOT be auto-snapped
    if (isWindowFloating(windowId)) {
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll) {
            return SnapResult::noSnap();
        }
    }

    if (!m_layoutManager) {
        return SnapResult::noSnap();
    }

    QString windowClass = Utils::extractWindowClass(windowId);
    if (windowClass.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Helper: given a match and a resolved screen name, build the SnapResult
    auto buildResult = [&](const AppRuleMatch& match, const QString& resolvedScreen) -> SnapResult {
        // Determine which screen to resolve the zone on
        QString effectiveScreen = match.targetScreen.isEmpty() ? resolvedScreen : match.targetScreen;

        // Validate that the target screen exists (may be connector name or screen ID)
        QScreen* screen = Utils::findScreenByIdOrName(effectiveScreen);
        if (!screen) {
            qCInfo(lcCore) << "App rule: screen" << effectiveScreen << "not found for" << windowClass
                           << (match.targetScreen.isEmpty() ? "(current screen)" : "(target screen)") << ", skipping";
            return SnapResult::noSnap();
        }

        // Get the layout for the effective screen to find the zone
        Layout* targetLayout = m_layoutManager->resolveLayoutForScreen(effectiveScreen);
        if (!targetLayout) {
            return SnapResult::noSnap();
        }

        Zone* zone = targetLayout->zoneByNumber(match.zoneNumber);
        if (!zone) {
            return SnapResult::noSnap();
        }

        QString zoneId = zone->id().toString();
        QRect geo = zoneGeometry(zoneId, effectiveScreen);
        if (!geo.isValid()) {
            return SnapResult::noSnap();
        }

        qCInfo(lcCore) << "App rule matched:" << windowClass << "-> zone" << match.zoneNumber << "on screen"
                       << effectiveScreen << "(" << zoneId << ")";

        SnapResult result;
        result.shouldSnap = true;
        result.geometry = geo;
        result.zoneId = zoneId;
        result.zoneIds = QStringList{zoneId};
        result.screenId = effectiveScreen;
        return result;
    };

    // Phase 1: Check the current screen's layout first (preserves existing behavior)
    Layout* currentLayout = m_layoutManager->resolveLayoutForScreen(windowScreenName);
    if (currentLayout) {
        AppRuleMatch match = currentLayout->matchAppRule(windowClass);
        if (match.matched()) {
            SnapResult result = buildResult(match, windowScreenName);
            if (result.isValid()) {
                return result;
            }
        } else {
            qCDebug(lcCore) << "calculateSnapToAppRule:" << windowClass << "no match in layout" << currentLayout->name()
                            << "(" << currentLayout->appRules().size() << "rules)";
        }
    } else {
        qCDebug(lcCore) << "calculateSnapToAppRule: no layout for screen" << windowScreenName;
    }

    // Phase 2: Scan other screens' layouts for cross-screen rules
    // Only accept matches that have targetScreen set (rules without targetScreen
    // are local to their layout's screen and shouldn't fire from other screens)
    QSet<QUuid> checkedLayouts;
    if (currentLayout) {
        checkedLayouts.insert(currentLayout->id());
    }

    // Use effective screen IDs (includes virtual screens) when available,
    // falling back to physical screens when ScreenManager is not initialized
    auto* smgr = ScreenManager::instance();
    const QStringList effectiveIds = smgr ? smgr->effectiveScreenIds() : QStringList();
    if (!effectiveIds.isEmpty()) {
        for (const QString& screenId : effectiveIds) {
            if (Utils::screensMatch(screenId, windowScreenName)) {
                continue;
            }

            Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
            if (!layout || checkedLayouts.contains(layout->id())) {
                continue;
            }
            checkedLayouts.insert(layout->id());

            AppRuleMatch match = layout->matchAppRule(windowClass);
            if (match.matched() && !match.targetScreen.isEmpty()) {
                SnapResult result = buildResult(match, screenId);
                if (result.isValid()) {
                    return result;
                }
            }
        }
    } else {
        for (QScreen* screen : Utils::allScreens()) {
            QString screenId = Utils::screenIdentifier(screen);
            if (Utils::screensMatch(screenId, windowScreenName)) {
                continue;
            }

            Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
            if (!layout || checkedLayouts.contains(layout->id())) {
                continue;
            }
            checkedLayouts.insert(layout->id());

            AppRuleMatch match = layout->matchAppRule(windowClass);
            if (match.matched() && !match.targetScreen.isEmpty()) {
                SnapResult result = buildResult(match, screenId);
                if (result.isValid()) {
                    return result;
                }
            }
        }
    }

    return SnapResult::noSnap();
}

SnapResult WindowTrackingService::calculateSnapToLastZone(const QString& windowId, const QString& windowScreenId,
                                                          bool isSticky) const
{
    // Check if feature is enabled
    if (!m_settings || !m_settings->moveNewWindowsToLastZone()) {
        return SnapResult::noSnap();
    }

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    if (isWindowFloating(windowId)) {
        qCDebug(lcCore) << "snapToLastZone:" << windowId << "was floating, skipping";
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll || handling == StickyWindowHandling::RestoreOnly) {
            return SnapResult::noSnap();
        }
    }

    // Need a last used zone
    if (m_lastUsedZoneId.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Check if window class was ever user-snapped
    QString windowClass = Utils::extractWindowClass(windowId);
    if (!m_userSnappedClasses.contains(windowClass)) {
        return SnapResult::noSnap();
    }

    // Don't cross-screen snap
    if (!windowScreenId.isEmpty() && !m_lastUsedScreenId.isEmpty()
        && !Utils::screensMatch(windowScreenId, m_lastUsedScreenId)) {
        return SnapResult::noSnap();
    }

    // Check virtual desktop match (unless sticky or desktop 0 = all)
    if (!isSticky && m_virtualDesktopManager && m_lastUsedDesktop > 0) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (currentDesktop != m_lastUsedDesktop) {
            return SnapResult::noSnap();
        }
    }

    // Calculate geometry
    QRect geo = zoneGeometry(m_lastUsedZoneId, m_lastUsedScreenId);
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = m_lastUsedZoneId;
    result.zoneIds = QStringList{m_lastUsedZoneId};
    result.screenId = m_lastUsedScreenId;
    return result;
}

SnapResult WindowTrackingService::calculateSnapToEmptyZone(const QString& windowId, const QString& windowScreenId,
                                                           bool isSticky) const
{
    // Do NOT skip floating windows here: this is called when the user explicitly dropped
    // a window on a monitor (dragStopped, no zone snap). If that monitor has auto-assign,
    // filling the first empty zone is intended. Floating list is for restore/last-zone
    // auto-snap; we clear floating state when we assign in snapToEmptyZone.

    // Check sticky window handling (auto-assign is an auto-snap, not a restore)
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll || handling == StickyWindowHandling::RestoreOnly) {
            qCDebug(lcCore) << "snapToEmptyZone: window" << Utils::extractAppId(windowId) << "sticky handling"
                            << static_cast<int>(handling);
            return SnapResult::noSnap();
        }
    }

    // Check layout has autoAssign enabled
    Layout* layout = m_layoutManager->resolveLayoutForScreen(windowScreenId);
    if (!layout) {
        qCDebug(lcCore) << "snapToEmptyZone: no layout for screen" << windowScreenId;
        return SnapResult::noSnap();
    }
    if (!layout->autoAssign()) {
        qCDebug(lcCore) << "snapToEmptyZone: layout" << layout->name() << "autoAssign=false";
        return SnapResult::noSnap();
    }

    // Reuse findEmptyZoneInLayout() with already-resolved layout to avoid double resolution
    QString emptyZoneId = findEmptyZoneInLayout(layout, windowScreenId);
    if (emptyZoneId.isEmpty()) {
        qCDebug(lcCore) << "snapToEmptyZone: no empty zone on" << windowScreenId;
        return SnapResult::noSnap();
    }

    QRect geo = zoneGeometry(emptyZoneId, windowScreenId);
    if (!geo.isValid()) {
        qCDebug(lcCore) << "snapToEmptyZone: invalid geometry for zone" << emptyZoneId;
        return SnapResult::noSnap();
    }

    return {true, geo, emptyZoneId, {emptyZoneId}, windowScreenId};
}

SnapResult WindowTrackingService::calculateRestoreFromSession(const QString& windowId, const QString& screenId,
                                                              bool isSticky) const
{
    QString appId = Utils::extractAppId(windowId);

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    if (isWindowFloating(windowId)) {
        qCDebug(lcCore) << "sessionRestore:" << windowId << "was floating, skipping";
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll) {
            return SnapResult::noSnap();
        }
    }

    // Check consumption queue
    auto queueIt = m_pendingRestoreQueues.constFind(appId);
    if (queueIt == m_pendingRestoreQueues.constEnd() || queueIt->isEmpty()) {
        return SnapResult::noSnap();
    }

    // Guard against wrong-instance restore for multi-instance apps.
    // After daemon-only restart (KWin still running), m_windowZoneAssignments is
    // loaded with full windowIds (appId|uuid). If another instance of this app
    // already has an exact full-ID match AND the effect has confirmed that sibling
    // is live, that instance owns the pending entry — this window should not
    // consume it. Only confirmed-live siblings block; stale entries from KWin
    // restarts (where UUIDs changed and no window will ever match) are ignored.
    if (appId != windowId) { // windowId contains UUID (full format)
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            if (it.key() != windowId && Utils::extractAppId(it.key()) == appId
                && m_effectReportedWindows.contains(it.key())) {
                qCDebug(lcCore) << "sessionRestore:" << windowId << "skipped — live sibling" << it.key()
                                << "has exact assignment";
                return SnapResult::noSnap();
            }
        }
    }

    // Take the first entry (FIFO — mirrors KWin's takeSessionInfo pattern)
    const PendingRestore& entry = queueIt->first();

    QStringList zoneIds = entry.zoneIds;
    if (zoneIds.isEmpty()) {
        return SnapResult::noSnap();
    }
    QString zoneId = zoneIds.first(); // Primary zone for validation
    QString savedScreen = entry.screenId.isEmpty() ? screenId : entry.screenId;

    // E7: Validate virtual screen still exists — configuration may have changed since save.
    // Fall back to physical screen ID if the virtual screen was removed.
    if (VirtualScreenId::isVirtual(savedScreen)) {
        auto* smgr = ScreenManager::instance();
        if (smgr) {
            const QStringList effectiveIds = smgr->effectiveScreenIds();
            if (!effectiveIds.contains(savedScreen)) {
                QString physId = VirtualScreenId::extractPhysicalId(savedScreen);
                qCInfo(lcCore) << "sessionRestore:" << appId << "saved virtual screen" << savedScreen
                               << "no longer exists, falling back to physical screen" << physId;
                savedScreen = physId;
            }
        }
    }

    // BUG FIX: Verify layout context matches before restoring
    // Without this check, windows would restore even if the current layout is different
    // from the layout that was active when the window was saved

    // Check if the current layout matches the saved layout
    // Use layoutForScreen() for proper multi-screen support - each screen can have
    // a different layout assigned, so we compare against the layout for the saved screen/desktop
    QString savedLayoutId = entry.layoutId;
    if (!savedLayoutId.isEmpty() && m_layoutManager) {
        int savedDesktop = entry.virtualDesktop;

        // Get the layout for the saved screen/desktop context (not just activeLayout)
        Layout* currentLayout =
            m_layoutManager->layoutForScreen(savedScreen, savedDesktop, m_layoutManager->currentActivity());
        if (!currentLayout) {
            // Fallback to active layout if no screen-specific assignment
            currentLayout = m_layoutManager->activeLayout();
        }

        if (!currentLayout) {
            // No layout available at all - cannot validate, skip restore to be safe
            qCDebug(lcCore) << "sessionRestore:" << appId << "no current layout, skipping";
            return SnapResult::noSnap();
        }

        // Use QUuid comparison to avoid string format issues (with/without braces)
        QUuid savedUuid = QUuid::fromString(savedLayoutId);
        if (!savedUuid.isNull() && currentLayout->id() != savedUuid) {
            qCInfo(lcCore) << "sessionRestore:" << appId << "saved layout" << savedLayoutId
                           << "but current layout for screen" << savedScreen << "desktop" << savedDesktop << "is"
                           << currentLayout->id().toString() << ", skipping";
            return SnapResult::noSnap();
        }
    }

    // Check virtual desktop match (unless sticky or desktop 0 = all)
    // This mirrors the check in calculateSnapToLastZone() for consistency
    int savedDesktop = entry.virtualDesktop;
    if (!isSticky && m_virtualDesktopManager && savedDesktop > 0) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (currentDesktop != savedDesktop) {
            qCDebug(lcCore) << "sessionRestore:" << appId << "saved on desktop" << savedDesktop << "but current is"
                            << currentDesktop << ", skipping";
            return SnapResult::noSnap();
        }
    }

    // Calculate geometry (use combined geometry for multi-zone)
    QRect geo = resolveZoneGeometry(zoneIds, savedScreen);

    // Zone-number fallback: zone UUIDs may have changed after layout edit.
    // Re-resolve layout for this screen and look up by zone number instead.
    if (!geo.isValid() && !savedLayoutId.isEmpty()) {
        QList<int> savedNumbers = entry.zoneNumbers;
        if (!savedNumbers.isEmpty()) {
            Layout* fallbackLayout = m_layoutManager ? m_layoutManager->resolveLayoutForScreen(savedScreen) : nullptr;
            if (fallbackLayout) {
                QStringList fallbackIds;
                for (int num : savedNumbers) {
                    Zone* z = fallbackLayout->zoneByNumber(num);
                    if (z)
                        fallbackIds.append(z->id().toString());
                }
                if (!fallbackIds.isEmpty()) {
                    geo = resolveZoneGeometry(fallbackIds, savedScreen);
                    if (geo.isValid()) {
                        zoneId = fallbackIds.first();
                        zoneIds = fallbackIds;
                        if (fallbackIds.size() < savedNumbers.size()) {
                            qCWarning(lcCore) << "zone-number fallback:" << appId << "partial match, requested"
                                              << savedNumbers.size() << "zones, matched" << fallbackIds.size();
                        }
                        qCInfo(lcCore) << "Zone-number fallback for" << appId << "numbers:" << savedNumbers << "->"
                                       << fallbackIds;
                    }
                }
            }
        }
    }

    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = zoneId;
    result.zoneIds = zoneIds;
    result.screenId = savedScreen;
    return result;
}

void WindowTrackingService::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (wasUserInitiated) {
        QString windowClass = Utils::extractWindowClass(windowId);
        if (!windowClass.isEmpty()) {
            m_userSnappedClasses.insert(windowClass);
            scheduleSaveState();
        }
    }
}

void WindowTrackingService::updateLastUsedZone(const QString& zoneId, const QString& screenId,
                                               const QString& windowClass, int virtualDesktop)
{
    m_lastUsedZoneId = zoneId;
    m_lastUsedScreenId = screenId;
    m_lastUsedZoneClass = windowClass;
    m_lastUsedDesktop = virtualDesktop;
    scheduleSaveState();
}

bool WindowTrackingService::clearStalePendingAssignment(const QString& windowId)
{
    // When a user explicitly snaps a window, clear any stale pending assignment
    // from a previous session. This prevents the window from restoring to the
    // wrong zone if it's closed and reopened.
    QString appId = Utils::extractAppId(windowId);
    bool hadPending = m_pendingRestoreQueues.remove(appId) > 0;
    if (hadPending) {
        qCDebug(lcCore) << "Cleared stale pending assignments for" << appId;
        scheduleSaveState();
    }
    return hadPending;
}

void WindowTrackingService::markWindowReported(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_effectReportedWindows.insert(windowId);
    }
}

void WindowTrackingService::markAsAutoSnapped(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_autoSnappedWindows.insert(windowId);
    }
}

bool WindowTrackingService::isAutoSnapped(const QString& windowId) const
{
    return m_autoSnappedWindows.contains(windowId);
}

bool WindowTrackingService::clearAutoSnapped(const QString& windowId)
{
    return m_autoSnappedWindows.remove(windowId);
}

void WindowTrackingService::consumePendingAssignment(const QString& windowId)
{
    QString appId = Utils::extractAppId(windowId);
    auto it = m_pendingRestoreQueues.find(appId);
    if (it != m_pendingRestoreQueues.end() && !it->isEmpty()) {
        it->removeFirst();
        if (it->isEmpty()) {
            m_pendingRestoreQueues.erase(it);
        }
        qCDebug(lcCore) << "Consumed pending assignment for" << appId
                        << "remaining:" << m_pendingRestoreQueues.value(appId).size();
        scheduleSaveState();
    }
}

} // namespace PlasmaZones
