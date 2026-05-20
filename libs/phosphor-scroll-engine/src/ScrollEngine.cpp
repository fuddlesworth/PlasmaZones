// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QJsonArray>
#include <QSet>

#include <cmath>
#include <utility>

namespace PhosphorScrollEngine {

using PhosphorEngine::IPlacementState;
using PhosphorEngine::IScrollSettings;
using PhosphorEngine::NavigationContext;
using PhosphorEngine::TilingStateKey;

QVector<qreal> ScrollEngine::toFractionVector(const QVariantList& list)
{
    // Drop entries that don't convert cleanly to a finite number. Two failure
    // modes: (a) a string that toReal() coerces to 0.0 with ok=false would
    // silently appear as a 0.0 entry and the wrapping clampedFractionVector
    // would pin it to kMinSizeFraction; (b) Qt's number parser accepts the
    // literal strings "nan" and "inf" with ok=true, and NaN propagates
    // through qBound (NaN comparisons are unordered) into the layout
    // resolver where it produces NaN geometry. Filter both cases.
    QVector<qreal> out;
    out.reserve(list.size());
    for (const QVariant& v : list) {
        bool ok = false;
        const qreal value = v.toReal(&ok);
        if (ok && std::isfinite(value)) {
            out.append(value);
        }
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
    // Cache the qobject_cast result keyed by the underlying QObject* identity.
    // The effective*() resolvers run on every relayout — making the cross-cast
    // a hot path — and the engineSettings pointer rarely changes (it's set
    // once at construction and survives daemon restarts). Re-cast only when
    // the underlying pointer flips so the fast path is a single comparison.
    QObject* current = engineSettings();
    if (current != m_cachedScrollSettingsSource) {
        m_cachedScrollSettingsSource = current;
        m_cachedScrollSettings = qobject_cast<PhosphorEngine::IScrollSettings*>(current);
    }
    return m_cachedScrollSettings;
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
            // If migrating the window emptied the old strip entirely, drop the
            // state so it doesn't linger as a phantom (eternally serialised
            // into scroll-session.json with zero columns). hasPersistableState
            // also relies on the m_states size, so leaving the husk in place
            // would make the daemon write empty-strip JSON every shutdown.
            //
            // CRITICAL: oldState is an interior pointer into m_states; after
            // m_states.erase(oldKey) it dangles. Null it out at the same scope
            // so any future maintenance access in this block fails fast
            // instead of corrupting heap.
            const bool oldEmpty = oldState->isEmpty();
            if (oldEmpty) {
                m_states.erase(oldKey);
                oldState = nullptr;
            }
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
// Navigation handlers (focus/move/swap/cycle/reapply/float, plus the niri
// strip ops consume/expel/cyclePreset/toggleFullWidth/adjustColumnWidth, plus
// the unsupported-op feedback paths) live in ScrollEngineNavigation.cpp to
// keep this translation unit under the 800-line limit.
// ─────────────────────────────────────────────────────────────────────────

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
    // An empty-strip state is bookkeeping noise — the daemon's saveScrollState
    // would otherwise write a non-empty scroll-session.json containing only
    // {"states": [{"columns": [], ...}]} every shutdown. Treat the engine as
    // having nothing to persist when every state has zero columns AND the
    // floating set is empty too. (managedWindows() empty implies both.)
    for (const auto& entry : m_states) {
        if (!entry.second.managedWindows().isEmpty()) {
            return true;
        }
    }
    return false;
}

QJsonObject ScrollEngine::serializeEngineState() const
{
    // The viewport mode is deliberately not serialized: it is derived on every
    // resolve from the scrollCenterFocusedColumn setting (per-screen override →
    // IScrollSettings global) by effectiveViewportMode(), so there is no
    // engine-local state to persist. Per-column full-width state *is* persisted
    // (in ScrollScreenState).
    //
    // Empty states (zero columns AND zero floating windows) are skipped: they
    // carry no information and would just bloat scroll-session.json.
    QJsonArray states;
    for (const auto& entry : m_states) {
        if (entry.second.managedWindows().isEmpty()) {
            continue;
        }
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
    // Full reset of every per-engine container before applying the persisted
    // state. m_states/m_windowToKey were already cleared; the rest are
    // session-time bookkeeping that the serialised JSON does not own (active
    // screens, per-screen overrides, focus hint, current desktop/activity).
    // Leaving them behind on a re-deserialise (e.g. a config-reload path)
    // would carry stale config from the previous session into the restored
    // strip and silently desync from the daemon's authoritative state.
    m_states.clear();
    m_windowToKey.clear();
    m_perScreenConfig.clear();
    m_activeScreens.clear();
    m_activeScreen.clear();
    m_currentDesktop = 1;
    m_currentActivity.clear();
    const QJsonArray states = state.value(QLatin1String("states")).toArray();
    for (const QJsonValue& value : states) {
        const QJsonObject entry = value.toObject();
        // Validate desktop range at the persistence boundary: virtual desktops
        // are 1-based, so a zero or negative entry from corrupt JSON would
        // create an unreachable state-key. Default to desktop 1 in that case
        // so the strip surfaces somewhere rather than disappearing entirely.
        const int rawDesktop = entry.value(QLatin1String("desktop")).toInt(1);
        const int desktop = rawDesktop > 0 ? rawDesktop : 1;
        const TilingStateKey key{entry.value(QLatin1String("screenId")).toString(), desktop,
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
    // Coalesce placementChanged emits: many stale windows on the same screen
    // would otherwise fan out into N daemon resolves. Bypass windowClosed()'s
    // per-window emit and dispatch one emit per affected screen at the end.
    QSet<QString> dirtyScreens;
    for (const QString& windowId : stale) {
        const auto it = m_windowToKey.constFind(windowId);
        if (it == m_windowToKey.constEnd()) {
            continue;
        }
        const TilingStateKey key = it.value();
        m_windowToKey.erase(it);
        if (ScrollScreenState* state = stateForKey(key, /*create=*/false)) {
            state->removeWindow(windowId);
            dirtyScreens.insert(key.screenId);
        }
    }
    for (const QString& screenId : dirtyScreens) {
        emitChanged(screenId);
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
