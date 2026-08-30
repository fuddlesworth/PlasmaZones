// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scrolling-strip clip predicate, shared by the paint cull
// (paint_pipeline.cpp) and the overhang input filter (input_filter.cpp).
// Split out of paint_pipeline.cpp by concern: this pair answers WHICH
// windows are strip members and where they are confined, while the paint
// pipeline consumes the answer.

#include "plasmazoneseffect.h"

// The lcStripDiag category for the stage 0 seam trace below. Included
// explicitly rather than leaned on from a unity batch, same rule as the
// windowanimator.h include below it.
#include "compositor/effectlogging.h"
#include "compositor/stripviewanimator.h"
// plasmazoneseffect.h only forward-declares WindowAnimator; currentValue() below
// needs the complete type, and unity batching must not be what supplies it.
#include "compositor/windowanimator.h"
#include "handlers/navigationhandler.h"
#include "tilinghandler/tilinghandler.h"

#include <core/output.h>

namespace PlasmaZones {

KWin::LogicalOutput* PlasmaZonesEffect::scrollManagedOutputFor(KWin::EffectWindow* w) const
{
    // Same predicate as scrollClipGeometryFor, stopping one step earlier at the
    // output itself. The paint path wants the output (to compare by identity
    // against the pass being rendered); the input filter genuinely wants a
    // rect, so it keeps the rect-returning wrapper below. Both must stay in
    // step — a window suppressed from a foreign output's paint must also be
    // non-interactive there — so the wrapper is defined in terms of this.
    //
    // No null guard on m_tilingHandler (nor on m_navigationHandler below, nor
    // on m_stripViewAnimator in scrollParkedOffscreen): all three are
    // constructed in the effect's ctor init list and never reset, and every
    // caller of this predicate is a paint-path or input-filter site that only
    // runs long after construction. A guard here would be dead code that reads
    // as a live possibility.
    if (!m_tilingHandler->hasScrollingScreens()) {
        return nullptr;
    }
    if (!w || w->isDeleted() || w->isUserMove() || w->isUserResize()) {
        return nullptr;
    }
    // Memoised per pass, and ONLY within a pass: prePaintWindow and
    // paintWindow both ask, for every window, on every output pass, and one
    // pass guarantees the strip state cannot change under the answer. TWO
    // caller classes run outside any pass and always compute fresh: the
    // INPUT filter (via scrollClipGeometryFor — a tile batch can land
    // between passes and is exactly what moves a column across the
    // boundary), and postPaintScreen's park-reap driver (m_currentPassOutput
    // is deliberately cleared at its top, so its per-parked-window resolve
    // is uncached by design — bounded by the parked population, and the
    // per-pass validity argument would not hold there either). Stale keys
    // for windows that died between passes CAN sit in the map until the
    // next prePaintScreen's clear, but they are never read before that
    // clear (every read is behind the same in-pass gate) and keys are only
    // hashed by pointer value, never dereferenced.
    const bool inPass = m_currentPassOutput != nullptr;
    if (inPass) {
        if (const auto it = m_scrollManagedCache.constFind(w); it != m_scrollManagedCache.constEnd()) {
            return it.value();
        }
    }
    KWin::LogicalOutput* managed = nullptr;
    const QString windowId = getWindowId(w);
    const QString trackedScreen = m_tilingHandler->scrollTrackedScreenFor(windowId);
    if (!trackedScreen.isEmpty() && !m_navigationHandler->isWindowFloating(windowId)) {
        managed = outputForScreenId(trackedScreen);
    }
    if (inPass) {
        m_scrollManagedCache.insert(w, managed);
    }
    return managed;
}

QRect PlasmaZonesEffect::scrollClipGeometryFor(KWin::EffectWindow* w) const
{
    // The rect form of scrollManagedOutputFor, for the input filter, which
    // needs the boundary itself rather than an output to compare against.
    // Defined in terms of it so the two can never disagree about WHICH windows
    // are strip straddlers: a window whose overhang is suppressed from a
    // foreign output's paint must be non-interactive in that same region.
    //
    // The predicate's own reasoning lives on scrollManagedOutputFor. The part
    // worth repeating here: it routes through scrollTrackedScreenFor, NOT
    // m_trackedScreenPerWindow, because that map is populated for EVERY window
    // setupWindowConnections runs on — dialogs, popups, excluded apps,
    // keep-above overlays — so keying on it clipped any window merely sitting
    // on a scrolling screen, and a modal straddling the boundary had half of
    // itself treated as dead overhang.
    const KWin::LogicalOutput* managedOutput = scrollManagedOutputFor(w);
    if (!managedOutput) {
        return QRect();
    }
    const KWin::Rect g = managedOutput->geometry();
    return QRect(g.x(), g.y(), g.width(), g.height());
}

QPoint PlasmaZonesEffect::scrollVisualTranslationFor(const QString& windowId, const QRectF& frameRect) const
{
    const auto it = m_scrollVisualDelta.constFind(windowId);
    if (it == m_scrollVisualDelta.constEnd()) {
        return {};
    }
    return scrollVisualTranslationFor(*it, frameRect);
}

QPoint PlasmaZonesEffect::scrollVisualTranslationFor(const ScrollVisualPlacement& placement, const QRectF& frameRect)
{
    // ALWAYS pass the window's FRAME here, never its expanded band and never a
    // rect that already carries a translation. The centring term below is
    // derived from the rect it is handed, so handing it a band centres by the
    // band's size and lands the answer half the decoration asymmetry away from
    // where the window is drawn. Consumers that move a band apply the returned
    // translation to the band; they resolve on the frame.
    //
    // Centred by the tile's OWN size within the column it was handed, which is
    // where both constrain paths put a smaller frame on screen (the X11
    // pre-pass does it explicitly; KWin does it for a Wayland client that
    // renegotiated). Clamped at zero to match constrainTileGeometry
    // (drag_snap.cpp, `qMax(0, ...)`): when a minimum size exceeds the column
    // the window stays anchored at the column's origin rather than shifting
    // past its edge, and the drawn position has to follow the committed one.
    //
    // Both terms come off toRect() rather than qRound()-ing width and height
    // separately, because QRectF::toRect() derives its integer extent from the
    // rect's POSITION as well as its size. Every commit path that must agree
    // with this uses frameGeometry().toRect(), so reading the same way is what
    // keeps the drawn and committed centring identical on a fractional-scale
    // output, where the two differ by a pixel.
    const QRect r = frameRect.toRect();
    const int offsetX = qMax(0, placement.columnSize.width() - r.width()) / 2;
    const int offsetY = qMax(0, placement.columnSize.height() - r.height()) / 2;
    // The translation to APPLY, not the destination: every consumer adds this
    // to a rect it already has, so the shape matches what the stored delta
    // used to hand them.
    return QPoint(placement.stripPos.x() + offsetX - r.x(), placement.stripPos.y() + offsetY - r.y());
}

bool PlasmaZonesEffect::scrollParkedOffscreen(KWin::EffectWindow* w, const QString& windowId) const
{
    // Ordered cheapest-first: the empty-map probe is the common-case exit on a
    // desktop with nothing parked, and the delta probe answers before the
    // predicate walk for every never-parked column.
    //
    // The empty-map exit is SKIPPED while the diagnostic category is on, and
    // that is the whole point rather than an oversight. An empty map is
    // precisely what a screen with no batch, or one whose entries the strip
    // retire just dropped, looks like — so exiting here made the MISS trace
    // below unreachable in one of the two states it exists to report, and the
    // log read identically to "no strip windows here". The isEmpty() test
    // stays FIRST so the disabled path pays an inlined size check and only
    // then an atomic load, never a string hash.
    if (!w) {
        return false;
    }
    if (m_scrollVisualDelta.isEmpty() && !lcStripDiag().isDebugEnabled()) {
        return false;
    }
    const auto vit = m_scrollVisualDelta.constFind(windowId);
    if (vit == m_scrollVisualDelta.constEnd()) {
        // A MISS on a window that IS strip-managed is the interesting case, and
        // reporting only hits made it invisible: a missing entry and a
        // non-strip window both produced silence, which is the difference the
        // trace exists to show. The strip-membership resolve is done HERE
        // rather than by reordering the probes above, so the hot path keeps its
        // cheapest-first order and pays nothing while the category is off.
        if (lcStripDiag().isDebugEnabled() && scrollManagedOutputFor(w)) {
            const StripDiagSample sample{false, {}, {}, false};
            const auto lastIt = m_stripDiagLast.constFind(windowId);
            if (lastIt == m_stripDiagLast.constEnd() || !(*lastIt == sample)) {
                m_stripDiagLast.insert(windowId, sample);
                qCDebug(lcStripDiag) << "park resolve:" << windowId
                                     << "placement=MISS (strip-managed, no visual delta) verdict= painted-at-commit";
            }
        }
        return false;
    }
    KWin::LogicalOutput* const managed = scrollManagedOutputFor(w);
    if (!managed) {
        // Report and advance the gate. A window that HOLDS a relocation but
        // resolves no managed output is a real transition — a strip retire, or
        // a screen mid-change — and returning silently left the gate holding
        // the pre-transition sample, so the eventual return to that same tuple
        // was suppressed as unchanged and the whole excursion was invisible in
        // the log.
        if (lcStripDiag().isDebugEnabled()) {
            const StripDiagSample sample{true, vit->stripPos, {}, false};
            const auto lastIt = m_stripDiagLast.constFind(windowId);
            if (lastIt == m_stripDiagLast.constEnd() || !(*lastIt == sample)) {
                m_stripDiagLast.insert(windowId, sample);
                qCDebug(lcStripDiag) << "park resolve:" << windowId
                                     << "placement=HIT but no managed output verdict= painted-at-commit";
            }
        }
        return false;
    }
    // The rect paintWindow actually draws: the window's expanded band moved
    // to its strip placement by the same resolver the draw uses, slid by the
    // live view offset. Expanded geometry (not the frame) so a decoration
    // shadow reaching into the viewport from a just-offscreen column keeps
    // painting; the chain's outer padding is added below for the same reason.
    //
    // The band is what gets tested, but the strip placement is always resolved
    // against the FRAME (see below), so a size-constrained frame still lands
    // centred inside its column rather than at the column's corner.
    const QRectF frame = w->frameGeometry();
    QRectF visual = w->expandedGeometry();
    if (visual.isEmpty()) {
        visual = frame;
    }
    if (visual.isEmpty() || frame.isEmpty()) {
        // Degenerate geometry (mid-unmap, zero-size commit): an empty rect
        // never intersects anything, so falling through would answer PARKED
        // for a window we cannot actually locate — and the reap consumer
        // would release its surface state on that answer. The frame is checked
        // too because it is what the placement resolves against; a zero-size
        // frame would centre the window by the whole column. Fail open like
        // every other unknown in this predicate.
        return false;
    }
    // A live per-window geometry leg draws at the ANIMATOR's current rect,
    // not the committed frame — the backdrop predictor models the same term
    // for the same reason (paint_pipeline's captureWindowBackdrop preamble).
    // Testing the committed band instead judges the window at its
    // destination for the whole leg: a column animating into (or out of)
    // its park blinks at the cull, drops setTransformed early, and clocks
    // the park reap off a rect nothing is drawn at yet. Relocate the
    // expanded band by the animator's frame, keeping the decoration margins
    // the frame rect does not carry. (The backdrop helper itself is not
    // shareable here — it composes a device-space capture rect, not the
    // logical band this intersects test needs.)
    const QRectF animated = m_windowAnimator->currentValue(w, QRectF());
    if (animated.isValid()) {
        visual = animated.adjusted(visual.left() - frame.left(), visual.top() - frame.top(),
                                   visual.right() - frame.right(), visual.bottom() - frame.bottom());
    }
    // Resolved against the COMMITTED frame and applied to the band. Resolving
    // against the band would centre by the band's size, which differs from the
    // frame's by the decoration margins and is asymmetric on the vertical axis
    // for a bottom-heavy shadow. Resolving against the ANIMATOR's rect would be
    // worse still: the resolver subtracts the rect it is handed, so the
    // relocation applied just above would cancel out and this predicate would
    // judge the window at its destination for the whole leg — exactly the blink
    // the relocation exists to prevent.
    const QPoint translation = scrollVisualTranslationFor(*vit, frame);
    visual.translate(translation.x(), translation.y());
    // The clip predicate has to follow the SAME axis the strip is sliding on,
    // or an off-view column is judged against a band ninety degrees from where
    // it is actually drawn. This one gates the park reap, the setTransformed
    // flag and the strip-capture anchor election, so getting it wrong either
    // culls a visible column or keeps a parked one painting forever.
    const QPointF viewOffset = m_stripViewAnimator->offsetFor(managed);
    visual.translate(viewOffset);
    if (const auto decoIt = m_windowDecorations.constFind(windowId); decoIt != m_windowDecorations.constEnd()) {
        const qreal pad = decoIt->outerPadding;
        visual.adjust(-pad, -pad, pad, pad);
    }
    const KWin::Rect g = managed->geometry();
    const bool parked = !visual.intersects(QRectF(g.x(), g.y(), g.width(), g.height()));

    // Seam diagnostics (docs/strip-identity-seam-plan.md, stage 0). This is the
    // one place both halves of a parked column's drawn position are in hand at
    // once: the m_scrollVisualDelta entry (candidate C) and the animator's view
    // offset (candidate B). Reporting them anywhere else would re-derive them
    // and could disagree with what the draw actually used.
    //
    // Runs per strip window per pass, so it is gated twice: the category is off
    // by default, and even enabled it only reports when the tuple changes. The
    // isDebugEnabled() check is not redundant with qCDebug — it keeps the
    // sample construction and the hash write off the hot path entirely.
    //
    // What it costs when the category IS on, since this instrument is read on
    // the timing it perturbs: the empty-map exit above is skipped, so every
    // strip-managed window resolves its output once per pass. That resolve is
    // memoised for the pass, except at the park-reap caller, which runs
    // outside a pass on purpose and therefore pays it uncached — though there
    // it iterates the relocation map, so the empty case never reaches it.
    if (lcStripDiag().isDebugEnabled()) {
        const StripDiagSample sample{true, vit->stripPos, viewOffset.toPoint(), parked};
        const auto lastIt = m_stripDiagLast.constFind(windowId);
        if (lastIt == m_stripDiagLast.constEnd() || !(*lastIt == sample)) {
            m_stripDiagLast.insert(windowId, sample);
            // outerPadding is reported because it is the one input that differs
            // systematically between the nested harness and a real session: the
            // harness assigns no decoration packs, so the band is NARROWER there
            // and columns cull EARLIER. A harness run that shows pad=0 cannot be
            // used to reason about the cull boundary at all.
            const auto padIt = m_windowDecorations.constFind(windowId);
            const qreal pad = padIt != m_windowDecorations.constEnd() ? padIt->outerPadding : 0.0;
            qCDebug(lcStripDiag) << "park resolve:" << windowId << "placement=HIT stripPos=" << sample.stripPos
                                 << "viewOffset=" << sample.viewOffset << "pad=" << pad
                                 << "verdict=" << (parked ? "CULLED" : "painted");
        }
    }
    return parked;
}

} // namespace PlasmaZones
