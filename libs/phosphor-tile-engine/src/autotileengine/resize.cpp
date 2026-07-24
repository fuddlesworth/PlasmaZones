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

namespace {
// Union the rendered zones of every leaf under @p node into @p bbox. Leaves are
// matched to zones by looking their window id up in @p tiled (parallel to
// @p zones), so this makes no assumption about leaf/zone ordering. Depth-guarded
// like the rest of the SplitTree recursion.
void unionSubtreeZones(const PhosphorTiles::SplitNode* node, const QStringList& tiled, const QVector<QRect>& zones,
                       QRect& bbox, int depth = 0)
{
    if (!node || depth > PhosphorTiles::AutotileDefaults::MaxRuntimeTreeDepth) {
        return;
    }
    if (node->isLeaf()) {
        const int idx = tiled.indexOf(node->windowId);
        if (idx >= 0 && idx < zones.size()) {
            bbox = bbox.united(zones[idx]);
        }
        return;
    }
    unionSubtreeZones(node->first.get(), tiled, zones, bbox, depth + 1);
    unionSubtreeZones(node->second.get(), tiled, zones, bbox, depth + 1);
}

// The rendered extent of a split = bounding box of its subtree's zones. Read
// from the currently rendered zones (not recomputed from the tree) so the
// interactive-resize edge→ratio math stays in the same coordinate space as the
// compositor-reported window frame.
QRect subtreeBoundingRect(const PhosphorTiles::SplitNode* split, const QStringList& tiled, const QVector<QRect>& zones)
{
    QRect bbox;
    unionSubtreeZones(split, tiled, zones, bbox);
    return bbox;
}
} // namespace

void AutotileEngine::onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                                     const QString& screenId)
{
    if (rawWindowId.isEmpty() || !oldFrame.isValid() || !newFrame.isValid()) {
        return;
    }

    // Resolve to the canonical instance id that keys m_states, the
    // TilingState, and the SplitTree. The daemon calls this public boundary with
    // the raw id (like every other IPlacementEngine override here); without this
    // a window whose app class was renamed mid-session would pass the adaptor's
    // canonicalizing screenForTrackedWindow guard but then miss every lookup
    // below and silently drop the reflow. Lookup-only (no canonical-key mutation)
    // since a resize is not a window-registration point.
    const QString windowId = canonicalizeForLookup(rawWindowId);

    // The daemon resolved screenId from the same window→state map, so treat it as
    // authoritative. Resolve the owning state with a pure lookup: stateForWindow
    // never creates state, whereas tilingStateForScreen would insert an empty
    // TilingState for a known-but-stateless screen that then just fails the guards
    // below. stateForWindow returns the state stored for the window's
    // TilingStateKey; the ownerScreen != resolvedScreen check below then enforces
    // that the state's screen agrees with the daemon-supplied one.
    const QString resolvedScreen = screenId;
    if (resolvedScreen.isEmpty() || !isAutotileScreen(resolvedScreen)) {
        return;
    }
    QString ownerScreen;
    PhosphorTiles::TilingState* state = stateForWindow(windowId, &ownerScreen);
    if (!state || ownerScreen != resolvedScreen) {
        return;
    }

    // Floating windows are not part of the tiling — they never reflow neighbours.
    if (state->isFloating(windowId)) {
        return;
    }

    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(resolvedScreen);
    if (!algo) {
        return;
    }

    // Need at least two tiled windows for a neighbour to absorb the resize. The
    // exception is an algorithm that owns the single-window layout and records
    // the resize itself through the hook (e.g. a centered layout remembering a
    // per-monitor width): it has no neighbour to reflow but still wants the
    // event. The exception excludes memory/tree algorithms: their reflow is
    // Tier A below, which bails for a lone window (leafCount < 2), so admitting
    // one here would swallow the resize before the hook (Tier B) could run.
    if (state->tiledWindowCount() < 2
        && !(state->tiledWindowCount() == 1 && algo->supportsSingleWindow() && algo->supportsResizeHook()
             && !algo->supportsMemory())) {
        return;
    }

    // Cross-output guard: if the resize carried the window's centre off its
    // screen, this is a monitor handoff — let windowScreenChanged own the
    // reassignment rather than reflowing a layout the window is leaving. Use the
    // full screen rect, not the strut-inset work area, so a window whose centre
    // legitimately lands under a panel is not misread as having left the screen.
    // ScreenManager::screenGeometry() resolves both physical and virtual
    // (region-bounded) IDs to their full rect; the engine's available-geometry
    // helper is only a last resort when the manager has no tracked rect for the id.
    QRect screen;
    if (m_screenManager) {
        screen = m_screenManager->screenGeometry(resolvedScreen);
    }
    if (!screen.isValid()) {
        screen = screenGeometry(resolvedScreen);
    }
    if (screen.isValid() && !screen.contains(newFrame.center())) {
        return;
    }

    // Tier A — tree/memory algorithms reflow gap-free by adjusting the split
    // ratio of the ancestor split that owns each moved edge. These keep the
    // adjustment in the SplitTree, not state.splitRatio, so they are
    // intentionally outside the m_userTunedSplitRatio mechanism — no
    // noteSplitRatioUserTuned here.
    if (algo->supportsMemory()) {
        if (applyTreeResizeReflow(state, windowId, oldFrame, newFrame, resolvedScreen)) {
            retileAfterOperation(resolvedScreen, true);
        }
        return;
    }

    // Tier B — a non-tree algorithm that opts into the resize hook records the
    // adjustment (typically into TilingState::scriptState) before we retile; the
    // follow-up retile then lays the windows out honouring it. Algorithms without
    // the hook have no reflow model and leave the user's manual geometry as-is.
    if (algo->supportsResizeHook()) {
        const int threshold = PhosphorTiles::AutotileDefaults::ResizeEdgeMoveThresholdPx;
        const bool leftMoved = std::abs(newFrame.x() - oldFrame.x()) > threshold;
        const bool rightMoved =
            std::abs((newFrame.x() + newFrame.width()) - (oldFrame.x() + oldFrame.width())) > threshold;
        const bool topMoved = std::abs(newFrame.y() - oldFrame.y()) > threshold;
        const bool bottomMoved =
            std::abs((newFrame.y() + newFrame.height()) - (oldFrame.y() + oldFrame.height())) > threshold;
        PhosphorTiles::ResizeEvent ev;
        ev.index = state->tiledWindows().indexOf(windowId);
        // Defensive backstop: the window cleared the floating and tracked guards
        // above, so under both current overflow modes it is present in
        // tiledWindows() (Float floats over-cap windows — they return at the
        // floating guard — and Unlimited has no cap), meaning indexOf normally
        // succeeds. Guard the result anyway so a future overflow mode that keeps
        // a non-floating window out of the tiled list can never hand a -1 index
        // to the script hook.
        if (ev.index < 0) {
            return;
        }
        ev.oldRect = oldFrame;
        ev.newRect = newFrame;
        // Report at most one edge per axis. When both edges of an axis moved
        // together that axis translated (a move, not a resize), so neither edge
        // is reported — mirroring applyTreeResizeReflow and the per-axis
        // mutual-exclusion the ResizeEvent contract guarantees to scripts.
        ev.left = leftMoved && !rightMoved;
        ev.right = rightMoved && !leftMoved;
        ev.top = topMoved && !bottomMoved;
        ev.bottom = bottomMoved && !topMoved;
        if (ev.left || ev.right || ev.top || ev.bottom) {
            // The hook may apply a new split ratio to the state (ratio-based
            // algorithms reflow this way). If it did, mark the state user-tuned so
            // the change stays local to this screen+desktop+activity and survives a
            // settings refresh — exactly like an interactive master-ratio keystroke.
            const qreal ratioBefore = state->splitRatio();
            algo->onWindowResized(state, ev);
            if (!qFuzzyCompare(1.0 + state->splitRatio(), 1.0 + ratioBefore)) {
                noteSplitRatioUserTuned(resolvedScreen);
            }
            retileAfterOperation(resolvedScreen, true);
        }
    }
}

bool AutotileEngine::applyTreeResizeReflow(PhosphorTiles::TilingState* state, const QString& windowId,
                                           const QRect& oldFrame, const QRect& newFrame, const QString& screenId)
{
    using Edge = PhosphorTiles::SplitTree::Edge;

    PhosphorTiles::SplitTree* tree = state->splitTree();
    if (!tree || tree->leafCount() < 2 || !tree->leafForWindow(windowId)) {
        return false;
    }

    const QStringList tiled = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();
    // The reflow reads split extents from the rendered zones, so they must be in
    // lockstep with the tiled-window list. The divergence is usually transient:
    // while a capped layout (recalculateLayout sizes calculatedZones to
    // min(tiledCount, maxWindows, MaxZones)) is being applied, applyTiling has
    // not yet floated the over-cap windows out of tiledWindows(), so the lists
    // briefly differ in length. Float mode reconverges once applyTiling floats
    // the over-cap windows. Unlimited mode still caps zones at MaxZones (the
    // recalculateLayout clamp), so with >MaxZones tiled windows the divergence
    // recurs on every retile until applyTiling floats the excess. Bail rather
    // than read a stale/short vector — resizing during the divergence is a
    // no-op, which is fine.
    if (zones.isEmpty() || zones.size() != tiled.size()) {
        return false;
    }

    const int innerGap = effectiveInnerGap(screenId);
    const int threshold = PhosphorTiles::AutotileDefaults::ResizeEdgeMoveThresholdPx;

    // Identify which edge(s) moved. A resize moves at most one edge per axis; a
    // corner moves one on each axis. If both edges of an axis shifted together
    // that axis describes a translation (move), not a resize — skip it.
    struct EdgeMove
    {
        Edge edge;
        int newPos;
    };
    QVarLengthArray<EdgeMove, 2> moves;
    const int oldL = oldFrame.x();
    const int newL = newFrame.x();
    const int oldR = oldFrame.x() + oldFrame.width();
    const int newR = newFrame.x() + newFrame.width();
    const int oldT = oldFrame.y();
    const int newT = newFrame.y();
    const int oldB = oldFrame.y() + oldFrame.height();
    const int newB = newFrame.y() + newFrame.height();
    const bool leftMoved = std::abs(newL - oldL) > threshold;
    const bool rightMoved = std::abs(newR - oldR) > threshold;
    const bool topMoved = std::abs(newT - oldT) > threshold;
    const bool bottomMoved = std::abs(newB - oldB) > threshold;
    if (leftMoved != rightMoved) {
        moves.push_back(rightMoved ? EdgeMove{Edge::Right, newR} : EdgeMove{Edge::Left, newL});
    }
    if (topMoved != bottomMoved) {
        moves.push_back(bottomMoved ? EdgeMove{Edge::Bottom, newB} : EdgeMove{Edge::Top, newT});
    }
    if (moves.isEmpty()) {
        return false;
    }

    for (const EdgeMove& move : moves) {
        PhosphorTiles::SplitNode* split = tree->splitOwningEdge(windowId, move.edge);
        if (!split) {
            continue; // edge coincides with a screen boundary — nothing to resize
        }

        const QRect splitRect = subtreeBoundingRect(split, tiled, zones);
        if (!splitRect.isValid()) {
            continue;
        }

        const bool alongY = (move.edge == Edge::Top || move.edge == Edge::Bottom);
        const int axisStart = alongY ? splitRect.y() : splitRect.x();
        const int content = (alongY ? splitRect.height() : splitRect.width()) - innerGap;
        if (content <= 0) {
            continue;
        }

        // firstSize is the first child's extent up to the moved boundary. A
        // Right/Bottom edge belongs to a first-child window and sits on the
        // split line (firstSize = pos - start). A Left/Top edge belongs to a
        // second-child window and sits one gap past it (firstSize = pos - start - gap).
        const bool secondSide = (move.edge == Edge::Left || move.edge == Edge::Top);
        const int firstSize = secondSide ? (move.newPos - axisStart - innerGap) : (move.newPos - axisStart);
        const qreal ratio = static_cast<qreal>(firstSize) / static_cast<qreal>(content);

        tree->resizeSplitNode(split, ratio); // clamps to [MinSplitRatio, MaxSplitRatio]
    }

    // At least one edge moved past the threshold (moves is non-empty, checked
    // above), so the compositor has already committed an out-of-tile geometry
    // for the dragged window. Always retile to re-snap it onto its zone — even
    // when no split ratio actually changed because the edge was a screen
    // boundary or was already pinned at Min/MaxSplitRatio. Without this the
    // window would be stranded at its dragged size until the next incidental
    // retile.
    return true;
}

void AutotileEngine::onScreenGeometryChanged(const QString& screenId)
{
    if (!isAutotileScreen(screenId) || !m_states.containsKey(currentKeyForScreen(screenId))) {
        return;
    }

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "onScreenGeometryChanged:" << screenId << "geometry=" << screenGeometry(screenId);

    // Min-sizes are NOT cleared here. Stored min-sizes represent the window's
    // actual compositor-declared minimum (from windowOpened or the centering
    // code's reportDiscoveredMinSize), not stale zone widths. Clearing them
    // forces a retile with zero constraints, which produces zones that oversized
    // windows can't fill — the centering code then pushes them off-screen.
    // The old feedback loop (zone width → stored min → expanded zone) was
    // eliminated by removing the targetZone.width() fallback in
    // reportDiscoveredMinSize (commit c1d0ea16). Without that feedback loop,
    // indiscriminate clearing does more harm than good.

    retileAfterOperation(screenId, true);
}

void AutotileEngine::onLayoutChanged(PhosphorZones::Layout* layout)
{
    Q_UNUSED(layout)
    // Autotile screens are managed by per-screen assignments, not the global
    // active layout. Retile is triggered by setAutotileScreens() and
    // onScreenGeometryChanged() instead.
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal implementation
// ═══════════════════════════════════════════════════════════════════════════════

} // namespace PhosphorTileEngine
