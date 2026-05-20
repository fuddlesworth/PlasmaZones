// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Drag-to-reorder — interactive-resize tracking + drop-resolution + the async
// windowDropped D-Bus call with failure rollback. Split out of scrollhandler.cpp
// to keep that translation unit under the 800-line limit; mirrors the
// autotilehandler/ sub-file layout and the engine's own ScrollEngine.cpp /
// ScrollEngineNavigation.cpp split for the same reason.

#include "../scrollhandler.h"
#include "../plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>

#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>
#include <QTimer>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void ScrollHandler::onWindowMoveResizeStarted(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    // isUserResize() is reliable here (the start signal) — an interactive
    // resize sets it, a plain move leaves it false — but not necessarily at
    // the finish signal, so the verdict is recorded now for onWindowDragFinished.
    const QString windowId = m_effect->getWindowId(w);
    if (w->isUserResize()) {
        m_interactiveResize.insert(windowId);
    } else {
        m_interactiveResize.remove(windowId);
    }
}

void ScrollHandler::onWindowDragFinished(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // A resize is not a drag-to-reorder. The verdict was recorded at the start
    // signal (isUserResize() is unreliable at finish); consume it either way.
    if (m_interactiveResize.remove(windowId)) {
        return;
    }
    if (!m_notifiedWindows.contains(windowId)) {
        return; // not a scroll-tracked window
    }
    const QString screenId = m_notifiedWindowScreens.value(windowId);
    if (!m_scrollScreens.contains(screenId)) {
        return;
    }
    // The dragged window kept its tile slot during the move (the geometry
    // re-assert is suppressed mid-drag). On release, reorder its column to the
    // strip slot nearest the drop point.
    //
    // Use m_slotGeometry (column rect) — NOT m_appliedGeometry (which is the
    // centered sub-rect for constrained windows). For a fixed-size window
    // centred in a wider slot, the applied rect is narrower than the column,
    // so column-edge comparisons against it would mis-classify within-column
    // nudges as cross-column drops.
    const auto draggedIt = m_slotGeometry.constFind(windowId);
    if (draggedIt == m_slotGeometry.constEnd()) {
        return; // the daemon has not resolved a slot for this window yet
    }
    const QRect draggedSlot = draggedIt.value();
    const int dropX = w->frameGeometry().toRect().center().x();
    // A drop still within the dragged window's own COLUMN is a no-op — the
    // window was nudged but not carried out of its slot.
    if (dropX >= draggedSlot.left() && dropX <= draggedSlot.right()) {
        return;
    }
    // Pick the anchor: among scroll windows on the same screen in a *different*
    // column (a different resolved x-range than the dragged window's own), the
    // one whose tile the drop-x is over or nearest. Ties resolve to the
    // leftmost candidate so the result does not depend on QHash iteration order.
    // Within a stacked column (every tile shares both x edges and the same
    // distance), break ties on otherId lexicographically so the chosen anchor
    // is reproducible across runs and across hash-seed variations.
    QString anchorId;
    int bestDistance = -1;
    int bestLeft = 0;
    bool placeAfter = false;
    for (auto it = m_slotGeometry.cbegin(); it != m_slotGeometry.cend(); ++it) {
        const QString& otherId = it.key();
        if (otherId == windowId || m_notifiedWindowScreens.value(otherId) != screenId) {
            continue;
        }
        const QRect& slot = it.value();
        // Same-column check uses BOTH edges. Tiles within a column always
        // share both x edges (they only differ in y). Comparing slot rects
        // (not applied rects) makes this reliable for constrained windows.
        if (slot.left() == draggedSlot.left() && slot.right() == draggedSlot.right()) {
            continue; // a tile of the dragged window's own column
        }
        int distance = 0;
        if (dropX < slot.left()) {
            distance = slot.left() - dropX;
        } else if (dropX > slot.right()) {
            distance = dropX - slot.right();
        }
        const bool fresh = (bestDistance < 0);
        const bool closerColumn = !fresh && (distance < bestDistance);
        const bool sameDistanceLeftmostColumn = !fresh && (distance == bestDistance) && (slot.left() < bestLeft);
        // Stacked-column tile pick: when the column itself is the anchor (same
        // distance AND same left edge), break on otherId lexicographic so the
        // result is deterministic. Without this, QHash iteration order picks
        // arbitrarily among tiles in the same column.
        const bool sameColumnLexicographicTile =
            !fresh && (distance == bestDistance) && (slot.left() == bestLeft) && (otherId < anchorId);
        if (fresh || closerColumn || sameDistanceLeftmostColumn || sameColumnLexicographicTile) {
            bestDistance = distance;
            bestLeft = slot.left();
            anchorId = otherId;
            // Distance ranks columns by their edges; the side to land on is
            // taken from the column centre — for a gap drop these compose
            // (a drop in the gap left of a column is left of its centre).
            placeAfter = dropX > slot.center().x();
        }
    }
    if (anchorId.isEmpty()) {
        return; // no other column on the strip to reorder against
    }
    // The reorder supersedes the drift re-assert: drop any pending one and
    // suppress new ones until the daemon's re-resolve lands. The reorder is a
    // genuine cross-column move (own-column drops and same-column anchors are
    // excluded above), so the engine always re-resolves and recordAppliedGeometry
    // clears m_reorderPending.
    m_reassertPending.remove(windowId);
    m_reorderPending.insert(windowId);
    // Tracked async call rather than fire-and-forget: m_reorderPending suppresses
    // drift re-asserts for this window until the daemon's re-resolve lands. If
    // the windowDropped call never reaches the daemon, that suppression must be
    // lifted here — otherwise the window stays permanently uncorrectable for
    // drift until it closes or the daemon reconnects.
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Scroll,
                                                   QStringLiteral("windowDropped"), {windowId, anchorId, placeAfter}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, windowId, epoch = m_daemonEpoch](QDBusPendingCallWatcher* watcher) {
                watcher->deleteLater();
                if (!watcher->isError()) {
                    return; // delivered — the daemon re-resolve will clear m_reorderPending
                }
                qCWarning(lcEffect) << "scroll windowDropped D-Bus call failed for" << windowId << ":"
                                    << watcher->error().message();
                // Skip if the daemon reconnected meanwhile — onDaemonReady has
                // already cleared m_reorderPending and rebuilt the tracking sets.
                if (epoch != m_daemonEpoch) {
                    return;
                }
                m_reorderPending.remove(windowId);
                // Only queue the re-assert if the window is STILL scroll-tracked
                // by the time the failure reply arrives. Between the drop and
                // this lambda, the window may have closed, moved off a scroll
                // screen, or gone sticky — and the tracking-set cleanups
                // (onWindowClosed, handleWindowOutputChanged, handleWindowSticky-
                // Changed) all run synchronously on their own paths, so they
                // cannot evict an entry we haven't inserted yet. Without this
                // guard, m_reassertPending accumulates a stale entry that
                // flushReasserts later discards on dispatch but which still
                // violates the "subset of m_notifiedWindows" invariant
                // recordAppliedGeometry's comment claims.
                if (!m_notifiedWindows.contains(windowId)) {
                    return;
                }
                // The post-drag windowFrameGeometryChanged event was already
                // suppressed while m_reorderPending held the window, so simply
                // clearing the flag re-enables drift correction but nothing
                // triggers it — the window would stay where the user dropped
                // it. Queue an explicit re-assert so flushReasserts() snaps it
                // back to its daemon-resolved slot. m_reasserted MUST be
                // cleared too: if the user dragged out of a slot the window
                // had already drift-re-asserted into once, m_reasserted held
                // the windowId and flushReasserts would silently no-op the
                // rescue (re-assert budget was already exhausted for that
                // resolve episode). The drop is a fresh episode — reset the
                // budget so the rescue can fire.
                m_reasserted.remove(windowId);
                m_reassertPending.insert(windowId);
                m_reassertTimer->start();
            });
    qCDebug(lcEffect) << "Notified scroll: windowDropped" << windowId << "anchor" << anchorId << "after" << placeAfter;
}

} // namespace PlasmaZones
