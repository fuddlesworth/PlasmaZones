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
    // at its recorded relative position instead of next-to-focus. Each id is
    // CONSUMED on use and the entry is dropped once empty — the header's
    // "consumed as windows arrive" contract. Without consumption a stale
    // seed would re-position an unrelated later open that happens to share
    // an id with the captured list.
    bool inserted = false;
    const auto pendingIt = m_pendingInitialOrder.find(screenId);
    if (pendingIt != m_pendingInitialOrder.end()) {
        const int orderIdx = pendingIt->indexOf(windowId);
        if (orderIdx >= 0) {
            int columnIdx = 0;
            const QStringList present = state->strip().windowsInOrder();
            for (const QString& earlier : pendingIt->mid(0, orderIdx)) {
                if (present.contains(earlier)) {
                    ++columnIdx;
                }
            }
            inserted = state->strip().insertWindowAt(columnIdx, windowId, width, display);
            if (inserted) {
                state->strip().setWindowMinimumSize(windowId, minWidth, minHeight);
            }
            pendingIt->removeAll(windowId);
            if (pendingIt->isEmpty()) {
                m_pendingInitialOrder.erase(pendingIt);
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
    // Track BEFORE inserting: insertOpenedWindow's oversized/rule-float
    // paths emit windowFloatingChanged, and a synchronous query-back from a
    // subscriber must already see the window as this engine's.
    m_states.setKeyForWindow(windowId, key);
    insertOpenedWindow(state, windowId, screenId, minWidth, minHeight);
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
    m_floatRestore.remove(windowId);
    m_scrollFloatedWindows.remove(windowId);

    if (inStrip && key == currentKeyForScreen(key.screenId)) {
        // Background-context guard: applyLayout resolves the screen's
        // CURRENT context, so a close on another desktop's state would
        // relayout the wrong strip (the mutated one must stay silent
        // until its desktop returns).
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
        // (the compositor initiated this focus). Background-context guard:
        // see windowClosed.
        if (key == currentKeyForScreen(key.screenId)) {
            applyLayout(key.screenId, false);
        }
    }
}

QSize ScrollEngine::windowMinimumSize(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    const ScrollState* state = stateForWindow(windowId);
    if (!state) {
        return {};
    }
    return state->strip().windowMinimumSize(windowId);
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
    // widths — they just shift). Width intent is only rewritten when the
    // WIDTH moved relative to the last applied rect — a vertical-only
    // resize must not pin a Proportion/Preset column to pixels.
    const QRect lastApplied = m_lastAppliedRect.value(windowId);
    const bool widthChanged = !lastApplied.isValid() || lastApplied.width() != newFrame.width();
    const bool heightChanged = !lastApplied.isValid() || lastApplied.height() != newFrame.height();
    if (state->strip().reconcileWindowSize(windowId, newFrame.size(), widthChanged, heightChanged)) {
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
        // Tracked in the reverse map yet in neither the strip nor the
        // floating set — a genuine bookkeeping inconsistency, not a
        // documented no-op like the other bails in this file.
        qCWarning(lcScrollEngine) << "floatWindowInternal:" << windowId
                                  << "tracked but absent from strip and floating set on" << key.screenId;
        return false;
    }
    FloatRestore restore;
    restore.column = columnIdx;
    restore.width = state->strip().columns().at(columnIdx).width;
    restore.display = state->strip().columns().at(columnIdx).display;
    if (state->strip().columns().at(columnIdx).tiles.size() > 1) {
        restore.tileIndex = state->strip().columns().at(columnIdx).indexOfWindow(windowId);
    }
    state->strip().takeWindow(windowId, params);
    state->addFloating(windowId);
    m_floatRestore.insert(windowId, restore);
    m_scrollFloatedWindows.insert(windowId);
    m_lastAppliedRect.remove(windowId);
    Q_EMIT windowFloatingChanged(windowId, true, screenId.isEmpty() ? key.screenId : screenId);
    // Background-context guard: see windowClosed.
    if (key == currentKeyForScreen(key.screenId)) {
        applyLayout(key.screenId, false);
    }
    Q_EMIT placementChanged(key.screenId);
    return true;
}

bool ScrollEngine::unfloatWindowInternal(ScrollState* state, const QString& windowId, const QString& screenId,
                                         bool applyAfter)
{
    if (!state->removeFloating(windowId)) {
        return false;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    // Restore the remembered column slot (minimize/unminimize and float
    // round-trips keep their place); fall back to next-to-focus.
    const bool hadSlot = m_floatRestore.contains(windowId);
    const FloatRestore restore = m_floatRestore.take(windowId);
    bool inserted = false;
    if (hadSlot && restore.tileIndex >= 0) {
        // The window left a SHARED column: return to the surviving stack
        // (the column outlived the float since the other tiles stayed).
        // The column index can have shifted; a miss falls through to a
        // fresh column at the remembered index.
        inserted = state->strip().insertWindowIntoColumnAt(restore.column, restore.tileIndex, windowId);
    }
    if (!inserted && hadSlot) {
        inserted = state->strip().insertWindowAt(restore.column, windowId, restore.width, restore.display);
    }
    if (!inserted) {
        inserted = state->strip().insertWindow(windowId, effectiveDefaultColumnWidth(screenId),
                                               effectiveDefaultColumnDisplay(screenId), params);
    }
    if (inserted) {
        state->strip().focusWindow(windowId, params);
    }
    m_scrollFloatedWindows.remove(windowId);
    Q_EMIT windowFloatingChanged(windowId, false, screenId);
    // Batch callers (snapAllWindows) relayout once for the whole batch.
    if (applyAfter) {
        applyLayout(screenId, false);
        Q_EMIT placementChanged(screenId);
    }
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
        if (target->strip().insertWindow(windowId, effectiveDefaultColumnWidth(targetScreen),
                                         effectiveDefaultColumnDisplay(targetScreen), params)) {
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
    m_floatRestore.remove(windowId);
    // The mode-transition float marker must not outlive this engine's
    // tracking: the receiving engine owns the float bit from here, and a
    // stale entry would keep isModeSpecificFloated answering true.
    m_scrollFloatedWindows.remove(windowId);
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
        // The screen's placement changed too (managed set grew), even
        // though no strip geometry moved.
        Q_EMIT placementChanged(ctx.toScreenId);
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(ctx.toScreenId);
    ColumnWidth width = effectiveDefaultColumnWidth(ctx.toScreenId);
    if (ctx.sourceGeometry.isValid()) {
        width = ColumnWidth::makeFixed(ctx.sourceGeometry.width());
    }
    // Entry position comes from the CALLER: the cross-mode dispatcher
    // derives insertIndex from the crossing direction (0 when entering from
    // the strip's left edge), and -1 appends at the right end. This
    // function has no direction of its own to derive an edge from.
    const int columnIdx = (ctx.insertIndex >= 0) ? ctx.insertIndex : state->strip().columnCount();
    if (state->strip().insertWindowAt(columnIdx, windowId, width, effectiveDefaultColumnDisplay(ctx.toScreenId))) {
        // Seed the source engine's last-known min size so the first relayout
        // clamps correctly instead of waiting a refuse/re-discover round-trip.
        if (ctx.minSize.width() > 0 || ctx.minSize.height() > 0) {
            state->strip().setWindowMinimumSize(windowId, ctx.minSize.width(), ctx.minSize.height());
        }
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
        // COLUMN index, not window index: the restore path feeds slot.order
        // to insertWindowAt(), which takes a column position. The two only
        // coincide while every column is single-tile — a stacked column
        // would shift every later window's restore slot.
        slot.order = state->strip().columnOfWindow(windowId);
    }
    placement.engines.insert(engineId(), slot);
    return placement;
}

} // namespace PhosphorScrollEngine
