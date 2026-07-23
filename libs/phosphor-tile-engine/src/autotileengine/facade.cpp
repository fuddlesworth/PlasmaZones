// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "engine_internal.h"

namespace PhosphorTileEngine {

bool AutotileEngine::warnIfEmptyWindowId(const QString& windowId, const char* operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << operation << "called with empty windowId";
        return false;
    }
    return true;
}

void AutotileEngine::setWindowRegistry(QObject* registry)
{
    m_windowRegistry = dynamic_cast<PhosphorEngine::IWindowRegistry*>(registry);
    if (!m_windowRegistry) {
        return;
    }
    auto resolver = [this](const QString& windowId) {
        return currentAppIdFor(windowId);
    };
    auto* algoRegistry = m_algorithmRegistry;
    if (!algoRegistry) {
        return;
    }
    for (PhosphorTiles::TilingAlgorithm* algo : algoRegistry->allAlgorithms()) {
        if (algo) {
            algo->setAppIdResolver(resolver);
        }
    }
    connect(algoRegistry, &PhosphorTiles::ITileAlgorithmRegistry::algorithmRegistered, this,
            [this, resolver](const QString& id) {
                auto* reg = m_algorithmRegistry;
                if (!reg) {
                    return;
                }
                if (auto* algo = reg->algorithm(id)) {
                    algo->setAppIdResolver(resolver);
                }
            });
}

QString AutotileEngine::canonicalizeWindowId(const QString& rawWindowId)
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    // When a registry is attached (production daemon), delegate so every
    // service in the daemon agrees on the same canonical form for a given
    // instance id. Unit tests construct the engine without a registry and
    // fall back to a local map to keep the canonicalization invariant.
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeWindowId(rawWindowId);
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(rawWindowId);
    auto it = m_canonicalByInstance.constFind(instanceId);
    if (it != m_canonicalByInstance.constEnd()) {
        return it.value();
    }
    m_canonicalByInstance.insert(instanceId, rawWindowId);
    return rawWindowId;
}

void AutotileEngine::cleanupCanonical(const QString& anyWindowId)
{
    if (anyWindowId.isEmpty()) {
        return;
    }
    // Do NOT release the registry-owned canonical map here: the registry is
    // shared across services and only the compositor bridge's close path
    // (via WindowTrackingAdaptor::windowClosed) is authorized to release it.
    // Other services might still resolve this instance id after the engine
    // has cleaned up its own state.
    if (m_windowRegistry) {
        return;
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(anyWindowId);
    m_canonicalByInstance.remove(instanceId);
}

QString AutotileEngine::canonicalizeForLookup(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    if (m_windowRegistry) {
        return m_windowRegistry->canonicalizeForLookup(rawWindowId);
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(rawWindowId);
    auto it = m_canonicalByInstance.constFind(instanceId);
    return (it != m_canonicalByInstance.constEnd()) ? it.value() : rawWindowId;
}

QString AutotileEngine::currentAppIdFor(const QString& anyWindowId) const
{
    if (anyWindowId.isEmpty()) {
        return QString();
    }
    if (m_windowRegistry) {
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(anyWindowId);
        const QString fromRegistry = m_windowRegistry->appIdFor(instanceId);
        if (!fromRegistry.isEmpty()) {
            return fromRegistry;
        }
    }
    // Fallback: parse the string. Note this returns the FIRST-seen class for
    // canonical ids; accurate only when the window has never renamed.
    return PhosphorIdentity::WindowId::extractAppId(anyWindowId);
}

bool AutotileEngine::cleanupPendingOrderIfResolved(const QString& screenId)
{
    auto pit = m_pendingInitialOrders.find(screenId);
    if (pit == m_pendingInitialOrders.end()) {
        return false;
    }

    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return false;
    }

    for (const QString& pendingWin : std::as_const(pit.value())) {
        if (!state->containsWindow(pendingWin)) {
            return false;
        }
    }

    qCDebug(PhosphorTileEngine::lcTileEngine) << "All pre-seeded windows resolved for screen" << screenId;
    m_pendingInitialOrders.erase(pit);
    m_pendingOrderGeneration.remove(screenId);
    m_strictInitialOrderScreens.remove(screenId);
    return true;
}

PhosphorTiles::TilingState* AutotileEngine::stateForWindow(const QString& windowId, QString* outScreenId)
{
    auto it = m_states.windowKeys().constFind(windowId);
    if (it == m_states.windowKeys().constEnd() || it->screenId.isEmpty()) {
        if (outScreenId) {
            outScreenId->clear();
        }
        return nullptr;
    }

    if (outScreenId) {
        *outScreenId = it->screenId;
    }
    // Use the stored key directly — this returns the state that owns the window,
    // even if the current desktop/activity has changed since the window was added.
    return m_states.stateForKey(*it);
}

void AutotileEngine::setInnerGap(int gap)
{
    gap = std::clamp(gap, PhosphorTiles::AutotileDefaults::MinGap, PhosphorTiles::AutotileDefaults::MaxGap);
    if (m_config && m_config->innerGap != gap) {
        m_config->innerGap = gap;
        retile(QString());
    }
}

void AutotileEngine::setOuterGap(int gap)
{
    gap = std::clamp(gap, PhosphorTiles::AutotileDefaults::MinGap, PhosphorTiles::AutotileDefaults::MaxGap);
    if (m_config && m_config->outerGap != gap) {
        m_config->outerGap = gap;
        retile(QString());
    }
}

void AutotileEngine::setSmartGaps(bool enabled)
{
    if (m_config && m_config->smartGaps != enabled) {
        m_config->smartGaps = enabled;
        retile(QString());
    }
}

void AutotileEngine::setFocusNewWindows(bool enabled)
{
    if (m_config) {
        m_config->focusNewWindows = enabled;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — navigation overrides
//
// Each method absorbs what AutotileNavigationAdapter did: translate
// the user-intent-shaped IPlacementEngine call into the existing
// concrete AutotileEngine method with the right parameters.
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileEngine::focusInDirection(const QString& direction, const NavigationContext& ctx)
{
    // Daemon-authoritative windowId overrides the per-state focusedWindow()
    // tracker, which can drift when focus moves through floating, snapped, or
    // never-tracked windows that don't update it (same root cause as the
    // toggleFocusedFloat fix).
    m_navigation->focusInDirection(direction, QStringLiteral("focus"), canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::moveFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    // In autotile, "move in direction" is implemented as swap-with-neighbour
    // in the tiling order — the only way to move is to trade places with
    // the neighbour. OSD label "move" keeps the user-facing wording.
    m_navigation->swapFocusedInDirection(direction, QStringLiteral("move"), canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::spanFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    Q_UNUSED(direction)
    // Zone spanning is a snap-mode concept with no autotile equivalent.
    // Report it instead of absorbing the press silently: every other
    // navigation shortcut on an autotile screen produces feedback, and a
    // silent shortcut reads as broken.
    Q_EMIT navigationFeedback(false, QStringLiteral("span"), QStringLiteral("not_supported"), QString(), QString(),
                              ctx.screenId);
}

void AutotileEngine::swapFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    m_navigation->swapFocusedInDirection(direction, QStringLiteral("swap"), canonicalizeForLookup(ctx.windowId));
}

QString AutotileEngine::entryWindowForCrossing(const QString& screenId, const QString& direction) const
{
    return m_navigation->entryWindowOnScreen(screenId, direction);
}

int AutotileEngine::windowOrderIndexForWindow(const QString& screenId, const QString& windowId) const
{
    return m_navigation->windowOrderIndexOnScreen(screenId, canonicalizeForLookup(windowId));
}

void AutotileEngine::moveFocusedToPosition(int position, const NavigationContext& ctx)
{
    m_navigation->moveFocusedToPosition(position, canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    rotateWindows(clockwise, ctx.screenId);
}

void AutotileEngine::reapplyLayout(const NavigationContext& ctx)
{
    retile(ctx.screenId);
}

void AutotileEngine::reapplyManagedWindowAppearance()
{
    // Re-emit the tile geometry + borderless state for every tracked window so
    // the compositor re-applies each window's border / hidden title bar after a
    // bridge reconnect. retile() recomputes the current layout, but with the
    // same windows and layout it yields identical geometry — so no window moves;
    // it just re-drives the tile-request signal the compositor consumes to
    // re-apply chrome. Empty screenId = all autotile screens.
    retile(QString());
}

std::optional<PhosphorEngine::WindowPlacement> AutotileEngine::capturePlacement(const QString& windowId) const
{
    using PhosphorEngine::WindowPlacement;
    const QString wid = canonicalizeForLookup(windowId);
    const auto keyIt = m_states.windowKeys().constFind(wid);
    if (keyIt == m_states.windowKeys().constEnd()) {
        return std::nullopt;
    }
    const PhosphorEngine::TilingStateKey key = keyIt.value();
    PhosphorTiles::TilingState* state = m_states.stateForKey(key);
    if (!state) {
        return std::nullopt;
    }

    WindowPlacement p;
    // Canonical id, not the raw argument: every engine map is keyed on the
    // canonical form, and a record persisted under a mutated-appId alias
    // would never exact-match again (only the appId FIFO fallback rescues it).
    p.windowId = wid;
    p.appId = currentAppIdFor(windowId);
    p.screenId = key.screenId;
    p.virtualDesktop = key.desktop;
    p.activity = key.activity;

    // The slot carries only the autotile engine's STATE + slot reference (tile
    // order) — NEVER a rectangle. The shared free/float geometry is filled by the
    // capture orchestrator from the live frame, and ONLY when floating.
    //
    // A genuine float persists across mode toggles. The discriminator is the
    // OVERFLOW set, NOT the user-float marker: a window can be floating via the
    // float shortcut (marker set) OR via drag-to-float (marker NOT set — it emits
    // windowFloatingStateSynced, the passive path), and BOTH are real user floats
    // that must persist. Only an OVERFLOW float (auto-floated by the maxWindows cap)
    // is recorded as tiled so it re-tiles — and overflows again if it still doesn't
    // fit — on restore, rather than sticking as a phantom user float.
    PhosphorEngine::EngineSlot slot;
    if (state->isFloating(wid) && !m_overflow.isOverflow(wid)) {
        slot.state = WindowPlacement::stateFloating();
    } else {
        slot.state = WindowPlacement::stateTiled();
        slot.order = state->windowOrder().indexOf(wid);
    }
    p.engines.insert(engineId(), slot);
    return p;
}

void AutotileEngine::snapAllWindows(const NavigationContext& ctx)
{
    // Autotile has no distinct "snap all" — retile picks up every window
    // the engine is tracking and inserts any new ones into the layout.
    retile(ctx.screenId);
}

void AutotileEngine::toggleFocusedFloat(const NavigationContext& ctx)
{
    // Prefer the daemon-provided windowId from KWin's authoritative focus
    // tracking. The legacy toggleFocusedWindowFloat() uses state->focusedWindow()
    // which is updated only when KWin emits windowActivated for an
    // autotile-tracked window — focus moves through floating, snapped, or
    // never-tracked windows leave it stale, and the next float shortcut then
    // toggles the wrong window.
    //
    // Fall back to the legacy "find a focused state" lookup only when ctx
    // doesn't carry a windowId (some test paths and direct invocations).
    if (!ctx.windowId.isEmpty()) {
        const QString screenId = ctx.screenId.isEmpty() ? m_activeScreen : ctx.screenId;
        toggleWindowFloat(ctx.windowId, screenId);
        return;
    }
    toggleFocusedWindowFloat();
}

void AutotileEngine::cycleFocus(bool forward, const NavigationContext& ctx)
{
    const QString dir = forward ? QStringLiteral("right") : QStringLiteral("left");
    m_navigation->focusInDirection(dir, QStringLiteral("cycle"), canonicalizeForLookup(ctx.windowId));
}

void AutotileEngine::pushToEmptyZone(const NavigationContext& /*ctx*/)
{
    // Autotile has no concept of empty zones — every tracked window is
    // placed by the layout algorithm. Deliberate no-op so the shortcut
    // becomes a harmless press in autotile mode.
}

void AutotileEngine::restoreFocusedWindow(const NavigationContext& ctx)
{
    // "Restore" in autotile means pulling the focused window out of the
    // tiling layout — toggling its float state achieves exactly that.
    // Same daemon-authoritative routing as toggleFocusedFloat: prefer
    // ctx.windowId over the engine's per-state focusedWindow() tracker.
    if (!ctx.windowId.isEmpty()) {
        const QString screenId = ctx.screenId.isEmpty() ? m_activeScreen : ctx.screenId;
        toggleWindowFloat(ctx.windowId, screenId);
        return;
    }
    toggleFocusedWindowFloat();
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — state access
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngine::IPlacementState* AutotileEngine::stateForScreen(const QString& screenId)
{
    return tilingStateForScreen(screenId);
}

const PhosphorEngine::IPlacementState* AutotileEngine::stateForScreen(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return nullptr;
    }
    const TilingStateKey key = currentKeyForScreen(screenId);
    return m_states.stateForKey(key);
}

} // namespace PhosphorTileEngine
