// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>

#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>

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
};

} // namespace PhosphorScrollEngine
