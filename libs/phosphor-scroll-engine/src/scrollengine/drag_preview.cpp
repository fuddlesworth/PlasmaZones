// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Drag-insert for the scrolling strip — DETACH-ONCE architecture, unlike
// autotile's live-restructure preview. Autotile can restructure per cursor
// tick because its zones are a fixed grid: moving a window never moves the
// ground under the cursor. A strip cannot: every take-and-reinsert shifts
// column positions and the view anchor, so a live preview slides the layout
// under a stationary cursor (and with a strip narrower than the viewport,
// no anchor compensation can keep a right-hand join target in place without
// unclamping the view). Live testing killed that design twice.
//
// So, niri-shaped semantics instead:
//   begin  — capture restoration state, detach the window from the strip
//            ONCE (neighbours close up, the strip settles and then never
//            moves again for the rest of the hold);
//   update — hit-test against the now-STABLE strip and remember the target;
//            no structural change, nothing slides;
//   commit — apply the remembered target in one structural insert at drop;
//   cancel — restore the captured slot (FloatRestore vocabulary: column,
//            tile, stack anchor, width/display/height intents — a raw index
//            goes stale when columns close mid-drag).

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorScreens/ScreenIdentity.h>

#include "scrollenginelogging.h"

#include <algorithm>
#include <cmath>

namespace PhosphorScrollEngine {

namespace {

/// Cursor-band width for the new-column edge zones, as a fraction of the
/// hovered column's width, capped in pixels so very wide columns keep a
/// usable join-as-tile middle.
constexpr int kEdgeBandMaxPx = 96;
constexpr int kEdgeBandDivisor = 4;

/// Edge auto-scroll: band inside the work area's left/right edges that arms
/// the scroll, and the maximum per-step size. The daemon drives steps from
/// a ~60 Hz timer (niri's dnd-edge-view-scroll shape), and the step scales
/// QUADRATICALLY with how deep the cursor sits in the band — brushing the
/// band's inner edge barely moves (no accidental yanks when aiming near an
/// edge column), parking at the screen edge reaches ~kDragScrollMaxStepPx
/// per step (~1400 px/s at 60 Hz).
constexpr int kDragScrollBandPx = 48;
constexpr int kDragScrollMaxStepPx = 24;

} // namespace

/// Capture @p windowId's current slot in FloatRestore vocabulary — the twin
/// of floatWindowInternal's capture block, minus the container bookkeeping.
ScrollEngine::FloatRestore ScrollEngine::captureDragSlot(const ScrollStrip& strip, const QString& windowId)
{
    FloatRestore slot;
    const int columnIdx = strip.columnOfWindow(windowId);
    if (columnIdx < 0) {
        return slot;
    }
    slot.column = columnIdx;
    const Column& column = strip.columns().at(columnIdx);
    slot.width = column.width;
    slot.display = column.display;
    const QSize minSize = strip.windowMinimumSize(windowId);
    slot.minWidth = minSize.width();
    slot.minHeight = minSize.height();
    const int tileIdx = column.indexOfWindow(windowId);
    if (tileIdx >= 0) {
        slot.height = column.tiles.at(tileIdx).height;
    }
    if (column.tiles.size() > 1) {
        slot.tileIndex = tileIdx;
        for (int i = slot.tileIndex - 1; i >= 0 && slot.stackAnchor.isEmpty(); --i) {
            slot.stackAnchor = column.tiles.at(i).windowId;
        }
        for (int i = slot.tileIndex + 1; i < column.tiles.size() && slot.stackAnchor.isEmpty(); ++i) {
            slot.stackAnchor = column.tiles.at(i).windowId;
        }
    }
    return slot;
}

bool ScrollEngine::dragPreviewRestoreSlot(ScrollState* state, const QString& windowId, const FloatRestore& slot,
                                          const ScrollLayoutParams& params, const QString& screenId)
{
    ScrollStrip& strip = state->strip();
    bool inserted = false;
    if (slot.tileIndex >= 0) {
        // The window left a SHARED column — relocate the stack through the
        // surviving-sibling anchor (the bare index goes stale when columns
        // close), same arm order as unfloatWindowInternal.
        const int anchoredColumn = slot.stackAnchor.isEmpty() ? -1 : strip.columnOfWindow(slot.stackAnchor);
        if (anchoredColumn >= 0) {
            inserted = strip.insertWindowIntoColumnAt(anchoredColumn, slot.tileIndex, windowId, params, slot.minWidth,
                                                      slot.minHeight);
        }
    }
    if (!inserted && slot.column >= 0) {
        inserted = strip.insertWindowAt(slot.column, windowId, slot.width, slot.display, params);
    }
    if (!inserted) {
        inserted = strip.insertWindow(windowId, effectiveDefaultColumnWidth(screenId),
                                      effectiveDefaultColumnDisplay(screenId), params, slot.minWidth, slot.minHeight);
    }
    if (inserted) {
        if (slot.minWidth > 0 || slot.minHeight > 0) {
            strip.setWindowMinimumSize(windowId, slot.minWidth, slot.minHeight);
        }
        if (slot.column >= 0 || slot.tileIndex >= 0) {
            strip.setWindowHeightIntent(windowId, slot.height);
        }
    }
    return inserted;
}

bool ScrollEngine::beginDragInsertPreview(const QString& rawWindowId, const QString& screenId)
{
    // canonicalizeForLookup, NOT a registering canonicalization: a drag
    // preview is a transient view of an existing window and must not mint a
    // canonical entry for an id the engine never tracked (autotile's begin
    // documents the same contract).
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (windowId.isEmpty() || screenId.isEmpty() || !isActiveOnScreen(screenId)) {
        return false;
    }
    if (m_dragInsertPreview) {
        cancelDragInsertPreview();
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    if (!params.workArea.isValid()) {
        return false;
    }
    const PhosphorEngine::PlacementStateKey targetKey = currentKeyForScreen(screenId);
    ScrollState* targetState = stateForKey(targetKey, /*createIfMissing=*/true);
    if (!targetState) {
        return false;
    }

    DragInsertPreview preview;
    preview.windowId = windowId;
    preview.targetScreenId = screenId;
    preview.targetKey = targetKey;

    ScrollState* priorState = nullptr;
    const auto it = m_states.windowKeys().constFind(windowId);
    if (it != m_states.windowKeys().constEnd()) {
        preview.hadPriorState = true;
        preview.priorKey = it.value();
        preview.priorSameScreen = (preview.priorKey == targetKey);
        priorState = m_states.stateForKey(preview.priorKey);
        if (priorState) {
            preview.priorFloating = priorState->isFloating(windowId);
            if (!preview.priorFloating) {
                preview.priorSlot = captureDragSlot(priorState->strip(), windowId);
            }
        }
    }

    // DETACH ONCE. The window leaves the strip model entirely for the rest
    // of the hold; commit re-inserts at the remembered target, cancel at
    // the captured slot. Carried intents are the window's OWN (begin-time)
    // — never refreshed from a transient host mid-drag, which is how column
    // widths were getting stamped across columns in the live-restructure
    // design.
    if (preview.hadPriorState && priorState) {
        if (preview.priorFloating) {
            preview.hadFloatRestoreEntry = m_floatRestore.contains(windowId);
            preview.floatRestoreEntry = m_floatRestore.take(windowId);
            preview.wasScrollFloated = m_scrollFloatedWindows.remove(windowId);
            priorState->removeFloating(windowId);
            preview.carried = preview.floatRestoreEntry;
            if (!preview.hadFloatRestoreEntry) {
                // A floating window with no restore entry carries no intents
                // of its own — seed the screen's configured defaults, not
                // FloatRestore's default-constructed 50% proportion.
                preview.carried.width = effectiveDefaultColumnWidth(screenId);
                preview.carried.display = effectiveDefaultColumnDisplay(screenId);
            }
        } else if (preview.priorSlot.column >= 0) {
            priorState->strip().takeWindow(windowId, layoutParamsForScreen(preview.priorKey.screenId));
            preview.carried = preview.priorSlot;
        } else {
            // Tracked but in NEITHER the strip nor the floating set —
            // detached residue from an earlier torn-down preview (or any
            // future bookkeeping slip). Refusing here would make the state
            // permanent: begin would never accept the window again and the
            // drop float cannot repair it either. Adopt-and-heal instead:
            // nothing to detach, default intents, and commit/cancel re-home
            // the window into the strip.
            qCWarning(lcScrollEngine) << "beginDragInsertPreview:" << windowId
                                      << "tracked but absent from strip and floating set — adopting to heal";
            preview.carried.width = effectiveDefaultColumnWidth(screenId);
            preview.carried.display = effectiveDefaultColumnDisplay(screenId);
        }
        // Keep the window tracked against the TARGET context while detached
        // (screen routing, isWindowTracked, the daemon's re-latch all keep
        // answering). Fresh adoption stays untracked until commit.
        m_states.setKeyForWindow(windowId, targetKey);
    } else {
        preview.carried.width = effectiveDefaultColumnWidth(screenId);
        preview.carried.display = effectiveDefaultColumnDisplay(screenId);
    }
    // Defensive: a stale forward state left the window in the target strip
    // without a matching reverse-map entry — clear it so commit cannot
    // double-insert.
    if (targetState->strip().containsWindow(windowId)) {
        targetState->strip().takeWindow(windowId, params);
    }

    m_dragInsertPreview = preview;
    // Neighbours close up once; from here the strip is STABLE until drop.
    applyLayout(screenId, false);
    if (preview.hadPriorState && !preview.priorSameScreen
        && preview.priorKey == currentKeyForScreen(preview.priorKey.screenId)) {
        applyLayout(preview.priorKey.screenId, false);
    }
    return true;
}

void ScrollEngine::updateDragInsertPreview(const DragInsertTarget& target)
{
    // No structural change mid-drag — the whole point of detach-once. The
    // hit-test already resolved against the settled strip, so the indexes
    // stay valid for commit (both insert arms clamp regardless).
    if (!m_dragInsertPreview || !target.isValid()) {
        return;
    }
    m_dragInsertPreview->lastTarget = target;
}

void ScrollEngine::commitDragInsertPreview()
{
    if (!m_dragInsertPreview) {
        return;
    }
    const DragInsertPreview p = *m_dragInsertPreview;
    m_dragInsertPreview.reset();

    ScrollState* targetState = stateForKey(p.targetKey, /*createIfMissing=*/true);
    if (!targetState) {
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(p.targetScreenId);
    ScrollStrip& strip = targetState->strip();

    bool inserted = false;
    if (p.lastTarget.isValid()) {
        if (p.lastTarget.newSlot || strip.isEmpty()) {
            inserted = strip.insertWindowAt(std::clamp(p.lastTarget.primary, 0, strip.columnCount()), p.windowId,
                                            p.carried.width, p.carried.display, params);
        } else {
            const int joinColumn = std::clamp(p.lastTarget.primary, 0, strip.columnCount() - 1);
            const int tileIndex =
                p.lastTarget.secondary >= 0 ? p.lastTarget.secondary : strip.columns().at(joinColumn).tiles.size();
            inserted = strip.insertWindowIntoColumnAt(joinColumn, tileIndex, p.windowId, params, p.carried.minWidth,
                                                      p.carried.minHeight);
        }
    }
    if (!inserted && p.hadPriorState && p.priorSameScreen) {
        // No target was ever resolved (zero-motion drop): back to the
        // captured slot rather than an arbitrary append.
        inserted = dragPreviewRestoreSlot(targetState, p.windowId, p.priorSlot.column >= 0 ? p.priorSlot : p.carried,
                                          params, p.targetScreenId);
    }
    if (!inserted) {
        inserted = strip.insertWindow(p.windowId, p.carried.width, p.carried.display, params, p.carried.minWidth,
                                      p.carried.minHeight, ScrollInsertPosition::Last);
    }
    if (!inserted) {
        // Never leave the window in the detached limbo (tracked, in neither
        // the strip nor the floating set) — that state is refused by every
        // normal path. Degrade to a floating window with its carried intents
        // preserved for a later unfloat.
        qCWarning(lcScrollEngine) << "commitDragInsertPreview: every insert refused for" << p.windowId
                                  << "— degrading to floating";
        targetState->addFloating(p.windowId);
        m_floatRestore.insert(p.windowId, p.carried);
        m_states.setKeyForWindow(p.windowId, p.targetKey);
        Q_EMIT windowFloatingStateSynced(p.windowId, true, p.targetScreenId);
        return;
    }
    if (p.carried.minWidth > 0 || p.carried.minHeight > 0) {
        strip.setWindowMinimumSize(p.windowId, p.carried.minWidth, p.carried.minHeight);
    }
    strip.setWindowHeightIntent(p.windowId, p.carried.height);
    // The dropped window is the one the user is looking at.
    strip.focusWindow(p.windowId, params);
    m_states.setKeyForWindow(p.windowId, p.targetKey);

    applyLayout(p.targetScreenId, false);
    // The window's float/tracking state changed for every entry mode except
    // the plain same-screen tiled reorder. floating=false routes through the
    // no-restore path (windowFloatingStateSynced), avoiding the
    // geometry-restore teleport of windowFloatingChanged.
    const bool adoptedOrUnfloated = !p.hadPriorState || !p.priorSameScreen || p.priorFloating;
    if (adoptedOrUnfloated) {
        Q_EMIT windowFloatingStateSynced(p.windowId, false, p.targetScreenId);
    }
    // Strip structure (and possibly the prior screen's) changed durably.
    Q_EMIT placementChanged(p.targetScreenId);
    if (p.hadPriorState && !p.priorSameScreen) {
        Q_EMIT placementChanged(p.priorKey.screenId);
    }
}

void ScrollEngine::cancelDragInsertPreview()
{
    if (!m_dragInsertPreview) {
        return;
    }
    const DragInsertPreview p = *m_dragInsertPreview;
    m_dragInsertPreview.reset();

    if (!p.hadPriorState) {
        // Fresh adoption never touched any state at begin — nothing to
        // restore.
        return;
    }

    if (p.priorSameScreen) {
        ScrollState* targetState = stateForKey(p.targetKey, /*createIfMissing=*/false);
        const ScrollLayoutParams params = layoutParamsForScreen(p.targetScreenId);
        if (p.priorFloating) {
            // The engine-global bookkeeping is restored UNCONDITIONALLY —
            // gating it on the state's survival destroyed the FloatRestore
            // entry when the context died between begin and cancel, leaving
            // the window unrestorable forever.
            if (p.hadFloatRestoreEntry) {
                m_floatRestore.insert(p.windowId, p.floatRestoreEntry);
            }
            if (p.wasScrollFloated) {
                m_scrollFloatedWindows.insert(p.windowId);
            }
            if (targetState) {
                targetState->addFloating(p.windowId);
            }
        } else if (targetState) {
            dragPreviewRestoreSlot(targetState, p.windowId, p.priorSlot, params, p.targetScreenId);
        }
        applyLayout(p.targetScreenId, false);
        return;
    }

    ScrollState* priorState = m_states.stateForKey(p.priorKey);
    if (!priorState) {
        // The prior context died between begin and cancel (desktop /
        // activity reconfigure). Re-home the detached window into the
        // TARGET strip rather than orphaning it, and sync bookkeeping.
        ScrollState* targetState = stateForKey(p.targetKey, /*createIfMissing=*/true);
        if (targetState) {
            const ScrollLayoutParams params = layoutParamsForScreen(p.targetScreenId);
            targetState->strip().insertWindow(p.windowId, p.carried.width, p.carried.display, params,
                                              p.carried.minWidth, p.carried.minHeight, ScrollInsertPosition::Last);
            m_states.setKeyForWindow(p.windowId, p.targetKey);
            applyLayout(p.targetScreenId, false);
        }
        Q_EMIT windowFloatingStateSynced(p.windowId, false, p.targetScreenId);
        return;
    }

    if (p.priorFloating) {
        priorState->addFloating(p.windowId);
        if (p.hadFloatRestoreEntry) {
            m_floatRestore.insert(p.windowId, p.floatRestoreEntry);
        }
        if (p.wasScrollFloated) {
            m_scrollFloatedWindows.insert(p.windowId);
        }
    } else {
        dragPreviewRestoreSlot(priorState, p.windowId, p.priorSlot, layoutParamsForScreen(p.priorKey.screenId),
                               p.priorKey.screenId);
    }
    m_states.setKeyForWindow(p.windowId, p.priorKey);
    applyLayout(p.targetScreenId, false);
    if (p.priorKey == currentKeyForScreen(p.priorKey.screenId)) {
        applyLayout(p.priorKey.screenId, false);
    }
}

PhosphorEngine::IPlacementEngine::DragInsertTarget
ScrollEngine::computeDragInsertTargetAtPoint(const QString& screenId, const QPoint& cursorPos) const
{
    DragInsertTarget target;
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    if (!state) {
        return target;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    if (!params.workArea.isValid()) {
        return target;
    }
    if (state->strip().isEmpty()) {
        target.primary = 0;
        target.newSlot = true;
        return target;
    }
    // The dragged window is DETACHED while a preview is live, so the strip
    // resolved here is stable across ticks — no own-slot special case is
    // needed (nothing the cursor hovers can be the dragged window).
    const ResolvedStrip resolved = state->strip().relayout(params);

    const ResolvedColumn* lastVisible = nullptr;
    for (const ResolvedColumn& column : resolved.columns) {
        if (!column.rect.intersects(params.workArea)) {
            continue;
        }
        lastVisible = &column;
        // Cursor left of this visible column's span: the gap before it (or
        // the strip's visible left edge) → a new column at its index.
        if (cursorPos.x() < column.rect.left()) {
            target.primary = column.columnIndex;
            target.newSlot = true;
            return target;
        }
        if (cursorPos.x() > column.rect.right()) {
            continue;
        }
        // Inside this column's x-span: edge bands open a new column beside
        // it, the middle joins it as a tile at the y-resolved slot.
        const int band = std::min(column.rect.width() / kEdgeBandDivisor, kEdgeBandMaxPx);
        if (cursorPos.x() < column.rect.left() + band) {
            target.primary = column.columnIndex;
            target.newSlot = true;
            return target;
        }
        if (cursorPos.x() > column.rect.right() - band) {
            target.primary = column.columnIndex + 1;
            target.newSlot = true;
            return target;
        }
        target.primary = column.columnIndex;
        target.secondary = column.tiles.size();
        for (int i = 0; i < column.tiles.size(); ++i) {
            const ResolvedTile& tile = column.tiles.at(i);
            if (tile.hidden) {
                continue;
            }
            if (cursorPos.y() <= tile.rect.center().y()) {
                target.secondary = i;
                break;
            }
            if (tile.rect.contains(cursorPos) || cursorPos.y() <= tile.rect.bottom()) {
                target.secondary = i + 1;
                break;
            }
        }
        return target;
    }
    if (lastVisible) {
        // Right of every visible column → a new column after the last one.
        target.primary = lastVisible->columnIndex + 1;
        target.newSlot = true;
        return target;
    }
    // Nothing visible (fully parked strip): hold the live preview's target
    // rather than snapping somewhere arbitrary.
    if (m_dragInsertPreview) {
        return m_dragInsertPreview->lastTarget;
    }
    return target;
}

bool ScrollEngine::nudgeDragScroll(const QString& screenId, const QPoint& cursorPos)
{
    if (!m_dragInsertPreview
        || !PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, screenId)) {
        return false;
    }
    ScrollState* state = stateForKey(m_dragInsertPreview->targetKey, /*createIfMissing=*/false);
    if (!state || state->strip().isEmpty()) {
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(m_dragInsertPreview->targetScreenId);
    if (!params.workArea.isValid()) {
        return false;
    }
    ScrollStrip& strip = state->strip();
    const ResolvedStrip resolved = strip.relayout(params);
    if (resolved.stripWidth <= params.workArea.width()) {
        return false; // strip fits the viewport — nothing to reveal
    }
    // Quadratic depth ramp: depth 0 at the band's inner edge, 1 at the
    // screen edge. Shallow contact scrolls barely at all, so dragging near
    // an edge column doesn't yank the strip; parking at the edge reaches
    // full speed.
    int step = 0;
    if (cursorPos.x() <= params.workArea.left() + kDragScrollBandPx) {
        const double depth = std::clamp(
            (params.workArea.left() + kDragScrollBandPx - cursorPos.x()) / double(kDragScrollBandPx), 0.0, 1.0);
        step = -std::max(1, static_cast<int>(std::lround(depth * depth * kDragScrollMaxStepPx)));
    } else if (cursorPos.x() >= params.workArea.right() - kDragScrollBandPx) {
        const double depth = std::clamp(
            (cursorPos.x() - (params.workArea.right() - kDragScrollBandPx)) / double(kDragScrollBandPx), 0.0, 1.0);
        step = std::max(1, static_cast<int>(std::lround(depth * depth * kDragScrollMaxStepPx)));
    } else {
        return false;
    }
    const int maxViewX = resolved.stripWidth - params.workArea.width();
    const int newViewX = std::clamp(resolved.viewX + step, 0, maxViewX);
    if (newViewX == resolved.viewX) {
        return false; // already pinned at this end
    }
    // viewX = columnStripX(active) - anchor: shifting the view right means
    // shrinking the anchor by the same amount. Raw restore — the clamp just
    // ran through newViewX above.
    strip.restoreViewAnchor(strip.viewAnchor() - (newViewX - resolved.viewX), params);
    applyLayout(m_dragInsertPreview->targetScreenId, false);
    return true;
}

void ScrollEngine::setInteractiveDragWindow(const QString& windowId)
{
    // Deliberately NO retile on clear: every drop path finalizes on its own
    // (commit re-applies, ApplyFloat floats the tile out, a cancelled move
    // has KWin restore the frame), and a defensive retile here would race
    // the async float call — snapping the window to its slot for a frame
    // before the float lands.
    m_interactiveDragWindow = canonicalizeForLookup(windowId);
}

void ScrollEngine::dropClosedWindowFromDragPreview(const QString& windowId)
{
    if (!m_dragInsertPreview) {
        return;
    }
    if (m_dragInsertPreview->windowId == windowId) {
        // The dragged window itself closed mid-preview: the close flow is
        // already removing it everywhere, so restoration would resurrect a
        // dead id — just drop the preview.
        m_dragInsertPreview.reset();
    }
}

} // namespace PhosphorScrollEngine
