// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Navigation handlers for ScrollEngine — focus, move, swap, cycle, niri-style
// strip operations (consume/expel/cyclePreset/toggleFullWidth/adjustColumnWidth)
// and the unsupported-op feedback paths (rotate/snapAll/pushToEmptyZone/
// restoreFocusedWindow). Split out of ScrollEngine.cpp to keep that
// translation unit under the 800-line limit; mirrors the Column / Layout /
// Config / ScreenState split.

#include <PhosphorScrollEngine/ScrollEngine.h>

namespace PhosphorScrollEngine {

using PhosphorEngine::NavigationContext;

void ScrollEngine::focusInDirection(const QString& direction, const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    bool moved = false;
    if (state) {
        if (direction == QLatin1String("left")) {
            moved = state->focusColumn(-1);
        } else if (direction == QLatin1String("right")) {
            moved = state->focusColumn(1);
        } else if (direction == QLatin1String("up")) {
            moved = state->focusTile(-1);
        } else if (direction == QLatin1String("down")) {
            moved = state->focusTile(1);
        }
    }
    if (moved) {
        emitChanged(screenId);
    }
    reportNav(moved, QStringLiteral("focus"), screenId);
}

void ScrollEngine::moveFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    bool moved = false;
    if (state) {
        if (direction == QLatin1String("left")) {
            moved = state->moveColumn(-1);
        } else if (direction == QLatin1String("right")) {
            moved = state->moveColumn(1);
        } else if (direction == QLatin1String("up")) {
            moved = state->moveTile(-1);
        } else if (direction == QLatin1String("down")) {
            moved = state->moveTile(1);
        }
    }
    if (moved) {
        emitChanged(screenId);
    }
    reportNav(moved, QStringLiteral("move"), screenId);
}

void ScrollEngine::swapFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    // A scrollable strip has no separate "swap" — reordering a column or tile
    // is the move. Route swap onto the same reorder.
    moveFocusedInDirection(direction, ctx);
}

void ScrollEngine::moveFocusedToPosition(int position, const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    bool moved = false;
    if (state && state->activeColumnIndex() >= 0) {
        const int targetIndex = position - 1; // navigation positions are 1-based
        moved = state->moveColumn(targetIndex - state->activeColumnIndex());
    }
    if (moved) {
        emitChanged(screenId);
    }
    reportNav(moved, QStringLiteral("move"), screenId);
}

void ScrollEngine::cycleFocus(bool forward, const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    if (!state || state->columnCount() == 0) {
        reportNav(false, QStringLiteral("focus"), screenId);
        return;
    }
    const int count = state->columnCount();
    const int current = state->activeColumnIndex();
    const int next = forward ? (current + 1) % count : (current - 1 + count) % count;
    const bool moved = state->focusColumn(next - current);
    if (moved) {
        emitChanged(screenId);
    }
    reportNav(moved, QStringLiteral("focus"), screenId);
}

void ScrollEngine::reapplyLayout(const NavigationContext& ctx)
{
    // Only re-emit when the engine actually owns this screen. resolveNavTarget
    // returns nullptr for non-scroll screens or screens with no state yet — in
    // those cases there is no scroll layout to re-apply, and emitting would
    // make the daemon dispatch a layout pass on a screen we don't manage.
    QString screenId;
    if (resolveNavTarget(ctx, &screenId)) {
        emitChanged(screenId);
    }
}

void ScrollEngine::toggleFocusedFloat(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    QString windowId = state ? state->focusedWindowId() : QString();
    if (windowId.isEmpty()) {
        windowId = ctx.windowId;
    }
    // The focused-window path yields a tracked window; the ctx.windowId
    // fallback may not — guard so an untracked id still reports feedback
    // rather than silently no-op'ing inside toggleWindowFloat().
    if (windowId.isEmpty() || !isWindowTracked(windowId)) {
        reportNav(false, QStringLiteral("float"), screenId);
        return;
    }
    toggleWindowFloat(windowId, screenId);
}

void ScrollEngine::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    // Rotating the whole layout is a stack/grid concept with no scrollable
    // equivalent — the strip is navigated, not rotated. Surface a negative
    // navigation feedback so the existing OSD machinery can render
    // "not supported in scroll mode" rather than silently absorbing the chord.
    Q_UNUSED(clockwise)
    QString screenId;
    resolveNavTarget(ctx, &screenId);
    reportNav(false, QStringLiteral("rotate"), screenId);
}

void ScrollEngine::snapAllWindows(const NavigationContext& ctx)
{
    // No unmanaged-window adoption in the skeleton — windows enter the strip
    // through windowOpened. Daemon-driven adoption arrives with M3c wiring;
    // until then surface a negative feedback so the chord isn't a silent no-op.
    QString screenId;
    resolveNavTarget(ctx, &screenId);
    reportNav(false, QStringLiteral("snap_all"), screenId);
}

void ScrollEngine::pushToEmptyZone(const NavigationContext& ctx)
{
    // "Empty zone" is a manual-layout concept; the strip has no fixed slots.
    QString screenId;
    resolveNavTarget(ctx, &screenId);
    reportNav(false, QStringLiteral("push_to_empty_zone"), screenId);
}

void ScrollEngine::restoreFocusedWindow(const NavigationContext& ctx)
{
    // Restoring a window's pre-tile geometry depends on the daemon-side
    // unmanaged-geometry store; wired in a later milestone. Surface a negative
    // feedback so the user sees an OSD instead of the shortcut feeling broken.
    QString screenId;
    resolveNavTarget(ctx, &screenId);
    reportNav(false, QStringLiteral("restore"), screenId);
}

// ─────────────────────────────────────────────────────────────────────────
// niri scrollable-tiling operations
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::consumeWindowIntoColumn(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    const bool ok = state && state->consumeIntoColumn();
    if (ok) {
        emitChanged(screenId);
    }
    reportNav(ok, QStringLiteral("consume"), screenId);
}

void ScrollEngine::expelWindowFromColumn(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    const bool ok = state && state->expelFromColumn();
    if (ok) {
        emitChanged(screenId);
    }
    reportNav(ok, QStringLiteral("expel"), screenId);
}

void ScrollEngine::cyclePresetColumnWidth(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    const QVector<qreal> presets = effectivePresetColumnWidths(screenId);
    if (!state || !state->activeColumn() || presets.isEmpty()) {
        reportNav(false, QStringLiteral("width"), screenId);
        return;
    }
    const int presetCount = static_cast<int>(presets.size());
    // A stale index (the preset list shrank since it was set) or the detached
    // -1 both normalise to -1, so the next press restarts the cycle at the
    // first preset rather than wrapping from an out-of-range value.
    const int rawIndex = state->activeColumn()->presetWidthIndex();
    const int current = (rawIndex >= 0 && rawIndex < presetCount) ? rawIndex : -1;
    const int next = (current + 1) % presetCount;
    // Skip emit when the cycle would no-op: same preset index AND the column's
    // current width already matches the preset value. A user with a single
    // preset matching their current width should not see flicker / a redundant
    // daemon resolve. Only the index-equal-and-value-equal case is suppressed
    // — a same-index but value-mismatched case (column edited via grow/shrink
    // and detached to -1, then cycle bringing it back to a different value)
    // still proceeds.
    const ColumnWidth currentWidth = state->activeColumn()->width();
    const bool isProportionAtPreset = currentWidth.kind == ColumnWidth::Kind::Proportion
        && qFuzzyCompare(currentWidth.value + 1.0, presets.at(next) + 1.0);
    if (next == current && isProportionAtPreset) {
        reportNav(false, QStringLiteral("width"), screenId);
        return;
    }
    state->setActiveColumnWidth(ColumnWidth::proportion(presets.at(next)), next);
    emitChanged(screenId);
    reportNav(true, QStringLiteral("width"), screenId);
}

void ScrollEngine::cyclePresetWindowHeight(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    const Column* column = state ? state->activeColumn() : nullptr;
    const Tile* tile = column ? column->activeTile() : nullptr;
    const QVector<qreal> presets = effectivePresetWindowHeights(screenId);
    if (!tile || presets.isEmpty()) {
        reportNav(false, QStringLiteral("height"), screenId);
        return;
    }
    const int presetCount = static_cast<int>(presets.size());
    // A stale preset index (the preset list shrank since it was set) or a
    // non-preset height both normalise to -1, so the next press restarts the
    // cycle at the first preset rather than wrapping from an out-of-range value.
    const int rawIndex = (tile->height.kind == WindowHeight::Kind::Preset) ? tile->height.presetIndex : -1;
    const int current = (rawIndex >= 0 && rawIndex < presetCount) ? rawIndex : -1;
    const int next = (current + 1) % presetCount;
    // Same no-op suppression as cyclePresetColumnWidth: skip the emit when the
    // cycle lands on the same preset index AND the tile is already in Preset
    // mode at that index. Auto/Fixed → Preset transitions still emit, since
    // that's a real kind change even at the "same" index 0.
    if (next == current && tile->height.kind == WindowHeight::Kind::Preset) {
        reportNav(false, QStringLiteral("height"), screenId);
        return;
    }
    state->setActiveTileHeight(WindowHeight::preset(next));
    emitChanged(screenId);
    reportNav(true, QStringLiteral("height"), screenId);
}

void ScrollEngine::toggleColumnFullWidth(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    if (!state || !state->activeColumn()) {
        reportNav(false, QStringLiteral("fullwidth"), screenId);
        return;
    }
    state->toggleActiveColumnFullWidth();
    emitChanged(screenId);
    reportNav(true, QStringLiteral("fullwidth"), screenId);
}

void ScrollEngine::adjustColumnWidth(qreal deltaFraction, const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    const Column* column = state ? state->activeColumn() : nullptr;
    if (!column || column->width().kind != ColumnWidth::Kind::Proportion) {
        // No focused column, or a fixed-pixel width — the geometry-agnostic
        // engine cannot resolve a pixel width to a working-area fraction.
        reportNav(false, QStringLiteral("width"), screenId);
        return;
    }
    // Clamp the starting width into range before applying the delta: a
    // Proportion width is normally already within [kMinSizeFraction,
    // kMaxSizeFraction], but clamping here keeps grow/shrink monotonic —
    // without it a shrink keypress on an out-of-range narrow column would
    // grow it. kMinSizeFraction is also the smallest width a column can
    // settle on, so it can never shrink away to nothing.
    const qreal current = qBound(kMinSizeFraction, column->width().value, kMaxSizeFraction);
    const qreal target = qBound(kMinSizeFraction, current + deltaFraction, kMaxSizeFraction);
    if (qFuzzyCompare(target, current)) {
        reportNav(false, QStringLiteral("width"), screenId); // already at the limit
        return;
    }
    // The width is no longer one of the presets; -1 detaches it from the cycle.
    state->setActiveColumnWidth(ColumnWidth::proportion(target), /*presetIndex=*/-1);
    emitChanged(screenId);
    reportNav(true, QStringLiteral("width"), screenId);
}

} // namespace PhosphorScrollEngine
