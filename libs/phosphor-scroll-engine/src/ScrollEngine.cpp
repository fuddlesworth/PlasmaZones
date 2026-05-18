// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QJsonArray>

#include <utility>

namespace PhosphorScrollEngine {

using PhosphorEngine::IPlacementState;
using PhosphorEngine::NavigationContext;
using PhosphorEngine::TilingStateKey;

ScrollEngine::ScrollEngine(QObject* parent)
    : PhosphorEngine::PlacementEngineBase(parent)
{
    // niri's default preset fractions: one third, one half, two thirds.
    m_presetColumnWidths = {1.0 / 3.0, 0.5, 2.0 / 3.0};
    m_presetWindowHeights = {1.0 / 3.0, 0.5, 2.0 / 3.0};
}

// ─────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────

TilingStateKey ScrollEngine::keyForScreen(const QString& screenId) const
{
    return TilingStateKey{screenId, m_currentDesktop, m_currentActivity};
}

ScrollScreenState* ScrollEngine::stateForKey(const TilingStateKey& key, bool create)
{
    auto it = m_states.find(key);
    if (it != m_states.end()) {
        return &it->second;
    }
    if (!create) {
        return nullptr;
    }
    return &m_states.try_emplace(key, key.screenId).first->second;
}

const ScrollScreenState* ScrollEngine::stateForWindowConst(const QString& windowId) const
{
    const auto keyIt = m_windowToKey.constFind(windowId);
    if (keyIt == m_windowToKey.constEnd()) {
        return nullptr;
    }
    const auto stateIt = m_states.find(keyIt.value());
    return stateIt == m_states.end() ? nullptr : &stateIt->second;
}

ScrollScreenState* ScrollEngine::resolveNavTarget(const NavigationContext& ctx, QString* outScreenId)
{
    const QString screenId = !ctx.screenId.isEmpty() ? ctx.screenId : m_activeScreen;
    if (outScreenId) {
        *outScreenId = screenId;
    }
    if (screenId.isEmpty()) {
        return nullptr;
    }
    return stateForKey(keyForScreen(screenId), /*create=*/false);
}

void ScrollEngine::emitChanged(const QString& screenId)
{
    if (!screenId.isEmpty()) {
        Q_EMIT placementChanged(screenId);
    }
}

void ScrollEngine::reportNav(bool success, const QString& action, const QString& screenId)
{
    Q_EMIT navigationFeedback(success, action, success ? QString() : QStringLiteral("no_target"), QString(), QString(),
                              screenId);
}

// ─────────────────────────────────────────────────────────────────────────
// Screen ownership
// ─────────────────────────────────────────────────────────────────────────

bool ScrollEngine::isActiveOnScreen(const QString& screenId) const
{
    return m_activeScreens.contains(screenId);
}

QSet<QString> ScrollEngine::activeScreens() const
{
    return m_activeScreens;
}

void ScrollEngine::setActiveScreens(const QSet<QString>& screens)
{
    m_activeScreens = screens;
}

bool ScrollEngine::isEnabled() const noexcept
{
    return !m_activeScreens.isEmpty();
}

QString ScrollEngine::activeScreen() const
{
    return m_activeScreen;
}

void ScrollEngine::setActiveScreenHint(const QString& screenId)
{
    if (!screenId.isEmpty()) {
        m_activeScreen = screenId;
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Desktop / activity context
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::setCurrentDesktop(int desktop)
{
    m_currentDesktop = desktop;
}

void ScrollEngine::setCurrentActivity(const QString& activity)
{
    m_currentActivity = activity;
}

QSet<int> ScrollEngine::desktopsWithActiveState() const
{
    QSet<int> desktops;
    for (const auto& entry : m_states) {
        desktops.insert(entry.first.desktop);
    }
    return desktops;
}

void ScrollEngine::pruneStatesForDesktop(int removedDesktop)
{
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (it->first.desktop == removedDesktop) {
            it = m_states.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_windowToKey.begin(); it != m_windowToKey.end();) {
        if (it.value().desktop == removedDesktop) {
            it = m_windowToKey.erase(it);
        } else {
            ++it;
        }
    }
}

void ScrollEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    const auto isStale = [&validActivities](const QString& activity) {
        return !activity.isEmpty() && !validActivities.contains(activity);
    };
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (isStale(it->first.activity)) {
            it = m_states.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_windowToKey.begin(); it != m_windowToKey.end();) {
        if (isStale(it.value().activity)) {
            it = m_windowToKey.erase(it);
        } else {
            ++it;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Window lifecycle
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    // minWidth/minHeight are part of the IPlacementEngine::windowOpened
    // signature (autotile uses them for column sizing). Scroll's strip model
    // is size-agnostic: a non-resizable window is fitted to its tile slot
    // effect-side (constrainToScrollSlot), so the constraints are unused here.
    Q_UNUSED(minWidth)
    Q_UNUSED(minHeight)
    if (windowId.isEmpty() || screenId.isEmpty() || m_windowToKey.contains(windowId)) {
        return;
    }
    const TilingStateKey key = keyForScreen(screenId);
    stateForKey(key, /*create=*/true)->addColumnForWindow(windowId);
    m_windowToKey.insert(windowId, key);
    m_activeScreen = screenId;
    emitChanged(screenId);
}

void ScrollEngine::windowClosed(const QString& windowId)
{
    const auto it = m_windowToKey.find(windowId);
    if (it == m_windowToKey.end()) {
        return;
    }
    const TilingStateKey key = it.value();
    m_windowToKey.erase(it);
    if (ScrollScreenState* state = stateForKey(key, /*create=*/false)) {
        state->removeWindow(windowId);
        emitChanged(key.screenId);
    }
}

void ScrollEngine::windowFocused(const QString& windowId, const QString& screenId)
{
    if (!screenId.isEmpty()) {
        m_activeScreen = screenId;
    }
    const auto it = m_windowToKey.constFind(windowId);
    if (it == m_windowToKey.constEnd()) {
        return;
    }
    if (ScrollScreenState* state = stateForKey(it.value(), /*create=*/false); state && state->focusWindow(windowId)) {
        emitChanged(it.value().screenId);
    }
}

void ScrollEngine::windowMinimizedChanged(const QString& windowId, bool minimized)
{
    const auto it = m_windowToKey.constFind(windowId);
    if (it == m_windowToKey.constEnd()) {
        return;
    }
    if (ScrollScreenState* state = stateForKey(it.value(), /*create=*/false);
        state && state->setWindowMinimized(windowId, minimized)) {
        emitChanged(it.value().screenId);
    }
}

void ScrollEngine::windowDropped(const QString& draggedWindowId, const QString& anchorWindowId, bool placeAfter)
{
    const auto it = m_windowToKey.constFind(draggedWindowId);
    if (it == m_windowToKey.constEnd()) {
        return;
    }
    if (ScrollScreenState* state = stateForKey(it.value(), /*create=*/false);
        state && state->moveColumnNextTo(draggedWindowId, anchorWindowId, placeAfter)) {
        // The drag focused the window — make its screen active so the viewport
        // fit-scrolls to it on the next resolve.
        m_activeScreen = it.value().screenId;
        emitChanged(it.value().screenId);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Float
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::setWindowFloat(const QString& windowId, bool shouldFloat)
{
    const auto it = m_windowToKey.constFind(windowId);
    if (it == m_windowToKey.constEnd()) {
        return;
    }
    ScrollScreenState* state = stateForKey(it.value(), /*create=*/false);
    if (!state || state->isFloating(windowId) == shouldFloat) {
        // Already in the requested state — no transition, no signal.
        return;
    }
    if (shouldFloat) {
        state->markFloating(windowId);
    } else {
        state->clearFloating(windowId);
        state->addColumnForWindow(windowId); // re-enter the strip
    }
    Q_EMIT windowFloatingChanged(windowId, shouldFloat, it.value().screenId);
    emitChanged(it.value().screenId);
}

void ScrollEngine::toggleWindowFloat(const QString& windowId, const QString& screenId)
{
    Q_UNUSED(screenId)
    const auto it = m_windowToKey.constFind(windowId);
    if (it == m_windowToKey.constEnd()) {
        return;
    }
    if (const ScrollScreenState* state = stateForKey(it.value(), /*create=*/false)) {
        setWindowFloat(windowId, !state->isFloating(windowId));
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────

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
    QString screenId;
    resolveNavTarget(ctx, &screenId);
    emitChanged(screenId);
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
    // equivalent — the strip is navigated, not rotated.
    Q_UNUSED(clockwise)
    Q_UNUSED(ctx)
}

void ScrollEngine::snapAllWindows(const NavigationContext& ctx)
{
    // No unmanaged-window adoption in the skeleton — windows enter the strip
    // through windowOpened. Daemon-driven adoption arrives with M3c wiring.
    Q_UNUSED(ctx)
}

void ScrollEngine::pushToEmptyZone(const NavigationContext& ctx)
{
    // "Empty zone" is a manual-layout concept; the strip has no fixed slots.
    Q_UNUSED(ctx)
}

void ScrollEngine::restoreFocusedWindow(const NavigationContext& ctx)
{
    // Restoring a window's pre-tile geometry depends on the daemon-side
    // unmanaged-geometry store; wired in a later milestone.
    Q_UNUSED(ctx)
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
    if (!state || !state->activeColumn() || m_presetColumnWidths.isEmpty()) {
        reportNav(false, QStringLiteral("width"), screenId);
        return;
    }
    const int current = state->activeColumn()->presetWidthIndex();
    const int next = (current + 1) % static_cast<int>(m_presetColumnWidths.size());
    if (next == current) {
        // Single-element preset list, already on it — nothing changes.
        reportNav(false, QStringLiteral("width"), screenId);
        return;
    }
    state->setActiveColumnWidth(ColumnWidth::proportion(m_presetColumnWidths.at(next)), next);
    emitChanged(screenId);
    reportNav(true, QStringLiteral("width"), screenId);
}

void ScrollEngine::cyclePresetWindowHeight(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    const Column* column = state ? state->activeColumn() : nullptr;
    const Tile* tile = column ? column->activeTile() : nullptr;
    if (!tile || m_presetWindowHeights.isEmpty()) {
        reportNav(false, QStringLiteral("height"), screenId);
        return;
    }
    const int current = (tile->height.kind == WindowHeight::Kind::Preset) ? tile->height.presetIndex : -1;
    const int next = (current + 1) % static_cast<int>(m_presetWindowHeights.size());
    if (next == current) {
        // Single-element preset list, already on it — nothing changes.
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
    // Smallest proportional column width grow/shrink can settle on, so a
    // column can never shrink away to nothing.
    constexpr qreal kMinColumnWidthFraction = 0.1;

    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    const Column* column = state ? state->activeColumn() : nullptr;
    if (!column || column->width().kind != ColumnWidth::Kind::Proportion) {
        // No focused column, or a fixed-pixel width — the geometry-agnostic
        // engine cannot resolve a pixel width to a working-area fraction.
        reportNav(false, QStringLiteral("width"), screenId);
        return;
    }
    const qreal current = column->width().value;
    const qreal target = qBound(kMinColumnWidthFraction, current + deltaFraction, qreal(1.0));
    if (qFuzzyCompare(target, current)) {
        reportNav(false, QStringLiteral("width"), screenId); // already at the limit
        return;
    }
    // The width is no longer one of the presets; -1 detaches it from the cycle.
    state->setActiveColumnWidth(ColumnWidth::proportion(target), /*presetIndex=*/-1);
    emitChanged(screenId);
    reportNav(true, QStringLiteral("width"), screenId);
}

void ScrollEngine::toggleCenterFocusedColumn(const NavigationContext& ctx)
{
    QString screenId;
    ScrollScreenState* state = resolveNavTarget(ctx, &screenId);
    if (!state) {
        reportNav(false, QStringLiteral("viewport"), screenId);
        return;
    }
    // The viewport mode is engine-global; flip it and re-resolve the focused
    // screen now — other scroll screens pick it up on their next relayout.
    m_viewportMode =
        (m_viewportMode == ScrollViewportMode::Fit) ? ScrollViewportMode::Centered : ScrollViewportMode::Fit;
    emitChanged(screenId);
    reportNav(true, QStringLiteral("viewport"), screenId);
}

// ─────────────────────────────────────────────────────────────────────────
// Tracking queries
// ─────────────────────────────────────────────────────────────────────────

bool ScrollEngine::isWindowTracked(const QString& windowId) const
{
    return m_windowToKey.contains(windowId);
}

bool ScrollEngine::isWindowTiled(const QString& windowId) const
{
    const ScrollScreenState* state = stateForWindowConst(windowId);
    return state && !state->placementIdForWindow(windowId).isEmpty();
}

bool ScrollEngine::isWindowManaged(const QString& windowId) const
{
    return isWindowTracked(windowId);
}

QString ScrollEngine::screenForTrackedWindow(const QString& windowId) const
{
    const auto it = m_windowToKey.constFind(windowId);
    return it == m_windowToKey.constEnd() ? QString() : it.value().screenId;
}

QStringList ScrollEngine::managedWindowOrder(const QString& screenId) const
{
    const IPlacementState* state = stateForScreen(screenId);
    return state ? state->managedWindows() : QStringList();
}

// ─────────────────────────────────────────────────────────────────────────
// Preset lists
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::setPresetColumnWidths(const QVector<qreal>& fractions)
{
    m_presetColumnWidths = fractions;
}

void ScrollEngine::setPresetWindowHeights(const QVector<qreal>& fractions)
{
    m_presetWindowHeights = fractions;
}

// ─────────────────────────────────────────────────────────────────────────
// State access
// ─────────────────────────────────────────────────────────────────────────

IPlacementState* ScrollEngine::stateForScreen(const QString& screenId)
{
    return stateForKey(keyForScreen(screenId), /*create=*/false);
}

const IPlacementState* ScrollEngine::stateForScreen(const QString& screenId) const
{
    const auto it = m_states.find(keyForScreen(screenId));
    return it == m_states.end() ? nullptr : &it->second;
}

// ─────────────────────────────────────────────────────────────────────────
// Persistence
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::saveState()
{
    // The daemon orchestrates disk persistence via serializeEngineState();
    // there is no engine-local config backend.
}

void ScrollEngine::loadState()
{
    // Counterpart to saveState() — restoration runs through
    // deserializeEngineState() under daemon control.
}

QJsonObject ScrollEngine::serializeEngineState() const
{
    QJsonArray states;
    for (const auto& entry : m_states) {
        QJsonObject obj = entry.second.toJson();
        obj.insert(QLatin1String("desktop"), entry.first.desktop);
        obj.insert(QLatin1String("activity"), entry.first.activity);
        states.append(obj);
    }
    QJsonObject result;
    result.insert(QLatin1String("states"), states);
    return result;
}

void ScrollEngine::deserializeEngineState(const QJsonObject& state)
{
    m_states.clear();
    m_windowToKey.clear();
    const QJsonArray states = state.value(QLatin1String("states")).toArray();
    for (const QJsonValue& value : states) {
        const QJsonObject entry = value.toObject();
        const TilingStateKey key{entry.value(QLatin1String("screenId")).toString(),
                                 entry.value(QLatin1String("desktop")).toInt(1),
                                 entry.value(QLatin1String("activity")).toString()};
        if (key.screenId.isEmpty()) {
            continue;
        }
        ScrollScreenState restored = ScrollScreenState::fromJson(entry);
        const QStringList windows = restored.managedWindows();
        m_states.emplace(key, std::move(restored));
        for (const QString& windowId : windows) {
            m_windowToKey.insert(windowId, key);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// PlacementEngineBase FSM hooks
//
// ScrollEngine keeps the strip model authoritative in its ScrollScreenState
// objects; the base-class unmanaged-geometry FSM is not engaged in the
// skeleton, so these hooks are intentionally empty.
// ─────────────────────────────────────────────────────────────────────────

void ScrollEngine::onWindowClaimed(const QString& windowId)
{
    Q_UNUSED(windowId)
}

void ScrollEngine::onWindowReleased(const QString& windowId)
{
    Q_UNUSED(windowId)
}

void ScrollEngine::onWindowFloated(const QString& windowId)
{
    Q_UNUSED(windowId)
}

void ScrollEngine::onWindowUnfloated(const QString& windowId)
{
    Q_UNUSED(windowId)
}

} // namespace PhosphorScrollEngine
