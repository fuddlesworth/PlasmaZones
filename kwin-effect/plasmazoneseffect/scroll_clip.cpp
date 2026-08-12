// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scrolling-strip clip predicate, shared by the paint cull
// (paint_pipeline.cpp) and the overhang input filter (input_filter.cpp).
// Split out of paint_pipeline.cpp by concern: this pair answers WHICH
// windows are strip members and where they are confined, while the paint
// pipeline consumes the answer.

#include "plasmazoneseffect.h"

#include "compositor/stripviewanimator.h"
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

bool PlasmaZonesEffect::scrollParkedOffscreen(KWin::EffectWindow* w, const QString& windowId) const
{
    // Ordered cheapest-first: the empty-map probe is the common-case exit on a
    // desktop with nothing parked, and the delta probe answers before the
    // predicate walk for every never-parked column.
    if (!w || m_scrollVisualDelta.isEmpty()) {
        return false;
    }
    const auto vit = m_scrollVisualDelta.constFind(windowId);
    if (vit == m_scrollVisualDelta.constEnd()) {
        return false;
    }
    KWin::LogicalOutput* const managed = scrollManagedOutputFor(w);
    if (!managed) {
        return false;
    }
    // The rect paintWindow actually draws: the window's expanded band
    // relocated by the stored strip-minus-park delta — the same additive
    // translate the draw applies — slid by the live view offset. Expanded
    // geometry (not the frame) so a decoration shadow reaching into the
    // viewport from a just-offscreen column keeps painting; the chain's outer
    // padding is added below for the same reason.
    //
    // The delta rides on the COMMITTED position rather than replacing it,
    // which is what keeps this predicate honest for a size-constrained X11
    // frame: such a window is committed centred inside its column, so
    // differencing an absolute strip position against the committed rect
    // would test a band at the column's corner instead of where the window
    // is drawn, and cull against the wrong rect.
    QRectF visual = w->expandedGeometry();
    if (visual.isEmpty()) {
        visual = w->frameGeometry();
    }
    if (visual.isEmpty()) {
        // Degenerate geometry (mid-unmap, zero-size commit): an empty rect
        // never intersects anything, so falling through would answer PARKED
        // for a window we cannot actually locate — and the reap consumer
        // would release its surface state on that answer. Fail open like
        // every other unknown in this predicate.
        return false;
    }
    visual.translate(vit->x(), vit->y());
    visual.translate(m_stripViewAnimator->offsetFor(managed), 0.0);
    if (const auto decoIt = m_windowDecorations.constFind(windowId); decoIt != m_windowDecorations.constEnd()) {
        const qreal pad = decoIt->outerPadding;
        visual.adjust(-pad, -pad, pad, pad);
    }
    const KWin::Rect g = managed->geometry();
    return !visual.intersects(QRectF(g.x(), g.y(), g.width(), g.height()));
}

} // namespace PlasmaZones
