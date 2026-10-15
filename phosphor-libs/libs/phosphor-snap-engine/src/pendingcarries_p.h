// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PhosphorSnapEngine {

/// A snap waiting for the activity it was moved to (F550): the zones the
/// window held on each desktop of the activity it left, put into the matching
/// zones of the destination's layout the first time its desktop shows there.
struct PendingActivityCarry
{
    QString screenId;
    QHash<int, QStringList> zonesByDesktop;
    QString fromActivity;
    QString toActivity;
};

/// A float a snapped window kept on a desktop out of view when it moved there
/// (F780). No apply goes now: the client is suspended there and drops the
/// grow. Its frame still stands on the zone it left, so the float-back is
/// re-stated the first time @c key shows, unless the frame has moved since.
struct OwedFloatBack
{
    PhosphorEngine::PlacementStateKey key;
    QRect frame;
};

/// The snap engine's carries waiting for their context to show, per canonical
/// window id. Not persisted: a restart leaves the window where it is.
struct SnapPendingCarries
{
    QHash<QString, PendingActivityCarry> byWindow;
    QHash<QString, OwedFloatBack> owedFloatBacks;
};

} // namespace PhosphorSnapEngine
