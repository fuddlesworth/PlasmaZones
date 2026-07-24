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

void AutotileEngine::requestPostRetileFocus(const QString& screenId, const QString& windowId)
{
    m_pendingFocusByScreen.insert(screenId, windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Session Persistence
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::saveState()
{
    if (m_persistSaveFn) {
        m_persistSaveFn();
    }
}

void AutotileEngine::loadState()
{
    if (m_persistLoadFn) {
        m_persistLoadFn();
    }
}

// serializeWindowOrders / deserializeWindowOrders / serializePendingRestores /
// deserializePendingRestores / pruneExcludedPendingRestores were removed: autotile
// restore state (tiled position, floated geometry) is now persisted by the common
// WindowPlacementStore via capturePlacement(), the same path as snap. Tile order
// (current and non-current contexts alike) is reconstructed from each window's saved
// position in insertWindow() as the window re-announces in its own context — no
// separate per-context order snapshot is needed.

void AutotileEngine::scheduleRetileForScreen(const QString& screenId)
{
    // Contract: pending retiles are keyed by screenId and resolved against
    // the CURRENT desktop/activity context when processPendingRetiles runs.
    // A retile scheduled for a state living in another context (or raced by
    // a desktop switch in the one-event-loop-pass window before processing)
    // retiles the current context's state instead; the source context's
    // zones stay stale until its next operation — bounded by the same
    // accepted-drift trade-off as the identical-set desktop-switch
    // early-return in setAutotileScreens.
    m_pendingRetileScreens.insert(screenId);

    if (!m_retilePending) {
        m_retilePending = true;
        // Qt::QueuedConnection (same-thread deferral, not cross-thread — see Qt docs
        // on QMetaObject::invokeMethod) fires after all currently-pending events
        // (including D-Bus messages from the same socket read) are processed.
        // This naturally coalesces bursts: on first activation, the KWin effect
        // sends N windowOpened D-Bus calls in rapid succession — they all arrive
        // in one socket read and are dispatched before this queued call fires.
        // Single-window opens retile on the very next event loop iteration (~0ms).
        QMetaObject::invokeMethod(this, &AutotileEngine::processPendingRetiles, Qt::QueuedConnection);
    }
}

void AutotileEngine::processPendingRetiles()
{
    m_retilePending = false;

    if (m_pendingRetileScreens.isEmpty()) {
        return;
    }

    const QSet<QString> screens = m_pendingRetileScreens;
    m_pendingRetileScreens.clear();

    for (const QString& screenId : screens) {
        bool isAt = isAutotileScreen(screenId);
        bool hasState = m_states.containsKey(currentKeyForScreen(screenId));
        if (isAt && hasState) {
            qCInfo(PhosphorTileEngine::lcTileEngine) << "processPendingRetiles: retiling screen" << screenId;
            retileAfterOperation(screenId, true);
        } else {
            qCWarning(PhosphorTileEngine::lcTileEngine) << "processPendingRetiles: skipping screen" << screenId
                                                        << "isAutotile=" << isAt << "hasState=" << hasState;
        }
    }
}

void AutotileEngine::scheduleRetileRetry(const QString& screenId)
{
    int& count = m_retileRetryCount[screenId];
    if (count >= MaxRetileRetries) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "scheduleRetileRetry: exhausted" << MaxRetileRetries << "retries for screen" << screenId
            << "- screen geometry may be permanently unavailable";
        m_retileRetryCount.remove(screenId);
        m_retileRetryScreens.remove(screenId);
        return;
    }
    ++count;
    m_retileRetryScreens.insert(screenId);
    qCInfo(PhosphorTileEngine::lcTileEngine) << "scheduleRetileRetry: attempt" << count << "/" << MaxRetileRetries
                                             << "for screen" << screenId << "in" << RetileRetryIntervalMs << "ms";
    // Single shared timer across all screens — a screen queued later in the
    // same interval gets a shorter effective wait, which is harmless (it just
    // retries sooner and re-schedules if geometry is still unavailable).
    if (!m_retileRetryTimer.isActive()) {
        m_retileRetryTimer.start();
    }
}

void AutotileEngine::processRetileRetries()
{
    if (m_retileRetryScreens.isEmpty()) {
        return;
    }

    const QSet<QString> screens = m_retileRetryScreens;
    m_retileRetryScreens.clear();

    for (const QString& screenId : screens) {
        if (!isAutotileScreen(screenId)) {
            m_retileRetryCount.remove(screenId);
            continue;
        }
        qCInfo(PhosphorTileEngine::lcTileEngine) << "processRetileRetries: retrying screen" << screenId;
        retileAfterOperation(screenId, true);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Manual tiling operations
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::retile(const QString& screenId)
{
    // m_retiling serves as a re-entrancy guard for both retile() and
    // retileAfterOperation(). Both methods set it with QScopeGuard and check it
    // on entry. They are mutually exclusive: retileAfterOperation() returns early
    // if m_retiling is already true (set by retile()), so the dual QScopeGuard
    // pattern cannot leave the flag inconsistent.
    if (m_retiling) {
        return;
    }
    QScopeGuard guard([this] {
        m_retiling = false;
    });
    m_retiling = true;

    if (screenId.isEmpty()) {
        // Retile autotile screens only (current desktop/activity)
        for (const QString& screen : m_autotileScreens) {
            if (m_states.containsKey(currentKeyForScreen(screen))) {
                retileScreen(screen);
            }
        }
    } else {
        if (!isAutotileScreen(screenId)) {
            return;
        }
        retileScreen(screenId);
    }
}

void AutotileEngine::swapWindows(const QString& rawId1, const QString& rawId2)
{
    const QString windowId1 = canonicalizeWindowId(rawId1);
    const QString windowId2 = canonicalizeWindowId(rawId2);
    // Early return if same window (no-op)
    if (windowId1 == windowId2) {
        return;
    }

    // Find screens for both windows
    const auto key1 = m_states.keyForWindow(windowId1);
    const auto key2 = m_states.keyForWindow(windowId2);
    const QString screen1 = key1.screenId;
    const QString screen2 = key2.screenId;

    if (screen1.isEmpty() || screen2.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::swapWindows: window not found";
        return;
    }

    if (screen1 != screen2) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::swapWindows: windows on different screens";
        return;
    }

    // Use the stored key — not tilingStateForScreen(currentContext) — to avoid
    // wrong-desktop lookups from stale D-Bus calls after a context switch.
    PhosphorTiles::TilingState* state = m_states.stateForKey(key1);
    if (!state) {
        return;
    }

    const bool swapped = state->swapWindowsById(windowId1, windowId2);
    retileAfterOperation(screen1, swapped);
}

void AutotileEngine::promoteToMaster(const QString& rawWindowId)
{
    const QString windowId = canonicalizeWindowId(rawWindowId);
    QString screenId;
    PhosphorTiles::TilingState* state = stateForWindow(windowId, &screenId);
    if (!state) {
        return;
    }

    const bool promoted = state->moveToTiledPosition(windowId, 0);
    retileAfterOperation(screenId, promoted);
}

void AutotileEngine::demoteFromMaster(const QString& rawWindowId)
{
    const QString windowId = canonicalizeWindowId(rawWindowId);
    QString screenId;
    PhosphorTiles::TilingState* state = stateForWindow(windowId, &screenId);
    if (!state) {
        return;
    }

    // Move to position after master area (only if currently in master area)
    const int masterCount = state->masterCount();
    const int currentPos = state->tiledWindowIndex(windowId);

    bool demoted = false;
    if (currentPos >= 0 && currentPos < masterCount) {
        demoted = state->moveToTiledPosition(windowId, masterCount);
    }

    retileAfterOperation(screenId, demoted);
}

void AutotileEngine::swapFocusedWithMaster()
{
    m_navigation->swapFocusedWithMaster();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Focus/window cycling
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::focusNext()
{
    m_navigation->focusNext();
}

void AutotileEngine::focusPrevious()
{
    m_navigation->focusPrevious();
}

void AutotileEngine::focusMaster()
{
    m_navigation->focusMaster();
}

void AutotileEngine::setFocusedWindow(const QString& rawWindowId)
{
    onWindowFocused(canonicalizeWindowId(rawWindowId));
}

void AutotileEngine::setActiveScreenHint(const QString& screenId)
{
    if (!screenId.isEmpty()) {
        m_activeScreen = screenId;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split ratio adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterRatio(qreal delta)
{
    m_navigation->increaseMasterRatio(delta);
}

void AutotileEngine::decreaseMasterRatio(qreal delta)
{
    m_navigation->decreaseMasterRatio(delta);
}

void AutotileEngine::setGlobalSplitRatio(qreal ratio)
{
    // An explicit global set overrides any per-desktop tunings, matching the
    // settings-refresh path — otherwise the next propagate would skip the
    // just-set value on still-tuned current-desktop states.
    //
    // This clear spans EVERY key, so the write below must span every state to
    // match: NavigationController::setGlobalSplitRatio walks all of them. Do not
    // narrow one scope without the other. A clear that outruns the write leaves a
    // state holding a tuned value with no flag protecting it, and the next
    // propagateGlobalSplitRatio to run while that state's desktop is current
    // overwrites the user's value — a clobber deferred until some unrelated
    // settings refresh, which is what makes it so hard to trace back here.
    m_userTunedSplitRatio.clear();
    m_navigation->setGlobalSplitRatio(ratio);
}

void AutotileEngine::setGlobalMasterCount(int count)
{
    // Same clear-scope/write-scope pairing as setGlobalSplitRatio above.
    m_userTunedMasterCount.clear();
    m_navigation->setGlobalMasterCount(count);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master count adjustment
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::increaseMasterCount()
{
    m_navigation->adjustMasterCount(1);
}

void AutotileEngine::decreaseMasterCount()
{
    m_navigation->adjustMasterCount(-1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window rotation and floating
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::rotateWindowOrder(bool clockwise)
{
    m_navigation->rotateWindowOrder(clockwise);
}

void AutotileEngine::swapFocusedInDirection(const QString& direction, const QString& action)
{
    m_navigation->swapFocusedInDirection(direction, action);
}

void AutotileEngine::retileScreen(const QString& screenId)
{
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return;
    }

    // Pre-validate screen geometry BEFORE mutating state (overflow recovery).
    // QScreen can be transiently unavailable during Wayland desktop switches
    // (Plasma rebuilds the output list). If invalid, schedule a bounded retry
    // rather than proceeding — without this, the ratio change / window add
    // that triggered this retile is silently dropped, leaving stale zones.
    //
    // Only gate on geometry when a PhosphorScreens::ScreenManager exists (production). Without
    // one (unit tests), the existing recalculateLayout gracefully handles
    // the empty geometry as a structural no-op, not a transient failure.
    if (m_screenManager) {
        const QRect preValidatedGeometry = screenGeometry(screenId);
        if (!preValidatedGeometry.isValid()) {
            qCWarning(PhosphorTileEngine::lcTileEngine)
                << "retileScreen: screen geometry transiently invalid for" << screenId << "- deferring retile";
            scheduleRetileRetry(screenId);
            return;
        }

        // Geometry valid — clear any pending retry state for this screen.
        m_retileRetryCount.remove(screenId);
        m_retileRetryScreens.remove(screenId);
    }

    // Step 1: Recover overflow windows when room is available.
    // Collect recovery list first, mutate state, then defer signal emission
    // until after the entire retile cycle completes (prevents re-entrant
    // signal handlers from seeing partially-modified state).
    QStringList unfloated;
    if (!m_overflow.isEmpty()) {
        // Clamp the recovery ceiling at MaxZones, mirroring recalculateLayout's
        // windowCount cap: under Unlimited overflow effectiveMaxWindows returns
        // a huge sentinel, but the layout never places more than MaxZones
        // zones. Recovery must not admit windows the layout can never place,
        // or applyTiling re-floats them and the next retile unfloats them
        // again — a stable float/unfloat signal churn.
        const int recoveryMaxWindows =
            std::min(effectiveMaxWindows(screenId), PhosphorTiles::AutotileDefaults::MaxZones);
        unfloated = m_overflow.recoverIfRoom(
            screenId, state->tiledWindowCount(), recoveryMaxWindows,
            [state](const QString& wid) {
                return state->isFloating(wid);
            },
            [state](const QString& wid) {
                return state->containsWindow(wid);
            });
        for (const QString& wid : unfloated) {
            state->setFloating(wid, false);
            m_windowMinSizes.remove(wid);
        }
    }

    // Step 2-3: Recalculate layout and apply tiling (applyTiling also handles
    // new overflow detection and collects overflow signals internally).
    // On failure, zones are unchanged from the last successful recalc —
    // applyTiling must still run to handle overflow recovery from step 1.
    if (!recalculateLayout(screenId)) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "retileScreen: recalculateLayout failed for" << screenId << "- applying previous zone layout";
    }
    applyTiling(screenId);

    // Step 4: Emit all deferred signals after state is fully consistent.
    // Recovery signals first (unfloated windows), then overflow signals
    // (newly floated windows) were already handled inside applyTiling's
    // batch emit, and placementChanged is emitted last.
    for (const QString& wid : unfloated) {
        Q_EMIT windowFloatingChanged(wid, false, screenId);
    }
    Q_EMIT placementChanged(screenId);
}

void AutotileEngine::retileAfterOperation(const QString& screenId, bool operationSucceeded)
{
    if (!operationSucceeded) {
        return; // No change, no signal
    }

    if (!isAutotileScreen(screenId)) {
        return;
    }

    // This synchronous retile recomputes from the current state, so any deferred
    // retile already queued for the SAME screen is now redundant. Drop it —
    // otherwise processPendingRetiles fires a second batch for this screen
    // microseconds later, and that duplicate supersedes the staggered apply of
    // this one, stranding every window past the first (a cross-output move left
    // the source monitor with windows that never reflowed).
    m_pendingRetileScreens.remove(screenId);

    // When already inside retile(), still recalc and apply for this screen so
    // navigation (rotate, swap, etc.) is never dropped — user expects geometry
    // to update immediately. Do not clear m_retiling; let the outer retile() do that.
    if (m_retiling) {
        retileScreen(screenId);
        return;
    }

    QScopeGuard guard([this] {
        m_retiling = false;
    });
    m_retiling = true;
    retileScreen(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

} // namespace PhosphorTileEngine
