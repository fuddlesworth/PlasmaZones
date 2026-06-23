// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/WindowTypes.h>
#include <PhosphorProtocol/phosphorprotocoltypes_export.h>

#include <QList>
#include <QMetaType>
#include <QRect>
#include <QString>

namespace PhosphorProtocol {

/// D-Bus struct for snap-all result entries: (sssiiii)
/// Carries targetZoneId so the plugin can confirm snaps without a second JSON parse.
struct SnapAllResultEntry
{
    QString windowId;
    QString targetZoneId;
    QString sourceZoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
    WindowGeometryEntry toGeometryEntry() const
    {
        return {windowId, x, y, width, height, QString()};
    }
};

using SnapAllResultList = QList<SnapAllResultEntry>;

/// D-Bus struct for move/push/zone-number navigation result: (bssiiiiss)
struct MoveTargetResult
{
    bool success = false;
    QString reason;
    QString zoneId;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    QString sourceZoneId;
    QString screenName;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

/// D-Bus struct for focus navigation result: (bsssss)
struct FocusTargetResult
{
    bool success = false;
    QString reason;
    QString windowIdToActivate;
    QString sourceZoneId;
    QString targetZoneId;
    QString screenName;
};

/// D-Bus struct for cycle navigation result: (bssss)
struct CycleTargetResult
{
    bool success = false;
    QString reason;
    QString windowIdToActivate;
    QString zoneId;
    QString screenName;
};

/// D-Bus struct for swap navigation result: (bssiiiissiiiisssss)
struct SwapTargetResult
{
    bool success = false;
    QString reason;
    QString windowId1;
    int x1 = 0;
    int y1 = 0;
    int w1 = 0;
    int h1 = 0;
    QString zoneId1;
    QString windowId2;
    int x2 = 0;
    int y2 = 0;
    int w2 = 0;
    int h2 = 0;
    QString zoneId2;
    QString screenName;
    QString sourceZoneId;
    QString targetZoneId;
    // Per-window target screen for window2. Empty for in-surface swaps (both
    // windows share screenName); set only on a cross-output swap, where window1
    // crosses to the neighbour output (screenName) and window2 returns to the
    // source output (screenName2). Appended last so the aggregate swapResult()
    // helper and its existing call sites keep compiling via default-init.
    QString screenName2;
};

/// D-Bus struct for restore navigation result: (bbiiii)
struct RestoreTargetResult
{
    bool success = false;
    bool found = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    QRect toRect() const
    {
        return QRect(x, y, width, height);
    }
};

} // namespace PhosphorProtocol

Q_DECLARE_METATYPE(PhosphorProtocol::SnapAllResultEntry)
Q_DECLARE_METATYPE(PhosphorProtocol::SnapAllResultList)
Q_DECLARE_METATYPE(PhosphorProtocol::MoveTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::FocusTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::CycleTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::SwapTargetResult)
Q_DECLARE_METATYPE(PhosphorProtocol::RestoreTargetResult)
