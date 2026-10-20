// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>

#include <functional>
#include <optional>

namespace PhosphorScrollEngine {

/// The scroll engine's evacuee park. Each parked context's structure is a
/// strip stash under its own key; this records which windows each parked
/// context still owes, per disconnected physical output.
struct ScrollEvacueePark
{
    QHash<QString, QHash<PhosphorEngine::PlacementStateKey, QSet<QString>>> byOutput;
    /// Windows parked floating, which re-enter floating.
    QSet<QString> floating;
    /// Each parked window's minimum size, which the release forgot.
    QHash<QString, QSize> minSizes;
    /// Windows KWin returned while their screen was not scrolling yet, per
    /// context, each with whether it was parked floating. The window's arrival
    /// in that context takes its grant (insertOpenedWindow). A float has no
    /// tile in the parked stash, so its grant carries the verdict and the
    /// arrival seats it floating where it stands. A tile's grant keeps the
    /// stash under that key parked until the tile arrives to claim it.
    QHash<PhosphorEngine::PlacementStateKey, QHash<QString, bool>> grants;

    /// Whether any parked context still owes @p windowId (canonical).
    bool owes(const QString& windowId) const;
    /// Whether a parked context under @p key still owes @p windowId.
    bool owesUnder(const PhosphorEngine::PlacementStateKey& key, const QString& windowId) const;
    /// Whether a tile granted under @p key has not arrived yet.
    bool hasTileGrant(const PhosphorEngine::PlacementStateKey& key) const;
    /// Drop @p windowId's floating and minimum-size entries once no parked
    /// context owes it.
    void forgetUnowed(const QString& windowId);
    /// Drop every parked context @p gone accepts, and the side entries of each
    /// window none still owes, and the grants under a dropped key. The stash
    /// under a dropped key is the caller's.
    void dropContextsWhere(const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& gone);
    /// Take @p windowId's grant under @p key: whether it was parked floating,
    /// or nothing when it holds no grant there.
    std::optional<bool> takeGrant(const PhosphorEngine::PlacementStateKey& key, const QString& windowId);
    /// Drop the grants under every key @p gone accepts: @p windowId's only, or
    /// every window's when it is empty.
    void dropGrantsWhere(const QString& windowId,
                         const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& gone);
};

} // namespace PhosphorScrollEngine
