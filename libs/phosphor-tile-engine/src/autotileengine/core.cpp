// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "engine_internal.h"

namespace PhosphorTileEngine {

// Canonicalize on every access so set/clear/read key on the same id regardless of
// the raw alias a caller passes — symmetric with isWindowFloatingInAutotile(). The
// daemon already passes canonical ids today, but relying on every caller's discipline
// is fragile: a raw mark + canonical clear would leak the marker across a mode flip.
void AutotileEngine::markAutotileFloated(const QString& rawWindowId)
{
    m_autotileFloatedWindows.insert(canonicalizeForLookup(rawWindowId));
}

void AutotileEngine::clearAutotileFloated(const QString& rawWindowId)
{
    m_autotileFloatedWindows.remove(canonicalizeForLookup(rawWindowId));
}

bool AutotileEngine::isAutotileFloated(const QString& rawWindowId) const
{
    return m_autotileFloatedWindows.contains(canonicalizeForLookup(rawWindowId));
}

QRect AutotileEngine::lastManagedRect(const QString& rawWindowId) const
{
    return m_lastAppliedTileRect.value(canonicalizeForLookup(rawWindowId));
}

int AutotileEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    for (auto it = m_autotileFloatedWindows.begin(); it != m_autotileFloatedWindows.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_autotileFloatedWindows.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    // Engine tracking sweep — the contract this override exists for ("window
    // died without a windowClosed signal"): a dead window's TilingState
    // membership is otherwise permanent, since windowOpened's ghost-removal
    // only fires when the same window re-announces and a dead window never
    // does — the layout retiles around a ghost tile forever. This is a BATCH
    // path (N dead windows reported at once), so do the lifecycle hook + state
    // removal per id but retile each affected screen ONCE afterward, rather
    // than N immediate retiles of the same screen via onWindowRemoved.
    QStringList staleTracked;
    for (auto it = m_states.windowKeys().constBegin(); it != m_states.windowKeys().constEnd(); ++it) {
        if (!aliveWindowIds.contains(it.key())) {
            staleTracked.append(it.key());
        }
    }
    QSet<QString> affectedScreens;
    for (const QString& windowId : std::as_const(staleTracked)) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "pruneStaleWindows: removing dead tracked window" << windowId;
        // Drop a dead window from an active drag-insert preview before tearing
        // down its state, mirroring windowClosed — else commit/cancel would
        // later re-add or float a dead id.
        dropClosedWindowFromDragPreview(windowId);
        const QString screenId = removeTrackedWindowNoRetile(windowId);
        if (!screenId.isEmpty()) {
            affectedScreens.insert(screenId);
        }
        ++pruned;
    }
    for (const QString& screenId : std::as_const(affectedScreens)) {
        retileAfterOperation(screenId, true);
    }
    // Min-size entries are keyed independently of tracking (windowOpened
    // stores them before any state insert), so sweep them directly.
    for (auto it = m_windowMinSizes.begin(); it != m_windowMinSizes.end();) {
        if (!aliveWindowIds.contains(it.key())) {
            it = m_windowMinSizes.erase(it);
        } else {
            ++it;
        }
    }
    // Same independent keying for the last-applied tile rects.
    for (auto it = m_lastAppliedTileRect.begin(); it != m_lastAppliedTileRect.end();) {
        if (!aliveWindowIds.contains(it.key())) {
            it = m_lastAppliedTileRect.erase(it);
        } else {
            ++it;
        }
    }
    return pruned;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Signal connections
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::onWindowZoneChanged(const QString& rawWindowId, const QString& zoneId)
{
    if (m_retiling)
        return;
    // Canonicalize at the event boundary like windowClosed()/windowFocused():
    // a mutated-appId alias would otherwise miss the isFloating() check below
    // and hand onWindowRemoved() an id no state tracks.
    const QString windowId = canonicalizeWindowId(rawWindowId);
    if (zoneId.isEmpty()) {
        for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
            if (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
                || it.key().activity != m_context.currentActivity()) {
                continue;
            }
            if (it.value() && it.value()->isFloating(windowId)) {
                return;
            }
        }
        onWindowRemoved(windowId);
    }
}

void AutotileEngine::connectSignals()
{
    // Window tracking signals
    // Primary window events (open/close/focus) are received via public methods:
    // windowOpened(), windowClosed(), windowFocused() - connected by Daemon to
    // WindowTrackingAdaptor signals. This connection also handles zone changes:
    if (m_windowTracker) {
        // String-based connect is required: m_windowTracker is IWindowTrackingService*
        // (non-QObject ABC), so PMF syntax is unavailable. The asQObject() escape
        // hatch is the standard Qt pattern for interface-based signal routing.
        // Verify the connection succeeds so signal/slot renames are caught at startup.
        bool ok = connect(m_windowTracker->asQObject(), SIGNAL(windowZoneChanged(QString, QString)), this,
                          SLOT(onWindowZoneChanged(QString, QString)));
        Q_ASSERT(ok);
        if (Q_UNLIKELY(!ok)) {
            qCCritical(PhosphorTileEngine::lcTileEngine)
                << "Failed to connect windowZoneChanged — autotile will not react to zone changes";
        }
    }

    // Screen geometry changes
    if (m_screenManager) {
        connect(m_screenManager, &PhosphorScreens::ScreenManager::availableGeometryChanged, this,
                [this](const PhosphorScreens::PhysicalScreen& screen, const QRect&) {
                    if (!screen.isValid()) {
                        return;
                    }
                    const QString physId = screen.identifier;
                    // When virtual screens are configured, autotile state is keyed by
                    // virtual screen IDs — retile each one instead of the physical ID.
                    if (m_screenManager->hasVirtualScreens(physId)) {
                        for (const QString& vsId : m_screenManager->virtualScreenIdsFor(physId)) {
                            onScreenGeometryChanged(vsId);
                        }
                    } else {
                        onScreenGeometryChanged(physId);
                    }
                });

        // Virtual screen reconfiguration — retile new virtual screens and
        // clean up orphaned PhosphorTiles::TilingState entries for virtual screens that no longer exist.
        connect(m_screenManager, &PhosphorScreens::ScreenManager::virtualScreensChanged, this,
                [this](const QString& physicalScreenId) {
                    const QStringList newVsIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
                    const QSet<QString> newVsSet(newVsIds.begin(), newVsIds.end());

                    // Cancel an active drag-insert preview whose target or
                    // prior screen is about to be orphaned — mirrors the
                    // setAutotileScreens guard. Without this the preview
                    // survives the state's deleteLater with dangling keys
                    // (null-guarded, but commit/cancel would then silently
                    // restore nothing or re-key a released window).
                    if (m_dragInsertPreview) {
                        const auto orphaned = [&](const QString& sid) {
                            return PhosphorIdentity::VirtualScreenId::isVirtual(sid)
                                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(sid) == physicalScreenId
                                && !newVsSet.contains(sid);
                        };
                        const QString targetScreen = m_dragInsertPreview->targetScreenId;
                        const QString priorScreen =
                            m_dragInsertPreview->hadPriorState ? m_dragInsertPreview->priorKey.screenId : QString();
                        if (orphaned(targetScreen) || (!priorScreen.isEmpty() && orphaned(priorScreen))) {
                            cancelDragInsertPreview();
                        }
                    }

                    // Find and release orphaned virtual screen states for this physical screen
                    QStringList releasedWindows;
                    QSet<QString> orphanedVsIds;
                    m_states.removeStatesIf(
                        [&](const TilingStateKey& key, PhosphorTiles::TilingState*) {
                            const QString& sid = key.screenId;
                            return PhosphorIdentity::VirtualScreenId::isVirtual(sid)
                                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(sid) == physicalScreenId
                                && !newVsSet.contains(sid);
                        },
                        [&](const TilingStateKey& key, PhosphorTiles::TilingState* state) {
                            const QString sid = key.screenId;
                            // This virtual screen no longer exists — release its
                            // windows via the shared teardown body. Unlike the
                            // toggle-off path, an orphaned VS id is never reused,
                            // so BOTH override layers go: the resolver's
                            // in-memory map here, the persisted settings below
                            // (clearPerScreenAutotileSettings). drainOverflow is
                            // deferred to the per-screen loop below: this loop
                            // visits EVERY desktop/activity context of the same
                            // VS id, and an in-helper drain on the first context
                            // would blind capturePlacement's overflow
                            // discriminator for the remaining contexts.
                            orphanedVsIds.insert(sid);
                            releaseScreenStateForTeardown(sid, state, releasedWindows,
                                                          /*drainOverflow=*/false);
                            // forgetScreen, not removeOverridesForScreen: this id
                            // is never reused, so remembering the algorithm it
                            // was on would strand a string for the session.
                            m_configResolver->forgetScreen(sid);
                            m_userTunedSplitRatio.remove(key);
                            m_userTunedMasterCount.remove(key);
                        });
                    for (const QString& sid : std::as_const(orphanedVsIds)) {
                        m_overflow.takeForScreen(sid);
                    }
                    // Drop stashed bags for every orphaned VS id. Driven off the
                    // same predicate the removal used rather than off
                    // orphanedVsIds, because that set is built inside the removal
                    // callback and so only holds ids that still had a live state.
                    // A VS toggled off earlier has no state, would be missing from
                    // it, and its bag is exactly the "sits in the stash forever"
                    // case this exists to prevent: the id is gone for good and is
                    // never reused, so nothing will ever harvest or match it again.
                    std::erase_if(m_scriptStateStash, [&](const auto& entry) {
                        const QString& sid = entry.first.screenId;
                        return PhosphorIdentity::VirtualScreenId::isVirtual(sid)
                            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(sid) == physicalScreenId
                            && !newVsSet.contains(sid);
                    });
                    for (const QString& windowId : std::as_const(releasedWindows)) {
                        m_states.removeWindow(windowId);
                    }
                    if (!releasedWindows.isEmpty()) {
                        Q_EMIT windowsReleased(releasedWindows, orphanedVsIds);
                    }

                    // Clean up per-screen autotile settings for removed virtual screens.
                    // Orphaned AutotileScreen: groups would otherwise accumulate indefinitely
                    // as virtual screen IDs are never reused after reconfiguration.
                    if (!orphanedVsIds.isEmpty()) {
                        const QSignalBlocker blocker(engineSettings());
                        if (auto* s = autotileSettings()) {
                            for (const QString& orphanId : orphanedVsIds)
                                s->clearPerScreenAutotileSettings(orphanId);
                        }
                        Q_EMIT settingsPersistRequested();
                    }

                    // Clean up per-screen desktop maps for removed virtual screens on this
                    // physical screen — BOTH the sticky-pin override and the per-output-VD
                    // map (#648). Use newVsSet (freshly-computed from
                    // PhosphorScreens::ScreenManager) rather than m_autotileScreens which
                    // reflects mode assignments and may not yet be updated for the new config.
                    m_context.removeScreensIf([&](const QString& key) {
                        return PhosphorIdentity::VirtualScreenId::isVirtual(key)
                            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(key) == physicalScreenId
                            && !newVsSet.contains(key);
                    });

                    // Retile the new virtual screens
                    for (const QString& vsId : newVsIds) {
                        onScreenGeometryChanged(vsId);
                    }
                });

        // Regions-only changes (VS swap/rotate/boundary resize) — skip the
        // orphan cleanup (no VSs removed/added) and just retile each VS with
        // its new geometry. This is the single authoritative retile for the
        // change; the Daemon's regions-only handler deliberately does NOT
        // call updateAutotileScreens so there is no second retile pass.
        connect(m_screenManager, &PhosphorScreens::ScreenManager::virtualScreenRegionsChanged, this,
                [this](const QString& physicalScreenId) {
                    const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
                    for (const QString& vsId : vsIds) {
                        onScreenGeometryChanged(vsId);
                    }
                });
    }

    // PhosphorZones::Layout changes — intentionally NOT connected.
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
    // if (m_layoutManager) {
    //     connect(m_layoutManager, &PhosphorZones::LayoutRegistry::activeLayoutChanged,
    //             this, &AutotileEngine::onLayoutChanged);
    // }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Enable/disable
// ═══════════════════════════════════════════════════════════════════════════════

} // namespace PhosphorTileEngine
