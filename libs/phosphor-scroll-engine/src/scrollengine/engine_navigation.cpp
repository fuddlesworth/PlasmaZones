// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/WindowRegistry.h>

#include "scrollenginelogging.h"
#include "scrollverbresolve_p.h"

#include <algorithm>

namespace PhosphorScrollEngine {

namespace {

int horizontalDelta(const QString& direction)
{
    if (direction == QLatin1String("left")) {
        return -1;
    }
    if (direction == QLatin1String("right")) {
        return 1;
    }
    return 0;
}

int verticalDelta(const QString& direction)
{
    if (direction == QLatin1String("up")) {
        return -1;
    }
    if (direction == QLatin1String("down")) {
        return 1;
    }
    return 0;
}

/// PHYSICAL direction token -> ROLE step, resolved against the screen's axis.
///
/// The token stays physical end to end: it is shared with the geometric
/// neighbour resolver, forwarded verbatim to the snap and autotile engines,
/// and rendered as an arrow by the navigation OSD. Only its INTERPRETATION
/// here depends on which way this screen's strip runs.
///
/// On a horizontal strip these are the historical mappings exactly. On a
/// vertical one up/down walk the strip and left/right walk the stack.
int mainDelta(const QString& direction, StripAxis axis)
{
    return axis.isHorizontal() ? horizontalDelta(direction) : verticalDelta(direction);
}

int crossDelta(const QString& direction, StripAxis axis)
{
    return axis.isHorizontal() ? verticalDelta(direction) : horizontalDelta(direction);
}

/// True for any of the four direction tokens, WITHOUT consulting an axis.
///
/// The empty-screen crossing arm needs this: P_SCROLL_RESOLVE leaves params
/// default-constructed when there is no state, so its axis is a meaningless
/// Horizontal there. Testing token validity through an axis-dependent helper
/// would read as axis-dependent over a value that is garbage on that path.
bool isDirectionToken(const QString& direction)
{
    return horizontalDelta(direction) != 0 || verticalDelta(direction) != 0;
}

/// The tile slot a window holds inside a SHARED column, plus a surviving
/// sibling to re-locate that column by once the window is gone. tileIndex
/// stays -1 when the window has its own column. The anchor exists for the
/// same reason FloatRestore::stackAnchor does: a bare column index goes
/// stale as columns close.
struct StackSlot
{
    int tileIndex = -1;
    QString anchor;
};

StackSlot stackSlotOf(const ScrollStrip& strip, const QString& windowId)
{
    StackSlot slot;
    const int col = strip.columnOfWindow(windowId);
    if (col < 0) {
        return slot;
    }
    const Column& column = strip.columns().at(col);
    if (column.tiles.size() < 2) {
        return slot;
    }
    slot.tileIndex = column.indexOfWindow(windowId);
    slot.anchor = column.anchorSiblingFor(slot.tileIndex);
    return slot;
}

} // namespace

void ScrollEngine::focusColumnByDelta(int delta, const QString& screenId)
{
    if (delta == 0) {
        return;
    }
    // The P_SCROLL_RESOLVE triple spelled out by hand, ONCE per notch (the
    // header doc on focusInDirectionResolved names the cost this removes), so
    // the axis picking the token and the screen the walk names agree. An
    // EMPTY resolve deliberately does NOT bail — the resolved body owns that
    // state, and the raw id is the axis source there, and only there.
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    const ScrollLayoutParams params = state ? layoutParamsForScreen(screen) : ScrollLayoutParams{};
    const StripAxis axis = state ? params.axis : stripAxisForScreen(screen.isEmpty() ? screenId : screen);
    // The TARGET screen's axis, so the wheel walks the strip on both
    // orientations: a call-site "left"/"right" walks the STACK on a vertical
    // strip and crosses to the physically-left monitor at its end.
    const QString token = Detail::physicalTokenForMain(delta, axis);
    if (token.isEmpty()) {
        return;
    }
    focusInDirectionResolved(token, PhosphorEngine::NavigationContext{QString(), screen}, screen, state, params);
}

void ScrollEngine::focusInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx)
{
    P_SCROLL_RESOLVE(ctx.screenId);
    focusInDirectionResolved(direction, ctx, screen, state, params);
}

void ScrollEngine::focusInDirectionResolved(const QString& direction, const PhosphorEngine::NavigationContext& ctx,
                                            const QString& screen, ScrollState* state, const ScrollLayoutParams& params)
{
    const QString action = QStringLiteral("focus");
    // Role steps, not physical ones: on a vertical strip up/down walk the
    // COLUMNS and left/right walk the stack. params carries this screen's
    // axis, and P_SCROLL_RESOLVE has already bound it.
    const int mainStep = mainDelta(direction, params.axis);
    const int crossStep = crossDelta(direction, params.axis);
    if (!state || state->strip().isEmpty()) {
        // An EMPTY screen still crosses (niri parity): a directional focus
        // press on a monitor with no strip walks onto the neighbour instead
        // of dead-ending — there is no in-strip candidate to prefer, so the
        // boundary IS the whole verb here. Only when no neighbour exists (or
        // the crossing found nothing to focus) does the empty screen answer
        // no_windows. Direction-gated like the non-empty arm below: this is
        // exported LGPL surface, and an embedder resolver that answers a
        // garbage token must not teleport focus off an empty screen.
        // Token validity only, checked WITHOUT the axis: there is no state
        // here, so P_SCROLL_RESOLVE left params default-constructed and its
        // axis is a meaningless Horizontal. mainStep and crossStep happen to
        // be correct for either axis (exactly one is non-zero for any of the
        // four tokens),
        // but reading them here would look axis-dependent over a garbage
        // value, so ask the question that is actually being asked.
        if (isDirectionToken(direction) && focusAcrossBoundary(screen, direction, QString())) {
            return;
        }
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    // Captured pre-op like the sibling verbs (move/swap/cycle): ctx.windowId
    // can be empty on a screen-hinted press, and the feedback's source slot
    // should name the window focus is leaving, not whatever the caller knew.
    const QString focusedBefore = state->strip().activeWindowId();
    const bool moved = (mainStep != 0) ? state->strip().focusAdjacentColumn(mainStep, params)
                                       : (crossStep != 0 && state->strip().focusAdjacentTile(crossStep));
    if (moved) {
        applyLayout(screen, true);
        // Focus is PERSISTED state: serializeStripState writes focusedWindow
        // and viewAnchor, and the only thing that marks DirtyScrollStrips is
        // the daemon's placementChanged connection. Without this emit a pure
        // focus walk (which moves both the active column and the view anchor)
        // never reaches the save, so the strip restores scrolled to whatever
        // column was focused before the walk. The move/tab/width verbs all
        // emit for the same reason.
        Q_EMIT placementChanged(screen);
        // Success carries the direction as the reason — the navigation OSD
        // derives its arrow from it (autotile fills the same slot).
        Q_EMIT navigationFeedback(true, action, direction, focusedBefore, state->strip().activeWindowId(), screen);
        return;
    }
    // Strip edge in ANY direction: cross onto the adjacent output, the
    // parity twin of moveFocusedInDirection's boundary arm — and of
    // autotile's plain focus, which already crosses outputs on the same
    // generic chord. Vertical crossings ride the same machinery: the
    // resolver answers "up"/"down" neighbours and entryWindowForCrossing's
    // vertical arm stands the target's own focused window in for the strip
    // edge a vertical press does not have.
    if ((mainStep != 0 || crossStep != 0) && focusAcrossBoundary(screen, direction, focusedBefore)) {
        return;
    }
    Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
}

bool ScrollEngine::focusAcrossBoundary(const QString& screenId, const QString& direction, const QString& focusedBefore)
{
    if (!m_crossSurfaceResolver) {
        return false;
    }
    const QString target = m_crossSurfaceResolver->neighborOutputInDirection(screenId, direction);
    if (target.isEmpty() || target == screenId) {
        return false;
    }
    const QString action = QStringLiteral("focus");
    if (!m_scrollingScreens.contains(target)) {
        // Different-mode neighbour: the daemon asks that engine for its
        // entry-edge window and activates it. The connection is
        // DirectConnection (enginewiring.cpp), so the handler has fully run
        // by the next line and the out-param carries its verdict — an empty
        // neighbour output is an everyday state for a focus, and announcing
        // a success card over a crossing that never happened is exactly what
        // the same-mode arm below refuses to do.
        bool handled = false;
        Q_EMIT crossModeFocusRequested(target, direction, &handled);
        if (!handled) {
            return false;
        }
        clearSourceFloatFocusAfterCrossing(screenId);
        Q_EMIT navigationFeedback(true, action, QStringLiteral("screen:") + direction, focusedBefore, QString(),
                                  target);
        return true;
    }
    const QString entry = entryWindowForCrossing(target, direction);
    if (entry.isEmpty()) {
        return false;
    }
    ScrollState* targetState = stateForKey(currentKeyForScreen(target), false);
    if (!targetState) {
        return false;
    }
    // focusWindow may answer false (the entry IS the neighbour's focused
    // window) — the crossing still happens: applyLayout's activation arm
    // activates the target strip's active window either way, with the
    // self-activation echo bookkeeping for free.
    targetState->strip().focusWindow(entry, layoutParamsForScreen(target));
    m_activeScreen = target;
    clearSourceFloatFocusAfterCrossing(screenId);
    applyLayout(target, true);
    Q_EMIT placementChanged(target);
    // Same "screen:<dir>" reason and destination-screen announcement as the
    // cross-output move (the snap convention).
    Q_EMIT navigationFeedback(true, action, QStringLiteral("screen:") + direction, focusedBefore, entry, target);
    return true;
}

void ScrollEngine::clearSourceFloatFocusAfterCrossing(const QString& sourceScreenId)
{
    // Focus demonstrably left the source output; its state must not keep
    // claiming the float layer holds focus there. The destination side is
    // handled by applyLayout's focus arm (same-mode) or by the compositor's
    // answering focus report (cross-mode).
    if (ScrollState* sourceState = stateForKey(currentKeyForScreen(sourceScreenId), false)) {
        sourceState->setFloatingHasFocus(false);
    }
}

void ScrollEngine::moveFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx)
{
    P_SCROLL_RESOLVE(ctx.screenId);
    const QString action = QStringLiteral("move");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    // While a float holds compositor focus the strip's activeWindowId still
    // names the last-focused TILE (windowFocused's float arm returns before
    // the strip is touched), so an unchecked move here would migrate a
    // background column the user is not looking at and announce success —
    // refuse instead, the same test moveFocusedToFloating applies. The
    // operand otherwise stays the strip's active window, NOT ctx.windowId
    // (the pinned contract crossOutputMoveKeepsHeightAndAnnouncesOnDestination
    // exercises with a deliberately mismatched ctx); a caller-named strip
    // member implies a genuine tile focus report already updated the strip.
    if (state->floatingHasFocus() && !state->strip().containsWindow(canonicalizeForLookup(ctx.windowId))) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), state->lastFloatingFocus(), QString(),
                                  screen);
        return;
    }
    // The window the move is about, captured BEFORE it moves: both success
    // arms name the same thing this way (the in-strip arm used to read the
    // post-move active window, the boundary arm ctx.windowId).
    const QString focused = state->strip().activeWindowId();
    const int mainStep = mainDelta(direction, params.axis);
    const int crossStep = crossDelta(direction, params.axis);
    const bool moved = (mainStep != 0) ? state->strip().moveActiveColumn(mainStep, params)
                                       : (crossStep != 0 && state->strip().moveActiveTile(crossStep));
    if (moved) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
        // Direction-as-reason on success: the OSD arrow reads it.
        Q_EMIT navigationFeedback(true, action, direction, focused, QString(), screen);
        return;
    }
    // Boundary in any direction: the strip has no further column (or the
    // column no further tile) this way — cross onto the adjacent output
    // when one exists. A vertical crossing enters the target as an appended
    // column (no strip edge to enter from; the handoffReceive convention).
    QString landingScreen;
    if ((mainStep != 0 || crossStep != 0)
        && moveActiveWindowAcrossBoundary(state, screen, direction, false, &landingScreen)) {
        // Same "screen:<dir>" spelling as autotile's cross-output move, and
        // announced on the DESTINATION screen (the snap convention): the
        // source output no longer holds the window the OSD is about.
        Q_EMIT navigationFeedback(true, action, QStringLiteral("screen:") + direction, focused, QString(),
                                  landingScreen);
        return;
    }
    Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
}

QString ScrollEngine::entryWindowForCrossing(const QString& screenId, const QString& direction) const
{
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    if (!state || state->strip().isEmpty()) {
        return QString();
    }
    // Params resolved once and threaded into the walks below: the public
    // visibleTiles overload would send this back through a second
    // ScreenManager query plus context-gap-provider call for the same values.
    //
    // Resolved BEFORE the entry-edge decision, because that decision needs
    // this screen's axis.
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    // The entry edge faces back toward the source: a crossing travelling in
    // the target's +main direction enters its LOW main edge, and vice versa.
    //
    // Keyed on the TARGET's axis, never the source's — screenId here is the
    // target, and both call sites pass it that way. A landscape monitor beside
    // a portrait one is precisely the topology this feature exists for, so the
    // two axes routinely differ.
    //
    // travel == 0 means the direction is PERPENDICULAR to the target's strip:
    // it has no edge facing the source, since every column spans the full
    // cross extent and none is nearer than another. That is today's
    // vertical-crossing arm, generalized rather than disturbed.
    const int travel = mainDelta(direction, params.axis);
    if (travel == 0) {
        // Stand-in only if it is actually ON screen: a hidden tab or a parked
        // column carries no zone number, and the contract at the tail of this
        // function (the entry slot may never address a tile the zone numbers
        // dropped) binds the perpendicular arm the same as the parallel one.
        const QString active = state->strip().activeWindowId();
        for (const VisibleTile& tile : visibleTiles(screenId, params)) {
            if (tile.windowId == active) {
                return active;
            }
        }
        return QString();
    }
    // Ranked over visibleTiles, not a private walk: one definition of
    // on-screen-ness for the whole engine (hidden tabs, parked columns and
    // empty-intersection tiles are excluded there), so the entry slot can
    // never address a tile the zone numbers dropped. Clipping does not
    // disturb the ranking — the clipped left/right edges keep the same
    // order as the true ones.
    QString best;
    int bestEdge = 0;
    for (const VisibleTile& tile : visibleTiles(screenId, params)) {
        const int edge = (travel > 0) ? params.axis.mainLow(tile.rect) : -params.axis.mainHigh(tile.rect);
        if (best.isEmpty() || edge < bestEdge) {
            best = tile.windowId;
            bestEdge = edge;
        }
    }
    // An EMPTY walk on a non-empty strip means nothing carries a number right
    // now (every column parked off-screen, or a work area the gaps swallowed).
    // The comment above is the contract: the entry slot may never address a
    // tile the zone numbers dropped, and the active window is exactly such a
    // tile here. Answer empty and let the caller degrade the swap to a move,
    // which is what it already does for an empty strip.
    return best;
}

int ScrollEngine::columnIndexForWindow(const QString& screenId, const QString& windowId) const
{
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    return state ? state->strip().columnOfWindow(canonicalizeForLookup(windowId)) : -1;
}

void ScrollEngine::adoptAsFloatAfterRefusal(ScrollState* owner, const QString& windowId, const QSize& minSize,
                                            const QString& announceScreen)
{
    // The FULL repair floatWindowInternal's heal arm applies to the "tracked
    // but absent" residue state: membership, a restore entry, the
    // mode-transition float marker, the rect/park/fullscreen drops (a
    // retained column rect would defeat applyLayout's emit-on-change gate on
    // the eventual re-adoption and answer lastManagedRect with a strip rect
    // for a float; a stale park edge would mis-anchor the arrival slide),
    // and the sync emit. Unlike the heal arm the CLAMP was captured before
    // takeWindow, so windowMinimumSize answers the client's real minimum
    // while the window floats and the eventual unfloat re-applies it instead
    // of a 0x0 default. Only the clamp: on a slotless entry (column and
    // tileIndex stay -1 — the source slot is genuinely gone) the unfloat
    // consumer never reads the width/display/height fields, so they are
    // deliberately not carried. The contains-guard keeps a pre-existing real
    // entry authoritative. Focus-memory seeding stays at the call sites:
    // only the mover leg holds compositor focus.
    owner->addFloating(windowId);
    if (!m_floatRestore.contains(windowId)) {
        FloatRestore restore;
        restore.minWidth = minSize.width();
        restore.minHeight = minSize.height();
        m_floatRestore.insert(windowId, restore);
    }
    m_scrollFloatedWindows.insert(windowId);
    m_lastAppliedRect.remove(windowId);
    m_parkedScrollEdge.remove(windowId);
    m_lastAppliedWindowedFs.remove(windowId);
    Q_EMIT windowFloatingStateSynced(windowId, true, announceScreen);
}

bool ScrollEngine::moveActiveWindowAcrossBoundary(ScrollState* state, const QString& screenId, const QString& direction,
                                                  bool swap, QString* landingScreen)
{
    if (!m_crossSurfaceResolver) {
        return false;
    }
    const QString windowId = state->strip().activeWindowId();
    if (windowId.isEmpty()) {
        return false;
    }
    const QString target = m_crossSurfaceResolver->neighborOutputInDirection(screenId, direction);
    if (target.isEmpty() || target == screenId) {
        return false;
    }
    if (landingScreen) {
        *landingScreen = target;
    }
    if (!m_scrollingScreens.contains(target)) {
        // Different-mode neighbour: the daemon relinquishes us and hands the
        // window to the owning engine (DirectConnection — synchronous). The
        // swap variant trades with the target's entry-edge window; the
        // daemon degrades it to a move when the entry slot is empty. Unlike
        // crossModeFocusRequested these signals carry no handled out-param,
        // so the true return here is OPTIMISTIC for this leg: a daemon-side
        // no-op still gets a success OSD. The autotile twin documents the
        // same trade (NavigationController.cpp's cross-mode move arm) — a
        // daemon-side failure emit cannot fix it because the handler runs
        // synchronously inside the Q_EMIT.
        if (swap) {
            Q_EMIT crossModeSwapRequested(windowId, target, 0, direction);
        } else {
            Q_EMIT crossModeMoveRequested(windowId, target, 0, direction);
        }
        // Optimistic like the return itself (focusAcrossBoundary gates the
        // identical call on its handled verdict; these signals carry none):
        // on a daemon no-op the flag reads false until the next genuine
        // focus report heals it, the same accepted trade as above.
        clearSourceFloatFocusAfterCrossing(screenId);
        return true;
    }
    // Scroll→scroll crossing: migrate between strips ourselves. The effect's
    // one-shot is armed inside each insert-success arm below — after the
    // strip accepted the window but before applyLayout issues the geometry —
    // so a refusal can never leave a stale marker armed to swallow the
    // window's next genuine outputChanged (the ordering rule
    // NavigationController.cpp documents for the autotile twin).
    const PhosphorEngine::PlacementStateKey sourceKey = currentKeyForScreen(screenId);
    const PhosphorEngine::PlacementStateKey targetKey = currentKeyForScreen(target);
    ScrollState* targetState = stateForKey(targetKey, true);
    if (!targetState) {
        return false;
    }
    const ScrollLayoutParams sourceParams = layoutParamsForScreen(screenId);
    const ScrollLayoutParams targetParams = layoutParamsForScreen(target);

    // All-or-nothing guard: the insert primitives' only reachable refusal for
    // this path is "the strip already contains the window" (the positional
    // fallback cannot fail any other way for a non-empty id), and that is a
    // double-tracking inconsistency best refused BEFORE the source strip is
    // mutated. With this check ahead of takeWindow, the refusal arms below
    // are pure defence and a crossing either completes or leaves both strips
    // untouched.
    if (targetState->containsWindow(windowId)) {
        // Double-tracking (one window in two current-context states) is a
        // state corruption with no locally-knowable correct side; refuse
        // and surface it rather than guessing which copy is stale. The old
        // mutate-then-refuse shape "healed" it only by untracking the
        // window entirely, which is not a repair. STATE-level containment,
        // not strip-only: a stale foreign FLOATING entry is the same
        // double-tracking, and inserting the window as a tile beside it
        // would make it simultaneously a target tile and a target float.
        qCWarning(lcScrollEngine) << "moveActiveWindowAcrossBoundary: target state already holds" << windowId
                                  << "— refusing before mutation";
        return false;
    }

    // Swap partner: the target's entry-edge window trades into the focused
    // window's vacated slot. Resolved BEFORE anything moves. A STACKED
    // source vacates a TILE slot, not a whole column: the column survives
    // takeWindow, so trading slots means re-entering that stack at the
    // vacated index — a plain positional insert would land the partner in a
    // fresh column beside the stack instead of in it.
    QString partner;
    int partnerLanding = -1;
    StackSlot partnerLandingSlot;
    if (swap) {
        partner = entryWindowForCrossing(target, direction);
        // Same all-or-nothing guard for the partner leg: refuse the whole
        // swap before anything moves rather than migrating one side.
        if (!partner.isEmpty() && state->containsWindow(partner)) {
            // State-level, matching the mover's guard above.
            qCWarning(lcScrollEngine) << "moveActiveWindowAcrossBoundary: source state already holds swap partner"
                                      << partner << "— refusing before mutation";
            return false;
        }
        partnerLanding = state->strip().columnOfWindow(windowId);
        partnerLandingSlot = stackSlotOf(state->strip(), windowId);
    }

    // Column intent, tile height intent and min size are all per-strip tile
    // state; capture every one of them before takeWindow so the crossing
    // preserves the user's width/display/height choices and the client's
    // clamp (the float round-trip does the same through FloatRestore).
    const QSize windowMinSize = state->strip().windowMinimumSize(windowId);
    // Value-anchored intent is screen-independent: the target's relayout
    // snaps the fraction into ITS vocabulary, so no cross-screen remap is
    // needed (the old index-based intent required one here).
    const WindowHeight windowHeight = state->strip().windowHeightIntent(windowId);
    // Windowed fullscreen is per-tile state too (niri keeps it across
    // move-column-to-monitor); without the carry the fresh tile on the
    // target defaults false and the crossing silently un-toggles it.
    const bool windowWindowedFs = state->strip().isWindowedFullscreen(windowId);
    const int sourceColIdx = state->strip().columnOfWindow(windowId);
    ColumnWidth windowWidth = targetParams.defaultColumnWidth;
    ColumnDisplay windowDisplay = effectiveDefaultColumnDisplay(target);
    if (sourceColIdx >= 0) {
        windowWidth = state->strip().columns().at(sourceColIdx).width;
        windowDisplay = state->strip().columns().at(sourceColIdx).display;
    }
    state->strip().takeWindow(windowId, sourceParams);

    // Entering from the facing edge: travelling in the target's +main
    // direction arrives as its FIRST column, the reverse as its last — unless
    // it takes the swap partner's slot, which for a STACKED partner is a slot
    // inside that partner's column rather than a column position.
    //
    // Keyed on the TARGET's axis (targetParams), never the source's: a
    // rightward press leaving a landscape screen for a portrait one is exactly
    // the topology this feature exists for.
    //
    // A PERPENDICULAR crossing has no facing strip edge, so it appends — the
    // same convention the daemon's handoffReceive documents for its
    // insertIndex -1 fallback, and what a vertical crossing already did.
    const int entryTravel = mainDelta(direction, targetParams.axis);
    int columnIdx = (entryTravel > 0) ? 0 : targetState->strip().columnCount();
    QSize partnerMinSize;
    WindowHeight partnerHeight;
    bool partnerWindowedFs = false;
    StackSlot moverLandingSlot;
    ColumnWidth partnerWidth = sourceParams.defaultColumnWidth;
    ColumnDisplay partnerDisplay = effectiveDefaultColumnDisplay(screenId);
    if (!partner.isEmpty()) {
        const int partnerColIdx = targetState->strip().columnOfWindow(partner);
        columnIdx = qMax(0, partnerColIdx);
        partnerMinSize = targetState->strip().windowMinimumSize(partner);
        partnerHeight = targetState->strip().windowHeightIntent(partner);
        moverLandingSlot = stackSlotOf(targetState->strip(), partner);
        if (partnerColIdx >= 0) {
            partnerWidth = targetState->strip().columns().at(partnerColIdx).width;
            partnerDisplay = targetState->strip().columns().at(partnerColIdx).display;
        }
        partnerWindowedFs = targetState->strip().isWindowedFullscreen(partner);
        targetState->strip().takeWindow(partner, targetParams);
    }
    // Shared stack-slot re-entry: try the vacated tile slot inside the
    // surviving stack first, fall back to the positional column insert.
    const auto insertCarryingStackSlot = [](ScrollStrip& strip, const StackSlot& slot, const QString& id,
                                            const ScrollLayoutParams& stripParams) {
        if (slot.tileIndex < 0) {
            return false;
        }
        const int anchored = strip.columnOfWindow(slot.anchor);
        return anchored >= 0 && strip.insertWindowIntoColumnAt(anchored, slot.tileIndex, id, stripParams);
    };
    bool moverInserted = insertCarryingStackSlot(targetState->strip(), moverLandingSlot, windowId, targetParams);
    if (!moverInserted) {
        moverInserted =
            targetState->strip().insertWindowAt(columnIdx, windowId, windowWidth, windowDisplay, targetParams);
    }
    if (moverInserted) {
        // Arm the effect's one-shot only for a crossing that actually
        // happened; applyLayout below is the geometry apply it must precede.
        Q_EMIT windowOutputMoveExpected(windowId, target);
        targetState->strip().setWindowMinimumSize(windowId, windowMinSize.width(), windowMinSize.height());
        // A non-Auto height claims the destination column's extent when that
        // column is tabbed, and unlike the unfloat and drag-cancel paths this
        // one carries no prior-ownership flag to restore. None is wanted: the
        // move focuses the window on arrival, so it IS the tab on show, and
        // the tab on show owning the column is the same rule the flip into
        // tabbed follows.
        //
        // Re-stated VERBATIM, not rescaled to the target's cross extent, the
        // same way the column width above crosses unrescaled. A Fixed intent
        // is a number the user (or the client) asked for, and this engine
        // does not reinterpret one against a different output; the relayout
        // clamps it to what the target can hold. The consequence worth
        // knowing: crossing to a shorter output clamps a tall intent and
        // moving back does not restore it, on both axes alike.
        targetState->strip().setWindowHeightIntent(windowId, windowHeight);
        if (windowWindowedFs) {
            targetState->strip().setWindowedFullscreen(windowId, true);
        }
        targetState->strip().focusWindow(windowId, targetParams);
        m_states.setKeyForWindow(windowId, targetKey);
        // The mover was just taken out of the source strip and re-inserted on
        // the target, so its retained rect belongs to the OTHER output. Left
        // standing it can equal what the target resolves and defeat
        // applyLayout's emit-on-change gate, so no batch would ever be issued
        // for the window that just crossed. The parked-edge memory belongs to
        // the other output too — a stale side would anchor the arrival slide
        // to the wrong edge of the NEW screen.
        m_lastAppliedRect.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId); // eviction symmetry with the float/handoff paths
    } else {
        // Refused, with the window already out of the source strip: adopt it
        // as a FLOAT of the source state rather than leaving it held by
        // neither side with no re-entry path short of a fresh windowOpened
        // (adoptAsFloatAfterRefusal carries the full repair set, built from
        // the tile state captured above). The reverse map still names the
        // source key (takeWindow does not touch it). The mover was the
        // source strip's ACTIVE window, so it still holds compositor focus
        // and no report will arrive to record the side change — seed the
        // focus-memory pair like floatWindowInternal's active-tile arm, or
        // moveFocusedToTiling answers "Nothing to restore" for a float the
        // user is looking at. Report failure below so the caller does not
        // announce a crossing that did not happen.
        state->setLastFloatingFocus(windowId);
        state->setFloatingHasFocus(true);
        adoptAsFloatAfterRefusal(state, windowId, windowMinSize, screenId);
        qCWarning(lcScrollEngine) << "moveActiveWindowAcrossBoundary: target strip refused" << windowId << "on"
                                  << target << "— adopted as floating on source";
    }
    if (!partner.isEmpty()) {
        // The partner's leg follows the mover's verdict. A completed swap
        // trades it into the vacated source slot; on the mover's refusal the
        // swap is OFF, and the partner — already taken out of the target
        // above — goes BACK to its own slot there. Migrating it to source
        // regardless left the target one window short and the source holding
        // BOTH halves (the mover floating, the partner tiled) of a crossing
        // the verb reports as failed.
        ScrollState* home = moverInserted ? state : targetState;
        const ScrollLayoutParams& homeParams = moverInserted ? sourceParams : targetParams;
        bool partnerInserted = insertCarryingStackSlot(
            home->strip(), moverInserted ? partnerLandingSlot : moverLandingSlot, partner, homeParams);
        if (!partnerInserted) {
            partnerInserted = home->strip().insertWindowAt(moverInserted ? qMax(0, partnerLanding) : columnIdx, partner,
                                                           partnerWidth, partnerDisplay, homeParams);
        }
        if (partnerInserted) {
            home->strip().setWindowMinimumSize(partner, partnerMinSize.width(), partnerMinSize.height());
            home->strip().setWindowHeightIntent(partner, partnerHeight);
            if (partnerWindowedFs) {
                home->strip().setWindowedFullscreen(partner, true);
            }
            if (moverInserted) {
                Q_EMIT windowOutputMoveExpected(partner, screenId); // same marker rule as the mover's arm
                m_states.setKeyForWindow(partner, sourceKey);
                m_lastAppliedRect.remove(partner); // same rationale as the mover's
                m_parkedScrollEdge.remove(partner);
                m_lastAppliedWindowedFs.remove(partner);
            }
            // The put-back changed no output: the partner keeps its target
            // key, its last-applied rect and its edge memory untouched.
        } else {
            // Same full repair as the mover's refusal, minus the focus
            // seeding: the partner never held compositor focus, so its
            // adoption must not claim the float layer's focus memory.
            adoptAsFloatAfterRefusal(targetState, partner, partnerMinSize, target);
            qCWarning(lcScrollEngine) << "moveActiveWindowAcrossBoundary: source strip refused swap partner" << partner
                                      << "on" << screenId << "— adopted as floating on target";
        }
    }
    if (moverInserted) {
        // Only a completed crossing moves the engine's active-screen hint;
        // on the defensive refusal path the user's focus never left source.
        // Focus also demonstrably left the source output, so its float
        // focus memory clears the same way focusAcrossBoundary's arms do.
        m_activeScreen = target;
        clearSourceFloatFocusAfterCrossing(screenId);
    }

    applyLayout(screenId, false);
    // Focus flag gated on moverInserted, same rationale as m_activeScreen
    // above: on the refusal path the crossing failed and is reported as
    // failure, so the target's applyLayout must not yank compositor focus
    // onto the other output's active window.
    applyLayout(target, moverInserted);
    Q_EMIT placementChanged(screenId);
    Q_EMIT placementChanged(target);
    // The partner's refusal (if any) is warned about above but does not gate
    // the verdict — the MOVER's crossing is what the caller announces.
    return moverInserted;
}

void ScrollEngine::swapFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx)
{
    // Within the strip, exchanging with the neighbour IS the move; the
    // difference shows at the boundary, where a swap trades with the
    // adjacent surface's entry window instead of just leaving.
    P_SCROLL_RESOLVE(ctx.screenId);
    const QString action = QStringLiteral("swap");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    // Same float-focus discipline as moveFocusedInDirection: refuse when a
    // float holds focus and the caller named no strip member. The operand
    // stays the strip's active window otherwise (the pinned contract).
    if (state->floatingHasFocus() && !state->strip().containsWindow(canonicalizeForLookup(ctx.windowId))) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), state->lastFloatingFocus(), QString(),
                                  screen);
        return;
    }
    // Pre-move focused window, for the same reason moveFocusedInDirection
    // captures one: both success arms must name the same window.
    const QString focused = state->strip().activeWindowId();
    const int mainStep = mainDelta(direction, params.axis);
    const int crossStep = crossDelta(direction, params.axis);
    const bool moved = (mainStep != 0) ? state->strip().moveActiveColumn(mainStep, params)
                                       : (crossStep != 0 && state->strip().moveActiveTile(crossStep));
    if (moved) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
        // Direction-as-reason on success: the OSD arrow reads it.
        Q_EMIT navigationFeedback(true, action, direction, focused, QString(), screen);
        return;
    }
    QString landingScreen;
    if ((mainStep != 0 || crossStep != 0)
        && moveActiveWindowAcrossBoundary(state, screen, direction, true, &landingScreen)) {
        // Destination screen, like the move twin: the traded-in partner is
        // what the source output now shows.
        Q_EMIT navigationFeedback(true, action, QStringLiteral("screen:") + direction, focused, QString(),
                                  landingScreen);
        return;
    }
    Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
}

void ScrollEngine::moveFocusedToPosition(int position, const PhosphorEngine::NavigationContext& ctx)
{
    P_SCROLL_RESOLVE(ctx.screenId);
    // "snap", not "move": this is the Snap-to-Zone digit path, and the OSD's
    // move copy renders a direction arrow a digit press has no direction
    // for. SnapEngine's twin verb uses the same action token.
    const QString action = QStringLiteral("snap");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    // Position N addresses the Nth VISIBLE tile (the zone-number space the
    // previews label — visibleTiles is the single source for both): the
    // strip may extend far off-screen, but the digits act on what the user
    // can see. What a digit MOVES is coarser than what it addresses; the
    // VisibleTile contract in ScrollEngine.h spells out the difference.
    // Params are reused, not re-resolved, all the way down this path.
    const QVector<VisibleTile> tiles = visibleTiles(screen, params);
    if (tiles.isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
        return;
    }
    // 1-based, and an out-of-range digit is REJECTED rather than clamped
    // (SnapEngine's convention): silently retargeting a position the strip
    // cannot honour moves a window the user never asked to move. Checked
    // before the subtraction so an INT_MIN off the wire cannot underflow.
    if (position < 1 || position > tiles.size()) {
        // invalid_zone_number renders "No zone with that number" in the OSD,
        // the accurate copy for an out-of-range digit (no_target reads as a
        // directional miss).
        Q_EMIT navigationFeedback(false, action, QStringLiteral("invalid_zone_number"), ctx.windowId, QString(),
                                  screen);
        return;
    }
    // The window the CALLER named is the one that moves (autotile honours
    // ctx.windowId the same way); the strip's own active window is only the
    // fallback for a screen-hinted press carrying no window.
    const QString requested = canonicalizeForLookup(ctx.windowId);
    const QString operating = state->strip().containsWindow(requested) ? requested : state->strip().activeWindowId();
    // Target captured by IDENTITY before anything moves: the focus change
    // below can re-center the view, and the index would then address a
    // different tile than the digit did.
    const QString targetWindow = tiles.at(position - 1).windowId;
    if (operating.isEmpty() || targetWindow.isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
        return;
    }
    if (targetWindow == operating) {
        // The digit named the window it would move: nothing to do, and
        // decided BEFORE any focus mutation so this arm leaves the strip
        // untouched. Emitted with success=false and NavigationController's
        // reason token, so both engines report the case identically and the
        // OSD renders its dedicated "already in that position" copy rather
        // than the snap success card.
        Q_EMIT navigationFeedback(false, action, QStringLiteral("already_at_position"), operating, targetWindow,
                                  screen);
        return;
    }
    // Focus is PERSISTED state and focusWindow re-anchors the view, so any
    // arm past this point must reach the applyLayout below even when the
    // move itself fails — otherwise on-screen geometry and DirtyScrollStrips
    // silently diverge from the model.
    const bool refocused =
        operating != state->strip().activeWindowId() && state->strip().focusWindow(operating, params);
    // targetWindow came out of THIS strip's own walk, so the lookup always
    // succeeds; the >= 0 guards only keep a -1 out of the movers below.
    const int targetColumn = state->strip().columnOfWindow(targetWindow);
    bool moved = false;
    if (targetColumn >= 0 && targetColumn != state->strip().activeColumnIndex()) {
        // Target tile lives in another column: the whole ACTIVE COLUMN
        // travels to that column's strip position, carrying its stack. The
        // operated window's own number can therefore differ from the digit
        // pressed — deliberate, and documented on the VisibleTile contract.
        moved = state->strip().moveActiveColumnTo(targetColumn, params);
    } else if (targetColumn >= 0 && state->strip().columns().at(targetColumn).display != ColumnDisplay::Tabbed) {
        // Target tile is a stack-mate of a NORMAL column: reorder the active
        // tile onto its slot. Read the operand from the column's
        // activeTileIdx, which is what moveActiveTile acts on —
        // activeWindowId() falls back past a minimized active tile and would
        // name a window the move never touches.
        //
        // A TABBED column is excluded by the guard above rather than assumed
        // unreachable. Only its shown tab carries a number, so a digit
        // landing here named that tab: when ctx names the column's own focus
        // the already_at_position arm took it, but a ctx naming a HIDDEN tab
        // of the same column reaches this branch, and reordering a window the
        // user cannot see onto a slot the user cannot see is not what the
        // digit asked for. It falls through to no_target instead.
        const Column& column = state->strip().columns().at(targetColumn);
        const int activeIdx = column.activeTileIdx;
        const int targetIdx = column.indexOfWindow(targetWindow);
        if (activeIdx >= 0 && activeIdx < column.tiles.size() && column.tiles.at(activeIdx).windowId == operating
            && targetIdx >= 0) {
            // Delta counts NON-MINIMIZED tiles between the two slots, which
            // is exactly the minimized-skipping step space moveActiveTile
            // walks.
            int steps = 0;
            for (int i = qMin(activeIdx, targetIdx); i < qMax(activeIdx, targetIdx); ++i) {
                steps += column.tiles.at(i).minimized ? 0 : 1;
            }
            moved = steps > 0 && state->strip().moveActiveTile(targetIdx > activeIdx ? steps : -steps);
        }
    }
    if (moved || refocused) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
    }
    if (moved) {
        // Target slot carries the OPERATED window (the one that landed), so
        // the OSD resolves the number the user's window now holds — passing
        // the displaced tile here rendered that other window's post-move
        // number instead. Matches SnapEngine, whose success feedback names
        // the landing zone.
        Q_EMIT navigationFeedback(true, action, QString(), operating, operating, screen);
        return;
    }
    // A refused move can still have REFOCUSED above (ctx named a hidden tab
    // of the target's own tabbed column): the focus mutation is kept — it is
    // what the digit's operand resolution asked for and it already reached
    // applyLayout — and the no_target verdict speaks only for the move.
    Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
}

void ScrollEngine::rotateWindows(bool clockwise, const PhosphorEngine::NavigationContext& ctx)
{
    P_SCROLL_RESOLVE(ctx.screenId);
    const QString action = QStringLiteral("rotate");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    // Captured pre-op like the sibling verbs: ctx.windowId can be empty on a
    // screen-hinted press, and the success feedback should name the window
    // focus started on.
    const QString focusedBefore = state->strip().activeWindowId();
    const int rotated = state->strip().rotateVisibleColumns(clockwise, params);
    if (rotated < 2) {
        // `rotated` is a TILE count: 0 when fewer than two visible columns
        // exist (the strip is untouched), and the tiles across the visible
        // columns otherwise. Every visible column holds at least one tile
        // (empty columns are removed on their last take), so < 2 here means
        // the no-op arm — nothing meaningfully rotated.
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
        return;
    }
    applyLayout(screen, true);
    Q_EMIT placementChanged(screen);
    // "direction:count" is the rotate OSD's wire convention (the overlay
    // splits it into the arrow and the "Rotated %n windows" copy).
    const QString reason =
        (clockwise ? QStringLiteral("clockwise:%1") : QStringLiteral("counterclockwise:%1")).arg(rotated);
    Q_EMIT navigationFeedback(true, action, reason, focusedBefore, state->strip().activeWindowId(), screen);
}

void ScrollEngine::reapplyLayout(const PhosphorEngine::NavigationContext& ctx)
{
    const QString screen = resolveOperationScreen(ctx.screenId);
    if (!screen.isEmpty()) {
        applyLayout(screen, false);
    }
    Q_EMIT navigationFeedback(!screen.isEmpty(), QStringLiteral("retile"),
                              screen.isEmpty() ? QStringLiteral("no_screen") : QString(), ctx.windowId, QString(),
                              screen);
}

void ScrollEngine::snapAllWindows(const PhosphorEngine::NavigationContext& ctx)
{
    // "Snap everything to the layout" in scrolling terms: pull every
    // floating window back into the strip. Hand-expanded resolve (not
    // P_SCROLL_RESOLVE): unlike the verb macro's users this path needs
    // layout params only on the SUCCESS branch (the priorActive re-focus
    // below), so it resolves them there rather than paying the macro's
    // unconditional resolve for every no-op press on a populated screen.
    const QString screen = resolveOperationScreen(ctx.screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    if (!state) {
        // A screen with no strip state has nothing to pull; say so rather
        // than staying silent (the snap path's empty candidate walk reports
        // the same reason).
        Q_EMIT navigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"),
                                  ctx.windowId, QString(), screen);
        return;
    }
    // Feedback contract shared with the snap path (snaphandler.cpp): nothing
    // to pull → the "snap_all"/"no_unsnapped_windows" failure OSD; a
    // successful pull needs no OSD — the windows visibly moving is the
    // feedback. Like the snap handler's candidate walk, MINIMIZED floats are
    // not candidates: the daemon models minimize as a float, so m_floating
    // holds them, and pulling one would materialise a hidden window as a
    // strip tile. The registry's isMinimized is re-pushed on every
    // minimizedChanged edge; an unknown value counts as visible so a window
    // the compositor never reported on is still pulled (the pre-filter
    // behaviour).
    QStringList candidates = state->floatingWindows();
    if (m_windowRegistry) {
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [this](const QString& id) {
                                            return m_windowRegistry->isMinimized(id);
                                        }),
                         candidates.end());
    }
    if (candidates.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"),
                                  ctx.windowId, QString(), screen);
        return;
    }
    // Capture the strip's active window BEFORE the pull:
    // unfloatWindowInternal unconditionally focuses each window it inserts,
    // and floatingWindows() is SORTED, so without a restore the focus (and
    // the view anchor with it) lands on the lexicographically-greatest
    // candidate — deterministic but arbitrary, and user-visible in the
    // resulting scroll position. If the user's window is still in the strip
    // after the pull, put the focus back.
    const QString activeBefore = state->strip().activeWindowId();
    bool any = false;
    for (const QString& windowId : candidates) {
        // Batched: one relayout + one placementChanged for the whole pull,
        // not N (each per-window call would relayout the strip again).
        any = unfloatWindowInternal(state, windowId, screen, /*applyAfter=*/false) || any;
    }
    if (any) {
        // Params resolved only on this arm — the no-op press stays free,
        // which is why this verb hand-expands the resolve.
        if (!activeBefore.isEmpty() && state->strip().containsWindow(activeBefore)) {
            state->strip().focusWindow(activeBefore, layoutParamsForScreen(screen));
        }
        applyLayout(screen, false);
        Q_EMIT placementChanged(screen);
    } else {
        // Floating windows existed but none could re-enter the strip —
        // surface it with the shared generic snap_all failure copy.
        Q_EMIT navigationFeedback(false, QStringLiteral("snap_all"), QString(), ctx.windowId, QString(), screen);
    }
}

void ScrollEngine::cycleFocus(bool forward, const PhosphorEngine::NavigationContext& ctx)
{
    P_SCROLL_RESOLVE(ctx.screenId);
    const QString action = QStringLiteral("cycle");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    const QStringList order = state->strip().windowsInOrder();
    const QString active = state->strip().activeWindowId();
    int idx = order.indexOf(active);
    // Active window absent from the order (untracked focus): forward's first
    // step from -1 already lands on entry 0; backward from -1 would land on
    // n-2, skipping the last entry — start it at 0 so it lands on n-1.
    if (idx < 0 && !forward) {
        idx = 0;
    }
    for (int step = 0; step < order.size(); ++step) {
        idx = (idx + (forward ? 1 : -1) + order.size()) % order.size();
        if (!state->strip().isWindowMinimized(order.at(idx)) && state->strip().focusWindow(order.at(idx), params)) {
            applyLayout(screen, true);
            // Focus and view anchor are persisted, and placementChanged is the
            // only producer of DirtyScrollStrips — same reason as
            // focusInDirection. This is the last focus-mutating verb; every
            // other one already emits.
            Q_EMIT placementChanged(screen);
            Q_EMIT navigationFeedback(true, action, QString(), active, order.at(idx), screen);
            return;
        }
    }
    Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), active, QString(), screen);
}

void ScrollEngine::pushToEmptyZone(const PhosphorEngine::NavigationContext& ctx)
{
    // Resolved like every other verb's feedback screen — the raw ctx id can
    // be empty and would push the OSD to the fallback output.
    Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("not_supported"), ctx.windowId, QString(),
                              resolveOperationScreen(ctx.screenId));
}

void ScrollEngine::spanFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx)
{
    // Zone spanning is a snap-mode concept with no strip analogue; report it
    // like AutotileEngine does so the shortcut is not a silent press.
    Q_UNUSED(direction)
    Q_EMIT navigationFeedback(false, QStringLiteral("span"), QStringLiteral("not_supported"), ctx.windowId, QString(),
                              resolveOperationScreen(ctx.screenId));
}

void ScrollEngine::restoreFocusedWindow(const PhosphorEngine::NavigationContext& ctx)
{
    // "Restore out of the managed state" — in scrolling terms, float it.
    // Delegating to the TOGGLE (so an already-floating window re-tiles) is
    // the shared convention with AutotileEngine's restoreFocusedWindow; do
    // not change it in one engine alone. The failure token is per-verb: a
    // "Restore" press with no window must report action "restore", not the
    // delegate's "float".
    toggleFocusedFloatAs(ctx, QStringLiteral("restore"));
}

void ScrollEngine::toggleFocusedFloat(const PhosphorEngine::NavigationContext& ctx)
{
    toggleFocusedFloatAs(ctx, QStringLiteral("float"));
}

void ScrollEngine::toggleFocusedFloatAs(const PhosphorEngine::NavigationContext& ctx, const QString& failureAction)
{
    const QString screen = resolveOperationScreen(ctx.screenId);
    QString windowId = ctx.windowId;
    if (windowId.isEmpty()) {
        if (ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false)) {
            windowId = state->strip().activeWindowId();
        }
    }
    if (windowId.isEmpty()) {
        // Interface contract (IPlacementEngine.h): a no-focused-window press
        // answers with feedback rather than silence.
        Q_EMIT navigationFeedback(false, failureAction, QStringLiteral("no_window"), QString(), QString(), screen);
        return;
    }
    // Untracked check HERE, not only in the delegate: the failure token is
    // per-verb (autotile's restore path keeps the same convention via its
    // toggleWindowFloatAs), and toggleWindowFloat's own not_managed emit can
    // only say "float" — a "Restore" press on an untracked window must
    // report action "restore". Emit the CANONICAL id, matching what the
    // delegate would have emitted.
    const QString canonical = canonicalizeForLookup(windowId);
    if (!stateForWindow(canonical)) {
        Q_EMIT navigationFeedback(false, failureAction, QStringLiteral("not_managed"), canonical, QString(), screen);
        return;
    }
    // The RESOLVED screen, not the raw ctx hint: when windowId came from the
    // resolved screen's strip, a foreign raw hint would label the resulting
    // windowFloatingChanged with a screen that does not own the window.
    toggleWindowFloat(windowId, screen);
}

// ── Scroll-specific vocabulary lives in engine_verbs.cpp ────────────────────
// The P_SCROLL_VERB one-liner family (focus/move/consume/center/width/height
// verbs) moved there wholesale, and the windowed-fullscreen trio
// (toggleWindowedFullscreen / clearWindowedFullscreen / reapplyWindowGeometry)
// followed when this file crossed the size ceiling — they are per-window
// state verbs, not directional navigation. P_SCROLL_RESOLVE comes from the
// shared scrollverbresolve_p.h and is deliberately not #undef'd (unity build).

} // namespace PhosphorScrollEngine
