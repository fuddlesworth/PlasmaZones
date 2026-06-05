// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Auto-snap calculation methods (moved from WindowTrackingService).
// Part of SnapEngine — split into its own translation unit for SRP.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include "snapenginelogging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>

namespace PhosphorSnapEngine {

using PhosphorEngine::PendingRestore;
using PhosphorEngine::SnapResult;
using PhosphorEngine::StickyWindowHandling;

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
    if (auto* s = snapSettings(); isSticky && s) {
        auto handling = s->stickyWindowHandling();
        if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll) {
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

        if (!screenManager) {
            qCWarning(PhosphorSnapEngine::lcSnapEngine)
                << "App rule: no screen manager, falling back to primary screen for" << windowClass;
        }
        const bool screenFound = screenManager ? screenManager->physicalScreenFor(effectiveScreen).isValid()
                                               : (QGuiApplication::primaryScreen() != nullptr);
        if (!screenFound) {
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "App rule: screen" << effectiveScreen << "not found for" << windowClass
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

        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "App rule matched:" << windowClass << "-> zone" << match.zoneNumber
                                                 << "on screen" << effectiveScreen << "(" << zoneId << ")";

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
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "calculateSnapToAppRule:" << windowClass << "no match in layout" << currentLayout->name() << "("
                << currentLayout->appRules().size() << "rules)";
        }
    } else {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "calculateSnapToAppRule: no layout for screen" << windowScreenName;
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
        const auto screens = QGuiApplication::screens();
        for (auto* s : screens) {
            screenIds.append(PhosphorScreens::ScreenIdentity::identifierFor(s));
        }
    }

    for (const QString& screenId : std::as_const(screenIds)) {
        if (PhosphorScreens::ScreenIdentity::screensMatch(screenId, windowScreenName)) {
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
    auto* s = snapSettings();
    if (!s || !s->moveNewWindowsToLastZone()) {
        return SnapResult::noSnap();
    }

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    if (m_windowTracker->isWindowFloating(windowId)) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToLastZone:" << windowId << "was floating, skipping";
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky) {
        auto handling = s->stickyWindowHandling();
        if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll
            || handling == PhosphorEngine::StickyWindowHandling::RestoreOnly) {
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
        && !PhosphorScreens::ScreenIdentity::screensMatch(windowScreenId, effectiveScreenId)) {
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
    if (auto* s = snapSettings(); isSticky && s) {
        auto handling = s->stickyWindowHandling();
        if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll
            || handling == PhosphorEngine::StickyWindowHandling::RestoreOnly) {
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "snapToEmptyZone: window" << m_windowTracker->currentAppIdFor(windowId) << "sticky handling"
                << static_cast<int>(handling);
            return SnapResult::noSnap();
        }
    }

    // Check the effective auto-assign gate. Autotile screens are filtered out
    // upstream (lifecycle.cpp short-circuits before reaching here), so by the
    // time we evaluate this gate we're always operating on a manual layout.
    // The global "all layouts" master toggle (#370) is a force-on override
    // that lights up auto-assign for any such layout regardless of its
    // per-layout flag; when the global toggle is off, the per-layout flag
    // remains the sole gate.
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(windowScreenId);
    if (!layout) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: no layout for screen" << windowScreenId;
        return SnapResult::noSnap();
    }
    auto* settings = snapSettings();
    const bool effectiveAuto = layout->autoAssign() || (settings && settings->autoAssignAllLayouts());
    if (!effectiveAuto) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: layout" << layout->name() << "autoAssign=false";
        return SnapResult::noSnap();
    }

    // Reuse findEmptyZoneInLayout() with already-resolved layout to avoid double resolution.
    // Filter occupancy by the current virtual desktop so windows parked on other desktops
    // don't prevent auto-snap placement on the current desktop.
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QString emptyZoneId = m_windowTracker->findEmptyZoneInLayout(layout, windowScreenId, desktopFilter);
    if (emptyZoneId.isEmpty()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: no empty zone on" << windowScreenId;
        return SnapResult::noSnap();
    }

    QRect geo = m_windowTracker->zoneGeometry(emptyZoneId, windowScreenId);
    if (!geo.isValid()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: invalid geometry for zone" << emptyZoneId;
        return SnapResult::noSnap();
    }

    return {true, geo, emptyZoneId, {emptyZoneId}, windowScreenId};
}

} // namespace PhosphorSnapEngine
