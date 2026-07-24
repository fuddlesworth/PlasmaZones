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

bool AutotileEngine::beginDragInsertPreview(const QString& rawWindowId, const QString& screenId)
{
    // Neither D-Bus caller canonicalizes, and every m_states lookup below keys on
    // the canonical id. canonicalizeForLookup, NOT canonicalizeWindowId: a drag
    // preview is a transient view of an existing window, not a registration
    // point, so it must not mint a canonical entry in m_canonicalByInstance for
    // an id the engine has never tracked — an unknown window falls through as its
    // raw self and the hadPriorState lookup below simply misses, exactly as it
    // does today.
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (windowId.isEmpty() || screenId.isEmpty() || !isAutotileScreen(screenId)) {
        return false;
    }
    if (m_dragInsertPreview) {
        // A preview is already active — cancel it first so we don't leak state.
        cancelDragInsertPreview();
    }
    PhosphorTiles::TilingState* targetState = tilingStateForScreen(screenId);
    if (!targetState) {
        return false;
    }

    DragInsertPreview preview;
    preview.windowId = windowId;
    preview.targetScreenId = screenId;

    const TilingStateKey targetKey = currentKeyForScreen(screenId);

    // Capture prior engine state (if any) for restoration on cancel.
    // Look up the prior PhosphorTiles::TilingState once and reuse below to avoid a redundant
    // m_states hash lookup in the cross-screen branch.
    PhosphorTiles::TilingState* priorState = nullptr;
    auto it = m_states.windowKeys().constFind(windowId);
    if (it != m_states.windowKeys().constEnd()) {
        preview.hadPriorState = true;
        preview.priorKey = it.value();
        preview.priorSameScreen = (preview.priorKey == targetKey);
        priorState = m_states.stateForKey(preview.priorKey);
        if (priorState) {
            preview.priorRawIndex = priorState->windowOrder().indexOf(windowId);
            preview.priorFloating = priorState->isFloating(windowId);
        }
    }

    if (preview.hadPriorState && preview.priorSameScreen) {
        // Same-screen reorder: unfloat if it was floating, otherwise leave in place.
        // The first updateDragInsertPreview() call will reposition within the stack.
        if (preview.priorFloating) {
            targetState->setFloating(windowId, false);
            m_overflow.clearOverflow(windowId);
        }
    } else {
        // Cross-screen adoption or fresh adoption: remove from prior state (if any)
        // and append to the target state's tiled list.
        if (preview.hadPriorState && priorState) {
            priorState->removeWindow(windowId);
        }
        if (targetState->containsWindow(windowId)) {
            // Defensive: a stale forward TilingState left the window in the target
            // state without a matching m_states reverse-map (windowKeys) entry.
            // Remove it first so addWindow() can place it cleanly at the end.
            targetState->removeWindow(windowId);
        }
        targetState->addWindow(windowId);
        m_states.setKeyForWindow(windowId, targetKey);
    }

    // Evict last tiled neighbour if adoption pushed us over the cap for the
    // target screen. Skipped when the dragged window was already tiled on
    // target (same-screen reorder with priorFloating=false) because that
    // doesn't grow the count.
    //
    // Uses effectiveMaxWindows(screenId) so per-screen MaxWindows overrides
    // and global Unlimited mode are both honored consistently — the per-
    // screen override wins even when global is Unlimited, matching the
    // PerScreenConfigResolver priority chain.
    //
    // setFloating preserves the victim's raw position in m_windowOrder, so
    // cancel can restore it with a simple unfloat — no index bookkeeping
    // needed.
    const int maxWindows = effectiveMaxWindows(screenId);
    if (maxWindows > 0 && targetState->tiledWindowCount() > maxWindows) {
        const QStringList tiled = targetState->tiledWindows();
        for (int i = tiled.size() - 1; i >= 0; --i) {
            if (tiled[i] != windowId) {
                preview.evictedWindowId = tiled[i];
                targetState->setFloating(tiled[i], true);
                break;
            }
        }
    }

    preview.lastInsertIndex = targetState->tiledWindowIndex(windowId);

    if (preview.lastInsertIndex < 0) {
        // Something went wrong: window isn't in the tiled list after the setup above.
        // Roll back the prior-state capture so we don't leave the engine in a bad state.
        if (!preview.evictedWindowId.isEmpty()) {
            targetState->setFloating(preview.evictedWindowId, false);
        }
        if (preview.hadPriorState && preview.priorSameScreen) {
            // Same-screen: we only mutated the floating flag (if priorFloating).
            // Re-float to restore the original state.
            if (preview.priorFloating) {
                targetState->setFloating(windowId, true);
            }
        } else if (preview.hadPriorState) {
            // Cross-screen: we removed from prior state and added to target.
            // Undo both.
            targetState->removeWindow(windowId);
            if (PhosphorTiles::TilingState* priorState = m_states.stateForKey(preview.priorKey)) {
                priorState->addWindow(windowId, preview.priorRawIndex);
                if (preview.priorFloating) {
                    priorState->setFloating(windowId, true);
                }
                m_states.setKeyForWindow(windowId, preview.priorKey);
            } else {
                m_states.removeWindow(windowId);
            }
        } else {
            // Fresh adoption: just undo the add.
            targetState->removeWindow(windowId);
            m_states.removeWindow(windowId);
        }
        return false;
    }

    m_dragInsertPreview = preview;
    // Retile target (filtered) so the dragged window is skipped in the batch
    // while neighbours animate into the new layout.
    retileAfterOperation(screenId, /*operationSucceeded=*/true);
    // If we removed the window from a different screen, retile that one too so
    // its remaining windows fill the gap left by the departure.
    if (preview.hadPriorState && !preview.priorSameScreen) {
        retileAfterOperation(preview.priorKey.screenId, /*operationSucceeded=*/true);
    }
    return true;
}

void AutotileEngine::updateDragInsertPreview(int insertIndex)
{
    if (!m_dragInsertPreview) {
        return;
    }
    PhosphorTiles::TilingState* state = tilingStateForScreen(m_dragInsertPreview->targetScreenId);
    if (!state) {
        return;
    }
    const int tileCount = state->tiledWindowCount();
    if (tileCount <= 0) {
        return;
    }
    const int clamped = std::clamp(insertIndex, 0, tileCount - 1);
    if (clamped == m_dragInsertPreview->lastInsertIndex) {
        return; // No change — avoid redundant retile.
    }
    if (!state->moveToTiledPosition(m_dragInsertPreview->windowId, clamped)) {
        return;
    }
    m_dragInsertPreview->lastInsertIndex = clamped;
    retileAfterOperation(m_dragInsertPreview->targetScreenId, /*operationSucceeded=*/true);
}

void AutotileEngine::commitDragInsertPreview()
{
    if (!m_dragInsertPreview) {
        return;
    }
    const QString targetScreenId = m_dragInsertPreview->targetScreenId;
    const QString windowId = m_dragInsertPreview->windowId;
    const QString evictedWindowId = m_dragInsertPreview->evictedWindowId;
    const bool crossScreenAdoption = m_dragInsertPreview->hadPriorState && !m_dragInsertPreview->priorSameScreen;
    const bool freshAdoption = !m_dragInsertPreview->hadPriorState;
    // Same-screen reorders that unfloated the window also need the WTS sync
    // below so the daemon drops its stale "floating" bookkeeping.
    const bool sameScreenUnfloat = m_dragInsertPreview->hadPriorState && m_dragInsertPreview->priorSameScreen
        && m_dragInsertPreview->priorFloating;
    m_dragInsertPreview.reset();
    // Retile target without the filter so the dragged window's geometry is
    // applied on the next windowsTiled emission (KWin's interactive move has
    // ended and will accept the geometry set).
    retileAfterOperation(targetScreenId, /*operationSucceeded=*/true);
    // Emit a float-state sync signal whenever the window's tiling/floating
    // state changed as a result of this drag: cross-screen adoption, fresh
    // adoption, or a same-screen unfloat. Passing floating=false routes
    // through the no-restore path (windowFloatingStateSynced), avoiding the
    // geometry-restore teleport of windowFloatingChanged.
    if (crossScreenAdoption || freshAdoption || sameScreenUnfloat) {
        Q_EMIT windowFloatingStateSynced(windowId, false, targetScreenId);
    }
    // Evicted neighbour: route through the batch-float path so the daemon
    // restores its pre-tile geometry in one pass. Without this the victim
    // would stay stuck in its old tile rect visually while flagged floating
    // in PhosphorTiles::TilingState.
    if (!evictedWindowId.isEmpty()) {
        Q_EMIT windowsBatchFloated(QStringList{evictedWindowId}, targetScreenId);
    }
}

void AutotileEngine::cancelDragInsertPreview()
{
    if (!m_dragInsertPreview) {
        return;
    }
    const DragInsertPreview p = *m_dragInsertPreview;
    m_dragInsertPreview.reset();

    PhosphorTiles::TilingState* targetState = tilingStateForScreen(p.targetScreenId);

    // Restore eviction first: setFloating(false) returns the victim to its
    // original raw-order slot, making the subsequent restoration logic see
    // the same tiled list the user started with.
    if (!p.evictedWindowId.isEmpty() && targetState) {
        targetState->setFloating(p.evictedWindowId, false);
    }

    if (p.hadPriorState && p.priorSameScreen) {
        // Same-screen path: window was already in targetState at begin time.
        // Move it back to its original raw index and restore floating flag.
        if (targetState) {
            targetState->moveToPosition(p.windowId, p.priorRawIndex);
            if (p.priorFloating) {
                targetState->setFloating(p.windowId, true);
            }
        }
    } else {
        // Cross-screen / fresh adoption path. If the window came from another
        // state that still exists, restore it there. If the prior state was
        // evicted (desktop/VS reconfigure between begin and cancel) we cannot
        // restore — in that case leave the window in target rather than
        // orphaning it, and notify WTS so bookkeeping stays consistent.
        PhosphorTiles::TilingState* priorState = (p.hadPriorState) ? m_states.stateForKey(p.priorKey) : nullptr;
        if (p.hadPriorState && !priorState) {
            // m_states already points at target from begin(); leave
            // it there and let the window live in target state.
            Q_EMIT windowFloatingStateSynced(p.windowId, false, p.targetScreenId);
        } else {
            if (targetState) {
                targetState->removeWindow(p.windowId);
            }
            m_states.removeWindow(p.windowId);
            if (priorState) {
                // Defensive: if the prior state was torn down and rebuilt
                // between begin() and cancel(), it may already contain an
                // entry for this window (e.g. the rebuild re-adopted it from
                // a pending order). Mirror the begin() guard so addWindow()
                // can place it at the captured raw index cleanly.
                if (priorState->containsWindow(p.windowId)) {
                    priorState->removeWindow(p.windowId);
                }
                priorState->addWindow(p.windowId, p.priorRawIndex);
                if (p.priorFloating) {
                    priorState->setFloating(p.windowId, true);
                }
                m_states.setKeyForWindow(p.windowId, p.priorKey);
            }
        }
    }

    retileAfterOperation(p.targetScreenId, /*operationSucceeded=*/true);
    if (p.hadPriorState && !p.priorSameScreen) {
        retileAfterOperation(p.priorKey.screenId, /*operationSucceeded=*/true);
    }
}

int AutotileEngine::computeDragInsertIndexAtPoint(const QString& screenId, const QPoint& cursorPos) const
{
    // Const-correct lookup: avoid tilingStateForScreen() which may create state.
    auto it = m_states.states().constFind(currentKeyForScreen(screenId));
    if (it == m_states.states().constEnd() || !it.value()) {
        return -1;
    }
    const PhosphorTiles::TilingState* state = it.value();
    const QVector<QRect> zones = state->calculatedZones();
    if (zones.isEmpty()) {
        return 0;
    }
    const QStringList tiled = state->tiledWindows();
    // Walk zones in order; return the first zone whose rect contains the cursor.
    // Do NOT skip the dragged window's own zone — cursor-over-own-zone must be a
    // stable identity (return its current index), otherwise we force a shuffle
    // to some neighbour slot which immediately re-matches under the cursor and
    // oscillates every dragMoved tick.
    //
    // maxWindows may cap the layout so zones.size() < tiled.size(). In that
    // case, windows past `limit` have no zone and can't be hit-tested. If the
    // dragged window fell past the cap (e.g. evicted-to-floating in a tight
    // monocle-style layout), the stable-identity contract can't hold — hold
    // the preview at its last index instead.
    const int limit = std::min(zones.size(), tiled.size());
    const int draggedIdx = m_dragInsertPreview ? tiled.indexOf(m_dragInsertPreview->windowId) : -1;
    const bool draggedBeyondCap = draggedIdx >= 0 && draggedIdx >= limit;
    if (!draggedBeyondCap) {
        for (int i = 0; i < limit; ++i) {
            if (zones[i].contains(cursorPos)) {
                return i;
            }
        }
    }
    // Cursor isn't over any zone (or the dragged window is past the cap) —
    // hold the preview at its current index to avoid snapping to an endpoint.
    if (m_dragInsertPreview && m_dragInsertPreview->lastInsertIndex >= 0) {
        return m_dragInsertPreview->lastInsertIndex;
    }
    return tiled.isEmpty() ? 0 : tiled.size() - 1;
}

} // namespace PhosphorTileEngine
