// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/NavigationContext.h>
#include <QHashFunctions>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QRect>

namespace PlasmaZones {

using NavigationContext = PhosphorEngine::NavigationContext;

using TilingStateKey = PhosphorEngine::TilingStateKey;
using PhosphorEngine::qHash;

using SnapIntent = PhosphorEngine::SnapIntent;
using ResnapEntry = PhosphorEngine::ResnapEntry;
using PendingRestore = PhosphorEngine::PendingRestore;
using SnapResult = PhosphorEngine::SnapResult;
using UnfloatResult = PhosphorEngine::UnfloatResult;
using ZoneAssignmentEntry = PhosphorEngine::ZoneAssignmentEntry;

struct PLASMAZONES_EXPORT DragInfo
{
    QString windowId;
    QRect geometry;
    QString appName;
    QString windowClass;
    QString screenId;
    bool isSticky = false;
    int virtualDesktop = 0;

    bool isValid() const
    {
        return !windowId.isEmpty();
    }
};

struct PLASMAZONES_EXPORT NavigationCommand
{
    enum class Type {
        MoveToZone,
        FocusZone,
        SwapWindows,
        PushToEmpty,
        Restore,
        ToggleFloat,
        SnapToNumber,
        Rotate
    };

    Type type = Type::MoveToZone;
    QString targetZoneId;
    QString targetWindowId;
    QString zoneGeometry;
    bool clockwise = true;

    static NavigationCommand moveToZone(const QString& zoneId, const QString& geometry)
    {
        return NavigationCommand{Type::MoveToZone, zoneId, QString(), geometry, true};
    }

    static NavigationCommand focusZone(const QString& zoneId, const QString& windowId)
    {
        return NavigationCommand{Type::FocusZone, zoneId, windowId, QString(), true};
    }

    static NavigationCommand swapWindows(const QString& zoneId, const QString& windowId, const QString& geometry)
    {
        return NavigationCommand{Type::SwapWindows, zoneId, windowId, geometry, true};
    }
};

} // namespace PlasmaZones
