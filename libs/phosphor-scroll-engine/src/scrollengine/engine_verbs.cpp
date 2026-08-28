// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/LayerFocusSwitch.h>
#include <PhosphorEngine/WindowRegistry.h>

#include "scrollverbresolve_p.h"

#include <cmath>

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
    // The reason token feeds the OSD's ARROW, so it has to name the direction
    // focus actually travelled — which on a vertical strip is up, not left.
    // These verbs are end-relative and their BEHAVIOUR needs no axis; only the
    // glyph does. params is in scope via P_SCROLL_RESOLVE inside the macro.
    P_SCROLL_VERB(screenId, state->strip().focusFirstColumn(params), "focus", true,
                  Detail::physicalTokenForMain(-1, params.axis));
}

void ScrollEngine::focusColumnLast(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusLastColumn(params), "focus", true,
                  Detail::physicalTokenForMain(1, params.axis));
}

void ScrollEngine::moveColumnToFirst(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().moveActiveColumnToFirst(params), "move", true,
                  Detail::physicalTokenForMain(-1, params.axis));
}

void ScrollEngine::moveColumnToLast(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().moveActiveColumnToLast(params), "move", true,
                  Detail::physicalTokenForMain(1, params.axis));
}

// NOTE on the P_SCROLL_* macros above: they deliberately inject `screen`,
// `state`, and `params` into the caller's scope and embed an early return.
// A helper struct + lambda was considered and rejected: every verb would
// still need the three names plus the bail-out, and the macro keeps almost
// every verb body in this file a one-liner. The names are part of the macro's
// documented contract. P_SCROLL_VERB is #undef'd at the end of this file; the
// tail note explains why P_SCROLL_RESOLVE deliberately is not.
// The hand-expanded verbs are the ones the macro cannot express:
// consumeOrExpelWindow (two ops, one feedback), resetStripToDefaults (the
// override map resolved once for three defaults), scrollViewByPercent (params
// needed before the op), the windowed-fullscreen trio (background-context
// guards) and the float-layer verbs at the tail (feedback branches differ per
// arm, not just per outcome).
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
    // Hand-expanded from P_SCROLL_VERB for the action token alone: the op
    // EXPELS when the active column holds more than one tile and consumes
    // otherwise (ScrollStrip::consumeOrExpel), and the OSD has distinct
    // success copy for the two — "Window expelled into its own column" vs
    // "Window moved between columns" — so the token is decided from the
    // pre-op column, the only place the answer exists.
    P_SCROLL_RESOLVE(screenId);
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("consume"), QStringLiteral("no_windows"), QString(), QString(),
                                  screen);
        return;
    }
    const int activeCol = state->strip().activeColumnIndex();
    const bool willExpel = activeCol >= 0 && state->strip().columns().at(activeCol).tiles.size() > 1;
    const QString action = willExpel ? QStringLiteral("expel") : QStringLiteral("consume");
    const QString sourceWindow = state->strip().activeWindowId();
    const bool changed = state->strip().consumeOrExpel(delta, params);
    if (changed) {
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
    }
    Q_EMIT navigationFeedback(changed, action, changed ? QString() : QStringLiteral("no_target"), sourceWindow,
                              changed ? state->strip().activeWindowId() : QString(), screen);
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

void ScrollEngine::toggleMaximizeColumn(const QString& screenId, const QString& windowId)
{
    // An empty windowId means "the active column", which is what the keyboard
    // shortcut wants: it acts on whatever the user is looking at. A NAMED
    // window aims at the column owning it, which is what the compositor's
    // maximize interception needs — that request arrives for one specific
    // window (titlebar click, a client's own request from a window that never
    // took focus) and the active column is often a different one.
    P_SCROLL_VERB(screenId,
                  windowId.isEmpty() ? state->strip().toggleMaximizeActiveColumn(params)
                                     : state->strip().toggleMaximizeColumnForWindow(windowId, params),
                  "resize", false, QString());
}

void ScrollEngine::expandColumnToAvailableWidth(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().expandActiveColumnToAvailableWidth(params), "resize", false, QString());
}

void ScrollEngine::equalizeVisibleColumnWidths(const QString& screenId)
{
    // "equalize" in the reason slot: the OSD's generic resize copy names one
    // column, and this verb rewrote a group. The failure token stays the
    // shared no_target.
    P_SCROLL_VERB(screenId, state->strip().equalizeVisibleColumnWidths(params), "resize", false,
                  QStringLiteral("equalize"));
}

void ScrollEngine::minimizeColumnWidth(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().minimizeActiveColumnWidth(params), "resize", false, QString());
}

void ScrollEngine::resetStripToDefaults(const QString& screenId)
{
    // "retile" rather than "resize": this is the scrolling arm of the
    // mode-neutral Retile shortcut, and the OSD's retile copy is what the
    // user pressed for. All three defaults are resolved here because the
    // strip does not carry the display in params, and either extent may be
    // "the client decides", which has no value to hand down (see
    // resetDefaultColumnWidthFor, resetDefaultWindowHeightFor and
    // resetToDefaults). Hand-expanded so the
    // per-context override map is resolved ONCE for all of them, the
    // file's resolve-once discipline; P_SCROLL_VERB's op expression would
    // have each default re-fetch it.
    P_SCROLL_RESOLVE(screenId);
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("retile"), QStringLiteral("no_windows"), QString(), QString(),
                                  screen);
        return;
    }
    const QString sourceWindow = state->strip().activeWindowId();
    const QVariantMap overrides = overridesForScreen(screen);
    const bool changed = state->strip().resetToDefaults(resetDefaultColumnWidthFor(overrides, params),
                                                        resetDefaultWindowHeightFor(overrides),
                                                        effectiveDefaultColumnDisplay(overrides), params);
    if (changed) {
        applyLayout(screen, false);
        Q_EMIT placementChanged(screen);
    }
    Q_EMIT navigationFeedback(changed, QStringLiteral("retile"), changed ? QString() : QStringLiteral("no_target"),
                              sourceWindow, changed ? state->strip().activeWindowId() : QString(), screen);
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
    // "span" distinguishes the whole-group centering from centerColumn's
    // single-column copy in the OSD (both ride the "center" action).
    P_SCROLL_VERB(screenId, state->strip().centerVisibleColumns(params), "center", false, QStringLiteral("span"));
}

void ScrollEngine::scrollViewByPercent(qreal percent, const QString& screenId)
{
    // Resolved by hand rather than through P_SCROLL_VERB: the pixel delta
    // needs params BEFORE the op expression runs, and the macro only brings
    // params into scope for the expression itself.
    P_SCROLL_RESOLVE(screenId);
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("scroll"), QStringLiteral("no_windows"), QString(), QString(),
                                  screen);
        return;
    }
    // Against the MAIN extent, so a percent means the same thing on either
    // axis and "100" is exactly one viewport, which is what the page variant
    // asks for. Rounded, then refused if it collapses to nothing, under its
    // OWN token: a 1% step on a tiny work area is not "the end of the
    // strip", and the OSD names the two refusals differently. A non-finite
    // percent reads as "nothing to move" rather than reaching qRound, the
    // same guard the drag auto-scroll tick applies to its public qreal: no
    // in-tree caller can pass one, but this is exported library API.
    const int deltaPx = std::isfinite(percent) ? qRound(percent / 100.0 * params.axis.mainSize(params.workArea)) : 0;
    const QString sourceWindow = state->strip().activeWindowId();
    const bool changed = deltaPx != 0 && state->strip().scrollViewBy(deltaPx, params);
    const QString refusal = deltaPx == 0 ? QStringLiteral("no_movement") : QStringLiteral("no_target");
    if (changed) {
        // focusAfter=false for the same reason the sizing verbs pass it: a pan
        // moves geometry and nothing else, and must not yank focus off a
        // floating window.
        applyLayout(screen, false);
        Q_EMIT placementChanged(screen);
    }
    // The active window is unchanged either way — that is the verb's whole
    // point — so the target slot carries it back unchanged on success too.
    Q_EMIT navigationFeedback(changed, QStringLiteral("scroll"),
                              changed ? Detail::physicalTokenForMain(deltaPx > 0 ? 1 : -1, params.axis) : refusal,
                              sourceWindow, changed ? sourceWindow : QString(), screen);
}

void ScrollEngine::focusWindowTop(const QString& screenId)
{
    // CROSS-axis ends: the stack runs top-to-bottom on a horizontal strip and
    // left-to-right on a vertical one, so these two swap words with the axis
    // just as the main-axis pair above does.
    P_SCROLL_VERB(screenId, state->strip().focusTileAtEnd(false), "focus", true,
                  Detail::physicalTokenForCross(-1, params.axis));
}

void ScrollEngine::focusWindowBottom(const QString& screenId)
{
    P_SCROLL_VERB(screenId, state->strip().focusTileAtEnd(true), "focus", true,
                  Detail::physicalTokenForCross(1, params.axis));
}

void ScrollEngine::focusColumnPlain(int delta, const QString& screenId)
{
    // Header contract: delta is -1 or +1. Reject anything else outright —
    // a zero must not read as a press. Silent by design: the D-Bus adaptor
    // refuses out-of-contract deltas the same way (its documented
    // silent-no-op policy), so no shipped caller reaches this, and feedback
    // here would give an out-of-contract call an OSD.
    if (delta != -1 && delta != 1) {
        return;
    }
    P_SCROLL_VERB(screenId, state->strip().focusAdjacentColumn(delta, params), "focus", true,
                  Detail::physicalTokenForMain(delta, params.axis));
}

void ScrollEngine::focusColumnWrap(int delta, const QString& screenId)
{
    // Same delta contract (and the same deliberate silence) as
    // focusColumnPlain — an invalid delta must not short-circuit into the
    // wrap fallback and teleport focus to an end.
    if (delta != -1 && delta != 1) {
        return;
    }
    // Wrap on refusal (niri focus-column-left-or-last / right-or-first): the
    // far end is the fallback, not a second op — short-circuit keeps a
    // successful adjacent step from also wrapping.
    P_SCROLL_VERB(screenId,
                  state->strip().focusAdjacentColumn(delta, params)
                      || (delta < 0 ? state->strip().focusLastColumn(params) : state->strip().focusFirstColumn(params)),
                  "focus", true, Detail::physicalTokenForMain(delta, params.axis));
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
    // does not own the window. A SUCCESSFUL press is deliberately silent (no
    // navigationFeedback): the verb delegates to setWindowFloat and the
    // window visibly moving IS the feedback, the rule snapAllWindows also
    // follows — only refusals speak. Pinned by
    // moveToFloatingAndBackAnswersEveryPress.
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
    // Resolved screen for the same reason as moveFocusedToFloating, and the
    // same silent-success convention.
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
    // Leg/target/refusal shape comes from the shared resolver so all three
    // engines answer this verb identically; what stays scroll-specific is
    // the activation policy below.
    //
    // The tiled side is the strip's active window with no fallback scan:
    // the strip IS the tiled side. The daemon models minimize as a float,
    // so a strip tile should never be hidden — but the eligibility filter
    // guards it anyway (same cheap check both siblings apply to their tiled
    // side), so a seeding path that violates the premise degrades to a
    // no_target refusal instead of "activating" a hidden window. That
    // refusal is reachable during the minimize→float conversion's debounce
    // window plus the D-Bus round trip, where the strip briefly holds a
    // minimized tile: the press refuses there instead of activate-restoring
    // the tile. The float side filters minimized windows on both the
    // remembered focus and the fallback pool. Fallback order is the sorted
    // floating set —
    // ARBITRARY (lowest id first), carrying no recency or position meaning;
    // the strip has no frontmost-float notion to prefer.
    PhosphorEngine::LayerSwitchSide tiledSide;
    tiledSide.candidate = state->strip().activeWindowId();
    tiledSide.focusForFeedback = tiledSide.candidate;
    tiledSide.isEligible = [this](const QString& id) {
        return !(m_windowRegistry && m_windowRegistry->isMinimized(id));
    };
    PhosphorEngine::LayerSwitchSide floatingSide;
    floatingSide.candidate = state->lastFloatingFocus();
    floatingSide.fallbacks = state->floatingWindows();
    floatingSide.isEligible = [this, state](const QString& id) {
        return state->isFloating(id) && !(m_windowRegistry && m_windowRegistry->isMinimized(id));
    };
    floatingSide.focusForFeedback = state->lastFloatingFocus();

    const auto result = PhosphorEngine::resolveLayerFocusSwitch(state->floatingHasFocus(), tiledSide, floatingSide);
    if (result.success && result.toTiled) {
        // Float → tiling. The activation is this engine's own doing, so it is
        // queued for the windowFocused echo filter like applyLayout's arm.
        // The flag write and the feedback are optimistic — a compositor that
        // drops the activation leaves the flag false until the next genuine
        // focus report heals it. Accepted: the activation round trip is
        // asynchronous and there is no ack to gate on at this seam.
        //
        // The tiling → float leg is deliberately NOT queued as a
        // self-activation echo: the float branch of windowFocused only
        // records bookkeeping (no strip focus to rewind), and swallowing the
        // report would leave floatingHasFocus false after a switch the
        // compositor honoured.
        state->setFloatingHasFocus(false);
        queueSelfActivation(result.target);
    }
    announceLayerSwitch(result, action, screen);
}

#undef P_SCROLL_VERB
// P_SCROLL_RESOLVE deliberately NOT #undef'd — it comes from
// scrollverbresolve_p.h, whose include guard would make an #undef here erase
// it for the next file in a unity chunk.

// ── The windowed-fullscreen trio ─────────────────────────────────────────────
// Per-window state verbs, moved from engine_navigation.cpp when that file
// crossed the size ceiling. None uses P_SCROLL_VERB (each hand-expands its
// resolve for its own feedback/guard shape), so they sit after the #undef.

void ScrollEngine::toggleWindowedFullscreen(const QString& screenId)
{
    // Hand-expanded (not P_SCROLL_VERB) for toggleColumnTabbed's reason —
    // the op is layout-neutral and never reads layout params — but the two
    // diverge downstream: this verb's feedback carries the resulting state
    // as the reason token, and the OSD has dedicated arms for it.
    const QString screen = resolveOperationScreen(screenId);
    ScrollState* state = screen.isEmpty() ? nullptr : stateForKey(currentKeyForScreen(screen), false);
    if (!state || state->strip().isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("fullscreen"), QStringLiteral("no_windows"), QString(),
                                  QString(), screen);
        return;
    }
    const QString sourceWindow = state->strip().activeWindowId();
    const bool changed = state->strip().toggleActiveWindowedFullscreen();
    if (changed) {
        // The flag never moves a rect; applyLayout's emit-on-change gate has
        // its own windowed-fullscreen leg (m_lastAppliedWindowedFs) so the
        // flip reaches the compositor on an otherwise motionless strip.
        applyLayout(screen, true);
        Q_EMIT placementChanged(screen);
    }
    // The success reason carries the RESULTING state, read back from the
    // strip, so the OSD can say which way the toggle went (the float verb's
    // state-token convention; an empty reason could only render a generic
    // "toggled").
    const QString resultingState = !changed                 ? QString()
        : state->strip().isWindowedFullscreen(sourceWindow) ? QStringLiteral("on")
                                                            : QStringLiteral("off");
    Q_EMIT navigationFeedback(changed, QStringLiteral("fullscreen"),
                              changed ? resultingState : QStringLiteral("no_target"), sourceWindow,
                              changed ? state->strip().activeWindowId() : QString(), screen);
}

void ScrollEngine::clearWindowedFullscreen(const QString& windowId)
{
    const QString id = canonicalizeForLookup(windowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(id, &key);
    if (!state || !state->strip().setWindowedFullscreen(id, false)) {
        return;
    }
    // Background-context guard, the same discipline every sibling
    // window-keyed mutation holds (unfloatWindowInternal is the shape): the
    // flag was cleared on the window's OWN context state, but applyLayout
    // resolves the screen's CURRENT context — on a background desktop that
    // is a different strip, and relayouting it would emit a batch for
    // windows this clear never touched. The cleared flag still reaches the
    // compositor on the context's next activation via the emit-gate leg.
    // (The emptiness guard is belt only: every resolved state's key carries
    // a real screen id today, so the model write above cannot in practice
    // land without its placementChanged.)
    if (!key.screenId.isEmpty()) {
        if (key == currentKeyForScreen(key.screenId)) {
            applyLayout(key.screenId, false);
        }
        Q_EMIT placementChanged(key.screenId);
    }
}

void ScrollEngine::reapplyWindowGeometry(const QString& windowId)
{
    const QString id = canonicalizeForLookup(windowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(id, &key);
    if (!state) {
        return;
    }
    // The emit-on-change gate compares against the last EMITTED rect, which
    // stops being the truth the moment the compositor moves the window
    // behind the engine's back. KWin's fullscreen-exit restore is the known
    // producer: it re-applies the window's pre-fullscreen rect one client
    // round-trip AFTER the batch that already placed the tile, and since
    // the strip's own rects never moved, the gate keeps every later batch
    // silent and the stray rect stands forever (seen live as a stranded
    // full-area frame, and as a toggle-off restoring a window to its old
    // PARK spot off-screen). Evicting the memory makes the next relayout
    // treat the rect as new and re-emit. Background-context guard as in
    // clearWindowedFullscreen: a background strip re-emits on activation.
    m_lastAppliedRect.remove(id);
    if (!key.screenId.isEmpty() && key == currentKeyForScreen(key.screenId)) {
        applyLayout(key.screenId, false);
    }
}

} // namespace PhosphorScrollEngine
