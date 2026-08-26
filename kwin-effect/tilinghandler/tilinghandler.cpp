// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilinghandler.h"
#include "compositor/effectlogging.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/windowanimator.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QtMath>

namespace PlasmaZones {

TilingHandler::TilingHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
    // Colour-scheme and system-font changes re-resolve the compositor-drawn
    // tab pills' theme (see eventFilter). qGuiApp outlives this handler, and
    // Qt drops the filter when the filtering object is destroyed, so no
    // explicit removal is needed.
    if (qGuiApp) {
        qGuiApp->installEventFilter(this);
    }
}

QSize TilingHandler::declaredMinSize(KWin::EffectWindow* w)
{
    int minWidth = 0;
    int minHeight = 0;
    KWin::Window* kw = w ? w->window() : nullptr;
    // Internal windows (our own overlays) crash on minSize(); see discussion #511.
    if (kw && !kw->isInternal()) {
        const QSizeF minSize = kw->minSize();
        if (minSize.isValid()) {
            minWidth = qCeil(minSize.width());
            minHeight = qCeil(minSize.height());
        }
    }
    return QSize(minWidth, minHeight);
}

void TilingHandler::applyFullScreenSuppressed(KWin::Window* kw, bool fullScreen)
{
    if (!kw) {
        return;
    }
    // Counter, not a bool: releaseWindowedFullscreenState and the batch
    // consumer nest their own brackets, and a plain set/clear here would hand
    // an outer scope back an un-suppressed window (the save/restore rule the
    // inGeometryApply guards follow for the same reason).
    ++m_suppressFullScreenChanged;
    kw->setFullScreen(fullScreen);
    --m_suppressFullScreenChanged;
}

void TilingHandler::suppressFfmUntilCursorMoves()
{
    if (!KWin::effects) {
        return;
    }
    m_ffmSuppressPending = true;
    m_ffmSuppressAnchor = KWin::effects->cursorPos();
}

void TilingHandler::handleCursorMoved(const QPointF& pos, const QString& screenId)
{
    // Both of the next two bails also DISARM the suppression latch. They sit
    // above it, so a latch armed just before the last screen left the union
    // (or just before a peek started) would otherwise survive with an anchor
    // naming a pre-event cursor position, and swallow the first move within
    // the resume radius once the condition cleared. Same reasoning as the
    // clears in setFocusFollowsMouse(false) and clearPerSessionDaemonState.
    // ffmOffEverywhere(), not the two global flags: the scrolling half's
    // authority is the daemon's resolved per-screen set, so a
    // SetScrollFocusFollowsMouse rule that turns the behaviour ON for one
    // monitor while the global setting is off must not be bailed out from
    // here — that bail sat upstream of the per-screen read below and made the
    // rule a one-way switch (off yes, on never).
    // KWin::effects folded into the entry bail rather than tested at the two
    // derefs below, which is where every sibling in this file guards it (see
    // atScrollPark's note: an unguarded deref in compositor code is a session
    // crash, not a wrong answer). Clearing the latch on the way out is right
    // for the same reason it is right for the other two arms — there is no
    // compositor left for a suppressed move to be suppressed against.
    if (!KWin::effects || ffmOffEverywhere() || m_managedScreens.isEmpty()) {
        m_ffmSuppressPending = false;
        return;
    }

    // Pause FFM entirely during show-desktop/peek. Peeked windows are hidden
    // from the scene but keep their frameGeometry, so the scan below would
    // find one under the cursor and activateWindow() would synchronously
    // cancel the peek (Workspace::activateWindow unhides on activation).
    // Peek is hover-driven, so without this bail it collapses on the very
    // first cursor move.
    if (PlasmaZonesEffect::isShowingDesktop()) {
        // The latch is deliberately LEFT ARMED here, unlike the bail above. A
        // peek does not move or teleport the cursor, so the anchor is still
        // exactly the strip-move position it was set to. Clearing it let an
        // incidental twitch during the peek disarm the latch, and the first
        // twitch after the peek ended then activated whatever column had slid
        // under the pointer — the focus-yank the latch exists to prevent.
        return;
    }

    // Only act on managed screens (screenId already resolved by caller)
    if (screenId.isEmpty() || !m_managedScreens.contains(screenId)) {
        return;
    }

    // Per-mode routing: the screen's own engine decides which FFM flag
    // governs it. A plain return (no latch disarm), like the screen gate
    // above — a screen whose mode has FFM off must not clear a latch a
    // managed screen of the other mode still relies on.
    // The scrolling arm reads the daemon's RESOLVED per-screen set rather
    // than the global flag: a SetScrollFocusFollowsMouse context rule can
    // turn the behaviour on for one monitor and off for another, and the
    // daemon has already folded `rule ?? config` into membership. The
    // non-scrolling arm keeps the global flag — that half has no per-screen
    // rule (snapping and tiling share the one setting).
    if (isScrollingScreen(screenId) ? !m_scrollFocusFollowsMouseScreens.contains(screenId) : !m_focusFollowsMouse) {
        return;
    }

    // Engine-driven strip movement pause (see the header doc): ignore
    // incidental pointer jitter after the strip scrolled under a
    // stationary cursor; a deliberate move past the radius resumes FFM.
    // Evaluated AFTER the screen gate so a move on an unmanaged screen
    // cannot clear a latch the managed screen is still relying on.
    if (m_ffmSuppressPending) {
        constexpr qreal kFfmResumeRadiusPx = 32.0;
        if ((pos - m_ffmSuppressAnchor).manhattanLength() < kFfmResumeRadiusPx) {
            return;
        }
        m_ffmSuppressPending = false;
    }

    // Pause focus-follows-mouse while the currently active window is one we
    // would refuse to focus via FFM anyway: excluded app, dialog, popup,
    // keep-above overlay, or a window below the min-size threshold. Without
    // this, the user opens an emoji picker or notification inside a zone,
    // moves the cursor across the underlying tiled window's visible area, and
    // FFM activates that tiled window first, sending the just-opened popup
    // straight to the background (discussion #461 item 3 follow-up).
    // Resumes naturally on the next cursor move once a tileable window is
    // active. Scoped to the same screen as the cursor so an unrelated focused
    // window on another monitor never freezes FFM here.
    if (KWin::EffectWindow* active = KWin::effects->activeWindow()) {
        if (!PlasmaZonesEffect::isOwnPassthroughOverlayClass(active->windowClass())
            && m_effect->getWindowScreenId(active) == screenId) {
            // Filter first, then size-check. This mirrors the under-cursor
            // guard below so the two predicates stay structurally aligned.
            // The cheap-to-skip min-size check is only paid when the active
            // window is otherwise tileable.
            if (!m_effect->isTileableWindow(active) || !m_effect->shouldHandleWindow(active)) {
                return;
            }
            // Also pause for floating active windows. FloatingCache covers
            // both manually-floated windows and overflow windows that the
            // daemon auto-floated past the maxWindows cap (applyFloatCleanup
            // path). Either kind is perched on top of the tiled stack while
            // the user works in it, so activating an underlying tiled window
            // on cursor wander sends the floating one straight to the
            // background — the same regression the excluded-active guard
            // above fixes (discussion #461 follow-up).
            if (m_effect->isWindowFloating(m_effect->getWindowId(active))) {
                return;
            }
            const QRectF aframe = active->frameGeometry();
            if ((m_effect->m_cachedMinWindowWidth > 0 && aframe.width() < m_effect->m_cachedMinWindowWidth)
                || (m_effect->m_cachedMinWindowHeight > 0 && aframe.height() < m_effect->m_cachedMinWindowHeight)) {
                return;
            }
        }
    }

    // Find the topmost autotile-managed window under the cursor.
    // Iterate stacking order in reverse (top → bottom).
    //
    // Cost, stated because this runs per pointer-motion event once
    // focus-follows-mouse is on for any managed screen: EffectsHandler::
    // stackingOrder() returns the list BY VALUE and builds it per call, so this
    // is one list allocation per motion event. There is no const-reference form
    // to take instead. It is paid only past every bail above (FFM off, peek,
    // unmanaged screen, suppression latch, unfocusable active window), and the
    // scan below is ordered cheap-test-first for the same reason.
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        // isDeleted: a close-grabbed dying window under the cursor must not
        // pause FFM via the occlusion bail (mirrors the snap FFM guard).
        // isHiddenByShowDesktop: belt-and-braces behind the showing-desktop
        // bail above, for the frame where peek engages mid-scan.
        if (!w || w->isDeleted() || w->isMinimized() || w->isHiddenByShowDesktop() || !w->isOnCurrentDesktop()
            || !w->isOnCurrentActivity()) {
            continue;
        }
        // Geometry check first (cheap QRectF::contains) before shouldHandleWindow (allocates via windowClass())
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        // Look through the daemon's own passthrough overlay surface — it is
        // full-screen and always topmost on the autotile monitor, so the bail
        // below would otherwise kill FFM forever (discussion #461 #3). The
        // editor is deliberately NOT looked through here: it is an interactive
        // fullscreen window, so it falls to the occluder bail below and FFM
        // leaves focus on it rather than stealing to the tiled window beneath.
        if (PlasmaZonesEffect::isOwnPassthroughOverlayClass(w->windowClass())) {
            continue;
        }
        // A non-autotile window (excluded app, keep-above overlay, popup, dialog,
        // Spectacle, etc.) occludes the cursor — don't look through it to focus a
        // tiled window beneath. This prevents focus-stealing from emoji pickers,
        // screenshot tools, and other excluded/overlay windows.
        if (!m_effect->isTileableWindow(w) || !m_effect->shouldHandleWindow(w)) {
            return;
        }
        // Also block focus for windows below the minimum size threshold.
        // These are normal windows (pass isTileableWindow) but too small
        // for autotile — e.g., emoji picker, small utilities. Without this,
        // hovering over them triggers auto-focus even though they're not tiled.
        {
            const QRectF frame = w->frameGeometry();
            if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
                || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
                return;
            }
        }
        // Skip the activateWindow call when the window under the cursor
        // already holds compositor focus. The live activeWindow() read is
        // load-bearing: a local "last auto-focused window" cache would go
        // stale every time focus moved through another path (keyboard
        // shortcut, click, daemon-driven activate, focus-stealing window),
        // and the next cursor pass over the originally-cached window would
        // short-circuit without re-focusing it. See discussion #461 item 13.
        if (w == KWin::effects->activeWindow()) {
            return; // Already focused — no-op
        }
        // Only focus windows on autotile screens, and only on the screen the
        // cursor is actually on. Membership alone is not enough: a scrolling
        // window's engine-tracked screen (the answer getWindowScreenId gives
        // it) can be a neighbouring managed output while its frame overhangs
        // into this one, so a hit on the overhang would activate a window that
        // is clipped invisible over there.
        if (m_effect->getWindowScreenId(w) != screenId) {
            return;
        }
        // The focus-follows-mouse scroll cap, niri's max-scroll-amount: the
        // daemon has already asked the strip how far focusing each window
        // would move the view and named the ones past their screen's cap, so
        // the answer here is a set lookup on a per-pointer-event path. The
        // set is the UNION across every capped screen and carries no screen
        // dimension of its own; what makes this window's membership the right
        // question is the screen match that returned just above.
        //
        // A plain return, like the occluder bails above rather than a
        // `continue`: the window under the cursor is still the window under
        // the cursor, and looking THROUGH a refused one to focus a tiled
        // window behind it would scroll the strip by the very amount the cap
        // just refused.
        if (m_scrollFocusScrollBlockedWindows.contains(m_effect->getWindowId(w))) {
            return;
        }
        KWin::effects->activateWindow(w);
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration points
// ═══════════════════════════════════════════════════════════════════════════════

void TilingHandler::reportScrollClipLoss(const QString& windowId, const QString& reason) const
{
    // Warning-level and on the diag category, so it shows with no logging rule
    // set. Reserved for the genuinely anomalous gate (a tracked scrolling
    // screen with no connected output) — the routine negatives ("not a strip
    // member", "screen not scrolling") are the predicate's normal answer for
    // every dialog and autotile window and must never reach here. Deduplicated
    // per window (see m_scrollClipLossReported) because the caller runs per
    // frame.
    if (m_scrollClipLossReported.contains(windowId)) {
        return;
    }
    m_scrollClipLossReported.insert(windowId);
    qCWarning(lcEffectDiag) << "scrollClip LOST for" << windowId << "-" << reason;
}

QString TilingHandler::scrollTrackedScreenFor(const QString& windowId) const
{
    // Membership FIRST, and it supplies the screen. The two facts used to come
    // from different maps written under different conditions: the apply loop
    // marks a window tiled unconditionally but only records its screen when
    // the window is in m_notifiedWindows, so a window demoted or rolled back
    // between the two ends up a tiled member with no recorded screen. This
    // function needs both, so it answered "unknown" — which is fail-open for
    // every caller: the paint clip and the input filter treat an invalid rect
    // as "not a straddler", so a centred column's overhang rendered on, and
    // took clicks on, the neighbouring output.
    const QString tiledScreen = TilingStateHelpers::screenForTiledWindow(m_border, windowId);
    if (tiledScreen.isEmpty()) {
        // Routine negative: every dialog, popup and non-strip window takes
        // this exit per frame. Not a clip loss — nothing to report.
        return QString();
    }
    // Both maps get a say, and the one that names an actual scrolling screen
    // wins. Neither is trustworthy enough alone:
    //   - tiledWindowsByScreen carries the daemon's authoritative screenId and
    //     nothing positional ever writes it, but it is a per-screen bucket set
    //     and a window can be left in a stale bucket; the lookup returns the
    //     first match in unspecified hash order, so a stale entry can shadow
    //     the live one.
    //   - m_notifiedWindowScreens is a single value per window so it cannot be
    //     ambiguous, but several of its writers store a POSITION-derived
    //     screen (the outputChanged frame-centre resolve, the virtual-screen
    //     re-resolve). A centred column straddling the screen edge can have
    //     its centre on the neighbouring output, so those can stamp a screen
    //     that runs no strip at all.
    // Taking whichever answer survives the scrolling-set test uses each map's
    // strength: a stale bucket or a positional stamp names a screen that is
    // not scrolling and is simply skipped, and both have to be wrong at once
    // for the predicate to fail. That matters because failing here fails OPEN
    // — the paint clip and the input filter both read an invalid rect as "not
    // a straddler", so a wrong answer does not merely mislocate the overhang,
    // it stops suppressing it at all.
    //
    // The RAW set, deliberately, NOT the isScrollingScreen intersection. Both
    // consumers want the conservative answer: the paint clip / input filter,
    // and getWindowScreenId's engine-authoritative screen override (a parked
    // column's frame centre lies inside the NEIGHBOUR output). The union and
    // the scrolling set arrive on independent signals, so intersecting made a
    // screen leaving scrolling lose clip AND override together for the frames
    // between them. The intersection belongs on the rule and verb consumers.
    QString tracked;
    if (m_scrollingScreens.contains(tiledScreen)) {
        tracked = tiledScreen;
    } else if (const QString recorded = m_notifiedWindowScreens.value(windowId);
               m_scrollingScreens.contains(recorded)) {
        tracked = recorded;
    } else {
        // Routine negative: a tiled member of a NON-scrolling screen — every
        // autotile and snap window in a mixed session answers here per frame.
        // Not a clip loss, and the message would have to allocate (a values()
        // copy plus a join) on the paint path just to say so.
        return QString();
    }
    // Connected-output gate (see the header doc): cached set lookup, so the
    // per-candidate calls inside the focus-follows-mouse stacking walks pay
    // a hash probe instead of an O(outputs) id-building scan.
    const QString trackedPhysical = PhosphorIdentity::VirtualScreenId::extractPhysicalId(tracked);
    if (!m_effect->connectedPhysicalIds().contains(trackedPhysical)) {
        reportScrollClipLoss(windowId, QStringLiteral("tracked screen %1 has no connected output").arg(tracked));
        return QString();
    }
    // Answered. Re-arm the report so a LATER loss for this window is announced
    // rather than swallowed as already-seen.
    m_scrollClipLossReported.remove(windowId);
    return tracked;
}

bool TilingHandler::atScrollPark(KWin::EffectWindow* w) const
{
    // "Sitting at a park right now", asked of the LIVE frame, for the batch
    // apply's arrival branch.
    //
    // The union of every output, deliberately, not the window's own screen
    // rect. The strip parks below the union precisely so no park can land on
    // any output, which makes "off the union" the exact statement of parked
    // and nothing else. Testing one output instead would also catch a window
    // arriving from ANOTHER MONITOR — which is on screen the whole way and
    // owns a real cross-monitor motion the caller would then flatten to a
    // degenerate leg.
    //
    // The frame, not the expanded geometry: a park sits kParkMargin past the
    // union bottom, which a tall enough decoration shadow would bridge, and
    // the question here is where the window IS, not what it reaches.
    if (!w) {
        return false;
    }
    const KWin::RectF f = w->frameGeometry();
    const QRect frame(qRound(f.x()), qRound(f.y()), qRound(f.width()), qRound(f.height()));
    if (frame.isEmpty()) {
        // Degenerate geometry (mid-unmap, zero-size commit) never intersects
        // anything, so falling through would answer PARKED for a window we
        // cannot locate. Fail closed: the caller's fallback is the ordinary
        // animated leg, which is what a non-parked window should get.
        return false;
    }
    if (!KWin::effects) {
        // The doc promises fail-closed on "no resolvable outputs", and every
        // sibling m_scrollVisualDelta damage pair guards this pointer — an
        // unguarded deref in compositor code is a session crash, not a wrong
        // answer.
        return false;
    }
    QRect unionRect;
    for (const KWin::LogicalOutput* output : KWin::effects->screens()) {
        unionRect = unionRect.united(QRect(output->geometry()));
    }
    // No resolvable outputs (disconnect race): the union is empty and every
    // rect is trivially "off" it. Fail closed for the same reason as above.
    return !unionRect.isEmpty() && !unionRect.intersects(frame);
}

bool TilingHandler::notifyWindowAdded(KWin::EffectWindow* w, bool knownFreeFloating)
{
    // Deleted windows bail before getWindowId (cache-pollution hazard).
    if (!w || w->isDeleted()) {
        return false;
    }

    const QString windowId = m_effect->getWindowId(w);

    bool minimizedOnly = false;
    if (!isEligibleForTilingNotify(w, &minimizedOnly)) {
        if (knownFreeFloating && minimizedOnly && m_initialScreenQueryPending) {
            m_pendingFreshWindows.insert(windowId);
        }
        return false;
    }

    if (m_notifiedWindows.contains(windowId)) {
        return false;
    }

    const QString screenId = m_effect->getWindowScreenId(w);
    if (knownFreeFloating && m_initialScreenQueryPending && !m_managedScreens.contains(screenId)) {
        // Retain spawn provenance until the initial screen query reveals
        // whether this window belongs in its follow-up batch.
        m_pendingFreshWindows.insert(windowId);
    }

    // Only notify autotile daemon for windows on autotile screens
    if (m_managedScreens.contains(screenId)) {
        // Consume the spawn-provenance marker UNCONDITIONALLY — a short-circuit
        // (|| with remove second) would leave the entry behind whenever the
        // caller already passed true, and a later RE-ADD that deliberately
        // passes false would then flip to true off the stale entry.
        const bool wasFresh = m_pendingFreshWindows.remove(windowId) > 0;
        knownFreeFloating = knownFreeFloating || wasFresh;
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;
        // Save pre-autotile geometry BEFORE the daemon tiles the window.
        // Without this, a window launched directly into autotile has no saved
        // geometry — floating it would leave it at its tiled position instead
        // of restoring to its original free-floating size.
        //
        // knownFreeFloating is passed EXPLICITLY by every caller (no default
        // argument): the genuine window-opened path passes true (the frame is
        // KWin's spawn geometry — the authoritative pre-autotile position —
        // and the FloatingCache is not yet populated, so the
        // isWindowFloating() guard would otherwise drop the one-shot save).
        // RE-ADD callers pass false so the floating guard runs and rejects a
        // tiled zone rect instead of persisting it as free geometry.
        saveAndRecordPreTileGeometry(windowId, screenId, w, w->frameGeometry(), knownFreeFloating);

        const QSize minSize = declaredMinSize(w);
        // Seed the change-poll cache with the announced value so the batch
        // consumer's re-report leg only fires when the hints actually move.
        m_effect->m_lastReportedMinSize.insert(windowId, minSize);

        // Announce stamp, the m_crossScreenRestoreGen idiom: the rollback below
        // may only undo the tracking THIS announce established. Without it an
        // error for announce N-1 erased the tracking announce N had just written,
        // and a NoReply for an already-closed window re-inserted a
        // spawn-provenance marker for a corpse — ids are appId-derived and
        // reusable, and the tail prune in completeDeferredWindowRoutes only runs
        // on a screen-query dispatch, so the stale marker would later flip a
        // same-app sibling's re-add to knownFreeFloating=true and poison its free
        // geometry with a zone rect. cleanupAutotileTracking erases the stamp, so
        // a corpse's error arm reads back 0 and no-ops.
        const quint64 announceGen = ++m_announceSeq;
        m_announceGen[windowId] = announceGen;

        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Tiling,
                                                       QStringLiteral("windowOpened"),
                                                       {windowId, screenId, minSize.width(), minSize.height()}),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, windowId, wasFresh, announceGen](QDBusPendingCallWatcher* pw) {
                    pw->deleteLater();
                    if (pw->isError()) {
                        // Superseded, or the window is gone (the stamp died with
                        // its tracking). Nothing here is ours to roll back —
                        // including endRestoreSuppression below, which is correct
                        // in both cases: a newer announce owns the suppression it
                        // armed, and a corpse resolves to null anyway.
                        if (m_announceGen.value(windowId) != announceGen) {
                            return;
                        }
                        m_announceGen.remove(windowId);
                        qCWarning(lcEffect)
                            << "windowOpened D-Bus call failed for" << windowId << ":" << pw->error().message();
                        m_notifiedWindows.remove(windowId);
                        m_notifiedWindowScreens.remove(windowId);
                        // Min-size seed rollback, mirroring the batch error
                        // arm's rationale: on a failed announce the daemon
                        // never heard the size, and the cache would
                        // otherwise record it as sent. (Symmetry only — a
                        // later re-announce re-seeds it either way.)
                        m_effect->m_lastReportedMinSize.remove(windowId);
                        // The spawn-provenance marker was consumed above on the
                        // assumption the announce landed. Put it back, or the
                        // re-announce passes knownFreeFloating=false and the floating
                        // guard rejects the window's spawn geometry — the free-geometry
                        // loss the marker exists to prevent.
                        if (wasFresh) {
                            m_pendingFreshWindows.insert(windowId);
                        }
                        // notifyWindowAdded() returned true on the synchronous
                        // path, so the caller (PlasmaZonesEffect::slotWindowAdded)
                        // left first-frame open suppression engaged expecting a
                        // moveResize from the daemon's tile decision. The D-Bus
                        // call failed — no moveResize is coming — so release
                        // suppression here rather than letting the window sit
                        // invisible until the 250 ms deadline. Exact-id re-check:
                        // the fuzzy appId fallback could resolve a same-app sibling
                        // for a just-closed window, ending the sibling's suppression
                        // early.
                        if (KWin::EffectWindow* effectWindow = m_effect->findWindowByIdExact(windowId)) {
                            m_effect->endRestoreSuppression(effectWindow);
                        }
                        return;
                    }
                    // Landed: nothing left to roll back, so retire the stamp and
                    // keep the map bounded. A still-in-flight older announce
                    // reads back 0 afterwards and stays superseded.
                    if (m_announceGen.value(windowId) == announceGen) {
                        m_announceGen.remove(windowId);
                    }
                });
        qCDebug(lcEffect) << "Notified autotile: windowOpened" << windowId << "on screen" << screenId
                          << "minSize:" << minSize.width() << "x" << minSize.height();
        return true;
    }
    return false;
}

void TilingHandler::notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows,
                                            const QSet<QString>& screenFilter, bool resetNotified,
                                            bool enteringAutotile,
                                            const QHash<KWin::EffectWindow*, QString>& screenOverrides)
{
    // Collect eligible windows using the same filtering as notifyWindowAdded,
    // then send one batch D-Bus call instead of per-window round-trips.
    PhosphorProtocol::WindowOpenedList batchEntries;
    QStringList batchWindowIds; // for error rollback
    QStringList batchFreshWindowIds; // spawn-provenance markers consumed below, restored on error
    // Announce stamps, one PER ENTRY rather than one for the batch: a single
    // batch-wide stamp would let one superseded window (re-announced on its own
    // between dispatch and reply, or closed) disarm the rollback for every other
    // window in the same batch. Same idiom as the single-window announce above.
    QHash<QString, quint64> batchAnnounceGens;

    for (KWin::EffectWindow* w : windows) {
        // Deleted windows bail before any id/screen lookup (cache-pollution hazard).
        if (!w || w->isDeleted()) {
            continue;
        }

        const QString windowId = m_effect->getWindowId(w);
        // A caller-supplied id wins over the positional resolve, and is then
        // the ONE value the filter, the notified-screen stamp, the pre-tile
        // capture and the wire entry all read (see the header): the engine
        // flip resolves under the pre-flip scrolling set, where a parked strip
        // column is still attributed to its own output.
        const auto overrideIt = screenOverrides.constFind(w);
        const QString screenId =
            overrideIt != screenOverrides.constEnd() ? overrideIt.value() : m_effect->getWindowScreenId(w);
        if (!screenFilter.isEmpty() && !screenFilter.contains(screenId)) {
            continue;
        }

        // Reset BEFORE the eligibility check: a window that is currently
        // ineligible (minimized, fullscreen) must still shed its stale
        // m_notifiedWindows entry on a re-announce cycle, or its later
        // notifyWindowAdded (unminimize, exit-fullscreen) hits the
        // already-notified bail and silently never announces it.
        if (resetNotified) {
            m_notifiedWindows.remove(windowId);
            // Its screen record travels with it — leaving it behind orphans a
            // stale screen association the next notify would read.
            m_notifiedWindowScreens.remove(windowId);
            // Deliberately NOT parked in m_savedNotifiedForDesktopReturn, whose
            // meaning is "the daemon STILL holds this window in the other
            // desktop's state, so re-track on return without re-notifying".
            //
            // Safe for all three resetNotified callers by construction: that
            // set is inserted at exactly one site (screenschanged.cpp's
            // desktop-switch demotion) and only inside `if (m_notifiedWindows.remove(...))`,
            // so "parked" implies "already absent from m_notifiedWindows" and
            // this remove() cannot disturb a park. Parking here instead made
            // the desktop-return branch silently re-insert the window with no
            // windowOpened, so a restarted daemon never learned it existed.
        }

        bool minimizedOnly = false;
        if (!isEligibleForTilingNotify(w, &minimizedOnly)) {
            if (minimizedOnly) {
                // Same resolved id, not a second resolve: this arm reads the
                // screen filter too, so a re-resolve after an engine flip
                // would drop the already-minimized windows the flip path
                // specifically re-announces to claim.
                claimAlreadyMinimizedAsFloated(w, windowId, screenFilter, enteringAutotile, screenId);
            }
            continue;
        }

        if (!m_managedScreens.contains(screenId)) {
            continue;
        }

        if (m_notifiedWindows.contains(windowId)) {
            continue;
        }
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;

        // Existing windows use the guarded path. A window first observed while
        // the initial screen query was pending retains explicit spawn provenance.
        const bool wasFresh = m_pendingFreshWindows.remove(windowId) > 0;
        if (wasFresh) {
            batchFreshWindowIds.append(windowId);
        }
        const bool knownFreeFloating =
            wasFresh || (enteringAutotile && !TilingStateHelpers::isTiledWindow(m_border, windowId));
        saveAndRecordPreTileGeometry(windowId, screenId, w, w->frameGeometry(), knownFreeFloating);

        const QSize minSize = declaredMinSize(w);
        // Seed the last-reported cache like the single-window announce does:
        // the batch carries the same minSize, and an unseeded entry costs one
        // redundant windowMinSizeUpdated on the window's first tile batch.
        m_effect->m_lastReportedMinSize.insert(windowId, minSize);

        PhosphorProtocol::WindowOpenedEntry entry;
        entry.windowId = windowId;
        entry.screenId = screenId;
        entry.minWidth = minSize.width();
        entry.minHeight = minSize.height();
        batchEntries.append(entry);
        batchWindowIds.append(windowId);
        const quint64 announceGen = ++m_announceSeq;
        m_announceGen[windowId] = announceGen;
        batchAnnounceGens.insert(windowId, announceGen);
    }

    if (batchEntries.isEmpty()) {
        return;
    }

    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Tiling,
                                        QStringLiteral("windowsOpenedBatch"), {QVariant::fromValue(batchEntries)}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, batchWindowIds, batchFreshWindowIds, batchAnnounceGens](QDBusPendingCallWatcher* pw) {
                pw->deleteLater();
                if (pw->isError()) {
                    qCWarning(lcEffect) << "windowsOpenedBatch D-Bus call failed:" << pw->error().message();
                    // Tracking rollback only — deliberately NO endRestoreSuppression
                    // here, unlike notifyWindowAdded's error handler: the batch paths
                    // (daemon-restart re-announce, toggle-on) serve windows that are
                    // still physically tiled and may be re-announced when the daemon
                    // returns.
                    //
                    // Per-entry stamp check: only windows whose newest announce is
                    // still THIS one are rolled back. A window re-announced on its
                    // own since dispatch is owned by that announce, and a window
                    // closed since then reads back 0 (cleanupAutotileTracking
                    // erased the stamp) so its dead, reusable id is not re-armed
                    // as spawn-provenance.
                    for (const QString& wid : batchWindowIds) {
                        if (m_announceGen.value(wid) != batchAnnounceGens.value(wid)) {
                            continue;
                        }
                        m_announceGen.remove(wid);
                        m_notifiedWindows.remove(wid);
                        m_notifiedWindowScreens.remove(wid);
                        // The min-size seed rolls back with the tracking: on
                        // a failed batch the daemon never heard the size,
                        // and the cache would otherwise record it as sent.
                        m_effect->m_lastReportedMinSize.remove(wid);
                        // The consumed spawn-provenance marker rolls back with the
                        // tracking — see the single-window handler above. Done in
                        // the same pass so it shares the stamp verdict.
                        if (batchFreshWindowIds.contains(wid)) {
                            m_pendingFreshWindows.insert(wid);
                        }
                    }
                    return;
                }
                // Landed: nothing to roll back, so retire the stamps this batch
                // still owns and keep the map bounded.
                for (const QString& wid : batchWindowIds) {
                    if (m_announceGen.value(wid) == batchAnnounceGens.value(wid)) {
                        m_announceGen.remove(wid);
                    }
                }
            });
    qCInfo(lcEffect) << "Notified autotile: windowsOpenedBatch with" << batchEntries.size() << "windows";
}

void TilingHandler::cleanupAutotileTracking(const QString& windowId)
{
    // Compositor-agnostic state cleanup (shared helper).
    TilingStateHelpers::TilingWindowState windowState{
        m_notifiedWindows,      m_notifiedWindowScreens,   m_minimizeFloatedWindows, m_tileTargetZones,
        m_centeredWaylandZones, m_monocleMaximizedWindows, m_preTileGeometries};
    TilingStateHelpers::cleanupClosedWindowState(windowId, m_border, windowState);
    m_untiledMinimizeFloats.remove(windowId);
    m_unfloatInFlight.remove(windowId);
    // Retry budget and route/provenance markers die with the tracking: a
    // reused windowId must not inherit an exhausted budget, and every direct
    // caller of this cleanup (not just onWindowClosed) must drop the
    // spawn-provenance entries or they leak past cross-mode moves.
    m_unfloatRetryAttempts.remove(windowId);
    m_pendingFreshWindows.remove(windowId);
    m_deferredWindowRoutes.remove(windowId);
    // The tab-colour verdict dies with the window: the cache is per window
    // and nothing else evicts it, so every close funnels through here (this
    // is the one untrack funnel every caller passes).
    dropScrollTabColorsForWindow(windowId);
    // The announce stamp dies with the tracking, and that erasure is what makes
    // a late error reply for a CLOSED window harmless: the arm reads back 0,
    // mismatches its captured stamp, and no-ops instead of re-inserting a
    // spawn-provenance marker under an id another instance of the same app can
    // be handed. Safe to erase rather than tombstone because the stamps come
    // from a session-global monotonic counter (the m_crossScreenRestoreGen
    // contract).
    m_announceGen.remove(windowId);
    // The clip-loss dedupe entry dies with the window too: ids are
    // appId-derived and reusable, and a stale entry would swallow a reused
    // id's first genuine report (the success-path re-arm only runs when the
    // predicate answers).
    m_scrollClipLossReported.remove(windowId);
    // The commanded-rect entry dies with the tracking too: the cross-output
    // transfer path otherwise leaves the OLD screen's rect behind, and once
    // the window is re-announced onto another scrolling screen the
    // counter-assert could fight one legitimate position with it before the
    // first batch overwrites it. The parked paint hint goes with it for the
    // same reason (inert off a scrolling output, but a stale relocation the
    // moment the window returns to one).
    //
    // The relocation drop pairs with damage, like every other remover: it
    // changes where the paint path draws the window. Not all of this funnel's
    // callers are mid-drag (where KWin damages continuously and the gap would
    // be invisible) — the cross-mode output handoff and the move-to-another-
    // desktop path both reach here with a live window and nothing else
    // scheduling a frame, so the last presented frame would keep the column at
    // its dead strip position. Pairing here rather than at the call sites
    // covers every caller at once.
    m_effect->m_scrollCommandedRects.remove(windowId);
    m_effect->m_scrollOfferedColumn.remove(windowId);
    if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    // Windowed fullscreen dies with the tracking: this funnel serves the
    // close path (where slotWindowClosed already removed the membership,
    // making this belt) and the cross-output transfer (where nothing else
    // does, and a window landing on a snapping screen would otherwise stay
    // KWin-fullscreen forever — snapping emits no tile batch to un-flag
    // it). Forget-then-release, membership first, so a synchronous
    // re-entry from setFullScreen finds the entry already gone.
    if (m_effect->m_windowedFullscreenWindows.contains(windowId)) {
        forgetWindowedFullscreen(windowId);
        releaseWindowedFullscreenState(windowId);
    }
    m_windowedFsClearInFlight.remove(windowId);
    cancelPendingMinimizeFloat(windowId);
    cancelPendingUnminimizeUnfloat(windowId);
    // KWin-specific cleanup. NOTE: m_savedPreTileForDesktopMove is NOT cleared
    // here — the desktop-move path stashes it immediately before close (consume
    // site / clearDesktopMoveStash cover it). Also drop the unconsumed output-move
    // marker and the pending cross-screen-restore connection (a stale one could
    // fire a spurious applyWindowGeometry).
    m_savedNotifiedForDesktopReturn.remove(windowId);
    m_expectedOutputMove.remove(windowId);
    if (auto pendingConn = m_pendingCrossScreenRestore.find(windowId);
        pendingConn != m_pendingCrossScreenRestore.end()) {
        QObject::disconnect(pendingConn.value());
        m_pendingCrossScreenRestore.erase(pendingConn);
    }
    // Safe to erase because the stamps come from a session-global monotonic
    // counter: a continuation still in flight holds a non-zero stamp, and a
    // removed entry reads back as 0, so it stays superseded without the map
    // having to retain a row per window forever.
    m_crossScreenRestoreGen.remove(windowId);
    // Every screen, not just the one passed. On a cross-output transfer the
    // caller passes the OLD screen, so the destination's saved order kept the
    // id, and other screens' orders can hold it from an earlier session. The
    // replay resolves by exact id and skips misses, so a residue was inert —
    // but it was also unbounded across a session, growing one dead id per
    // screen per transfer.
    for (auto orderIt = m_savedAutotileStackingOrder.begin(); orderIt != m_savedAutotileStackingOrder.end();
         ++orderIt) {
        orderIt.value().removeAll(windowId);
    }
}

void TilingHandler::onWindowClosed(const QString& windowId, const QString& screenId)
{
    // The spawn-provenance and deferred-route entries are cleared by
    // cleanupAutotileTracking, which owns them for every caller.
    cleanupAutotileTracking(windowId);

    // Notify autotile daemon
    if (m_managedScreens.contains(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Tiling,
                                                       QStringLiteral("windowClosed"), {windowId},
                                                       QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenId;
    }
}

void TilingHandler::releaseWindowTracking(const QString& windowId, const QString& screenId)
{
    cleanupAutotileTracking(windowId);

    if (m_managedScreens.contains(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Tiling,
                                                       QStringLiteral("releaseWindowTracking"), {windowId},
                                                       QStringLiteral("releaseWindowTracking"));
        qCDebug(lcEffect) << "Notified tiling: releaseWindowTracking" << windowId << "on screen" << screenId;
    }
}

void TilingHandler::deferWindowRouting(KWin::EffectWindow* window, bool canSnapRestore)
{
    if (!window || window->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(window);
    m_pendingFreshWindows.insert(windowId);
    m_deferredWindowRoutes.insert(windowId, DeferredWindowRoute{QPointer<KWin::EffectWindow>(window), canSnapRestore});
}

QSet<QString> TilingHandler::completeDeferredWindowRoutes()
{
    const auto routes = m_deferredWindowRoutes;
    m_deferredWindowRoutes.clear();
    QSet<QString> routedWindowIds;
    routedWindowIds.reserve(routes.size());
    for (auto it = routes.constBegin(); it != routes.constEnd(); ++it) {
        routedWindowIds.insert(it.key());
        KWin::EffectWindow* window = it->window.data();
        if (!window || window->isDeleted()) {
            m_pendingFreshWindows.remove(it.key());
            continue;
        }
        const QString windowId = m_effect->getWindowId(window);
        routedWindowIds.insert(windowId);
        // The pending-fresh entry was keyed by the id at defer time; if the
        // live id diverged, the old key would leak forever (the tail prune
        // below only drops dead/off-screen windows, and this window is
        // neither).
        if (windowId != it.key()) {
            m_pendingFreshWindows.remove(it.key());
        }
        // The defer-time first-frame suppression was armed with the standard
        // deadline, but the screen query this dispatch waited on can outlast
        // it — re-arm (deadline only, no-op for unsuppressed windows) so the
        // window doesn't return to compositing at its centred spawn placement
        // between deadline expiry and the reposition below.
        m_effect->refreshRestoreSuppressionDeadline(window);
        // Consume (and maybe apply) the instant snap-restore cache entry,
        // exactly as the non-deferred open path does — a deferred window must
        // not leave its entry alive for a later same-app sibling to claim.
        // A teleport can move the window to another screen; re-resolve after.
        QString screenId = m_effect->getWindowScreenId(window);
        if (it->canSnapRestore && !window->isMinimized()
            && m_effect->tryInstantSnapRestore(window, windowId, /*canSnapRestore=*/true)) {
            screenId = m_effect->getWindowScreenId(window);
        }
        if (m_managedScreens.contains(screenId)) {
            if (window->isMinimized()) {
                // A window that minimized while the screen query was pending
                // is excluded from the follow-up batch (it is in
                // routedWindowIds), so nothing else will claim it — claim it
                // here, release the first-frame suppression (a minimized
                // window paints nothing, and leaving the suppression armed
                // stalls its eventual restore for the 250 ms deadline), and
                // drop the spawn-provenance marker so a later re-add cannot
                // inherit knownFreeFloating=true from a stale entry.
                // Empty filter: passing m_managedScreens duplicated the
                // claim's own internal autotile-screen gate verbatim.
                claimAlreadyMinimizedAsFloated(window, windowId, {}, /*enteringAutotile=*/true);
                m_pendingFreshWindows.remove(windowId);
                m_effect->endRestoreSuppression(window);
                continue;
            }
            if (it->canSnapRestore && m_effect->snapHandler()) {
                QPointer<KWin::EffectWindow> safeWindow = window;
                m_effect->snapHandler()->callResolveWindowRestore(
                    window,
                    [this, safeWindow, windowId](bool snapApplied) {
                        if (!safeWindow || safeWindow->isDeleted()) {
                            return;
                        }
                        if (!m_managedScreens.contains(m_effect->getWindowScreenId(safeWindow.data()))) {
                            m_pendingFreshWindows.remove(windowId);
                            m_effect->endRestoreSuppression(safeWindow.data());
                            return;
                        }
                        // knownFreeFloating only when the restore did NOT
                        // apply — a zone-placed window's live frame is the
                        // zone rect, not a genuine free frame.
                        if (!notifyWindowAdded(safeWindow.data(), /*knownFreeFloating=*/!snapApplied)
                            && !m_notifiedWindows.contains(windowId)) {
                            m_effect->endRestoreSuppression(safeWindow.data());
                        }
                    },
                    /*releaseSuppressionOnMiss=*/false);
            } else if (!notifyWindowAdded(window, /*knownFreeFloating=*/true)
                       && !m_notifiedWindows.contains(windowId)) {
                m_effect->endRestoreSuppression(window);
            }
            continue;
        }

        m_pendingFreshWindows.remove(it.key());
        m_pendingFreshWindows.remove(windowId);
        if (it->canSnapRestore && !window->isMinimized() && m_effect->snapHandler()) {
            m_effect->snapHandler()->callResolveWindowRestore(window);
        } else {
            m_effect->endRestoreSuppression(window);
        }
    }

    const auto pendingIds = m_pendingFreshWindows.values();
    for (const QString& windowId : pendingIds) {
        // EXACT resolve: the entry is keyed to a specific instance's id, so a
        // fuzzy hit on a same-app sibling must not keep a dead entry alive —
        // a retained stale entry later flips knownFreeFloating to true and
        // poisons the free-geometry capture.
        KWin::EffectWindow* window = m_effect->findWindowByIdExact(windowId);
        if (!window || window->isDeleted() || !m_managedScreens.contains(m_effect->getWindowScreenId(window))) {
            m_pendingFreshWindows.remove(windowId);
        }
    }
    return routedWindowIds;
}

void TilingHandler::handleDragToFloat(KWin::EffectWindow* w, const QString& windowId, bool immediate)
{
    // Restore border and clear tiling state synchronously — don't wait for
    // the daemon's async windowFloatingChanged signal, which may never arrive
    // (e.g., cross-screen drag where onWindowClosed removes daemon tracking
    // before setWindowFloatingForScreen processes).
    applyFloatCleanup(windowId);

    // Restore pre-autotile SIZE at the window's current position. The
    // all-bucket lookup matters here: size is coordinate-space-independent,
    // so any bucket's rect is safe.
    if (w) {
        const QRectF savedGeo = findPreTileGeometry(windowId);
        if (savedGeo.isValid()) {
            const int savedW = qRound(savedGeo.width());
            const int savedH = qRound(savedGeo.height());

            if (immediate) {
                // Drag-start path: apply synchronously during the
                // interactive move (allowDuringDrag=true) so the user
                // sees the window return to its free-floating size the
                // moment they start dragging — matches snap-mode behavior.
                // Re-center horizontally under the cursor so the window
                // doesn't "jump away" from the grab point when it shrinks.
                QRectF currentFrame = w->frameGeometry();
                const QPointF cursor = KWin::effects->cursorPos();
                int newX = qRound(currentFrame.x());
                int newY = qRound(currentFrame.y());
                if (currentFrame.width() > 0 && savedW < currentFrame.width()) {
                    const qreal cursorOffsetRatio = (cursor.x() - currentFrame.x()) / currentFrame.width();
                    newX = qRound(cursor.x() - cursorOffsetRatio * savedW);
                }
                QRect sizeRestored(newX, newY, savedW, savedH);
                // No animation profile, deliberately rather than by omission,
                // and matching the live drag-out unsnap in drag_snap.cpp:
                // allowDuringDrag skips the whole animated branch in
                // applyWindowGeometry, so a during-drag restore is instant by
                // construction and any profile handed in here would be a dead
                // argument. The window is following the pointer — it has to
                // resize under the cursor now, not ease into the size. The
                // drag-STOP arm below is not during a drag and does pass the
                // snap-out profile.
                m_effect->applyWindowGeometry(w, sizeRestored, /*allowDuringDrag=*/true, /*skipAnimation=*/false);
                qCInfo(lcEffect) << "Drag-start float: restored pre-autotile size for" << windowId << savedW << "x"
                                 << savedH;
            } else {
                // Drag-stop path: defer to next event loop tick so
                // KWin has finished the interactive move and the window's
                // frame geometry reflects the actual drop position.
                QPointer<KWin::EffectWindow> wp = w;
                PlasmaZonesEffect* effect = m_effect;
                QTimer::singleShot(0, effect, [effect, wp, windowId, savedW, savedH]() {
                    if (!wp || wp->isDeleted()) {
                        return;
                    }
                    // Skip if the window was re-snapped during the deferred tick
                    // (e.g., dropped on a zone on a snap screen during cross-VS drag).
                    if (!effect->isWindowFloating(effect->getWindowId(wp))) {
                        qCDebug(lcEffect) << "Drag-to-float: skipping size restore for re-snapped window" << windowId;
                        return;
                    }
                    QRectF currentFrame = wp->frameGeometry();
                    QRect sizeRestored(qRound(currentFrame.x()), qRound(currentFrame.y()), savedW, savedH);
                    // Snap-out: leaving tile-managed sizing.
                    effect->applyWindowGeometry(wp, sizeRestored, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                                PhosphorAnimation::ProfilePaths::WindowSnapOut);
                    qCInfo(lcEffect) << "Drag-to-float: restored pre-autotile size for" << windowId << savedW << "x"
                                     << savedH;
                });
            }
        }
    }

    m_effect->updateAllDecorations();
}

void TilingHandler::drainDeadSessionState()
{
    // Everything in this function describes the daemon session that just
    // ended. It runs on the successor's name-claim edge, before that successor
    // has emitted daemonReady and therefore before it can have pushed a single
    // signal — see the header for why draining any later destroyed the new
    // session's own state.
    //
    // The scrolling set is a pure discriminator with no lifecycle attached,
    // so clear the DEAD session's snapshot ahead of onDaemonReady's re-query:
    // an errored/timed-out Properties.Get (daemon still starting, or one
    // without org.plasmazones.Scrolling) is reply.isValid()-gated and would
    // otherwise leave the old set stamping Mode "scrolling" indefinitely.
    // m_managedScreens is deliberately NOT cleared here — it carries real
    // per-screen lifecycle state whose removal transitions run through
    // slotScreensChanged / the serviceUnregistered teardown.
    //
    // announceFlipped=false: this is BRING-UP, not a live engine flip. With the
    // dead session's set still in m_scrollingScreens and m_managedScreens not
    // yet cleared, the flip path would fire a windowsOpenedBatch whose
    // m_notifiedWindows inserts are wiped by the clears a few lines below —
    // leaving the daemon tracking windows the effect considers untracked until
    // loadSettings' own batch lands — and bump per-screen stagger generations
    // that m_tileStaggerGenByScreen.clear() then discards. loadSettings owns
    // the bring-up re-announce.
    //
    // Release the dead session's windowed-fullscreen members FIRST, and do it
    // here rather than leaning on the teardown's own release: the
    // announceFlipped=false call below cannot release them, its collection
    // loop riding the announce enumeration this path skips, so a member that
    // survived the teardown (or a bring-up that never saw one) would strand a
    // client at a dead column's fullscreen rect. Bring-up is exactly when the
    // effect's flags are stale; the adopt-on-batch arm re-establishes them
    // from the new daemon's truth. Idempotent after the teardown's release.
    restoreAllWindowedFullscreen();
    setScrollingScreens({}, /*announceFlipped=*/false);
    // The resolved per-screen scroll behaviour belongs to the dead session
    // too, and bring-up clears it rather than trusting the teardown to have:
    // a surviving crop set keeps blocksDirectScanout forcing composition
    // session-wide, and a surviving focus-follows-mouse set answers for
    // screens the new daemon has not published. loadSettings' fetch, once the
    // new daemon signals ready, re-seeds the whole map. Idempotent on the
    // teardown-first path (clearScrollingScreensForTeardown already ran it).
    clearScrollEffectBehaviourForTeardown();
    // The per-screen active-layout map is the same shape of dead-session
    // ruleQuery input, and the consequence of arriving at bring-up with it
    // intact is worse than for the scrolling set: m_activeLayoutsSeeded would
    // still be TRUE over the dead daemon's map, ActiveLayout rules would be
    // admitted and resolved against layouts the new daemon has not published,
    // and the seeding edge that re-drives the admission filter could never
    // fire again for this session. Idempotent on the teardown-first path.
    //
    // The teardown variant's pairing contract is satisfied by this function's
    // own tail: invalidateAllRuleCaches (below, with the tiled-membership
    // clear) drops every verdict memoised against the dead map, and
    // scheduleBorderSweep rebuilds the decorations those verdicts baked in.
    clearActiveLayoutsForTeardown();
    // That call also re-slices the ActiveLayout-scoped rules back out of the
    // five effect-bound rule sets (which SURVIVE the teardown by design) and
    // SETS m_activeLayoutRulesWithheld when it removed any — so on this path
    // the marker is correct by construction, and the seed edge fired by the
    // reply to onDaemonReady's loadSettings re-drives the fetch that restores
    // them.
    //
    // m_activeLayoutRulesWithheld is deliberately never CLEARED alongside the
    // unseeding. Every loadRuleAnimationsFromDbus reply that PARSES recomputes
    // it outright (shader_config_dbus.cpp assigns, never ORs); the
    // malformed-payload and over-cap arms return before that assignment and
    // re-arm the marker to TRUE on the way out, since the seed edge consumed
    // it before dispatching and those arms admit nothing.
    // Clearing it here would be unsafe in the one case that matters: if this
    // bring-up's getAllRules errors or times out, no reply recomputes the
    // marker, and a cleared marker leaves the withheld ActiveLayout rules
    // disarmed for the rest of the session because the seed edge in state.cpp
    // is gated on it. A stale-TRUE marker costs exactly one redundant re-drive,
    // which is the safe direction. The seed edge consumes and clears it.
    // Void the DEAD session's in-flight managedScreens property reply too:
    // onDaemonReady's loadSettings re-queries, and a stale reply from the
    // previous daemon would otherwise pass its generation gate and reinstate
    // a screen set the new daemon never published.
    ++m_screensSignalGeneration;
    // The per-session drain lives in one named function so the list has a
    // single home (see the header for the paired invariant and for why the
    // serviceUnregistered teardown deliberately drains a smaller set).
    clearPerSessionDaemonState();
    // Paired with the tiled-membership clear inside that drain: every memoised
    // rule verdict was resolved against the dead session's IsTiled membership.
    m_effect->invalidateAllRuleCaches();
    // The cache clear alone revives nothing — an IsTiled-keyed border or
    // opacity rule is baked into the decoration at build time.
    m_effect->scheduleBorderSweep();
}

void TilingHandler::onDaemonReady()
{
    // The dead session's state is already gone: drainDeadSessionState ran on
    // the serviceRegistered edge, which precedes this signal by the whole of
    // the daemon's start(). Nothing here may re-clear it — the new daemon has
    // been pushing since it claimed the name, and this is where that push is
    // completed rather than discarded.
    //
    // Connect BEFORE querying: a screensChanged emitted after the daemon
    // serves Properties.Get but before our AddMatch lands would be both lost
    // and unable to bump the generation guard. Connect-then-query is
    // strictly sound — a signal lost pre-AddMatch implies the Get (served
    // after the change) already returns the new set.
    connectSignals();
    // No m_initialScreenQueryPending write here: loadSettings() arms it for
    // its query and the reply clears it — a false store first was dead.
    loadSettings();

    // Re-send the effect's pre-autotile geometry cache to the freshly
    // (re)connected daemon as a backstop. storePreTileGeometry lands in the
    // unified WindowPlacementStore record (which IS persisted), but a record
    // the store had not flushed before the daemon died — or a daemon started
    // with wiped state — would leave already-tiled windows with no
    // pre-autotile position to return to on autotile→snap or drag-to-float.
    // The effect survives daemon restarts and still holds each window's true
    // pre-autotile frame here. overwrite=false so anything the daemon
    // restored from its own persisted records wins.
    if (m_effect->m_daemonGate.serviceRegistered) {
        int resent = 0;
        for (auto scrIt = m_preTileGeometries.constBegin(); scrIt != m_preTileGeometries.constEnd(); ++scrIt) {
            const QString& screenId = scrIt.key();
            for (auto winIt = scrIt.value().constBegin(); winIt != scrIt.value().constEnd(); ++winIt) {
                const QRectF& geo = winIt.value();
                if (geo.width() <= 0 || geo.height() <= 0) {
                    continue;
                }
                // qRound, not truncation: fractional-scale sub-pixel residue
                // (matches the toRect() geometry-capture convention).
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("storePreTileGeometry"),
                    {winIt.key(), qRound(geo.x()), qRound(geo.y()), qRound(geo.width()), qRound(geo.height()), screenId,
                     false},
                    QStringLiteral("storePreTileGeometry"));
                ++resent;
            }
        }
        if (resent > 0) {
            qCInfo(lcEffect) << "Re-sent" << resent << "pre-autotile geometries to daemon after reconnect";
        }
    }
}

void TilingHandler::clearPerSessionDaemonState()
{
    m_notifiedWindows.clear();
    // Cleared with m_notifiedWindows, as its lifetime partner.
    //
    // Holding it across a restart was tried and is INERT: scrollTrackedScreenFor
    // also requires live tiled membership, and the serviceUnregistered teardown
    // runs clearTiledTracking() before this, so m_border is empty and every
    // retained entry fails that gate anyway. It cannot arm the clip for a
    // single frame. The clip genuinely re-arms when the new daemon's first tile
    // batch calls markWindowTiled, and that ordering is already safe:
    // notifyWindowsAddedBatch inserts BOTH maps synchronously before the D-Bus
    // dispatch, and the daemon cannot tile a window it has not been told about.
    m_notifiedWindowScreens.clear();
    m_scrollClipLossReported.clear();
    m_savedNotifiedForDesktopReturn.clear();
    m_savedPreTileForDesktopMove.clear();
    // The per-session scroll maps the serviceUnregistered teardown
    // clears, repeated here so bring-up is authoritative on its own rather
    // than on what that edge left behind. A stale commanded rect
    // re-arms the counter-assert against the dead session's position the
    // moment the new daemon's batches re-open the gates (and before they
    // overwrite the entry); a stale relocation entry paints a parked column at
    // the dead session's strip position; a stale offered column is read as a
    // column the client has already answered, so the first batch of the new
    // session skips offering it and hands back whatever size the window is
    // holding; the min-size cache says "already sent" about a daemon that never
    // heard it (mildest — the re-announce re-seeds it, cleared for symmetry
    // with the teardown). The relocation-entry
    // clear pairs with damage like its teardown twin: the removal changes
    // where the paint path draws those windows.
    if (!m_effect->m_scrollVisualDelta.isEmpty()) {
        m_effect->m_scrollVisualDelta.clear();
        if (KWin::effects) {
            KWin::effects->addRepaintFull();
        }
    }
    m_effect->m_scrollCommandedRects.clear();
    m_effect->m_scrollOfferedColumn.clear();
    m_effect->m_lastReportedMinSize.clear();
    // The tab-indicator model, colour verdicts and paint overrides describe
    // the dead session's strips. Without this drain the painter could keep
    // blitting pills for columns the new daemon never laid out: the replay
    // only names screens that still have a strip, and the "[]" retraction is
    // latched on the engine's own membership set, so neither retracts a
    // screen the new session simply does not know about. The bring-up fetches
    // in onDaemonReady's loadSettings re-seed what the new session has; the
    // clear pairs with a full repaint because clearAll damages nothing itself.
    clearScrollTabState();
    if (KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    // Tiled membership belongs to the dead session as well. The
    // serviceUnregistered teardown normally clears it (paired there with the
    // decoration restore); bring-up clears it again so it cannot reach the new
    // session carrying the previous daemon's IsTiled membership, with every
    // memoised rule verdict resolved against it. Idempotent on the
    // teardown-first path.
    //
    // Only the membership half of clearTiledTracking: m_managedScreens is
    // deliberately kept here for the reason given above, so its per-screen
    // removal transitions still run through slotScreensChanged.
    m_border.tiledWindowsByScreen.clear();
    // The rule-cache invalidate and border sweep this membership clear needs are
    // the CALLER's, immediately after this function returns: they are the
    // bring-up half of the pairing, and the teardown path already does its own.
    // The FFM suppression latch belongs to the dead session too: its anchor
    // names a cursor position from before the restart, and leaving it armed
    // swallows the first cursor move that lands within the resume radius.
    m_ffmSuppressPending = false;
    // Centering state is per-retile transient: the restarted daemon has no
    // memory of the zones these entries point at, and a stale
    // m_centeredWaylandZones entry that happens to equal the first
    // post-restart tile request would trip the skipMoveResize short-circuit
    // in slotWindowsTileRequested against a freshly-restored decoration,
    // leaving a title-bar-height gap because the geometry is never re-asserted.
    m_tileTargetZones.clear();
    m_centeredWaylandZones.clear();
    // Per-screen stagger generations describe the dead session's in-flight
    // batches. They are otherwise only ever inserted (one entry per distinct
    // screenId ever seen, never pruned), so resetting here both restarts the
    // staggered-apply epochs cleanly and keeps the map bounded across reconnects.
    //
    // The GLOBAL epoch is bumped alongside the clear, and the bump is the part
    // that actually retires the dead session's cascades. The clear alone puts
    // every per-screen value back to absent (reading 0), so the new session's
    // first batch on a screen re-derives 1 — exactly the value a pre-restart
    // batch still cascading captured. Both guard sites test the global epoch
    // first and unconditionally, so one bump voids every straddling batch. It
    // creates no new supersession victim: the clear already voided the same
    // batches by the per-screen term.
    ++m_tileStaggerGeneration;
    m_tileStaggerGenByScreen.clear();
    // Daemon-owned cross-output move markers belong to the dead session. A
    // stale one-shot armed before the restart (windowOutputMoveExpected fired,
    // matching outputChanged not yet seen) would swallow the next genuine
    // outputChanged for that window — taking the bookkeeping-only path and
    // skipping the real transfer. Clear it like every other per-session map.
    m_expectedOutputMove.clear();
    // In-flight debounced minimize→float commits and minimize-float records
    // belong to the dead daemon session — a timer firing now would issue a
    // setWindowFloatingForScreen against state the new daemon never had, and
    // a stale record would mis-route the next unminimize. Pending
    // cross-screen size-restore connections are likewise per-session.
    // clearAllPendingMinimizeFloats() also cancels the pending deferred
    // unminimize→unfloat timers; an escapee's timeout would bail anyway when
    // ownership lookup misses after the clear below.
    clearAllPendingMinimizeFloats();
    m_minimizeFloatedWindows.clear();
    m_unfloatInFlight.clear();
    m_unfloatRetryAttempts.clear();
    m_untiledMinimizeFloats.clear();
    // Routes deferred against the dead daemon session and their provenance
    // markers must not survive into the new one: a stale m_pendingFreshWindows
    // entry silently upgrades a later re-add to knownFreeFloating=true, which
    // is the free-geometry overwrite this contract exists to prevent.
    m_pendingFreshWindows.clear();
    m_deferredWindowRoutes.clear();
    for (auto connIt = m_pendingCrossScreenRestore.begin(); connIt != m_pendingCrossScreenRestore.end(); ++connIt) {
        QObject::disconnect(connIt.value());
    }
    m_pendingCrossScreenRestore.clear();
    // Clearing is enough: the stamps are globally monotonic, so a watcher still
    // in flight across the daemon restart holds a non-zero stamp that a cleared
    // map (reading back 0) can never match.
    m_crossScreenRestoreGen.clear();
    // A stacking order saved before the restart describes a dead session's
    // z-order — the first post-restart retile's onComplete would replay it
    // and re-raise windows in stale order; same for a stale pending focus id.
    m_savedAutotileStackingOrder.clear();
    m_pendingAutotileFocusWindowId.clear();
    // The announce stamps describe windowOpened / windowsOpenedBatch calls made
    // to the DEAD daemon. Clearing is enough for the same reason it is for
    // m_crossScreenRestoreGen: the stamps are globally monotonic, so an error
    // reply still in flight across the restart holds a non-zero stamp that a
    // cleared map (reading back 0) can never match, and its rollback would
    // otherwise undo the tracking this bring-up's own re-announce establishes.
    m_announceGen.clear();
}

// handleAutotileFloatToggle removed: float toggle is now daemon-local via
// SnapAdaptor::toggleFloatForWindow (which emits applyGeometryRequested).

// connectSignals() / loadSettings() live in tilinghandler/wiring.cpp.

} // namespace PlasmaZones
