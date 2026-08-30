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
#include <QList>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Monocle helpers
// ═══════════════════════════════════════════════════════════════════════════════

void TilingHandler::unmaximizeMonocleWindow(const QString& windowId)
{
    if (!m_monocleMaximizedWindows.contains(windowId)) {
        return;
    }
    // EXACT resolve: a stale monocle entry whose window is gone must restore
    // nothing — the fuzzy appId fallback would un-maximize an unrelated
    // same-app sibling under suppression, invisibly.
    KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
    if (!w) {
        m_monocleMaximizedWindows.remove(windowId);
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        m_monocleMaximizedWindows.remove(windowId);
        return;
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
        return;
    }
    m_monocleMaximizedWindows.remove(windowId);
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
    kw->maximize(mode);
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
    // interception computes what to cancel to from membership, so a click in
    // the interim cancels TO MaximizeFull.
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
    // entry, while the tiled record is dropped in the same pass. Re-applying
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
    // The cancel half of interceptMaximizeRequest, with no dispatch.
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
    const QString windowId = m_effect->getWindowId(w);
    if (windowId.isEmpty()) {
        return false;
    }
    // A one-shot pass-through, armed when the daemon refused this window's
    // last request and we replayed the user's maximize. Consuming it here is
    // what stops the replay being cancelled and dispatched again, which would
    // loop at one D-Bus round trip per iteration.
    if (m_maximizePassThrough.remove(windowId)) {
        return false;
    }
    // A FLOATED window on a scrolling screen is not a tile, and float is the
    // one genuine pass-through: the user took it out of the strip, so its
    // maximize is KWin's business and the engine has no column to act on.
    if (!isTiledWindow(windowId)) {
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
        return false;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return false;
    }
    // Decline BEFORE the cancel when there is no daemon to answer.
    //
    // The dispatch below is fire-and-forget and drops silently on this same
    // gate, but by then the cancel has already run and this function has
    // already claimed the event — so with the daemon down or restarting the
    // maximize button did nothing AND un-maximized the window, on every click,
    // with nothing recording that a request was lost. Declining here hands the
    // event back to KWin, whose own maximize will fight the strip's rect; that
    // is the better half of the trade, because with no daemon there is no
    // batch coming to impose one.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return false;
    }
    // CANCEL, then dispatch. The bit goes back to whatever the engine last
    // said (membership), never to what the click asked for: the engine owns
    // this state and is about to answer for itself, so writing the user's
    // request here would make the effect a second authority and open the
    // race resolveMaximizeToEdgesAction's contract note says does not exist.
    //
    // The cancel is also what makes a REFUSED verb safe. toggleMaximizeToEdges
    // is a silent no-op for a screen the engine does not own or a context
    // gate that is closed, and in that case no batch follows at all — without
    // the cancel the window would simply stay KWin-maximized, fighting the
    // strip's next rect with nothing left to correct it.
    const KWin::MaximizeMode restored =
        m_maximizedToEdgesWindows.contains(windowId) ? KWin::MaximizeFull : KWin::MaximizeRestore;
    // ALREADY AGREES — nothing was requested, so nothing is redirected. This
    // is the arm that makes the interception idempotent against the echo of
    // its OWN cancel below: m_suppressMaximizeChanged covers the synchronous
    // X11 emission from inside maximize(), but on Wayland the committed
    // signal arrives a client round-trip later with the counter back at 0,
    // and without this the echo would read as a fresh user maximize and
    // dispatch a second toggle that undoes the first.
    //
    // Claimed rather than declined: the caller must not run its maximize
    // shader for a state change this handler authored.
    if (kw->maximizeMode() == restored) {
        return true;
    }
    {
        // maximize() emits windowFrameGeometryChanged synchronously on X11
        // and moveResizes to the restore rect, which can sit in a different
        // virtual-screen region — the same edge unmaximizeMonocleWindow
        // guards. Save/restore so the guard nests inside an already-guarded
        // caller.
        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
        m_effect->m_daemonGate.inGeometryApply = true;
        const auto geomGuard = qScopeGuard([this, prevInApply] {
            m_effect->m_daemonGate.inGeometryApply = prevInApply;
        });
        applyMaximizeSuppressed(kw, restored);
        // Tracker re-seed, pairing with the suppressed VS-crossing detectors
        // exactly as unmaximizeMonocleWindow does.
        m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    }
    dispatchMaximizeToEdgesToggle(screenId, windowId);
    return true;
}

void TilingHandler::consumeSuppressedMaximizePassThrough(KWin::EffectWindow* w)
{
    // X11 ONLY, and the platform test is what keeps this from stealing the
    // Wayland marker. There the refusal replay's committed echo arrives after
    // the suppression bracket has closed, so the interception is the consumer
    // and the marker has to survive every bracketed write until then. On
    // XWayland maximize() emits inside the bracket, the effect's skip arm
    // returns before the interception runs, and this is the only edge the
    // marker will ever be offered.
    if (!w || w->isDeleted() || !w->isX11Client()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }
    m_maximizePassThrough.remove(windowId);
}

void TilingHandler::dispatchMaximizeToEdgesToggle(const QString& screenId, const QString& windowId)
{
    // The reply IS consumed, unlike every other dispatch in this file.
    //
    // A lost or errored call leaves the window where the cancel put it, which
    // is the right answer for a request that never arrived. A REFUSED one is
    // different: the daemon received it, declined at the boundary (the engine
    // is not active on that screen, or the per-context gate is closed), and no
    // batch will ever follow — so the pre-click state is now permanent and the
    // maximize button does nothing on that screen, on every click, forever.
    // That is why the verb reports acceptance at all.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Scrolling,
                                                   QStringLiteral("toggleMaximizeToEdges"), {screenId, windowId}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* pw) {
        pw->deleteLater();
        const QDBusPendingReply<bool> reply = *pw;
        if (reply.isError() || reply.value()) {
            return;
        }
        // Refused. Hand the request back to KWin by replaying it, and arm a
        // one-shot pass-through so the interception does NOT cancel the replay.
        //
        // Without that marker this loops: the replayed maximize emits its own
        // state change, on Wayland the committed echo arrives with the
        // suppression counter back at 0, the interception runs again, the
        // window is still not a member so the already-agrees arm does not
        // fire, and it cancels and dispatches once more — one round trip per
        // iteration, indefinitely.
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
        KWin::Window* kw = w ? w->window() : nullptr;
        if (!kw) {
            return;
        }
        // Window-id reuse guard, and the same predicate the interception
        // itself gates on: ids are appId-derived and reusable, and this reply
        // is asynchronous, so by now the id can resolve to a DIFFERENT
        // same-app window — or to the same one after it left the strip. Either
        // way the request this replays no longer belongs to it, and arming the
        // marker would hand a genuine future edge straight through.
        if (!isScrollTiledWindow(windowId, w)) {
            return;
        }
        // The direction the USER asked for, derived the way the interception
        // derives what it cancelled to: a member's click asked to un-maximize,
        // so replaying MaximizeFull would hand back the very bit the click
        // asked to drop.
        const bool wasMember = m_maximizedToEdgesWindows.contains(windowId);
        const KWin::MaximizeMode replayed = wasMember ? KWin::MaximizeRestore : KWin::MaximizeFull;
        // The membership goes with it. A refusal means the daemon did not act,
        // so the mirror can no longer answer for this window, and leaving the
        // entry standing over a bit the replay just cleared is not merely
        // stale — two arms re-drive the bit straight off membership, so
        // cancelAxisOnlyMaximize and reconcileMaximizeAfterGesture would each
        // silently re-maximize the window the user just restored. Shedding
        // costs nothing the engine needs: its own belief is untouched by a
        // refusal, so the next batch carrying this window re-arms the
        // membership from flagOnWire. Until then the effect claims nothing it
        // cannot answer for.
        if (wasMember) {
            m_maximizedToEdgesWindows.remove(windowId);
        }
        // Replayed only when it would WRITE, and that is a question about the
        // REQUESTED mode, not the committed one: the value being raced here is
        // the interception's own cancel, issued microseconds ago and
        // pending-only until the client commits, which is the lag
        // scrolldecisions.h documents for the batch arm. Reading the committed
        // bit on Wayland can see the pre-cancel state, skip the replay, and
        // let the cancel land — turning a refused un-maximize into a maximize
        // with no ledger entry behind it. The two agree on X11.
        if (kw->requestedMaximizeMode() != replayed) {
            // The marker is a separate question with the OTHER term. It exists
            // to be spent by the echo of this write, and an echo only comes
            // when the COMMITTED mode actually changes. With the cancel still
            // unacked the committed bit can already read as the replay's
            // target, in which case nothing will echo and a marker armed here
            // would sit until it swallowed the user's next genuine edge.
            if (kw->maximizeMode() != replayed) {
                m_maximizePassThrough.insert(windowId);
            }
            applyMaximizeSuppressed(kw, replayed);
        }
    });
}

void TilingHandler::releaseMaximizedToEdges(const QString& windowId, KWin::EffectWindow* w)
{
    if (!m_maximizedToEdgesWindows.contains(windowId)) {
        return;
    }
    // isDeleted before window(), because callers now pass a pointer straight
    // out of signal scope rather than one they re-resolved: a closing window
    // answers a stale KWin::Window* that must not be moveResized.
    KWin::Window* kw = (w && !w->isDeleted()) ? w->window() : nullptr;
    if (!kw) {
        // Nothing left to hand the bit back to, so the entry is dead weight.
        m_maximizedToEdgesWindows.remove(windowId);
        return;
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
        return;
    }
    m_maximizedToEdgesWindows.remove(windowId);
    if (kw->requestedMaximizeMode() == KWin::MaximizeRestore) {
        return;
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
        kw->maximize(KWin::MaximizeRestore);
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
    ++m_suppressFullScreenChanged;
    kw->setFullScreen(false);
    --m_suppressFullScreenChanged;
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
