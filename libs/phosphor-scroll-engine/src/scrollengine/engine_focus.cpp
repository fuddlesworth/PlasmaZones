// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The compositor focus-report handler (windowFocused), split out of
// engine_lifecycle.cpp when that TU crossed the size ceiling a FIFTH time:
// the self-activation echo filter, the declined-open consume, the drag-hold
// report drop, and the detached-view hand-back all live here.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorScreens/ScreenIdentity.h>

#include "scrollenginelogging.h"

#include <iterator>

namespace PhosphorScrollEngine {

void ScrollEngine::windowFocused(const QString& rawWindowId, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        m_activeScreen = screenId;
    }
    // Self-activation echo filter, m_pendingSelfActivations' consume side
    // and the home of its contract: the effect reports EVERY activation
    // back through notifyWindowFocused, including ones this engine
    // initiated, and the round trip is asynchronous — on a rapid focus
    // scroll the strip has already advanced past the echoed window by the
    // time the report lands, and treating the stale echo as user focus
    // would rewind the active column below (the next scroll step then
    // advances from the rewound column and skips one). Entries ahead of the
    // match go with it: the effect's calls share one ordered D-Bus
    // connection, so their echoes were dropped (show desktop, window gone)
    // and can never arrive after this one. The tab-click path never queues
    // here — its activation goes out via the adaptor's focusWindowRequested,
    // never this engine's emit, so its echo still drives the strip
    // (signals.cpp documents that contract).
    if (const int selfIdx = m_pendingSelfActivations.indexOf(windowId); selfIdx >= 0) {
        // Expiry check BEFORE swallowing: an entry whose echo the compositor
        // dropped (show desktop, focus-stealing prevention) has no reclaim
        // path any more (see below), and without this it ate the FIRST real
        // click on its window. Echo round trips are milliseconds; a stamp
        // this old means the echo is dead and this report is the user.
        const qint64 queuedAt = m_pendingSelfActivationQueuedAt.value(windowId, -1);
        const bool expired = queuedAt < 0 || !m_selfActivationClock.isValid()
            || m_selfActivationClock.elapsed() - queuedAt > kSelfActivationEchoExpiryMs;
        m_pendingSelfActivations.erase(m_pendingSelfActivations.begin(),
                                       m_pendingSelfActivations.begin() + selfIdx + 1);
        for (auto stampIt = m_pendingSelfActivationQueuedAt.begin();
             stampIt != m_pendingSelfActivationQueuedAt.end();) {
            stampIt = m_pendingSelfActivations.contains(stampIt.key()) ? std::next(stampIt)
                                                                       : m_pendingSelfActivationQueuedAt.erase(stampIt);
        }
        if (!expired) {
            // The swallow is silent to every other observer; without this
            // line a report eaten here is indistinguishable in the journal
            // from one that never arrived.
            qCDebug(lcScrollEngine) << "windowFocused: swallowed self-activation echo for" << windowId;
            return;
        }
        // Fall through: adopt as genuine focus.
    }
    // Declined-open consume, m_declinedOpenFocus' read side. An
    // `openFocused = false` arrival was focused by the compositor anyway, and
    // the rewind that declined it has already asked for the prior window back;
    // adopting this report would undo that. Consumed exactly once, so the next
    // report naming the same window is a real user click and adopts below.
    //
    // Placed ahead of the reclaim on the next line ON PURPOSE: this report is
    // not the "genuine focus" the reclaim reasons about, and clearing the queue
    // here would drop the prior window's activation echo that the rewind just
    // queued, letting that echo rewind the strip a second time when it lands.
    if (m_declinedOpenFocus.remove(windowId)) {
        return;
    }
    // Deliberately NO reclaim of m_pendingSelfActivations here. An earlier
    // form cleared the queue on every genuine report, reasoning that a
    // genuine focus implies every previously-sent echo already landed. That
    // inference is false ACROSS DIRECTIONS: on a window close the compositor
    // activates its own successor pick and that genuine report is in flight
    // BEFORE this engine queues its competing self-activation (the close
    // handler's focusWindowAfter), so the queued entry's echo is still
    // legitimately in flight when the genuine report lands. The clear wiped
    // it, the echo then arrived against an empty queue, was mis-read as user
    // focus, and the two picks re-anchored the strip against each other —
    // the post-close anchor ping-pong (0↔1916 several times in two seconds,
    // re-parking live windows on every bounce). The queue stays bounded
    // without the reclaim: the prefix-drop at match above, the close-time
    // removeAll, and the kMaxPendingSelfActivations cap all still run — and
    // the dropped-echo case the reclaim used to (over-)serve is now handled
    // precisely by the per-entry expiry at the match above.
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    if (state->isFloating(windowId)) {
        // Focus-side memory for switchFocusBetweenFloatingAndTiling: a
        // genuine report naming a float is the only place the engine learns
        // the float layer holds focus, and which member holds it.
        state->setLastFloatingFocus(windowId);
        state->setFloatingHasFocus(true);
        return;
    }
    // A genuine report naming a tile means the float layer lost focus,
    // whether or not the strip's own focus slot moves below.
    state->setFloatingHasFocus(false);
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    // DETACH-ONCE (drag_preview.cpp): a live drag-insert preview on this
    // screen owns the view for the rest of the hold. screensMatch, not ==,
    // for the applyLayout guard's reason.
    const bool dragPreviewSteersView = m_dragInsertPreview
        && PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, key.screenId);
    if (dragPreviewSteersView) {
        // Not just the hand-back arms: the focusWindow call itself reanchors
        // on a column change, and a genuine mid-hold report is exactly how a
        // focus-follows-mouse hover crossing the strip's windows slides the
        // layout under a stationary cursor — the DETACH-ONCE hazard. Nothing
        // is lost by dropping the whole report: commit focuses the dropped
        // window itself, and cancel restores the captured view, so the
        // strip's focus slot at release is decided by the drag's own ending
        // either way.
        return;
    }
    const bool focusMoved = state->strip().focusWindow(windowId, params);
    // A view still detached AFTER the focus move is one no re-anchor took
    // back, and there are two ways to arrive here holding one. focusWindow
    // REFUSED the report, because it names the window the strip already calls
    // active (scrollstrip_navigation.cpp's same-column, same-tile bail); or it
    // accepted a same-COLUMN tile move, which re-anchors nothing because no
    // strip geometry moved. Both are right about the focus SLOT and wrong
    // about the VIEW. A pan detaches the view from the centering policy, and
    // the re-anchor that re-attaches it (reanchorAfterFocusChange) is reached
    // from focusWindow on a COLUMN change and on nothing else, so neither of
    // these two outcomes gets there. No later pass revisits the question
    // either: updateViewForFocus returns early while detached, so even the
    // applyLayout a desktop return runs cannot re-derive the anchor. The
    // result was that clicking the focused window, or switching away from its
    // desktop and back, did nothing at all — the whole report was dropped,
    // latch and all.
    //
    // Re-attach and let the POLICY answer, rather than re-anchoring outright.
    // Under Never/OnOverflow updateViewForFocus leaves a fully-visible column
    // alone, so a pan that kept the focused column on screen survives an
    // incidental activation — KWin re-fires windowActivated on restacking,
    // fullscreen exit and desktop switches, not only on real focus moves.
    // Under Always it re-centres, which is what that setting asks for.
    //
    // The report must NAME the active window: a minimized tile in the active
    // column is refused by focusWindow without becoming the column's active
    // tile, and that report has no claim on the view.
    const bool activeReport = state->strip().activeWindowId() == windowId;
    const bool handBackView = state->strip().viewDetached() && activeReport;
    // ATTACHED-view twin of the hand-back: the report names the active window,
    // focusWindow refused it (same column), and there is no detach latch to
    // clear — but the active column sits entirely OFF the viewport, so the
    // user just activated a window they cannot see. That state is reachable
    // without any pan: a desktop return renders from the stored per-context
    // anchor, and when that anchor and the active column disagree the column
    // renders parked off-screen while the view stays attached. Dropping the
    // report here left every click on the parked window dead (the tester's
    // "missing from the strip" windows). Let it through: applyLayout's own
    // updateViewForFocus re-anchors an attached view whose active column is
    // off-screen, under every anchor policy. Viewport INTERSECTION on
    // purpose, not full visibility — a partially visible column stays put, so
    // KWin's incidental re-activations (restacking, fullscreen exit, desktop
    // switch) still cannot nudge a view the user can see their window in.
    const int activeIdx = state->strip().activeColumnIndex();
    const bool activeOffViewport = activeReport && !state->strip().viewDetached() && activeIdx >= 0
        && !state->strip().visibleColumnIndices(params).contains(activeIdx);
    if (!focusMoved && !handBackView && !activeOffViewport) {
        // A refused report for a BACKGROUND context is not the no-op it is
        // for the current one. All three refusal tests above measured the
        // STORED strip state, but the compositor's own scroll view is keyed
        // per output, not per desktop, so what it is actually showing for
        // this strip can disagree with everything the tests trusted. Arm the
        // pending emit anyway: the desktop return then re-asserts this
        // strip's geometry unconditionally, which is exactly the repair a
        // report the model could not classify still deserves.
        //
        // Membership-gated, unlike the mutate arm below which by construction
        // has one: a refused open can leave a reverse-map key whose state holds
        // the window in neither the strip nor the float set, and a focus report
        // naming such a phantom would otherwise arm a forced full-place batch
        // for a context that never held it.
        if (key != currentKeyForScreen(key.screenId) && state->containsWindow(windowId)) {
            m_pendingFocusEmitContexts.insert(key);
        }
        return;
    }
    if (handBackView) {
        state->strip().setViewDetached(false);
    }
    // The focus change may scroll the viewport; never re-activate here (the
    // compositor initiated this focus). Background-context guard: see
    // windowClosed. The latch is cleared for a background context too (it is
    // persisted state), but only the on-screen context re-derives the anchor
    // now — a background one re-derives on the applyLayout its own desktop
    // return runs, which is the pass that used to return early.
    if (key == currentKeyForScreen(key.screenId)) {
        applyLayout(key.screenId, false);
    } else {
        // Background context: the strip's focus and anchor moved above, but
        // no geometry batch carries it — and the desktop return that brings
        // this context on screen cannot be trusted to emit one on its own.
        // Its retile's rects can all match the stored baseline (the strip is
        // returning to where it was), and the context switch's own force arm
        // is a screen-keyed flag any interleaved pass can spend. Record the
        // KEY this report belongs to; applyLayout promotes it to a forced
        // emit only when it runs with this context current, so the centering
        // from this activation survives whatever ordering the desktop switch
        // and the focus report arrive in.
        m_pendingFocusEmitContexts.insert(key);
    }
    // Focus and view anchor are persisted (serializeStripState), and
    // placementChanged is the only thing that marks DirtyScrollStrips.
    // Emitted for a background context too: the strip that changed is
    // serialized whether or not it is the one on screen right now.
    Q_EMIT placementChanged(key.screenId);
}

} // namespace PhosphorScrollEngine
