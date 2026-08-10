// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/WindowRegistry.h>

#include "scrollverbresolve_p.h"

namespace PhosphorScrollEngine {

// ── Scroll-specific vocabulary ──────────────────────────────────────────────

// Body shared by every parameterless column verb: run the strip op, then
// relayout + notify when it changed something.
//
// `focusAfterExpr` decides whether the relayout re-asserts compositor focus
// on the strip's active window. Focus and move verbs pass true — activation
// is their point. Sizing, centering and display verbs pass false: a geometry
// verb has no business yanking focus off a floating window, and applyLayout's
// focus arm also clears floatingHasFocus, so a scripted width write over
// D-Bus would silently flip the float layer's focus memory.
//
// `successReasonExpr` rides the feedback's reason slot on success.
// Direction-bearing verbs pass "left"/"right"/"up"/"down" so the OSD's arrow
// points the way the verb went — its directionArrow default is a RIGHT arrow,
// so an empty reason on a leftward verb renders the wrong glyph.
#define P_SCROLL_VERB(screenIdExpr, opExpr, actionStr, focusAfterExpr, successReasonExpr)                              \
    P_SCROLL_RESOLVE(screenIdExpr);                                                                                    \
    if (!state || state->strip().isEmpty()) {                                                                          \
        Q_EMIT navigationFeedback(false, QStringLiteral(actionStr), QStringLiteral("no_windows"), QString(),           \
                                  QString(), screen);                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
    const QString sourceWindow = state->strip().activeWindowId();                                                      \
    const bool changed = (opExpr);                                                                                     \
    if (changed) {                                                                                                     \
        applyLayout(screen, (focusAfterExpr));                                                                         \
        Q_EMIT placementChanged(screen);                                                                               \
    }                                                                                                                  \
    Q_EMIT navigationFeedback(changed, QStringLiteral(actionStr),                                                      \
                              changed ? (successReasonExpr) : QStringLiteral("no_target"), sourceWindow,               \
                              changed ? state->strip().activeWindowId() : QString(), screen)

void ScrollEngine::focusColumnFirst(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusFirstColumn(params), "focus", true, QStringLiteral("left"));
}

void ScrollEngine::focusColumnLast(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusLastColumn(params), "focus", true, QStringLiteral("right"));
}

void ScrollEngine::moveColumnToFirst(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().moveActiveColumnToFirst(params), "move", true, QStringLiteral("left"));
}

void ScrollEngine::moveColumnToLast(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().moveActiveColumnToLast(params), "move", true, QStringLiteral("right"));
}

// NOTE on the P_SCROLL_* macros above: they deliberately inject `screen`,
// `state`, and `params` into the caller's scope and embed an early return.
// A helper struct + lambda was considered and rejected: every verb would
// still need the three names plus the bail-out, and the macro keeps almost
// every verb body in this file a one-liner. The names are part of the macro's
// documented contract, and both macros are #undef'd at the end of this file.
// Only the float-layer verbs at the tail are hand-expanded — their feedback
// branches differ per arm, not just per outcome.
void ScrollEngine::consumeWindowIntoColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().consumeWindowIntoColumn(params), "consume", true, QString());
}

void ScrollEngine::expelWindowFromColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().expelWindowFromColumn(params), "expel", true, QString());
}

void ScrollEngine::consumeOrExpelWindow(int delta, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().consumeOrExpel(delta, params), "consume", true, QString());
}

void ScrollEngine::centerColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().centerActiveColumn(params), "center", false, QString());
}

void ScrollEngine::toggleColumnTabbed(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().toggleActiveColumnTabbed(), "tabbed", false, QString());
}

void ScrollEngine::cycleColumnPresetWidth(int delta, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().cycleActiveColumnPresetWidth(delta, params), "resize", false, QString());
}

void ScrollEngine::adjustColumnWidth(qreal deltaPercent, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().adjustActiveColumnWidth(deltaPercent, params), "resize", false, QString());
}

void ScrollEngine::toggleMaximizeColumn(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().toggleMaximizeActiveColumn(params), "resize", false, QString());
}

void ScrollEngine::expandColumnToAvailableWidth(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().expandActiveColumnToAvailableWidth(params), "resize", false, QString());
}

void ScrollEngine::cycleWindowPresetHeight(int delta, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().cycleActiveWindowPresetHeight(delta, params), "resize", false, QString());
}

void ScrollEngine::adjustWindowHeight(qreal deltaPercent, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().adjustActiveWindowHeight(deltaPercent, params), "resize", false, QString());
}

void ScrollEngine::resetWindowHeights(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().resetActiveColumnHeights(), "resize", false, QString());
}

void ScrollEngine::centerVisibleColumns(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().centerVisibleColumns(params), "center", false, QString());
}

void ScrollEngine::focusWindowTop(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusTileAtEnd(false), "focus", true, QStringLiteral("up"));
}

void ScrollEngine::focusWindowBottom(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusTileAtEnd(true), "focus", true, QStringLiteral("down"));
}

void ScrollEngine::focusColumnPlain(int delta, const QString& screenId)
{
    // Header contract: delta is -1 or +1. Reject anything else outright —
    // a zero must not read as a press.
    if (delta != -1 && delta != 1) {
        return;
    }
    P_SCROLL_VERB(screenId, state->strip().focusAdjacentColumn(delta, params), "focus", true,
                  delta < 0 ? QStringLiteral("left") : QStringLiteral("right"));
}

void ScrollEngine::focusColumnWrap(int delta, const QString& screenId)
{
    // Same delta contract as focusColumnPlain — an invalid delta must not
    // short-circuit into the wrap fallback and teleport focus to an end.
    if (delta != -1 && delta != 1) {
        return;
    }
    // Wrap on refusal (niri focus-column-left-or-last / right-or-first): the
    // far end is the fallback, not a second op — short-circuit keeps a
    // successful adjacent step from also wrapping.
    P_SCROLL_VERB(screenId,
                  state->strip().focusAdjacentColumn(delta, params)
                      || (delta < 0 ? state->strip().focusLastColumn(params) : state->strip().focusFirstColumn(params)),
                  "focus", true, delta < 0 ? QStringLiteral("left") : QStringLiteral("right"));
}

void ScrollEngine::setColumnWidth(const ColumnWidth& width, const QString& screenId)
{
    // Exported library boundary: the strip stores intent verbatim by contract
    // ("callers own validation"), and an unclamped Proportion reaches
    // qRound(p * workExtent) at relayout — UB for an out-of-range product and
    // a 1px column for a non-positive one. ONLY the Proportion kind needs
    // this: Fixed goes through relayout's own qBound(1, px, workW) and a
    // Preset anchor is snapped to the vocabulary by nearestPresetValue, so
    // both are safe for any value (the height twin below is safe on all
    // three kinds for the same reasons). The in-tree D-Bus caller clamps
    // already; this keeps any embedder inside the same envelope the
    // library's other entry points (serialize, open rules) enforce.
    ColumnWidth clamped = width;
    if (clamped.kind == ColumnWidth::Proportion) {
        clamped.proportion = qBound<qreal>(MinColumnWidthFraction, clamped.proportion, 1.0);
    }
    P_SCROLL_VERB(screenId, state->strip().setActiveColumnWidth(clamped), "resize", false, QString());
}

void ScrollEngine::setWindowHeight(const WindowHeight& height, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().setActiveWindowHeight(height), "resize", false, QString());
}

void ScrollEngine::moveFocusedToFloating(const QString& screenId)
{
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    const QString action = QStringLiteral("float");
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screen);
        return;
    }
    // The float layer holds focus: the focused window is already floating,
    // and the explicit verb must not answer by floating the strip's stale
    // active tile instead. no_target keeps the press audible.
    if (state->floatingHasFocus()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), state->lastFloatingFocus(), QString(),
                                  screen);
        return;
    }
    const QString windowId = state->strip().activeWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_window"), QString(), QString(), screen);
        return;
    }
    // The RESOLVED screen, not the caller's raw hint: state and windowId came
    // from `screen`, and floatWindowInternal's announcement uses the id it is
    // handed — a foreign raw hint would label the change with a screen that
    // does not own the window.
    setWindowFloat(windowId, true, screen);
}

void ScrollEngine::moveFocusedToTiling(const QString& screenId)
{
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    // "restore" — the OSD's restore arm renders "Nothing to restore" on the
    // failure paths, which is the accurate copy for "move this window back
    // into tiling". The delegate's own "float" token would render "Floating
    // is unavailable", the opposite of what was asked.
    const QString action = QStringLiteral("restore");
    if (!state) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screen);
        return;
    }
    // Only a focused FLOAT can be sent to tiling; a tile answering this verb
    // is already there.
    if (!state->floatingHasFocus()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), state->strip().activeWindowId(),
                                  QString(), screen);
        return;
    }
    const QString windowId = state->lastFloatingFocus();
    if (windowId.isEmpty() || !state->isFloating(windowId)) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_window"), QString(), QString(), screen);
        return;
    }
    // A minimized float must not materialise as a visible strip tile — the
    // same hazard snapAllWindows filters its candidates for. The daemon
    // models minimize as a float, so a stale focus memory can name one.
    if (m_windowRegistry && m_windowRegistry->isMinimized(windowId)) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), windowId, QString(), screen);
        return;
    }
    // Resolved screen for the same reason as moveFocusedToFloating.
    setWindowFloat(windowId, false, screen);
}

void ScrollEngine::switchFocusBetweenFloatingAndTiling(const QString& screenId)
{
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    // Action "float": the OSD's float success arm renders layer copy
    // ("Tiled" / "Floating") from the reason token. Emitting under "focus"
    // would run the directional success arm, whose arrow default points
    // right for a verb that has no direction.
    const QString action = QStringLiteral("float");
    if (!state) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screen);
        return;
    }
    if (state->floatingHasFocus()) {
        // Float → tiling. The activation is this engine's own doing, so it is
        // queued for the windowFocused echo filter like applyLayout's arm.
        // The flag write and the feedback are optimistic — a compositor that
        // drops the activation leaves the flag false until the next genuine
        // focus report heals it. Accepted: the activation round trip is
        // asynchronous and there is no ack to gate on at this seam.
        const QString target = state->strip().activeWindowId();
        if (target.isEmpty()) {
            Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), state->lastFloatingFocus(), QString(),
                                      screen);
            return;
        }
        state->setFloatingHasFocus(false);
        m_pendingSelfActivations.append(target);
        while (m_pendingSelfActivations.size() > kMaxPendingSelfActivations) {
            m_pendingSelfActivations.removeFirst();
        }
        Q_EMIT activateWindowRequested(target);
        Q_EMIT navigationFeedback(true, action, QStringLiteral("tiled"), state->lastFloatingFocus(), target, screen);
        return;
    }
    // Tiling → float. Deliberately NOT queued as a self-activation echo: the
    // float branch of windowFocused only records bookkeeping (no strip focus
    // to rewind), and swallowing the report would leave floatingHasFocus
    // false after a switch the compositor honoured.
    //
    // Minimized floats are not focus candidates — the daemon models minimize
    // as a float, so both the remembered focus and the fallback scan must
    // skip hidden windows rather than "activating" one and reporting success.
    const auto isHidden = [this](const QString& id) {
        return m_windowRegistry && m_windowRegistry->isMinimized(id);
    };
    QString target = state->lastFloatingFocus();
    if (target.isEmpty() || !state->isFloating(target) || isHidden(target)) {
        // Fallback order is the sorted floating set — ARBITRARY (lowest id
        // first), carrying no recency or position meaning; the strip has no
        // frontmost-float notion to prefer.
        target.clear();
        const QStringList floats = state->floatingWindows();
        for (const QString& id : floats) {
            if (!isHidden(id)) {
                target = id;
                break;
            }
        }
    }
    if (target.isEmpty()) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), state->strip().activeWindowId(),
                                  QString(), screen);
        return;
    }
    Q_EMIT activateWindowRequested(target);
    Q_EMIT navigationFeedback(true, action, QStringLiteral("floating"), state->strip().activeWindowId(), target,
                              screen);
}

#undef P_SCROLL_VERB
// P_SCROLL_RESOLVE deliberately NOT #undef'd — it comes from
// scrollverbresolve_p.h, whose include guard would make an #undef here erase
// it for the next file in a unity chunk.

} // namespace PhosphorScrollEngine
