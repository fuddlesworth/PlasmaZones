// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>

/// Pure decision logic for the scroll-managed window mechanisms in
/// TilingHandler, extracted so the branch chains are unit-testable without a
/// compositor: kwin-effect has no linkable test target, but header-only
/// logic is covered by including this file directly (the established pattern
/// for the anchor-uniform and shader-timing helpers). The KWin-facing side
/// effects (setFullScreen, moveResize, layer demotion, D-Bus dispatch) stay
/// at the call sites in the tiling handler's and the effect's translation
/// units. These functions decide, they do not act.
namespace PlasmaZones::ScrollDecisions {

/// What the windowed-fullscreen half of a batch entry does for one window.
enum class WfsAction {
    None, ///< untouched (not flagged and not held, or adopt refused by the in-flight marker)
    Adopt, ///< flagged, not held: enter windowed fullscreen (also the effect-restart re-adoption)
    DeferredReconcile, ///< flagged + held but fullscreen not even REQUESTED: the client exited on
                       ///< its own while the daemon gate was closed — drop membership and tell the daemon
    Refresh, ///< flagged + held + requested: steady state, refresh the stored rect and the demotion
    Release, ///< un-flagged while held: leave windowed fullscreen
};

struct WfsDecision
{
    WfsAction action = WfsAction::None;
    /// Consume the clear-in-flight marker, BEFORE the action runs. Set by the
    /// two entries that spend it: a flag-off entry, the authoritative echo of
    /// a clearWindowedFullscreen this effect sent, and the flagged entry whose
    /// stale adopt the armed marker refuses, which spends it in the refusal
    /// (single-shot, see resolveWindowedFullscreenAction).
    bool consumeClearMarker = false;
};

/// The arm's name, for the batch consumer's log line.
///
/// A switch with no default rather than an array indexed by the enum: a new
/// arm is then a compiler warning here instead of a silently wrong label (or,
/// with an array, a read past the end). Same reasoning as the labelled
/// no-default arms in the strip's height resolver.
inline const char* wfsActionName(WfsAction action)
{
    switch (action) {
    case WfsAction::None:
        return "None";
    case WfsAction::Adopt:
        return "Adopt";
    case WfsAction::DeferredReconcile:
        return "DeferredReconcile";
    case WfsAction::Refresh:
        return "Refresh";
    case WfsAction::Release:
        return "Release";
    }
    return "?";
}

/// The 5-way windowed-fullscreen batch decision over its four inputs.
/// @p flagOnWire      the batch entry's isWindowedFullscreen
/// @p inSet           effect-side membership (m_windowedFullscreenWindows)
/// @p requestedFullscreen KWin's isRequestedFullScreen() for the window
/// @p clearInFlight   the clear-in-flight marker is armed for this window
inline WfsDecision resolveWindowedFullscreenAction(bool flagOnWire, bool inSet, bool requestedFullscreen,
                                                   bool clearInFlight)
{
    WfsDecision d;
    if (!flagOnWire) {
        d.consumeClearMarker = clearInFlight;
        d.action = inSet ? WfsAction::Release : WfsAction::None;
        return d;
    }
    if (!inSet) {
        // The armed marker refuses re-adoption: a batch the daemon emitted
        // BEFORE processing our clear can still carry flag=true, and
        // adopting it would re-fullscreen the window the user just exited.
        //
        // SINGLE-SHOT. The marker only has to outlive the batches already in
        // flight when the clear was sent, which is normally one, and consuming
        // it here is the only thing that bounds it. Waiting for a flag=false
        // entry to consume it latches for the session whenever the user
        // re-enters windowed fullscreen before the daemon's flag-off batch
        // arrives: the flag goes true again, no flag-off entry is ever
        // emitted, and every later batch is refused. The successful reply
        // cannot consume it either, because the reply can land before the
        // stale batch, which is the whole race the marker exists for.
        if (clearInFlight) {
            d.consumeClearMarker = true;
            d.action = WfsAction::None;
            return d;
        }
        d.action = WfsAction::Adopt;
        return d;
    }
    // Requested state, not committed: during our OWN enter round-trip the
    // committed isFullScreen() lags a client round-trip while requested is
    // already true, and a batch landing in that window must not read the
    // lag as a client exit.
    d.action = requestedFullscreen ? WfsAction::Refresh : WfsAction::DeferredReconcile;
    return d;
}

/// What the column-maximize half of a batch entry does for one window.
enum class MaximizeAction {
    None, ///< the KWin bit already agrees with the engine
    Apply, ///< engine says maximized and KWin's bit is not set (or we are not yet a member)
    Release, ///< engine dropped the maximize while we hold the bit
};

/// The column-maximize batch decision over its four inputs.
///
/// TAKES AN IN-FLIGHT MARKER, like its windowed-fullscreen sibling above,
/// though for a different reason. That one needs one because its dispatch
/// (clearWindowedFullscreen) drops effect-side membership at once, opening a
/// window where a pre-clear batch still carrying flag=true would take the
/// Adopt arm and re-fullscreen the window the user just exited.
///
/// This one needs one because the interception no longer writes KWin's bit
/// before dispatching. It used to cancel the user's flip back to whatever the
/// engine last said, which meant effect-side state agreed with the pre-toggle
/// flag for the whole round trip and a stale batch genuinely did resolve to
/// None on its own terms. That is where the older "no marker is needed"
/// reasoning came from, and deleting the cancel took the guarantee with it.
///
/// What is exposed without a marker is the RESTORE direction only. The user's
/// click has already cleared KWin's bit, membership has not moved, and a batch
/// the daemon emitted before it dequeued the toggle still carries flag=true —
/// (1, 1, 0), which is Apply. The window is re-maximized mid-flight and the
/// same batch's geometry apply commits the stale maximized rect, so it flashes
/// back to full width until the answering batch lands. Maximize is (0, 0, 1)
/// and resolves to None, so the exposure is asymmetric.
///
/// Only the `inSet && !kwinMaximized` leg is suppressed. The adopt leg
/// (`!inSet`) and Release must both still run while armed, or the engine's own
/// answer would be dropped. The suppressed leg exists to repair an effect
/// restart or a KWin-dropped bit, neither of which is time-critical, so
/// deferring it by a batch costs nothing.
///
/// @p flagOnWire     the batch entry's maximizedToEdges
/// @p inSet          effect-side membership (m_maximizedToEdgesWindows)
/// @p kwinMaximized  whether KWin holds MaximizeFull. The batch arm, the one
///                   caller, passes requestedMaximizeMode(), not the committed
///                   maximizeMode(): the committed bit trails a client round
///                   trip on Wayland, and reading it re-resolves to Apply on
///                   every batch that lands inside that window.
/// @p toggleInFlight a dispatched toggleMaximizeToEdges for this window whose
///                   reply has not arrived. Deliberately has NO default
///                   argument: a defaulted parameter would let the call site
///                   keep compiling while silently never passing the marker,
///                   which is a fix present in the header and absent in
///                   production that no test would catch.
inline MaximizeAction resolveMaximizeToEdgesAction(bool flagOnWire, bool inSet, bool kwinMaximized, bool toggleInFlight)
{
    if (!flagOnWire) {
        return inSet ? MaximizeAction::Release : MaximizeAction::None;
    }
    // A stale pre-toggle batch on the restore direction lands exactly here.
    if (inSet && !kwinMaximized && toggleInFlight) {
        return MaximizeAction::None;
    }
    // Apply covers two situations that want the identical side effect: a
    // column that just became maximized, and a member whose bit went missing
    // (an effect restart with the daemon still holding the state, or KWin
    // dropping it during a screen change). kwinMaximized is what keeps the
    // steady state from re-calling maximize() on every batch of every tile of
    // a maximized column.
    return (!inSet || !kwinMaximized) ? MaximizeAction::Apply : MaximizeAction::None;
}

/// Whether a maximize Apply arm that is about to call maximize() takes the
/// claim (F680). Only for a window KWin will maximize, or one already at
/// MaximizeFull (the adopt case): KWin refuses a window that cannot be
/// maximized, and a claim over a bit it never holds kept the counter-assert
/// exempt for good while the decision resolved Apply on every batch anyway.
/// An arm that skips the call (fullscreen, a gesture) takes the claim as it
/// always did, so a later batch or the gesture end can pay it.
inline constexpr bool takesMaximizeMembership(bool maximizable, bool alreadyFull)
{
    return maximizable || alreadyFull;
}

/// Whether a column-maximize arm is the answer to a press the maximize
/// interception claimed (F989), given a live marker for it. The interception
/// dispatches without writing KWin's bit, so KWin already holds the mode the
/// press asked for when the answer lands, and the arm writes nothing: an Apply
/// that finds KWin maximized, or a Release that finds it restored. That answer
/// anchors its leg at the press's departure rect, and a Release rides placeOut.
/// The effect-restart adopt (Apply over a held bit) carries no marker.
inline constexpr bool answersClaimedPress(MaximizeAction action, bool kwinMaximized, bool markerLive)
{
    return markerLive
        && ((action == MaximizeAction::Apply && kwinMaximized)
            || (action == MaximizeAction::Release && !kwinMaximized));
}

/// Where a leg of a window the strip's view carries starts: @p from, rounded,
/// backed out by @p viewDelta along the strip's axis. The paint position is the
/// animated rect plus the view offset, which starts at viewDelta, so the first
/// frame lands on @p from. Rounded because AnimatedValue judges a degenerate
/// leg with no pixel epsilon, and fractional residue would start a spring on
/// every scroll tick. @p from is the live frame, or the frame a claimed
/// maximize press left (F989).
inline QRect viewCarriedOrigin(const QRectF& from, int viewDelta, bool vertical)
{
    return QRect(qRound(from.x()) - (vertical ? 0 : viewDelta), qRound(from.y()) - (vertical ? viewDelta : 0),
                 qRound(from.width()), qRound(from.height()));
}

/// Whether an apply that finds the window already commanded to its target
/// still owes a leg (F989). Only the answer to a claimed press does: KWin's own
/// flip already commanded the batch's rect, so the leg from the press's
/// departure is the only motion the press gets. It needs an origin that is not
/// the target itself. Every other apply at its target stays a no-op.
inline bool claimedPressOwesLeg(bool answeringClaimedPress, const QRectF& origin, const QRect& target)
{
    return answeringClaimedPress && origin.isValid() && origin.toRect() != target;
}

/// The counter-assert burst budget for scroll-managed windows an EXTERNAL
/// mover relocated: RATE-LIMITED to @p maxPerWindow counters per rolling
/// window of @p windowMs, re-armed by every fresh batch command (the caller
/// resets the pair on batch insert). Mutates the per-window bookkeeping in
/// place and answers whether THIS frame change should be countered.
/// @p frameDiffers is whether the counter would move the window at all, and a
/// change that would not consumes nothing.
inline bool shouldCounterAssert(qint64& burstStartMs, int& burstCount, qint64 nowMs, bool frameDiffers,
                                qint64 windowMs = 1000, int maxPerWindow = 3)
{
    if (!frameDiffers) {
        return false;
    }
    if (nowMs - burstStartMs > windowMs) {
        burstStartMs = nowMs;
        burstCount = 0;
    }
    if (burstCount < maxPerWindow) {
        ++burstCount;
        return true;
    }
    return false;
}

/// Whether the counter-assert treats a scroll-managed window as one an
/// EXTERNAL mover can relocate. An X11 client moves itself through
/// ConfigureRequests; a Wayland client cannot, so a Wayland window qualifies
/// only where KWin is the mover: its maximize-area re-assert over a column
/// maximized to the edges (@p maximizedMember), and on KWin 6.8 the
/// keep-visible clamp it applies to a commit that acks a configure or changes
/// the window geometry, which pulls a column the strip committed outside its
/// output (@p outOfViewEntry: a park, or a straddler cropped at the edge) back
/// in. Never mid maximize transition: that frame change is a maximize or
/// restore, not a mover.
inline constexpr bool counterAssertMovable(bool inMaximizeTransition, bool waylandClient, bool maximizedMember,
                                           bool outOfViewEntry)
{
    return !inMaximizeTransition && (!waylandClient || maximizedMember || outOfViewEntry);
}

/// Whether the counter-assert puts a window back by POSITION alone, a move()
/// that sends no configure of its own (handing the window to another output
/// still can, see the counter-assert) and leaves a running leg alone, instead of
/// re-applying the commanded rect (which drops the leg). Only for a window the
/// strip committed outside its output. A plain Wayland column always: the
/// clamp moved it and nothing resized it, and a client that holds its own size
/// would only refuse a re-offered column again. Anything else only when the
/// size still matches the command, so a mover that also RESIZED it (a client
/// re-asserting a saved size, KWin's maximize re-assert) still gets the
/// commanded size back.
inline constexpr bool counterAssertsByPosition(bool outOfViewEntry, bool waylandClient, bool maximizedMember,
                                               bool sizeMatches)
{
    return outOfViewEntry && ((waylandClient && !maximizedMember) || sizeMatches);
}

/// Where a position-only counter seats a frame of size @p live in @p column:
/// centred, with the shift clamped at zero so a frame larger than its column
/// stays at the column's origin. The same centring the offered-column pass in
/// the frame hook and the paint resolver (scrollVisualTranslationFor) apply,
/// so the three agree on where the window sits.
inline QPoint reseatOrigin(const QRect& column, const QSize& live)
{
    return QPoint(column.x() + qMax(0, column.width() - live.width()) / 2,
                  column.y() + qMax(0, column.height() - live.height()) / 2);
}

/// Whether the scroll size-continuity cache may carry a client's committed
/// size forward as this batch's offer, instead of offering the column rect.
///
/// The cache (`m_scrollOfferedColumn`) exists to end a few-pixel size
/// renegotiation with clients that enforce their own geometry: once such a
/// client has answered for a given column SIZE, the batch offers the size it
/// actually holds, centred in the column, which is a pure move it cannot
/// renegotiate. Extracted here because the whole defect it caused was a
/// MISSING TERM in an inline guard, which is the one shape a truth table
/// catches and a reviewer does not.
///
/// @p declaredRect  the column's rect is DECLARED state — maximize-to-edges,
///                  monocle, or windowed fullscreen. Never carried forward:
///                  in those the gap between the declared rect and the
///                  client's preference is the whole point of the state, so
///                  honouring the client commits a maximized window at its
///                  pre-maximize size in the middle of the screen. (The
///                  mechanism's own premise — that the difference is a few
///                  pixels of renegotiation — is simply false for them.)
/// @p columnAnswered  a cache entry exists whose SIZE equals this column's.
/// @p committed     the client's current frame size.
/// @p column        this batch's column size.
///
/// A committed size LARGER than the column on either axis is refused too. It
/// can be a stale frame from a bigger column the client has not been
/// reconfigured out of yet, which is reachable on the un-maximize batch: the
/// column is back at its stored width while the client still holds the raw
/// work area, and carrying it forward would commit the maximized size at the
/// restored column's origin. It can also be a genuine answer from a client that
/// will not fit the column, and that one is not this cache's to settle either:
/// the frame hook reports it to the engine (shouldReportCommittedSize), which
/// widens the column to it, and re-offering the column keeps the client's
/// answer readable until then. Carrying it forward would freeze an overhang the
/// engine has not been told about.
inline bool mayCarryCommittedSize(bool declaredRect, bool columnAnswered, const QSize& committed, const QSize& column)
{
    if (declaredRect || !columnAnswered || committed.isEmpty() || committed == column) {
        return false;
    }
    return committed.width() <= column.width() && committed.height() <= column.height();
}

/// Whether a strip batch's entry is a new offer to a Wayland client: it sends
/// the client a SIZE, so whatever the client settles on next answers this
/// column (ColumnAnswer::Owed). @p commanded is KWin's move-resize size before
/// the apply, @p delivered the size the entry delivers (the column, or a size
/// mayCarryCommittedSize carried). It is the split both arms of
/// applyWindowGeometry take: an equal size goes out as a pure move, or not at
/// all, and asks the client nothing.
///
/// No rect has to move for an entry to be an offer. At rest KWin's move-resize
/// size is the client's committed frame, so a batch the engine forces with
/// every rect unchanged, as it does when minimum sizes are turned on (F50),
/// offers the column again to a client whose frame differs from it, and the
/// answer the engine could not learn while they were off is read again. A
/// client whose smaller answer is carried is not re-offered, and needs not
/// be: a narrower answer never sets a floor.
inline bool stripEntrySendsSize(const QSize& commanded, const QSize& delivered)
{
    return commanded != delivered;
}

/// Whether a geometry request made outside every effect apply resizes a strip
/// window on KWin's account (its Expand and Shrink Window verbs, a script
/// setting the frame), so the frame the client acks for it must not read as
/// the client's answer to its column (F141). @p requested is KWin's
/// move-resize geometry at the request: Window::moveResize, move and resize
/// store it before the frameGeometryAboutToChange signal that carries the
/// request. A request that keeps the frame's size, or asks for exactly the
/// column the strip offered, resizes nothing of KWin's own: the keep-visible
/// clamp, a script's move, or a move while the strip's own resize is in
/// flight.
///
/// The sizes are rounded on their own (QSizeF::toSize), never through
/// QRectF::toRect, whose rounded size depends on where the rect sits: a move
/// that keeps a fractional size would round the two rects a pixel apart and
/// read as a resize.
inline bool resizesOnKWinsAccount(const QRectF& requested, const QRectF& frame, const QSize& offered)
{
    const QSize requestedSize = requested.size().toSize();
    return requestedSize != frame.size().toSize() && requestedSize != offered;
}

/// Whether the configure KWin sent for a quick tile it has just been made to
/// drop can still be acked at the tile's size after the strip put the column
/// back, so the effect must answer that ack (WindowCommandStamps::StaleAck)
/// rather than let the tile's frame land and read as the client's answer to
/// its column (F123).
///
/// @p requestedAtDrop  KWin's move-resize size just before the drop: the
///                     tile's size, which a configure may already carry.
/// @p committedAtDrop  the client's frame size then.
/// @p commandedAfter   KWin's move-resize size once the column is back.
/// @p committedAfter   the client's frame size then.
///
/// Outstanding when a size other than the frame's was requested at the drop
/// (the tile's configure may be on its way to the client or already sent) and
/// the column put back asks for the size the client already has, which KWin
/// applies with no configure of its own (XdgSurfaceWindow::moveResizeInternal),
/// so nothing supersedes the tile's. A tile the client had already acked is
/// left with the frame at the tile's size, and the column then goes out in a
/// configure of its own.
inline bool droppedTileAckOutstanding(const QSize& requestedAtDrop, const QSize& committedAtDrop,
                                      const QSize& commandedAfter, const QSize& committedAfter)
{
    return requestedAtDrop != committedAtDrop && commandedAfter == committedAfter;
}

/// Where a Wayland strip window's answer to the column it was offered stands
/// (ScrollOfferedColumn::answer in the effect).
///
/// The answer is the frame the client settles on after the strip sent it a
/// column SIZE, and each such offer owes one reading of it: from the frame
/// change that carries it, or from the client's commit when the settle leaves
/// the frame unchanged (a client answering with the frame it already had, or
/// settling on a state-only configure that followed the resize), which emits no
/// frame change at all. Once read, a size the client later settles on by
/// itself is read too (isNewColumnAnswer).
enum class ColumnAnswer : quint8 {
    Owed, ///< a batch sent the client this column's size; no settled answer has been read since
    Read, ///< the settled answer was read: reported to the engine, or nothing to report
    /// The frame answers a resize nobody on the strip asked for: KWin's own
    /// (its Expand and Shrink Window verbs, a script setting the frame, an
    /// axis-only maximize) or the user's. Marked when the request is made,
    /// from the size KWin requests then (resizesOnKWinsAccount). A
    /// re-decoration is not one: KWin makes no geometry request for it and
    /// configures the client to keep its frame size. Nothing the client
    /// commits answers the strip's column until the column is delivered
    /// again: the next batch, or a cancel that puts it back.
    Foreign,
};

/// The facts readsAsColumnAnswer weighs, each gathered by the effect at the
/// moment of reading. Named rather than positional, so a call site and a test
/// row say which gate they set.
struct ColumnAnswerGates
{
    /// scroll-managed: tracked on a strip, not floating, not deleted, not
    /// under a user move or resize (PlasmaZonesEffect::scrollManagedOutputFor)
    bool managed = false;
    /// no output change is in progress and the strip's output has not moved
    /// since its batch: the offer is in the coordinates the frame is (F815)
    bool coordinatesHold = false;
    /// KWin requests no maximize, fullscreen or tile for the window and it is
    /// not fullscreen. KWin commits the frame of its own maximize, fullscreen
    /// and quick tile before the state change that explains it.
    bool kwinStateAtRest = false;
    /// the resize hold still waits for the client's answer to the user's
    /// resize (PlasmaZonesEffect::m_resizeHold)
    bool resizeHeld = false;
    /// KWin's move-resize geometry equals the frame, so the frame is not one
    /// a configure still in flight is about to replace
    bool settled = false;
};

/// Whether a strip window's frame reads as its client's answer to the column
/// it was offered. The ONE predicate both consumers of the answer ask, the
/// report to the engine and the offered-column centring, so neither can act on
/// a frame the other would refuse.
///
/// @p settled is a statement about KWin's bookkeeping, not about configures:
/// KWin brings its move-resize geometry back in line with the frame once the
/// client acknowledges the last configure that places the window, so a client
/// acking an older configure late reads unsettled and its frame is not passed
/// off as the answer to the newest offer. It is not proof that nothing is in
/// flight: a move clears the placing flag of the configures still queued, so
/// after a move issued while several configures were queued, an older one's
/// ack can read settled (F49). The centring this predicate gates no longer
/// moves an unsettled window, which leaves a batch's size-preserving move
/// issued over queued resizes as the one producer of that ordering.
///
/// @p offered is the column rect, @p live the frame. A frame that left the
/// column entirely was moved elsewhere (KWin's Move to Screen, F973, F738).
/// An empty one, a mid-unmap commit, is refused by the same test, because
/// QRect::intersects is false for an empty rect.
inline bool readsAsColumnAnswer(const ColumnAnswerGates& gates, ColumnAnswer answer, const QRect& offered,
                                const QRect& live)
{
    return gates.managed && gates.coordinatesHold && gates.kwinStateAtRest && !gates.resizeHeld && gates.settled
        && answer != ColumnAnswer::Foreign && live.intersects(offered);
}

/// Whether a readable answer is one the engine has not heard yet: always while
/// the answer is Owed, and once Read only when the client has since settled on
/// another size by itself (@p lastRead is the size read then). A frame change
/// that kept the size, such as a pure move, repeats nothing.
inline bool isNewColumnAnswer(ColumnAnswer answer, const QSize& lastRead, const QSize& live)
{
    return answer == ColumnAnswer::Owed || (answer == ColumnAnswer::Read && live != lastRead);
}

/// The largest difference between a committed and an offered extent that is
/// not an answer. A server-side decoration with fractional borders at a
/// fractional scale rounds the frame of a client that took its column exactly
/// a pixel either way, depending on where the frame sits, and reporting that
/// pixel would leave the engine a floor the client never asked for.
///
/// MUST equal the engine's kCommittedExtentSlackPx (phosphor-scroll-engine,
/// enginelimits.h), which reads an answer within that slack of the offer as
/// the offer. The two sides cannot share the constant, because the engine's
/// is private to its library. A tolerance wider here would withhold answers
/// the engine would act on, and a narrower one would send it answers it
/// discards as the offer.
inline constexpr int CommittedSizeTolerancePx = 1;

/// Whether a readable answer goes to the engine (Scrolling.reportCommittedSize).
///
/// The engine lays a column out at the extent it asked for, so a client that
/// commits WIDER along the strip sits across the next column until the engine
/// hears about it, and one that later commits narrower than a column the
/// engine widened for it leaves that width standing empty.
///
/// @p offered    the column size the strip offered.
/// @p committed  the frame size the client committed.
/// @p vertical   the strip runs vertically, so its main axis is the height.
///
/// The MAIN axis reports a difference either way. The CROSS axis reports only
/// a longer answer, which hangs past the work area's edge and is the engine's
/// to judge (one that exceeds the work area floats the window like an
/// oversized declared minimum). A shorter cross answer is not reported: the
/// commit path centres it, and a client holding an aspect ratio answers a
/// full-height column a little short on every placement, which would
/// otherwise send the engine a no-op each time. Differences of
/// CommittedSizeTolerancePx or less are rounding, never answers.
///
/// Stateless itself; the effect asks it once per answer (isNewColumnAnswer),
/// so a client that keeps its answer is not reported again on every frame
/// change.
inline bool shouldReportCommittedSize(const QSize& offered, const QSize& committed, bool vertical)
{
    if (offered.isEmpty() || committed.isEmpty()) {
        return false;
    }
    const int mainDelta = vertical ? committed.height() - offered.height() : committed.width() - offered.width();
    const int crossDelta = vertical ? committed.width() - offered.width() : committed.height() - offered.height();
    return qAbs(mainDelta) > CommittedSizeTolerancePx || crossDelta > CommittedSizeTolerancePx;
}

// ── Compositor-state claims ────────────────────────────────────────────────
//
// The effect imposes three kinds of compositor state that only it can hand
// back, each tracked in its own per-window ledger. Which of them a given exit
// path releases is decided HERE, as data, rather than by whether somebody
// remembered to write the call at that site.
//
// That is the whole point: a missing release is an ABSENCE, so it compiles,
// tests and reviews clean, and what it leaves behind is a window holding
// compositor state with nothing recording the debt. The third claim below
// arrived missing eight of its releases, and the two asymmetries introduced
// while fixing those were the same shape. A table cannot forget a cell.

/// A kind of compositor state the effect imposed on a window.
enum class Claim {
    MonocleMaximize, ///< KWin maximize held for a monocle tile
    WindowedFullscreen, ///< KWin fullscreen + a keep-flag layer demotion
    MaximizedToEdges, ///< KWin maximize held for a maximized-to-edges scroll column
};

/// Why the effect's authority over a window is ending. Each exit path names
/// itself, and the table below says which claims answer to that name — so a
/// deliberate blank is a cell a reader can see rather than a call nobody
/// wrote.
///
/// MODEFLIP AND TEARDOWN HAVE NO PRODUCTION CALLER, and that is a decision,
/// not a gap. Every other scope routes its releases through releaseAllClaims,
/// directly or through cleanupAutotileTracking. These two do not, because the
/// funnel's shape is wrong for them:
///
///  - ModeFlip's release is deliberately SPLIT IN TWO. The demote pass sheds
///    before the engines re-place, and the pre-tile restores run after; one
///    funnel call at either end would collapse a split the ordering depends
///    on.
///  - Teardown releases in bulk across every tracked window and has to be
///    re-entrant (the daemon can drop mid-sweep). It handles that itself,
///    per claimReleaseOrder; funnelling it per-window would re-enter the
///    funnel's own guards N times to no benefit.
///
/// They stay in the enum because the table is what the tests drive, and
/// because the answer to "does monocle release on a mode flip" has to be
/// written down somewhere a reader can find it. Do not "finish the wiring"
/// by pointing them at releaseAllClaims without re-deriving their reasons.
enum class ClaimScope {
    /// The window is leaving the strip outright and no later batch will carry
    /// it: the untile diff and the active float channel (and the passive one
    /// on a scrolling screen, see passiveFloatScope).
    StripExit,
    /// The untrack funnel for a LIVE window, the scope cleanupAutotileTracking
    /// and releaseWindowTracking default to: a cross-output move
    /// (handleWindowOutputChanged's transfer, and its cross-mode arm), the
    /// flags-settle eviction, a drag whose start screen no engine owns any
    /// more, a suspended tile dropped on the snap path, and a batch float onto
    /// another screen. Every claim releases, the monocle maximize included,
    /// on LiveDeparture's terms: the window survives, and the screen it lands
    /// on re-claims what it runs. Left to the scrub, a monocle member kept
    /// KWin's maximize with no membership, which nothing hands back: the
    /// monocle arm takes no membership over a window already at MaximizeFull,
    /// and a float or a snapping screen sends no batch to clear it. A release
    /// that has to wait (fullscreen, a gesture) keeps its membership for the
    /// fullscreen exit or the gesture end to pay.
    UntrackFunnel,
    /// The untrack funnel for a closing window (onWindowClosed). Monocle does
    /// not release: a dying window has nothing to restore, so its membership
    /// is dropped bare by cleanupClosedWindowState's scrub.
    Close,
    /// The daemon's passive float channel, whose producers never reach the
    /// active funnel.
    PassiveFloat,
    /// A screen changed mode, desktop or activity: the demote pass (the loop
    /// over the windows of a screen leaving scrolling or autotile), the
    /// removed-screens sweep, the pre-tile restores.
    ModeFlip,
    /// A window left fullscreen on an exit arm no batch follows
    /// (slotWindowFullScreenChanged): a windowed-fullscreen member leaving it
    /// by itself, a window this handler no longer tracks, one with no tracked
    /// screen or on a screen no longer managed, and one floated during the
    /// hold. The repair for a maximize claim retained through the hold, whose
    /// release skipped while the window was fullscreen.
    FullscreenExit,
    /// Engine disable, daemon loss, daemon bring-up, effect unload. Every
    /// claim answers, and the ORDER matters (see claimReleaseOrder).
    Teardown,
    /// KWin moved the window off an output that disconnected. Its claims are
    /// scrubbed from the ledgers without touching KWin's state: a maximized or
    /// fullscreen evacuee stays that way, so KWin can still return it as it
    /// was when the output comes back.
    Evacuation,
    /// A placement on a snapping screen took the window: preparePlacement has
    /// already handed every claim back, anchored at the placement rect, so the
    /// untrack it runs releases none.
    SnapPlacement,
    /// A live window left the context in view (a desktop or activity edit) and
    /// survives off-screen: every claim releases, the monocle maximize included,
    /// against KWin's geometryRestore, so it does not arrive elsewhere holding a
    /// maximize nothing hands back (F675, l18_monocle_move). The funnel releases
    /// the monocle claim before its scrub drops the membership the release reads.
    LiveDeparture,
};

/// Whether @p claim releases on @p scope.
///
/// Every blank below is decided, and its reason is at its case or on the
/// scope. Changing one is a behaviour change and needs its own argument.
inline constexpr bool claimReleasesOn(Claim claim, ClaimScope scope)
{
    switch (scope) {
    case ClaimScope::StripExit:
        // All three: the defining case. Nothing else will carry the window.
        return true;
    case ClaimScope::UntrackFunnel:
        // All three: the window survives, and nothing here carries it on.
        return true;
    case ClaimScope::Close:
        // Monocle blank: a dying window has nothing to restore.
        return claim != Claim::MonocleMaximize;
    case ClaimScope::PassiveFloat:
        // Monocle is EXCLUDED, and documented at its site: re-driving a
        // maximize restore from a passive float signal has not been shown
        // safe against the monocle batch that owns that membership. The
        // other two have no such owner on this channel. passiveFloatScope
        // sends a float on a scrolling screen to StripExit instead.
        return claim != Claim::MonocleMaximize;
    case ClaimScope::ModeFlip:
        return true;
    case ClaimScope::FullscreenExit:
        // Windowed fullscreen blank: the window has just left fullscreen,
        // and the one arm that still holds a membership (a member leaving it
        // with the daemon gone) keeps it on purpose, for the batch consumer's
        // deferred reconcile. The other two may have retained a claim through
        // the hold, and this is their repair.
        return claim != Claim::WindowedFullscreen;
    case ClaimScope::Teardown:
        return true;
    case ClaimScope::Evacuation:
        // No claim releases: a release restores KWin's state, and an evacuee
        // keeps it. The caller scrubs the ledgers bare instead.
        return false;
    case ClaimScope::SnapPlacement:
        // Released already, by the placement, at the placement's rect.
        return false;
    case ClaimScope::LiveDeparture:
        // All three: the context the window lands in re-claims what it runs.
        return true;
    }
    return false;
}

/// Whether a monocle member's committed un-maximize is the user's (F1013). A
/// commit with either axis still maximized is not an un-maximize at all, and a
/// Restore commit while KWin still holds a MaximizeFull request is the late ack
/// of a configure a monocle batch has since superseded: on Wayland a
/// non-monocle batch and a monocle batch can land inside one client round trip,
/// and the client acks the Restore after the Full was requested.
inline constexpr bool isUserMonocleUnmaximize(bool committedHorizontal, bool committedVertical, bool requestedFull)
{
    return !committedHorizontal && !committedVertical && !requestedFull;
}

/// Whether KWin's restore rect for a monocle member leads back onto the output
/// its stack is on (F1011). A column maximized to edges and handed to a monocle
/// stack on another monitor keeps the strip frame KWin recorded there, so
/// un-maximizing it would send it back to that monitor. An invalid rect reads
/// as off the output.
inline bool restoreRectOnOutput(const QRectF& restore, const QRectF& outputGeometry)
{
    return restore.isValid() && outputGeometry.contains(restore.center());
}

/// Whether an output change of a strip tile is a move KWin made itself (its
/// Move to Screen, a window rule), not a column the engine parked or handed
/// over (F738). The engine's own moves arm a marker, a parked column sits off
/// every output by design, and a drag is the drop's. Anything else whose frame
/// has left the output of the rect the strip last gave it has gone to another
/// output: a column straddling its output's edge still intersects it.
/// @p commandedOutput the output under that rect (commanded, or the offered column)
inline bool isKWinMoveOffStrip(const QRectF& frame, const QRectF& commandedOutput, bool markerArmed, bool parked,
                               bool userMove)
{
    return !markerArmed && !parked && !userMove && commandedOutput.isValid() && frame.isValid()
        && !frame.intersects(commandedOutput);
}

/// The scope the passive float channel releases under (F723). On a scrolling
/// screen a float ends every claim (StripExit): a strip runs no monocle batch
/// that could own the membership, so a monocle member left over from an
/// autotile Monocle context has nothing else to hand its bit back. Anywhere
/// else the monocle membership stays with the autotile monocle batch
/// (PassiveFloat).
inline constexpr ClaimScope passiveFloatScope(bool onScrollingScreen)
{
    return onScrollingScreen ? ClaimScope::StripExit : ClaimScope::PassiveFloat;
}

/// Release order, lowest first. releaseAllClaims static-asserts it, and the
/// bulk teardowns (the engine disable, the daemon loss, the effect unload)
/// follow it by hand.
///
/// Windowed fullscreen goes BEFORE either maximize claim. Both maximize
/// releases skip a window that still holds (or has requested) fullscreen, and
/// on X11 setFullScreen(false) has already landed by the time the next claim
/// runs — so releasing fullscreen first is what lets a window holding both
/// get a real restore instead of a skip. Reversing this was a live regression
/// during PR #994's remediation.
inline constexpr int claimReleaseOrder(Claim claim)
{
    switch (claim) {
    case Claim::WindowedFullscreen:
        return 0;
    case Claim::MonocleMaximize:
        return 1;
    case Claim::MaximizedToEdges:
        return 2;
    }
    return 3;
}

/// The screens a scrolling-set change flips between engines inside the tiling
/// union, whose windows the effect re-announces: a screen entering the strip
/// from autotile (still in the managed set), or leaving it for autotile (the
/// daemon names those, because the managed set that says so lands after this
/// change). A desktop or activity switch flips nothing: the engines already
/// hold the context entered, and the switch's own announce places its windows
/// (F231, F286, F1004).
inline QSet<QString> engineFlipScreens(const QSet<QString>& oldSet, const QSet<QString>& newSet,
                                       const QSet<QString>& managed, bool isContextSwitch,
                                       const QSet<QString>& leavingToAutotile)
{
    if (isContextSwitch) {
        return {};
    }
    return ((newSet - oldSet) & managed) + ((oldSet - newSet) & leavingToAutotile);
}

/// The non-empty ids of a wire screen list, as a set. An empty id names no
/// screen, and in the scrolling set it would also match every window whose
/// screen lookup falls back to an empty QString (F359).
inline QSet<QString> nonEmptyIdSet(const QStringList& ids)
{
    QSet<QString> out;
    out.reserve(ids.size());
    for (const QString& id : ids) {
        if (!id.isEmpty()) {
            out.insert(id);
        }
    }
    return out;
}

} // namespace PlasmaZones::ScrollDecisions
