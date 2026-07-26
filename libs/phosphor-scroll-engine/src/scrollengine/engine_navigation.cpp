// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/ICrossSurfaceResolver.h>

#include "scrollenginelogging.h"

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

} // namespace

// Shared preamble for every strip operation: resolve the target screen and
// its current-context state. Emits no feedback itself — callers own that.
#define P_SCROLL_RESOLVE(screenIdExpr)                                                                                 \
    const QString screen = resolveOperationScreen(screenIdExpr);                                                       \
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);                 \
    const ScrollLayoutParams params = screen.isEmpty() ? ScrollLayoutParams{} : layoutParamsForScreen(screen)

void ScrollEngine::focusInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx)
{
    P_SCROLL_RESOLVE(ctx.screenId);
    const QString action = QStringLiteral("focus");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    const int h = horizontalDelta(direction);
    const int v = verticalDelta(direction);
    const bool moved =
        (h != 0) ? state->strip().focusAdjacentColumn(h, params) : (v != 0 && state->strip().focusAdjacentTile(v));
    if (moved) {
        applyLayout(screen, true);
        Q_EMIT navigationFeedback(true, action, QString(), ctx.windowId, state->strip().activeWindowId(), screen);
    } else {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
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
    const int h = horizontalDelta(direction);
    const int v = verticalDelta(direction);
    const bool moved =
        (h != 0) ? state->strip().moveActiveColumn(h, params) : (v != 0 && state->strip().moveActiveTile(v));
    if (moved) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
        Q_EMIT navigationFeedback(true, action, QString(), state->strip().activeWindowId(), QString(), screen);
        return;
    }
    // Horizontal boundary: the strip has no further column in this
    // direction — cross onto the adjacent output when one exists.
    if (h != 0 && moveActiveWindowAcrossBoundary(state, screen, direction, false)) {
        Q_EMIT navigationFeedback(true, action, QString(), ctx.windowId, QString(), screen);
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
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    const ResolvedStrip resolved = state->strip().relayout(params);
    // The entry edge faces back toward the source: a crossing moving "right"
    // enters the viewport's LEFT edge, "left" its right edge. Rank only
    // tiles actually visible at the current view (parked columns are not a
    // meaningful entry slot); vertical crossings have no strip edge, so the
    // focused window stands in.
    const int wantLeftmost = (direction == QLatin1String("right")) ? 1 : (direction == QLatin1String("left")) ? -1 : 0;
    if (wantLeftmost == 0) {
        return state->strip().activeWindowId();
    }
    QString best;
    int bestEdge = 0;
    for (const ResolvedColumn& rc : resolved.columns) {
        for (const ResolvedTile& rt : rc.tiles) {
            if (rt.hidden || !rt.rect.intersects(params.workArea)) {
                continue;
            }
            const int edge = (wantLeftmost > 0) ? rt.rect.left() : -rt.rect.right();
            if (best.isEmpty() || edge < bestEdge) {
                best = rt.windowId;
                bestEdge = edge;
            }
        }
    }
    return best.isEmpty() ? state->strip().activeWindowId() : best;
}

int ScrollEngine::columnIndexForWindow(const QString& screenId, const QString& windowId) const
{
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    return state ? state->strip().columnOfWindow(canonicalizeForLookup(windowId)) : -1;
}

bool ScrollEngine::moveActiveWindowAcrossBoundary(ScrollState* state, const QString& screenId, const QString& direction,
                                                  bool swap)
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
    if (!m_scrollingScreens.contains(target)) {
        // Different-mode neighbour: the daemon relinquishes us and hands the
        // window to the owning engine (DirectConnection — synchronous). The
        // swap variant trades with the target's entry-edge window; the
        // daemon degrades it to a move when the entry slot is empty.
        if (swap) {
            Q_EMIT crossModeSwapRequested(windowId, target, 0, direction);
        } else {
            Q_EMIT crossModeMoveRequested(windowId, target, 0, direction);
        }
        return true;
    }
    // Scroll→scroll crossing: migrate between strips ourselves. The imminent
    // output changes are daemon-owned; arm the effect's one-shot BEFORE the
    // geometry lands so the reactive close/open re-issue is skipped.
    const PhosphorEngine::PlacementStateKey sourceKey = currentKeyForScreen(screenId);
    const PhosphorEngine::PlacementStateKey targetKey = currentKeyForScreen(target);
    ScrollState* targetState = stateForKey(targetKey, true);
    if (!targetState) {
        return false;
    }
    const ScrollLayoutParams sourceParams = layoutParamsForScreen(screenId);
    const ScrollLayoutParams targetParams = layoutParamsForScreen(target);

    // Swap partner: the target's entry-edge window trades into the focused
    // window's vacated column slot. Resolved BEFORE anything moves.
    QString partner;
    int partnerLanding = -1;
    if (swap) {
        partner = entryWindowForCrossing(target, direction);
        partnerLanding = state->strip().columnOfWindow(windowId);
    }

    // Min sizes are per-strip tile state; capture before takeWindow so the
    // receiving strip keeps the clamp without a refuse/re-discover round-trip.
    const QSize windowMinSize = state->strip().windowMinimumSize(windowId);
    state->strip().takeWindow(windowId, sourceParams);
    Q_EMIT windowOutputMoveExpected(windowId, target);

    // Entering from the facing edge: moving right arrives as the target's
    // first column, moving left as its last — unless it takes the swap
    // partner's slot.
    int columnIdx = (direction == QLatin1String("right")) ? 0 : targetState->strip().columnCount();
    QSize partnerMinSize;
    if (!partner.isEmpty()) {
        columnIdx = qMax(0, targetState->strip().columnOfWindow(partner));
        partnerMinSize = targetState->strip().windowMinimumSize(partner);
        targetState->strip().takeWindow(partner, targetParams);
        Q_EMIT windowOutputMoveExpected(partner, screenId);
    }
    targetState->strip().insertWindowAt(columnIdx, windowId, effectiveDefaultColumnWidth(target),
                                        effectiveDefaultColumnDisplay(target));
    targetState->strip().setWindowMinimumSize(windowId, windowMinSize.width(), windowMinSize.height());
    targetState->strip().focusWindow(windowId, targetParams);
    m_states.setKeyForWindow(windowId, targetKey);
    if (!partner.isEmpty()) {
        state->strip().insertWindowAt(qMax(0, partnerLanding), partner, effectiveDefaultColumnWidth(screenId),
                                      effectiveDefaultColumnDisplay(screenId));
        state->strip().setWindowMinimumSize(partner, partnerMinSize.width(), partnerMinSize.height());
        m_states.setKeyForWindow(partner, sourceKey);
    }
    m_activeScreen = target;

    applyLayout(screenId, false);
    applyLayout(target, true);
    Q_EMIT placementChanged(screenId);
    Q_EMIT placementChanged(target);
    return true;
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
    const int h = horizontalDelta(direction);
    const int v = verticalDelta(direction);
    const bool moved =
        (h != 0) ? state->strip().moveActiveColumn(h, params) : (v != 0 && state->strip().moveActiveTile(v));
    if (moved) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
        Q_EMIT navigationFeedback(true, action, QString(), state->strip().activeWindowId(), QString(), screen);
        return;
    }
    if (h != 0 && moveActiveWindowAcrossBoundary(state, screen, direction, true)) {
        Q_EMIT navigationFeedback(true, action, QString(), ctx.windowId, QString(), screen);
        return;
    }
    Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), ctx.windowId, QString(), screen);
}

void ScrollEngine::moveFocusedToPosition(int position, const PhosphorEngine::NavigationContext& ctx)
{
    P_SCROLL_RESOLVE(ctx.screenId);
    const QString action = QStringLiteral("move");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), ctx.windowId, QString(), screen);
        return;
    }
    const int target = qBound(0, position - 1, state->strip().columnCount() - 1);
    const bool moved = state->strip().moveActiveColumnTo(target, params);
    if (moved) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
    }
    Q_EMIT navigationFeedback(moved, action, moved ? QString() : QStringLiteral("no_target"),
                              state->strip().activeWindowId(), QString(), screen);
}

void ScrollEngine::rotateWindows(bool clockwise, const PhosphorEngine::NavigationContext& ctx)
{
    Q_UNUSED(clockwise)
    Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("not_supported"), ctx.windowId, QString(),
                              ctx.screenId);
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
    // floating window back into the strip.
    P_SCROLL_RESOLVE(ctx.screenId);
    Q_UNUSED(params)
    if (!state) {
        return;
    }
    const QStringList floating = state->floatingWindows();
    bool any = false;
    for (const QString& windowId : floating) {
        // Batched: one relayout + one placementChanged for the whole pull,
        // not N (each per-window call would relayout the strip again).
        any = unfloatWindowInternal(state, windowId, screen, /*applyAfter=*/false) || any;
    }
    if (any) {
        applyLayout(screen, false);
        Q_EMIT placementChanged(screen);
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
    for (int step = 0; step < order.size(); ++step) {
        idx = (idx + (forward ? 1 : -1) + order.size()) % order.size();
        if (!state->strip().isWindowMinimized(order.at(idx)) && state->strip().focusWindow(order.at(idx), params)) {
            applyLayout(screen, true);
            Q_EMIT navigationFeedback(true, action, QString(), active, order.at(idx), screen);
            return;
        }
    }
    Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), active, QString(), screen);
}

void ScrollEngine::pushToEmptyZone(const PhosphorEngine::NavigationContext& ctx)
{
    Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("not_supported"), ctx.windowId, QString(),
                              ctx.screenId);
}

void ScrollEngine::restoreFocusedWindow(const PhosphorEngine::NavigationContext& ctx)
{
    // "Restore out of the managed state" — in scrolling terms, float it.
    toggleFocusedFloat(ctx);
}

void ScrollEngine::toggleFocusedFloat(const PhosphorEngine::NavigationContext& ctx)
{
    QString windowId = ctx.windowId;
    if (windowId.isEmpty()) {
        const QString screen = resolveOperationScreen(ctx.screenId);
        if (ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false)) {
            windowId = state->strip().activeWindowId();
        }
    }
    if (!windowId.isEmpty()) {
        toggleWindowFloat(windowId, ctx.screenId);
    }
}

// ── Scroll-specific vocabulary ──────────────────────────────────────────────

// Body shared by every parameterless column verb: run the strip op, then
// relayout + activate + notify when it changed something.
#define P_SCROLL_VERB(screenIdExpr, opExpr, actionStr)                                                                 \
    P_SCROLL_RESOLVE(screenIdExpr);                                                                                    \
    if (!state || state->strip().isEmpty()) {                                                                          \
        Q_EMIT navigationFeedback(false, QStringLiteral(actionStr), QStringLiteral("no_windows"), QString(),           \
                                  QString(), screen);                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
    const bool changed = (opExpr);                                                                                     \
    if (changed) {                                                                                                     \
        applyLayout(screen, true);                                                                                     \
        Q_EMIT placementChanged(screen);                                                                               \
    }                                                                                                                  \
    Q_EMIT navigationFeedback(changed, QStringLiteral(actionStr), changed ? QString() : QStringLiteral("no_target"),   \
                              QString(), changed ? state->strip().activeWindowId() : QString(), screen)

void ScrollEngine::focusColumnFirst(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusFirstColumn(params), "focus");
}

void ScrollEngine::focusColumnLast(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusLastColumn(params), "focus");
}

void ScrollEngine::moveColumnToFirst(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().moveActiveColumnToFirst(params), "move");
}

void ScrollEngine::moveColumnToLast(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().moveActiveColumnToLast(params), "move");
}

// NOTE on the P_SCROLL_* macros above: they deliberately inject `screen`,
// `state`, and `params` into the caller's scope and embed an early return.
// A helper struct + lambda was considered and rejected: every verb would
// still need the three names plus the bail-out, and the macro keeps the 20
// verb bodies one line each. The names are part of the macro's documented
// contract, and both macros are #undef'd at the end of this file.
void ScrollEngine::consumeWindowIntoColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().consumeWindowIntoColumn(params), "consume");
}

void ScrollEngine::expelWindowFromColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().expelWindowFromColumn(params), "expel");
}

void ScrollEngine::consumeOrExpelWindow(int delta, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().consumeOrExpel(delta, params), "consume");
}

void ScrollEngine::centerColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().centerActiveColumn(params), "center");
}

void ScrollEngine::toggleColumnTabbed(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().toggleActiveColumnTabbed(), "tabbed");
}

void ScrollEngine::cycleColumnPresetWidth(int delta, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().cycleActiveColumnPresetWidth(delta, params), "resize");
}

void ScrollEngine::adjustColumnWidth(qreal deltaPercent, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().adjustActiveColumnWidth(deltaPercent, params), "resize");
}

void ScrollEngine::toggleMaximizeColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().toggleMaximizeActiveColumn(params), "resize");
}

void ScrollEngine::expandColumnToAvailableWidth(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().expandActiveColumnToAvailableWidth(params), "resize");
}

void ScrollEngine::cycleWindowPresetHeight(int delta, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().cycleActiveWindowPresetHeight(delta, params), "resize");
}

void ScrollEngine::adjustWindowHeight(qreal deltaPercent, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().adjustActiveWindowHeight(deltaPercent, params), "resize");
}

void ScrollEngine::resetWindowHeights(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().resetActiveColumnHeights(), "resize");
}

#undef P_SCROLL_VERB
#undef P_SCROLL_RESOLVE

} // namespace PhosphorScrollEngine
