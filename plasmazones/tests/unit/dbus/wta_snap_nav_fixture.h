// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file wta_snap_nav_fixture.h
 * @brief The float-back fixture with what the snap keyboard verbs need: a
 *        virtual desktop manager, the zone adjacency of a real
 *        ZoneDetectionAdaptor and a scripted neighbour-surface resolver.
 */

#include "wta_float_back_fixture.h"
#include "dbus/zonedetectionadaptor.h"

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/NavigationContext.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QHash>

/// Built before FloatBackFixture (base-from-member) so the engine and the
/// adaptor it hands the manager to never outlive it.
struct VdmHolder
{
    VdmHolder()
    {
        vdm.updateScreenDesktop(kLeft, 1);
        vdm.updateScreenDesktop(kRight, 1);
    }
    PhosphorWorkspaces::VirtualDesktopManager vdm{nullptr};
};

/// Neighbour outputs keyed "screen|direction"; desktops 1..desktopCount in a
/// row, "right" stepping up and "left" stepping down.
struct NavCrossSurface : PhosphorEngine::ICrossSurfaceResolver
{
    QString neighborOutputInDirection(const QString& screenId, const QString& direction) const override
    {
        return outputs.value(screenId + QLatin1Char('|') + direction);
    }
    int neighborDesktopInDirection(int currentDesktop, const QString& direction) const override
    {
        if (direction == QLatin1String("right") && currentDesktop < desktopCount) {
            return currentDesktop + 1;
        }
        if (direction == QLatin1String("left") && currentDesktop > 1) {
            return currentDesktop - 1;
        }
        return 0;
    }
    QHash<QString, QString> outputs;
    int desktopCount = 1;
};

struct SnapNavFixture : VdmHolder, FloatBackFixture
{
    SnapNavFixture()
        : FloatBackFixture(&vdm)
    {
        zda = new ZoneDetectionAdaptor(&detector, layouts, screenMgr.get(), &settings, &parent);
        wta->setZoneDetectionAdaptor(zda);
        snap->setZoneAdjacencyResolver(zda);
        snap->setCrossSurfaceResolver(&cross);
        layouts->assignLayout(kLeft, 1, QString(), layout);
        layouts->assignLayout(kRight, 1, QString(), layout);
    }
    ~SnapNavFixture()
    {
        snap->setCrossSurfaceResolver(nullptr);
        snap->setZoneAdjacencyResolver(nullptr);
    }
    /// The rows' precondition: the zone adjacency answers on the fake outputs.
    bool ready() const
    {
        return !zda->getAdjacentZone(zone(0), QStringLiteral("right"), kLeft).isEmpty();
    }
    /// A live window (app "app") whose frame is @p frame.
    QString live(const QString& instance, const QRect& frame = QRect(100, 100, 400, 300))
    {
        const QString windowId = registerWindow(instance);
        setFrame(windowId, frame);
        return windowId;
    }
    /// @p windowId snapped into @p zones on @p screen, pinned to @p desktop.
    void snapOn(const QString& windowId, const QStringList& zones, const QString& screen, int desktop = 0)
    {
        if (zones.size() > 1) {
            snap->commitMultiZoneSnap(windowId, zones, screen, PhosphorEngine::SnapIntent::UserInitiated, desktop);
        } else {
            snap->commitSnap(windowId, zones.first(), screen, PhosphorEngine::SnapIntent::UserInitiated, desktop);
        }
    }
    /// @p screen shows desktop @p desktop.
    void showDesktop(const QString& screen, int desktop)
    {
        vdm.updateScreenDesktop(screen, desktop);
        snap->setCurrentDesktopForScreen(screen, desktop);
    }
    /// The mode @p screen runs on @p desktop (the layout registry's cascade).
    void setMode(const QString& screen, int desktop, PhosphorZones::AssignmentEntry::Mode mode)
    {
        PhosphorZones::AssignmentEntry entry;
        entry.mode = mode;
        entry.snappingLayout = layout->id().toString();
        if (mode == PhosphorZones::AssignmentEntry::Autotile) {
            entry.tilingAlgorithm = QStringLiteral("bsp");
        }
        layouts->setAssignmentEntryDirect(screen, desktop, QString(), entry);
    }
    /// The keyboard verbs act on this window (the WTA's last active one).
    void focus(const QString& windowId, const QString& screen)
    {
        wta->windowActivated(windowId, screen);
    }

    ZoneDetectionAdaptor* zda = nullptr; // parent-owned
    NavCrossSurface cross;
};
