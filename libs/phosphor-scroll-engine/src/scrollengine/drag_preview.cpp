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

} // namespace

/// Capture @p windowId's current slot in FloatRestore vocabulary — the twin
/// of floatWindowInternal's capture block, minus the container bookkeeping.
FloatRestore ScrollEngine::captureDragSlot(const ScrollStrip& strip, const QString& windowId)
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
    slot.ownedTabbedHeight = column.display == ColumnDisplay::Tabbed && column.heightOwnerId == windowId;
    const QSize minSize = strip.windowMinimumSize(windowId);
    slot.minWidth = minSize.width();
    slot.minHeight = minSize.height();
    const int tileIdx = column.indexOfWindow(windowId);
    if (tileIdx >= 0) {
        slot.height = column.tiles.at(tileIdx).height;
        slot.windowedFullscreen = column.tiles.at(tileIdx).windowedFullscreen;
    }
    if (column.tiles.size() > 1) {
        slot.tileIndex = tileIdx;
        slot.stackAnchor = column.anchorSiblingFor(slot.tileIndex);
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
        inserted = strip.insertWindow(windowId, params.defaultColumnWidth, effectiveDefaultColumnDisplay(screenId),
                                      params, slot.minWidth, slot.minHeight);
    }
    if (inserted) {
        if (slot.minWidth > 0 || slot.minHeight > 0) {
            strip.setWindowMinimumSize(windowId, slot.minWidth, slot.minHeight);
        }
        if (slot.column >= 0 || slot.tileIndex >= 0) {
            strip.setWindowHeightIntent(windowId, slot.height);
            // Same tile-captured gate: an Escape must hand back windowed
            // fullscreen and the tabbed extent ownership exactly as it hands
            // back the height intent. The ownership is not implied by that
            // write — it claims nothing for an Auto intent — so a cancelled
            // drag of the owning tab would otherwise leave the column at the
            // height of whichever tab replaced it.
            if (slot.ownedTabbedHeight) {
                strip.setTabbedHeightOwner(windowId);
            }
            if (slot.windowedFullscreen) {
                strip.setWindowedFullscreen(windowId, true);
            }
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
            // Before removeFloating, which clears the pair when this window
            // holds it — same capture-then-clear as unfloatWindowInternal.
            preview.priorFloatHadFocus = priorState->floatingHasFocus() && priorState->lastFloatingFocus() == windowId;
            priorState->removeFloating(windowId);
            preview.carried = preview.floatRestoreEntry;
            if (!preview.hadFloatRestoreEntry) {
                // A floating window with no restore entry carries no intents
                // of its own — seed the screen's configured defaults, not
                // FloatRestore's default-constructed 50% proportion.
                preview.carried.width = params.defaultColumnWidth;
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
            preview.carried.width = params.defaultColumnWidth;
            preview.carried.display = effectiveDefaultColumnDisplay(screenId);
        }
        // Keep the window tracked against the TARGET context while detached
        // (screen routing, isWindowTracked, the daemon's re-latch all keep
        // answering). Fresh adoption stays untracked until commit.
        m_states.setKeyForWindow(windowId, targetKey);
    } else {
        preview.carried.width = params.defaultColumnWidth;
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
    ScrollStrip& strip = targetState->strip();
    // Params resolved against the POST-drop column count, mirroring
    // dragInsertIndicatorRect's own resolve: while the preview holds the
    // dragged window detached, a strip that will have two columns still
    // reads as one, so live params would run the smart-gaps single-column
    // regime and bake a Fixed default height — persisted intent with no
    // relayout self-heal — against a work area the dropped window never
    // occupies. The join arm keeps the count; every column-creating arm
    // (new slot, empty strip, and the no-target append/restore fallbacks
    // below) adds one, so absent a valid join target the +1 is the better
    // prediction for the defensive arms too.
    const int postDropColumns =
        strip.columnCount() + ((!p.lastTarget.isValid() || p.lastTarget.newSlot || strip.isEmpty()) ? 1 : 0);
    const ScrollLayoutParams params = layoutParamsForScreen(p.targetScreenId, postDropColumns);
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
        // The float capture's exclusivity invariant (ScrollStashTypes.h):
        // every m_floatRestore entry carries this false, and a drag-captured
        // true must not leak in through the degrade arm — a future unfloat
        // that honoured the field would resurrect fullscreen on a float.
        carried.windowedFullscreen = false;
        m_floatRestore.insert(p.windowId, carried);
        // Mode marker: this is a scroll-decided float, same as every other
        // float-producing exit (begin removed the marker on the way in).
        m_scrollFloatedWindows.insert(p.windowId);
        // Same drop floatWindowInternal makes on this transition. The window
        // is leaving the tiled set, so a remembered tile rect can only serve
        // as a stale comparand for the emit-on-change gate; the sibling paths
        // all clear it and this one was the exception. The parked-edge memory
        // dies with it — floatWindowInternal drops it for the same reason.
        m_lastAppliedRect.remove(p.windowId);
        m_parkedScrollEdge.remove(p.windowId);
        // Windowed fullscreen dies with the tile — floatWindowInternal drops
        // this third memory for the same reason, and this arm was the
        // exception (a stale true only forces one redundant emit, but the
        // symmetry is the documented contract).
        m_lastAppliedWindowedFs.remove(p.windowId);
        m_lastAppliedColumnMaximized.remove(p.windowId);
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
        // The drop re-seats the tile, so windowed fullscreen survives the
        // reorder the same way the height intent does. The float-drop arm
        // above never reaches here, which is the exclusivity holding.
        if (p.carried.windowedFullscreen) {
            strip.setWindowedFullscreen(p.windowId, true);
        }
    }
    // The dropped window is the one the user is looking at.
    strip.focusWindow(p.windowId, params);
    m_states.setKeyForWindow(p.windowId, p.targetKey);

    // Drop the last-applied memory so the re-tile emit survives the
    // emit-on-change gate even when the window resolves back to its
    // pre-drag rect (single-column strip: no neighbour ever moves, so this
    // is the ONLY signal that re-tiles the dropped frame). A parked edge
    // recorded before the drag detached the window is stale for the drop
    // slot and would mis-anchor its arrival.
    m_lastAppliedRect.remove(p.windowId);
    m_parkedScrollEdge.remove(p.windowId);
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
                    // Same emit-on-change escape as every other restore arm
                    // in this function: the restored slot is typically the
                    // pre-drag rect, so without dropping the memories the
                    // re-tile emit is suppressed.
                    m_lastAppliedRect.remove(p.windowId);
                    m_parkedScrollEdge.remove(p.windowId);
                    applyLayout(p.targetScreenId, false);
                    // The restore mutates persisted strip structure like
                    // every other restore arm, and applyLayout's own emits
                    // are anchor-conditional — the slot typically resolves
                    // back to its pre-drag rect, so without this the save
                    // scheduler and the strip-selector cards never hear it.
                    Q_EMIT placementChanged(p.targetScreenId);
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
                // begin's removeFloating cleared the focus-memory pair when
                // this window held it; the window is a float again, so put
                // both halves back (unfloatWindowInternal's restore idiom).
                if (p.priorFloatHadFocus) {
                    targetState->setLastFloatingFocus(p.windowId);
                    targetState->setFloatingHasFocus(true);
                }
            }
        } else if (targetState) {
            dragPreviewRestoreSlot(targetState, p.windowId, p.priorSlot, params, p.targetScreenId);
            // Same emit-on-change escape as commit: the restored slot is
            // typically the pre-drag rect, so without dropping the memory
            // the re-tile emit is suppressed. Parked-edge memory goes with
            // it, as on commit.
            m_lastAppliedRect.remove(p.windowId);
            m_parkedScrollEdge.remove(p.windowId);
        }
        applyLayout(p.targetScreenId, false);
        // The detach at begin and this restore both mutate persisted strip
        // structure.
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
            // Post-insert stamps, gated exactly like commit's and
            // dragPreviewRestoreSlot's: for a cross-key TILED drag the
            // carried slot holds the user's height intent and the
            // windowed-fullscreen flag, and the bare insert above seeded
            // the context default height with the flag off. NOT routed
            // through dragPreviewRestoreSlot — its column index names the
            // PRIOR screen's slot and would misplace the window here.
            if (p.carried.column >= 0 || p.carried.tileIndex >= 0) {
                targetState->strip().setWindowHeightIntent(p.windowId, p.carried.height);
                if (p.carried.windowedFullscreen) {
                    targetState->strip().setWindowedFullscreen(p.windowId, true);
                }
            }
            m_states.setKeyForWindow(p.windowId, p.targetKey);
            m_lastAppliedRect.remove(p.windowId);
            m_parkedScrollEdge.remove(p.windowId);
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
        // Same pair restore as the priorSameKey floating arm above.
        if (p.priorFloatHadFocus) {
            priorState->setLastFloatingFocus(p.windowId);
            priorState->setFloatingHasFocus(true);
        }
    } else {
        dragPreviewRestoreSlot(priorState, p.windowId, p.priorSlot, layoutParamsForScreen(p.priorKey.screenId),
                               p.priorKey.screenId);
        m_lastAppliedRect.remove(p.windowId);
        m_parkedScrollEdge.remove(p.windowId);
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
    // hit-test against a work area the commit path never uses. Its sibling
    // (dragInsertIndicatorRect) already uses the preview's.
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

    // Visible columns gathered up front: the band mapping below needs to
    // know whether a column is the first or last visible one, which a
    // single streaming pass cannot answer at the column being tested.
    const QVector<const ResolvedColumn*> visibleColumns = visibleColumnsOf(resolved, params.workArea);
    const ResolvedColumn* lastVisible = visibleColumns.isEmpty() ? nullptr : visibleColumns.constLast();
    // The cursor in ROLE coordinates: the strip's own direction picks the
    // column, and the within-column stack's direction picks the tile slot.
    // Reading x and y directly would ask a vertical strip's columns to be
    // ordered by x, which they are not.
    const int cursorMain = params.axis.mainPos(cursorPos);
    const int cursorCross = params.axis.crossPos(cursorPos);
    for (int vi = 0; vi < visibleColumns.size(); ++vi) {
        const ResolvedColumn& column = *visibleColumns.at(vi);
        const bool isFirstVisible = vi == 0;
        const bool isLastVisible = vi == visibleColumns.size() - 1;
        // Cursor before this visible column's main-axis span: the gap ahead
        // of it (or the strip's visible leading edge) → a new column at its
        // index. From the leading edge that is "insert ahead of everything I
        // can see", rendered as a past-the-edge hint.
        if (cursorMain < params.axis.mainLow(column.rect)) {
            target.primary = column.columnIndex;
            target.newSlot = true;
            target.leadingEdge = isFirstVisible;
            return target;
        }
        if (cursorMain > params.axis.mainHigh(column.rect)) {
            continue;
        }
        // Inside this column's main-axis span: the two end bands open a new
        // column at THIS column's spot (the column steps aside and the
        // indicator covers it), the middle joins it as a tile at the
        // cross-resolved slot. Symmetric by construction: each boundary
        // belongs to exactly one band — the trailing neighbour's leading band
        // — and only the view's two extremes differ, hinting past their
        // screen edge instead (the first visible column's leading band and
        // the last one's trailing band).
        // Floor of 1 so integer division on a degenerate sliver still
        // leaves bands.
        const int band = std::clamp(params.axis.mainSize(column.rect) / kEdgeBandDivisor, 1, kEdgeBandMaxPx);
        if (cursorMain < params.axis.mainLow(column.rect) + band) {
            target.primary = column.columnIndex;
            target.newSlot = true;
            target.leadingEdge = isFirstVisible;
            return target;
        }
        if (cursorMain > params.axis.mainHigh(column.rect) - band) {
            target.primary = isLastVisible ? column.columnIndex + 1 : column.columnIndex;
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
            const int tileCrossMid = params.axis.crossLow(tile.rect) + params.axis.crossSize(tile.rect) / 2;
            if (cursorCross <= tileCrossMid) {
                target.secondary = std::max(0, modelColumn.indexOfWindow(tile.windowId));
                break;
            }
            if (cursorCross <= params.axis.crossHigh(tile.rect)) {
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
    // remembered target.
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
    // relayout, and read back the rect the dropped window resolves to. The
    // layout code becomes the single source of truth for the layout, which is
    // the only way this can stay correct as the strip gains features.
    //
    // The probe stops at RELAYOUT: applyLayout's screen-boundary pass (edge
    // clamp, peek-floor park) runs after it and is not simulated, so a slot
    // at the screen edge promises the unclamped rect while the commit clamps
    // it, and a slot whose remainder falls under the peek floor promises an
    // on-screen rect for a window the commit parks. Deliberate: the
    // indicator marks the SLOT being aimed at, and simulating the boundary
    // pass would need the clamp/park decision factored out of applyLayout —
    // more coupling than an aiming aid justifies.
    //
    // The copy is per call and the strip is a plain value type. That is not
    // free, and the ledger already tracks the per-tick relayout cost of this
    // whole path — but a cheap wrong rect is worth less than an accurate one.
    // The edge auto-scroll heartbeat DOES reach here with a stationary
    // cursor, on every tick the view moved (~60 Hz for the length of an edge
    // hold): the view motion is what moves the rect, so those calls are the
    // accurate-rect work, not waste the old daemon change-gate would have
    // skipped.
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
    // The view the user is looking at, measured on the UN-INSERTED strip but
    // with the SAME params the probe will use. Same params on purpose: the
    // shift below must isolate the insert's view side effect, and measuring
    // the two sides under different gap regimes would fold the smart-gaps
    // difference into it as well.
    const int liveViewOffset = state->strip().relayout(params).viewOffset;

    // Mirror of commit's insert selection, deliberately kept line-for-line
    // comparable with it: if the two ever diverge, the indicator lies.
    const int preInsertColumns = probe.columnCount();
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
    // The layout-affecting post-insert stamps commit applies, under the same
    // gates — both change the resolved geometry, so omitting either would
    // reintroduce a modelling error by the back door. Commit's THIRD stamp
    // (the windowed-fullscreen re-seat) is deliberately absent: the flag is
    // layout-neutral and never moves a resolved rect, so the probe has
    // nothing to mirror for it.
    if (p.carried.minWidth > 0 || p.carried.minHeight > 0) {
        probe.setWindowMinimumSize(p.windowId, p.carried.minWidth, p.carried.minHeight);
    }
    if (p.carried.column >= 0 || p.carried.tileIndex >= 0) {
        probe.setWindowHeightIntent(p.windowId, p.carried.height);
    }

    // The probe does NOT mirror commit's focusWindow re-anchor. It once did
    // (to keep an indicator visible for the outer slots of a FULL viewport,
    // where the raw slot resolves outside the work area and the overlay clips
    // it), but the live-view translation below cancels a probe-side re-anchor
    // by construction: shiftToLiveView subtracts the probe's view to pin the
    // rectangle to what is on screen RIGHT NOW. The full-viewport case is
    // covered instead by the niri-parity visibility clamp at the return —
    // see the comment there.
    const ResolvedStrip resolved = probe.relayout(params);
    // Translate the slot back into the LIVE view.
    //
    // The probe's inserts carry FOCUS side effects that production wants and a
    // read-only preview must not inherit: insertWindowIntoColumnAt makes the
    // joined column active and re-anchors onto it, and commit additionally
    // focuses the dropped window. Left in, they pin the indicator to a
    // post-drop viewport rather than the one the user is looking at, which
    // is precisely what a drop indicator must not do.
    //
    // Both terms are needed and neither substitutes for the other: the STRIP
    // position must be POST-insert, because that is the slot being previewed,
    // while the VIEW must be PRE-insert, because that is what is on screen
    // right now.
    const int shiftToLiveView = resolved.viewOffset - liveViewOffset;
    for (const ResolvedColumn& column : resolved.columns) {
        for (const ResolvedTile& tile : column.tiles) {
            if (tile.windowId == p.windowId) {
                // Along the strip: the view only ever slides that way.
                QRect rect = params.axis.translatedMain(tile.rect, shiftToLiveView);
                // A LEADING-EDGE aim ("insert ahead of everything I can see",
                // tagged by the hit-test) mirrors the after-the-last one:
                // its raw promise is the current position of the first
                // visible column, so it would cover that column at full
                // size. Place it just OUTSIDE that column instead (niri
                // positions its leading insert hint the same way): with
                // dead space the promise fills it, and on a flush edge it
                // crosses the screen edge and reaches the half-in clamp
                // below — the mirror of the trailing edge. The SAME slot
                // aimed from the first visible column's INNER band carries
                // no tag and keeps the full rect over that column, exactly
                // as the right neighbour's inner band covers it.
                if (target.newSlot && target.leadingEdge && preInsertColumns > 0) {
                    rect = params.axis.translatedMain(rect, -(params.axis.mainSize(rect) + params.gap));
                }
                // niri-parity visibility clamp, new-column slots only (niri
                // gates its identical clamp on InsertPosition::NewColumn, and
                // a join target's column is on screen by construction — it
                // was hit-tested under the cursor). Reachable when a window
                // that detached NOTHING from this strip (a cross-screen or
                // floating drag) aims past the last column of a FULL
                // viewport: the slot resolves outside the work area and the
                // per-screen overlay clips the unclamped rect away, leaving
                // the one drop that most needs feedback with none. (A strip
                // window's own drag cannot reach this — detach-once frees
                // its column's width and the outer slot resolves into that
                // dead space.) Clamp the x so at least HALF the rect stays
                // visible: the half-in band hugging the screen edge is the
                // standard "insert past this edge" affordance, it never lies
                // about the direction, and slots already on screen are
                // untouched (the bounds are no-ops for them).
                if (target.newSlot) {
                    const int mainSize = params.axis.mainSize(rect);
                    const int areaLow = params.axis.mainLow(params.workArea);
                    const int minLow = areaLow - mainSize / 2;
                    const int maxLow = areaLow + params.axis.mainSize(params.workArea) - mainSize / 2;
                    params.axis.moveMain(rect, qBound(minLow, params.axis.mainLow(rect), maxLow));
                }
                return rect;
            }
        }
    }
    // Resolved away (every column zero-width on a degenerate work area, or the
    // tile hidden behind an active tab). Nothing truthful to paint.
    return {};
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
    // landing at a shifted index, and the next cursor motion re-resolves a
    // fresh target.
    //
    // The heartbeat still ticks with the cursor held still. With the cursor
    // OUTSIDE both bands it cannot relight the cleared target: the cancel
    // below drops ownership, so the next tick's disarmAndReaim
    // short-circuits on `owned == false` and leaves lastTarget alone — the
    // indicator stays dark until genuine cursor motion re-resolves, which is
    // deliberate: after a neighbour vanishes there is no honest target to
    // paint, and painting the OLD rect would promise a slot that no longer
    // exists. With the cursor parked IN a band the disarm buys a fresh start
    // delay instead — the direction-change branch re-arms and, once the
    // delay is served, re-writes the edge slot against the POST-close strip,
    // which is an honest target again. A momentary dark indicator plus a
    // deliberate re-light is the most this layer can promise.
    const auto it = m_states.windowKeys().constFind(windowId);
    if (it != m_states.windowKeys().constEnd() && it.value() == m_dragInsertPreview->targetKey) {
        m_dragInsertPreview->lastTarget = DragInsertTarget{};
        // Give the edge auto-scroll's ownership back along with the target it
        // was writing. Clearing lastTarget alone would not hold at all: the
        // very next heartbeat re-writes the edge slot and re-lights the
        // indicator within a frame. Disarming does not make the promise above
        // literally true either — a cursor still in the band re-arms and
        // re-lights after a fresh start delay — but it turns an immediate
        // relight into a deliberate one, and it stops the ownership latch
        // outliving the target it was justifying.
        cancelDragAutoScroll();
    }
}

} // namespace PhosphorScrollEngine
