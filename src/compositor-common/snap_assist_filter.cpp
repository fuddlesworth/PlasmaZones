// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snap_assist_filter.h"

#include <QHash>

namespace PlasmaZones {
namespace SnapAssistFilter {

SnapAssistCandidateList buildCandidates(ICompositorBridge* bridge, const QString& excludeWindowId,
                                        const QString& screenId, const QSet<QString>& snappedWindowIds)
{
    SnapAssistCandidateList candidates;
    if (!bridge) {
        return candidates;
    }
    const auto allWindows = bridge->stackingOrder();

    // Pre-compute per-appId window counts for O(1) lookups in the main loop
    QHash<QString, int> appIdCounts;
    for (WindowHandle wh : allWindows) {
        if (wh && bridge->shouldHandleWindow(wh)) {
            QString otherId = bridge->windowId(wh);
            QString appId = WindowIdUtils::extractAppId(otherId);
            ++appIdCounts[appId];
        }
    }

    for (WindowHandle wh : allWindows) {
        if (!wh || !bridge->shouldHandleWindow(wh)) {
            continue;
        }

        WindowInfo info = bridge->windowInfo(wh);
        if (info.isMinimized || !info.isOnCurrentDesktop || !info.isOnCurrentActivity) {
            continue;
        }
        if (info.windowId == excludeWindowId) {
            continue;
        }
        if (snappedWindowIds.contains(info.windowId)) {
            continue;
        }

        // If this app is snapped (by appId), skip unless there are multiple windows of this app
        bool snappedByAppId = false;
        for (const QString& snappedId : snappedWindowIds) {
            if (WindowIdUtils::extractAppId(snappedId) == info.appId) {
                snappedByAppId = true;
                break;
            }
        }
        if (snappedByAppId) {
            if (appIdCounts.value(info.appId, 0) <= 1) {
                continue;
            }
        }

        // Screen filter
        if (!screenId.isEmpty() && info.screenId != screenId) {
            continue;
        }

        // Prefer windowClass (WM_CLASS) for icon name; fall back to appId for XWayland
        // apps where windowClass may be empty.
        QString iconName = WindowIdUtils::deriveShortName(info.windowClass.isEmpty() ? info.appId : info.windowClass);
        if (iconName.isEmpty()) {
            iconName = QStringLiteral("application-x-executable");
        }

        SnapAssistCandidate c;
        c.windowId = info.windowId;
        // compositorHandle left empty — compositor-specific code fills it (e.g. KWin's internalId)
        c.icon = iconName;
        c.caption = info.caption;
        candidates.append(c);
    }
    return candidates;
}

} // namespace SnapAssistFilter
} // namespace PlasmaZones
