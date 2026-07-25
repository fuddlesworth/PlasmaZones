// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

void ScrollEngine::insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId,
                                      int minWidth, int minHeight)
{
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);

    // Fixed-size / oversized windows cannot honour a column slot: float them
    // at their native size instead of forcing a tile (the min size is the
    // only constraint the compositor reports; a rule `float` action covers
    // the rest).
    const bool oversized =
        params.workArea.isValid() && (minWidth > params.workArea.width() || minHeight > params.workArea.height());
    const bool ruleFloated = m_floatPredicate && m_floatPredicate(windowId);
    if (oversized || ruleFloated) {
        state->addFloating(windowId);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        return;
    }

    // Unified-placement restore: a window recorded tiled in scrolling mode
    // reopens at its recorded column slot; a floating record reopens
    // floating (the shared free geometry is restored by the common layer).
    int restoreColumn = -1;
    if (m_windowTracker) {
        if (const auto record = m_windowTracker->placementStore().peekExact(windowId)) {
            const PhosphorEngine::EngineSlot slot = record->slotFor(engineId());
            if (slot.state == PhosphorEngine::WindowPlacement::stateFloating()) {
                state->addFloating(windowId);
                Q_EMIT windowFloatingChanged(windowId, true, screenId);
                return;
            }
            if (slot.state == PhosphorEngine::WindowPlacement::stateTiled() && slot.order >= 0) {
                restoreColumn = slot.order;
            }
        }
    }

    ColumnWidth width = effectiveDefaultColumnWidth(screenId);
    ColumnDisplay display = effectiveDefaultColumnDisplay(screenId);
    if (m_defaultWidthClientDecides && m_windowTracker) {
        // "Client decides": open at the client's own size when one is on
        // record; the first client resize reconciles it afterwards.
        if (const auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId)) {
            width = ColumnWidth::makeFixed(geo->width());
        }
    }

    // Per-window open rules layer over the context/config defaults.
    ScrollOpenParams openParams;
    if (m_openParamsResolver) {
        openParams = m_openParamsResolver(windowId);
    }
    if (openParams.widthFraction) {
        width = ColumnWidth::makeProportion(qBound<qreal>(0.05, *openParams.widthFraction, 1.0));
    }
    if (openParams.tabbed) {
        display = *openParams.tabbed ? ColumnDisplay::Tabbed : ColumnDisplay::Normal;
    }
    if (openParams.consume && *openParams.consume && !state->strip().isEmpty()) {
        if (state->strip().insertWindowIntoActiveColumn(windowId, width, display, params, minWidth, minHeight)) {
            return;
        }
    }

    // Deterministic mode-transition seeding: when the previous engine's
    // window order was captured for this screen, insert each arriving window
    // at its recorded relative position instead of next-to-focus.
    bool inserted = false;
    const QStringList pendingOrder = m_pendingInitialOrder.value(screenId);
    if (!pendingOrder.isEmpty()) {
        const int orderIdx = pendingOrder.indexOf(windowId);
        if (orderIdx >= 0) {
            int columnIdx = 0;
            const QStringList present = state->strip().windowsInOrder();
            for (const QString& earlier : pendingOrder.mid(0, orderIdx)) {
                if (present.contains(earlier)) {
                    ++columnIdx;
                }
            }
            inserted = state->strip().insertWindowAt(columnIdx, windowId, width, display);
            if (inserted) {
                state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
            }
        }
    }
    if (!inserted && restoreColumn >= 0) {
        inserted = state->strip().insertWindowAt(restoreColumn, windowId, width, display);
        if (inserted) {
            state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
        }
    }
    if (!inserted) {
        inserted = state->strip().insertWindow(windowId, width, display, params, minWidth, minHeight);
    }
    if (!inserted) {
        qCWarning(lcScrollEngine) << "insertOpenedWindow: duplicate window" << windowId;
    }
}

void ScrollEngine::windowOpened(const QString& rawWindowId, const QString& screenId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(screenId)) {
        return;
    }

    PhosphorEngine::PlacementStateKey oldKey;
    ScrollState* oldState = stateForWindow(windowId, &oldKey);
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    if (oldState && oldKey == key) {
        return; // re-announce of a window we already track here
    }
    if (oldState) {
        // The window moved context (screen or desktop) — migrate.
        const ScrollLayoutParams oldParams = layoutParamsForScreen(oldKey.screenId);
        oldState->strip().takeWindow(windowId, oldParams);
        oldState->removeFloating(windowId);
        scheduleRetileForScreen(oldKey.screenId);
    }

    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return;
    }
    insertOpenedWindow(state, windowId, screenId, minWidth, minHeight);
    m_states.setKeyForWindow(windowId, key);
    m_activeScreen = screenId;

    bool focusNew = true;
    if (auto* settings = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        focusNew = settings->scrollingFocusNewWindows();
    }
    applyLayout(screenId, focusNew && state->strip().activeWindowId() == windowId);
    Q_EMIT placementChanged(screenId);
}

void ScrollEngine::windowClosed(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    const bool wasActive = state->strip().activeWindowId() == windowId;
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    const bool inStrip = state->strip().removeWindow(windowId, params);
    if (!inStrip) {
        state->removeFloating(windowId);
    }
    m_states.removeWindow(windowId);
    m_lastAppliedRect.remove(windowId);
    m_floatRestoreColumn.remove(windowId);
    m_scrollFloatedWindows.remove(windowId);

    if (inStrip) {
        applyLayout(key.screenId, wasActive && !state->strip().activeWindowId().isEmpty());
    }
    Q_EMIT placementChanged(key.screenId);
}

void ScrollEngine::windowFocused(const QString& rawWindowId, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    if (!screenId.isEmpty() && m_scrollingScreens.contains(screenId)) {
        m_activeScreen = screenId;
    }
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    if (state->strip().focusWindow(windowId, params)) {
        // The focus change may scroll the viewport; never re-activate here
        // (the compositor initiated this focus).
        applyLayout(key.screenId, false);
    }
}

void ScrollEngine::windowMinSizeUpdated(const QString& rawWindowId, int minWidth, int minHeight)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    if (state->strip().setWindowMinimumSize(windowId, minWidth, minHeight)) {
        scheduleRetileForScreen(key.screenId);
    }
}

void ScrollEngine::onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                                   const QString& screenId)
{
    Q_UNUSED(oldFrame)
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state || state->isFloating(windowId)) {
        return;
    }
    // Reconcile the column to the size the client/user actually settled on;
    // only the owning column relayouts (a resize never reflows neighbours'
    // widths — they just shift).
    if (state->strip().reconcileWindowSize(windowId, newFrame.size())) {
        scheduleRetileForScreen(screenId.isEmpty() ? key.screenId : screenId);
    }
}

// ── Float management ────────────────────────────────────────────────────────

bool ScrollEngine::floatWindowInternal(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                                       const QString& windowId, const QString& screenId)
{
    if (state->isFloating(windowId)) {
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    const int columnIdx = state->strip().columnOfWindow(windowId);
    if (columnIdx < 0) {
        return false;
    }
    state->strip().takeWindow(windowId, params);
    state->addFloating(windowId);
    m_floatRestoreColumn.insert(windowId, columnIdx);
    m_scrollFloatedWindows.insert(windowId);
    m_lastAppliedRect.remove(windowId);
    Q_EMIT windowFloatingChanged(windowId, true, screenId.isEmpty() ? key.screenId : screenId);
    applyLayout(key.screenId, false);
    Q_EMIT placementChanged(key.screenId);
    return true;
}

bool ScrollEngine::unfloatWindowInternal(ScrollState* state, const QString& windowId, const QString& screenId)
{
    if (!state->removeFloating(windowId)) {
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    // Restore the remembered column slot (minimize/unminimize and float
    // round-trips keep their place); fall back to next-to-focus.
    const bool hadSlot = m_floatRestoreColumn.contains(windowId);
    const int restoreColumn = m_floatRestoreColumn.take(windowId);
    bool inserted = false;
    if (hadSlot) {
        inserted = state->strip().insertWindowAt(restoreColumn, windowId, m_defaultColumnWidth, m_defaultColumnDisplay);
    }
    if (!inserted) {
        inserted = state->strip().insertWindow(windowId, m_defaultColumnWidth, m_defaultColumnDisplay, params);
    }
    if (inserted) {
        state->strip().focusWindow(windowId, params);
    }
    m_scrollFloatedWindows.remove(windowId);
    Q_EMIT windowFloatingChanged(windowId, false, screenId);
    applyLayout(screenId, false);
    Q_EMIT placementChanged(screenId);
    return inserted;
}

void ScrollEngine::setWindowFloat(const QString& rawWindowId, bool shouldFloat, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);

    if (!state) {
        if (shouldFloat) {
            return; // never tracked — nothing to pull out of a strip
        }
        // Unfloat a window the engine has not adopted yet (e.g. floated
        // before the screen switched to scrolling): adopt into the
        // authoritative screen's current strip.
        const QString targetScreen = resolveOperationScreen(screenId);
        if (targetScreen.isEmpty()) {
            return;
        }
        ScrollState* target = stateForKey(currentKeyForScreen(targetScreen), true);
        if (!target || target->containsWindow(windowId)) {
            return;
        }
        const ScrollLayoutParams params = layoutParamsForScreen(targetScreen);
        if (target->strip().insertWindow(windowId, m_defaultColumnWidth, m_defaultColumnDisplay, params)) {
            m_states.setKeyForWindow(windowId, currentKeyForScreen(targetScreen));
            Q_EMIT windowFloatingChanged(windowId, false, targetScreen);
            applyLayout(targetScreen, false);
            Q_EMIT placementChanged(targetScreen);
        }
        return;
    }

    if (shouldFloat) {
        floatWindowInternal(state, key, windowId, screenId);
    } else {
        unfloatWindowInternal(state, windowId, key.screenId);
    }
}

void ScrollEngine::toggleWindowFloat(const QString& rawWindowId, const QString& screenId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    ScrollState* state = stateForWindow(windowId);
    const bool floating = state && state->isFloating(windowId);
    setWindowFloat(windowId, !floating, screenId);
}

// ── Cross-engine handoff ────────────────────────────────────────────────────

void ScrollEngine::handoffRelease(const QString& rawWindowId)
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return;
    }
    // Tracking-only clear: the receiving engine places the window; this
    // screen's remaining columns close up on the scheduled retile.
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    state->strip().takeWindow(windowId, params);
    state->removeFloating(windowId);
    m_states.removeWindow(windowId);
    m_lastAppliedRect.remove(windowId);
    m_floatRestoreColumn.remove(windowId);
    scheduleRetileForScreen(key.screenId);
}

void ScrollEngine::handoffReceive(const HandoffContext& ctx)
{
    const QString windowId = canonicalizeForLookup(ctx.windowId);
    if (windowId.isEmpty() || !m_scrollingScreens.contains(ctx.toScreenId)) {
        return;
    }
    PhosphorEngine::PlacementStateKey key = currentKeyForScreen(ctx.toScreenId);
    if (ctx.toDesktop > 0) {
        key.desktop = ctx.toDesktop;
    }
    ScrollState* state = stateForKey(key, true);
    if (!state || state->containsWindow(windowId)) {
        return;
    }
    if (ctx.wasFloating) {
        state->addFloating(windowId);
        m_states.setKeyForWindow(windowId, key);
        Q_EMIT windowFloatingChanged(windowId, true, ctx.toScreenId);
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(ctx.toScreenId);
    ColumnWidth width = m_defaultColumnWidth;
    if (ctx.sourceGeometry.isValid()) {
        width = ColumnWidth::makeFixed(ctx.sourceGeometry.width());
    }
    // Arriving from the left edge enters as the FIRST column, from the right
    // as the LAST — mirroring the niri "entered from this edge" intuition.
    const int columnIdx = (ctx.insertIndex >= 0) ? ctx.insertIndex : state->strip().columnCount();
    if (state->strip().insertWindowAt(columnIdx, windowId, width, m_defaultColumnDisplay)) {
        m_states.setKeyForWindow(windowId, key);
        const bool isCurrentContext = key == currentKeyForScreen(ctx.toScreenId);
        if (isCurrentContext) {
            state->strip().focusWindow(windowId, params);
            applyLayout(ctx.toScreenId, false);
        }
        Q_EMIT placementChanged(ctx.toScreenId);
    }
}

// ── Unified placement capture ───────────────────────────────────────────────

std::optional<PhosphorEngine::WindowPlacement> ScrollEngine::capturePlacement(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    PhosphorEngine::PlacementStateKey key;
    const ScrollState* state = stateForWindow(windowId, &key);
    if (!state) {
        return std::nullopt;
    }
    PhosphorEngine::WindowPlacement placement;
    placement.windowId = windowId;
    placement.appId = PhosphorIdentity::WindowId::extractAppId(windowId);
    placement.screenId = key.screenId;
    placement.virtualDesktop = key.desktop;
    placement.activity = key.activity;

    PhosphorEngine::EngineSlot slot;
    if (state->isFloating(windowId)) {
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
    } else {
        slot.state = PhosphorEngine::WindowPlacement::stateTiled();
        slot.order = state->strip().windowsInOrder().indexOf(windowId);
    }
    placement.engines.insert(engineId(), slot);
    return placement;
}

} // namespace PhosphorScrollEngine
