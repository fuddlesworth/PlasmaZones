// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DwindleMemoryAlgorithm.h"
#include "DwindleAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../SplitTree.h"
#include "../TilingState.h"
#include "pz_i18n.h"

namespace PlasmaZones {

// Self-registration: Dwindle Memory provides persistent split tracking (priority 45)
namespace {
AlgorithmRegistrar<DwindleMemoryAlgorithm> s_dwindleMemoryRegistrar(DBus::AutotileAlgorithm::DwindleMemory, 45);
}

DwindleMemoryAlgorithm::DwindleMemoryAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
    , m_fallback(new DwindleAlgorithm(this))
{
}

DwindleMemoryAlgorithm::~DwindleMemoryAlgorithm() = default;

QString DwindleMemoryAlgorithm::name() const
{
    return PzI18n::tr("Dwindle (Memory)");
}

QString DwindleMemoryAlgorithm::description() const
{
    return PzI18n::tr("Remembers split positions — resize one split without affecting others");
}

void DwindleMemoryAlgorithm::prepareTilingState(TilingState* state) const
{
    if (!state || state->splitTree()) {
        return; // Already has a tree (or no state)
    }

    const QStringList tiledWindows = state->tiledWindows();
    if (tiledWindows.size() <= 1) {
        return; // No tree needed for 0-1 windows
    }

    auto newTree = std::make_unique<SplitTree>();
    for (const QString& windowId : tiledWindows) {
        newTree->insertAtEnd(windowId);
    }
    state->setSplitTree(std::move(newTree));
}

QVector<QRect> DwindleMemoryAlgorithm::calculateZones(const TilingParams& params) const
{
    const int windowCount = params.windowCount;
    const auto& screenGeometry = params.screenGeometry;
    const auto& outerGaps = params.outerGaps;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const QRect area = innerRect(screenGeometry, outerGaps);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Use persistent split tree if available and leaf count matches
    SplitTree* tree = params.state->splitTree();

    if (tree && tree->leafCount() == windowCount) {
        return tree->applyGeometry(area, params.innerGap);
    }

    // Fallback: count mismatch — behave like stateless dwindle
    return calculateStatelessFallback(params);
}

QVector<QRect> DwindleMemoryAlgorithm::calculateStatelessFallback(const TilingParams& params) const
{
    // Delegate to stateless DwindleAlgorithm — avoids duplicating 77 lines
    return m_fallback->calculateZones(params);
}

} // namespace PlasmaZones
