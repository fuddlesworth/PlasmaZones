// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scrolling-strip clip predicate, shared by the paint cull
// (paint_pipeline.cpp) and the overhang input filter (input_filter.cpp).
// Split out of paint_pipeline.cpp by concern: this pair answers WHICH
// windows are strip members and where they are confined, while the paint
// pipeline consumes the answer.

#include "plasmazoneseffect.h"

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
    if (!m_tilingHandler || !m_tilingHandler->hasScrollingScreens()) {
        return nullptr;
    }
    if (!w || w->isDeleted() || w->isUserMove() || w->isUserResize()) {
        return nullptr;
    }
    // Memoised per pass, and ONLY within a pass: prePaintWindow and
    // paintWindow both ask, for every window, on every output pass, and one
    // pass guarantees the strip state cannot change under the answer. The
    // INPUT filter also routes through here (via scrollClipGeometryFor) but
    // runs outside any pass — a tile batch can land between passes and is
    // exactly what moves a column across the boundary — so outside a pass
    // the predicate is always computed fresh and never cached. Stale keys
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
    if (!trackedScreen.isEmpty() && m_navigationHandler && !m_navigationHandler->isWindowFloating(windowId)) {
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

} // namespace PlasmaZones
