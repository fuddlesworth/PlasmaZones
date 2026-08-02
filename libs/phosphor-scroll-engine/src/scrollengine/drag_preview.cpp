// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Drag-insert preview for the scrolling strip — the scrolling twin of
// AutotileEngine's drag_preview.cpp, with the same architecture: the
// "preview" is a live relayout that skips the dragged window's own geometry
// (applyLayout consults m_dragInsertPreview), so neighbours animate around
// the previewed slot while KWin's interactive move keeps the dragged window
// under the cursor. Every structural edit in here is SIGNAL-SILENT; float
// bookkeeping signals fire only at commit.
//
// Where autotile restores by raw window-order index, the strip restores in
// FloatRestore vocabulary (column + tile + stack anchor + width/display/
// height intents) — a bare index goes stale the moment a column closes
// mid-drag, and the anchor-based relocation already solved that for the
// float round trip.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorScreens/ScreenIdentity.h>

#include "scrollenginelogging.h"

#include <algorithm>

namespace PhosphorScrollEngine {

namespace {

/// Cursor-band width for the new-column edge zones, as a fraction of the
/// hovered column's width, capped in pixels so very wide columns keep a
/// usable join-as-tile middle.
constexpr int kEdgeBandMaxPx = 96;
constexpr int kEdgeBandDivisor = 4;

/// Edge auto-scroll: band inside the work area's left/right edges that arms
/// the scroll, and the per-tick step. ~30 Hz drag ticks make 32 px/tick
/// roughly a full 1080p-width strip crossing in two seconds.
constexpr int kDragScrollBandPx = 48;
constexpr int kDragScrollStepPx = 32;

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

/// Keep the strip pixel-stable under the cursor across a structural edit:
/// re-derive the anchor so the viewport's left edge stays at @p oldViewX.
/// Without this every take-and-reinsert can re-clamp the active-relative
/// anchor and slide the whole strip under a stationary cursor — the
/// feedback loop autotile (fixed zones) never has.
static void holdViewX(ScrollStrip& strip, const ScrollLayoutParams& params, int oldViewX)
{
    const int newViewX = strip.relayout(params).viewX;
    if (newViewX != oldViewX) {
        // viewX = columnStripX(active) - anchor, so shifting the anchor by
        // the drift cancels it. restoreViewAnchor is raw (no clamp), which
        // is what we want mid-drag; the next structural mutation re-clamps.
        strip.restoreViewAnchor(strip.viewAnchor() + (newViewX - oldViewX), params);
    }
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

    const int oldViewX = targetState->strip().relayout(params).viewX;
    bool ok = true;
    if (preview.hadPriorState && preview.priorSameScreen && !preview.priorFloating) {
        // Same-screen reorder of a strip tile: no structural change at begin;
        // the first update repositions. Carried intents come from the live
        // slot.
        preview.carried = preview.priorSlot;
        if (preview.carried.column < 0) {
            ok = false; // reverse map said tracked, strip disagrees
        }
    } else if (preview.hadPriorState && preview.priorSameScreen) {
        // Same-screen FLOATING window adopted into the strip (the trigger
        // was held over a strip while dragging a floated window): silently
        // unfloat into the remembered slot — unfloatWindowInternal's shape
        // without its signals/apply.
        preview.hadFloatRestoreEntry = m_floatRestore.contains(windowId);
        preview.floatRestoreEntry = m_floatRestore.take(windowId);
        preview.wasScrollFloated = m_scrollFloatedWindows.remove(windowId);
        targetState->removeFloating(windowId);
        ok = dragPreviewRestoreSlot(targetState, windowId, preview.floatRestoreEntry, params, screenId);
        if (!ok) {
            targetState->addFloating(windowId);
            if (preview.hadFloatRestoreEntry) {
                m_floatRestore.insert(windowId, preview.floatRestoreEntry);
            }
            if (preview.wasScrollFloated) {
                m_scrollFloatedWindows.insert(windowId);
            }
        } else {
            preview.carried = preview.floatRestoreEntry;
        }
    } else {
        // Cross-screen adoption or fresh adoption: detach from the prior
        // context (if any), open a fresh column at the strip's right end —
        // the first update moves it to the real target, mirroring
        // autotile's append-then-move.
        if (preview.hadPriorState && priorState) {
            if (preview.priorFloating) {
                preview.hadFloatRestoreEntry = m_floatRestore.contains(windowId);
                preview.floatRestoreEntry = m_floatRestore.take(windowId);
                preview.wasScrollFloated = m_scrollFloatedWindows.remove(windowId);
                priorState->removeFloating(windowId);
                preview.carried = preview.floatRestoreEntry;
            } else {
                priorState->strip().takeWindow(windowId, layoutParamsForScreen(preview.priorKey.screenId));
                preview.carried = preview.priorSlot;
            }
        }
        // Defensive: a stale forward state left the window in the target
        // strip without a matching reverse-map entry — clear it first.
        if (targetState->strip().containsWindow(windowId)) {
            targetState->strip().takeWindow(windowId, params);
        }
        targetState->removeFloating(windowId);
        if (!preview.hadPriorState || !priorState) {
            // Fresh adoption carries no intents — seed from the target
            // screen's configured defaults.
            preview.carried.width = effectiveDefaultColumnWidth(screenId);
            preview.carried.display = effectiveDefaultColumnDisplay(screenId);
        }
        ok = targetState->strip().insertWindow(windowId, preview.carried.width, preview.carried.display, params,
                                               preview.carried.minWidth, preview.carried.minHeight,
                                               ScrollInsertPosition::Last);
        if (ok) {
            m_states.setKeyForWindow(windowId, targetKey);
        } else if (preview.hadPriorState && priorState) {
            // Roll the detach back so the engine is exactly as before.
            if (preview.priorFloating) {
                priorState->addFloating(windowId);
                if (preview.hadFloatRestoreEntry) {
                    m_floatRestore.insert(windowId, preview.floatRestoreEntry);
                }
                if (preview.wasScrollFloated) {
                    m_scrollFloatedWindows.insert(windowId);
                }
            } else {
                dragPreviewRestoreSlot(priorState, windowId, preview.priorSlot,
                                       layoutParamsForScreen(preview.priorKey.screenId), preview.priorKey.screenId);
            }
        }
    }
    if (!ok) {
        return false;
    }

    // Seed lastTarget from the applied position — the identity the hit-test
    // returns while the cursor sits over the window's own slot.
    const ScrollStrip& strip = targetState->strip();
    preview.lastTarget.primary = strip.columnOfWindow(windowId);
    if (preview.lastTarget.primary < 0) {
        // Same-screen reorder path never mutated, so this cannot miss; the
        // insert paths returned ok above. Belt only.
        return false;
    }
    const Column& homeColumn = strip.columns().at(preview.lastTarget.primary);
    preview.lastTarget.secondary = homeColumn.tiles.size() > 1 ? homeColumn.indexOfWindow(windowId) : -1;
    preview.lastTarget.newSlot = false;

    holdViewX(targetState->strip(), params, oldViewX);
    m_dragInsertPreview = preview;
    // Filtered relayout: the dragged window is skipped in the geometry batch
    // while neighbours animate into the preview layout.
    applyLayout(screenId, false);
    if (preview.hadPriorState && !preview.priorSameScreen
        && preview.priorKey == currentKeyForScreen(preview.priorKey.screenId)) {
        applyLayout(preview.priorKey.screenId, false);
    }
    return true;
}

void ScrollEngine::updateDragInsertPreview(const DragInsertTarget& target)
{
    if (!m_dragInsertPreview || !target.isValid()) {
        return;
    }
    DragInsertPreview& preview = *m_dragInsertPreview;
    ScrollState* state = stateForKey(preview.targetKey, /*createIfMissing=*/false);
    if (!state) {
        return;
    }
    if (target == preview.lastTarget) {
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(preview.targetScreenId);
    if (!params.workArea.isValid()) {
        return;
    }
    ScrollStrip& strip = state->strip();
    const int currentColumn = strip.columnOfWindow(preview.windowId);
    if (currentColumn < 0) {
        return;
    }
    const bool soleOwner = strip.columns().at(currentColumn).tiles.size() == 1;

    // Detach-shift normalization: taking a solo window removes its column,
    // so targets to the right of it shift one left. A join-target naming
    // the solo column itself IS the own slot (stable identity).
    int primary = target.primary;
    if (soleOwner) {
        if (!target.newSlot && primary == currentColumn) {
            return;
        }
        if (primary > currentColumn) {
            --primary;
        }
    }

    // Refresh the carried intents from the live slot so a mid-drag width or
    // height change (unlikely but possible via shortcuts) travels along.
    preview.carried = captureDragSlot(strip, preview.windowId);
    const ResolvedStrip before = strip.relayout(params);
    const int oldViewX = before.viewX;

    // View-stability reference for JOIN targets: the join column must stay
    // stationary under the cursor. The take below removes the dragged
    // window's own solo column first, and when that column sat LEFT of the
    // join target the strip contracts leftward — a bare viewX hold then
    // slides the target out from under the cursor, the next tick's hit-test
    // reads "right of the strip", and the window is expelled straight back
    // out (a stack into a right-hand column could never form). Pin the join
    // column through a surviving tile instead. newSlot inserts keep the
    // plain viewX hold: there the inserted column itself lands under the
    // cursor.
    QString referenceWindow;
    int referenceXBefore = 0;
    if (!target.newSlot) {
        const int joinIdx = std::clamp(target.primary, 0, strip.columnCount() - 1);
        const Column& joinColumn = strip.columns().at(joinIdx);
        for (const Tile& tile : joinColumn.tiles) {
            if (tile.windowId != preview.windowId) {
                referenceWindow = tile.windowId;
                break;
            }
        }
        for (const ResolvedColumn& rc : before.columns) {
            if (rc.columnIndex == joinIdx) {
                referenceXBefore = rc.rect.x();
                break;
            }
        }
    }

    strip.takeWindow(preview.windowId, params);

    bool inserted = false;
    if (target.newSlot || strip.isEmpty()) {
        inserted = strip.insertWindowAt(std::clamp(primary, 0, strip.columnCount()), preview.windowId,
                                        preview.carried.width, preview.carried.display, params);
    } else {
        const int joinColumn = std::clamp(primary, 0, strip.columnCount() - 1);
        const int tileIndex = target.secondary >= 0 ? target.secondary : strip.columns().at(joinColumn).tiles.size();
        inserted = strip.insertWindowIntoColumnAt(joinColumn, tileIndex, preview.windowId, params,
                                                  preview.carried.minWidth, preview.carried.minHeight);
    }
    if (!inserted) {
        // Both arms clamp into range, so a refusal means the strip emptied
        // under us — re-open a fresh column rather than losing the window.
        inserted = strip.insertWindow(preview.windowId, preview.carried.width, preview.carried.display, params,
                                      preview.carried.minWidth, preview.carried.minHeight);
        if (!inserted) {
            qCWarning(lcScrollEngine) << "updateDragInsertPreview: every insert refused for" << preview.windowId;
            return;
        }
    }
    if (preview.carried.minWidth > 0 || preview.carried.minHeight > 0) {
        strip.setWindowMinimumSize(preview.windowId, preview.carried.minWidth, preview.carried.minHeight);
    }
    strip.setWindowHeightIntent(preview.windowId, preview.carried.height);

    const int landedColumn = strip.columnOfWindow(preview.windowId);
    preview.lastTarget.primary = landedColumn;
    const Column& landed = strip.columns().at(landedColumn);
    preview.lastTarget.secondary = landed.tiles.size() > 1 ? landed.indexOfWindow(preview.windowId) : -1;
    preview.lastTarget.newSlot = false;

    bool held = false;
    if (!referenceWindow.isEmpty()) {
        // Re-anchor so the reference column's screen X is unchanged:
        // screenX = stripX - viewX, so absorbing the drift into viewX means
        // shrinking the anchor by the same amount (viewX = colX(active) -
        // anchor). Raw restore, same contract as holdViewX.
        const ResolvedStrip after = strip.relayout(params);
        for (const ResolvedColumn& rc : after.columns) {
            bool containsReference = false;
            for (const ResolvedTile& rt : rc.tiles) {
                if (rt.windowId == referenceWindow) {
                    containsReference = true;
                    break;
                }
            }
            if (!containsReference) {
                continue;
            }
            const int adjust = rc.rect.x() - referenceXBefore;
            if (adjust != 0) {
                strip.restoreViewAnchor(strip.viewAnchor() - adjust, params);
            }
            held = true;
            break;
        }
    }
    if (!held) {
        holdViewX(strip, params, oldViewX);
    }
    applyLayout(preview.targetScreenId, false);
}

void ScrollEngine::commitDragInsertPreview()
{
    if (!m_dragInsertPreview) {
        return;
    }
    const DragInsertPreview p = *m_dragInsertPreview;
    m_dragInsertPreview.reset();
    // Unfiltered relayout: the dragged window's geometry is finally emitted
    // (KWin's interactive move has ended and will accept the set).
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

    ScrollState* targetState = stateForKey(p.targetKey, /*createIfMissing=*/false);
    const ScrollLayoutParams params = layoutParamsForScreen(p.targetScreenId);

    if (p.hadPriorState && p.priorSameScreen) {
        if (targetState) {
            ScrollStrip& strip = targetState->strip();
            if (p.priorFloating) {
                // Re-float: pull back out of the strip and hand the
                // consumed FloatRestore entry back, exactly as taken.
                strip.takeWindow(p.windowId, params);
                targetState->addFloating(p.windowId);
                if (p.hadFloatRestoreEntry) {
                    m_floatRestore.insert(p.windowId, p.floatRestoreEntry);
                }
                if (p.wasScrollFloated) {
                    m_scrollFloatedWindows.insert(p.windowId);
                }
            } else {
                strip.takeWindow(p.windowId, params);
                dragPreviewRestoreSlot(targetState, p.windowId, p.priorSlot, params, p.targetScreenId);
            }
        }
    } else {
        ScrollState* priorState = p.hadPriorState ? m_states.stateForKey(p.priorKey) : nullptr;
        if (p.hadPriorState && !priorState) {
            // The prior context died between begin and cancel (desktop /
            // activity reconfigure). Leave the window in the target strip
            // rather than orphaning it, and sync bookkeeping.
            Q_EMIT windowFloatingStateSynced(p.windowId, false, p.targetScreenId);
        } else {
            if (targetState) {
                targetState->strip().takeWindow(p.windowId, params);
            }
            if (priorState) {
                if (p.priorFloating) {
                    priorState->addFloating(p.windowId);
                    if (p.hadFloatRestoreEntry) {
                        m_floatRestore.insert(p.windowId, p.floatRestoreEntry);
                    }
                    if (p.wasScrollFloated) {
                        m_scrollFloatedWindows.insert(p.windowId);
                    }
                } else {
                    dragPreviewRestoreSlot(priorState, p.windowId, p.priorSlot,
                                           layoutParamsForScreen(p.priorKey.screenId), p.priorKey.screenId);
                }
                m_states.setKeyForWindow(p.windowId, p.priorKey);
            } else {
                m_states.removeWindow(p.windowId);
            }
        }
    }

    applyLayout(p.targetScreenId, false);
    if (p.hadPriorState && !p.priorSameScreen && p.priorKey == currentKeyForScreen(p.priorKey.screenId)) {
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
    const QString previewWindow = m_dragInsertPreview ? m_dragInsertPreview->windowId : QString();
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
        // Inside this column's x-span. The dragged window's own slot is a
        // stable identity: over its own solo column (edge bands INCLUDED —
        // a new column where the solo column already sits is the same
        // layout, and answering newSlot would take-and-reinsert every
        // tick), or over its own tile in a shared column, return the
        // current target verbatim.
        const bool ownsColumnSolo =
            m_dragInsertPreview && column.tiles.size() == 1 && column.tiles.first().windowId == previewWindow;
        if (ownsColumnSolo) {
            return m_dragInsertPreview->lastTarget;
        }
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
        // Join the column as a tile: resolve the slot by y over the visible
        // tiles. A tabbed column shows one tile; joining appends.
        target.primary = column.columnIndex;
        target.secondary = column.tiles.size();
        for (int i = 0; i < column.tiles.size(); ++i) {
            const ResolvedTile& tile = column.tiles.at(i);
            if (tile.hidden) {
                continue;
            }
            if (m_dragInsertPreview && tile.windowId == previewWindow && tile.rect.contains(cursorPos)) {
                return m_dragInsertPreview->lastTarget;
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
    // Nothing visible (fully parked strip): hold the live preview's slot
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
    int step = 0;
    if (cursorPos.x() <= params.workArea.left() + kDragScrollBandPx) {
        step = -kDragScrollStepPx;
    } else if (cursorPos.x() >= params.workArea.right() - kDragScrollBandPx) {
        step = kDragScrollStepPx;
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
    // (commit re-applies unfiltered, ApplyFloat floats the tile out, a
    // cancelled move has KWin restore the frame), and a defensive retile
    // here would race the async float call — snapping the window to its
    // slot for a frame before the float lands.
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
