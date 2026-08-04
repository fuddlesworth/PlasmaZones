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
        for (int i = slot.tileIndex + 1; i < static_cast<int>(column.tiles.size()) && slot.stackAnchor.isEmpty(); ++i) {
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
        preview.priorSameKey = (preview.priorKey == targetKey);
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
        // Recorded, because this take happens on the !hadPriorState path too
        // and cancel's early return there says "never touched any state at
        // begin". Unrecorded, an Escape on such a window left it out of the
        // strip with nothing tracking it — removed from the engine outright
        // rather than put back. Capture the slot BEFORE the take.
        if (!preview.hadPriorState) {
            preview.defensivelyDetached = true;
            preview.defensiveSlot = captureDragSlot(targetState->strip(), windowId);
        }
        targetState->strip().takeWindow(windowId, params);
    }

    m_dragInsertPreview = preview;
    // Neighbours close up once; from here the strip is STABLE until drop.
    applyLayout(screenId, false);
    if (preview.hadPriorState && !preview.priorSameKey
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
        // The target screen left the scrolling set mid-hold and the cancels
        // that normally cover that raced past this commit. Do not return
        // with the reverse-map entry intact — that is the detached-residue
        // limbo every other path heals.
        m_states.removeWindow(p.windowId);
        return;
    }
    // The STATE above came from the preview's captured key; params and the
    // applyLayout below resolve from the screen's CURRENT key. Those agree
    // only while the context holds still for the drag, which is a contract
    // the DAEMON keeps: its six context-change handlers all cancel a live
    // preview before touching desktop/activity state, and the engine's own
    // prunes and unpin migration do the same. Nothing here can enforce it,
    // and a violation is silent — the window lands in the old desktop's
    // strip while the layout is computed and emitted for the new one, so it
    // is placed where nobody can see it and no geometry is applied. Say so
    // rather than letting it pass as a mystery.
    if (currentKeyForScreen(p.targetScreenId) != p.targetKey) {
        qCWarning(lcScrollEngine) << "commitDragInsertPreview: context moved under the preview for" << p.windowId
                                  << "on" << p.targetScreenId
                                  << "— a context-change handler failed to cancel first; the drop lands in the "
                                     "captured context";
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
            const int tileIndex = p.lastTarget.secondary >= 0
                ? p.lastTarget.secondary
                : static_cast<int>(strip.columns().at(joinColumn).tiles.size());
            inserted = strip.insertWindowIntoColumnAt(joinColumn, tileIndex, p.windowId, params, p.carried.minWidth,
                                                      p.carried.minHeight);
        }
    }
    if (!inserted && p.hadPriorState && p.priorSameKey) {
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
        // A cross-screen carried slot names the PRIOR screen's strip — a
        // later unfloat against the target strip must not consume it.
        FloatRestore carried = p.carried;
        if (!p.priorSameKey) {
            carried.column = -1;
            carried.tileIndex = -1;
            carried.stackAnchor.clear();
        }
        m_floatRestore.insert(p.windowId, carried);
        // Mode marker: this is a scroll-decided float, same as every other
        // float-producing exit (begin removed the marker on the way in).
        m_scrollFloatedWindows.insert(p.windowId);
        // Same drop floatWindowInternal makes on this transition. The window
        // is leaving the tiled set, so a remembered tile rect can only serve
        // as a stale comparand for the emit-on-change gate; the sibling paths
        // all clear it and this one was the exception.
        m_lastAppliedRect.remove(p.windowId);
        m_states.setKeyForWindow(p.windowId, p.targetKey);
        Q_EMIT windowFloatingStateSynced(p.windowId, true, p.targetScreenId);
        Q_EMIT placementChanged(p.targetScreenId);
        return;
    }
    if (p.carried.minWidth > 0 || p.carried.minHeight > 0) {
        strip.setWindowMinimumSize(p.windowId, p.carried.minWidth, p.carried.minHeight);
    }
    if (p.carried.column >= 0 || p.carried.tileIndex >= 0) {
        // Only a FloatRestore that really captured a tile carries a height
        // intent; a default-constructed one would stamp Auto over the
        // context default the insert just seeded (same gate as
        // dragPreviewRestoreSlot and unfloatWindowInternal).
        strip.setWindowHeightIntent(p.windowId, p.carried.height);
    }
    // The dropped window is the one the user is looking at.
    strip.focusWindow(p.windowId, params);
    m_states.setKeyForWindow(p.windowId, p.targetKey);

    // Drop the last-applied memory so the re-tile emit survives the
    // emit-on-change gate even when the window resolves back to its
    // pre-drag rect (single-column strip: no neighbour ever moves, so this
    // is the ONLY signal that re-tiles the dropped frame).
    m_lastAppliedRect.remove(p.windowId);
    applyLayout(p.targetScreenId, false);
    // The window's float/tracking state changed for every entry mode except
    // the plain same-screen tiled reorder. floating=false routes through the
    // no-restore path (windowFloatingStateSynced), avoiding the
    // geometry-restore teleport of windowFloatingChanged. The adopt-and-heal
    // entry (tracked but slotless: priorSlot never captured a tile) counts
    // too — the window reached that arm precisely because bookkeeping was
    // inconsistent, so the corrective announcement must not be skipped.
    const bool adoptedOrUnfloated =
        !p.hadPriorState || !p.priorSameKey || p.priorFloating || (p.priorSlot.column < 0 && p.priorSlot.tileIndex < 0);
    if (adoptedOrUnfloated) {
        Q_EMIT windowFloatingStateSynced(p.windowId, false, p.targetScreenId);
    }
    // Strip structure (and possibly the prior screen's) changed durably.
    Q_EMIT placementChanged(p.targetScreenId);
    if (p.hadPriorState && !p.priorSameKey) {
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
        // Fresh adoption normally touched no state at begin, so there is
        // nothing to restore. The exception is begin's defensive take: the
        // window WAS sitting in the target strip (with no reverse-map entry
        // behind it) and begin pulled it out, so returning here would leave
        // it removed from the strip and untracked — gone from the engine.
        // Put it back in the slot it held and re-key it, which is the same
        // healing begin performs for the tracked form of this residue.
        if (p.defensivelyDetached) {
            if (ScrollState* targetState = stateForKey(p.targetKey, /*createIfMissing=*/false)) {
                const ScrollLayoutParams params = layoutParamsForScreen(p.targetScreenId);
                if (dragPreviewRestoreSlot(targetState, p.windowId, p.defensiveSlot, params, p.targetScreenId)) {
                    m_states.setKeyForWindow(p.windowId, p.targetKey);
                    applyLayout(p.targetScreenId, false);
                }
            }
        }
        return;
    }

    if (p.priorSameKey) {
        // createIfMissing, matching the cross-key !priorState arm below. With
        // false, a context that died between begin and cancel left the tiled
        // sub-arm's `else if (targetState)` unentered: the window stayed
        // tracked at targetKey while held by no strip and no floating set,
        // which is the detached-residue limbo every other path goes out of
        // its way to avoid manufacturing. Re-homing into a fresh state for
        // the key costs a placeholder that a later prune reaps.
        ScrollState* targetState = stateForKey(p.targetKey, /*createIfMissing=*/true);
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
            // Same emit-on-change escape as commit: the restored slot is
            // typically the pre-drag rect, so without dropping the memory
            // the re-tile emit is suppressed.
            m_lastAppliedRect.remove(p.windowId);
        }
        applyLayout(p.targetScreenId, false);
        // The detach at begin and this restore both mutate persisted strip
        // structure (and the view anchor when edge-scroll ran mid-hold).
        Q_EMIT placementChanged(p.targetScreenId);
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
            m_lastAppliedRect.remove(p.windowId);
            applyLayout(p.targetScreenId, false);
            Q_EMIT placementChanged(p.targetScreenId);
        } else {
            // Target context refused too — the window would stay tracked
            // against a key holding it nowhere. Drop the reverse-map entry
            // instead of latching the detached-residue limbo.
            m_states.removeWindow(p.windowId);
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
        m_lastAppliedRect.remove(p.windowId);
    }
    m_states.setKeyForWindow(p.windowId, p.priorKey);
    applyLayout(p.targetScreenId, false);
    if (p.priorKey == currentKeyForScreen(p.priorKey.screenId)) {
        applyLayout(p.priorKey.screenId, false);
    }
    // Cross-key cancel changes which strip holds the window on BOTH ends.
    Q_EMIT placementChanged(p.targetScreenId);
    Q_EMIT placementChanged(p.priorKey.screenId);
}

PhosphorEngine::IPlacementEngine::DragInsertTarget
ScrollEngine::computeDragInsertTargetAtPoint(const QString& screenId, const QPoint& cursorPos) const
{
    DragInsertTarget target;
    // While a preview is live for this screen, resolve against the preview's
    // CAPTURED key, not the screen's current context: a desktop/activity
    // switch mid-drag would otherwise hit-test the new context's strip while
    // commit applies those indexes to the old one.
    const bool previewOwnsScreen = m_dragInsertPreview
        && PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, screenId);
    const ScrollState* state = previewOwnsScreen ? m_states.stateForKey(m_dragInsertPreview->targetKey)
                                                 : m_states.stateForKey(currentKeyForScreen(screenId));
    if (!state) {
        return target;
    }
    // The PREVIEW's screen id, not the caller's, whenever a preview owns this
    // screen. screensMatch above accepts a virtual/physical spelling
    // difference between the two, and layoutParamsForScreen resolves gaps and
    // the work area per SCREEN ID — so passing the caller's spelling could
    // hit-test against a work area the commit path never uses. Both siblings
    // (dragInsertIndicatorRect and nudgeDragScroll) already use the preview's.
    const ScrollLayoutParams params =
        layoutParamsForScreen(previewOwnsScreen ? m_dragInsertPreview->targetScreenId : screenId);
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
        // it, the middle joins it as a tile at the y-resolved slot. Floor of
        // 1 so integer division on a degenerate sliver still leaves bands.
        const int band = std::clamp(column.rect.width() / kEdgeBandDivisor, 1, kEdgeBandMaxPx);
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
        // secondary indexes the MODEL column's tiles — that is what
        // insertWindowIntoColumnAt consumes at commit. Resolved tiles omit
        // minimized ones, so map through the hovered tile's windowId rather
        // than reusing the resolved position, which skews by one slot per
        // preceding minimized tile.
        const Column& modelColumn = state->strip().columns().at(column.columnIndex);
        target.secondary = static_cast<int>(modelColumn.tiles.size());
        for (const ResolvedTile& tile : column.tiles) {
            if (tile.hidden) {
                continue;
            }
            if (cursorPos.y() <= tile.rect.center().y()) {
                target.secondary = std::max(0, modelColumn.indexOfWindow(tile.windowId));
                break;
            }
            if (cursorPos.y() <= tile.rect.bottom()) {
                target.secondary = modelColumn.indexOfWindow(tile.windowId) + 1;
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
    // rather than snapping somewhere arbitrary. Only for the preview's OWN
    // screen — a stateless sibling screen must not inherit another screen's
    // remembered target (nudgeDragScroll carries the same guard).
    if (previewOwnsScreen) {
        return m_dragInsertPreview->lastTarget;
    }
    return target;
}

QRect ScrollEngine::dragInsertIndicatorRect(const QString& screenId) const
{
    if (!m_dragInsertPreview
        || !PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, screenId)) {
        return {};
    }
    const DragInsertTarget target = m_dragInsertPreview->lastTarget;
    if (!target.isValid()) {
        return {};
    }
    // The preview's CAPTURED key, like commit and the hit-test: a context
    // switch mid-drag must not paint an indicator over a strip the drop
    // will not land in.
    const ScrollState* state = m_states.stateForKey(m_dragInsertPreview->targetKey);
    if (!state) {
        return {};
    }
    const DragInsertPreview& p = *m_dragInsertPreview;

    // SIMULATE THE DROP RATHER THAN MODEL IT.
    //
    // This function used to re-derive the landing rect with its own column and
    // slot arithmetic, and every version of that arithmetic was wrong in a
    // different way: it indexed the resolved column vector with a model index,
    // split a column into equal shares when relayout distributes by height
    // intent and weight, omitted the inner gap the real layout subtracts,
    // ignored the min-height clamp and its rebalance, treated a tabbed column
    // as a vertical stack when every tab shares one rect, and read the full
    // column extent where the tiles get contentRectFor. Each of those was a
    // separate defect with a separate patch, and the list kept growing.
    //
    // So: copy the strip, apply the SAME insert commitDragInsertPreview would,
    // relayout, and read back the rect the dropped window actually gets. The
    // layout code becomes the single source of truth for the layout, which is
    // the only way this can stay correct as the strip gains features.
    //
    // The copy is per call and the strip is a plain value type. That is not
    // free, and the ledger already tracks the per-tick relayout cost of this
    // whole path — but a cheap wrong rect is worth less than an accurate one,
    // and the change-gate in the daemon means a stationary cursor never
    // reaches here at all.
    ScrollStrip probe = state->strip();

    // Params resolved against the POST-drop column count. Smart gaps zero the
    // outer gaps for a single-column strip, and while the preview holds the
    // dragged window detached a strip that will have two columns still reads
    // as one — so the live params describe a work area the dropped window
    // never occupies.
    const int postDropColumns = probe.columnCount() + ((target.newSlot || probe.isEmpty()) ? 1 : 0);
    const ScrollLayoutParams params = layoutParamsForScreen(p.targetScreenId, postDropColumns);
    if (!params.workArea.isValid()) {
        return {};
    }

    // Mirror of commit's insert selection, deliberately kept line-for-line
    // comparable with it: if the two ever diverge, the indicator lies.
    bool inserted = false;
    if (target.newSlot || probe.isEmpty()) {
        inserted = probe.insertWindowAt(std::clamp(target.primary, 0, probe.columnCount()), p.windowId, p.carried.width,
                                        p.carried.display, params);
    } else {
        const int joinColumn = std::clamp(target.primary, 0, probe.columnCount() - 1);
        const int tileIndex =
            target.secondary >= 0 ? target.secondary : static_cast<int>(probe.columns().at(joinColumn).tiles.size());
        inserted = probe.insertWindowIntoColumnAt(joinColumn, tileIndex, p.windowId, params, p.carried.minWidth,
                                                  p.carried.minHeight);
    }
    if (!inserted) {
        return {};
    }
    // The same two post-insert stamps commit applies, under the same gates —
    // both change the resolved geometry, so omitting either would reintroduce
    // a modelling error by the back door.
    if (p.carried.minWidth > 0 || p.carried.minHeight > 0) {
        probe.setWindowMinimumSize(p.windowId, p.carried.minWidth, p.carried.minHeight);
    }
    if (p.carried.column >= 0 || p.carried.tileIndex >= 0) {
        probe.setWindowHeightIntent(p.windowId, p.carried.height);
    }

    // MIRRORS commit's focusWindow, which re-anchors the view so the dropped
    // column is scrolled into place. This used to be deliberately omitted, on
    // the reasoning that the indicator should mark the place under the cursor
    // rather than jump to where the window lands after the view scrolls. That
    // reasoning only holds while the target slot is ON SCREEN.
    //
    // With a FULL viewport it is not, and omitting the re-anchor produced no
    // indicator at all. Two columns filling a 1200px work area, aim at either
    // outer edge: inserting before the first resolves the slot to x=-600 and
    // inserting after the last to x=1200. Both are outside the work area, so
    // the overlay clipped them and the drag ran with no drop feedback in the
    // one configuration where a user most needs it. The DROP was correct
    // throughout — commit re-anchors and the window lands visibly — so the
    // indicator was contradicting an outcome that was already right.
    //
    // Mirroring it costs nothing when the slot is already visible: focusWindow
    // re-anchors only when the focused column actually changes, so a target in
    // the middle of a partly-filled strip still resolves under the cursor.
    // Every drop-equivalence test in the suite pins that, comparing this rect
    // against the post-commit tile rect.
    probe.focusWindow(p.windowId, params);
    const ResolvedStrip resolved = probe.relayout(params);
    for (const ResolvedColumn& column : resolved.columns) {
        for (const ResolvedTile& tile : column.tiles) {
            if (tile.windowId == p.windowId) {
                return tile.rect;
            }
        }
    }
    // Resolved away (every column zero-width on a degenerate work area, or the
    // tile hidden behind an active tab). Nothing truthful to paint.
    return {};
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
    // Pick the band by the NEARER edge so a work area narrower than two
    // bands cannot route a right-edge cursor into the left band, then ramp
    // quadratically with depth. A depth whose step ROUNDS TO ZERO is treated
    // as outside the band: with a 24px maximum that is roughly the outer 14%
    // of the band, so the shallowest contact does nothing at all rather than
    // creeping. Note this is a rounding threshold, not a 1px floor — the step
    // goes 0, 1, 2, ... as the cursor deepens, so a slow crawl IS reachable
    // just inside the threshold. That is the intended feel; the guard only
    // exists so brushing the very edge of the band is inert.
    //
    // The nearer-edge arm is deliberately UNTESTED, and cannot be tested
    // through the view anchor. The bands only overlap on a work area under
    // 2*kDragScrollBandPx wide, and on a viewport that small the columns
    // leave the anchor a single legal value, so it lands there whichever arm
    // fired. Both regimes were measured: at 90px the overlap yields a zero
    // step in both arms, and at 60px the anchor is identical for a pure
    // left-band cursor and a pure right-band one. The arm stays because it
    // costs nothing and the alternative is a wrong-direction scroll on a
    // sliver of an output, not because anything pins it.
    const int leftEdge = params.workArea.left() + kDragScrollBandPx;
    const int rightEdge = params.workArea.right() - kDragScrollBandPx;
    const bool inLeftBand = cursorPos.x() <= leftEdge;
    const bool inRightBand = cursorPos.x() >= rightEdge;
    int step = 0;
    if (inLeftBand
        && (!inRightBand || cursorPos.x() - params.workArea.left() <= params.workArea.right() - cursorPos.x())) {
        const double depth = std::clamp((leftEdge - cursorPos.x()) / double(kDragScrollBandPx), 0.0, 1.0);
        step = -static_cast<int>(std::lround(depth * depth * kDragScrollMaxStepPx));
    } else if (inRightBand) {
        const double depth = std::clamp((cursorPos.x() - rightEdge) / double(kDragScrollBandPx), 0.0, 1.0);
        step = static_cast<int>(std::lround(depth * depth * kDragScrollMaxStepPx));
    } else {
        return false;
    }
    if (step == 0) {
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
    // The anchor is persisted state, and applyLayout's own anchorMoved gate
    // cannot see this shift (updateViewForFocus is skipped while the preview
    // steers the view) — without this emit an edge-scroll followed by Escape
    // loses the anchor across a restart.
    Q_EMIT placementChanged(m_dragInsertPreview->targetScreenId);
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
        return;
    }
    // A NEIGHBOUR in the target strip is going away: the remembered target
    // indexes were hit-tested against a structure that is about to change,
    // and a stationary cursor never re-aims. Discard the stale aim — commit
    // then takes the restore-slot / append fallback instead of silently
    // landing at a shifted index, and the next motion or scroll tick
    // re-resolves a fresh target.
    //
    // With the cursor held still and outside the edge-scroll bands, neither
    // of those ticks fires, so the indicator stays dark until the user moves
    // again. That is deliberate: after a neighbour vanishes there is no
    // honest target to paint, and painting the OLD rect would promise a slot
    // that no longer exists. A dark indicator says "aim again", which is what
    // the fallback at commit will otherwise decide for the user.
    const auto it = m_states.windowKeys().constFind(windowId);
    if (it != m_states.windowKeys().constEnd() && it.value() == m_dragInsertPreview->targetKey) {
        m_dragInsertPreview->lastTarget = DragInsertTarget{};
    }
}

} // namespace PhosphorScrollEngine
