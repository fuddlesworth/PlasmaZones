// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Monocle, windowed-fullscreen and column-maximize lifecycle for TilingHandler.
//
// Split out of state.cpp by concern. These THREE share one invariant and belong
// together: each maintains an OWNERSHIP LEDGER of a compositor state PlasmaZones
// imposed on a window (KWin maximize for monocle, KWin fullscreen plus a
// keep-flag demotion for windowed fullscreen, KWin maximize again for a
// maximized scroll column), and in every case the effect is the only thing that
// can hand that state back. Membership is therefore shed only on an arm that
// actually restores, never merely because the window's situation changed. See
// unmaximizeMonocleWindow for why the fullscreen guard sits above the removal
// rather than below it — releaseMaximizedToEdges follows the same shape, and its
// body records what went wrong when it did not.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QList>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Monocle helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool TilingHandler::unmaximizeMonocleWindow(const QString& windowId)
{
    if (!m_monocleMaximizedWindows.contains(windowId)) {
        return false;
    }
    // EXACT resolve: a stale monocle entry whose window is gone must restore
    // nothing — the fuzzy appId fallback would un-maximize an unrelated
    // same-app sibling under suppression, invisibly.
    KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
    if (!w) {
        m_monocleMaximizedWindows.remove(windowId);
        return false;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        m_monocleMaximizedWindows.remove(windowId);
        return false;
    }
    // KWin's maximize() has NO fullscreen conditional: called on a
    // still-fullscreen window it takes both "no longer maximized" branches
    // and moveResizes to geometryRestore, shrinking a fullscreen game or
    // video out of its presentation. Skip on the REQUESTED bit alone, not on
    // the union with the committed one.
    //
    // The union looked safer and was not. This project is Wayland-only, where
    // the committed bit trails a client round-trip, so after our own
    // setFullScreen(false) the requested bit reads false while the committed
    // one is still true. A union skips the whole exit gap, which is exactly
    // the window in which the maximize restore is owed and correct. The enter
    // gap is still covered: there the requested bit is true first, so this
    // guard fires before the surface ever commits, and a genuinely presenting
    // client has both bits set and skips too. Requested-false means fullscreen
    // is going away no matter who asked, and that is when we want the bit back.
    //
    // MEMBERSHIP IS RETAINED ON THIS ARM, deliberately, and it is why the
    // remove above became a contains. Shedding it here while the window is
    // still KWin-MaximizeFull would strand it with nothing left owning the
    // flag — byte for byte the defect that removing the fullscreen-enter drop
    // was meant to end. And nothing would take it back: the sole insert site
    // (the monocle tile batch) is gated on the window NOT already being
    // maximized, so "the next batch re-establishes membership" is false.
    // Holding the entry means the next call on a non-fullscreen window does
    // the real restore.
    if (kw->isRequestedFullScreen()) {
        return false;
    }
    m_monocleMaximizedWindows.remove(windowId);
    // The write below is a no-op on a window KWin already reports restored
    // (maximize() emits nothing), so no windowMaximizedStateAboutToChange
    // refreshes the captured departure rect and the caller must not anchor a
    // maximize leg on it. Same test releaseMaximizedToEdges makes.
    const bool wroteRestore = kw->requestedMaximizeMode() != KWin::MaximizeRestore;
    // maximize() emits windowFrameGeometryChanged SYNCHRONOUSLY, and the
    // restore rect can sit in a different virtual-screen region of the same
    // monitor. Without the geometry-apply gate that edge takes the
    // VS-crossing path (handleWindowOutputChanged -> windowClosed +
    // notifyWindowAdded) and tears down whatever float/tile transition the
    // caller is mid-way through. Save/restore rather than set/clear so the
    // guard nests inside already-guarded callers.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
    // The gate suppressed the VS-crossing detectors, whose early return sits
    // BEFORE their m_trackedScreenPerWindow write — and unlike a daemon
    // apply this move is not transient, so the tracker must be re-seeded
    // here (the pairing daemon_apply.cpp documents). Post-move resolve is
    // authoritative on this path: no daemon rotation is in flight, so the
    // restore rect's position is the answer.
    m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    return wroteRestore;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Column maximize (scrolling)
// ═══════════════════════════════════════════════════════════════════════════════

bool TilingHandler::isScrollTiledWindow(const QString& windowId, KWin::EffectWindow* w) const
{
    if (windowId.isEmpty() || !isTiledWindow(windowId)) {
        return false;
    }
    // The screen the strip placed it on, not wherever it sits this instant: a
    // window mid-way through a cross-screen move has already been notified for
    // its destination, and that is the strip that owns its column.
    QString screenId = m_notifiedWindowScreens.value(windowId);
    if (screenId.isEmpty() && w) {
        screenId = m_effect->getWindowScreenId(w);
    }
    return isScrollingScreen(screenId);
}

void TilingHandler::applyMaximizeSuppressed(KWin::Window* kw, KWin::MaximizeMode mode)
{
    if (!kw) {
        return;
    }
    // Counter for the same reason applyFullScreenSuppressed uses one: the
    // batch consumer and the interception arm nest their own brackets, and a
    // plain set/clear would hand an outer scope back an un-suppressed window.
    // RAII rather than a bare ++/--, matching the inGeometryApply guards its
    // callers hold. The counter's failure mode is silent and permanent: leak
    // one increment and isSuppressingMaximizeChanged() answers true for the
    // rest of the session, so the interception never fires again.
    ++m_suppressMaximizeChanged;
    const auto suppressGuard = qScopeGuard([this] {
        --m_suppressMaximizeChanged;
    });
    // AUTHORSHIP STAMP, and the reason it is not just the counter above.
    //
    // The counter is held only across the call, which answers on X11 where
    // maximize() re-enters windowMaximizedStateChanged synchronously — but on
    // Wayland the committed echo arrives a client round trip later with the
    // counter back at 0, the same asymmetry interceptMaximizeRequest's
    // already-agrees arm exists to absorb. And the marker is armed ABOVE that
    // skip in any case, so the counter never reaches it on either platform.
    // Either way it would arm for the effect's own writes and hand the next
    // unrelated placement a maximize leg it never earned. A stamp outlives the
    // bracket, so the edge this write produces is recognised as the effect's
    // wherever the echo lands.
    //
    // WRITTEN ONLY WHEN THE WRITE CAN PRODUCE THE EDGE ITS CONSUMER READS, and
    // this test has to mirror that consumer exactly rather than merely ask
    // whether the mode changed.
    //
    // noteMaximizeEdge sits BELOW the maximized lambda's full-maximize edge
    // filter, so it is reached only when `horizontal && vertical` actually
    // flips. A write that changes the mode without flipping that bit —
    // cancelAxisOnlyMaximize restoring a quick-tiled window from
    // MaximizeVertical, or the batch clearing a stray half-maximize before
    // tiling — emits an axis-only edge that returns before the consumer. A
    // stamp left by one of those has nothing to take it back off, and the next
    // genuine USER maximize inside the deadline would consume it and be
    // swallowed: no marker armed, snapIn played, which is the exact failure
    // this whole mechanism exists to fix. Comparing the fully-maximized bit on
    // both sides admits every write that flips it — Restore→Full, partial→Full,
    // Full→Restore, Full→partial — and refuses the two that cannot reach the
    // consumer, partial→Restore and Restore→partial. No caller writes a partial
    // mode today; the rows are enumerated because what makes this correct is
    // the correspondence with the consumer, not the current caller set.
    //
    // The gesture flags for the same reason: the consumer's call site skips
    // arming under an interactive move or resize, so a write issued during one
    // would strand its stamp too. Exact on X11, where the echo is synchronous
    // and both tests read the same instant. On Wayland a round trip separates
    // them, so a gesture that starts or ends inside it is the residual gap —
    // every caller already refuses to write mid-gesture, which leaves only a
    // stamp stranded by a drag begun in that window, bounded by the deadline.
    //
    // Read from the consumer's OWN state, `lastFullyMaximized`, and not from
    // this window's requested or committed mode. The sibling maximize
    // DECISIONS in this file all read requested, and rightly — they are
    // deciding what the window should hold. This is not a decision about the
    // window; it is a prediction of what one signal handler will do, and the
    // only way to make a prediction exact is to evaluate the handler's own
    // test. Reading requested instead leaves a gap wherever requested and
    // committed disagree (a user's maximize in flight when the batch clears a
    // stray half-maximize, an effect reload mid-transition), and each side of
    // that gap is a defect: a stamp with no edge swallows the user's next
    // genuine maximize, an edge with no stamp arms a marker the effect caused.
    //
    // It also subsumes the pre-written echo. noteMaximizeDemotedForSnap forces
    // this value false so the demote's echo reads as a no-edge; the gate now
    // reads the same false and declines to stamp, so that write needs no
    // special-case cleanup on the other side.
    //
    // Before the call, not after: on X11 the handler has already run by the
    // time maximize() returns.
    if (KWin::EffectWindow* ew = kw->effectWindow(); ew && !ew->isUserMove() && !ew->isUserResize()) {
        if ((mode == KWin::MaximizeFull) != m_effect->m_shaderManager.lastFullyMaximized(ew)) {
            m_effect->m_shaderManager.noteEffectAuthoredMaximizeWrite(ew);
        }
    }
    kw->maximize(mode);
}

void TilingHandler::demoteMaximizeForSnapPlacement(KWin::EffectWindow* w, const QRect& zoneRect)
{
    if (!w || w->isDeleted() || !zoneRect.isValid()) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // REQUESTED mode, matching every sibling maximize write in this file: on
    // Wayland the committed bit trails a client round-trip, and the window the
    // demote exists for (snapped straight out of a maximized state) is exactly
    // the one mid-transition.
    if (kw->requestedMaximizeMode() == KWin::MaximizeRestore) {
        return;
    }
    // Engine-owned maximize claims are handed back by their own release arms
    // (the ledger contract at the top of this file). A snap placement for such
    // a window arrives only after untrack has already routed the claim through
    // its release; a demote racing ahead of that would clear a bit the ledger
    // still records as held.
    const QString windowId = m_effect->getWindowId(w);
    if (!windowId.isEmpty()
        && (m_monocleMaximizedWindows.contains(windowId) || m_maximizedToEdgesWindows.contains(windowId))) {
        return;
    }
    // Fullscreen and gesture guards, the pair every sibling maximize write
    // carries: maximize() has no fullscreen conditional and would moveResize a
    // presenting surface down to its restore rect, and mid-gesture it snaps
    // the window out from under the user's pointer. The ApplySnap caller
    // cancels its interactive move before calling here, so a still-set flag
    // means a gesture this placement does not own.
    if (kw->isRequestedFullScreen() || w->isUserMove() || w->isUserResize()) {
        return;
    }
    qCInfo(lcEffect) << "Demoting KWin maximize for snap placement of" << windowId << "into" << zoneRect;
    // On Wayland the committed echo of this restore arrives with the
    // suppression counter back at 0 (the platform split window_connections.cpp
    // documents for the monocle writes) and would read as a genuine
    // unmaximize edge: the maximize lambda would replay a WindowMaximize
    // morph anchored at the full-monitor pre-frame over the snap-in leg.
    // Pre-write the edge tracker so the echo takes the no-edge branch — its
    // only side effect, cancelAxisOnlyMaximize, no-ops for a window that is
    // not scroll-tiled. That branch skips two writes the edge branch owed
    // this transition, so pay them here: drop any pending morph a just-prior
    // genuine maximize edge armed (the zone-rect commit would otherwise
    // complete it through the geometry hook and replay the same wrong
    // morph), and refresh the IsMaximized rule verdict.
    m_effect->m_shaderManager.noteMaximizeDemotedForSnap(w);
    m_effect->invalidateRuleCacheForStateChange(windowId);
    // Zone rect FIRST, so the restore moveResize inside maximize() lands
    // directly on the placement's target — never on a stale restore rect
    // whose screen the window is not being placed on (the cross-screen
    // teleport this method exists to prevent; see the declaration).
    kw->setGeometryRestore(KWin::RectF(zoneRect));
    // maximize() emits windowFrameGeometryChanged SYNCHRONOUSLY — the same
    // edge unmaximizeMonocleWindow guards. The rect it moves to is the zone
    // rect on the placement's own screen, so no tracker re-seed is owed
    // here: the zone apply that follows immediately re-stamps the tracked
    // screen through its own frame-change processing (for an X11 client the
    // committed rect can differ by constrainTileGeometry's centring shift, a
    // one-apply transient that same processing heals). Save/restore so the
    // bracket nests inside ApplySnap's.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    // No authorship stamp is owed here, and none is written: the pre-write
    // above forced lastFullyMaximized false, and applyMaximizeSuppressed's gate
    // reads that same value, so a Restore write against it registers as no edge
    // and declines. The echo it produces likewise takes the pre-written
    // no-edge branch and never reaches noteMaximizeEdge. Both sides decline
    // together, which is the property the gate is written to have — and the
    // reason this path needs no cleanup of its own on the other side.
    applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
}

void TilingHandler::reconcileMaximizeAfterGesture(KWin::EffectWindow* w)
{
    // Pay the claims the batch arms took but could not apply mid-gesture.
    //
    // Both maximize arms insert membership BEFORE their compositor call and
    // then skip that call while the window is under a user move or resize,
    // because maximize() moveResizes and the geometry apply that would
    // override it defers during a drag. The ledger therefore says the effect
    // holds a bit KWin does not, and that disagreement is read: the maximize
    // interception computes the engine's state from membership and compares
    // KWin's bit against it, so a click in the interim reads as already
    // agreeing and is claimed without ever reaching the engine.
    //
    // Nothing else closes it. The gesture end replays geometry only, and the
    // engine emits on change, so a drag that moves no column produces no
    // batch to re-resolve against.
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // Through the same predicate the Apply arms use, not a bare maximize: a
    // window that entered fullscreen during the gesture must still be skipped,
    // and the gesture flags must genuinely be clear by now.
    if (kw->isRequestedFullScreen() || w->isUserMove() || w->isUserResize()) {
        return;
    }
    const bool owesMaximizedToEdges = m_maximizedToEdgesWindows.contains(windowId);
    const bool owesMonocle = m_monocleMaximizedWindows.contains(windowId);
    if (!owesMaximizedToEdges && !owesMonocle) {
        return;
    }
    // A maximize-to-edges claim is only owed while the window is still a tile
    // on a scrolling screen, and a DRAG-TO-FLOAT gesture ends with membership
    // held but the strip gone: applyFloatCleanup runs at the START of the drag,
    // when releaseMaximizedToEdges takes its mid-gesture retain and keeps the
    // entry, while the tiled record is dropped in the same pass.
    //
    // Start, not drop, and it is worth naming where: the DragTracker::dragStarted
    // handler's synchronous managed-screen fast path calls handleDragToFloat
    // (lifecycle_wiring_drag.cpp), which is applyFloatCleanup's only caller on
    // that path, and dragStarted is emitted straight out of
    // windowStartUserMovedResized. Scrolling screens are excluded from the
    // Reorder suppression there, so a strip tile always takes it. The drag END
    // also floats (drag_end.cpp's ApplyFloat arm), but that runs off the async
    // endDrag reply and so lands AFTER this function, which the compositor calls
    // synchronously from windowFinishUserMovedResized. Re-applying
    // MaximizeFull here would maximize the window the user just floated, and
    // permanently — a floater gets no batch, so no Release arm can ever undo
    // it. Pay the release the mid-drag skip owed instead: the gesture flags are
    // clear by now, so it performs the real restore and sheds the entry.
    //
    // Monocle needs no such gate: unmaximizeMonocleWindow has no gesture
    // retain, so a monocle member reaching here still holds a live claim.
    if (owesMaximizedToEdges && !isScrollTiledWindow(windowId, w)) {
        releaseMaximizedToEdges(windowId, w);
        if (!owesMonocle) {
            return;
        }
    }
    if (kw->requestedMaximizeMode() == KWin::MaximizeFull) {
        return;
    }
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    applyMaximizeSuppressed(kw, KWin::MaximizeFull);
    m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
}

void TilingHandler::cancelAxisOnlyMaximize(KWin::EffectWindow* w)
{
    // Puts KWin's bit back to the engine's state, the way the maximize
    // interception does only on a REFUSED request — but unconditionally, and
    // with no dispatch.
    //
    // An axis-only maximize (KWin's quick tile) never reaches the interception,
    // because the caller's edge filter only passes a change in the FULLY
    // maximized state. On a scroll-managed tile nothing else takes the bit back
    // either: the batch arm that clears a stray partial maximize needs a batch,
    // and the engine emits on change, so a quick tile that moves no column
    // schedules none. Put the bit back to whatever the engine last said and
    // stop there — the engine has no half-maximize to express, so there is
    // nothing to dispatch and asking it would turn a quick tile into a column
    // maximize.
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (!isScrollTiledWindow(windowId, w)) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // Mid-gesture is a skip, the guard every sibling compositor-touching
    // maximize write carries: maximize() moveResizes, and the geometry apply
    // that would override it defers during a drag, so writing here snaps the
    // window out from under the user's pointer. A MEMBER is re-driven at the
    // gesture end by reconcileMaximizeAfterGesture; a non-member's stray
    // half-maximize is left for the next batch, the same bounded staleness the
    // batch Apply arm accepts for the same reason.
    if (w->isUserMove() || w->isUserResize()) {
        return;
    }
    const KWin::MaximizeMode restored =
        m_maximizedToEdgesWindows.contains(windowId) ? KWin::MaximizeFull : KWin::MaximizeRestore;
    if (kw->maximizeMode() == restored) {
        return;
    }
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    applyMaximizeSuppressed(kw, restored);
}

bool TilingHandler::interceptMaximizeRequest(KWin::EffectWindow* w)
{
    if (!w || w->isDeleted()) {
        return false;
    }
    // A held gesture is never a maximize REQUEST, so there is nothing here to
    // redirect. The user cannot press a maximize button mid-drag; the only
    // fully-maximized edge that arrives with these flags set is KWin's own
    // restore-on-drag, fired when the user pulls a maximized window off its
    // rect. For a member that edge disagrees with the engine state, so the
    // already-agrees arm below does not catch it and the handler would claim
    // the event and dispatch a toggle the user never asked for, un-maximizing
    // the column mid-drag. The refusal reply's own bit write would then land
    // under the pointer, which is the skip every sibling compositor-touching
    // maximize write in this file takes for the same reason: maximize()
    // moveResizes, and the geometry apply that would override it defers during
    // a drag.
    //
    // Declining does not strand the ledger, and that is what makes it safe
    // without knowing how the drag's own float cleanup orders against this
    // edge. KWin clears the bit while membership stands, and every gesture ends
    // in reconcileMaximizeAfterGesture (window_connections.cpp's
    // windowFinishUserMovedResized, the one point that always runs): a window
    // still tiled on its strip has the claim re-driven there, and one the drag
    // took off the strip is routed to releaseMaximizedToEdges instead, which
    // sheds the entry with the gesture flags now clear. Both arms are reached
    // whichever way the ordering falls.
    if (w->isUserMove() || w->isUserResize()) {
        return false;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (windowId.isEmpty()) {
        return false;
    }
    // A FLOATED window on a scrolling screen is not a tile, and float is the
    // one genuine pass-through: the user took it out of the strip, so its
    // maximize is KWin's business and the engine has no column to act on.
    if (!isTiledWindow(windowId)) {
        // Logged because it is indistinguishable from a broken maximize at the
        // screen: the user's click falls through to KWin, which maximizes to
        // its own area rather than to the strip's, and nothing else records
        // that PlasmaZones declined. The common cause is legitimate (the window
        // is floated, or its desktop resolves to no engine at all), but it is
        // exactly the confusion a support report has to be able to settle.
        qCInfo(lcEffect) << "Maximize interception: declining" << windowId
                         << "— not a tiled window, KWin keeps the request";
        return false;
    }
    // The screen the strip placed it on, not wherever it sits this instant.
    // A window mid-way through a cross-screen move has already been notified
    // for its destination, and that is the strip whose focused column the
    // verb acts on.
    QString screenId = m_notifiedWindowScreens.value(windowId);
    if (screenId.isEmpty()) {
        screenId = m_effect->getWindowScreenId(w);
    }
    if (!isScrollingScreen(screenId)) {
        qCInfo(lcEffect) << "Maximize interception: declining" << windowId << "— screen" << screenId
                         << "is not in scrolling mode, KWin keeps the request";
        return false;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return false;
    }
    // Decline BEFORE the cancel when there is no daemon to answer.
    //
    // The dispatch below is fire-and-forget and drops silently on this same
    // gate, and this function would already have claimed the event — so with
    // the daemon down or restarting the maximize button would do nothing, on
    // every click, with nothing recording that a request was lost. Declining
    // here hands the event back to KWin, whose own maximize will fight the
    // strip's rect; that is the better half of the trade, because with no
    // daemon there is no batch coming to impose one.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        qCWarning(lcEffect) << "Maximize interception: declining" << windowId
                            << "— no daemon to answer, KWin's own maximize will fight the strip's rect";
        return false;
    }
    // DISPATCH WITHOUT WRITING THE BIT. What KWin just did to the window
    // stands until the engine answers.
    //
    // This used to cancel first — put the bit back to whatever the engine last
    // said (membership) — and let the answering batch drive it to the result.
    // Correct, and it cost an extra visible jump on every press. KWin emits
    // frameGeometryChanged from inside maximize() BEFORE maximizedChanged, so
    // by the time this runs the window has already been moved once; cancelling
    // moved it a second time, and only then did the batch's window.snapIn leg
    // animate from wherever the cancel had left it. Two unanimated jumps and a
    // transition starting from the wrong origin, where the Meta+Alt+F path —
    // which never touches KWin's bit — plays one clean leg between two column
    // rects. Nothing can remove the FIRST jump from inside an effect, but the
    // second was ours.
    //
    // This is not the effect becoming a second authority, which is what
    // resolveMaximizeToEdgesAction's contract note rules out. The effect writes
    // NOTHING here and predicts nothing: it claims the event and asks. The
    // engine names the result, the batch imposes it, and the decision function
    // resolves against membership exactly as before. The window simply holds
    // the user's own request for the length of the round trip instead of
    // holding the pre-click state.
    //
    // What that costs is the REFUSED case, which the cancel used to cover for
    // free: a request nothing acts on now leaves the window in the state the
    // user asked for with no batch coming. That is why the verb reports
    // whether the strip changed rather than merely whether the call arrived,
    // and why the reply handler below puts the bit back.
    //
    // ALREADY AGREES — KWin's state is the one the engine already holds, so
    // there is nothing to toggle. This is what keeps the interception
    // idempotent against an echo that clears the edge filter: the effect's own
    // batch writes are bracketed by m_suppressMaximizeChanged on X11, but on
    // Wayland their committed signal arrives a client round-trip later with
    // the counter back at 0, and dispatching there would undo the batch that
    // authored the state.
    //
    // Claimed rather than declined: the caller must not run its maximize
    // shader for a state change this handler is redirecting.
    //
    // COALESCED while a toggle is in flight, rather than swallowed. Inside the
    // round trip KWin's bit is back at the pre-press value while membership
    // has not moved yet, so a genuine second press looks exactly like the echo
    // above and the arm claimed it and dispatched nothing — two presses netted
    // to one action. Recording it instead lets the reply act on it once the
    // first answer has settled, so a fast double-click toggles twice. The echo
    // cannot reach this arm, because an echo of the effect's own write arrives
    // only after the reply has cleared the flight entry.
    //
    // ONE press is remembered, not a queue, so a triple-click inside a single
    // round trip nets to two toggles. That is the intended ceiling: the point
    // is to keep a double-click from being swallowed, not to replay an
    // arbitrary backlog against a state each press was aimed at from a
    // different starting point.
    const KWin::MaximizeMode engineState =
        m_maximizedToEdgesWindows.contains(windowId) ? KWin::MaximizeFull : KWin::MaximizeRestore;
    if (kw->maximizeMode() == engineState) {
        const bool inFlight = maximizeToggleInFlight(windowId);
        if (inFlight) {
            m_maximizeToggleInFlight[windowId].pendingPress = true;
        }
        // Entry presence is logged separately from the live/expired answer,
        // because the case where they disagree is the one worth reading in a
        // report: a flight that expired with a press already recorded keeps
        // its entry while answering false here, so this press is neither
        // coalesced nor dispatched. (An expiry with no recorded press erases
        // the entry, so both read false there.) The press is deliberately not
        // recorded in that case — the reply may still land tens of seconds
        // later, and honouring it then would toggle the window long after the
        // user gave up on the click.
        qCInfo(lcEffect) << "Maximize interception: KWin already agrees with the engine for" << windowId
                         << "— no toggle dispatched (coalesced press:" << inFlight
                         << "flight entry standing:" << m_maximizeToggleInFlight.contains(windowId) << ")";
        return true;
    }
    qCInfo(lcEffect) << "Maximize interception: claiming" << windowId << "on" << screenId << "— dispatching toggle ("
                     << (engineState == KWin::MaximizeFull ? "un-maximize" : "maximize") << ")";
    dispatchMaximizeToEdgesToggle(screenId, windowId);
    return true;
}

bool TilingHandler::maximizeToggleInFlight(const QString& windowId)
{
    const auto it = m_maximizeToggleInFlight.find(windowId);
    if (it == m_maximizeToggleInFlight.end()) {
        return false;
    }
    // Expire on read rather than on a timer. A timer is one more thing that
    // can be lost or outlive the window, and the only reader that matters is
    // the batch arm, which runs often enough that an expired entry is
    // collected promptly.
    if (QDateTime::currentMSecsSinceEpoch() - it->armedAtMs > MaximizeToggleFlightMs) {
        // A recorded press SURVIVES expiry. The suppression this entry
        // provides is time-bounded because a latched one would disable the
        // repair arm for the session, but the user's second click is not
        // something to throw away on a slow round trip. The reply still takes
        // the entry and honours it; only the suppression lapses.
        if (it->pendingPress) {
            it->armedAtMs = 0;
            return false;
        }
        m_maximizeToggleInFlight.erase(it);
        return false;
    }
    return true;
}

void TilingHandler::dispatchMaximizeToEdgesToggle(const QString& screenId, const QString& windowId)
{
    // The reply IS consumed, unlike every other dispatch in this file, and
    // since the interception stopped writing KWin's bit this is the ONLY thing
    // standing between a refused request and a window left in a state the
    // engine never agreed to.
    //
    // False means the strip did not change, from either kind of refusal (the
    // wire note on ApiVersion 7 spells out both and why they are not
    // distinguished). No batch follows, so nothing else will ever correct the
    // state the user's click imposed.
    //
    // A LOST OR ERRORED CALL IS TREATED AS A REFUSAL, which is the opposite of
    // the sibling dispatches' fail-open and deliberate here. Those can fail
    // open because a call they lose was still probably acted on, and the batch
    // that follows is then the authority. This verb has no such backstop on
    // the outcome that matters: the refusal is exactly the case that changes
    // nothing, and a call that changes nothing emits NO tile batch, so on an
    // error there is no following batch to correct anything. Failing open
    // there leaves the window KWin-maximized against the strip's rects with
    // nothing armed to pull it back, until some unrelated change happens to
    // re-drive applyLayout for that screen.
    //
    // The write is safe on the other branch of the error. If the call WAS
    // acted on and only the reply was lost, this writes the pre-toggle state
    // and the batch that is already on its way immediately supersedes it, so
    // the cost is one transient bit write rather than a stranded window. The
    // write is also idempotent: it early-returns when KWin already agrees with
    // membership.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }
    // ARM BEFORE THE CALL. A batch can arrive before the reply, and the arm
    // that reads this is the one that must not act on it.
    //
    // RE-ARMING PRESERVES pendingPress. A second press that DISAGREES with
    // membership takes the dispatch path rather than the coalesce path, so
    // this is reachable with an entry already present, and value-overwriting
    // would silently drop a press the interception had recorded.
    MaximizeToggleFlight& flightEntry = m_maximizeToggleInFlight[windowId];
    flightEntry.armedAtMs = QDateTime::currentMSecsSinceEpoch();
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Scrolling,
                                                   QStringLiteral("toggleMaximizeToEdges"), {screenId, windowId}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* pw) {
        pw->deleteLater();
        const QDBusPendingReply<bool> reply = *pw;
        // DISARM FIRST, before any write below and on every exit.
        //
        // Ordering is load-bearing, not tidiness. The refusal write emits a
        // committed echo that re-enters interceptMaximizeRequest, and the
        // already-agrees arm there must fire for it — that is the whole
        // anti-loop argument. With the entry still armed the echo would be
        // recorded as a pending press instead and re-dispatched forever.
        //
        // Disarming on the success path too is what keeps the entry from
        // outliving the round trip it describes.
        const MaximizeToggleFlight flight = m_maximizeToggleInFlight.take(windowId);
        // A press that arrived mid-flight is honoured once the first answer
        // has settled, so a fast double-click toggles twice instead of once.
        // Re-dispatching cannot loop: only a real user press sets this, and
        // the entry it re-arms starts with pendingPress false.
        //
        // RE-RESOLVES the screen rather than reusing the one captured at
        // dispatch, and re-runs both gates. This is the only thing in the
        // lambda that sends a new verb, so it needs the round-trip re-checks
        // at least as much as the write below does: a window that changed
        // output mid-flight would otherwise have a toggle addressed to the
        // strip it left.
        const auto honourPendingPress = qScopeGuard([this, windowId, flight] {
            if (!flight.pendingPress) {
                return;
            }
            KWin::EffectWindow* pw2 = m_effect->findWindowByIdExact(windowId);
            if (!pw2 || pw2->isDeleted() || !isTiledWindow(windowId)) {
                return;
            }
            QString pendingScreenId = m_notifiedWindowScreens.value(windowId);
            if (pendingScreenId.isEmpty()) {
                pendingScreenId = m_effect->getWindowScreenId(pw2);
            }
            if (!isScrollingScreen(pendingScreenId)) {
                return;
            }
            dispatchMaximizeToEdgesToggle(pendingScreenId, windowId);
        });
        if (!reply.isError() && reply.value()) {
            return;
        }
        // The one outcome that leaves the user's click visibly unhonoured, and
        // until now the only record of it was the window snapping back.
        if (reply.isError()) {
            qCWarning(lcEffect) << "Maximize toggle refused for" << windowId
                                << "— D-Bus error:" << reply.error().message()
                                << "; restoring KWin's bit to the engine's state";
        } else {
            qCWarning(lcEffect) << "Maximize toggle refused for" << windowId
                                << "— strip reported no change; restoring KWin's bit to the engine's state";
        }
        // Refused, so put the bit back where the ENGINE has it — membership,
        // never the pre-click value and never the user's request. This is the
        // cancel the interception used to do unconditionally, now paid only on
        // the path that actually needs it.
        //
        // No pass-through marker is needed to keep this from looping, and that
        // is a property of writing the engine's state rather than replaying the
        // user's. The write is suppressed, so X11's synchronous emission is
        // covered by the counter; on Wayland the committed echo arrives with
        // the counter back at 0 and re-enters the interception, where the
        // already-agrees arm now fires by construction — the bit was just set
        // to exactly what membership says. The old replay wrote the USER's
        // request instead, which by definition disagrees with membership, so
        // it fell through and dispatched again once per round trip forever.
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
        KWin::Window* kw = w ? w->window() : nullptr;
        if (!w || w->isDeleted() || !kw) {
            return;
        }
        // RE-RUN the two gates the interception took before dispatching. A
        // full round trip separates them, and the window can have floated out
        // of the strip, been untracked, or moved to a non-scrolling screen in
        // between. Without these the bit gets written from column membership
        // for a window that is no longer a column member, so a user who floats
        // a window and maximizes it mid-flight has it silently un-maximized.
        if (!isTiledWindow(windowId)) {
            return;
        }
        QString replyScreenId = m_notifiedWindowScreens.value(windowId);
        if (replyScreenId.isEmpty()) {
            replyScreenId = m_effect->getWindowScreenId(w);
        }
        if (!isScrollingScreen(replyScreenId)) {
            return;
        }
        const KWin::MaximizeMode engineState =
            m_maximizedToEdgesWindows.contains(windowId) ? KWin::MaximizeFull : KWin::MaximizeRestore;
        if (kw->maximizeMode() == engineState) {
            return;
        }
        // Fullscreen and gesture guards, the pair every sibling maximize write
        // in this file carries: maximize() has no fullscreen conditional and
        // would moveResize a presenting surface down to its restore rect, and
        // mid-gesture it snaps the window under the user's pointer.
        if (kw->isRequestedFullScreen() || w->isUserMove() || w->isUserResize()) {
            return;
        }
        // maximize() emits windowFrameGeometryChanged synchronously on X11 and
        // moveResizes to the restore rect, which can sit in a different
        // virtual-screen region — the same edge unmaximizeMonocleWindow guards.
        // Save/restore so the guard nests inside an already-guarded caller.
        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
        m_effect->m_daemonGate.inGeometryApply = true;
        const auto geomGuard = qScopeGuard([this, prevInApply] {
            m_effect->m_daemonGate.inGeometryApply = prevInApply;
        });
        applyMaximizeSuppressed(kw, engineState);
        // Tracker re-seed, pairing with the suppressed VS-crossing detectors
        // exactly as unmaximizeMonocleWindow does.
        m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    });
}

bool TilingHandler::releaseMaximizedToEdges(const QString& windowId, KWin::EffectWindow* w)
{
    if (!m_maximizedToEdgesWindows.contains(windowId)) {
        return false;
    }
    // isDeleted before window(), because callers now pass a pointer straight
    // out of signal scope rather than one they re-resolved: a closing window
    // answers a stale KWin::Window* that must not be moveResized.
    KWin::Window* kw = (w && !w->isDeleted()) ? w->window() : nullptr;
    if (!kw) {
        // Nothing left to hand the bit back to, so the entry is dead weight.
        m_maximizedToEdgesWindows.remove(windowId);
        return false;
    }
    // Fullscreen guard, for the reason unmaximizeMonocleWindow spells out:
    // maximize() has no fullscreen conditional, so on a still-fullscreen
    // window it moveResizes down to geometryRestore and shrinks a presenting
    // surface. Reachable here because windowed fullscreen and column
    // maximize are deliberately NOT exclusive.
    //
    // MEMBERSHIP IS RETAINED ON THIS ARM, the monocle ledger's choice rather
    // than its opposite. Shedding it here while the window is still
    // KWin-MaximizeFull strands the bit with nothing owning it, and the
    // "next batch re-asserts through the Apply arm" reasoning does not hold
    // for this function: Apply requires flagOnWire, and BOTH callers reach
    // this only when the engine has already dropped the maximize (the batch
    // Release arm by definition, cleanupAutotileTracking on untrack), so
    // Apply can never re-fire on a path that gets here. Retaining instead
    // leaves resolveMaximizeToEdgesAction answering Release on each following
    // batch until the client leaves fullscreen, which is the self-heal the
    // Apply arm's own fullscreen skip relies on in the other direction.
    //
    // That self-heal needs a following batch, so it does NOT cover the callers
    // that end strip membership outright (the untrack funnel, the float
    // funnels): for those the entry is held until the fullscreen-exit repair
    // in slotWindowFullScreenChanged, or until a teardown restore. Retaining
    // is still the better half of that trade — the old shed left the same bit
    // stranded with nothing even recording that we owed it.
    // Mid-gesture is a retain too, on the same terms as the fullscreen skip
    // and for the reason the batch Apply arm spells out: maximize() moveResizes
    // to the restore rect, and the geometry apply that would override it defers
    // during a drag, so releasing here snaps the window out from under the
    // user's pointer.
    //
    // WHEN the retained entry is paid depends on where the drag ends. A window
    // still on its strip is paid by the next batch that carries it, and the
    // engine emits on change, so a drag that leaves the strip alone schedules
    // none — the same bounded staleness the Apply arm accepts in the other
    // direction. A window the drag FLOATED off the strip gets no batch at all,
    // and reconcileMaximizeAfterGesture pays that case at the gesture end: it
    // re-tests strip membership and routes a claim with no strip left back
    // here, with the gesture flags now clear.
    if (kw->isRequestedFullScreen() || w->isUserMove() || w->isUserResize()) {
        return false;
    }
    m_maximizedToEdgesWindows.remove(windowId);
    if (kw->requestedMaximizeMode() == KWin::MaximizeRestore) {
        return false;
    }
    // maximize() emits windowFrameGeometryChanged SYNCHRONOUSLY and moves to
    // the restore rect, which can sit in a different virtual-screen region —
    // the same edge unmaximizeMonocleWindow guards. Load-bearing here and not
    // merely symmetric: the cleanupAutotileTracking caller runs on the
    // cross-output transfer path holding no guard of its own, which is
    // precisely the case the VS-crossing detector reacts to. Save/restore so
    // the guard nests inside an already-guarded caller such as the batch.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
    // The gate suppressed the VS-crossing detectors, whose early return sits
    // BEFORE their m_trackedScreenPerWindow write, and this move is not
    // transient — the same pairing unmaximizeMonocleWindow documents.
    m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    return true;
}

TilingHandler::ClaimReleaseResult TilingHandler::releaseAllClaims(const QString& windowId, KWin::EffectWindow* w,
                                                                  ScrollDecisions::ClaimScope scope)
{
    using ScrollDecisions::Claim;
    using ScrollDecisions::claimReleasesOn;

    ClaimReleaseResult result;

    // Order is claimReleaseOrder's, spelled out rather than sorted: three
    // claims do not justify a sort, and writing them in order keeps the reason
    // readable. Windowed fullscreen FIRST — both maximize releases skip a
    // window that still holds fullscreen, and on X11 setFullScreen has already
    // landed by the time they run, so this order is what lets a window holding
    // both get a real restore instead of a skip.
    static_assert(ScrollDecisions::claimReleaseOrder(Claim::WindowedFullscreen)
                          < ScrollDecisions::claimReleaseOrder(Claim::MonocleMaximize)
                      && ScrollDecisions::claimReleaseOrder(Claim::WindowedFullscreen)
                          < ScrollDecisions::claimReleaseOrder(Claim::MaximizedToEdges),
                  "windowed fullscreen must be released before either maximize claim");

    if (claimReleasesOn(Claim::WindowedFullscreen, scope)) {
        // Membership OR a surviving layer snapshot — the guard
        // applyPassiveFloatShed argued for, and the one place this funnel
        // unifies rather than reproduces: a lone snapshot means membership was
        // dropped by a path that never called the release, so the release is
        // still owed. releaseWindowedFullscreenState is idempotent and
        // deliberately does not consult membership, so re-driving it off the
        // snapshot is safe everywhere, not only there.
        const bool hadMembership = m_effect->m_windowedFullscreenWindows.contains(windowId);
        // OUTSIDE the guard below, which is where both migrated sites had it:
        // a marker outliving its hold can only refuse a future adopt, and the
        // case that needs it most is exactly the one the guard rejects. On the
        // close half of the untrack funnel slotWindowClosed has already removed
        // membership and the release has already erased the snapshot, so the
        // guard answers no while an armed marker is still sitting there — and
        // window ids are appId-derived and reusable, so it would refuse the
        // adopt of whatever reuses the id.
        m_windowedFsClearInFlight.remove(windowId);
        if (hadMembership || m_effect->m_windowedFsLayerSnapshots.contains(windowId)) {
            if (hadMembership) {
                forgetWindowedFullscreen(windowId);
            }
            releaseWindowedFullscreenState(windowId);
            result.windowedFullscreen = true;
        }
    }
    if (claimReleasesOn(Claim::MonocleMaximize, scope)) {
        // What was HANDED BACK, not what was held: both maximize releases
        // RETAIN membership when they skip a still-fullscreen window, so a
        // pre-read contains() reports true for a claim the effect is still
        // holding. The struct documents itself as what the call actually
        // released, and a caller gating a repaint or a decoration re-resolve
        // on it would act on a bit that never moved.
        const bool hadMonocle = m_monocleMaximizedWindows.contains(windowId);
        unmaximizeMonocleWindow(windowId);
        result.monocle = hadMonocle && !m_monocleMaximizedWindows.contains(windowId);
    }
    if (claimReleasesOn(Claim::MaximizedToEdges, scope)) {
        const bool hadMaximizedToEdges = m_maximizedToEdgesWindows.contains(windowId);
        releaseMaximizedToEdges(windowId, w);
        result.maximizedToEdges = hadMaximizedToEdges && !m_maximizedToEdgesWindows.contains(windowId);
    }
    return result;
}

void TilingHandler::restoreAllMaximizedToEdges()
{
    if (m_maximizedToEdgesWindows.isEmpty()) {
        return;
    }
    // Snapshot and clear FIRST — same iterator-invalidation hazard as
    // restoreAllMonocleMaximized: maximize() can synchronously re-enter
    // cleanupClosedWindowState through the output-changed path.
    const QStringList ids = m_maximizedToEdgesWindows.values();
    m_maximizedToEdgesWindows.clear();
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    for (const QString& wid : ids) {
        // EXACT resolve — the fuzzy appId fallback would un-maximize an
        // unrelated same-app sibling under suppression, invisibly.
        //
        // A MISS retains the entry, on the same terms as the fullscreen skip
        // below: both mean "the bit was not handed back", and dropping one
        // while retaining the other would make the ledger's record of what we
        // owe depend on WHY we could not pay. The daemon-loss caller keeps the
        // effect running, so a window whose id does not resolve this instant
        // (the stale pre-restore-UUID window this file guards elsewhere) can
        // still be reached by a later drain. At unload nothing survives to
        // read either way.
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(wid);
        if (!w) {
            m_maximizedToEdgesWindows.insert(wid);
            continue;
        }
        KWin::Window* kw = w->window();
        if (!kw) {
            // THIRD unpaid arm, and it retains for the same reason as the
            // other two. Without this the entry falls through every `kw &&`
            // test below, hands nothing back, and is then discarded by the
            // snapshot-clear above — the ledger forgets a bit the window is
            // still holding. That is exactly the "record depends on WHY we
            // could not pay" asymmetry the comment below rules out, and it
            // was the one case that had it.
            m_maximizedToEdgesWindows.insert(wid);
            continue;
        }
        // A window still holding fullscreen is SKIPPED, and its entry goes
        // BACK rather than being discarded by the snapshot-clear above. This
        // is the same retention releaseMaximizedToEdges takes, and for the same
        // reason: dropping an entry whose bit was never handed back strands
        // that bit with nothing owning it. It matters on the daemon-loss
        // caller, where the effect keeps running and a later arm can still do
        // the real restore; at unload nothing survives to care either way.
        if (kw->isRequestedFullScreen()) {
            m_maximizedToEdgesWindows.insert(wid);
            continue;
        }
        if (kw->requestedMaximizeMode() != KWin::MaximizeRestore) {
            // Through the shared bracket rather than an inline ++/maximize/--:
            // three hand-rolled copies of this write had drifted apart, and
            // this one was the copy missing nothing but easy to break next.
            applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
            m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
        }
    }
}

void TilingHandler::restoreAllMonocleMaximized()
{
    if (m_monocleMaximizedWindows.isEmpty()) {
        return;
    }
    // Snapshot and clear FIRST: maximize() can synchronously re-enter
    // cleanupClosedWindowState (via the VS-crossing / output-changed path),
    // which mutates m_monocleMaximizedWindows — iterating the live set here
    // is iterator invalidation in a compositor loop.
    const QStringList ids = m_monocleMaximizedWindows.values();
    m_monocleMaximizedWindows.clear();
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    ++m_suppressMaximizeChanged;
    const auto suppressGuard = qScopeGuard([this] {
        --m_suppressMaximizeChanged;
    });
    for (const QString& wid : ids) {
        // EXACT resolve — same sibling hazard as unmaximizeMonocleWindow.
        //
        // RETENTION ON EVERY UNPAID ARM, matching restoreAllMaximizedToEdges.
        // The two functions are twins and used to argue opposite positions
        // about the same situation: this one dropped a skipped member on the
        // grounds that the skip "IS the effect giving up ownership", while its
        // column sibling put the entry back on the grounds that dropping a bit
        // that was never handed back strands it. The column reading is the
        // right one, and it applies here identically — on the daemon-loss and
        // engine-disable callers the effect keeps running and a later arm can
        // still pay. Worse here than there, in fact: the sole insert site is
        // gated on the window NOT already being maximized, so a dropped entry
        // is never re-established by any batch.
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(wid);
        if (!w) {
            m_monocleMaximizedWindows.insert(wid);
            continue;
        }
        KWin::Window* kw = w->window();
        if (!kw) {
            m_monocleMaximizedWindows.insert(wid);
            continue;
        }
        // Fullscreen members are skipped for the reason spelled out in
        // unmaximizeMonocleWindow: maximize() has no fullscreen conditional
        // and would moveResize a presenting surface down to its restore rect.
        if (kw->isRequestedFullScreen()) {
            m_monocleMaximizedWindows.insert(wid);
            continue;
        }
        // Through the helper, not a bare maximize(), even though the loop-wide
        // bracket above already holds the counter. The counter does not cover
        // this on EITHER platform, which is easy to misread: noteMaximizeEdge
        // is armed near the top of the maximized lambda, above the suppression
        // skip, so a bare write here armed the user maximize-edge marker for a
        // restore the effect authored — synchronously on X11, and a round trip
        // later on Wayland. The next placement of that window would then claim
        // a maximize leg it never earned. The authorship stamp the helper
        // writes is what the marker actually consults; the helper's own counter
        // bracket nests inside this one harmlessly.
        applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
        // Same tracker re-seed as unmaximizeMonocleWindow, and more
        // load-bearing here: the daemon-loss caller has no apply path left to
        // heal a stale entry.
        m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    }
}

void TilingHandler::forgetWindowedFullscreen(const QString& windowId)
{
    // Membership half only, deliberately without the compositor call: the
    // desktop-switch demote must shed membership BEFORE its geometry restore
    // (or the apply-path exemption still fires) while the setFullScreen(false)
    // has to land AFTER m_managedScreens is rewritten (or the deferred
    // committed-exit signal evaluates against the stale set) — one atomic
    // helper cannot sit on both sides of that write.
    m_effect->m_windowedFullscreenWindows.remove(windowId);
}

void TilingHandler::applyWindowedFullscreenLayerDemotion(const QString& windowId, KWin::Window* kw)
{
    // KWin promotes an active fullscreen window to the ActiveLayer — and
    // keeps it there while the active window sits on ANOTHER output
    // (Window::isActiveFullScreen) — which stacks a windowed-fullscreen
    // tile above every strip neighbour and above the daemon's own overlay
    // surfaces. Scrolling then slides the incoming column UNDERNEATH the
    // tile: seen live as the neighbour "not rendering" and windows
    // overlapping the fullscreen-presented game. The keep-below flag is
    // the one input belongsToLayer() consults BEFORE the fullscreen
    // promotion, so holding it keeps the tile stacked with its strip.
    // Snapshot-once + restore mirrors the SetWindowLayer rule discipline;
    // reconcileRuleWindowLayer skips flagged windows so the two flag
    // owners never fight mid-hold.
    // Null guard matching the restore's contract: both current callers hold
    // a checked pointer, but an unguarded deref here is a compositor crash
    // the day a third caller does not.
    if (!kw) {
        return;
    }
    if (!m_effect->m_windowedFsLayerSnapshots.contains(windowId)) {
        m_effect->m_windowedFsLayerSnapshots.insert(windowId, {kw->keepAbove(), kw->keepBelow()});
    }
    kw->setKeepAbove(false);
    kw->setKeepBelow(true);
}

void TilingHandler::restoreWindowedFullscreenLayerDemotion(const QString& windowId, KWin::Window* kw)
{
    const auto it = m_effect->m_windowedFsLayerSnapshots.find(windowId);
    if (it == m_effect->m_windowedFsLayerSnapshots.end()) {
        return;
    }
    // Erase BEFORE the setters, the reconcileRuleWindowLayer shape: they
    // emit KWin signals, and holding a QHash iterator across re-entrant
    // code is undefined the day a connection routes back into this map.
    const WindowLayerSnapshot snapshot = *it;
    m_effect->m_windowedFsLayerSnapshots.erase(it);
    if (!kw) {
        return; // window already gone; dropping the snapshot IS the cleanup
    }
    kw->setKeepAbove(snapshot.keepAbove);
    kw->setKeepBelow(snapshot.keepBelow);
}

void TilingHandler::releaseWindowedFullscreenState(const QString& windowId)
{
    // Compositor half: drop the client's KWin fullscreen state if it still
    // holds it. Deliberately does NOT consult the membership hash — callers
    // that forget first (the demote) and callers that snapshot-and-clear
    // (the bulk restore) both arrive here with the entry already gone.
    KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
    if (!w || w->isDeleted()) {
        restoreWindowedFullscreenLayerDemotion(windowId, nullptr);
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        restoreWindowedFullscreenLayerDemotion(windowId, nullptr);
        return;
    }
    // Keep-flag restore runs even when fullscreen is already gone (the
    // client may have exited on its own before this release landed).
    restoreWindowedFullscreenLayerDemotion(windowId, kw);
    // Requested-state term: during our OWN enter round-trip the committed
    // state lags behind while requested is already true (the same lag
    // drag_snap's apply bail and the self-heal arm handle explicitly). A
    // release landing inside that gap must still un-set, or the client
    // commits fullscreen with membership already gone — the apply bail then
    // rejects every geometry and the ack arm can't fire, stranding the
    // window fullscreen at full-output size with no owner.
    if (!kw->isFullScreen() && !kw->isRequestedFullScreen()) {
        return;
    }
    // Own inGeometryApply bracket: setFullScreen(false) synchronously emits
    // windowFrameGeometryChanged on X11, and none of the release call sites
    // holds the guard — without it the VS-crossing detector re-enters the
    // very teardown loop that is running. Save/restore, not set/clear: the
    // demote and funnel callers can nest inside an outer apply.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    // Through the helper, not a hand-rolled ++/--: its own note spells out why
    // the bracket has to be RAII, and this was one of the last two copies that
    // was not. An early return added between the pair leaks the increment, and
    // the failure is silent and permanent — isSuppressingFullScreenChanged()
    // then answers true for the rest of the session, so slotWindowFullScreenChanged
    // stops seeing a client leaving fullscreen on its own and windowed
    // fullscreen never reconciles again.
    applyFullScreenSuppressed(kw, false);
}

void TilingHandler::dispatchWindowedFullscreenClear(const QString& windowId)
{
    // Arm the in-flight marker: a batch the daemon emitted BEFORE
    // processing this clear can still carry flag=true (cross-direction
    // D-Bus has no ordering guarantee), and its adopt arm would
    // re-fullscreen the window the user just exited. The batch arm's
    // flag-off echo consumes the marker on success.
    //
    // Reply-gated, NOT fireAndForget: on a LOST clear the daemon keeps its
    // flag true, so every subsequent batch entry carries flag=true and the
    // flag-off echo never arrives — the marker would latch and refuse
    // re-adoption for the rest of the session. The error arm drops it so
    // the next batch can adopt again (the daemon's flag surviving an error
    // is exactly the state re-adoption reconciles).
    m_windowedFsClearInFlight.insert(windowId);
    QDBusPendingCall call = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("clearWindowedFullscreen"), {windowId});
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCWarning(lcEffect) << "clearWindowedFullscreen failed for" << windowId
                                << "- dropping in-flight marker:" << reply.error().message();
            m_windowedFsClearInFlight.remove(windowId);
        }
    });
}

void TilingHandler::restoreAllWindowedFullscreen()
{
    // The clear-in-flight markers die unconditionally, BEFORE the empty-hash
    // early return: this is the shared drain for disable, teardown, daemon
    // loss AND bring-up, and a marker surviving a daemon that died before
    // echoing its clear would silently refuse the NEW daemon's adopt batches
    // forever. Both marker-arm sites remove membership before inserting the
    // marker, so the last-member case arrives here with the hash EMPTY and
    // the marker set — a drain gated behind the early return would miss it.
    m_windowedFsClearInFlight.clear();
    if (!m_effect->m_windowedFullscreenWindows.isEmpty()) {
        // Snapshot and clear FIRST, the restoreAllMonocleMaximized shape and
        // for a sharper reason here: setFullScreen can synchronously re-enter
        // handleWindowOutputChanged → cleanupAutotileTracking, which now
        // forgets hash entries — iterating the live hash is iterator
        // invalidation in a compositor loop, and the funnel's re-entrant
        // forget must find the entry already gone.
        const QStringList ids = m_effect->m_windowedFullscreenWindows.keys();
        m_effect->m_windowedFullscreenWindows.clear();
        for (const QString& wid : ids) {
            releaseWindowedFullscreenState(wid);
        }
    }
    // Keep-flag sweep for snapshots with no membership behind them. The drain
    // above is membership-keyed, so an entry whose membership was dropped by
    // a path that did not also release would survive every teardown — and the
    // flag it owns is a real client state (setKeepBelow(true)) with nothing
    // left to hand it back. Runs OUTSIDE the membership guard on purpose: the
    // orphan case is precisely the one where the membership hash can already
    // be empty, so a sweep gated behind that early return would never see it.
    //
    // Keys snapshotted first: the release erases from this very hash and then
    // emits KWin signals through the keep-flag setters, so iterating it live is
    // the same re-entrancy hazard the drain above documents. A key the drain
    // already consumed is a no-op, and a vanished window resolves to
    // drop-the-snapshot, which is the whole cleanup for a dead client.
    //
    // releaseWindowedFullscreenState, NOT restoreWindowedFullscreenLayerDemotion:
    // the demotion restores only the keep flags. An orphan is by definition a
    // snapshot whose membership was dropped without a release, so KWin fullscreen
    // may still be held — and leaving it held is exactly the "stranded in
    // fullscreen with nothing owning the flag" state the destructor calls this
    // function to prevent. The release is a strict superset: it calls the
    // demotion itself, resolves the window on its own, and does not consult
    // membership, so it is correct for a key that has none.
    if (m_effect->m_windowedFsLayerSnapshots.isEmpty()) {
        return;
    }
    const QStringList orphanIds = m_effect->m_windowedFsLayerSnapshots.keys();
    for (const QString& wid : orphanIds) {
        releaseWindowedFullscreenState(wid);
    }
}

} // namespace PlasmaZones
