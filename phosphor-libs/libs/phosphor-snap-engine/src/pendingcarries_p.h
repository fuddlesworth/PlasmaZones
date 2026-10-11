// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
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

/// The snap engine's pending activity carries, per canonical window id. Not
/// persisted: a restart leaves the window where it is.
struct SnapPendingCarries
{
    QHash<QString, PendingActivityCarry> byWindow;
};

} // namespace PhosphorSnapEngine
