// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PhosphorSnapEngine {

/// What one store held for a window on an output that disconnected.
struct ParkedSnapContext
{
    PhosphorEngine::PlacementStateKey key;
    QStringList zoneIds;
    bool floating = false;
    QStringList preFloatZones;
    QString preFloatScreen;
    bool autoSnapped = false;
};

/// The snap engine's evacuee park: per disconnected physical output, per
/// canonical window, every context the window held there. Session-scoped.
struct SnapEvacueePark
{
    QHash<QString, QHash<QString, QVector<ParkedSnapContext>>> byOutput;
    /// Parked floats whose float-false the removed-screen prune must not
    /// broadcast: the daemon adopts them floating on the output they landed on.
    QSet<QString> suppressFloatFalse;
};

} // namespace PhosphorSnapEngine
