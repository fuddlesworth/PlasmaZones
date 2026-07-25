// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — scrolling-engine screen-set management
//
// The scrolling counterpart of updateEngineScreens' engine push: order
// seeding across mode transitions, per-context rule-param resolution, and
// the setActiveScreens handoff. Driven from updateEngineScreens (one
// cascade walk derives both engines' sets) so the two sets always flip in
// the same recompute.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"
#include "helpers.h"

#include "core/platform/logging.h"

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

void Daemon::updateScrollingScreens(const QSet<QString>& scrollingScreens)
{
    if (!m_scrollEngine || !m_layoutManager) {
        return;
    }
    const QString activity = currentActivity();

    // Capture window order for screens LEAVING scrolling before their strip
    // states are destroyed, and seed order for screens ENTERING scrolling
    // from a captured order — the same deterministic mode-transition
    // contract autotile keeps via m_lastEngineOrders (the map is shared:
    // an autotile→scrolling flip replays the tiled order as columns and the
    // reverse replays columns as tiles).
    const QSet<QString> currentScrollScreens = m_scrollEngine->activeScreens();
    for (const QString& screenId : currentScrollScreens - scrollingScreens) {
        const int desktop = currentDesktopForScreen(screenId);
        const QStringList order = m_scrollEngine->managedWindowOrder(screenId);
        if (!order.isEmpty()) {
            m_lastEngineOrders[TilingStateKey{screenId, desktop, activity}] = order;
        }
    }
    for (const QString& screenId : scrollingScreens - currentScrollScreens) {
        const int desktop = currentDesktopForScreen(screenId);
        const auto it = m_lastEngineOrders.constFind(TilingStateKey{screenId, desktop, activity});
        if (it != m_lastEngineOrders.constEnd()) {
            m_scrollEngine->setInitialWindowOrder(screenId, it.value());
        }
    }

    // Per-context rule overrides (slot ?? config): resolve the scrolling
    // params for each active context and layer them onto the engine BEFORE
    // setActiveScreens retiles entering screens.
    for (const QString& screenId : scrollingScreens) {
        const int desktop = currentDesktopForScreen(screenId);
        const PhosphorZones::ContextScrollingParams params =
            m_layoutManager->resolveContextScrollingParams(screenId, desktop, activity);
        QVariantMap overrides;
        if (params.centerFocusedColumn) {
            overrides.insert(QStringLiteral("CenterFocusedColumn"), *params.centerFocusedColumn);
        }
        if (params.defaultColumnWidth) {
            overrides.insert(QStringLiteral("DefaultColumnWidth"), *params.defaultColumnWidth);
        }
        if (params.defaultColumnDisplay) {
            overrides.insert(QStringLiteral("DefaultColumnDisplay"), *params.defaultColumnDisplay);
        }
        if (overrides.isEmpty()) {
            m_scrollEngine->clearPerScreenConfig(screenId);
        } else {
            m_scrollEngine->applyPerScreenConfig(screenId, overrides);
        }
    }

    m_scrollEngine->setActiveScreens(scrollingScreens);
}

} // namespace PlasmaZones
