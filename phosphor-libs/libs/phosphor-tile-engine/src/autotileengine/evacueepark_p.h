// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace PhosphorTileEngine {

/// One tiling context of an output that disconnected, as it stood when the
/// output went away.
struct ParkedTileContext
{
    PhosphorEngine::TilingStateKey key;
    /// windowOrder(), floats included: what a re-seat counts predecessors in.
    QStringList order;
    QSet<QString> floating;
    /// Windows not yet re-seated or dropped.
    QSet<QString> pending;
    /// Windows granted their place on a context out of view, by the key they
    /// may take it on when they arrive there.
    QHash<QString, PhosphorEngine::TilingStateKey> granted;
    /// The user's own tuning of the context, when it had one.
    std::optional<qreal> splitRatio;
    std::optional<int> masterCount;
    QJsonObject scriptState;
    QString algorithmId;
};

/// The autotile engine's evacuee park, per disconnected physical output.
struct TileEvacueePark
{
    QHash<QString, QVector<ParkedTileContext>> byOutput;
};

} // namespace PhosphorTileEngine
