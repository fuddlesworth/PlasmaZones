// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file navigation_actions.cpp
 * @brief Snap-mode navigation entry points moved out of WindowTrackingAdaptor.
 *
 * Before the Phase 5 cleanup these methods lived on WindowTrackingAdaptor,
 * which had grown into a partial engine: target resolution, feedback
 * emission, bookkeeping, and zone-routing logic all sat in the D-Bus
 * facade class. That violated "the adaptor is a thin facade" and made
 * the daemon branch on mode at every shortcut handler.
 *
 * Navigation is now SnapEngine's concern. Every entry point takes an
 * explicit NavigationContext {windowId, screenId} from the daemon's
 * shortcut handler, so the engine no longer reaches into the WTA shadow
 * store on every call. Compositor-layer fallbacks (last-active window,
 * last-cursor screen, frame geometry) are now accessed through the typed
 * INavigationStateProvider interface rather than opaque QObject* invoke.
 *
 * Signals emitted by these methods are SnapEngine signals; SnapAdaptor
 * relays them to WindowTrackingAdaptor, which exposes them on D-Bus.
 */

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>

#include <PhosphorEngine/ICrossSurfaceResolver.h>

#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorZones/Layout.h>

#include <PhosphorWindowRule/RuleEvaluator.h>
#include <PhosphorWindowRule/WindowQuery.h>
#include <PhosphorWindowRule/WindowRuleSet.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "snapenginelogging.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorSnapEngine {

using PhosphorEngine::NavigationContext;
using PhosphorEngine::SnapIntent;
using PhosphorEngine::ZoneAssignmentEntry;

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Resolve the screen to use for a navigation operation on @p windowId.
///
/// Prefers (in order):
///   1. The stored screen assignment for the window (if currently snapped).
///      This keeps the operation on the monitor the user originally chose,
///      rather than the one KWin happens to think the window is on.
///   2. An explicit @p preferredScreen from the NavigationContext.
///   3. The INavigationStateProvider cursor / last-active screen values.
QString resolveNavScreen(INavigationStateProvider* navState, const QString& windowId,
                         PhosphorEngine::IWindowTrackingService* service, const QString& preferredScreen = QString())
{
    if (service && !windowId.isEmpty()) {
        const QString zoneId = service->zoneForWindow(windowId);
        if (!zoneId.isEmpty()) {
            const QString storedScreen = service->screenAssignments().value(windowId);
            if (!storedScreen.isEmpty()) {
                if (PhosphorIdentity::VirtualScreenId::isVirtual(storedScreen)) {
                    const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreen);
                    // Bare presence checks — the returned QScreen* is
                    // intentionally unused; the call validates that the
                    // identifier still resolves on the live screen set.
                    if (PhosphorScreens::ScreenIdentity::findByIdOrName(physId)) {
                        // `service` is already non-null per the outer
                        // guard above; no need to re-check it here.
                        auto* mgr = service->screenManager();
                        if (mgr && mgr->effectiveScreenIds().contains(storedScreen)) {
                            return storedScreen;
                        }
                    }
                } else if (PhosphorScreens::ScreenIdentity::findByIdOrName(storedScreen)) {
                    return storedScreen;
                }
            }
        }
    }
    if (!preferredScreen.isEmpty()) {
        return preferredScreen;
    }
    if (!navState) {
        return QString();
    }
    QString screen = navState->lastCursorScreenName();
    if (screen.isEmpty()) {
        screen = navState->lastActiveScreenName();
    }
    return screen;
}

/// Pick the effective window id: the explicit one from NavigationContext
/// if set, otherwise the last-active window from INavigationStateProvider.
/// Returns empty when neither is available — caller emits "no_window" feedback.
QString effectiveWindowId(const NavigationContext& ctx, INavigationStateProvider* navState)
{
    if (!ctx.windowId.isEmpty()) {
        return ctx.windowId;
    }
    return navState ? navState->lastActiveWindowId() : QString();
}

/// Pick the effective screen id: the explicit one from NavigationContext
/// if set, otherwise the last-active screen from INavigationStateProvider.
QString effectiveScreenId(const NavigationContext& ctx, INavigationStateProvider* navState)
{
    if (!ctx.screenId.isEmpty()) {
        return ctx.screenId;
    }
    return navState ? navState->lastActiveScreenName() : QString();
}

} // namespace

void SnapEngine::setExcludeRuleSet(const PhosphorWindowRule::WindowRuleSet* ruleSet)
{
    if (m_excludeRuleSet == ruleSet) {
        return;
    }
    m_excludeRuleSet = ruleSet;
    // The cached evaluator binds a reference to the previously-pointed-at
    // rule set. Dropping it forces the next isAppIdExcluded call to rebind
    // against the new pointer — a held evaluator with a stale binding would
    // resolve against the WRONG store. The evaluator's per-revision
    // internal cache key off the bound rule set's revision counter, so an
    // in-place edit to the SAME pointer needs no reset here — only the
    // pointer-changed case does.
    m_excludeEvaluator.reset();
}

bool SnapEngine::isAppIdExcluded(const QString& appId) const
{
    // No-wiring fast path: early-init can call isAppIdExcluded before the
    // daemon hands the rule store over.
    if (!m_excludeRuleSet) {
        return false;
    }
    if (m_excludeRuleSet->isEmpty()) {
        return false; // no-exclusions fast path
    }
    if (!m_excludeEvaluator) {
        m_excludeEvaluator.emplace(*m_excludeRuleSet);
    }
    PhosphorWindowRule::WindowQuery query;
    query.appId = appId;
    return m_excludeEvaluator->resolve(query).isExcluded();
}

bool SnapEngine::isWindowExcludedForAction(const QString& windowId, const QString& action, const QString& screenId)
{
    if (!m_windowTracker) {
        return false;
    }
    const QString appId = m_windowTracker->currentAppIdFor(windowId);
    if (isAppIdExcluded(appId)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << action << ":" << windowId << "excluded by rule, appId:" << appId;
        Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(), screenId);
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation entry points
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::focusInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::focusInDirection:" << direction;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("focus"));
    if (!resolver) {
        return; // ensureTargetResolver emitted feedback
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window"), QString(), QString(),
                                  ctx.screenId);
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::FocusTargetResult result = resolver->getFocusTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        return; // resolver already emitted feedback via its callback
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void SnapEngine::moveFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::moveFocusedInDirection:" << direction;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("move"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_window"), QString(), QString(),
                                  ctx.screenId);
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("move"), ctx.screenId)) {
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::MoveTargetResult result = resolver->getMoveTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        // At a zone-layout boundary with no neighbour output, the resolver
        // deferred the decision to us — try crossing to the adjacent desktop.
        if (result.reason == QLatin1String("no_adjacent_zone")) {
            if (tryCrossDesktopMove(windowId, direction, screenId)) {
                return;
            }
            // No neighbour desktop either — emit the boundary feedback the
            // resolver left to us.
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"), QString(),
                                      QString(), screenId);
        }
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::moveFocusedInDirection: invalid geometry from nav result";
        return;
    }
    commitSnap(windowId, result.zoneId, result.screenName);
    m_windowTracker->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId,
                                  result.screenName, false);
}

bool SnapEngine::tryCrossDesktopMove(const QString& windowId, const QString& direction, const QString& screenId)
{
    if (!m_crossSurfaceResolver || !m_snapState) {
        return false;
    }
    const int targetDesktop = m_crossSurfaceResolver->neighborDesktopInDirection(currentVirtualDesktop(), direction);
    if (targetDesktop <= 0) {
        return false;
    }
    // Re-stamp the window's desktop membership (keeping its snapped slot) and
    // ask the compositor to move the real window to the target desktop.
    m_snapState->reassignDesktop(windowId, targetDesktop);
    Q_EMIT windowDesktopMoveRequested(windowId, targetDesktop);
    Q_EMIT navigationFeedback(true, QStringLiteral("move"), QStringLiteral("desktop:") + direction, QString(),
                              QString(), screenId);
    return true;
}

void SnapEngine::swapFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::swapFocusedInDirection:" << direction;
    // m_snapState is set by SnapEngine's ctor as a Qt-child; the
    // `m_snapState->screenAssignments()` dereference further down would
    // otherwise be the first thing to crash if a future refactor moves
    // m_snapState ownership to a delegated setter that can leave it
    // null. Asserted unconditionally on entry (mirrors
    // toggleFocusedFloat) so the invariant fires regardless of which
    // early-return path runs below.
    Q_ASSERT(m_snapState);
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("swap"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_window"), QString(), QString(),
                                  ctx.screenId);
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("swap"), ctx.screenId)) {
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::SwapTargetResult result = resolver->getSwapTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        return;
    }
    commitSnap(result.windowId1, result.zoneId1, result.screenName);
    m_windowTracker->recordSnapIntent(result.windowId1, true);
    Q_EMIT applyGeometryRequested(result.windowId1, result.x1, result.y1, result.w1, result.h1, result.zoneId1,
                                  result.screenName, false);

    if (!result.windowId2.isEmpty()) {
        QString screen2 = m_snapState->screenAssignments().value(result.windowId2);
        if (screen2.isEmpty()) {
            screen2 = result.screenName;
        }
        commitSnap(result.windowId2, result.zoneId2, screen2);
        m_windowTracker->recordSnapIntent(result.windowId2, true);
        Q_EMIT applyGeometryRequested(result.windowId2, result.x2, result.y2, result.w2, result.h2, result.zoneId2,
                                      screen2, false);
    }
}

void SnapEngine::moveFocusedToPosition(int zoneNumber, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "SnapEngine::moveFocusedToPosition: zone" << zoneNumber << "screen" << ctx.screenId;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::moveFocusedToPosition: invalid zone number" << zoneNumber;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("snap"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("snap"), ctx.screenId)) {
        return;
    }
    const QString effectiveScreen = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::MoveTargetResult result =
        resolver->getSnapToZoneByNumberTarget(windowId, zoneNumber, effectiveScreen);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::moveFocusedToPosition: invalid geometry from nav result";
        return;
    }
    commitSnap(windowId, result.zoneId, effectiveScreen);
    m_windowTracker->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId, effectiveScreen,
                                  false);
}

void SnapEngine::pushFocusedToEmptyZone(const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::pushFocusedToEmptyZone: screen" << ctx.screenId;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("push"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("push"), ctx.screenId)) {
        return;
    }
    const QString effectiveScreen = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::MoveTargetResult result = resolver->getPushTargetForWindow(windowId, effectiveScreen);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::pushFocusedToEmptyZone: invalid geometry from nav result";
        return;
    }
    commitSnap(windowId, result.zoneId, effectiveScreen);
    m_windowTracker->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId, effectiveScreen,
                                  false);
}

void SnapEngine::restoreFocusedWindow(const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::restoreFocusedWindow";
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("restore"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::RestoreTargetResult result = resolver->getRestoreForWindow(windowId, screenId);
    if (!result.success) {
        return;
    }
    uncommitSnap(windowId);
    if (m_windowTracker) {
        m_windowTracker->clearFreeGeometry(windowId);
    }
    Q_EMIT applyGeometryRequested(windowId, result.x, result.y, result.width, result.height, QString(), screenId,
                                  false);
}

void SnapEngine::toggleFocusedFloat(const NavigationContext& ctx)
{
    Q_ASSERT(m_snapState);
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::toggleFocusedFloat";
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    const QString screenId = effectiveScreenId(ctx, m_navState);
    if (windowId.isEmpty() || screenId.isEmpty()) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "toggleFocusedFloat: no active window in context";
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_active_window"), QString(),
                                  QString(), screenId);
        return;
    }

    // Pre-tile capture semantics (from the historical WTA::toggleWindowFloat
    // implementation): when the window is CURRENTLY floating, its live frame
    // geometry is a valid free-float position and we capture it so the next
    // un-float restores to the user's most recent floated location. When the
    // window is snapped/tiled, the live shadow holds the zone rect — storing
    // it would poison the pre-tile entry with tile coordinates, so we leave
    // whatever's already stored untouched.
    if (m_navState && m_snapState->isFloating(windowId)) {
        QRect geo = m_navState->frameGeometry(windowId);
        if (geo.isValid() && m_windowTracker) {
            // Single float-back store: the unified record's shared free geometry.
            m_windowTracker->recordFreeGeometry(windowId, screenId, geo, /*overwrite=*/true);
        }
    }

    // Dispatch to the IPlacementEngine toggle path (SnapEngine::toggleWindowFloat
    // lives in snapengine/float.cpp). No need to route through WTA —
    // the router already ensured this screen is snap-mode.
    toggleWindowFloat(windowId, screenId);
}

void SnapEngine::cycleFocus(bool forward, const NavigationContext& ctx)
{
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::cycleFocus: forward=" << forward;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("cycle"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_navState);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_navState));
        return;
    }
    const QString screenId = resolveNavScreen(m_navState, windowId, m_windowTracker, ctx.screenId);
    PhosphorProtocol::CycleTargetResult result = resolver->getCycleTargetForWindow(windowId, forward, screenId);
    if (!result.success) {
        return;
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void SnapEngine::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    qCDebug(PhosphorSnapEngine::lcSnapEngine)
        << "SnapEngine::rotateWindowsInLayout: clockwise=" << clockwise << "screen=" << screenId;
    if (!m_windowTracker || !m_layoutManager) {
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), screenId);
        return;
    }
    QVector<ZoneAssignmentEntry> entries = calculateRotation(clockwise, screenId);
    if (entries.isEmpty()) {
        auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_active_layout"), QString(),
                                      QString(), screenId);
        } else if (layout->zoneCount() < 2) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("single_zone"), QString(),
                                      QString(), screenId);
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_snapped_windows"), QString(),
                                      QString(), screenId);
        }
        return;
    }

    // Apply the batch through WTS's unified helper. UserInitiated intent
    // preserves historical rotate semantics (each window's snap updates
    // last-used-zone). The fallback resolver queries the compositor-layer
    // cursor/active-screen shadows via INavigationStateProvider — only
    // used when none of the built-in strategies (targetScreenId /
    // geometry.center() / QGuiApplication::screens()) yield a screen.
    PhosphorProtocol::WindowGeometryList geometries =
        applyBatchAssignments(entries, SnapIntent::UserInitiated, [this]() -> QString {
            if (!m_navState) {
                return QString();
            }
            QString cursor = m_navState->lastCursorScreenName();
            if (cursor.isEmpty()) {
                cursor = m_navState->lastActiveScreenName();
            }
            return cursor;
        });
    if (!geometries.isEmpty()) {
        Q_EMIT applyGeometriesBatch(geometries, QStringLiteral("rotate"));
    }

    const QString direction = clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise");
    const QString reason = QStringLiteral("%1:%2").arg(direction).arg(entries.size());
    Q_EMIT navigationFeedback(true, QStringLiteral("rotate"), reason, entries.first().sourceZoneId,
                              entries.first().targetZoneId, screenId);
}

// Note: resnapToNewLayout() and resnapCurrentAssignments(const QString&)
// live in snapengine/navigation.cpp. They existed before this file and use
// the emitBatchedResnap → resnapToNewLayoutRequested → WTA::handleBatchedResnap
// pipeline, which remains the canonical batch-resnap path.

} // namespace PhosphorSnapEngine
