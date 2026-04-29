// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorEngineApi/EngineTypes.h>
#include <PhosphorEngineApi/NavigationContext.h>
#include <QHashFunctions>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QRect>

namespace PlasmaZones {

using NavigationContext = PhosphorEngineApi::NavigationContext;

using TilingStateKey = PhosphorEngineApi::TilingStateKey;
using PhosphorEngineApi::qHash;

using SnapIntent = PhosphorEngineApi::SnapIntent;
using ResnapEntry = PhosphorEngineApi::ResnapEntry;
using PendingRestore = PhosphorEngineApi::PendingRestore;
using SnapResult = PhosphorEngineApi::SnapResult;
using UnfloatResult = PhosphorEngineApi::UnfloatResult;
using ZoneAssignmentEntry = PhosphorEngineApi::ZoneAssignmentEntry;

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
