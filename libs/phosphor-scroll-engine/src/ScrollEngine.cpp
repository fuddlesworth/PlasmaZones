// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QJsonArray>
#include <QSet>

#include <utility>

namespace PhosphorScrollEngine {

using PhosphorEngine::IPlacementState;
using PhosphorEngine::IScrollSettings;
using PhosphorEngine::NavigationContext;
using PhosphorEngine::TilingStateKey;

QVector<qreal> ScrollEngine::toFractionVector(const QVariantList& list)
{
    QVector<qreal> out;
    out.reserve(list.size());
    for (const QVariant& v : list) {
        out.append(v.toReal());
    }
    return out;
}

ScrollEngine::ScrollEngine(QObject* parent)
    : PhosphorEngine::PlacementEngineBase(parent)
{
}

// ─────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────

PhosphorEngine::IScrollSettings* ScrollEngine::scrollSettings() const
{
    return qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings());
}

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

void ScrollEngine::pruneStatesForScreen(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (it->first.screenId == screenId) {
            it = m_states.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_windowToKey.begin(); it != m_windowToKey.end();) {
        if (it.value().screenId == screenId) {
            it = m_windowToKey.erase(it);
        } else {
            ++it;
        }
    }
    m_perScreenConfig.remove(screenId);
    m_activeScreens.remove(screenId);
    if (m_activeScreen == screenId) {
        m_activeScreen.clear();
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
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    const TilingStateKey newKey = keyForScreen(screenId);
    const auto existingIt = m_windowToKey.constFind(windowId);
    if (existingIt != m_windowToKey.constEnd()) {
        // Already tracked. If the report names the same context, nothing to do
        // — addColumnForWindow self-gates on duplicates anyway. If the screen
        // (or desktop / activity) differs, the user moved the window between
        // sessions while the daemon was down: the post-restore reconcile path
        // already pruned non-live windows, but a live window that simply
        // changed monitors still has its restored column under the OLD key.
        // Migrate it to the new strip so geometry resolves against the right
        // working area on the next placementChanged.
        if (existingIt.value() == newKey) {
            return;
        }
        const TilingStateKey oldKey = existingIt.value();
        if (ScrollScreenState* oldState = stateForKey(oldKey, /*create=*/false)) {
            oldState->removeWindow(windowId);
            emitChanged(oldKey.screenId);
        }
        m_windowToKey.erase(existingIt);
    }
    stateForKey(newKey, /*create=*/true)
        ->addColumnForWindow(windowId, ColumnWidth::proportion(effectiveDefaultColumnWidth(screenId)));
    m_windowToKey.insert(windowId, newKey);
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
        // Re-enter the strip as a new column at the configured default width.
        state->addColumnForWindow(windowId, ColumnWidth::proportion(effectiveDefaultColumnWidth(it.value().screenId)));
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
    if (next == current) {
        // Single-element preset list, already on it — nothing changes.
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

bool ScrollEngine::hasPersistableState() const
{
    return !m_states.empty();
}

QJsonObject ScrollEngine::serializeEngineState() const
{
    // The viewport mode is deliberately not serialized: it is derived on every
    // resolve from the scrollCenterFocusedColumn setting (per-screen override →
    // IScrollSettings global) by effectiveViewportMode(), so there is no
    // engine-local state to persist. Per-column full-width state *is* persisted
    // (in ScrollScreenState).
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
    // A restored strip is structural — it must be reconciled against the live
    // window set once the effect reports it; see reconcileRestoredWindows().
    m_pendingRestoreReconcile = !m_states.empty();
}

void ScrollEngine::reconcileRestoredWindows(const QSet<QString>& liveWindowIds)
{
    if (!m_pendingRestoreReconcile) {
        return;
    }
    m_pendingRestoreReconcile = false; // one-shot — only the first batch reconciles

    // Any restored window the live set did not confirm was closed while the
    // daemon was down: drop it so its column does not linger as a phantom.
    // Collected first, then removed, so m_windowToKey is not mutated mid-scan.
    QStringList stale;
    for (auto it = m_windowToKey.constBegin(); it != m_windowToKey.constEnd(); ++it) {
        if (!liveWindowIds.contains(it.key())) {
            stale.append(it.key());
        }
    }
    for (const QString& windowId : stale) {
        windowClosed(windowId);
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
