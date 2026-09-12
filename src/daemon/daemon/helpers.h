// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Inline helpers shared across the daemon TU files in this directory
// (start.cpp, signals.cpp, navigation.cpp, osd.cpp, cheatsheet.cpp,
// lifecycle.cpp, the init_*.cpp trio, autotile_init.cpp).  Defined inline to avoid ODR
// issues in both unity and normal builds.

#include <QScreen>
#include "core/platform/logging.h"
#include "core/interfaces/settings_interfaces.h"
#include "core/utils/utils.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorContext/DisabledReason.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

#include <optional>

namespace PlasmaZones {

/// Run one phase of the sticky-screen pin pass on the tiling-family engines.
///
/// The phases bracket a context change and the order is not cosmetic: Release
/// run BEFORE the context moves resolves the migration's destination against
/// the context being LEFT, dropping the pinned state on that context's live
/// one and force-releasing every window it held. See
/// PhosphorEngine::StickyPinPhase.
inline void applyStickyScreenPins(WindowTrackingAdaptor* adaptor, PhosphorEngine::IPlacementEngine* autotileEngine,
                                  PhosphorEngine::IPlacementEngine* scrollEngine, PhosphorEngine::StickyPinPhase phase)
{
    // Null-guarded service, matching every other daemon ->service() consumer.
    auto* service = adaptor ? adaptor->service() : nullptr;
    if (!service) {
        return;
    }
    const auto sticky = [service](const QString& windowId) {
        return service->isWindowSticky(windowId);
    };
    // Only the tiling-family engines keep pins; the snap engine has no pin
    // concept and inherits the interface's no-op.
    if (autotileEngine) {
        autotileEngine->updateStickyScreenPins(sticky, phase);
    }
    if (scrollEngine) {
        scrollEngine->updateStickyScreenPins(sticky, phase);
    }
}

/// Build the desktop-span query the engines' per-desktop membership pass
/// takes, reading the registry that the effect keeps stamped.
///
/// Empty means "every desktop" — a sticky window — and also what an unknown
/// desktop looks like, the same reading TilingAdaptor's membership reconcile
/// uses. A span carries its full list in virtualDesktops with virtualDesktop
/// equal to the first entry, so the list wins when present.
inline PhosphorEngine::DesktopSpanQuery makeDesktopSpanQuery(PhosphorEngine::WindowRegistry* registry)
{
    return [registry](const QString& windowId) -> QSet<int> {
        QSet<int> desktops;
        if (!registry) {
            return desktops;
        }
        const auto meta = registry->metadata(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        if (!meta) {
            return desktops;
        }
        for (const int desktop : meta->virtualDesktops) {
            if (desktop > 0) {
                desktops.insert(desktop);
            }
        }
        // Falls through when the span list is absent or filtered to nothing,
        // rather than returning empty from inside the span branch — reading a
        // list of junk as "sticky" would be the wrong answer.
        if (desktops.isEmpty() && meta->virtualDesktop > 0) {
            desktops.insert(meta->virtualDesktop);
        }
        return desktops;
    };
}

/// Re-run the per-desktop membership pass for one screen on both
/// tiling-family engines. Each no-ops for a screen it does not own, so the
/// caller need not know which mode the screen is in; the snap engine has no
/// per-desktop membership and inherits the interface's no-op.
inline void reconcileMembershipsForScreen(PhosphorEngine::IPlacementEngine* autotileEngine,
                                          PhosphorEngine::IPlacementEngine* scrollEngine,
                                          PhosphorEngine::WindowRegistry* registry, const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }
    const auto spanOf = makeDesktopSpanQuery(registry);
    if (autotileEngine) {
        autotileEngine->reconcileDesktopMemberships(screenId, spanOf);
    }
    if (scrollEngine) {
        scrollEngine->reconcileDesktopMemberships(screenId, spanOf);
    }
}

/// Re-run that pass whenever a window's sticky state actually changes.
///
/// Becoming sticky changes which desktops a window belongs to without any
/// desktop switch happening, so the pass that rides the switch never runs for
/// it. Scoped to the window's own screen: stickiness is a property of the
/// window, and the other outputs' strips are unaffected.
inline void wireStickyMembershipUpdates(QObject* owner, WindowTrackingAdaptor* adaptor,
                                        PhosphorEngine::IPlacementEngine* autotileEngine,
                                        PhosphorEngine::IPlacementEngine* scrollEngine,
                                        PhosphorEngine::WindowRegistry* registry)
{
    // Straight to the service rather than through an adaptor relay: it owns
    // the sticky state and service() hands back the concrete type, so a relay
    // signal would only add a hop.
    auto* service = adaptor ? adaptor->service() : nullptr;
    if (!owner || !service) {
        return;
    }
    QObject::connect(service, &PhosphorPlacement::WindowTrackingService::windowStickyChanged, owner,
                     [autotileEngine, scrollEngine, registry](const QString& windowId, bool) {
                         // heldScreenForWindow, not wherever the window sits
                         // now: an engine that does not track it has no
                         // membership to reconcile, and the empty answer gates
                         // exactly that. Ask both — the window is on one
                         // screen in one mode, and only that engine answers.
                         QString screenId = autotileEngine ? autotileEngine->heldScreenForWindow(windowId) : QString();
                         if (screenId.isEmpty() && scrollEngine) {
                             screenId = scrollEngine->heldScreenForWindow(windowId);
                         }
                         reconcileMembershipsForScreen(autotileEngine, scrollEngine, registry, screenId);
                     });
}

/// Ask plasmashell to show its own text OSD.
///
/// Lives here rather than in osd.cpp because cheatsheet.cpp calls it too. It
/// was a static in osd.cpp's anonymous namespace, which only linked because a
/// UNITY build puts both TUs in one blob; a non-unity configure (a packager
/// build, or -DCMAKE_UNITY_BUILD=OFF) failed to resolve it. Inline here for the
/// same reason the rest of this header is: one definition that works in both
/// build modes.
inline void showKdeTextOsd(const QString& icon, const QString& text)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                                       QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));
    msg << icon << text;
    QDBusConnection::sessionBus().asyncCall(msg);
}

inline DisabledReason toDaemonDisabledReason(PhosphorContext::DisabledReason reason)
{
    switch (reason) {
    case PhosphorContext::DisabledReason::NotDisabled:
        return DisabledReason::NotDisabled;
    case PhosphorContext::DisabledReason::MonitorDisabled:
        return DisabledReason::MonitorDisabled;
    case PhosphorContext::DisabledReason::DesktopDisabled:
        return DisabledReason::DesktopDisabled;
    case PhosphorContext::DisabledReason::ActivityDisabled:
        return DisabledReason::ActivityDisabled;
    }
    Q_UNREACHABLE();
    return DisabledReason::NotDisabled;
}

/**
 * @brief Resolve a physical screen ID to a virtual screen ID if subdivisions exist.
 *
 * When the KWin effect sends a physical screen ID (e.g., during daemon reconnect
 * before virtual screen configs are loaded), the daemon must resolve it to the
 * correct virtual screen. Resolution cascade: the focused window's
 * daemon-tracked screen assignment, then the effect-reported active screen,
 * then the optional cursor-position hint, then vs:0 (leftmost).
 *
 * @p mgr is the injected ScreenManager (required — pass null only in tests
 * that don't exercise VS resolution; null is treated as "no subdivision").
 * @p cursorPos optional cursor hint; disengaged by default. No production
 * caller currently supplies one — the parameter is kept for shortcut paths
 * that have an effect-reported cursor and no focused window.
 */
inline QString resolveVirtualScreenId(PhosphorScreens::ScreenManager* mgr, const QString& physicalId,
                                      const WindowTrackingAdaptor* trackingAdaptor,
                                      const std::optional<QPoint>& cursorPos = std::nullopt)
{
    if (!mgr || !mgr->hasVirtualScreens(physicalId)) {
        return physicalId;
    }

    // Best source: the focused window's daemon-tracked screen assignment.
    // When a window is snapped to a zone, windowSnapped() stores the virtual
    // screen ID in m_windowScreenAssignments.  This is authoritative — it was
    // set at snap time by the daemon itself, not by the effect.
    if (trackingAdaptor && trackingAdaptor->service()) {
        const QString activeWindowId = trackingAdaptor->lastActiveWindowId();
        if (!activeWindowId.isEmpty()) {
            const QString trackedScreen = trackingAdaptor->service()->screenForWindow(activeWindowId);
            if (PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen) == physicalId) {
                return trackedScreen;
            }
        }
    }

    // Fallback: effect-reported active screen (may be virtual if effect has VS configs)
    if (trackingAdaptor) {
        const QString activeScreen = trackingAdaptor->lastActiveScreenName();
        if (PhosphorIdentity::VirtualScreenId::isVirtual(activeScreen)
            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(activeScreen) == physicalId) {
            return activeScreen;
        }
    }

    // Cursor position hint: when a keyboard shortcut fires with no focused window,
    // the cursor position (from the effect) can resolve the correct virtual
    // screen. std::optional, not a negative-coord sentinel — multi-monitor
    // layouts legitimately have negative coordinates, so (-1,-1) was a valid
    // position masquerading as "absent".
    if (cursorPos.has_value()) {
        const QString vsAtCursor = mgr->effectiveScreenAt(*cursorPos);
        if (PhosphorIdentity::VirtualScreenId::isVirtual(vsAtCursor)
            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(vsAtCursor) == physicalId) {
            return vsAtCursor;
        }
    }

    // Last resort: first virtual screen (vs:0)
    QStringList vsIds = mgr->virtualScreenIdsFor(physicalId);
    if (!vsIds.isEmpty()) {
        return vsIds.first();
    }
    return physicalId;
}

/**
 * @brief Resolve the screen ID for a keyboard shortcut action (virtual-screen-aware).
 *
 * Every navigation shortcut (float, move, focus, restore, swap, ...) targets
 * the focused window, so the focused window's screen is authoritative — the
 * cursor is only consulted when there is no active window (e.g. empty-desktop
 * shortcuts). Using cursor-first silently misroutes actions when the user
 * lets the mouse rest on a different virtual screen than the focused window.
 */
inline QString resolveShortcutScreenId(PhosphorScreens::ScreenManager* mgr,
                                       const WindowTrackingAdaptor* trackingAdaptor)
{
    if (!trackingAdaptor) {
        QScreen* primary = Utils::primaryScreen();
        return primary ? PhosphorScreens::ScreenIdentity::identifierFor(primary) : QString();
    }

    // Primary: focused window's screen (may already be a virtual ID from the effect)
    const QString activeScreen = trackingAdaptor->lastActiveScreenName();
    if (!activeScreen.isEmpty()) {
        if (!PhosphorIdentity::VirtualScreenId::isVirtual(activeScreen)) {
            return resolveVirtualScreenId(mgr, activeScreen, trackingAdaptor);
        }
        return activeScreen;
    }

    // Fallback: cursor screen, for shortcuts fired with no focused window
    const QString cursorScreen = trackingAdaptor->lastCursorScreenName();
    if (!cursorScreen.isEmpty()) {
        if (!PhosphorIdentity::VirtualScreenId::isVirtual(cursorScreen)) {
            return resolveVirtualScreenId(mgr, cursorScreen, trackingAdaptor);
        }
        return cursorScreen;
    }

    // Last resort: primary screen physical ID
    QScreen* primary = Utils::primaryScreen();
    return primary ? PhosphorScreens::ScreenIdentity::identifierFor(primary) : QString();
}

/**
 * @brief Resolve the screen ID for a screen-targeted shortcut (cursor-first).
 *
 * Sibling of @ref resolveShortcutScreenId for actions that target a screen
 * (per-VS layout-source toggle) rather than a window (focus, move, swap).
 * The user's intent for a screen-targeted shortcut is "the screen I am
 * looking at right now" — i.e. the cursor's screen. Routing those off the
 * focused window's screen silently misroutes the action when the focused
 * window lives on a different VS than the cursor.
 *
 * Falls back to focused-window screen, then primary screen.
 *
 * @p mgr is unused today: WindowTrackingAdaptor::cursorScreenChanged resolves
 * m_lastCursorScreenId to a virtual ID at the source whenever the physical
 * screen is split, so a physical ID from lastCursorScreenName means the
 * screen has no virtual split and IS the effective ID — no further mapping
 * needed. (resolveShortcutScreenId still re-resolves its inputs because its
 * primary source, lastActiveScreenName, arrives from the effect and may be a
 * raw physical ID on a split screen; the two helpers' assumptions differ
 * because their sources do, not because either is wrong.) Kept in the
 * signature so the call site mirrors @ref resolveShortcutScreenId and so a
 * future resolution policy that needs ScreenManager has a place to land.
 */
inline QString resolveCursorScreenId(PhosphorScreens::ScreenManager* /*mgr*/,
                                     const WindowTrackingAdaptor* trackingAdaptor)
{
    if (trackingAdaptor) {
        const QString cursorScreen = trackingAdaptor->lastCursorScreenName();
        if (!cursorScreen.isEmpty()) {
            return cursorScreen;
        }
        const QString activeScreen = trackingAdaptor->lastActiveScreenName();
        if (!activeScreen.isEmpty()) {
            return activeScreen;
        }
    }
    QScreen* primary = Utils::primaryScreen();
    return primary ? PhosphorScreens::ScreenIdentity::identifierFor(primary) : QString();
}

/**
 * @brief Convert NavigationDirection enum to string for D-Bus/engine calls
 */
inline QString navigationDirectionToString(NavigationDirection direction)
{
    switch (direction) {
    case NavigationDirection::Left:
        return QStringLiteral("left");
    case NavigationDirection::Right:
        return QStringLiteral("right");
    case NavigationDirection::Up:
        return QStringLiteral("up");
    case NavigationDirection::Down:
        return QStringLiteral("down");
    }
    Q_UNREACHABLE();
    return QString();
}

} // namespace PlasmaZones
