// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// SnapAdaptor: the one gate a bus-requested snap of a single window passes.
// A D-Bus caller can name any window, zone and screen; a keyboard snap can
// only act where snapping runs. This decides where a bus snap lands and
// refuses it unless a keyboard snap could make the same move (F20).

#include "snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "core/interfaces/interfaces.h"
#include "core/platform/logging.h"

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>

namespace PlasmaZones {

namespace {
/// The desktop @p screenId shows: the per-output value, else the global current
/// one the layout registry holds (a WindowTrackingAdaptor with no desktop
/// manager answers 0, "unknown").
int shownDesktop(WindowTrackingAdaptor* adaptor, const QString& screenId)
{
    const int perOutput = adaptor->currentDesktopForScreen(screenId);
    if (perOutput >= 1) {
        return perOutput;
    }
    PhosphorZones::LayoutRegistry* const layouts = adaptor->layoutRegistry();
    return layouts ? layouts->currentVirtualDesktop() : 0;
}
} // namespace

std::optional<int> SnapAdaptor::landingDesktop(const QString& windowId, const QString& screenId,
                                               int pinnedDesktop) const
{
    const int shown = shownDesktop(m_adaptor, screenId);
    const auto context = m_adaptor->windowDesktopContext(windowId);
    const std::optional<QSet<int>> set = context ? context->desktopSet() : std::nullopt;
    if (pinnedDesktop >= 1) {
        if (set && !set->contains(pinnedDesktop)) {
            return std::nullopt;
        }
        return pinnedDesktop == shown ? 0 : pinnedDesktop;
    }
    if (!set || shown < 1 || set->contains(shown)) {
        return 0;
    }
    if (set->size() == 1) {
        return *set->cbegin();
    }
    // On several desktops none of which is in view: which one the snap is
    // for cannot be told (user decision l13_bus_snap_hidden_multidesktop).
    return std::nullopt;
}

std::optional<SnapAdaptor::BusSnapTarget> SnapAdaptor::validBusSnapTarget(const QString& windowId,
                                                                          const QStringList& zoneIds,
                                                                          const QString& screenHint,
                                                                          int pinnedDesktop) const
{
    const auto refuse = [&windowId](const char* reason, const QString& screen = QString()) {
        qCInfo(lcDbusWindow) << "Bus snap of" << windowId << "refused:" << reason << screen;
        return std::optional<BusSnapTarget>();
    };
    if (!m_engine || !m_adaptor || !m_adaptor->service()) {
        return std::nullopt;
    }
    if (!m_adaptor->isRegistryTracked(windowId)) {
        return refuse("not a live window"); // F142
    }
    if (zoneIds.isEmpty() || std::any_of(zoneIds.cbegin(), zoneIds.cend(), [](const QString& id) {
            return id.isEmpty() || id == PhosphorEngine::RestoreSentinel;
        })) {
        return refuse("no zone");
    }
    PhosphorZones::LayoutRegistry* const layouts = m_adaptor->layoutRegistry();
    const QString activity = layouts ? layouts->currentActivity() : QString();
    const auto holds = [&](const QString& screen, int desktop) {
        return PhosphorZones::LayoutUtils::contextLayoutHoldsZones(
            layouts, screen, desktop >= 1 ? desktop : shownDesktop(m_adaptor, screen), activity, zoneIds);
    };
    QString screen;
    if (!screenHint.isEmpty()) {
        screen = m_adaptor->resolveBusScreen(screenHint, windowId);
        if (screen.isEmpty()) {
            return refuse("unknown screen", screenHint); // F81
        }
    } else {
        // The window's own screen when its layout holds the zone, so one
        // layout on two screens lands where the window is (F21).
        const QString own = m_adaptor->liveScreenForWindow(windowId);
        const std::optional<int> ownDesktop =
            own.isEmpty() ? std::nullopt : landingDesktop(windowId, own, pinnedDesktop);
        if (ownDesktop && holds(own, *ownDesktop)) {
            screen = own;
        } else {
            screen = m_adaptor->resolveBusScreen(m_adaptor->resolveScreenForSnap(QString(), zoneIds.first()), windowId);
        }
        if (screen.isEmpty()) {
            return refuse("no screen for the zone");
        }
    }
    const std::optional<int> desktop = landingDesktop(windowId, screen, pinnedDesktop);
    if (!desktop) {
        return refuse("not on the landing desktop", screen); // F179
    }
    // A commit pins the current activity's store.
    if (const auto context = m_adaptor->windowDesktopContext(windowId);
        context && !context->activity.isEmpty() && !activity.isEmpty() && context->activity != activity) {
        return refuse("on another activity", screen);
    }
    if (!holds(screen, *desktop)) {
        return refuse("the landing layout lacks the zone", screen); // F179, F459
    }
    return BusSnapTarget{screen, *desktop};
}

std::optional<SnapAdaptor::BusSnapTarget> SnapAdaptor::admitBusSnap(const QString& windowId, const QStringList& zoneIds,
                                                                    const QString& screenHint, int pinnedDesktop) const
{
    const std::optional<BusSnapTarget> target = validBusSnapTarget(windowId, zoneIds, screenHint, pinnedDesktop);
    if (!target) {
        return std::nullopt;
    }
    // What the keyboard twin applies to a user verb.
    if (!m_engine->isActiveOnScreen(target->screenId)) {
        qCInfo(lcDbusWindow) << "Bus snap of" << windowId << "refused: snapping does not run" << target->screenId;
        return std::nullopt;
    }
    if (!snapPermittedForContext(windowId, target->screenId, target->desktop)) {
        return std::nullopt;
    }
    if (m_engine->isWindowExcluded(windowId, target->screenId)) {
        qCInfo(lcDbusWindow) << "Bus snap of" << windowId << "refused: excluded on" << target->screenId;
        return std::nullopt;
    }
    return target;
}

bool SnapAdaptor::focusedVerbPermitted(const QString& screenHint, bool placesWindow) const
{
    if (!m_engine || !m_adaptor) {
        return false;
    }
    // Mirrors navigatorForShortcut and isFocusedContextGated: focus and cycle
    // place nothing, so they skip the context gate as handleFocus and
    // handleCycle do.
    if (m_settings && !m_settings->snappingEnabled()) {
        qCInfo(lcDbusWindow) << "Bus verb refused: snapping is switched off";
        return false;
    }
    const QString resolvedHint = screenHint.isEmpty() ? QString() : m_adaptor->resolveBusScreen(screenHint);
    if (!screenHint.isEmpty() && resolvedHint.isEmpty()) {
        qCInfo(lcDbusWindow) << "Bus verb refused: unknown screen" << screenHint;
        return false;
    }
    const QString windowId = m_adaptor->lastActiveWindowId();
    const QString screen = m_engine->navigationScreenFor(windowId, resolvedHint);
    if (screen.isEmpty()) {
        return true; // the engine reports no_window itself
    }
    if (!m_engine->isActiveOnScreen(screen)) {
        qCInfo(lcDbusWindow) << "Bus verb refused: snapping does not run" << screen;
        return false;
    }
    return !placesWindow || snapPermittedForContext(windowId, screen, 0);
}

void SnapAdaptor::moveWindowToZoneNumberOnScreen(const QString& windowId, int zoneNumber, const QString& screenHint)
{
    if (!m_adaptor || !m_adaptor->service() || windowId.isEmpty() || zoneNumber < 1) {
        return;
    }
    const QString screen = screenHint.isEmpty() ? m_adaptor->liveScreenForWindow(windowId)
                                                : m_adaptor->resolveBusScreen(screenHint, windowId);
    if (screen.isEmpty()) {
        qCInfo(lcDbusWindow) << "Bus snap of" << windowId << "refused: no screen for" << screenHint;
        return;
    }
    const std::optional<int> desktop = landingDesktop(windowId, screen, 0);
    if (!desktop) {
        qCInfo(lcDbusWindow) << "Bus snap of" << windowId << "refused: not on the landing desktop" << screen;
        return;
    }
    PhosphorZones::LayoutRegistry* const layouts = m_adaptor->layoutRegistry();
    PhosphorZones::Layout* const layout = layouts
        ? layouts->layoutForScreen(screen, *desktop >= 1 ? *desktop : shownDesktop(m_adaptor, screen),
                                   layouts->currentActivity())
        : nullptr;
    PhosphorZones::Zone* const zone = layout ? layout->zoneByNumber(zoneNumber) : nullptr;
    if (!zone) {
        Q_EMIT m_adaptor->navigationFeedback(false, QStringLiteral("snap"),
                                             layout ? QStringLiteral("zone_not_found")
                                                    : QStringLiteral("no_active_layout"),
                                             QString(), QString(), screen);
        return;
    }
    moveWindowToZoneOnScreen(windowId, zone->id().toString(), screen);
}

} // namespace PlasmaZones
