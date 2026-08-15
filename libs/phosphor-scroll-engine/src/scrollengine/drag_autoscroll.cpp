// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Edge auto-scroll for a live drag-insert preview — niri's
// dnd-edge-view-scroll. Hold a dragged window near either edge of the work
// area and the strip's VIEW scrolls, so a drop can reach a column that is
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

#include <PhosphorScreens/ScreenIdentity.h>

#include <algorithm>
#include <cmath>

namespace PhosphorScrollEngine {

namespace {

/// Ceiling on the elapsed time a single tick may integrate. The daemon's
/// timer is nominally ~60 Hz, but a stalled event loop (a heavy relayout, a
/// suspend/resume) can deliver one tick carrying whole seconds — and speed
/// times that lurches the strip to an end in a single frame, which reads as
/// a teleport rather than a scroll. Two frames' worth of slack is enough to
/// stay smooth under ordinary jitter.
constexpr qreal kMaxTickSeconds = 0.05;

} // namespace

bool ScrollEngine::dragAutoScrollActive() const
{
    return m_dragInsertPreview && m_dragInsertPreview->autoScrollOwnsTarget;
}

bool ScrollEngine::dragAutoScrollTick(const QString& screenId, const QPoint& cursorPos, qreal dtSeconds)
{
    if (!m_dragScrollEnabled || !m_dragInsertPreview) {
        return false;
    }
    // The preview's screen, spelled the preview's way: screensMatch accepts
    // a virtual/physical spelling difference, and everything below resolves
    // layout params per screen id, so the caller's spelling could scroll a
    // work area the commit path never uses. Same rule as the hit-test's.
    if (!PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, screenId)) {
        return false;
    }
    DragInsertPreview& preview = *m_dragInsertPreview;

    const auto disarm = [&preview]() {
        preview.autoScrollDirection = 0;
        preview.autoScrollResidue = 0.0;
        preview.autoScrollOwnsTarget = false;
        return false;
    };

    ScrollState* state = stateForKey(preview.targetKey, false);
    if (!state || state->strip().isEmpty()) {
        return disarm();
    }
    const ScrollLayoutParams params = layoutParamsForScreen(preview.targetScreenId);
    if (!params.workArea.isValid()) {
        return disarm();
    }
    const ResolvedStrip resolved = state->strip().relayout(params);
    if (resolved.stripWidth <= params.workArea.width()) {
        // The whole strip already fits — there is nothing off screen to
        // reveal, so the bands stay inert and the ordinary hit-test keeps
        // the target. Disarming (rather than returning false while armed)
        // matters: otherwise a strip that shrank to fit mid-drag would hold
        // the target locked to an edge slot with no motion to justify it.
        return disarm();
    }

    // Linear ramp, niri's: zero at the band's inner edge, full speed at the
    // work area's edge. The trigger width is clamped to a third of the work
    // area so a hand-edited config cannot make the bands meet in the middle
    // and leave no neutral zone to aim from.
    const int triggerWidth = std::clamp(m_dragScrollTriggerWidth, 1, qMax(1, params.workArea.width() / 3));
    const int leftInner = params.workArea.left() + triggerWidth;
    const int rightInner = params.workArea.right() - triggerWidth;

    int direction = 0;
    qreal depth = 0.0;
    if (cursorPos.x() < leftInner) {
        direction = -1;
        depth = qreal(leftInner - cursorPos.x()) / qreal(triggerWidth);
    } else if (cursorPos.x() > rightInner) {
        direction = 1;
        depth = qreal(cursorPos.x() - rightInner) / qreal(triggerWidth);
    } else {
        return disarm();
    }
    // Past the work area's edge (a cursor over an adjacent output, or over
    // a panel) is still full speed rather than more than full speed.
    depth = std::clamp(depth, 0.0, 1.0);

    if (preview.autoScrollDirection != direction) {
        // Entering a band, or crossing straight from one to the other on a
        // very narrow screen. Either way the delay restarts: the direction
        // the strip is about to move in is a fresh intent.
        preview.autoScrollDirection = direction;
        preview.autoScrollResidue = 0.0;
        preview.autoScrollOwnsTarget = false;
        preview.autoScrollArmed.start();
        return false;
    }
    if (!preview.autoScrollOwnsTarget) {
        if (preview.autoScrollArmed.isValid() && preview.autoScrollArmed.elapsed() < m_dragScrollDelayMs) {
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
    const qreal dt = std::clamp(dtSeconds, 0.0, kMaxTickSeconds);
    preview.autoScrollResidue += qreal(direction) * depth * qreal(m_dragScrollMaxSpeed) * dt;
    const int dx = int(preview.autoScrollResidue > 0.0 ? std::floor(preview.autoScrollResidue)
                                                       : std::ceil(preview.autoScrollResidue));
    preview.autoScrollResidue -= qreal(dx);

    bool moved = false;
    if (dx != 0) {
        moved = state->strip().scrollViewBy(dx, params);
        if (!moved) {
            // Pinned at an end. Drop the residue so a long hold there does
            // not bank up travel that releases as a lurch the moment the
            // cursor crosses to the other band. A tick whose dx merely
            // ROUNDED to zero must keep its remainder — that is the whole
            // point of carrying one.
            preview.autoScrollResidue = 0.0;
        }
    }
    // The target is rewritten even on a tick that did not move — at a strip
    // end the view is pinned but the cursor is still asking to insert past
    // that edge, and that is exactly the append/leading slot.
    const bool targetChanged = writeDragAutoScrollTarget(*state, params, direction);

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

bool ScrollEngine::writeDragAutoScrollTarget(const ScrollState& state, const ScrollLayoutParams& params, int direction)
{
    // Resolved again rather than handed down from the tick: the caller's copy
    // predates this tick's scroll, and which column is first or last VISIBLE
    // is exactly what the scroll just changed.
    const ResolvedStrip resolved = state.strip().relayout(params);
    const ResolvedColumn* firstVisible = nullptr;
    const ResolvedColumn* lastVisible = nullptr;
    for (const ResolvedColumn& column : resolved.columns) {
        if (!column.rect.intersects(params.workArea)) {
            continue;
        }
        if (!firstVisible) {
            firstVisible = &column;
        }
        lastVisible = &column;
    }
    if (!firstVisible) {
        return false;
    }

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
