// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "../overlayservice.h"
#include "../modetracker.h"
#include "../unifiedlayoutcontroller.h"
#include "../../core/layoutmanager.h"
#include "../../core/layoutworker/layoutcomputeservice.h"
#include <PhosphorScreens/Manager.h>
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/constants.h"
#include "../../core/utils.h"
#include "../../core/windowtrackingservice.h"
#include "../config/settings.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../autotile/AutotileEngine.h"
#include "../../autotile/AutotileConfig.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <QGuiApplication>
#include <memory>
#include <QScreen>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {
// Follow-up panel geometry requery delay (ms): after the debounced geometry update
// completes, re-query panel geometry once so we pick up settled state (e.g. panel editor close).
// Conceptually distinct from GEOMETRY_UPDATE_DEBOUNCE_MS in daemon.cpp (which coalesces
// rapid geometry change events into a single update).
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
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        // Skip screens/desktops/activities where PlasmaZones is disabled
        if (isContextDisabled(m_settings.get(), screenId, desktop, activity)) {
            continue;
        }
        QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
            autotileScreens.insert(screenId);
            QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId);
            if (!algoId.isEmpty()) {
                screenAlgorithms[screenId] = algoId;
            }
        }
    }

    // Capture window order for screens LEAVING autotile before PhosphorTiles::TilingState is destroyed.
    // This preserves the tiling arrangement so re-entering autotile (e.g. cycling back)
    // restores the same window positions. Without this, only the settingsChanged path
    // (handleAutotileDisabled) captured orders — layout cycling lost them.
    const QSet<QString>& currentAutotileScreens = m_autotileEngine->autotileScreens();
    const QSet<QString> removedScreens = currentAutotileScreens - autotileScreens;
    for (const QString& screenId : removedScreens) {
        QStringList order = m_autotileEngine->tiledWindowOrder(screenId);
        if (!order.isEmpty()) {
            m_lastAutotileOrders[TilingStateKey{screenId, desktop, activity}] = order;
        }
    }

    // Seed window order for screens ENTERING autotile from saved state.
    // Must happen before setAutotileScreens() which retiles added screens.
    const QSet<QString> addedScreens = autotileScreens - currentAutotileScreens;
    for (const QString& screenId : addedScreens) {
        seedAutotileOrderForScreen(screenId);
    }

    // Apply per-screen overrides BEFORE setAutotileScreens so that newly added
    // screens are retiled with the correct per-screen algorithm (not the global
    // fallback).  applyPerScreenConfig lazily creates TilingStates via
    // stateForScreen(), which setAutotileScreens reuses for added screens.
    if (m_settings) {
        for (const QString& screenId : effectiveIds) {
            if (!autotileScreens.contains(screenId))
                continue;
            QVariantMap overrides = m_settings->getPerScreenAutotileSettings(screenId);
            // Inject algorithm from layout assignment (authoritative source)
            if (screenAlgorithms.contains(screenId)) {
                const QString screenAlgo = screenAlgorithms.value(screenId);
                overrides[QStringLiteral("Algorithm")] = screenAlgo;

                // When the per-screen algorithm differs from the engine's
                // current global algorithm and there's no explicit MaxWindows
                // override, inject the per-screen algorithm's default — but
                // only if the global maxWindows is still at the global
                // algorithm's default.  If the user customized maxWindows,
                // respect their choice.
                //
                // Use the engine's runtime algorithm (m_autotileEngine->algorithm())
                // instead of m_settings->defaultAutotileAlgorithm(). During layout cycling,
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
                if (screenAlgo != globalAlgo && !overrides.contains(PerScreenKeys::MaxWindows)) {
                    auto* screenAlgoPtr = m_algorithmRegistry.get()->algorithm(screenAlgo);
                    auto* globalAlgoPtr = m_algorithmRegistry.get()->algorithm(globalAlgo);
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
                                            << "for screen" << screenId;
                    }
                }
            }
            // Compare against currently applied overrides to avoid redundant retiles
            QVariantMap current = m_autotileEngine->perScreenOverrides(screenId);
            if (overrides != current) {
                if (!overrides.isEmpty()) {
                    m_autotileEngine->applyPerScreenConfig(screenId, overrides);
                } else {
                    m_autotileEngine->clearPerScreenConfig(screenId);
                }
            }
        }
    }

    // setAutotileScreens creates TilingStates for newly added screens (reusing
    // any already created by applyPerScreenConfig above) and retiles them.
    // Because per-screen overrides are set first, retileAfterOperation inside
    // setAutotileScreens uses effectiveAlgorithm() with the correct per-screen algo.
    const bool setChanged = (m_autotileEngine->autotileScreens() != autotileScreens);
    m_autotileEngine->setAutotileScreens(autotileScreens);

    // When the autotile set didn't change (e.g., VS inherited autotile from
    // the physical screen's cascade before the explicit assignment was written),
    // setAutotileScreens early-returns without retiling. Force a retile for
    // screens that are in the set so mode-swap toggles always take effect.
    if (!setChanged) {
        for (const QString& screenId : autotileScreens) {
            m_autotileEngine->scheduleRetileForScreen(screenId);
        }
    }

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
    // Feature disabled: flip every autotile assignment's mode to Snapping.
    // clearAutotileAssignments() preserves each screen's saved snappingLayout
    // and tilingAlgorithm fields, so a screen that was previously autotile
    // reverts to the snap layout it had before entering autotile. Screens
    // already in Snapping mode are untouched — this is what makes mixed-mode
    // setups (screen A snap + screen B autotile) recover correctly.
    if (m_layoutManager) {
        QSignalBlocker blocker(m_layoutManager.get());
        m_layoutManager->clearAutotileAssignments();
    }

    // Some screens may have entered autotile without ever having a snap layout
    // assigned (fresh installs, imported configs). For those, assignmentEntry's
    // snappingLayout is empty, and layoutForScreen() would fall through to the
    // default layout — which is fine except the overlay/layout model prefers
    // an explicit per-screen assignment. Fill those in individually without
    // clobbering screens that already have a valid snap layout.
    if (m_layoutManager && m_screenManager) {
        const int desktop = currentDesktop();
        const QString activity = currentActivity();
        const QStringList effectiveIds = m_screenManager->effectiveScreenIds();

        PhosphorZones::Layout* fallbackLayout = m_layoutManager->activeLayout();
        if (!fallbackLayout && !m_layoutManager->layouts().isEmpty()) {
            fallbackLayout = m_layoutManager->layouts().first();
        }

        {
            QSignalBlocker blocker(m_layoutManager.get());
            for (const QString& screenId : effectiveIds) {
                const QString existingSnapId = m_layoutManager->snappingLayoutForScreen(screenId, desktop, activity);
                PhosphorZones::Layout* existing =
                    existingSnapId.isEmpty() ? nullptr : m_layoutManager->layoutById(QUuid::fromString(existingSnapId));
                if (existing) {
                    continue; // Per-screen snap layout already valid — don't overwrite.
                }
                if (!fallbackLayout) {
                    continue;
                }
                if (!activity.isEmpty()) {
                    m_layoutManager->clearAssignment(screenId, desktop, activity);
                }
                m_layoutManager->assignLayout(screenId, desktop, QString(), fallbackLayout);
            }
        }
        // Set active layout OUTSIDE the blocker so activeLayoutChanged fires
        // and UnifiedLayoutController syncs its internal state.
        if (fallbackLayout && !m_layoutManager->activeLayout()) {
            m_layoutManager->setActiveLayout(fallbackLayout);
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
    if (!m_settings || !m_unifiedLayoutController || !m_layoutManager || !m_screenManager) {
        return;
    }

    // Resolve algorithm from settings (this is a global enable, not per-desktop toggle)
    QString algoId = m_settings->defaultAutotileAlgorithm();
    if (algoId.isEmpty()) {
        algoId = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
    }
    const QString autotileLayoutId = PhosphorLayout::LayoutId::makeAutotileId(algoId);

    if (m_autotileEngine) {
        m_autotileEngine->setAlgorithm(algoId);
    }

    // Pre-save snap-float state before autotile entry (same rationale as toggle handler)
    presaveSnapFloats();

    // Determine which screens need to be converted to autotile. Skip screens
    // that already have an autotile assignment so we preserve their per-screen
    // algorithm customization (mixed-mode: screen A snap → autotile, screen B
    // already autotile stays on its configured algorithm).
    const int desktop = currentDesktop();
    const QString activity = currentActivity();
    QStringList screensToConvert;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        const QString existing = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (!PhosphorLayout::LayoutId::isAutotile(existing)) {
            screensToConvert.append(screenId);
        }
    }

    if (screensToConvert.isEmpty()) {
        return;
    }

    // Pre-seed autotile engine with zone-ordered windows BEFORE layout switch
    // so we get deterministic window ordering (zone 1 → master, zone 2 → second).
    // Only seed screens that are actually being converted.
    for (const QString& sid : screensToConvert) {
        seedAutotileOrderForScreen(sid);
    }

    // Assign autotile layout to the screens being converted. Block signals to
    // avoid N intermediate updateAutotileScreens() from layoutAssigned — one
    // final call from the caller is sufficient. Write with empty activity so
    // the entry is visible to D-Bus/KCM queries that use empty activity for
    // cascading resolution.
    {
        QSignalBlocker blocker(m_layoutManager.get());
        for (const QString& screenId : screensToConvert) {
            if (!activity.isEmpty()) {
                m_layoutManager->clearAssignment(screenId, desktop, activity);
            }
            m_layoutManager->assignLayoutById(screenId, desktop, QString(), autotileLayoutId);
        }
    }
}

QHash<TilingStateKey, QStringList> Daemon::captureAutotileOrders() const
{
    QHash<TilingStateKey, QStringList> orders;
    if (!m_autotileEngine) {
        return orders;
    }
    const int desktop = currentDesktop();
    const QString activity = currentActivity();
    for (const QString& screenId : m_autotileEngine->autotileScreens()) {
        QStringList order = m_autotileEngine->tiledWindowOrder(screenId);
        if (!order.isEmpty()) {
            orders[TilingStateKey{screenId, desktop, activity}] = order;
        }
    }
    return orders;
}

void Daemon::restoreAutotileOnlyGeometries(const QSet<QString>& excludeWindows, int desktop, const QString& activity)
{
    // Legacy path — emits individual D-Bus signals. Prefer buildAutotileRestoreEntries()
    // + emitBatchedResnap() to batch float-restores into the resnap signal.
    if (!m_windowTrackingAdaptor || m_lastAutotileOrders.isEmpty()) {
        return;
    }
    WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    if (!wts) {
        return;
    }
    for (auto it = m_lastAutotileOrders.constBegin(); it != m_lastAutotileOrders.constEnd(); ++it) {
        if (desktop >= 0 && (it.key().desktop != desktop || it.key().activity != activity)) {
            continue;
        }
        const QString& screenId = it.key().screenId;
        for (const QString& windowId : it.value()) {
            if (excludeWindows.contains(windowId))
                continue;
            if (wts->isWindowSnapped(windowId))
                continue;
            if (wts->isWindowFloating(windowId))
                continue;
            m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenId);
        }
    }
}

QVector<ZoneAssignmentEntry> Daemon::buildAutotileRestoreEntries(const QSet<QString>& excludeWindows, int desktop,
                                                                 const QString& activity)
{
    QVector<ZoneAssignmentEntry> entries;
    if (!m_windowTrackingAdaptor || m_lastAutotileOrders.isEmpty()) {
        return entries;
    }
    WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    if (!wts) {
        return entries;
    }
    for (auto it = m_lastAutotileOrders.constBegin(); it != m_lastAutotileOrders.constEnd(); ++it) {
        if (desktop >= 0 && (it.key().desktop != desktop || it.key().activity != activity)) {
            continue;
        }
        const QString& screenId = it.key().screenId;
        for (const QString& windowId : it.value()) {
            if (excludeWindows.contains(windowId))
                continue;
            if (wts->isWindowSnapped(windowId))
                continue;
            if (wts->isWindowFloating(windowId))
                continue;
            // Strict per-instance lookup — no appId fallback. A window that was
            // only ever auto-tiled (never explicitly snapped, never explicitly
            // floated) has no authoritative pre-float position to restore to.
            // Falling back to the cross-session appId entry would teleport the
            // window to stale coordinates left behind by a ghost instance.
            // Leaving the window at its current tiled position is the least
            // surprising outcome.
            auto geo = wts->validatedPreTileGeometryExact(windowId, screenId);
            if (geo) {
                ZoneAssignmentEntry entry;
                entry.windowId = windowId;
                entry.targetZoneId = RestoreSentinel;
                entry.targetGeometry = *geo;
                entries.append(entry);
                qCInfo(lcDaemon) << "Batched float-restore: windowId=" << windowId << "geo=" << *geo
                                 << "screen=" << screenId;
            } else {
                qCInfo(lcDaemon) << "Batched float-restore: skipping" << windowId
                                 << "— no exact per-instance pre-tile entry";
            }
        }
    }
    return entries;
}

void Daemon::presaveSnapFloats(const QString& screenId)
{
    if (!m_windowTrackingAdaptor) {
        return;
    }
    WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    const QStringList floatingIds = wts->floatingWindows();
    for (const QString& fid : floatingIds) {
        if (wts->isAutotileFloated(fid)) {
            continue;
        }
        // When scoped to a screen, only save windows on that screen.
        // Windows floating on other screens are not entering autotile
        // and must not have their snap-float state recorded.
        if (!screenId.isEmpty()) {
            const QString windowScreen = wts->screenAssignments().value(fid);
            if (!windowScreen.isEmpty() && windowScreen != screenId) {
                continue;
            }
        }
        wts->saveSnapFloating(fid);
        qCDebug(lcDaemon) << "Pre-saved snap-float for" << fid << "screen=" << screenId;
    }
}

void Daemon::seedAutotileOrderForScreen(const QString& screenId)
{
    if (!m_autotileEngine || !m_windowTrackingAdaptor) {
        return;
    }

    // Prefer saved autotile order from last mode toggle (deterministic re-entry).
    // Falls back to zone-ordered window list when no saved order exists (first
    // activation, or windows changed between toggles).
    TilingStateKey orderKey{screenId, currentDesktop(), currentActivity()};
    QStringList order = m_lastAutotileOrders.value(orderKey);
    if (order.isEmpty()) {
        WindowTrackingService* wts = m_windowTrackingAdaptor->service();
        if (wts) {
            order = wts->buildZoneOrderedWindowList(screenId);
        }
    }

    if (!order.isEmpty()) {
        // Clear saved-floating for windows that were re-snapped to zones.
        // If the user floated a window in autotile then re-snapped it in manual
        // mode, the re-snap shows intent to tile — don't restore as floating.
        // Un-snapped windows (not in zoneOrder) keep their saved floating state
        // so they stay floating when autotile is re-enabled.
        m_autotileEngine->clearSavedFloatingForWindows(order);
        m_autotileEngine->setInitialWindowOrder(screenId, order);
    }
}

void Daemon::processPendingGeometryUpdates()
{
    if (!m_geometryUpdatePending) {
        return;
    }

    // Recalculate zone geometries for each effective screen (virtual or physical)
    // so fixed-mode zones stay normalized correctly against the correct screen geometry.
    // Async: each screen's computation runs on the worker thread. A barrier
    // tracks pending (screenId, layoutId) pairs explicitly so unrelated
    // geometriesComputed emissions (e.g. from an async layoutAssigned firing
    // mid-barrier) cannot drain it prematurely.
    const int desktop = m_virtualDesktopManager->currentDesktop();
    const QString activity =
        m_activityManager && ActivityManager::isAvailable() ? m_activityManager->currentActivity() : QString();
    const QStringList screenIds = m_screenManager->effectiveScreenIds();
    QSet<QUuid> processedLayouts;

    // Key = (screenId, layoutId). Matches what geometriesComputed carries.
    using PendingKey = QPair<QString, QUuid>;
    auto pending = std::make_shared<QSet<PendingKey>>();

    auto requestFor = [this, pending](PhosphorZones::Layout* layout, const QString& screenId, const QRectF& geom) {
        if (!layout) {
            return;
        }
        if (m_layoutComputeService->requestRecalculate(layout, screenId, geom)) {
            pending->insert({screenId, layout->id()});
        }
    };

    for (const QString& screenId : screenIds) {
        PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(screenId, desktop, activity);
        if (layout) {
            requestFor(layout, screenId,
                       GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, screenId));
            processedLayouts.insert(layout->id());
        }
    }

    // Also recalculate layouts not currently assigned to any screen
    // to prevent stale geometries when the user switches layouts.
    // Use primary screen geometry as fallback reference.
    const QVector<PhosphorZones::Layout*> allLayouts = m_layoutManager->layouts();
    for (auto* layout : allLayouts) {
        if (!processedLayouts.contains(layout->id())) {
            const QScreen* primaryScreen = Utils::primaryScreen();
            if (primaryScreen) {
                QString primaryId = Phosphor::Screens::ScreenIdentity::identifierFor(primaryScreen);
                requestFor(layout, primaryId,
                           GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, primaryId));
            }
        }
    }

    m_geometryUpdatePending = false;

    if (pending->isEmpty()) {
        m_overlayService->updateGeometries();
        m_reapplyGeometriesTimer.setInterval(REAPPLY_DELAY_MS);
        m_reapplyGeometriesTimer.start();
        return;
    }

    // Completion barrier: only fire the continuation once every pending
    // (screen, layout) pair has reported back. Unrelated emissions for
    // keys not in `pending` are ignored, so a concurrent async caller
    // cannot drain our barrier prematurely. We key off the signal's
    // `layoutId` (not `layout->id()`), so the barrier still drains when
    // a tracked PhosphorZones::Layout is destroyed mid-compute — LayoutComputeService
    // emits with layout==nullptr in that case.
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(
        m_layoutComputeService.get(), &LayoutComputeService::geometriesComputed, this,
        [this, pending, conn](const QString& screenId, const QUuid& layoutId, PhosphorZones::Layout* /*layout*/) {
            const PendingKey key{screenId, layoutId};
            if (!pending->remove(key)) {
                return; // not one of ours
            }
            if (pending->isEmpty()) {
                QObject::disconnect(*conn);
                m_overlayService->updateGeometries();
                m_reapplyGeometriesTimer.setInterval(REAPPLY_DELAY_MS);
                m_reapplyGeometriesTimer.start();
            }
        });

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
