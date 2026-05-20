// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollhandler.h"
#include "plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QTimer>
#include <QVariant>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
/// Debounce window for re-asserting geometry after an app-initiated resize —
/// long enough to coalesce a noisy resize stream into one corrective move.
constexpr int ReassertDebounceMs = 150;
} // namespace

ScrollHandler::ScrollHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
    , m_reassertTimer(new QTimer(this))
{
    m_reassertTimer->setSingleShot(true);
    m_reassertTimer->setInterval(ReassertDebounceMs);
    connect(m_reassertTimer, &QTimer::timeout, this, &ScrollHandler::flushReasserts);
}

bool ScrollHandler::isEligibleForScroll(KWin::EffectWindow* w) const
{
    // Scroll mode adds one exclusion to the shared tiling predicate: a window
    // pinned to all desktops (sticky) is never tiled — the strip is
    // per-desktop, so a sticky window would have to occupy every strip at
    // once. It is left floating instead.
    return m_effect->isEligibleForTilingNotify(w) && !m_effect->isWindowSticky(w);
}

void ScrollHandler::notifyWindowAdded(KWin::EffectWindow* w, bool focusOnAdd)
{
    if (!w) {
        return;
    }
    if (!isEligibleForScroll(w)) {
        return;
    }

    const QString screenId = m_effect->getWindowScreenId(w);
    if (!m_scrollScreens.contains(screenId)) {
        return; // not a scroll screen — autotile or snap owns this window
    }

    const QString windowId = m_effect->getWindowId(w);

    // Window was already closed before we could report the open — skip
    // (D-Bus ordering race; see m_pendingCloses).
    if (m_pendingCloses.remove(windowId)) {
        return;
    }
    if (m_notifiedWindows.contains(windowId)) {
        return;
    }
    m_notifiedWindows.insert(windowId);
    m_notifiedWindowScreens[windowId] = screenId;

    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Scroll,
                                                   QStringLiteral("windowOpened"), {windowId, screenId}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, windowId, epoch = m_daemonEpoch](QDBusPendingCallWatcher* watcher) {
                watcher->deleteLater();
                if (!watcher->isError()) {
                    return;
                }
                qCWarning(lcEffect) << "scroll windowOpened D-Bus call failed for" << windowId << ":"
                                    << watcher->error().message();
                // Skip the rollback if the daemon reconnected meanwhile —
                // onDaemonReady has already rebuilt the tracking sets, and
                // removing windowId here would corrupt that fresh state.
                if (epoch == m_daemonEpoch) {
                    m_notifiedWindows.remove(windowId);
                    m_notifiedWindowScreens.remove(windowId);
                    // The window never joined the engine — undo the decoration
                    // applied synchronously below so a failed open cannot leave
                    // it chrome-less (title bar hidden, untracked, with nothing
                    // left to restore it until the next daemon reconnect). The
                    // focus-new-windows activateWindow() is deliberately NOT
                    // rolled back: a genuinely-opened window legitimately holds
                    // focus regardless of whether the scroll engine accepted it.
                    clearDecoration(windowId, m_effect->findWindowById(windowId));
                }
            });
    qCDebug(lcEffect) << "Notified scroll: windowOpened" << windowId << "on screen" << screenId;

    // Decorate just this window — hide its title bar (when the setting is on)
    // and draw its own border. A new column does not change existing windows'
    // decoration, and their borders track their own geometry, so the per-open
    // path decorates one window instead of rebuilding the whole strip
    // (refreshDecorations() — reserved for the batch path). w is non-minimized
    // here: isEligibleForScroll rejects minimized windows.
    if (m_border.hideTitleBars) {
        setWindowBorderless(w, windowId, true);
    }
    m_effect->updateWindowBorder(windowId, w);

    // Focus-new-windows: a genuinely-opened window takes focus. Only the
    // window-open path passes focusOnAdd — re-add callers (screen change,
    // sticky toggle, un-minimize) and notifyWindowsAddedBatch (startup /
    // daemon reconnect) deliberately do not steal focus.
    if (focusOnAdd && m_focusNewWindows) {
        KWin::effects->activateWindow(w);
    }
}

void ScrollHandler::notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows)
{
    // Callers MUST pass the full window list (KWin::effects->stackingOrder()):
    // the daemon treats the first windowsOpenedBatch after a (re)connect as the
    // complete live scroll-window set and reconciles a restored strip against
    // it. A partial list would make the daemon prune live windows.
    PhosphorProtocol::WindowOpenedList batchEntries;
    QStringList batchWindowIds; // for error rollback

    for (KWin::EffectWindow* w : windows) {
        if (!isEligibleForScroll(w)) {
            continue;
        }
        const QString screenId = m_effect->getWindowScreenId(w);
        if (!m_scrollScreens.contains(screenId)) {
            continue;
        }
        const QString windowId = m_effect->getWindowId(w);
        if (m_pendingCloses.remove(windowId)) {
            continue;
        }
        if (m_notifiedWindows.contains(windowId)) {
            continue; // already reported — callers clear the set for a full re-announce
        }
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;

        // The shared WindowOpenedList carries minWidth/minHeight (autotile uses
        // them for column sizing); scroll's strip model is size-agnostic, so
        // they are left at their 0 default — a non-resizable window is fitted
        // to its tile slot effect-side via constrainToScrollSlot.
        PhosphorProtocol::WindowOpenedEntry entry;
        entry.windowId = windowId;
        entry.screenId = screenId;
        batchEntries.append(entry);
        batchWindowIds.append(windowId);
    }

    // An empty batch is still sent: org.plasmazones.Scroll.windowsOpenedBatch
    // doubles as the daemon's post-restore reconcile signal. The daemon must
    // receive one batch even when no scroll window currently exists — otherwise
    // a restored strip whose windows all closed while the daemon was down would
    // never be pruned. The daemon treats a zero-entry batch as "no live
    // windows" and reconciles the restored strip away.

    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Scroll,
                                        QStringLiteral("windowsOpenedBatch"), {QVariant::fromValue(batchEntries)}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, batchWindowIds, epoch = m_daemonEpoch](QDBusPendingCallWatcher* watcher) {
                watcher->deleteLater();
                if (!watcher->isError()) {
                    return;
                }
                qCWarning(lcEffect) << "scroll windowsOpenedBatch D-Bus call failed:" << watcher->error().message();
                // Skip the rollback if the daemon reconnected meanwhile.
                if (epoch != m_daemonEpoch) {
                    return;
                }
                for (const QString& wid : batchWindowIds) {
                    m_notifiedWindows.remove(wid);
                    m_notifiedWindowScreens.remove(wid);
                    // Undo the decoration refreshDecorations() applied below —
                    // an untracked window must not be left chrome-less.
                    clearDecoration(wid, m_effect->findWindowById(wid));
                }
            });
    qCInfo(lcEffect) << "Notified scroll: windowsOpenedBatch with" << batchEntries.size() << "windows";

    // Decorate every window that just joined the tracked set in one pass.
    refreshDecorations();
}

void ScrollHandler::onWindowClosed(const QString& windowId, const QString& screenId)
{
    // If we haven't reported this window open yet, record the close so a
    // late windowOpened (D-Bus ordering race) is suppressed.
    if (!m_notifiedWindows.contains(windowId) && m_scrollScreens.contains(screenId)) {
        m_pendingCloses.insert(windowId);
    }
    m_notifiedWindows.remove(windowId);
    m_notifiedWindowScreens.remove(windowId);
    m_appliedGeometry.remove(windowId);
    m_slotGeometry.remove(windowId);
    m_reassertPending.remove(windowId);
    m_reasserted.remove(windowId);
    m_reorderPending.remove(windowId);
    m_interactiveResize.remove(windowId);
    if (m_lastFocusFollowsMouseWindowId == windowId) {
        m_lastFocusFollowsMouseWindowId.clear();
    }

    if (m_scrollScreens.contains(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Scroll,
                                                       QStringLiteral("windowClosed"), {windowId},
                                                       QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified scroll: windowClosed" << windowId << "on screen" << screenId;
    }

    // The window left scroll management — restore any title bar scroll hid and
    // drop its border. findWindowById returns null for a genuinely-closed
    // window (the title-bar restore is then skipped — the window is gone); for
    // a window that merely left a scroll screen it is still alive and restored.
    clearDecoration(windowId, m_effect->findWindowById(windowId));
}

void ScrollHandler::onWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    const bool minimized = w->isMinimized();

    if (!m_notifiedWindows.contains(windowId)) {
        // The window was never reported open — e.g. it opened already
        // minimized (isEligibleForTilingNotify rejects minimized windows).
        // On restore, treat it as a fresh open; a minimize for a window the
        // engine never knew about needs no report.
        if (!minimized) {
            notifyWindowAdded(w);
        }
        return;
    }

    const QString screenId = m_notifiedWindowScreens.value(windowId);
    if (!m_scrollScreens.contains(screenId)) {
        return;
    }
    if (minimized) {
        // A minimized window leaves the visible layout — its resolved-geometry
        // reference is stale until the strip re-resolves on restore. Drop it so
        // onWindowFrameGeometryChanged cannot re-assert against a stale slot,
        // and drop any in-flight drag-reorder bookkeeping: a minimize while a
        // windowDropped is in flight supersedes it (the daemon re-resolve that
        // would have cleared m_reorderPending now excludes the minimized
        // window, so it must be cleared here). Any interactive move/resize is
        // likewise over once the window is minimized — clear all of this
        // window's transient effect-side state in one place. m_reasserted is
        // cleared too: the next daemon resolve after restore is a fresh
        // episode that gets its own re-assert budget.
        m_appliedGeometry.remove(windowId);
        m_slotGeometry.remove(windowId);
        m_reassertPending.remove(windowId);
        m_reasserted.remove(windowId);
        m_reorderPending.remove(windowId);
        m_interactiveResize.remove(windowId);
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Scroll,
                                                   QStringLiteral("windowMinimizedChanged"), {windowId, minimized},
                                                   QStringLiteral("windowMinimizedChanged"));
    qCDebug(lcEffect) << "Notified scroll: windowMinimizedChanged" << windowId << minimized;

    // A minimized window leaves the visible layout (updateWindowBorder skips
    // it); a restored one rejoins it. A minimized window must also not stay
    // borderless — it would otherwise show chrome-less in the overview / task
    // switcher — so its title bar is restored on minimize and re-hidden on
    // restore. refreshDecorations() and updateHideTitleBarsSetting() skip
    // minimized windows, so this state is not clobbered while it stays minimized.
    if (m_border.hideTitleBars) {
        setWindowBorderless(w, windowId, !minimized);
    }
    // Only this window's border changed — updateWindowBorder drops it on
    // minimize (it skips minimized windows) and recreates it on restore. A
    // full updateAllBorders() rebuild would be redundant work for siblings.
    m_effect->updateWindowBorder(windowId, w);
}

void ScrollHandler::handleWindowOutputChanged(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    const QString newScreenId = m_effect->getWindowScreenId(w);
    const bool wasTracked = m_notifiedWindows.contains(windowId);
    if (wasTracked && m_notifiedWindowScreens.value(windowId) == newScreenId) {
        return; // a tracked window whose screen did not actually change
    }

    // Left its old strip — drop it there. onWindowClosed reports windowClosed
    // only when the old screen is (still) scroll mode.
    if (wasTracked) {
        onWindowClosed(windowId, m_notifiedWindowScreens.value(windowId));
    }
    // Arrived somewhere new — notifyWindowAdded re-adds it iff the new screen
    // is a scroll-mode screen and the window is eligible.
    notifyWindowAdded(w);
}

void ScrollHandler::handleWindowStickyChanged(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (m_effect->isWindowSticky(w)) {
        // Pinned to all desktops: drop it from its strip if tracked — scroll
        // mode does not tile sticky windows. The window stays put and floats.
        if (m_notifiedWindows.contains(windowId)) {
            onWindowClosed(windowId, m_notifiedWindowScreens.value(windowId));
        }
    } else if (!m_notifiedWindows.contains(windowId)) {
        // No longer sticky — re-tile it. notifyWindowAdded self-gates on
        // eligibility, the scroll-screen set and the current desktop.
        notifyWindowAdded(w);
    }
}

void ScrollHandler::recordAppliedGeometry(const QString& windowId, const QRect& slotRect, const QRect& appliedRect)
{
    if (!m_notifiedWindows.contains(windowId)) {
        // Only scroll-tracked windows — keeps m_appliedGeometry a subset of
        // m_notifiedWindows so the two cannot drift apart (a scroll batch may
        // momentarily name a window dropped from tracking mid-flight).
        return;
    }
    m_appliedGeometry[windowId] = appliedRect;
    m_slotGeometry[windowId] = slotRect;
    // The window is being moved to match this geometry — drop any stale
    // re-assert queued from an earlier drift, and reset the re-assert budget:
    // a fresh daemon resolve is a new episode. A drag-reorder for this window
    // is likewise now resolved, so the re-assert suppression can lift.
    // m_interactiveResize is also cleared: a daemon resolve cannot fire mid-
    // drag (onWindowFrameGeometryChanged skips during isUserMove/isUserResize
    // and applyGeometriesBatch doesn't fire mid-drag), so its presence here
    // would be a stale flag from a prior drag that was already over.
    m_reassertPending.remove(windowId);
    m_reasserted.remove(windowId);
    m_reorderPending.remove(windowId);
    m_interactiveResize.remove(windowId);
}

void ScrollHandler::onWindowFrameGeometryChanged(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    // Never correct geometry mid interactive move/resize — that would fight
    // the user's drag. A minimized window is not in the visible layout, so its
    // frame changes are KWin bookkeeping, not an app resize. After an
    // interactive op ends, a final frame change re-runs this and re-asserts.
    if (w->isUserMove() || w->isUserResize() || w->isMinimized()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (!m_notifiedWindows.contains(windowId)) {
        return; // not a scroll-tracked window
    }
    if (m_reorderPending.contains(windowId)) {
        // A drag-reorder (windowDropped) is in flight for this window; the
        // daemon's re-resolve will land via recordAppliedGeometry. Suppressing
        // the drift re-assert until then avoids snapping the window back to its
        // pre-drag slot in the gap before the reorder applies.
        return;
    }
    const auto it = m_appliedGeometry.constFind(windowId);
    if (it == m_appliedGeometry.constEnd()) {
        return; // the daemon has not resolved a geometry to compare against yet
    }
    // Ignore sub-pixel rounding and small compositor size-hint adjustments; a
    // genuine app-initiated resize drifts further than the tolerance. Bumped
    // from 4 to 8 to absorb the larger rounding errors that surface on HiDPI
    // (2x) outputs without needing a per-window scale lookup. A genuine app
    // resize still drifts well past 8 px on its first commit.
    constexpr int kTolerance = 8;
    const QRect frame = w->frameGeometry().toRect();
    const QRect& expected = it.value();
    const bool drifted = qAbs(frame.x() - expected.x()) > kTolerance || qAbs(frame.y() - expected.y()) > kTolerance
        || qAbs(frame.width() - expected.width()) > kTolerance || qAbs(frame.height() - expected.height()) > kTolerance;
    if (!drifted) {
        m_reassertPending.remove(windowId);
        return;
    }
    if (m_reasserted.contains(windowId)) {
        // Already re-asserted once since the daemon last resolved this window.
        // The residual drift is the window's own size constraints (e.g. an X11
        // terminal's cell-size increments) that it cannot resolve to the exact
        // tile rect — re-asserting again would just loop. Accept it; the next
        // daemon push (recordAppliedGeometry) resets the budget.
        return;
    }
    // Debounce: coalesce a noisy resize stream (and any in-progress user drag)
    // into one corrective move once it settles.
    m_reassertPending.insert(windowId);
    m_reassertTimer->start();
}

void ScrollHandler::flushReasserts()
{
    const QStringList pending(m_reassertPending.cbegin(), m_reassertPending.cend());
    m_reassertPending.clear();
    for (const QString& windowId : pending) {
        const auto it = m_appliedGeometry.constFind(windowId);
        if (it == m_appliedGeometry.constEnd() || !m_notifiedWindows.contains(windowId)) {
            continue;
        }
        KWin::EffectWindow* w = m_effect->findWindowById(windowId);
        if (!w || w->isDeleted()) {
            continue;
        }
        // Re-assert the daemon's resolved geometry — an app cannot resize its
        // way out of the scroll strip. Interactive resize arrives in Phase 3.
        // Mark re-asserted BEFORE applySnapGeometry: moveResize emits
        // windowFrameGeometryChanged synchronously, re-entering
        // onWindowFrameGeometryChanged — which must already see this window as
        // re-asserted so it does not re-queue a second cycle. One re-assert per
        // daemon-resolve episode; a window that still drifts after it cannot
        // hit the exact tile rect (size hints) and is left as-is.
        m_reasserted.insert(windowId);
        m_effect->applySnapGeometry(w, it.value(), /*allowDuringDrag=*/false, /*skipAnimation=*/true);
        qCDebug(lcEffect) << "Re-asserted scroll geometry for" << windowId << "->" << it.value();
    }
}

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
        if (bestDistance < 0 || distance < bestDistance || (distance == bestDistance && slot.left() < bestLeft)) {
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
                // back to its daemon-resolved slot.
                m_reassertPending.insert(windowId);
                m_reassertTimer->start();
            });
    qCDebug(lcEffect) << "Notified scroll: windowDropped" << windowId << "anchor" << anchorId << "after" << placeAfter;
}

void ScrollHandler::notifyWindowFocused(const QString& windowId, const QString& screenId)
{
    // Keep the focus-follows-mouse dedup key in step with every focus change —
    // including focus leaving to a non-scroll window or another screen — not
    // just FFM-originated ones. The key must equal the focused window iff that
    // window is scroll-managed AND on a scroll screen right now; otherwise be
    // empty. A stale key left pointing at a no-longer-focused scroll window —
    // OR a tracked window now on a non-scroll screen — would make
    // handleCursorMoved's "already focused" short-circuit suppress a legitimate
    // re-focus when the cursor returns. Both predicates are required: a window
    // tracked on its OLD strip (after the user moved it to a non-scroll screen)
    // is still in m_notifiedWindows but should not act as a dedup anchor for
    // scroll-mode focus events. Updated before the scroll-screen guard so a
    // focus change onto a snapping/autotile/unmanaged screen still clears it.
    const bool isCurrentlyOnScrollScreen = m_notifiedWindows.contains(windowId) && m_scrollScreens.contains(screenId);
    m_lastFocusFollowsMouseWindowId = isCurrentlyOnScrollScreen ? windowId : QString();
    if (!m_scrollScreens.contains(screenId)) {
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Scroll,
                                                   QStringLiteral("notifyWindowFocused"), {windowId, screenId},
                                                   QStringLiteral("notifyWindowFocused"));
}

void ScrollHandler::handleCursorMoved(const QPointF& pos, const QString& screenId)
{
    if (!m_focusFollowsMouse || m_scrollScreens.isEmpty()) {
        return;
    }
    // Only act on scroll screens — the caller already resolved screenId.
    if (screenId.isEmpty() || !m_scrollScreens.contains(screenId)) {
        return;
    }
    // Find the topmost window under the cursor (stacking order, top → bottom).
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        if (!w || w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
            continue;
        }
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        const QString windowId = m_effect->getWindowId(w);
        // The topmost window under the cursor is not scroll-managed (a dialog,
        // popup, excluded app, floating window). Don't look through it to focus
        // a scroll window beneath — that would steal focus from the overlay.
        if (!m_notifiedWindows.contains(windowId)) {
            return;
        }
        if (windowId == m_lastFocusFollowsMouseWindowId) {
            return; // already focused — no-op
        }
        m_lastFocusFollowsMouseWindowId = windowId;
        KWin::effects->activateWindow(w);
        return;
    }
}

void ScrollHandler::onDaemonReady()
{
    // Bump the epoch so any D-Bus reply still in flight from before this
    // (re)connect skips its rollback instead of corrupting the rebuilt sets.
    ++m_daemonEpoch;
    // Reset every tracking structure before re-querying — a daemon (re)connect
    // rebuilds scroll state from scratch, so no stale entry may survive into
    // the new session. In particular a pending re-assert must be dropped: it
    // targets geometry the old daemon resolved, which the new one has not.
    // Cleared first so loadSettings()'s async batch reply repopulates cleanly.
    //
    // Restore title bars before dropping m_borderlessWindows: a (re)connect
    // rebuilds the scroll window set from scratch and refreshDecorations()
    // re-hides them, but a borderless window left untracked here would keep
    // its hidden title bar with nothing left to restore it. Snapshot first —
    // setWindowBorderless mutates m_borderlessWindows under the loop.
    const QStringList borderless(m_borderlessWindows.cbegin(), m_borderlessWindows.cend());
    for (const QString& windowId : borderless) {
        setWindowBorderless(m_effect->findWindowById(windowId), windowId, false);
    }
    m_borderlessWindows.clear();
    m_notifiedWindows.clear();
    m_notifiedWindowScreens.clear();
    m_pendingCloses.clear();
    m_appliedGeometry.clear();
    m_slotGeometry.clear();
    m_reassertPending.clear();
    m_reasserted.clear();
    m_reorderPending.clear();
    m_interactiveResize.clear();
    m_lastFocusFollowsMouseWindowId.clear();
    m_reassertTimer->stop();
    connectSignals();
    loadSettings();
}

void ScrollHandler::connectSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Disconnect first so daemon restarts don't accumulate duplicate match
    // rules — Qt registers the same handler twice if connect() is called
    // twice with identical args.
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Scroll, QStringLiteral("scrollScreensChanged"), this,
                   SLOT(slotScrollScreensChanged(QStringList)));
    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Scroll, QStringLiteral("scrollScreensChanged"), this,
                SLOT(slotScrollScreensChanged(QStringList)));

    qCInfo(lcEffect) << "Connected to scroll D-Bus signals";
}

void ScrollHandler::loadSettings()
{
    // Query the initial scroll-mode screen set from the daemon. Mirrors
    // AutotileHandler::loadSettings — the foreign Properties interface is
    // correct for D-Bus property reads; bound by SyncCallTimeoutMs so a
    // wedged daemon doesn't leak a watcher for Qt's default 25 s.
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Scroll << QStringLiteral("scrollScreens");

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, epoch = m_daemonEpoch](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                // A reply in flight from before a daemon (re)connect is stale:
                // onDaemonReady has since bumped the epoch, cleared the tracking sets
                // and re-issued loadSettings. Applying this older scrollScreens
                // snapshot (and its batch re-announce) would clobber the fresh state.
                if (epoch != m_daemonEpoch) {
                    return;
                }
                QDBusPendingReply<QDBusVariant> reply = *w;
                if (reply.isValid()) {
                    const QStringList screens = reply.value().variant().toStringList();
                    m_scrollScreens = QSet<QString>(screens.cbegin(), screens.cend());
                    qCInfo(lcEffect) << "Loaded scroll screens:" << m_scrollScreens;

                    // Batch-notify all existing windows on scroll screens in one
                    // D-Bus call instead of per-window windowOpened round-trips.
                    // loadSettings() runs only at construction or right after
                    // onDaemonReady() — both leave m_notifiedWindows empty — so no
                    // explicit reset is needed: every window is reported afresh,
                    // and a window already added individually in the async gap is
                    // correctly skipped (no duplicate windowOpened).
                    //
                    // Sent unconditionally — even when m_scrollScreens is empty,
                    // which yields an empty batch. The batch doubles as the
                    // daemon's post-restore reconcile signal, so a strip restored
                    // from scroll-session.json is reconciled (and pruned) even
                    // when no screen is currently in scroll mode.
                    notifyWindowsAddedBatch(KWin::effects->stackingOrder());
                } else {
                    qCDebug(lcEffect) << "Scroll screens: query failed, daemon may not be running";
                }
            });
}

QStringList ScrollHandler::trackedWindowsOnScreen(const QString& screenId) const
{
    QStringList ids;
    for (auto it = m_notifiedWindowScreens.cbegin(); it != m_notifiedWindowScreens.cend(); ++it) {
        if (it.value() == screenId) {
            ids.append(it.key());
        }
    }
    return ids;
}

void ScrollHandler::slotScrollScreensChanged(const QStringList& screenIds)
{
    const QSet<QString> updated(screenIds.cbegin(), screenIds.cend());
    const QSet<QString> added = updated - m_scrollScreens;
    const QSet<QString> removed = m_scrollScreens - updated;

    // Screens leaving scroll mode: drop their tracked windows from the engine
    // first — while m_scrollScreens still marks them scroll, since
    // onWindowClosed gates its report on that. The windows stay put; another
    // placement mode now owns them.
    for (const QString& screenId : removed) {
        const QStringList stale = trackedWindowsOnScreen(screenId);
        for (const QString& windowId : stale) {
            onWindowClosed(windowId, screenId);
        }
    }

    m_scrollScreens = updated;

    // Screens entering scroll mode: batch-report the windows already on them
    // so a runtime layout switch or a hotplugged scroll-mode monitor adopts
    // existing windows, not only ones opened afterwards.
    if (!added.isEmpty()) {
        notifyWindowsAddedBatch(KWin::effects->stackingOrder());
    }
    qCDebug(lcEffect) << "Scroll screens updated:" << m_scrollScreens;
}

} // namespace PlasmaZones
