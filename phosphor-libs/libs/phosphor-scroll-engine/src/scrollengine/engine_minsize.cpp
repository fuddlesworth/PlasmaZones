// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>

#include "enginelimits.h"
#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

namespace {

/// How far an edge of a resized frame may sit from where the gesture began and
/// still count as not moved (onWindowResized's one-edge test). On a fractional
/// scale the effect rounds the frame to whole pixels edge by edge, so the edge
/// a one-edge drag never touched can read a pixel off at the end of it.
constexpr int kFrameEdgeRoundingPx = 1;

} // namespace

// Minimum-size bookkeeping and the user's resize it feeds: the clamp a window
// reports, the re-layout its change forces, and the resize echo that has to
// tell an effect-applied rect apart from a user drag. Split from
// engine_lifecycle.cpp on the concern seam its earlier splits established —
// these answer "how big may this window be" rather than "is it here yet", and
// the open/close/focus arrivals it left behind never call into it except
// through the engine's own retile scheduling. The size a client settled on
// when offered its tile (reportCommittedSize) lives in engine_committed.cpp.

QSize ScrollEngine::windowMinimumSize(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (const ScrollState* state = stateForWindow(windowId)) {
        if (state->strip().containsWindow(windowId)) {
            // Verbatim, including one-axis clamps like 900x0: the strip
            // answers (0, 0) only for a window it does not hold.
            return state->strip().windowMinimumSize(windowId);
        }
    }
    // A floated (or, via the effect's minimize-as-float model, minimized)
    // window is not a strip tile, but its clamp is not unknown — the
    // FloatRestore entry carries it. The cross-engine handoff queries this
    // whatever state the window is in, and answering 0x0 hands the receiving
    // engine an unclamped window.
    const auto it = m_floatRestore.constFind(windowId);
    // Unknown window: an INVALID QSize, deliberately, and a divergence from
    // the sibling engines — AutotileEngine answers 0x0 for an unknown window,
    // which is also what an unconstrained known window answers. The two cases
    // are not the same thing here: the handoff asks this whatever state the
    // window is in, and "I have never heard of it" has to be distinguishable
    // from "it reported no minimum", or a receiving engine cannot tell a real
    // 0x0 clamp from a missing answer. Callers that just want a clamp can
    // treat both alike, since an invalid QSize's width/height are -1 and every
    // clamp site takes a qMax against 0.
    return it != m_floatRestore.constEnd() ? QSize(it->minWidth, it->minHeight) : QSize();
}

void ScrollEngine::windowMinSizeUpdated(const QString& rawWindowId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // qMax(0, ...): same negative-floor contract as insertOpenedWindow's
    // boundary clamp and seedFloatRestoreForOpen — a negative floor flows from
    // here into Tile::minWidth/minHeight (directly, or through the
    // FloatRestore entry an unfloat re-applies), and the relayout slack math
    // is not written for one. (No live crash today; every consumer happens to
    // guard, but this is exported LGPL API and the sibling paths all clamp at
    // the boundary.)
    const int clampedMinWidth = qMax(0, minWidth);
    const int clampedMinHeight = qMax(0, minHeight);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    // The minimum this report replaces, read before any write below: the
    // strip tile while the window tiles, the FloatRestore entry while it
    // floats (windowMinimumSize answers both).
    QSize previousMin = windowMinimumSize(windowId);
    // While the window floats there is no tile to write to, and unfloat
    // re-applies the captured clamp — so without this write-through the
    // restore puts back whatever the client reported at float time.
    if (const auto it = m_floatRestore.find(windowId); it != m_floatRestore.end()) {
        it->minWidth = clampedMinWidth;
        it->minHeight = clampedMinHeight;
    }
    // The same write-through for a window a live drag-insert preview holds
    // detached: it is in no strip and has no float slot for the hold, so the
    // writes here would land nowhere, and the commit or the cancel would put
    // the window back with the minimum it had at begin. The preview's copies
    // are what put it back, so the new clamp goes into each of them.
    if (m_dragInsertPreview && m_dragInsertPreview->windowId == windowId) {
        DragInsertPreview& preview = *m_dragInsertPreview;
        if (!previousMin.isValid()) {
            previousMin = QSize(preview.carried.minWidth, preview.carried.minHeight);
        }
        const auto writeMinimum = [clampedMinWidth, clampedMinHeight](FloatRestore& slot) {
            slot.minWidth = clampedMinWidth;
            slot.minHeight = clampedMinHeight;
        };
        writeMinimum(preview.carried);
        writeMinimum(preview.priorSlot);
        writeMinimum(preview.defensiveSlot);
        if (preview.hadFloatRestoreEntry) {
            writeMinimum(preview.floatRestoreEntry);
        }
    }
    // A LOWERED minimum along a strip's main axis is the client saying it can
    // now be narrower there, so what its earlier commits taught on that axis
    // is stale: it may have answered at the old minimum. Dropped, the column
    // is offered as asked, and a client that still will not take it teaches
    // the floor again. A raised minimum takes nothing. The cross floors
    // answer to the minimum across their strip the same way.
    //
    // Whether the drop changed the layout is MEASURED, on the column the
    // window holds in the strip this report acts on and on its tile's share
    // of that column, before and after: a floor in the orientation that strip
    // does not run, a floating window's floor, a floor its column already
    // outgrows, or one read while minimum sizes are off moves nothing, and
    // announcing it would republish and retile a strip that did not change.
    // Measured ahead of the minimum write below, so only the floor's part of
    // the change is counted here.
    const bool mayHoldFloor = state && m_committedExtents.contains(windowId) && state->strip().containsWindow(windowId);
    const ScrollLayoutParams floorParams = mayHoldFloor ? layoutParamsForKey(key) : ScrollLayoutParams{};
    const int extentBeforeDrop = mayHoldFloor ? state->strip().windowColumnMainExtentPx(windowId, floorParams) : -1;
    const int crossBeforeDrop = mayHoldFloor ? state->strip().windowTileCrossExtentPx(windowId, floorParams) : -1;
    dropCommittedForLoweredMinimum(windowId, previousMin, QSize(clampedMinWidth, clampedMinHeight));
    const bool floorDropped = mayHoldFloor
        && (state->strip().windowColumnMainExtentPx(windowId, floorParams) != extentBeforeDrop
            || state->strip().windowTileCrossExtentPx(windowId, floorParams) != crossBeforeDrop);
    if (!state) {
        return;
    }
    // EVERY context the window holds a column in, not just the one in view:
    // the compositor reports a minimum per window and only when it changes, so
    // a desktop whose strip missed this write would keep the clamp it adopted
    // the window with for as long as the window lives.
    bool minChanged = false;
    for (const PhosphorEngine::PlacementStateKey& member : m_states.membershipsForWindow(windowId)) {
        ScrollState* memberState = m_states.stateForKey(member);
        if (memberState && memberState->strip().setWindowMinimumSize(windowId, clampedMinWidth, clampedMinHeight)
            && member == key) {
            minChanged = true;
        }
    }
    // Re-run insertOpenedWindow's oversized verdict on the updated clamp. The
    // open path floats a window whose minimum no column slot can honour, but
    // clients that pin their size hints AFTER mapping (a Wine game maps
    // hintless, then pins min to its configured resolution) used to dodge
    // that verdict on timing alone and stay tiled at a size the strip can
    // never satisfy — exactly the stranded state the refused-ack latch in
    // onWindowResized documents waiting on "something else" to heal.
    // Evaluated whatever minChanged says: an adoption path can seat a tiled
    // window with an already-oversized clamp, and since the compositor
    // re-reports a minimum only when it changes, a report that changed
    // nothing here may still be the only one this state ever gets.
    // floatWindowInternal owns the whole transition (slot memory for the
    // eventual unfloat, float-focus seeding, context-guarded apply,
    // placementChanged), and the strip write above means the FloatRestore it
    // captures carries the new clamp. The reverse transition is deliberately
    // absent — a min that shrinks back below the work area does not
    // auto-unfloat, because the float may have been rearranged meanwhile and
    // the manual unfloat path already restores the remembered slot.
    //
    // Judged in the context in view only (the membership stateForWindow
    // resolves). A window that holds columns on other desktops stays tiled
    // there at the oversized clamp: the unfloat slot is per WINDOW
    // (m_floatRestore), so floating it in several contexts at once would
    // overwrite one context's slot with another's and the unfloat would
    // misplace it. The relayout clamps those columns to the work area until
    // the window is floated or the client lowers its minimum.
    if (floatIfOutgrowsWorkArea(state, key, windowId, "windowMinSizeUpdated:")) {
        return;
    }
    // A dropped floor changes the strip's layout with no verb behind it, so
    // it is republished like reportCommittedSize's own changes.
    if (floorDropped) {
        Q_EMIT placementChanged(key.screenId);
    }
    // Background-context guard, the same one windowClosed and the float paths
    // carry: a scheduled retile resolves the screen's CURRENT context, so a
    // min-size report for a window on another desktop would relayout a strip
    // this change did not touch. The model write still lands; the switch back
    // retiles the mutated strip.
    if ((minChanged || floorDropped) && key == currentKeyForScreen(key.screenId)) {
        scheduleRetileForScreen(key.screenId);
    }
}

void ScrollEngine::onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                                   const QString& screenId)
{
    // key.screenId is authoritative below; a mismatched caller value would
    // retile the wrong strip.
    Q_UNUSED(screenId)
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // A window under a compositor interactive move: its frames are drag
    // motion, not a size the user settled on. Reconciling them pinned the
    // column's width/height intents to transient drag rects, and the
    // refused-ack arm below re-emitted the slot rect against the move —
    // the ~1 Hz mid-drag teleport fight. The daemon clears the mark before
    // the drop settles, and the drop paths re-apply authoritative geometry.
    if (!m_interactiveDragWindow.isEmpty() && windowId == m_interactiveDragWindow) {
        return;
    }
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    // Background-context guard, as windowMinSizeUpdated and the float paths
    // carry: a scheduled retile resolves the screen's CURRENT context, so a
    // resize of a window on another desktop must not drive one. The model
    // reconcile still happens — it is the persisted intent — and the switch
    // back retiles.
    const bool currentContext = key == currentKeyForScreen(key.screenId);
    // Reconcile the column to the size the client/user actually settled on;
    // only the owning column relayouts (a resize never reflows neighbours'
    // widths — they just shift). Width intent is only rewritten when the
    // WIDTH moved over the gesture (its start frame against the frame it
    // ended on) — a vertical-only resize must not pin a Proportion/Preset
    // column to pixels.
    //
    // Derived in ROLE terms against the same params the reconcile decodes the
    // acked size with. Comparing physical width/height here while the
    // reconcile reads main/cross would make each guard protect the intent it
    // was not written for.
    //
    // Measured against the gesture's own START frame (@p oldFrame), which is
    // what the user's drag actually moved from. The last applied rect is the
    // OFFER, and differs from the frame on an axis the user never touched
    // whenever the client did not take its offer (a client committed wider
    // than its column, say): measured against it, a bottom-edge drag read as
    // a main-axis resize and pinned a proportional width to Fixed pixels. The
    // applied rect stands in only for a caller that sends no usable start.
    //
    // With NEITHER there is no baseline to compare against, and treating that
    // as "both changed" pinned BOTH intents to pixels — so a purely vertical
    // resize arriving in the window between an adoption (handoffReceive, the
    // setWindowFloat adoption branch, unfloatWindowInternal) and its scheduled
    // applyLayout converted a Proportion column to Fixed, which is exactly
    // what the mainChanged gate exists to prevent. Reconcile nothing in that
    // case and let the pending relayout establish the baseline. A missing
    // last-applied rect alone is not that case: a start frame measures the
    // gesture just as well, and returning on it discarded the user's next
    // resize after any path that dropped the memory (the refused arm below
    // does, on a frame held at its minimum), which the retile then undid.
    const QRect lastApplied = m_lastAppliedRect.value(windowId);
    const QRect gestureStart = oldFrame.isValid() ? oldFrame : lastApplied;
    if (!gestureStart.isValid()) {
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    const ScrollLayoutParams resizeParams = layoutParamsForKey(key);
    const StripAxis resizeAxis = resizeParams.axis;
    const bool mainChanged = resizeAxis.mainSize(gestureStart) != resizeAxis.mainSize(newFrame);
    const bool crossChanged = resizeAxis.crossSize(gestureStart) != resizeAxis.crossSize(newFrame);
    const ScrollStrip& strip = state->strip();
    const int colIdx = strip.columnOfWindow(windowId);
    // The column's extent along the strip before anything below moves it,
    // for the one-edge drag the view does not centre (the anchor shift after
    // the reconcile).
    const int mainExtentBefore = strip.windowColumnMainExtentPx(windowId, resizeParams);
    // The windows whose committed floors this resize settles, per axis. ALONG
    // the strip, every tile of the column: they share the column's extent,
    // which is held to the largest floor among them (columnMinExtentPx), so a
    // sibling's floor left standing held the column and the release reverted
    // the drag. ACROSS it, the window's own tile in a stack, whose share is
    // its own, and every tab of a TABBED column, which is committed at the one
    // column rect and held to the largest cross floor in the set
    // (tabbedColumnCrossPx).
    QStringList mainSettled{windowId};
    QStringList crossSettled{windowId};
    if (colIdx >= 0) {
        const Column& column = strip.columns().at(colIdx);
        for (const Tile& tile : column.tiles) {
            if (tile.windowId == windowId) {
                continue;
            }
            mainSettled.append(tile.windowId);
            if (column.display == ColumnDisplay::Tabbed) {
                crossSettled.append(tile.windowId);
            }
        }
    }
    // The user's own resize ALONG the strip settles the window's extent there,
    // so whatever its client's commits taught on this axis is superseded:
    // without the drop the floor kept the column wider than the size the user
    // released it at (a narrowing drag reverted, or a game that rescales
    // narrower under a bottom-edge drag left a dead gap). This is the one
    // entry point that counts as a user resize — reconcileWindowSize is
    // reached from nowhere else — and the keyboard and D-Bus width verbs
    // deliberately do not: they write intents a floor can still outrank.
    // Every settled window is dropped: no drop may short-circuit another.
    bool floorDropped = false;
    // Its resize ACROSS the strip settles the tile's share the same way, and
    // drops the cross floor on its own: a drag of both edges drops both, and
    // neither drop short-circuits the other. The height verbs, like the width
    // verbs, write intents a floor can still outrank and drop nothing.
    bool crossFloorDropped = false;
    if (mainChanged) {
        for (const QString& settled : std::as_const(mainSettled)) {
            if (dropCommittedForUserResize(settled, resizeAxis)) {
                floorDropped = true;
            }
        }
    }
    if (crossChanged) {
        for (const QString& settled : std::as_const(crossSettled)) {
            if (dropCommittedCrossForUserResize(settled, resizeAxis)) {
                crossFloorDropped = true;
            }
        }
    }
    // The extents the user settled on. When the relayout this resize triggers
    // will centre the window on an axis, a drag of ONE edge on that axis is
    // read as a drag from the centre: the extent grows, or shrinks, by twice
    // the drag. Once it is centred again the edge the user dragged lands where
    // they let go, and the far edge has moved out (or in) by the same amount.
    // Taken one-sided, the re-centre moved each edge by half the drag, so the
    // dragged edge sprang back from the cursor at release and a drag to the
    // screen edge took several tries to fill the screen. Only during the
    // relayout, not live: KWin owns the frame for the whole interactive resize
    // and recomputes it from the cursor on every motion, so a live mirror of
    // the far edge would fight it. A drag of both edges on an axis, or of
    // none, is taken as it is.
    //
    // An edge counts as moved only past kFrameEdgeRoundingPx: on a fractional
    // scale the frame is rounded edge by edge, so the edge the user did not
    // touch can read a pixel off, and a strict test took a one-edge drag for
    // a drag of both and skipped the doubling. The drag is measured on the
    // edge that moved, so that pixel does not leak into the extent.
    //
    // Bounded to the work area: the relayout clamps a larger extent to it
    // anyway, and the intent is persisted. A shrink that would take the
    // doubled extent below the window's own minimum (or past the centre,
    // where it reaches nothing) is taken one-sided instead: the frame the
    // drag ended at already honours the minimum, and a doubled answer under
    // it would persist an intent the window can never take.
    const auto centredExtent = [](int startLow, int startHigh, int startExtent, int newLow, int newHigh, int newExtent,
                                  int minExtent, int areaExtent) {
        const bool lowMoved = qAbs(newLow - startLow) > kFrameEdgeRoundingPx;
        const bool highMoved = qAbs(newHigh - startHigh) > kFrameEdgeRoundingPx;
        if (lowMoved == highMoved) {
            return newExtent;
        }
        const int grownBy = lowMoved ? startLow - newLow : newHigh - startHigh;
        const int doubled = startExtent + 2 * grownBy;
        if (doubled < qMax(1, minExtent)) {
            return newExtent;
        }
        return qMin(doubled, qMax(1, areaExtent));
    };
    const QSize declaredMin = strip.windowMinimumSize(windowId);
    int settledMain = resizeAxis.mainSize(newFrame);
    int settledCross = resizeAxis.crossSize(newFrame);
    // Along the strip, the focused column is what centres, and only it, and
    // only while the policy owns the view: a detached view (a pan, a drop) is
    // left where it is by the relayout (updateViewForFocus), so nothing would
    // re-centre a doubled extent.
    const bool activeColumn = colIdx >= 0 && colIdx == strip.activeColumnIndex();
    const bool viewCentresColumn = strip.isCenteringActiveColumn(resizeParams) && !strip.viewDetached();
    // Under every other view, the column's leading edge sits at the view
    // anchor and the relayout grows the column from there, so a drag of the
    // LEADING edge alone would land the trailing edge out by the whole drag
    // and leave the dragged edge where it started. The anchor is shifted
    // after the reconcile instead, by however much the column actually grew,
    // so the trailing edge stays put and the dragged one lands at the release.
    bool keepTrailingEdge = false;
    if (mainChanged && activeColumn) {
        if (viewCentresColumn) {
            settledMain = centredExtent(resizeAxis.mainLow(gestureStart), resizeAxis.mainHigh(gestureStart),
                                        resizeAxis.mainSize(gestureStart), resizeAxis.mainLow(newFrame),
                                        resizeAxis.mainHigh(newFrame), settledMain, resizeAxis.mainSize(declaredMin),
                                        resizeAxis.mainSize(resizeParams.workArea));
        } else {
            keepTrailingEdge =
                qAbs(resizeAxis.mainLow(newFrame) - resizeAxis.mainLow(gestureStart)) > kFrameEdgeRoundingPx
                && qAbs(resizeAxis.mainHigh(newFrame) - resizeAxis.mainHigh(gestureStart)) <= kFrameEdgeRoundingPx;
        }
    }
    // Across it, centerShortColumns centres any column whose content comes out
    // short, as one stack. Only a column that shows one rect is read this way:
    // a lone tile, or a tabbed column whose tabs share it. In a stack of
    // several the drag also moves the neighbouring tiles' shares, so no single
    // doubling lands the edge, and the drag is taken as it is.
    if (crossChanged && resizeParams.centerShortColumns && colIdx >= 0) {
        const Column& col = strip.columns().at(colIdx);
        if (col.display == ColumnDisplay::Tabbed || col.visibleTileCount() == 1) {
            settledCross = centredExtent(
                resizeAxis.crossLow(gestureStart), resizeAxis.crossHigh(gestureStart),
                resizeAxis.crossSize(gestureStart), resizeAxis.crossLow(newFrame), resizeAxis.crossHigh(newFrame),
                settledCross, resizeAxis.crossSize(declaredMin), resizeAxis.crossSize(resizeParams.workArea));
        }
    }
    const QSize settledSize = resizeAxis.makeRect(0, 0, settledMain, settledCross).size();
    if (state->strip().reconcileWindowSize(windowId, settledSize, mainChanged, crossChanged, resizeParams)
        || floorDropped || crossFloorDropped) {
        // The trailing-edge hold for a leading-edge drag (keepTrailingEdge):
        // the anchor moves back by what the column grew, measured on the
        // column itself so a floor or the work-area cap that held it to
        // another extent than the drag is counted as it resolves. Persisted
        // with the strip like any anchor, which the emit below covers.
        if (keepTrailingEdge) {
            const int grownBy = state->strip().windowColumnMainExtentPx(windowId, resizeParams) - mainExtentBefore;
            if (grownBy != 0 && mainExtentBefore >= 0) {
                state->strip().restoreViewAnchor(state->strip().viewAnchor() - grownBy, resizeParams);
            }
        }
        // The reconcile WROTE persisted intent (the column's Fixed width, the
        // tile's Fixed height — both serialized by serializeStripState), and
        // placementChanged is this engine's only way to mark DirtyScrollStrips
        // (the tracking service's scheduleSaveState, DirtyAll, also sets that
        // bit). Without this emit a resize that is the session's last strip
        // interaction is never saved and the column comes back at its old
        // width.
        // reconcileWindowSize returns true only on a genuine change, and a
        // dropped floor is one too (the column's extent moves with it), so
        // emit-on-change holds.
        Q_EMIT placementChanged(key.screenId);
        // The window now SITS at newFrame, not at the rect this map still
        // remembers, so the memory is rewritten to where the window truly
        // is. Without this the accepted arm shared the refused arm's stale-
        // memory hole from the other direction: a resize the relayout
        // CLAMPS straight back (a bottom edge dragged into the outer gap, a
        // lone window pulled past the work area) resolved to exactly the
        // rect already remembered, applyLayout's emit-on-change gate read
        // "nothing moved" and stayed silent, and the window was stranded
        // at the overshoot with the gap swallowed. The stale memory was
        // only harmless when the retile happened to shift the rect anyway
        // (a width change recentres, a shrink recentres across), which is
        // why the report read as intermittent.
        m_lastAppliedRect.insert(windowId, newFrame);
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    // The strip REFUSED the size (no-op ack): the window
    // is now displaced from the engine's rect, but m_lastAppliedRect still
    // holds it, so the emit-on-change gate would treat the corrective
    // relayout as "nothing moved" and never re-issue the rect. Drop the
    // memory and retile so the authoritative geometry is re-applied.
    //
    // EXCEPT when the displacement is the window's own minimum floor: the
    // user's drag ended with KWin holding the frame at a frame minimum the
    // slot cannot honour, and the reconcile above refused because the Fixed
    // intent already records that size. Re-applying the slot rect would offer
    // the client the very size it already refused and change nothing anyone
    // can see, so the retile is skipped — but the gate memory is still
    // dropped: the frame genuinely differs from the engine's rect, and keeping
    // the memory would make the eventual healing relayout (a later min-size
    // drop, a focus move, any scheduled retile) read as "nothing moved" and
    // stay silent, stranding the window oversized forever. With the memory
    // gone each such relayout re-emits once, and nothing here schedules one.
    // This path is reached by a user's finished resize only (see the
    // reconcile's note); a client answering its offer with another size is
    // reportCommittedSize's to handle, not this arm's.
    //
    // With no memory at all (a path dropped it, and the gesture's start frame
    // stood in as the baseline) there is nothing to compare: the retile
    // re-applies the authoritative geometry and lays a fresh baseline down.
    if (!lastApplied.isValid()) {
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
        return;
    }
    if (lastApplied != newFrame) {
        // Compared within the fractional-scale rounding the committed sizes
        // are judged with (kCommittedExtentSlackPx): the frame and the applied
        // rect are both rounded to whole pixels, and an exact test read a
        // frame a pixel off its offer, or off its minimum, as a displacement
        // and re-offered the size the client had already refused.
        const auto within = [](int a, int b) {
            return qAbs(a - b) <= kCommittedExtentSlackPx;
        };
        const QSize knownMin = state->strip().windowMinimumSize(windowId);
        const bool pinnedAtMinW = knownMin.width() > 0 && within(newFrame.width(), knownMin.width())
            && newFrame.width() > lastApplied.width();
        const bool pinnedAtMinH = knownMin.height() > 0 && within(newFrame.height(), knownMin.height())
            && newFrame.height() > lastApplied.height();
        const bool widthExplained = within(lastApplied.width(), newFrame.width()) || pinnedAtMinW;
        const bool heightExplained = within(lastApplied.height(), newFrame.height()) || pinnedAtMinH;
        m_lastAppliedRect.remove(windowId);
        if (widthExplained && heightExplained && within(lastApplied.x(), newFrame.x())
            && within(lastApplied.y(), newFrame.y())) {
            return;
        }
        if (currentContext) {
            scheduleRetileForScreen(key.screenId);
        }
    }
}

} // namespace PhosphorScrollEngine
