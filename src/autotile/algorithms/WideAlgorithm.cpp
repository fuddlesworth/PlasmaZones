// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WideAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "pz_i18n.h"
#include <algorithm>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Wide is a MasterStack variant (alphabetical priority 110)
namespace {
AlgorithmRegistrar<WideAlgorithm> s_wideRegistrar(DBus::AutotileAlgorithm::Wide, 110);
}

WideAlgorithm::WideAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString WideAlgorithm::name() const
{
    return PzI18n::tr("Wide");
}

QString WideAlgorithm::description() const
{
    return PzI18n::tr("Master area on top, remaining windows stacked below");
}

QString WideAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-split-top-bottom");
}

QVector<QRect> WideAlgorithm::calculateZones(const TilingParams& params) const
{
    const int windowCount = params.windowCount;
    const auto& screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const auto& outerGaps = params.outerGaps;
    const auto& minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto& state = *params.state;

    const QRect area = innerRect(screenGeometry, outerGaps);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Get master count and split ratio from state
    const int masterCount = std::clamp(state.masterCount(), 1, windowCount);
    const int stackCount = windowCount - masterCount;
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Compute per-row minimum heights from minSizes
    int minMasterHeight = 0;
    int minStackHeight = 0;
    if (!minSizes.isEmpty()) {
        for (int i = 0; i < masterCount && i < minSizes.size(); ++i) {
            minMasterHeight = std::max(minMasterHeight, minSizes[i].height());
        }
        for (int i = masterCount; i < windowCount && i < minSizes.size(); ++i) {
            minStackHeight = std::max(minStackHeight, minSizes[i].height());
        }
    }

    // Calculate master and stack heights
    int masterHeight;
    int stackHeight;

    if (stackCount == 0) {
        // All windows are masters - they take full height
        masterHeight = area.height();
        stackHeight = 0;
    } else {
        // Deduct the horizontal gap between master and stack rows
        const int contentHeight = area.height() - innerGap;
        masterHeight = static_cast<int>(contentHeight * splitRatio);
        stackHeight = contentHeight - masterHeight;

        // Joint min-height solve for master and stack rows
        const int totalMin = std::max(minMasterHeight, 0) + std::max(minStackHeight, 0);
        if (totalMin > contentHeight && totalMin > 0) {
            // Unsatisfiable: distribute proportionally by minimum weight
            masterHeight =
                static_cast<int>(static_cast<qint64>(contentHeight) * std::max(minMasterHeight, 1) / totalMin);
            stackHeight = contentHeight - masterHeight;
        } else {
            if (minMasterHeight > 0 && masterHeight < minMasterHeight) {
                masterHeight = minMasterHeight;
                stackHeight = contentHeight - masterHeight;
            }
            if (minStackHeight > 0 && stackHeight < minStackHeight) {
                stackHeight = minStackHeight;
                masterHeight = contentHeight - stackHeight;
            }
        }
    }

    // Extract per-window min widths for master and stack rows
    QVector<int> masterMinWidths;
    QVector<int> stackMinWidths;
    if (!minSizes.isEmpty()) {
        masterMinWidths.resize(masterCount);
        for (int i = 0; i < masterCount; ++i) {
            masterMinWidths[i] = (i < minSizes.size()) ? minSizes[i].width() : 0;
        }
        stackMinWidths.resize(stackCount);
        for (int i = 0; i < stackCount; ++i) {
            int idx = masterCount + i;
            stackMinWidths[i] = (idx < minSizes.size()) ? minSizes[idx].width() : 0;
        }
    }

    // Calculate zone widths with gaps between horizontally arranged zones
    const QVector<int> masterWidths = masterMinWidths.isEmpty()
        ? distributeWithGaps(area.width(), masterCount, innerGap)
        : distributeWithMinSizes(area.width(), masterCount, innerGap, masterMinWidths);

    // Generate master zones (top row, laid out horizontally)
    int currentX = area.x();
    for (int i = 0; i < masterCount; ++i) {
        zones.append(QRect(currentX, area.y(), masterWidths[i], masterHeight));
        currentX += masterWidths[i] + innerGap;
    }

    // Generate stack zones (bottom row, laid out horizontally)
    if (stackCount > 0) {
        const QVector<int> stackWidths = stackMinWidths.isEmpty()
            ? distributeWithGaps(area.width(), stackCount, innerGap)
            : distributeWithMinSizes(area.width(), stackCount, innerGap, stackMinWidths);
        const int stackY = area.y() + masterHeight + innerGap;

        currentX = area.x();
        for (int i = 0; i < stackCount; ++i) {
            zones.append(QRect(currentX, stackY, stackWidths[i], stackHeight));
            currentX += stackWidths[i] + innerGap;
        }
    }

    return zones;
}

} // namespace PlasmaZones
