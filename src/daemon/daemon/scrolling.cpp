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

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

void Daemon::captureScrollingOrders(const QSet<QString>& scrollingScreens)
{
    // Capture window order for screens LEAVING scrolling before their strip
    // states are destroyed. Called from updateEngineScreens in the shared
    // capture phase (capture-all → seed-all → apply-all), BEFORE either
    // engine seeds, so a same-pass scrolling→autotile flip replays the
    // column order as tiles and the reverse replays tiles as columns.
    if (!m_scrollEngine) {
        return;
    }
    const QString activity = currentActivity();
    const QSet<QString> currentScrollScreens = m_scrollEngine->activeScreens();
    for (const QString& screenId : currentScrollScreens - scrollingScreens) {
        const int desktop = currentDesktopForScreen(screenId);
        const QStringList order = m_scrollEngine->managedWindowOrder(screenId);
        if (!order.isEmpty()) {
            m_lastEngineOrders[TilingStateKey{screenId, desktop, activity}] = order;
        }
    }
}

void Daemon::updateScrollingScreens(const QSet<QString>& scrollingScreens)
{
    if (!m_scrollEngine || !m_layoutManager) {
        return;
    }
    const QString activity = currentActivity();

    // Seed order for screens ENTERING scrolling from a captured order — the
    // deterministic mode-transition contract shared with autotile via
    // m_lastEngineOrders. Leaving-screen capture already ran in
    // captureScrollingOrders (the shared capture phase in
    // updateEngineScreens).
    const QSet<QString> currentScrollScreens = m_scrollEngine->activeScreens();
    for (const QString& screenId : scrollingScreens - currentScrollScreens) {
        // (Snap-float presave for screens entering scrolling from snapping
        // runs in updateEngineScreens' derive phase, BEFORE any engine set
        // is applied — by this point snap's capturePlacement would already
        // refuse the screen's new mode and capture nothing.)
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
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::centerFocusedColumn(),
                             *params.centerFocusedColumn);
        }
        if (params.defaultColumnWidth) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnWidth(),
                             *params.defaultColumnWidth);
        }
        if (params.defaultColumnDisplay) {
            overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnDisplay(),
                             *params.defaultColumnDisplay);
        }
        if (overrides.isEmpty()) {
            m_scrollEngine->clearPerScreenConfig(screenId);
        } else {
            m_scrollEngine->applyPerScreenConfig(screenId, overrides);
        }
    }
    m_scrollEngine->setActiveScreens(scrollingScreens);

    // Screens LEAVING scrolling drop their override entries too — otherwise a
    // stale map is replayed on re-entry before any rule change re-resolves it.
    // AFTER setActiveScreens: clearPerScreenConfig schedules a retile for the
    // screen it clears, and a departing screen is no longer in the engine's
    // live set by now, so the schedule is refused instead of queueing a no-op.
    for (const QString& screenId : currentScrollScreens - scrollingScreens) {
        m_scrollEngine->clearPerScreenConfig(screenId);
    }

    // setActiveScreens retiles only ADDED screens on a changed set (the
    // identical-set branch retiles everything itself). Force a retile for
    // every already-active screen so a rule save that changes GAP rules on a
    // screen whose overrides map did not move still applies live — gaps
    // resolve through the context-gap provider at retile time, never through
    // the overrides diff. Mirrors the load-bearing autotile loop in
    // updateEngineScreens; scheduleRetileForScreen coalesces, so the
    // identical-set overlap costs nothing.
    if (scrollingScreens != currentScrollScreens) {
        for (const QString& screenId : (scrollingScreens & currentScrollScreens)) {
            m_scrollEngine->scheduleRetileForScreen(screenId);
        }
    }
}

} // namespace PlasmaZones
