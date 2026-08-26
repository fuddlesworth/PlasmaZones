// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Edge auto-scroll for a live drag-insert preview — niri's
// dnd-edge-view-scroll. Hold a dragged window near either END of the strip (left/right on a
// horizontal strip, top/bottom on a vertical one) and the strip's VIEW scrolls, so a drop can reach a column that is
// off screen. Without it the drag can only target what was already visible
// when the hold began.
//
// This lives beside drag_preview.cpp's DETACH-ONCE design rather than
// against it. That design's invariant is that the ANSWER holds still under a
// stationary cursor: the strip's structure never changes mid-hold, so the
// hit-test resolves the same target tick after tick. Moving the view is the
// one mid-drag motion that keeps the invariant, but only if it stops the
// hit-test from re-resolving — which is the second half of this file.
//
// A first implementation of this feature was reverted (33a7c1f2b) for
// exactly that second half. It scrolled the view and kept hit-testing, so
// columns sliding under the parked cursor re-answered on every boundary that
// passed: the drop indicator flipped between a full-height new column and a
// half-height join, over and over. The cause is a band mismatch — the scroll
// band is measured from the SCREEN edge while a column's own new-column band
// is a quarter of ITS width, so a narrow column arriving at the edge puts a
// cursor that never moved into that column's join middle.
//
// So while this scrolls, it OWNS the target: the view's leading (or
// trailing) new-column slot, rewritten every tick, never re-hit-tested. The
// indicator pins at the screen edge and the columns slide past a stationary
// promise. Ordinary per-column targeting resumes the moment the cursor
// leaves the band, against the columns the scroll revealed.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/StripAxis.h>

#include <PhosphorScreens/ScreenIdentity.h>

#include "scrollenginelogging.h"

#include <algorithm>
#include <cmath>

namespace PhosphorScrollEngine {

namespace {

/// Ceiling on the elapsed time a single tick may integrate. The daemon's
/// timer is nominally ~60 Hz, but a stalled event loop (a heavy relayout, a
/// suspend/resume) can deliver one tick carrying whole seconds — and speed
/// times that lurches the strip to an end in a single frame, which reads as
/// a teleport rather than a scroll. Three frames' worth of slack is enough
/// to stay smooth under ordinary jitter.
constexpr qreal kMaxTickSeconds = 0.05;

} // namespace

bool ScrollEngine::dragAutoScrollActive() const
{
    return m_dragInsertPreview && m_dragInsertPreview->autoScrollOwnsTarget;
}

void ScrollEngine::cancelDragAutoScroll()
{
    if (!m_dragInsertPreview) {
        return;
    }
    // The ONE disarm. Clearing autoScrollDirection is what makes the delay
    // restart on the next band entry, clearing autoScrollOwnsTarget is what
    // hands the drop target back to the caller's ordinary hit-test, and the
    // timer goes with the direction because it is only meaningful while one
    // is armed. Every exit that leaves a live preview comes through here:
    // returning while still owning the target freezes the drop at the edge
    // slot for the rest of the drag, with no motion to justify it.
    m_dragInsertPreview->autoScrollDirection = 0;
    m_dragInsertPreview->autoScrollResidue = 0.0;
    m_dragInsertPreview->autoScrollOwnsTarget = false;
    m_dragInsertPreview->autoScrollArmed.invalidate();
}

bool ScrollEngine::repairDragAutoScrollTarget(const QPoint& cursorPos)
{
    // Ordinary per-column targeting is resuming, and resuming it means
    // REPAIRING the target rather than just releasing the latch: while the
    // scroll owned the target it wrote an edge slot on every tick, and
    // nothing else rewrites lastTarget. The caller only re-hit-tests on
    // cursor MOTION, so a cursor that stops asking for a scroll and then
    // parks would otherwise keep the stale edge slot as both the painted
    // indicator and the thing a release commits — a drop at the far end of
    // the strip instead of where the user pointed.
    //
    // PRECONDITION (asserted, with a release-build refusal): every in-tree
    // caller holds a live preview, but a null deref here is a session-killing
    // compositor crash, and the pair is a natural target for a future caller.
    Q_ASSERT(m_dragInsertPreview);
    if (!m_dragInsertPreview) {
        return false;
    }
    const DragInsertTarget repaired = computeDragInsertTargetAtPoint(m_dragInsertPreview->targetScreenId, cursorPos);
    if (!repaired.isValid() || m_dragInsertPreview->lastTarget == repaired) {
        return false;
    }
    m_dragInsertPreview->lastTarget = repaired;
    return true;
}

bool ScrollEngine::dragAutoScrollTick(const QString& screenId, const QPoint& cursorPos, qreal dtSeconds)
{
    if (!m_dragInsertPreview) {
        // No preview means no state to clear and no ownership to give back:
        // dragAutoScrollActive() already answers false without a preview.
        return false;
    }
    DragInsertPreview& preview = *m_dragInsertPreview;

    // Disarm with no geometry to re-aim against: gives the target back but
    // leaves lastTarget alone, for the exits where a hit-test is impossible
    // or meaningless (a foreign screen, a vanished state, a dead work area).
    const auto disarm = [this]() {
        cancelDragAutoScroll();
        return false;
    };
    // Disarm AND repair, for the exits that happen with a usable cursor on
    // the preview's own screen. Returns true when the stored target actually
    // changed, which the caller reads as "repaint the indicator" — the rect
    // is moving off the edge and back under the cursor even though the view
    // did not scroll. Only repairs when ownership was actually held, so a
    // tick that never owned the target costs nothing.
    const auto disarmAndReaim = [this, &preview, &cursorPos]() {
        const bool owned = preview.autoScrollOwnsTarget;
        cancelDragAutoScroll();
        return owned && repairDragAutoScrollTarget(cursorPos);
    };

    // The preview's screen, spelled the preview's way: screensMatch accepts
    // a virtual/physical spelling difference, and everything below resolves
    // layout params per screen id, so the caller's spelling could scroll a
    // work area the commit path never uses. Same rule as the hit-test's.
    //
    // Disarms rather than returning bare: dragAutoScrollActive() carries no
    // screen, so a mismatched tick that kept ownership would block the
    // hit-test on every screen. The heartbeat caller passes the preview's own
    // id and cannot trip this; the settle caller passes the RELEASE screen's
    // id precisely so a cross-screen release lands here and takes the bare
    // disarm instead of re-aiming a foreign cursor. It is also a public
    // IPlacementEngine virtual, so an arbitrary caller must fail closed.
    //
    // FIRST, ahead of every exit that re-aims: cursorPos belongs to the
    // caller's screen, so hit-testing it against the preview's work area
    // would answer from the wrong geometry — plausibly rather than invalidly,
    // which is worse.
    if (!PhosphorScreens::ScreenIdentity::screensMatch(preview.targetScreenId, screenId)) {
        return disarm();
    }
    if (!m_dragScrollEnabled) {
        // Turning the setting off mid-hold has to give the target back, not
        // merely stop scrolling. refreshConfigFromSettings runs on any
        // settings save, so this is reachable during a drag.
        return disarmAndReaim();
    }

    ScrollState* state = stateForKey(preview.targetKey, false);
    if (!state || state->strip().isEmpty()) {
        return disarm();
    }
    if (currentKeyForScreen(preview.targetScreenId) != preview.targetKey) {
        // Same contract as commitDragInsertPreview's guard: the daemon's
        // context-change handlers all cancel a live preview before touching
        // desktop/activity state, so a mismatch here means one of them
        // failed. Scrolling the captured (now background) strip while the
        // batch and persist mark describe the foreground one would be
        // silent; say so and give the target back instead. Disarm, not
        // re-aim — the hit-test would answer from the wrong context too.
        // Warned ONCE per preview: the heartbeat keeps ticking and the
        // mismatch persists for the rest of the drag, so an ungated warning
        // repeats per 16 ms tick.
        if (!preview.contextMoveWarned) {
            preview.contextMoveWarned = true;
            qCWarning(lcScrollEngine) << "dragAutoScrollTick: context moved under the preview for" << preview.windowId
                                      << "on" << preview.targetScreenId
                                      << "— a context-change handler failed to cancel first; disarming";
        }
        return disarm();
    }
    const ScrollLayoutParams params = layoutParamsForScreen(preview.targetScreenId);
    if (!params.workArea.isValid()) {
        return disarm();
    }

    // Linear ramp, niri's: zero at the band's inner edge, full speed at the
    // work area's edge. Measured ALONG THE STRIP: the bands sit at the two
    // ends of the main axis (left/right on a horizontal strip, top/bottom on
    // a vertical one), and the cursor's main-axis coordinate is what enters
    // them. The trigger width is clamped to a third of the work area's main
    // extent so a hand-edited config cannot make the bands meet in the middle
    // and leave no neutral zone to aim from.
    const StripAxis axis = params.axis;
    const int mainExtent = axis.mainSize(params.workArea);
    const int triggerWidth = std::clamp(m_dragScrollTriggerWidth, 1, qMax(1, mainExtent / 3));
    const int leadInner = axis.mainLow(params.workArea) + triggerWidth;
    const int trailInner = axis.mainHigh(params.workArea) - triggerWidth;
    const int cursorMain = axis.mainPos(cursorPos);

    int direction = 0;
    qreal depth = 0.0;
    if (cursorMain < leadInner) {
        direction = -1;
        depth = qreal(leadInner - cursorMain) / qreal(triggerWidth);
    } else if (cursorMain > trailInner) {
        direction = 1;
        depth = qreal(cursorMain - trailInner) / qreal(triggerWidth);
    } else {
        // Out of both bands: ordinary per-column targeting resumes, aimed at
        // the columns the scroll brought into view. Tested BEFORE the
        // settled-strip walk below: this is the overwhelmingly common tick
        // (the whole hold, minus the moments actually spent in a band), and
        // the four disarm cells the two tests share resolve identically in
        // either order, so the strip walk is paid only in-band.
        return disarmAndReaim();
    }
    // Past the work area's edge (a cursor over an adjacent output, or over
    // a panel) is still full speed rather than more than full speed.
    depth = std::clamp(depth, 0.0, 1.0);

    if (state->strip().stripSettledInViewport(params)) {
        // Nothing to reveal: the strip fits AND the view is settled inside
        // its range, so the bands stay inert and the ordinary hit-test keeps
        // the target. Disarming (rather than returning false while armed)
        // matters: otherwise a strip that shrank to fit mid-drag would hold
        // the target locked to an edge slot with no motion to justify it.
        //
        // SETTLED, not merely FITS: the centering mutators deliberately park
        // out-of-range anchors (a fitting strip can still have a column
        // hanging off one edge), and the clamped-delta scroll is the one
        // motion that walks such a view back — a fits-only disarm refused
        // exactly that motion.
        //
        // Asked as a predicate rather than off a relayout, and only on
        // in-band ticks (the band test above returns first): relayout()
        // allocates per column, and this runs every frame of a band hold.
        //
        // Re-aims as well as disarming: a strip that shrank to fit (a panel
        // auto-hiding, a gap change on a settings save) leaves the same stale
        // edge slot behind as leaving the band does.
        return disarmAndReaim();
    }

    if (preview.autoScrollDirection != direction) {
        // Entering a band, or crossing straight from one to the other on a
        // very narrow screen. Either way the delay restarts: the direction
        // the strip is about to move in is a fresh intent.
        preview.autoScrollDirection = direction;
        preview.autoScrollResidue = 0.0;
        preview.autoScrollOwnsTarget = false;
        preview.autoScrollArmed.start();
        // Falls through to the delay test rather than returning: a delay of
        // zero means the scroll starts on THIS tick, not one frame later.
    }
    if (!preview.autoScrollOwnsTarget) {
        // Fail CLOSED on an invalid timer. Unreachable as the code stands —
        // every route here either just called start() on the branch above or
        // arrived with a matching non-zero direction, which only a tick that
        // called start() can set — but it is the safe reading if that ever
        // stops holding: an invalid timer means no delay was ever served, so
        // it cannot have elapsed.
        if (!preview.autoScrollArmed.isValid() || preview.autoScrollArmed.elapsed() < m_dragScrollDelayMs) {
            // Armed but still inside the start delay: a drag that merely
            // passes near an edge on its way somewhere else must not scroll.
            return false;
        }
        preview.autoScrollOwnsTarget = true;
    }

    // Integrate. The residue carries the sub-pixel remainder between ticks
    // because the view anchor is integer pixels — without it any speed under
    // one pixel per tick (the inner half of the band, at 60 Hz) would round
    // to zero on every tick and the band's shallow end would be dead.
    // isfinite first: std::clamp passes NaN through (both comparisons are
    // false), and a NaN residue never recovers — floor/ceil of it is UB and
    // `residue -= step` keeps it NaN, silently deadening the scroll for the
    // rest of the drag. Unreachable from the in-tree callers (both compute
    // dt from a monotonic clock or pass 0.0), but this is the same public
    // IPlacementEngine virtual the ceiling above already defends.
    const qreal dt = std::isfinite(dtSeconds) ? std::clamp(dtSeconds, 0.0, kMaxTickSeconds) : 0.0;
    preview.autoScrollResidue += qreal(direction) * depth * qreal(m_dragScrollMaxSpeed) * dt;
    const int step = int(preview.autoScrollResidue > 0.0 ? std::floor(preview.autoScrollResidue)
                                                         : std::ceil(preview.autoScrollResidue));
    preview.autoScrollResidue -= qreal(step);

    bool moved = false;
    if (step != 0) {
        moved = state->strip().scrollViewBy(step, params);
        if (!moved) {
            // Pinned at an end. Drop the residue so a long hold there does
            // not bank up travel that releases as a lurch the moment the
            // cursor crosses to the other band. A tick whose step merely
            // ROUNDED to zero must keep its remainder — that is the whole
            // point of carrying one.
            preview.autoScrollResidue = 0.0;
        }
    }
    // The target is rewritten even on a tick that did not move — at a strip
    // end the view is pinned but the cursor is still asking to insert past
    // that edge, and that is exactly the append/leading slot.
    const bool targetChanged = writeDragAutoScrollTarget(*state, params, direction, cursorPos);

    if (!moved) {
        return targetChanged;
    }
    // By VALUE, before applyLayout: the preview reference must not be read
    // across a call that can reach the engine's own prune paths.
    const QString targetScreenId = preview.targetScreenId;
    applyLayout(targetScreenId, false);
    // applyLayout's own anchorMoved gate cannot see this: updateViewForFocus
    // is skipped for the whole hold (dragPreviewSteersView), so the anchor it
    // compares never changes there. Without an explicit emit, scrolling and
    // then cancelling the drag loses the scrolled view across a restart.
    Q_EMIT placementChanged(targetScreenId);
    return true;
}

bool ScrollEngine::writeDragAutoScrollTarget(const ScrollState& state, const ScrollLayoutParams& params, int direction,
                                             const QPoint& cursorPos)
{
    Q_ASSERT(m_dragInsertPreview);
    if (!m_dragInsertPreview) {
        return false;
    }
    // Resolved again rather than handed down from the tick: the caller's copy
    // predates this tick's scroll, and which column is first or last VISIBLE
    // is exactly what the scroll just changed.
    const ResolvedStrip resolved = state.strip().relayout(params);
    // Shared with computeDragInsertTargetAtPoint's walk so the owned target
    // and the hit-test can never disagree about which columns are visible.
    const QVector<const ResolvedColumn*> visible = visibleColumnsOf(resolved, params.workArea);
    if (visible.isEmpty()) {
        // Nothing on screen to anchor a promise to. Give the target back
        // AND re-aim like every other ownership-ending exit — a bare cancel
        // would leave the stale edge slot standing for a release to commit.
        // KNOWN residual of this arm: ownership is cleared before this
        // tick's own applyLayout, so the batch the tick already produced is
        // emitted without the viewImmediate stamp (one animated frame); the
        // re-aim does not change that. The settled-strip disarm makes this
        // unreachable today; it is a latch that must not exist, and if it
        // ever fires it must behave like the others.
        cancelDragAutoScroll();
        return repairDragAutoScrollTarget(cursorPos);
    }
    const ResolvedColumn* firstVisible = visible.constFirst();
    const ResolvedColumn* lastVisible = visible.constLast();

    // The two slots the view's extremes already have names for, in
    // computeDragInsertTargetAtPoint's own vocabulary: a new column at the
    // first visible column's index tagged as a past-the-edge hint, or a new
    // column after the last visible one. Nothing downstream learns a new
    // case — commit, the indicator's half-in clamp and the leadingEdge
    // presentation rule all already handle these two.
    DragInsertTarget target;
    target.newSlot = true;
    if (direction < 0) {
        target.primary = firstVisible->columnIndex;
        target.leadingEdge = true;
    } else {
        target.primary = lastVisible->columnIndex + 1;
    }
    if (m_dragInsertPreview->lastTarget == target) {
        return false;
    }
    m_dragInsertPreview->lastTarget = target;
    return true;
}

} // namespace PhosphorScrollEngine
