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
    // Two-branch lookup: never construct-then-erase on a miss. The
    // create=false path (windowFocused / windowMinimizedChanged /
    // windowDropped / setWindowFloat / toggleWindowFloat) hits this on every
    // unknown key, so building a ScrollScreenState{key.screenId} just to
    // immediately discard it would be a per-call waste. The create=true path
    // does a second hash via emplace, but only on the actual-miss branch
    // where allocation is unavoidable anyway.
    auto it = m_states.find(key);
    if (it != m_states.end()) {
        return &it->second;
    }
    if (!create) {
        return nullptr;
    }
    return &m_states.emplace(key, key.screenId).first->second;
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
        // Skip husk states — a state with zero managed windows is the
        // residue of every-window-closed that reconcileRestoredWindows /
        // pruneStatesForDesktop / pruneStatesForScreen left behind. The
        // signal "this desktop has live state" must reflect actual content,
        // not lifetime artefacts. Mirrors serializeEngineState's filter.
        if (entry.second.managedWindows().isEmpty()) {
            continue;
        }
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
            // Even on the same-context idempotent re-report, refresh the
            // active-screen hint so the next navigation chord (or viewport
            // re-resolve) targets the right strip. The first windowOpened
            // sets m_activeScreen at the bottom of this method; a duplicate
            // report from a D-Bus retry / effect re-announce should match
            // that contract rather than leave m_activeScreen stale on
            // whatever value preceded it.
            m_activeScreen = screenId;
            return;
        }
        const TilingStateKey oldKey = existingIt.value();
        bool oldStateExisted = false;
        {
            // Inner scope owns the (potentially-dangling-after-erase) pointer
            // so emitChanged below cannot accidentally touch it. The structural
            // scope is the safety net — there is no `oldState` name visible
            // outside.
            ScrollScreenState* oldState = stateForKey(oldKey, /*create=*/false);
            if (oldState) {
                oldStateExisted = true;
                oldState->removeWindow(windowId);
                // If migrating the window emptied the old strip entirely, drop
                // the state so it doesn't linger as a phantom (eternally
                // serialised into scroll-session.json with zero columns).
                // hasPersistableState also relies on the m_states size, so
                // leaving the husk in place would make the daemon write
                // empty-strip JSON every shutdown.
                //
                // Use managedWindows().isEmpty() — NOT isEmpty() — so a state
                // with zero columns but non-empty floating set is correctly
                // treated as still occupied. isEmpty() reports column count
                // only, which would erase a state that still owns floating
                // windows (a tracked floater migrated away while another window
                // remained floating on the old strip). serializeEngineState and
                // hasPersistableState use the same managedWindows()-based
                // emptiness predicate, so this matches the persistence contract.
                if (oldState->managedWindows().isEmpty()) {
                    m_states.erase(oldKey);
                    oldState = nullptr; // erased — every read after this would dangle
                }
            }
        }
        if (oldStateExisted) {
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
    bool stateExisted = false;
    {
        // Inner scope owns the (potentially-dangling-after-erase) state pointer
        // so emitChanged below cannot accidentally touch it. The structural
        // scope is the safety net — there is no `state` name visible outside.
        ScrollScreenState* state = stateForKey(key, /*create=*/false);
        if (state) {
            stateExisted = true;
            state->removeWindow(windowId);
            // If closing this window emptied the strip entirely, drop the state
            // so it doesn't linger as bookkeeping noise. desktopsWithActiveState()
            // and the daemon's prune paths would otherwise report this desktop
            // as "having state" forever even after every window closed there.
            // Symmetric with the windowOpened migration path's empty-state
            // erase and with serializeEngineState's empty-state skip.
            if (state->managedWindows().isEmpty()) {
                m_states.erase(key);
                state = nullptr; // erased — every read after this would dangle
            }
        }
    }
    if (stateExisted) {
        emitChanged(key.screenId);
    }
}

void ScrollEngine::windowFocused(const QString& windowId, const QString& screenId)
{
    // Only update the active-screen hint when the focused window is
    // scroll-tracked. Otherwise a focus event for an unmanaged window
    // (a snap floater, an autotile window, an override-redirect popup)
    // would silently shift the engine's active-screen target — and the
    // next navigation chord would then operate on a strip the user
    // never expected. The hint is scroll-engine state; only scroll-mode
    // windows are entitled to move it.
    const auto it = m_windowToKey.constFind(windowId);
    if (it == m_windowToKey.constEnd()) {
        return;
    }
    if (!screenId.isEmpty()) {
        m_activeScreen = screenId;
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
    if (!state) {
        return;
    }
    // Already in the requested state — no transition, no signal. The
    // isFloating==shouldFloat half catches the two equality paths
    // (floating→float and tiled→tile, the latter being the "not floating,
    // asked to un-float" case): isFloating returns false for a tiled window,
    // and shouldFloat=false matches it, so the comparison guards both
    // directions in one branch.
    if (state->isFloating(windowId) == shouldFloat) {
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

ScrollScreenState* ScrollEngine::scrollStateForScreen(const QString& screenId)
{
    // IScrollEngine's contract is "no creation here", so an explicit find is
    // both clearer than threading `create=false` through stateForKey() and
    // mirrors the const overload below — the only structural difference
    // between the two is the return-type constness.
    const auto it = m_states.find(keyForScreen(screenId));
    return it == m_states.end() ? nullptr : &it->second;
}

const ScrollScreenState* ScrollEngine::scrollStateForScreen(const QString& screenId) const
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
    // Reset only the persisted-shape containers — the strip states keyed by
    // {screenId, desktop, activity} and the windowId→key index. The runtime
    // context (m_currentDesktop / m_currentActivity / m_activeScreens / focus
    // hint / per-screen overrides) is OWNED by the daemon: on boot it calls
    // setCurrentDesktop / setActiveScreens / applyPerScreenConfig BEFORE
    // handing the persisted JSON to the engine, so clearing those fields here
    // would silently overwrite the daemon's authoritative live values with
    // whatever defaults the engine carried at construction. Only m_states and
    // m_windowToKey are derived from the JSON blob, so only they are reset.
    m_states.clear();
    m_windowToKey.clear();
    const QJsonArray states = state.value(QLatin1String("states")).toArray();
    // Track every windowId already claimed by a previously-restored state so
    // a corrupt scroll-session.json with the same windowId in two states
    // doesn't leave m_windowToKey pointing at the second state while the
    // first state still owns the column. ScrollScreenState::fromJson dedupes
    // WITHIN a state; this set provides the cross-state dedup the engine
    // owns. First occurrence wins (consistent with windowsOpenedBatch's
    // dedup contract). The rejected duplicate's column is dropped from the
    // second state so the in-memory shape matches m_windowToKey.
    QSet<QString> claimedWindowIds;
    for (const QJsonValue& value : states) {
        const QJsonObject entry = value.toObject();
        // Validate desktop range at the persistence boundary: virtual desktops
        // are 1-based, so a zero or negative entry from corrupt JSON would
        // create an unreachable state-key. Default to kDefaultDesktopId in
        // that case so the strip surfaces somewhere rather than disappearing
        // entirely.
        const int rawDesktop = entry.value(QLatin1String("desktop")).toInt(kDefaultDesktopId);
        const int desktop = rawDesktop > 0 ? rawDesktop : kDefaultDesktopId;
        const TilingStateKey key{entry.value(QLatin1String("screenId")).toString(), desktop,
                                 entry.value(QLatin1String("activity")).toString()};
        if (key.screenId.isEmpty()) {
            continue;
        }
        ScrollScreenState restored = ScrollScreenState::fromJson(entry);
        // Drop any window already claimed by an earlier state.
        const QStringList rawWindows = restored.managedWindows();
        for (const QString& windowId : rawWindows) {
            if (claimedWindowIds.contains(windowId)) {
                restored.removeWindow(windowId);
            }
        }
        // Re-read the survivors after the dedup pass so m_windowToKey only
        // points at windows actually still in this state.
        const QStringList windows = restored.managedWindows();
        if (windows.isEmpty()) {
            // Whole state dedup'd away — skip the empty husk.
            continue;
        }
        // Cross-state dedup above prunes duplicate WINDOWS, but a corrupt
        // scroll-session.json could still hand us two state entries with the
        // same {screenId, desktop, activity} TilingStateKey. std::map::emplace
        // returns inserted=false in that case and discards the second value —
        // but the windowId loop below would still wire those (now-orphaned)
        // windows into m_windowToKey, leaving permanent dangling references
        // pointing at the FIRST state. Skip the windowId wiring whenever the
        // emplace was rejected; this state's column is silently dropped, which
        // is consistent with the cross-state dedup contract above (first
        // occurrence wins).
        if (!m_states.emplace(key, std::move(restored)).second) {
            continue;
        }
        for (const QString& windowId : windows) {
            m_windowToKey.insert(windowId, key);
            claimedWindowIds.insert(windowId);
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
    // No re-lookup of m_windowToKey: every windowId in `stale` was just
    // observed live in the first pass and nothing mutates the map between
    // collection and now, so QHash::take() is safe and one operation cheaper
    // than constFind() + erase(it).
    QSet<QString> dirtyScreens;
    QSet<TilingStateKey> touchedKeys;
    for (const QString& windowId : stale) {
        const TilingStateKey key = m_windowToKey.take(windowId);
        if (ScrollScreenState* state = stateForKey(key, /*create=*/false)) {
            state->removeWindow(windowId);
            dirtyScreens.insert(key.screenId);
            touchedKeys.insert(key);
        }
    }
    // A state that loses every window during reconcile is a husk — leaving it
    // in m_states would inflate desktopsWithActiveState() / serializeEngineState
    // with empty entries that carry no information. Drop them now so the
    // post-reconcile shape matches a fresh boot with the same surviving
    // window set.
    for (const TilingStateKey& key : touchedKeys) {
        const auto it = m_states.find(key);
        if (it != m_states.end() && it->second.managedWindows().isEmpty()) {
            m_states.erase(it);
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
