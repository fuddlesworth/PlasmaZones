// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The untile diff: what a completed tile batch does to the windows its screen
// tracked as tiled and no longer carries. Split from tiling.cpp, whose
// onComplete runs it once per batch screen.

#include "tilinghandler.h"
#include "untiledecisions.h"
#include "handlers/dragtracker.h"
#include "plasmazoneseffect/desktopvisibility.h"
#include "plasmazoneseffect/plasmazoneseffect.h"

#include <effect/effecthandler.h>

namespace PlasmaZones {

void TilingHandler::untileWindowsLeftOnScreen(const QString& screenId, const QSet<QString>& newSet)
{
    const QSet<QString> previous = TilingStateHelpers::tiledOnScreen(m_border, screenId);
    // What lands in `untiled`: mostly a window the daemon stopped tiling on
    // this screen. A window that moved to another screen is not among them:
    // the single-owner sweep in markWindowTiled took it out of this bucket
    // when its new screen's entry tiled it (F358). Two shapes are still not a
    // genuine untile, and nothing here excludes them: a tracked window whose
    // own entry failed applyTiling's validator (it needs a producer that
    // emits a degenerate or over-cap rect), and a window left unpicked in a
    // fuzzy bucket with more candidates than entries (a stale id after a KWin
    // restart, on the entry's own output).
    const QSet<QString> untiled = previous - newSet;
    KWin::LogicalOutput* const batchOutput = m_effect->outputForScreenId(screenId);
    const QString dragged = m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()
        ? m_effect->m_dragTracker->draggedWindowId()
        : QString();
    for (const QString& wid : untiled) {
        // Exact resolve only: findWindowById's appId fuzzy fallback could hand
        // back a same-app SIBLING for a gone id, and the jurisdiction test
        // must read the REAL window's desktop and activity (a vanished window
        // resolves null and still clears).
        KWin::EffectWindow* win = m_effect->findWindowByIdExact(wid);
        const auto move = m_expectedOutputMove.constFind(wid);
        // The batch's jurisdiction (UntileDecisions::untilesOnBatchScreen):
        // not a window on a desktop or activity the batch's output is not
        // showing (#808, F211), not the window the user is dragging (F185),
        // not a window whose output move the daemon armed from this screen
        // (F283). Genuine untiles off-context (float, close) flow through
        // funnels that clear every screen regardless.
        if (!UntileDecisions::untilesOnBatchScreen(
                win != nullptr, win && isOnDesktopShownOn(win, batchOutput), win && win->isOnCurrentActivity(),
                !dragged.isEmpty() && dragged == wid,
                move != m_expectedOutputMove.constEnd() && move->sourceScreenId == screenId)) {
            continue;
        }
        // Every untiled window drops its per-screen tiled tracking,
        // minimized/unresolvable or not.
        clearWindowTiledOnScreen(screenId, wid);
        // The parked-column paint hint dies with the tiled tracking: a window
        // in a SUPERSEDED batch's entry never reached the per-entry write, and
        // a window this batch no longer carries would otherwise keep the
        // previous batch's strip position. The removal changes where the paint
        // path draws the window, so it pairs with damage.
        if (m_effect->m_scrollVisualDelta.remove(wid) > 0 && KWin::effects) {
            KWin::effects->addRepaintFull();
        }
        // The other two strip companions go with it, as the teardown funnels
        // shed all three: a window untiled by a rule change otherwise kept the
        // column it was last OFFERED, and on re-tile the apply read
        // columnUnchanged against that stale offer and skipped offering the
        // column. Neither is a paint input, so no damage.
        m_effect->m_scrollCommandedRects.remove(wid);
        m_effect->m_scrollOfferedColumn.remove(wid);
        if (!win || win->isMinimized()) {
            // A minimized (or vanished) window KEEPS its centering target and
            // its claims: the re-tile on unminimize re-asserts them.
            continue;
        }
        // A daemon-initiated untile that is not a float/fullscreen/close (a
        // rule change dropping the window from the layout) must not leave a
        // stale centering target that teleport-centers the window on its next
        // frameGeometryChanged.
        m_tileTargetZones.remove(wid);
        m_centeredWaylandZones.remove(wid);
        // The one strip exit with no other release owner (float, close,
        // cross-output transfer, mode/screen change and teardown all have
        // theirs): every claim the window held goes, windowed fullscreen,
        // monocle and column maximize, in the release order (F186). `untiled`
        // is a local copy, so a release that re-enters cleanupAutotileTracking
        // cannot invalidate this loop.
        releaseAllClaims(wid, win, ScrollDecisions::ClaimScope::StripExit);
    }
}

} // namespace PlasmaZones
