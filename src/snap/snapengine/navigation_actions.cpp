// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
 * store on every call. The back-reference to WTA is retained for last-
 * resort fallback (when the daemon can't resolve a target), for the
 * frame-geometry shadow used by the float pre-tile capture, and for
 * the resnap fallback-screen helper in applyBatchAssignments.
 *
 * Signals emitted by these methods are SnapEngine signals; SnapAdaptor
 * relays them to WindowTrackingAdaptor, which exposes them on D-Bus.
 */

#include "../SnapEngine.h"
#include <PhosphorZones/SnapState.h>

#include "../../config/settings.h"
#include "../../core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "../../core/logging.h"
#include <PhosphorScreens/Manager.h>
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../../core/windowtrackingservice.h"
#include "../../dbus/snapnavigationtargets.h"
#include "../../dbus/windowtrackingadaptor.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

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
///   3. The WTA cursor / last-active screen shadows.
QString resolveNavScreen(const WindowTrackingAdaptor* wta, const QString& windowId, WindowTrackingService* service,
                         const QString& preferredScreen = QString())
{
    if (service && !windowId.isEmpty()) {
        const QString zoneId = service->zoneForWindow(windowId);
        if (!zoneId.isEmpty()) {
            const QString storedScreen = service->screenAssignments().value(windowId);
            if (!storedScreen.isEmpty()) {
                if (PhosphorIdentity::VirtualScreenId::isVirtual(storedScreen)) {
                    const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreen);
                    QScreen* physScreen = Phosphor::Screens::ScreenIdentity::findByIdOrName(physId);
                    if (physScreen) {
                        auto* mgr = service ? service->screenManager() : nullptr;
                        if (mgr && mgr->effectiveScreenIds().contains(storedScreen)) {
                            return storedScreen;
                        }
                    }
                } else if (Phosphor::Screens::ScreenIdentity::findByIdOrName(storedScreen)) {
                    return storedScreen;
                }
            }
        }
    }
    if (!preferredScreen.isEmpty()) {
        return preferredScreen;
    }
    if (!wta) {
        return QString();
    }
    QString screen = wta->lastCursorScreenName();
    if (screen.isEmpty()) {
        screen = wta->lastActiveScreenName();
    }
    return screen;
}

/// Pick the effective window id: the explicit one from NavigationContext
/// if set, otherwise the last-active shadow on WTA. Returns empty when
/// neither is available — caller emits "no_window" feedback.
QString effectiveWindowId(const NavigationContext& ctx, const WindowTrackingAdaptor* wta)
{
    if (!ctx.windowId.isEmpty()) {
        return ctx.windowId;
    }
    return wta ? wta->lastActiveWindowId() : QString();
}

/// Pick the effective screen id: the explicit one from NavigationContext
/// if set, otherwise the last-active screen shadow on WTA.
QString effectiveScreenId(const NavigationContext& ctx, const WindowTrackingAdaptor* wta)
{
    if (!ctx.screenId.isEmpty()) {
        return ctx.screenId;
    }
    return wta ? wta->lastActiveScreenName() : QString();
}

} // namespace

bool SnapEngine::isWindowExcludedForAction(const QString& windowId, const QString& action, const QString& screenId)
{
    if (!m_settings || !m_windowTracker) {
        return false;
    }
    const QString appId = m_windowTracker->currentAppIdFor(windowId);
    for (const QString& excluded : m_settings->excludedApplications()) {
        if (PhosphorIdentity::WindowId::appIdMatches(appId, excluded)) {
            qCInfo(lcCore) << action << ":" << windowId << "excluded by app rule:" << excluded;
            Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(), screenId);
            return true;
        }
    }
    for (const QString& excluded : m_settings->excludedWindowClasses()) {
        if (PhosphorIdentity::WindowId::appIdMatches(appId, excluded)) {
            qCInfo(lcCore) << action << ":" << windowId << "excluded by class rule:" << excluded;
            Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(), screenId);
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation entry points
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::focusInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(lcCore) << "SnapEngine::focusInDirection:" << direction;
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
    const QString windowId = effectiveWindowId(ctx, m_wta);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window"), QString(), QString(),
                                  ctx.screenId);
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker, ctx.screenId);
    FocusTargetResult result = resolver->getFocusTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        return; // resolver already emitted feedback via its callback
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void SnapEngine::moveFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(lcCore) << "SnapEngine::moveFocusedInDirection:" << direction;
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
    const QString windowId = effectiveWindowId(ctx, m_wta);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_window"), QString(), QString(),
                                  ctx.screenId);
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("move"), ctx.screenId)) {
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker, ctx.screenId);
    MoveTargetResult result = resolver->getMoveTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcCore) << "SnapEngine::moveFocusedInDirection: invalid geometry from nav result";
        return;
    }
    commitSnap(windowId, result.zoneId, result.screenName);
    m_windowTracker->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId,
                                  result.screenName, false);
}

void SnapEngine::swapFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    qCInfo(lcCore) << "SnapEngine::swapFocusedInDirection:" << direction;
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
    const QString windowId = effectiveWindowId(ctx, m_wta);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_window"), QString(), QString(),
                                  ctx.screenId);
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("swap"), ctx.screenId)) {
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker, ctx.screenId);
    SwapTargetResult result = resolver->getSwapTargetForWindow(windowId, direction, screenId);
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
    qCInfo(lcCore) << "SnapEngine::moveFocusedToPosition: zone" << zoneNumber << "screen" << ctx.screenId;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcCore) << "SnapEngine::moveFocusedToPosition: invalid zone number" << zoneNumber;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("snap"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_wta);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_wta));
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("snap"), ctx.screenId)) {
        return;
    }
    const QString effectiveScreen = resolveNavScreen(m_wta, windowId, m_windowTracker, ctx.screenId);
    MoveTargetResult result = resolver->getSnapToZoneByNumberTarget(windowId, zoneNumber, effectiveScreen);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcCore) << "SnapEngine::moveFocusedToPosition: invalid geometry from nav result";
        return;
    }
    commitSnap(windowId, result.zoneId, effectiveScreen);
    m_windowTracker->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId, effectiveScreen,
                                  false);
}

void SnapEngine::pushFocusedToEmptyZone(const NavigationContext& ctx)
{
    qCInfo(lcCore) << "SnapEngine::pushFocusedToEmptyZone: screen" << ctx.screenId;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("push"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_wta);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_wta));
        return;
    }
    if (isWindowExcludedForAction(windowId, QStringLiteral("push"), ctx.screenId)) {
        return;
    }
    const QString effectiveScreen = resolveNavScreen(m_wta, windowId, m_windowTracker, ctx.screenId);
    MoveTargetResult result = resolver->getPushTargetForWindow(windowId, effectiveScreen);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcCore) << "SnapEngine::pushFocusedToEmptyZone: invalid geometry from nav result";
        return;
    }
    commitSnap(windowId, result.zoneId, effectiveScreen);
    m_windowTracker->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId, effectiveScreen,
                                  false);
}

void SnapEngine::restoreFocusedWindow(const NavigationContext& ctx)
{
    qCInfo(lcCore) << "SnapEngine::restoreFocusedWindow";
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("restore"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_wta);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_wta));
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker, ctx.screenId);
    RestoreTargetResult result = resolver->getRestoreForWindow(windowId, screenId);
    if (!result.success) {
        return;
    }
    uncommitSnap(windowId);
    clearUnmanagedGeometry(windowId);
    Q_EMIT applyGeometryRequested(windowId, result.x, result.y, result.width, result.height, QString(), screenId,
                                  false);
}

void SnapEngine::toggleFocusedFloat(const NavigationContext& ctx)
{
    qCInfo(lcCore) << "SnapEngine::toggleFocusedFloat";
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_wta);
    const QString screenId = effectiveScreenId(ctx, m_wta);
    if (windowId.isEmpty() || screenId.isEmpty()) {
        qCInfo(lcCore) << "toggleFocusedFloat: no active window in context";
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
    //
    // The frame-geometry shadow is the one compositor-layer piece of state
    // still living on WTA. Reading it here is the last remaining behavior
    // coupling to the adaptor in this file.
    if (m_wta && m_snapState->isFloating(windowId)) {
        const QRect geo = m_wta->frameGeometry(windowId);
        if (geo.isValid()) {
            storeUnmanagedGeometry(windowId, geo, screenId, /*overwrite=*/true);
        }
    }

    // Dispatch to the IPlacementEngine toggle path (SnapEngine::toggleWindowFloat
    // lives in snapengine/float.cpp). No need to route through WTA —
    // the router already ensured this screen is snap-mode.
    toggleWindowFloat(windowId, screenId);
}

void SnapEngine::cycleFocus(bool forward, const NavigationContext& ctx)
{
    qCInfo(lcCore) << "SnapEngine::cycleFocus: forward=" << forward;
    if (!m_windowTracker) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("engine_unavailable"), QString(),
                                  QString(), ctx.screenId);
        return;
    }
    auto* resolver = ensureTargetResolver(QStringLiteral("cycle"));
    if (!resolver) {
        return;
    }
    const QString windowId = effectiveWindowId(ctx, m_wta);
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("no_window"), QString(), QString(),
                                  effectiveScreenId(ctx, m_wta));
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker, ctx.screenId);
    CycleTargetResult result = resolver->getCycleTargetForWindow(windowId, forward, screenId);
    if (!result.success) {
        return;
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void SnapEngine::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    qCDebug(lcCore) << "SnapEngine::rotateWindowsInLayout: clockwise=" << clockwise << "screen=" << screenId;
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
    // preserves historical rotate semantics (each windows snap updates
    // last-used-zone). The fallback resolver is the compositor-layer
    // cursor/active-window shadow on WTA — only used when none of the
    // built-in strategies (targetScreenId / geometry.center() /
    // QGuiApplication::screens()) yield a screen.
    const QPointer<WindowTrackingAdaptor> wta = m_wta;
    WindowGeometryList geometries = applyBatchAssignments(entries, SnapIntent::UserInitiated, [wta]() -> QString {
        if (!wta) {
            return QString();
        }
        const QString cursor = wta->lastCursorScreenName();
        return cursor.isEmpty() ? wta->lastActiveScreenName() : cursor;
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

} // namespace PlasmaZones
