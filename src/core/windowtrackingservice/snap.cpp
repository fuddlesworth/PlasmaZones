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

    QString windowClass = currentAppIdFor(windowId);
    if (windowClass.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Helper: given a match and a resolved screen name, build the SnapResult
    auto buildResult = [&](const AppRuleMatch& match, const QString& resolvedScreen) -> SnapResult {
        // Determine which screen to resolve the zone on
        QString effectiveScreen = match.targetScreen.isEmpty() ? resolvedScreen : match.targetScreen;

        // Validate that the target screen exists. Use ScreenManager::resolvePhysicalScreen
        // which properly handles virtual screen IDs (resolving to backing QScreen*).
        QScreen* screen = ScreenManager::resolvePhysicalScreen(effectiveScreen);
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

    // Build a unified list of screen IDs from either effective screens (includes
    // virtual screens) or physical screens as fallback, then use a single loop.
    QStringList screenIds;
    auto* smgr = ScreenManager::instance();
    if (smgr) {
        screenIds = smgr->effectiveScreenIds();
    } else {
        const auto screens = Utils::allScreens();
        for (auto* s : screens) {
            screenIds.append(Utils::screenIdentifier(s));
        }
    }

    for (const QString& screenId : std::as_const(screenIds)) {
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
    QString windowClass = currentAppIdFor(windowId);
    if (!m_userSnappedClasses.contains(windowClass)) {
        return SnapResult::noSnap();
    }

    // Validate virtual screen still exists — configuration may have changed since last snap.
    // Fall back to physical screen ID if the virtual screen was removed.
    QString effectiveScreenId = resolveEffectiveScreenId(m_lastUsedScreenId);

    // Don't cross-screen snap
    if (!windowScreenId.isEmpty() && !effectiveScreenId.isEmpty()
        && !Utils::screensMatch(windowScreenId, effectiveScreenId)) {
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
    QRect geo = zoneGeometry(m_lastUsedZoneId, effectiveScreenId);
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = m_lastUsedZoneId;
    result.zoneIds = QStringList{m_lastUsedZoneId};
    result.screenId = effectiveScreenId;
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
            qCDebug(lcCore) << "snapToEmptyZone: window" << currentAppIdFor(windowId) << "sticky handling"
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

    // Reuse findEmptyZoneInLayout() with already-resolved layout to avoid double resolution.
    // Pass current desktop filter so zones occupied on other desktops aren't counted.
    int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QString emptyZoneId = findEmptyZoneInLayout(layout, windowScreenId, desktopFilter);
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
    QString appId = currentAppIdFor(windowId);

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
            if (it.key() != windowId && currentAppIdFor(it.key()) == appId
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
    // Use stored screen from the pending restore entry, falling back to the caller's
    // screenId. If both are empty, resolveEffectiveScreenId returns it unchanged and
    // downstream resolveZoneGeometry falls back to the primary screen via
    // ScreenManager::resolvePhysicalScreen.
    QString savedScreen = entry.screenId.isEmpty() ? screenId : entry.screenId;

    // E7: Validate virtual screen still exists — configuration may have changed since save.
    // Fall back to physical screen ID if the virtual screen was removed.
    savedScreen = resolveEffectiveScreenId(savedScreen);

    // If the saved zone's screen is currently in autotile mode, this path is the
    // wrong owner — autotile on that screen will claim the window via its own
    // windowOpened flow. Return noSnap WITHOUT consuming the pending entry:
    // autotile might push the window back into our queue on next close, and
    // another snap-mode screen's context could legitimately claim this entry.
    //
    // This is the architecturally correct version of the PR #320 / commit
    // 05519844 guard: it checks the SAVED zone's screen (where the zone
    // actually lives), not the caller's screen (where KWin happened to drop
    // the window). The old version broke the cross-screen case where a window
    // belonged on a snap-mode VS but KWin opened it on an autotile VS.
    if (m_layoutManager) {
        int modeDesktop = entry.virtualDesktop;
        if (modeDesktop <= 0 && m_virtualDesktopManager) {
            modeDesktop = m_virtualDesktopManager->currentDesktop();
        }
        if (m_layoutManager->modeForScreen(savedScreen, modeDesktop, m_layoutManager->currentActivity())
            != AssignmentEntry::Mode::Snapping) {
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
        QString windowClass = currentAppIdFor(windowId);
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

// ═══════════════════════════════════════════════════════════════════════════════
// Snap commit orchestration — moved out of WindowTrackingAdaptor.
//
// These methods used to live in WindowTrackingAdaptor::windowSnapped /
// windowSnappedMultiZone / windowUnsnapped, where they orchestrated a
// sequence of WTS primitive calls plus a D-Bus signal emit. That made
// WTA a partial snap engine rather than a thin facade. The orchestration
// is pure state-management work that belongs on WTS; WTA retains its
// D-Bus slot entry points but forwards to these methods and relays the
// WTS signals to its own D-Bus signals at connection wiring time.
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::commitSnap(const QString& windowId, const QString& zoneId, const QString& screenId)
{
    if (windowId.isEmpty() || zoneId.isEmpty()) {
        qCWarning(lcCore) << "commitSnap: empty windowId or zoneId";
        return;
    }

    // 1. Clear pre-existing floating state so the snap is authoritative.
    if (clearFloatingForSnap(windowId)) {
        Q_EMIT windowFloatingClearedForSnap(windowId, screenId);
    }

    // 2. Auto-snap flag: clearAutoSnapped returns true if the flag was set.
    //    When it was, skip "consume pending" and "update last-used zone" — an
    //    auto-snap shouldn't disturb either.
    const bool wasAutoSnapped = clearAutoSnapped(windowId);

    // 3. Consume one pending-restore entry (FIFO) for user-initiated snaps so
    //    a stale session entry can't drag the window back on next close/reopen.
    if (!wasAutoSnapped) {
        consumePendingAssignment(windowId);
    }

    // 4. Actually assign the window to the zone on the resolved screen.
    const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    assignWindowToZone(windowId, zoneId, screenId, currentDesktop);

    // 5. Update last-used-zone tracking unless this is a zone-selector sentinel
    //    or an auto-snap. The sentinel bail-out matches the historical WTA
    //    behaviour — zoneselector-* IDs represent ephemeral selection state,
    //    not real zones the user wants to remember.
    if (!zoneId.startsWith(QStringLiteral("zoneselector-")) && !wasAutoSnapped) {
        const QString windowClass = currentAppIdFor(windowId);
        updateLastUsedZone(zoneId, screenId, windowClass, currentDesktop);
    }

    qCInfo(lcCore) << "commitSnap:" << windowId << "zone=" << zoneId << "screen=" << screenId;

    // 6. Notify subscribers (WTA re-emits as the D-Bus windowStateChanged).
    Q_EMIT windowSnapStateChanged(
        windowId, WindowStateEntry{windowId, zoneId, screenId, false, QStringLiteral("snapped"), QStringList{}, false});
}

void WindowTrackingService::commitMultiZoneSnap(const QString& windowId, const QStringList& zoneIds,
                                                const QString& screenId)
{
    if (windowId.isEmpty() || zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        qCWarning(lcCore) << "commitMultiZoneSnap: empty windowId or zoneIds";
        return;
    }

    if (clearFloatingForSnap(windowId)) {
        Q_EMIT windowFloatingClearedForSnap(windowId, screenId);
    }

    const bool wasAutoSnapped = clearAutoSnapped(windowId);
    if (!wasAutoSnapped) {
        consumePendingAssignment(windowId);
    }

    const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    assignWindowToZones(windowId, zoneIds, screenId, currentDesktop);

    // Last-used tracking keys off the primary (first) zone.
    const QString& primaryZoneId = zoneIds.first();
    if (!primaryZoneId.startsWith(QStringLiteral("zoneselector-")) && !wasAutoSnapped) {
        const QString windowClass = currentAppIdFor(windowId);
        updateLastUsedZone(primaryZoneId, screenId, windowClass, currentDesktop);
    }

    qCInfo(lcCore) << "commitMultiZoneSnap:" << windowId << "zones=" << zoneIds << "screen=" << screenId;

    Q_EMIT windowSnapStateChanged(
        windowId,
        WindowStateEntry{windowId, primaryZoneId, screenId, false, QStringLiteral("snapped"), zoneIds, false});
}

void WindowTrackingService::uncommitSnap(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    // Only act if the window is actually snapped — callers may blind-call
    // this on any window, and uncommitting a never-snapped window is both
    // pointless and noisy.
    const QString previousZoneId = zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        qCDebug(lcCore) << "uncommitSnap: window not in any zone:" << windowId;
        return;
    }

    // Clear any queued pending-restore entry so an explicit unsnap stays
    // sticky across the window's next close/reopen cycle.
    consumePendingAssignment(windowId);

    unassignWindow(windowId);

    qCInfo(lcCore) << "uncommitSnap:" << windowId << "from zone" << previousZoneId;

    Q_EMIT windowSnapStateChanged(
        windowId,
        WindowStateEntry{windowId, QString(), QString(), false, QStringLiteral("unsnapped"), QStringList{}, false});
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

bool WindowTrackingService::consumePendingAssignment(const QString& windowId)
{
    // Pop the oldest pending-restore entry for this window's live appId.
    // Single authoritative implementation — see header for why earlier
    // consumePendingAssignment / clearStalePendingAssignment twins were
    // merged. Callers that don't care about the result ignore the bool.
    const QString appId = currentAppIdFor(windowId);
    auto it = m_pendingRestoreQueues.find(appId);
    if (it == m_pendingRestoreQueues.end() || it->isEmpty()) {
        return false;
    }
    it->removeFirst();
    if (it->isEmpty()) {
        m_pendingRestoreQueues.erase(it);
    }
    qCDebug(lcCore) << "Consumed pending assignment for" << appId
                    << "remaining:" << m_pendingRestoreQueues.value(appId).size();
    scheduleSaveState();
    return true;
}

} // namespace PlasmaZones
