// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross-output transfer: everything that happens when a tracked window's
// KWin output changes — daemon-driven handoff echoes, the autotile↔autotile
// re-add, and the autotile→snapping size restore.

#include "tilinghandler.h"

#include "plasmazoneseffect/plasmazoneseffect.h"
#include "handlers/snaphandler.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>

#include <algorithm>
#include <memory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void TilingHandler::handleWindowOutputChanged(KWin::EffectWindow* w)
{
    if (!w || w->isDeleted()) {
        return;
    }

    const QString windowId = m_effect->getWindowId(w);
    // Re-entrancy guard, PER WINDOW: geometry changes from
    // releaseWindowTracking/notifyWindowAdded below can trigger outputChanged
    // again if tiling moves the window across screens, and re-entering for THAT
    // window mid-transfer is what must be refused. A nested call for a different
    // window is a genuine hop and is let through — refusing it dropped the hop
    // permanently, since the caller has already pre-written the tracked-screen
    // record and the detector never re-fires for the same physical move.
    if (m_outputChangeInFlight.contains(windowId)) {
        return;
    }
    m_outputChangeInFlight.insert(windowId);
    const auto guard = qScopeGuard([this, windowId] {
        m_outputChangeInFlight.remove(windowId);
    });
    const QString newScreenId = m_effect->getWindowScreenId(w);

    if (!m_notifiedWindows.contains(windowId)) {
        // A cross-mode SWAP arms windowOutputMoveExpected for the snap
        // partner migrating onto this source screen. That partner is
        // untracked effect-side, so its outputChanged lands here — its
        // arrival IS the marker's expected echo, so consume the one-shot
        // UNCONDITIONALLY (an ineligible arrival — minimized, off-desktop,
        // unmanaged screen — still IS the echo; leaving the marker armed
        // would swallow the window's next genuine outputChanged).
        m_expectedOutputMove.remove(windowId);
        // Window not tracked — but if it moved TO an autotile screen, add
        // it. The daemon already placed it via handoffReceive; the
        // notifyWindowAdded below establishes the effect-side tracking the
        // daemon does not touch, and is a no-op daemon-side (insertWindow
        // rejects an already-tracked window).
        if (m_managedScreens.contains(newScreenId) && m_effect->shouldHandleWindow(w) && !w->isMinimized()
            && w->isOnCurrentDesktop() && w->isOnCurrentActivity()) {
            // knownFreeFloating only when the border state does NOT already
            // track the window as tiled: the handoffReceive that placed it has
            // its frame sitting in the destination zone rect, and passing true
            // would push that rect as the daemon's free/float-back geometry.
            notifyWindowAdded(w, /*knownFreeFloating=*/!TilingStateHelpers::isTiledWindow(m_border, windowId));
            m_effect->updateAllDecorations();
        }
        return;
    }

    const QString oldScreenId = m_notifiedWindowScreens.value(windowId);

    // Daemon-driven handoff FIRST, and by POSITION. For a window tiled on a
    // scrolling screen, getWindowScreenId answers from the very map
    // oldScreenId was read from (the engine-authoritative override), so the
    // old-vs-new diff below is an identity for scroll windows and can never
    // route a scroll handoff — the marker is the only reliable signal.
    // Positional match alone is NOT sufficient: on a two-output layout a
    // strip parking a column onto the neighbour lands its centre exactly
    // where an armed handoff points. The second conjunct — the window must
    // no longer be scroll-TILED on the source — separates the two: a real
    // handoff's release-driven tile update (FIFO-ordered ahead of the
    // geometry echo) has already emptied the source bucket, while a
    // parking hop's window is still tiled there, so the marker survives
    // for the genuine echo and the hop falls through to the same-screen
    // return below. The source screen comes from the marker, not from
    // oldScreenId: the move's tile requests land before this echo and
    // pre-seed m_notifiedWindowScreens with the DESTINATION, so oldScreenId
    // here would name the destination bucket and the test would always say
    // "still tiled". The daemon puts the source on the wire for the arm
    // sites that run after the handoff, which are the ones where even the
    // marker's own arm-time snapshot would have been the destination.
    if (const auto expIt = m_expectedOutputMove.constFind(windowId); expIt != m_expectedOutputMove.constEnd()) {
        const QPointF cf = w->frameGeometry().center();
        const QString positional =
            m_effect->resolveEffectiveScreenId(QPoint(qRound(cf.x()), qRound(cf.y())), m_effect->windowOutput(w));
        const QString sourceScreenId = expIt.value().sourceScreenId;
        // constFind, not value(): value() materialises a copy of the whole
        // per-screen QSet just to probe one id.
        const auto srcBucket = m_border.tiledWindowsByScreen.constFind(sourceScreenId);
        const bool stillTiledOnSource = m_scrollingScreens.contains(sourceScreenId)
            && srcBucket != m_border.tiledWindowsByScreen.constEnd() && srcBucket.value().contains(windowId);
        if (expIt.value().targetScreenId == positional && !stillTiledOnSource) {
            m_expectedOutputMove.erase(expIt);
            // The cross-mode test reads the MARKER's source, not oldScreenId,
            // for the same reason stillTiledOnSource does: the destination
            // engine's placement batch pre-seeds m_notifiedWindowScreens with
            // the DESTINATION before this echo (daemon_apply's pre-seed runs
            // for snap placements too), so a scroll→snap handoff would read
            // oldScreenId == the snapping destination, fail both managed
            // tests, and fall into the no-cleanup branch — leaving the
            // window KWin-fullscreen with no owner left to clear it.
            const QString trueSource = sourceScreenId.isEmpty() ? oldScreenId : sourceScreenId;
            if (m_managedScreens.contains(positional)) {
                m_notifiedWindowScreens[windowId] = positional;
                // On a scroll→scroll handoff the SOURCE screen's commanded
                // rect survives this bookkeeping-only arm (the funnel that
                // drops it, cleanupAutotileTracking, is bypassed here), and
                // once the record above names the destination the
                // counter-assert could fight one legitimate X11 position
                // with the old screen's rect before the destination's first
                // batch overwrites it. Same hazard the funnel documents.
                //
                // The relocation hint is deliberately NOT dropped alongside
                // it, despite the two looking symmetric. By the ordering this
                // function documents above, the destination's tile requests
                // have NORMALLY already landed (that is what pre-seeded the
                // record this arm is echoing), so the entry here is usually
                // the DESTINATION's, freshly written. This arm only proves the
                // source bucket emptied, so it does not guarantee that — but
                // keeping the entry is right either way. Where the destination
                // batch has not run yet, its per-window insert-or-remove
                // corrects a stale source value as soon as it does. Where it
                // has, dropping would be permanent: the engine emits on change,
                // so with the strip at rest nothing rewrites an unchanged
                // value, and the column would be drawn at its committed rect
                // below the union of all outputs, i.e. invisible, until the
                // user scrolled it. A stale commanded rect only disarms a
                // counter-assert, which is invisible; a lost relocation is not.
                m_effect->m_scrollCommandedRects.remove(windowId);
            } else if (m_managedScreens.contains(trueSource)) {
                // Cross-MODE move: window left autotile. Drop effect-side
                // autotile tracking (daemon already relinquished via
                // handoffRelease) — else it lingers phantom.
                cleanupAutotileTracking(windowId);
            } else {
                // snap↔snap (neither side managed by this handler's engines):
                // keep the record current so later diffs measure from the
                // true screen. Belt for the transient signal-gap case where a
                // scrolling source is momentarily outside the managed union:
                // a windowed-fullscreen member landing here has no strip left
                // to clear it, so release directly.
                m_notifiedWindowScreens[windowId] = positional;
                if (m_effect->m_windowedFullscreenWindows.contains(windowId)) {
                    forgetWindowedFullscreen(windowId);
                    releaseWindowedFullscreenState(windowId);
                }
                // The per-session scroll companions go with the hold — the
                // cross-mode branch clears the same set via
                // cleanupAutotileTracking, and this branch is the same "no
                // strip left to own it" outcome by another route. The
                // relocation-delta removal changes where the paint path draws the
                // window, so it pairs with damage like every other remover.
                m_windowedFsClearInFlight.remove(windowId);
                m_effect->m_scrollCommandedRects.remove(windowId);
                if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
                    KWin::effects->addRepaintFull();
                }
            }
            m_effect->updateAllDecorations();
            return;
        }
    }

    // "Is this window a live member of some scrolling strip?" — membership
    // tested DIRECTLY, in the two places below that need the answer.
    //
    // Deliberately not scrollTrackedScreenFor: that function is not a pure
    // predicate. It emits the scrollClip LOST warning and mutates its mutable
    // dedupe set, neither of which belongs on a bookkeeping path — a probe from
    // here would either raise a paint-path diagnostic about a window nobody was
    // clipping, or consume the dedupe slot and swallow the genuine report that
    // follows. It also derives from the same computation oldScreenId does, which
    // is the circularity the bail below rejects.
    //
    // The RAW m_scrollingScreens set, deliberately (not isScrollingScreen's
    // managed intersection): both callers want the wide, fail-safe answer — the
    // union and the scrolling set arrive on independent signals. The connected-
    // output gate scrollTrackedScreenFor applies is deliberately absent for the
    // same reason: this asks who OWNS the window, not whether its clip can be
    // computed, and a window whose output vanished is still the strip's.
    const auto inScrollStrip = [this, &windowId]() {
        for (const QString& scrollScreen : std::as_const(m_scrollingScreens)) {
            const auto bucket = m_border.tiledWindowsByScreen.constFind(scrollScreen);
            if (bucket != m_border.tiledWindowsByScreen.constEnd() && bucket.value().contains(windowId)) {
                return true;
            }
        }
        // Recorded-screen fallback: screenForTiledWindow alone returns the FIRST
        // bucket in hash order and has no such fallback, so it misses exactly the
        // stale-bucket case this predicate was built for.
        return isTiledWindow(windowId) && m_scrollingScreens.contains(m_notifiedWindowScreens.value(windowId));
    };

    if (oldScreenId.isEmpty() || oldScreenId == newScreenId) {
        // Belt-and-braces drains for a marker whose move never
        // materialised (refused receive, window closed mid-move):
        //  - record already equals the marked destination → no echo left;
        //  - window is not a parked strip tile → a same-screen settle
        //    means the marked move to a THIRD screen never happened (a
        //    scroll window is exempt: its parking hops legitimately pass
        //    through here while the marker waits for the real echo).
        // Either way, drop the one-shot rather than let it swallow a later
        // genuine move's notification.
        if (const auto expIt = m_expectedOutputMove.constFind(windowId); expIt != m_expectedOutputMove.constEnd()) {
            // The scroll exemption must not outlive the output it names. This
            // drain used to run scrollTrackedScreenFor, whose connected-output
            // gate answered empty for a window whose tracked screen had been
            // unplugged, so the marker was dropped. inScrollStrip() reads the
            // membership buckets, which still name the dead screen until the
            // daemon's set update lands on its own independent signal — so
            // without this term a marker survives that window forever and
            // swallows its next genuine hop. Same gate, spelled locally, and
            // without the warning and dedupe-set mutation that made calling the
            // real predicate wrong on a bookkeeping path.
            const QString recordedScreen = m_notifiedWindowScreens.value(windowId);
            const bool trackedOutputGone = !recordedScreen.isEmpty()
                && !m_effect->connectedPhysicalIds().contains(
                    PhosphorIdentity::VirtualScreenId::extractPhysicalId(recordedScreen));
            if (expIt.value().targetScreenId == newScreenId || !inScrollStrip() || trackedOutputGone) {
                m_expectedOutputMove.erase(expIt);
            }
        }
        return; // Same screen or unknown — no transfer needed
    }

    // A window still in a SCROLLING screen's tiled bucket belongs to that
    // strip, and only the engine moves it between strips — through the marker
    // path above, which has already had its chance. Everything past this point
    // is a POSITIONAL transfer, and a strip column's position is not evidence
    // about which strip owns it: a centred column legitimately straddles the
    // screen edge, so its frame centre can sit on the neighbouring output.
    //
    // The identity check just above was the only thing that used to stop that
    // transfer, and it cannot be relied on: both oldScreenId and newScreenId
    // derive from scrollTrackedScreenFor, so the moment that answers empty for
    // any reason the two diverge and the guard opens exactly when it is
    // needed. The transfer then calls releaseWindowTracking, which wipes the
    // membership that scrollTrackedScreenFor reads, so it can never answer
    // again — and because the tile batch is emit-on-change, a focus-only
    // relayout resolving to identical rects sends nothing that would restore
    // it. One transient miss became a permanently broken clip.
    //
    // Testing membership directly is independent of that computation, so a
    // transient miss stays transient. This enforces an invariant the handler
    // already assumed rather than adding a new rule: the comment on the marker
    // block above states the old-vs-new diff "is an identity for scroll
    // windows and can never route a scroll handoff".
    //
    // The test must answer the same question scrollTrackedScreenFor answers
    // without CALLING it — oldScreenId derives from it, which is the
    // circularity the paragraph above rejects. That is the inScrollStrip
    // predicate defined above: "in ANY scrolling bucket, or recorded on a
    // scrolling screen while tiled". A miss here runs the transfer, whose
    // releaseWindowTracking wipes the bucket permanently, which is why the
    // predicate is deliberately wide.
    //
    // A scroll window returning here deliberately keeps any armed
    // m_expectedOutputMove marker: the marker path is the only reliable
    // scroll transfer signal, and the same-screen drain above already
    // handles its disposal for scroll windows.
    if (inScrollStrip()) {
        return;
    }

    const bool oldIsAutotile = m_managedScreens.contains(oldScreenId);
    const bool newIsAutotile = m_managedScreens.contains(newScreenId);

    if (!oldIsAutotile && !newIsAutotile) {
        // Neither screen is autotiled — snapping unsnap is handled by the
        // effect's outputChanged. Still refresh the notified-screen record:
        // a window that left autotile via the desktop-switch float skip
        // stays tracked, and a stale screen here would make every later
        // frameGeometryChanged re-detect a phantom VS crossing.
        //
        // An armed m_expectedOutputMove marker deliberately SURVIVES this
        // arm (one of three exits that neither consume nor drain one; the
        // two scroll bails above are the others, equally deliberate): a
        // marked move still in flight can see intermediate positional
        // echoes that classify snap↔snap here, and the marker is the only
        // reliable signal left for the real echo — dropping it would break
        // the genuine handoff. The residual is benign: a marker that never
        // gets its echo is drained by the same-screen belt above on the
        // next settle, and one that later matches its target runs the
        // bookkeeping-only arm, which is what a genuine arrival at that
        // screen wants anyway.
        if (m_notifiedWindowScreens.contains(windowId)) {
            m_notifiedWindowScreens[windowId] = newScreenId;
        }
        return;
    }

    // A marker surviving to this point is SUPERSEDED: the marker-first
    // block above already consumed every daemon-owned echo (for non-scroll
    // windows newScreenId is byte-identical to its positional resolve, so
    // a matching destination cannot reach here), which leaves only a
    // marker for a move that a later genuine user move overtook. Drop it
    // and run the normal transfer.
    m_expectedOutputMove.remove(windowId);

    qCInfo(lcEffect) << "Window moved between monitors:" << windowId << oldScreenId << "->" << newScreenId;

    // Snapshot the pre-autotile geometry BEFORE releaseWindowTracking clears it
    // (its cleanup funnel sweeps the geometry out of EVERY screen bucket).
    QRectF savedPreAutotileGeo;
    QString savedPreAutotileBucket;
    if (oldIsAutotile) {
        savedPreAutotileGeo = findPreTileGeometry(windowId, &savedPreAutotileBucket);
    }

    // The predicate mirrors the re-add condition below.
    const bool willReAdd = newIsAutotile && !w->isMinimized() && w->isOnCurrentDesktop() && w->isOnCurrentActivity();

    // Minimize-float ownership must SURVIVE the transfer: releaseWindowTracking's
    // cleanup wipes it wholesale, the window is alive and still floated
    // daemon-side, and willReAdd is false for a minimized window — without
    // re-establishing the claim (or handing it to the snap handler for a
    // snap destination) nobody ever unfloats the window and it stays
    // floating until the next mode toggle. Reachable in bulk on monitor
    // hotplug and VS reconfigure.
    // isMinimizeFloated covers the in-flight unfloat interval too — a window
    // crossing screens mid-unfloat is still owned, and letting the cleanup
    // below drop it without re-establishing the claim strands it floating.
    const bool ownedMinimizeFloat = isMinimizeFloated(windowId);
    const bool wasUntiledMinimizeFloat = m_untiledMinimizeFloats.contains(windowId);
    // The in-flight half of that ownership, snapshotted separately: a window
    // mid-unfloat has already been unminimized, so isMinimized() is FALSE for
    // it and the re-establish below would skip exactly the case the comment
    // claims to cover, leaving the daemon's floating echo without an owner.
    const bool wasUnfloatInFlight = hasUnfloatInFlight(windowId);
    // And the THIRD ownership state: the post-unminimize animation grace. A
    // grace window is unminimized (so the isMinimized term below is false)
    // and not yet in flight, and the cleanup below cancels its timer — the
    // unminimize edge will never repeat, so without carrying this state the
    // window strands floating on the new screen.
    const bool wasInUnminimizeGrace = hasPendingUnminimizeUnfloat(windowId);
    // Snapshot the retry budget too: cleanupAutotileTracking erases it, and
    // the header documents that the budget must survive the ownership hop
    // (a persistently-refused unfloat must not reset to a fresh budget on
    // every cross-screen bounce).
    const int savedUnfloatBudget = unfloatRetryBudgetUsed(windowId);

    // Remove from old screen's autotile state. releaseWindowTracking, NOT
    // onWindowClosed: the window is alive and crossing outputs, so the close
    // relay's placement capture would persist the OLD screen's slot as its
    // final state.
    releaseWindowTracking(windowId, oldScreenId);

    if (ownedMinimizeFloat && (w->isMinimized() || wasUnfloatInFlight || wasInUnminimizeGrace)) {
        if (newIsAutotile) {
            m_minimizeFloatedWindows.insert(windowId);
            if (wasUntiledMinimizeFloat) {
                m_untiledMinimizeFloats.insert(windowId);
            }
            seedUnfloatRetryBudget(windowId, savedUnfloatBudget);
            if (wasUnfloatInFlight) {
                // The in-flight request named the OLD screen, and its watcher
                // was orphaned by the cleanup above. Re-issue against the new
                // screen: the fresh generation this takes retires the old
                // watcher through its own generation guard, and the window
                // lands back in m_unfloatInFlight rather than being demoted to
                // a plain marker its completion path can no longer find.
                dispatchUnminimizeUnfloat(windowId, newScreenId);
            } else if (wasInUnminimizeGrace) {
                // The grace timer died with the old screen's cleanup and the
                // unminimize edge will not repeat. The retry path re-validates
                // against the window's CURRENT screen and dispatches, so it is
                // the re-arm — but a HOP is not a refusal, and it must not spend
                // the budget: a window bounced across screens three times during
                // its grace exhausted the whole allowance without one unfloat
                // ever having been declined, and then stayed floating with the
                // marker held. Restore the budget the hop carried in, so only
                // genuine refusals (the retry timer's own re-arms, and the
                // dispatch error arms) count against it.
                scheduleUnminimizeUnfloatRetry(windowId);
                if (savedUnfloatBudget > 0) {
                    m_unfloatRetryAttempts[windowId] = savedUnfloatBudget;
                } else {
                    m_unfloatRetryAttempts.remove(windowId);
                }
            }
            qCInfo(lcEffect) << "Autotile: minimize-float ownership carried across screens:" << windowId << "->"
                             << newScreenId;
        } else if (SnapHandler* snap = m_effect->snapHandler()) {
            // Snap destination: hand the record over so the unminimize edge
            // on that screen finds an owner (mirrors the deferred-commit
            // transfer in the unfloat grace path). The untiled marker is
            // deliberately NOT carried: snap has no untiled fast path, so an
            // adopted window whose rect belongs to the prior mode takes
            // snap's normal deferred grace and may visibly hop once at the
            // unminimize — accepted over teaching snap a second commit path
            // for this rare cross-screen-while-minimized case.
            // An in-flight unfloat is deliberately DEMOTED to a plain marker
            // here: snap's adopt API takes ownership of the marker only, and
            // the abandoned watcher's generation guard is this handler's, so
            // snap re-drives the unfloat from its own unminimize edge.
            snap->adoptMinimizeFloated(windowId);
            snap->seedUnfloatRetryBudget(windowId, savedUnfloatBudget);
            if (wasInUnminimizeGrace) {
                // A grace window's unminimize edge already happened and will
                // never reach snap's minimize machinery — offer it directly,
                // mirroring the deferred-commit transfer in minimizefloat.cpp
                // (refusal is impossible here: the offer only refuses
                // autotile-managed screens, and this arm is the non-autotile
                // destination).
                snap->offerMinimizeEdge(w, windowId, newScreenId);
            }
            qCInfo(lcEffect) << "Autotile: minimize-float ownership handed to snap on screen change:" << windowId
                             << "->" << newScreenId;
        } else {
            // No snap handler to hand off to: the record is dropped and the
            // unminimize edge on the destination will find no owner. Both
            // sibling arms log their outcome, so log this one rather than
            // losing the ownership silently.
            qCWarning(lcEffect) << "Autotile: minimize-float ownership dropped, no snap handler for" << windowId << "->"
                                << newScreenId;
        }
    } else if (ownedMinimizeFloat) {
        // Owned, but none of the three carry terms hold: the window is neither
        // minimized, nor mid-unfloat, nor inside the unminimize grace. In
        // practice that is a window whose retry budget ran out — the grace and
        // the retry timer are both gone, so nothing re-arms — and the cleanup
        // above has just dropped the marker. The predicate is deliberately NOT
        // relaxed: carrying a dead budget onto the new screen re-establishes an
        // ownership that can never act. Log it so the loss is legible instead of
        // silent, which is the only thing the sibling arms guarantee.
        qCWarning(lcEffect) << "Autotile: minimize-float ownership dropped on screen change for" << windowId << "->"
                            << newScreenId << "- window is not minimized, in flight or in the unminimize grace"
                            << "(retry budget used:" << savedUnfloatBudget << ')';
    }

    // Re-file the snapshot on EVERY exit, not just the re-add one. The capture
    // above is unconditional, and the cleanup that motivated it has already run,
    // so filing it only under willReAdd discarded it permanently on the two
    // other exits: the minimized / off-desktop hop onto another autotile screen,
    // and the autotile→snapping arm. handleDragToFloat then found no pre-tile
    // rect and silently skipped the size restore for the rest of the session.
    //
    // Re-filed under the CAPTURE screen's key, not the destination's. The bucket
    // key names the rect's coordinate space (see savePreTileForDesktopMove), and
    // this rect was measured on the old monitor — keying it to the new one would
    // make a later cross-screen desktop move accept a rect it exists to decline.
    // findPreTileGeometry scans every bucket, so the restore path finds it
    // either way.
    if (savedPreAutotileGeo.isValid()) {
        m_preTileGeometries[savedPreAutotileBucket][windowId] = savedPreAutotileGeo;
    }

    if (willReAdd) {
        // RE-ADD: a tiled window's current frame is its zone rect on the old
        // screen — knownFreeFloating=false lets the floating guard reject it
        // (a genuinely floating window passes the guard and keeps its float
        // geometry captured). Without this the tiled rect would be pushed with
        // overwrite=true and destroy the daemon's persisted free-back.
        // Re-add on the new autotile screen. Eligibility drift (e.g. the tiled
        // frame now sits below the user's min-size threshold) or a pending
        // close can filter it locally — a no-op then, the daemon's tile path
        // owns any follow-up.
        notifyWindowAdded(w, /*knownFreeFloating=*/false);
    } else if (oldIsAutotile && !newIsAutotile) {
        // Autotile → snapping: restore the window's original (pre-snap/pre-tile)
        // SIZE after the drag ends.  The effect-side m_preTileGeometries may
        // hold the snap zone geometry (if the window was snapped before entering
        // autotile), so we ask the daemon for the true pre-tile geometry instead.
        // If unavailable, fall back to the effect-side cache.
        QPointer<KWin::EffectWindow> safeW = w;
        const QString wid = windowId;

        // No "cancel any prior pending restore" block here (nor in the re-add
        // arm above). releaseWindowTracking already disconnected and erased this
        // window's entry through cleanupAutotileTracking, and nothing between
        // there and here spins the event loop, so the map cannot have re-acquired
        // one — the continuation that writes it only runs on a later D-Bus
        // dispatch. The generation stamp below is what supersedes a watcher whose
        // call has not come back yet, which is the case no cancel site can reach.

        // Fetch the daemon's pre-tile geometry (original size before any snapping).
        // The effect-side savedPreAutotileGeo is kept as fallback.
        const int fallbackW = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.width())) : 0;
        const int fallbackH = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.height())) : 0;

        // Supersede any continuation still in flight for this window. The
        // disconnect above only reaches a watcher that already STORED its
        // connection, i.e. one that has replied and is waiting on the drop —
        // a watcher whose D-Bus call has not come back yet is unreachable by
        // every cancel site. Without this, two hops in quick succession leave
        // both continuations live: the older one applies the FIRST hop's
        // pre-tile size, and its inner lambda's map removal consumes the newer
        // hop's entry, so the newer connection then survives every later cancel
        // and resizes the window after an unrelated user move.
        const quint64 restoreGen = ++m_crossScreenRestoreSeq;
        m_crossScreenRestoreGen[windowId] = restoreGen;

        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getValidatedPreTileGeometry"), {windowId}),
            m_effect);
        // Context is `this` (the handler), not m_effect: the lambda captures and
        // dereferences TilingHandler members (m_managedScreens,
        // m_pendingCrossScreenRestore), and the handler is a unique_ptr member of
        // the effect destroyed before its sibling watcher. A `this` context auto-
        // disconnects the callback when the handler dies, matching the sibling
        // watchers above.
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeW, wid, fallbackW, fallbackH, restoreGen](QDBusPendingCallWatcher* pw) {
                    pw->deleteLater();

                    // Superseded by a later hop while this call was in flight.
                    if (m_crossScreenRestoreGen.value(wid) != restoreGen) {
                        return;
                    }

                    int restoreW = fallbackW;
                    int restoreH = fallbackH;
                    QDBusPendingReply<bool, int, int, int, int> reply = *pw;
                    // No arity term: count() is compile-time constant 5 (see
                    // requestDaemonPreTileRestore) — isValid + the success
                    // flag + the positive-extent checks below are the guard.
                    if (reply.isValid() && reply.argumentAt<0>()) {
                        int dw = reply.argumentAt<3>();
                        int dh = reply.argumentAt<4>();
                        if (dw > 0 && dh > 0) {
                            restoreW = dw;
                            restoreH = dh;
                        }
                    }

                    if (restoreW <= 0 || restoreH <= 0 || !safeW || safeW->isDeleted()) {
                        return;
                    }

                    // If the window bounced back to an autotile screen during the
                    // D-Bus round-trip, skip the restore — it's being tiled again.
                    const QString currentScreen = m_effect->getWindowScreenId(safeW);
                    if (m_managedScreens.contains(currentScreen)) {
                        return;
                    }

                    // If the drag already ended, apply immediately — unless the
                    // window was snapped to a zone by dragStopped during this D-Bus
                    // round-trip. In that case, zone geometry is already correct.
                    if (!safeW->isUserMove() && !safeW->isUserResize()) {
                        if (!m_effect->isWindowFloating(wid)) {
                            return; // Snapped to zone — don't clobber zone geometry
                        }
                        const QRectF frame = safeW->frameGeometry();
                        const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                        // Snap-out: the window is leaving tile-managed sizing.
                        m_effect->applyWindowGeometry(safeW, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                                      PhosphorAnimation::ProfilePaths::WindowSnapOut);
                        return;
                    }

                    // Still dragging — wait for drop, then restore size once.
                    // Use a shared connection so the lambda can disconnect itself
                    // after firing once — preventing subsequent user resizes from
                    // snapping the window back to the pre-autotile size.
                    auto sharedConn = std::make_shared<QMetaObject::Connection>();
                    // Context is `this` (the handler), not m_effect: the lambda
                    // mutates m_pendingCrossScreenRestore, so the connection must
                    // die with the handler.
                    *sharedConn =
                        connect(safeW.data(), &KWin::EffectWindow::windowFinishUserMovedResized, this,
                                [this, safeW, wid, restoreW, restoreH, sharedConn, restoreGen](KWin::EffectWindow*) {
                                    // Disconnect immediately so this only fires once (the drop),
                                    // not on every subsequent user resize.
                                    QObject::disconnect(*sharedConn);
                                    // Only consume the map entry if it is still
                                    // OURS: a superseding hop overwrote it, and
                                    // removing it here would leave that newer
                                    // connection unreachable by every cancel site.
                                    if (m_crossScreenRestoreGen.value(wid) != restoreGen) {
                                        return;
                                    }
                                    m_pendingCrossScreenRestore.remove(wid);
                                    if (!safeW || safeW->isDeleted()) {
                                        return;
                                    }
                                    // Guard: window may have bounced back to autotile during drag.
                                    const QString dropScreen = m_effect->getWindowScreenId(safeW);
                                    if (m_managedScreens.contains(dropScreen)) {
                                        return;
                                    }
                                    // Guard: window may have been snapped to a zone by dragStopped.
                                    if (!m_effect->isWindowFloating(wid)) {
                                        return;
                                    }
                                    const QRectF frame = safeW->frameGeometry();
                                    const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                                    // Snap-out: leaving tile-managed sizing.
                                    m_effect->applyWindowGeometry(safeW, geo, /*allowDuringDrag=*/false,
                                                                  /*skipAnimation=*/false,
                                                                  PhosphorAnimation::ProfilePaths::WindowSnapOut);
                                });
                    m_pendingCrossScreenRestore[wid] = *sharedConn;
                });

        // NOTE: Do NOT call windowUnsnapped here. The drag-drop handler
        // (WindowDragAdaptor::dragStopped) manages the zone assignment transition.
        // Firing windowUnsnapped now would race with the snap-to-zone D-Bus call
        // from the drop handler, destroying the zone assignment that was just created.
        // The drop handler's cross-screen path already clears stale state.

        // Raise above existing windows so it doesn't end up buried behind
        // snapped windows on the target screen.
        //
        // Not for a MINIMIZED window. This whole arm was written for a drag that
        // ended on a snapping screen (its continuation branches on isUserMove /
        // isUserResize, which a minimized window can never be in), and a
        // minimized window reaches it only incidentally — willReAdd is false for
        // it. There is no drop to keep visible, so the raise has no target
        // ordering to establish, and KWin raises the window itself when the user
        // eventually restores it.
        KWin::Window* kw = w->isMinimized() ? nullptr : w->window();
        if (kw) {
            auto* ws = KWin::Workspace::self();
            if (ws) {
                ws->raiseWindow(kw);
            }
        }
    }

    m_effect->updateAllDecorations();
}

} // namespace PlasmaZones
