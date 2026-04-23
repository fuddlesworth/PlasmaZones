// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Auto-snap calculation methods (moved from WindowTrackingService).
// Part of SnapEngine — split into its own translation unit for SRP.

#include "../SnapEngine.h"
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "core/interfaces.h"
#include "core/logging.h"
#include "core/utils.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-Snap Logic
// ═══════════════════════════════════════════════════════════════════════════════

SnapResult SnapEngine::calculateSnapToAppRule(const QString& windowId, const QString& windowScreenName,
                                              bool isSticky) const
{
    // Check if window was floating - floating windows should NOT be auto-snapped
    if (m_windowTracker->isWindowFloating(windowId)) {
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

    QString windowClass = m_windowTracker->currentAppIdFor(windowId);
    if (windowClass.isEmpty()) {
        return SnapResult::noSnap();
    }

    auto* screenManager = m_windowTracker->screenManager();

    // Helper: given a match and a resolved screen name, build the SnapResult
    auto buildResult = [&](const PhosphorZones::AppRuleMatch& match, const QString& resolvedScreen) -> SnapResult {
        // Determine which screen to resolve the zone on
        QString effectiveScreen = match.targetScreen.isEmpty() ? resolvedScreen : match.targetScreen;

        // Validate that the target screen exists. Use Phosphor::Screens::ScreenManager::resolvePhysicalScreen
        // which properly handles virtual screen IDs (resolving to backing QScreen*).
        QScreen* screen = (screenManager ? screenManager->physicalQScreenFor(effectiveScreen)
                                         : Utils::findScreenAtPosition(QPoint(0, 0)));
        if (!screen) {
            qCInfo(lcCore) << "App rule: screen" << effectiveScreen << "not found for" << windowClass
                           << (match.targetScreen.isEmpty() ? "(current screen)" : "(target screen)") << ", skipping";
            return SnapResult::noSnap();
        }

        // Get the layout for the effective screen to find the zone
        PhosphorZones::Layout* targetLayout = m_layoutManager->resolveLayoutForScreen(effectiveScreen);
        if (!targetLayout) {
            return SnapResult::noSnap();
        }

        PhosphorZones::Zone* zone = targetLayout->zoneByNumber(match.zoneNumber);
        if (!zone) {
            return SnapResult::noSnap();
        }

        QString zoneId = zone->id().toString();
        QRect geo = m_windowTracker->zoneGeometry(zoneId, effectiveScreen);
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
    PhosphorZones::Layout* currentLayout = m_layoutManager->resolveLayoutForScreen(windowScreenName);
    if (currentLayout) {
        PhosphorZones::AppRuleMatch match = currentLayout->matchAppRule(windowClass);
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

    // Build a unified list of screen IDs from either effective screens (includes
    // virtual screens) or physical screens as fallback, then use a single loop.
    QStringList screenIds;
    if (screenManager) {
        screenIds = screenManager->effectiveScreenIds();
    } else {
        const auto screens = Utils::allScreens();
        for (auto* s : screens) {
            screenIds.append(Phosphor::Screens::ScreenIdentity::identifierFor(s));
        }
    }

    for (const QString& screenId : std::as_const(screenIds)) {
        if (Phosphor::Screens::ScreenIdentity::screensMatch(screenId, windowScreenName)) {
            continue;
        }

        PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!layout || checkedLayouts.contains(layout->id())) {
            continue;
        }
        checkedLayouts.insert(layout->id());

        PhosphorZones::AppRuleMatch match = layout->matchAppRule(windowClass);
        if (match.matched() && !match.targetScreen.isEmpty()) {
            SnapResult result = buildResult(match, screenId);
            if (result.isValid()) {
                return result;
            }
        }
    }

    return SnapResult::noSnap();
}

SnapResult SnapEngine::calculateSnapToLastZone(const QString& windowId, const QString& windowScreenId,
                                               bool isSticky) const
{
    // Check if feature is enabled
    if (!m_settings || !m_settings->moveNewWindowsToLastZone()) {
        return SnapResult::noSnap();
    }

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    if (m_windowTracker->isWindowFloating(windowId)) {
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
    const QString lastUsedZoneId = m_snapState->lastUsedZoneId();
    if (lastUsedZoneId.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Check if window class was ever user-snapped
    QString windowClass = m_windowTracker->currentAppIdFor(windowId);
    if (!m_snapState->userSnappedClasses().contains(windowClass)) {
        return SnapResult::noSnap();
    }

    // Validate virtual screen still exists — configuration may have changed since last snap.
    // Fall back to physical screen ID if the virtual screen was removed.
    const QString lastUsedScreenId = m_snapState->lastUsedScreenId();
    QString effectiveScreenId = m_windowTracker->resolveEffectiveScreenId(lastUsedScreenId);

    // Don't cross-screen snap
    if (!windowScreenId.isEmpty() && !effectiveScreenId.isEmpty()
        && !Phosphor::Screens::ScreenIdentity::screensMatch(windowScreenId, effectiveScreenId)) {
        return SnapResult::noSnap();
    }

    // Check virtual desktop match (unless sticky or desktop 0 = all)
    const int lastUsedDesktop = m_snapState->lastUsedDesktop();
    if (!isSticky && m_virtualDesktopManager && lastUsedDesktop > 0) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (currentDesktop != lastUsedDesktop) {
            return SnapResult::noSnap();
        }
    }

    // Calculate geometry
    QRect geo = m_windowTracker->zoneGeometry(lastUsedZoneId, effectiveScreenId);
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = lastUsedZoneId;
    result.zoneIds = QStringList{lastUsedZoneId};
    result.screenId = effectiveScreenId;
    return result;
}

SnapResult SnapEngine::calculateSnapToEmptyZone(const QString& windowId, const QString& windowScreenId,
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
            qCDebug(lcCore) << "snapToEmptyZone: window" << m_windowTracker->currentAppIdFor(windowId)
                            << "sticky handling" << static_cast<int>(handling);
            return SnapResult::noSnap();
        }
    }

    // Check layout has autoAssign enabled
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(windowScreenId);
    if (!layout) {
        qCDebug(lcCore) << "snapToEmptyZone: no layout for screen" << windowScreenId;
        return SnapResult::noSnap();
    }
    if (!layout->autoAssign()) {
        qCDebug(lcCore) << "snapToEmptyZone: layout" << layout->name() << "autoAssign=false";
        return SnapResult::noSnap();
    }

    // Reuse findEmptyZoneInLayout() with already-resolved layout to avoid double resolution.
    // Filter occupancy by the current virtual desktop so windows parked on other desktops
    // don't prevent auto-snap placement on the current desktop.
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QString emptyZoneId = m_windowTracker->findEmptyZoneInLayout(layout, windowScreenId, desktopFilter);
    if (emptyZoneId.isEmpty()) {
        qCDebug(lcCore) << "snapToEmptyZone: no empty zone on" << windowScreenId;
        return SnapResult::noSnap();
    }

    QRect geo = m_windowTracker->zoneGeometry(emptyZoneId, windowScreenId);
    if (!geo.isValid()) {
        qCDebug(lcCore) << "snapToEmptyZone: invalid geometry for zone" << emptyZoneId;
        return SnapResult::noSnap();
    }

    return {true, geo, emptyZoneId, {emptyZoneId}, windowScreenId};
}

SnapResult SnapEngine::calculateRestoreFromSession(const QString& windowId, const QString& screenId,
                                                   bool isSticky) const
{
    QString appId = m_windowTracker->currentAppIdFor(windowId);

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    if (m_windowTracker->isWindowFloating(windowId)) {
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
    const auto& pendingQueues = m_windowTracker->pendingRestoreQueues();
    auto queueIt = pendingQueues.constFind(appId);
    if (queueIt == pendingQueues.constEnd() || queueIt->isEmpty()) {
        return SnapResult::noSnap();
    }

    // Guard against wrong-instance restore for multi-instance apps.
    // After daemon-only restart (KWin still running), m_windowZoneAssignments is
    // loaded with full windowIds (appId|uuid). If another instance of this app
    // already has an exact full-ID match AND the effect has confirmed that sibling
    // is live, that instance owns the pending entry — this window should not
    // consume it. Only confirmed-live siblings block; stale entries from KWin
    // restarts (where UUIDs changed and no window will ever match) are ignored.
    const auto& zoneAssignments = m_windowTracker->zoneAssignments();
    const auto& effectReported = effectReportedWindows();
    if (appId != windowId) { // windowId contains UUID (full format)
        for (auto it = zoneAssignments.constBegin(); it != zoneAssignments.constEnd(); ++it) {
            if (it.key() != windowId && m_windowTracker->currentAppIdFor(it.key()) == appId
                && effectReported.contains(it.key())) {
                qCDebug(lcCore) << "sessionRestore:" << windowId << "skipped — live sibling" << it.key()
                                << "has exact assignment";
                return SnapResult::noSnap();
            }
        }
    }

    // Take the first entry (FIFO — mirrors KWin's takeSessionInfo pattern)
    const WindowTrackingService::PendingRestore& entry = queueIt->first();

    QStringList zoneIds = entry.zoneIds;
    if (zoneIds.isEmpty()) {
        return SnapResult::noSnap();
    }
    QString zoneId = zoneIds.first(); // Primary zone for validation
    // Use stored screen from the pending restore entry, falling back to the caller's
    // screenId. If both are empty, resolveEffectiveScreenId returns it unchanged and
    // downstream resolveZoneGeometry falls back to the primary screen via
    // Phosphor::Screens::ScreenManager::resolvePhysicalScreen.
    QString savedScreen = entry.screenId.isEmpty() ? screenId : entry.screenId;

    // E7: Validate virtual screen still exists — configuration may have changed since save.
    // Fall back to physical screen ID if the virtual screen was removed.
    savedScreen = m_windowTracker->resolveEffectiveScreenId(savedScreen);

    // If the saved zone's screen is currently in autotile mode, this path is the
    // wrong owner — autotile on that screen will claim the window via its own
    // windowOpened flow. Return noSnap WITHOUT consuming the pending entry:
    // autotile might push the window back into our queue on next close, and
    // another snap-mode screen's context could legitimately claim this entry.
    if (m_layoutManager) {
        int modeDesktop = entry.virtualDesktop;
        if (modeDesktop <= 0 && m_virtualDesktopManager) {
            modeDesktop = m_virtualDesktopManager->currentDesktop();
        }
        if (m_layoutManager->modeForScreen(savedScreen, modeDesktop, m_layoutManager->currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            qCDebug(lcCore) << "sessionRestore:" << appId << "saved screen" << savedScreen
                            << "is now in autotile mode — deferring to autotile engine";
            return SnapResult::noSnap();
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
        PhosphorZones::Layout* currentLayout =
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
    QRect geo = m_windowTracker->resolveZoneGeometry(zoneIds, savedScreen);

    // PhosphorZones::Zone-number fallback: zone UUIDs may have changed after layout edit.
    // Re-resolve layout for this screen and look up by zone number instead.
    if (!geo.isValid() && !savedLayoutId.isEmpty()) {
        QList<int> savedNumbers = entry.zoneNumbers;
        if (!savedNumbers.isEmpty()) {
            PhosphorZones::Layout* fallbackLayout =
                m_layoutManager ? m_layoutManager->resolveLayoutForScreen(savedScreen) : nullptr;
            if (fallbackLayout) {
                QStringList fallbackIds;
                for (int num : savedNumbers) {
                    PhosphorZones::Zone* z = fallbackLayout->zoneByNumber(num);
                    if (z)
                        fallbackIds.append(z->id().toString());
                }
                if (!fallbackIds.isEmpty()) {
                    geo = m_windowTracker->resolveZoneGeometry(fallbackIds, savedScreen);
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

} // namespace PlasmaZones
