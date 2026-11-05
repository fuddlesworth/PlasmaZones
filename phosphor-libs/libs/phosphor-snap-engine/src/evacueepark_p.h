// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <iterator>

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

/// Drop every context in @p park that @p gone matches, then the windows and
/// outputs left with none, and the float-false suppression of a window parked
/// nowhere any more. For the prunes of a removed desktop or activity.
template<typename Matches>
void dropParkedContextsIf(SnapEvacueePark& park, const Matches& gone)
{
    for (auto outIt = park.byOutput.begin(); outIt != park.byOutput.end();) {
        for (auto winIt = outIt->begin(); winIt != outIt->end();) {
            QVector<ParkedSnapContext>& contexts = winIt.value();
            contexts.erase(std::remove_if(contexts.begin(), contexts.end(), gone), contexts.end());
            winIt = contexts.isEmpty() ? outIt->erase(winIt) : std::next(winIt);
        }
        outIt = outIt->isEmpty() ? park.byOutput.erase(outIt) : std::next(outIt);
    }
    park.suppressFloatFalse.removeIf([&park](const QString& windowId) {
        return std::none_of(park.byOutput.cbegin(), park.byOutput.cend(), [&windowId](const auto& windows) {
            return windows.contains(windowId);
        });
    });
}

} // namespace PhosphorSnapEngine
