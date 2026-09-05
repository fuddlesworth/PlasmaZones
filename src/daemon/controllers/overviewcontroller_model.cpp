// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// OverviewController::buildInputs: the bridge from the daemon's live objects
// (workspace map, VDM, screen manager, layout registry, window registry) to
// the builder's provider struct. Kept apart from the open-state and publish
// logic so the model's sourcing rules read in one place.

#include "overviewcontroller.h"

#include "workspacecontroller.h"

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/WorkspaceReconciler.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

namespace {

/// Window types the overview never draws. Docks are drawn by the effect from
/// its own dock model; desktops are the wallpaper; notifications and their
/// kin are transient chrome. Matches KWin's Overview filter.
bool omittedType(PhosphorProtocol::WindowType type)
{
    switch (type) {
    case PhosphorProtocol::WindowType::Dock:
    case PhosphorProtocol::WindowType::Desktop:
    case PhosphorProtocol::WindowType::Notification:
    case PhosphorProtocol::WindowType::OnScreenDisplay:
    case PhosphorProtocol::WindowType::Tooltip:
    case PhosphorProtocol::WindowType::Menu:
    case PhosphorProtocol::WindowType::Popup:
    case PhosphorProtocol::WindowType::Splash:
        return true;
    default:
        return false;
    }
}

QLatin1String modeName(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Snapping:
        return OverviewMode::Snapping;
    case PhosphorZones::AssignmentEntry::Autotile:
        return OverviewMode::Tiling;
    case PhosphorZones::AssignmentEntry::Scrolling:
        return OverviewMode::Scrolling;
    }
    return OverviewMode::None;
}

} // namespace

OverviewModelBuilder::Inputs OverviewController::buildInputs() const
{
    OverviewModelBuilder::Inputs in;
    in.activity = currentActivity();
    in.snapping = m_sources.snapping;
    in.tiling = m_sources.tiling;
    in.scrolling = m_sources.scrolling;
    if (!m_workspaces) {
        return in;
    }
    in.map = &m_workspaces->reconciler().map();

    for (const QString& screenId : in.map->screenOrder()) {
        in.currentDesktopByScreen.insert(screenId, m_vdm ? m_vdm->currentDesktopForScreen(screenId) : 0);
    }
    in.desktopIndexOf = [vdm = m_vdm](const QString& desktopId) {
        return vdm ? vdm->desktopIndexOf(desktopId) : 0;
    };
    in.screenGeometry = [screens = m_screens](const QString& screenId) {
        return screens ? screens->screenGeometry(screenId) : QRect();
    };
    in.virtualScreensFor = [screens = m_screens](const QString& screenId) {
        if (!screens || !screens->hasVirtualScreens(screenId)) {
            return QStringList();
        }
        return screens->virtualScreenIdsFor(screenId);
    };
    // Mode per (screen, desktop) through the registry's full cascade, NOT the
    // daemon's current-context router: every non-current workspace would
    // otherwise be labelled with the current one's mode. A context whose
    // active layout is suppressed (disabled) reports none.
    in.modeFor = [layouts = m_layouts, activity = in.activity](const QString& screenId, int desktop) {
        if (!layouts) {
            return OverviewMode::None;
        }
        if (layouts->isContextActiveLayoutSuppressed(screenId, desktop, activity)) {
            return OverviewMode::None;
        }
        return modeName(layouts->modeForScreen(screenId, desktop, activity));
    };

    if (m_registry) {
        const QStringList ids = m_registry->instanceIds();
        in.windows.reserve(ids.size());
        for (const QString& instanceId : ids) {
            const auto meta = m_registry->metadata(instanceId);
            if (!meta) {
                continue;
            }
            OverviewTrackedWindow w;
            // Engines and the effect key windows by the registry's canonical
            // composite id; the census walks by instance id.
            w.id = m_registry->canonicalizeForLookup(instanceId);
            w.omitted = omittedType(meta->windowType);
            w.sticky = meta->isSticky.value_or(false) || meta->virtualDesktops.size() > 1;
            if (m_windowStickyPredicate && m_windowStickyPredicate(w.id)) {
                w.sticky = true;
            }
            w.desktop = meta->virtualDesktop;
            w.minimized = meta->isMinimized.value_or(false);
            if (meta->positionX && meta->positionY && meta->width && meta->height) {
                w.frame = QRect(*meta->positionX, *meta->positionY, *meta->width, *meta->height);
            }
            QString screen = m_windowScreenResolver ? m_windowScreenResolver(w.id) : QString();
            if (screen.isEmpty() && m_screens && w.frame.isValid()) {
                // Untracked by every engine and the placement store: key it
                // by where its frame sits.
                screen = m_screens->physicalScreenFor(m_screens->effectiveScreenAt(w.frame.center())).identifier;
            } else if (!screen.isEmpty() && m_screens) {
                const PhosphorScreens::PhysicalScreen physical = m_screens->physicalScreenFor(screen);
                if (physical.isValid()) {
                    screen = physical.identifier;
                }
            }
            w.screenId = WorkspaceController::canonicalScreenId(screen);
            in.windows.append(w);
        }
    }
    return in;
}

} // namespace PlasmaZones
