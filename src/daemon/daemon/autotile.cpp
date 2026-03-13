// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "../overlayservice.h"
#include "../modetracker.h"
#include "../unifiedlayoutcontroller.h"
#include "../../core/layoutmanager.h"
#include "../../core/screenmanager.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/windowtrackingservice.h"
#include "../config/settings.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../autotile/AutotileEngine.h"
#include "../../autotile/AutotileConfig.h"
#include "../../autotile/AlgorithmRegistry.h"
#include "../../autotile/TilingAlgorithm.h"
#include <QScreen>

namespace PlasmaZones {

namespace {
// Geometry/panel timing (ms) — keep in sync with daemon.cpp constants
// After processing geometry we re-query panels once so we pick up settled state (e.g. panel editor close).
constexpr int DELAYED_PANEL_REQUERY_MS = 400;
// Reapply requested on next event loop (0); daemon state is already updated when we start the timer.
constexpr int REAPPLY_DELAY_MS = 0;
} // anonymous namespace

void Daemon::updateAutotileScreens()
{
    if (!m_autotileEngine || !m_layoutManager || !m_screenManager) {
        return;
    }

    const int desktop = currentDesktop();
    const QString activity = currentActivity();

    QSet<QString> autotileScreens;
    QHash<QString, QString> screenAlgorithms;
    for (QScreen* screen : m_screenManager->screens()) {
        QString screenId = Utils::screenIdentifier(screen);
        QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (LayoutId::isAutotile(assignmentId)) {
            autotileScreens.insert(screen->name());
            QString algoId = LayoutId::extractAlgorithmId(assignmentId);
            if (!algoId.isEmpty()) {
                screenAlgorithms[screen->name()] = algoId;
            }
        }
    }

    // Apply per-screen overrides BEFORE setAutotileScreens so that newly added
    // screens are retiled with the correct per-screen algorithm (not the global
    // fallback).  applyPerScreenConfig lazily creates TilingStates via
    // stateForScreen(), which setAutotileScreens reuses for added screens.
    if (m_settings) {
        for (QScreen* screen : m_screenManager->screens()) {
            if (!autotileScreens.contains(screen->name()))
                continue;
            QString screenId = Utils::screenIdentifier(screen);
            QVariantMap overrides = m_settings->getPerScreenAutotileSettings(screenId);
            // Inject algorithm from layout assignment (authoritative source)
            if (screenAlgorithms.contains(screen->name())) {
                const QString screenAlgo = screenAlgorithms.value(screen->name());
                overrides[QStringLiteral("Algorithm")] = screenAlgo;

                // When the per-screen algorithm differs from the engine's
                // current global algorithm and there's no explicit MaxWindows
                // override, inject the per-screen algorithm's default — but
                // only if the global maxWindows is still at the global
                // algorithm's default.  If the user customized maxWindows,
                // respect their choice.
                //
                // Use the engine's runtime algorithm (m_autotileEngine->algorithm())
                // instead of m_settings->autotileAlgorithm(). During layout cycling,
                // the settings algorithm retains the initial KCM value while the
                // engine's algorithm changes with each cycle. Using the stale settings
                // value caused incorrect MaxWindows injection and unpredictable
                // per-screen overrides.
                //
                // Note: in applyEntry(), this runs BEFORE setAlgorithm() updates
                // the global ID, so globalAlgo is the OLD algorithm. This is safe:
                // effectiveMaxWindows() has identical fallback logic (step 2) that
                // dynamically derives the correct MaxWindows at retile time even
                // without a per-screen override. The override here is an optimization.
                const QString globalAlgo = m_autotileEngine->algorithm();
                if (screenAlgo != globalAlgo && !overrides.contains(QLatin1String("MaxWindows"))) {
                    auto* screenAlgoPtr = AlgorithmRegistry::instance()->algorithm(screenAlgo);
                    auto* globalAlgoPtr = AlgorithmRegistry::instance()->algorithm(globalAlgo);
                    if (screenAlgoPtr) {
                        if (!globalAlgoPtr) {
                            qCDebug(lcDaemon) << "updateAutotileScreens: global algorithm" << globalAlgo
                                              << "not found - injecting per-screen default MaxWindows";
                        }
                        // Use the engine's runtime maxWindows (not m_settings->
                        // autotileMaxWindows()) — during cycling, settings may
                        // be stale if updateAutotileScreens runs before
                        // setAlgorithm syncs settings via QSignalBlocker.
                        const int runtimeMaxWindows = m_autotileEngine->config()->maxWindows;
                        if (!globalAlgoPtr || runtimeMaxWindows == globalAlgoPtr->defaultMaxWindows()) {
                            overrides[QStringLiteral("MaxWindows")] = screenAlgoPtr->defaultMaxWindows();
                        }
                    } else {
                        qCWarning(lcDaemon) << "updateAutotileScreens: unknown per-screen algorithm" << screenAlgo
                                            << "for screen" << screen->name();
                    }
                }
            }
            // Compare against currently applied overrides to avoid redundant retiles
            QVariantMap current = m_autotileEngine->perScreenOverrides(screen->name());
            if (overrides != current) {
                if (!overrides.isEmpty()) {
                    m_autotileEngine->applyPerScreenConfig(screen->name(), overrides);
                } else {
                    m_autotileEngine->clearPerScreenConfig(screen->name());
                }
            }
        }
    }

    // setAutotileScreens creates TilingStates for newly added screens (reusing
    // any already created by applyPerScreenConfig above) and retiles them.
    // Because per-screen overrides are set first, retileAfterOperation inside
    // setAutotileScreens uses effectiveAlgorithm() with the correct per-screen algo.
    m_autotileEngine->setAutotileScreens(autotileScreens);

    // Retile for existing screens whose overrides changed is handled by the
    // deferred retile scheduled inside applyPerScreenConfig()/clearPerScreenConfig().
    // This coalesces with deferred retiles from setAlgorithm() (called by
    // applyEntry() after updateAutotileScreens), producing a SINGLE retile pass
    // with fully consistent state. An immediate retile() here would fire BEFORE
    // setAlgorithm() updates the global algorithm/splitRatio, causing a second
    // windowsTiled D-Bus signal whose stagger generation increment invalidates
    // the first signal's pending stagger timers — leaving windows at old
    // positions and producing the left-overlapping-right bug.

    // Propagate to overlay service so initializeOverlay() skips autotile screens
    if (m_overlayService) {
        m_overlayService->setExcludedScreens(autotileScreens);
    }

    qCDebug(lcDaemon) << "Updated autotile screens=" << autotileScreens;
}

/**
 * @brief Deactivate autotile: clear assignments, restore manual layout, resnap windows.
 *
 * @note Callers MUST call updateAutotileScreens() + updateLayoutFilter()
 *       afterward to derive per-screen state and update the layout model.
 */
void Daemon::handleAutotileDisabled()
{
    // Feature disabled: clear all autotile assignments.
    // Block signals to avoid N intermediate updateAutotileScreens()
    // from layoutAssigned — one final call below is sufficient.
    if (m_layoutManager) {
        QSignalBlocker blocker(m_layoutManager.get());
        m_layoutManager->clearAutotileAssignments();
    }
    // Restore last manual layout so windows aren't stuck in tiled positions.
    // Assign per-screen (not just globally) so layoutForScreen() finds explicit
    // assignments instead of falling through to the default layout.
    if (m_modeTracker && m_layoutManager && m_screenManager) {
        m_modeTracker->setCurrentMode(TilingMode::Manual);
        const QString lastLayoutId = m_modeTracker->lastManualLayoutId();
        Layout* layout =
            lastLayoutId.isEmpty() ? nullptr : m_layoutManager->layoutById(QUuid::fromString(lastLayoutId));
        // Fallback to active layout or first available layout
        if (!layout) {
            layout = m_layoutManager->activeLayout();
        }
        if (!layout && !m_layoutManager->layouts().isEmpty()) {
            layout = m_layoutManager->layouts().first();
        }
        if (layout) {
            const int desktop = currentDesktop();
            const QString activity = currentActivity();
            // Block signals: per-screen assignments are batch-written here
            // and the caller will call updateAutotileScreens() afterward.
            {
                QSignalBlocker blocker(m_layoutManager.get());
                for (QScreen* screen : m_screenManager->screens()) {
                    QString screenId = Utils::screenIdentifier(screen);
                    m_layoutManager->assignLayout(screenId, desktop, activity, layout);
                }
            }
            // Set active layout OUTSIDE blocker so activeLayoutChanged fires
            // and UnifiedLayoutController syncs its internal state.
            m_layoutManager->setActiveLayout(layout);
        }
    }
    // Clear ALL saved floating state when autotile is disabled globally.
    // Stale entries would incorrectly float windows on next activation.
    if (m_autotileEngine) {
        m_autotileEngine->clearAllSavedFloating();
    }
    // Clear saved snap-floats — we're fully back in snap mode, so the
    // save/restore mechanism is no longer needed until next autotile entry.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->service()->clearSavedSnapFloating();
    }
    // Note: resnap happens at the call site AFTER updateAutotileScreens() so that
    // windowsReleasedFromTiling clears floating state before windows are resnapped.
}

/**
 * @brief Activate autotile on all screens using the last algorithm.
 *
 * Assigns autotile layout to every screen and sets mode to Autotile.
 * @note Callers MUST call updateAutotileScreens() + updateLayoutFilter()
 *       afterward to derive per-screen state and update the layout model.
 */
void Daemon::handleSnappingToAutotile()
{
    if (!m_settings || !m_modeTracker || !m_unifiedLayoutController || !m_layoutManager || !m_screenManager) {
        return;
    }

    const QString algoId = resolveAlgorithmId();
    const QString autotileLayoutId = LayoutId::makeAutotileId(algoId);

    if (m_autotileEngine) {
        m_autotileEngine->setAlgorithm(algoId);
    }

    // Pre-save snap-float state before autotile entry (same rationale as toggle handler)
    presaveSnapFloats();

    // Pre-seed autotile engine with zone-ordered windows BEFORE layout switch.
    // This ensures deterministic window ordering: zone 1 → master, zone 2 → second, etc.
    for (QScreen* screen : m_screenManager->screens()) {
        seedAutotileOrderForScreen(screen->name());
    }

    // Assign autotile layout to ALL screens (global toggle).
    // Block signals to avoid N intermediate updateAutotileScreens()
    // from layoutAssigned — one final call below is sufficient.
    const int desktop = currentDesktop();
    const QString activity = currentActivity();
    {
        QSignalBlocker blocker(m_layoutManager.get());
        for (QScreen* screen : m_screenManager->screens()) {
            QString screenId = Utils::screenIdentifier(screen);
            m_layoutManager->assignLayoutById(screenId, desktop, activity, autotileLayoutId);
        }
    }
    m_modeTracker->setCurrentMode(TilingMode::Autotile);
}

QHash<Daemon::DesktopContextKey, QStringList> Daemon::captureAutotileOrders() const
{
    QHash<DesktopContextKey, QStringList> orders;
    if (!m_autotileEngine) {
        return orders;
    }
    const int desktop = currentDesktop();
    const QString activity = currentActivity();
    for (const QString& screenName : m_autotileEngine->autotileScreens()) {
        QStringList order = m_autotileEngine->tiledWindowOrder(screenName);
        if (!order.isEmpty()) {
            // Use connector name as screenId — consistent with how
            // seedAutotileOrderForScreen looks up by connector name.
            orders[DesktopContextKey{screenName, desktop, activity}] = order;
        }
    }
    return orders;
}

void Daemon::restoreAutotileOnlyGeometries(const QSet<QString>& excludeWindows)
{
    if (!m_windowTrackingAdaptor || m_lastAutotileOrders.isEmpty()) {
        return;
    }
    WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    if (!wts) {
        return;
    }
    for (auto it = m_lastAutotileOrders.constBegin(); it != m_lastAutotileOrders.constEnd(); ++it) {
        const QString& screenName = it.key().screenId;
        for (const QString& windowId : it.value()) {
            if (excludeWindows.contains(windowId))
                continue;
            if (wts->isWindowSnapped(windowId))
                continue;
            m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenName);
        }
    }
}

void Daemon::presaveSnapFloats()
{
    if (!m_windowTrackingAdaptor) {
        return;
    }
    WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    const QStringList floatingIds = wts->floatingWindows();
    for (const QString& fid : floatingIds) {
        if (!wts->isAutotileFloated(fid)) {
            wts->saveSnapFloating(fid);
            qCDebug(lcDaemon) << "Pre-saved snap-float for" << fid << "before autotile entry";
        }
    }
}

void Daemon::seedAutotileOrderForScreen(const QString& screenName)
{
    if (!m_autotileEngine || !m_windowTrackingAdaptor) {
        return;
    }

    // Prefer saved autotile order from last mode toggle (deterministic re-entry).
    // Falls back to zone-ordered window list when no saved order exists (first
    // activation, or windows changed between toggles).
    DesktopContextKey orderKey{screenName, currentDesktop(), currentActivity()};
    QStringList order = m_lastAutotileOrders.value(orderKey);
    if (order.isEmpty()) {
        WindowTrackingService* wts = m_windowTrackingAdaptor->service();
        if (wts) {
            order = wts->buildZoneOrderedWindowList(screenName);
        }
    }

    if (!order.isEmpty()) {
        // Clear saved-floating for windows that were re-snapped to zones.
        // If the user floated a window in autotile then re-snapped it in manual
        // mode, the re-snap shows intent to tile — don't restore as floating.
        // Un-snapped windows (not in zoneOrder) keep their saved floating state
        // so they stay floating when autotile is re-enabled.
        m_autotileEngine->clearSavedFloatingForWindows(order);
        m_autotileEngine->setInitialWindowOrder(screenName, order);
    }
}

void Daemon::processPendingGeometryUpdates()
{
    if (m_pendingGeometryUpdates.isEmpty()) {
        return;
    }

    // Recalculate zone geometries for ALL layouts so fixed-mode zones stay
    // normalized correctly.  Uses primary screen as the reference geometry
    // (per-layout useFullScreenGeometry is respected by effectiveScreenGeometry).
    QScreen* primaryScreen = Utils::primaryScreen();
    if (primaryScreen) {
        for (Layout* layout : m_layoutManager->layouts()) {
            layout->recalculateZoneGeometries(GeometryUtils::effectiveScreenGeometry(layout, primaryScreen));
        }
    }

    m_pendingGeometryUpdates.clear();

    // Single overlay update after all geometry recalculations
    m_overlayService->updateGeometries();

    // Ask effect to reapply snapped window positions (next event loop when REAPPLY_DELAY_MS is 0).
    m_reapplyGeometriesTimer.setInterval(REAPPLY_DELAY_MS);
    m_reapplyGeometriesTimer.start();

    // Re-query panel geometry once after a delay to pick up settled state (e.g. panel editor close).
    // That completion emits availableGeometryChanged → debounce → processPendingGeometryUpdates → reapply.
    m_screenManager->scheduleDelayedPanelRequery(DELAYED_PANEL_REQUERY_MS);

    // Retile autotile windows to adapt to new screen geometry
    // (panels added/removed, resolution changes, etc.)
    if (m_autotileEngine && m_autotileEngine->isEnabled()) {
        m_autotileEngine->retile();
    }
}

} // namespace PlasmaZones
