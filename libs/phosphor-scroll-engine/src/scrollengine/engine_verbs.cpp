// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

namespace PhosphorScrollEngine {

// Shared preamble for every strip operation: resolve the target screen and
// its current-context state. Emits no feedback itself — callers own that.
// Duplicated from engine_navigation.cpp (both TUs #undef at end of file).
#define P_SCROLL_RESOLVE(screenIdExpr)                                                                                 \
    const QString screen = resolveOperationScreen(screenIdExpr);                                                       \
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);                 \
    const ScrollLayoutParams params = screen.isEmpty() ? ScrollLayoutParams{} : layoutParamsForScreen(screen)

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
    const QString sourceWindow = state->strip().activeWindowId();                                                      \
    const bool changed = (opExpr);                                                                                     \
    if (changed) {                                                                                                     \
        applyLayout(screen, true);                                                                                     \
        Q_EMIT placementChanged(screen);                                                                               \
    }                                                                                                                  \
    Q_EMIT navigationFeedback(changed, QStringLiteral(actionStr), changed ? QString() : QStringLiteral("no_target"),   \
                              sourceWindow, changed ? state->strip().activeWindowId() : QString(), screen)

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
// still need the three names plus the bail-out, and the macro keeps 14 of the
// 16 verb bodies one line each (toggleColumnTabbed and resetWindowHeights are
// hand-expanded — neither op reads layout params). The names are part of the
// macro's documented contract, and both macros are #undef'd at the end of this
// file.
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
    // Hand-expanded (not P_SCROLL_VERB): the op never reads layout params,
    // and the macro's resolve pays a ScreenManager query plus a
    // context-gap-provider invocation per call — same reasoning as
    // snapAllWindows.
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("tabbed"), QStringLiteral("no_windows"), QString(), QString(),
                                  screen);
        return;
    }
    const QString sourceWindow = state->strip().activeWindowId();
    const bool changed = state->strip().toggleActiveColumnTabbed();
    if (changed) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
    }
    Q_EMIT navigationFeedback(changed, QStringLiteral("tabbed"), changed ? QString() : QStringLiteral("no_target"),
                              sourceWindow, changed ? state->strip().activeWindowId() : QString(), screen);
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
    // Hand-expanded (not P_SCROLL_VERB): the op never reads layout params,
    // and the macro's resolve pays a ScreenManager query plus a
    // context-gap-provider invocation per call — same reasoning as
    // snapAllWindows.
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("resize"), QStringLiteral("no_windows"), QString(), QString(),
                                  screen);
        return;
    }
    const QString sourceWindow = state->strip().activeWindowId();
    const bool changed = state->strip().resetActiveColumnHeights();
    if (changed) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
    }
    Q_EMIT navigationFeedback(changed, QStringLiteral("resize"), changed ? QString() : QStringLiteral("no_target"),
                              sourceWindow, changed ? state->strip().activeWindowId() : QString(), screen);
}

void ScrollEngine::centerVisibleColumns(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().centerVisibleColumns(params), "center");
}

void ScrollEngine::focusWindowTop(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusTileAtEnd(false), "focus");
}

void ScrollEngine::focusWindowBottom(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusTileAtEnd(true), "focus");
}

void ScrollEngine::focusColumnPlain(int delta, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusAdjacentColumn(delta, params), "focus");
}

void ScrollEngine::focusColumnWrap(int delta, const QString& screenId)
{
    // Wrap on refusal (niri focus-column-left-or-last / right-or-first): the
    // far end is the fallback, not a second op — short-circuit keeps a
    // successful adjacent step from also wrapping.
    P_SCROLL_VERB(screenId,
                  state->strip().focusAdjacentColumn(delta, params)
                      || (delta < 0 ? state->strip().focusLastColumn(params) : state->strip().focusFirstColumn(params)),
                  "focus");
}

void ScrollEngine::setColumnWidth(const ColumnWidth& width, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().setActiveColumnWidth(width), "resize");
}

void ScrollEngine::setWindowHeight(const WindowHeight& height, const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().setActiveWindowHeight(height), "resize");
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
    setWindowFloat(windowId, true, screenId);
}

void ScrollEngine::moveFocusedToTiling(const QString& screenId)
{
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    const QString action = QStringLiteral("float");
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
    setWindowFloat(windowId, false, screenId);
}

void ScrollEngine::switchFocusBetweenFloatingAndTiling(const QString& screenId)
{
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    const QString action = QStringLiteral("focus");
    if (!state) {
        Q_EMIT navigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screen);
        return;
    }
    if (state->floatingHasFocus()) {
        // Float → tiling. The activation is this engine's own doing, so it is
        // queued for the windowFocused echo filter like applyLayout's arm
        // (same cap; the constant is TU-local there).
        const QString target = state->strip().activeWindowId();
        if (target.isEmpty()) {
            Q_EMIT navigationFeedback(false, action, QStringLiteral("no_target"), state->lastFloatingFocus(), QString(),
                                      screen);
            return;
        }
        state->setFloatingHasFocus(false);
        constexpr int kMaxPendingSelfActivations = 16; // engine_apply.cpp's cap
        m_pendingSelfActivations.append(target);
        while (m_pendingSelfActivations.size() > kMaxPendingSelfActivations) {
            m_pendingSelfActivations.removeFirst();
        }
        Q_EMIT activateWindowRequested(target);
        Q_EMIT navigationFeedback(true, action, QStringLiteral("tiling"), state->lastFloatingFocus(), target, screen);
        return;
    }
    // Tiling → float. Deliberately NOT queued as a self-activation echo: the
    // float branch of windowFocused only records bookkeeping (no strip focus
    // to rewind), and swallowing the report would leave floatingHasFocus
    // false after a switch the compositor honoured.
    QString target = state->lastFloatingFocus();
    if (target.isEmpty() || !state->isFloating(target)) {
        const QStringList floats = state->floatingWindows();
        target = floats.isEmpty() ? QString() : floats.first();
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
#undef P_SCROLL_RESOLVE

} // namespace PhosphorScrollEngine
