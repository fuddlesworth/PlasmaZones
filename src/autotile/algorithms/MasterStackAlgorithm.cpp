// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MasterStackAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <algorithm>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: MasterStack is the default algorithm (priority 10)
namespace {
AlgorithmRegistrar<MasterStackAlgorithm> s_masterStackRegistrar(DBus::AutotileAlgorithm::MasterStack, 10);
}

MasterStackAlgorithm::MasterStackAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString MasterStackAlgorithm::name() const
{
    return i18n("Master + Stack");
}

QString MasterStackAlgorithm::description() const
{
    return i18n("Large master area with stacked secondary windows");
}

QString MasterStackAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-left-close");
}

int MasterStackAlgorithm::masterZoneIndex() const noexcept
{
    return 0; // First zone is always master
}

bool MasterStackAlgorithm::supportsMasterCount() const noexcept
{
    return true;
}

bool MasterStackAlgorithm::supportsSplitRatio() const noexcept
{
    return true;
}

qreal MasterStackAlgorithm::defaultSplitRatio() const noexcept
{
    return DefaultSplitRatio; // 0.6 (60% master)
}

int MasterStackAlgorithm::defaultMaxWindows() const noexcept
{
    return 4; // 1 master + 3 stack
}

QVector<QRect> MasterStackAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const int outerGap = params.outerGap;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto &state = *params.state;

    const QRect area = innerRect(screenGeometry, outerGap);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Get master count and split ratio from state
    const int masterCount = std::clamp(state.masterCount(), 1, windowCount);
    const int stackCount = windowCount - masterCount;
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Compute per-column minimum widths from minSizes
    int minMasterWidth = 0;
    int minStackWidth = 0;
    if (!minSizes.isEmpty()) {
        for (int i = 0; i < masterCount && i < minSizes.size(); ++i) {
            minMasterWidth = std::max(minMasterWidth, minSizes[i].width());
        }
        for (int i = masterCount; i < windowCount && i < minSizes.size(); ++i) {
            minStackWidth = std::max(minStackWidth, minSizes[i].width());
        }
    }

    // Calculate master and stack widths
    int masterWidth;
    int stackWidth;

    if (stackCount == 0) {
        // All windows are masters - they take full width
        masterWidth = area.width();
        stackWidth = 0;
    } else {
        // Deduct the vertical gap between master and stack columns
        const int contentWidth = area.width() - innerGap;
        masterWidth = static_cast<int>(contentWidth * splitRatio);
        stackWidth = contentWidth - masterWidth;

        // Joint min-width solve for master and stack columns
        const int totalMin = std::max(minMasterWidth, 0) + std::max(minStackWidth, 0);
        if (totalMin > contentWidth && totalMin > 0) {
            // Unsatisfiable: distribute proportionally by minimum weight
            masterWidth = static_cast<int>(
                static_cast<qint64>(contentWidth) * std::max(minMasterWidth, 1) / totalMin);
            stackWidth = contentWidth - masterWidth;
        } else {
            if (minMasterWidth > 0 && masterWidth < minMasterWidth) {
                masterWidth = minMasterWidth;
                stackWidth = contentWidth - masterWidth;
            }
            if (minStackWidth > 0 && stackWidth < minStackWidth) {
                stackWidth = minStackWidth;
                masterWidth = contentWidth - stackWidth;
            }
        }
    }

    // Extract per-window min heights for master and stack columns
    QVector<int> masterMinHeights;
    QVector<int> stackMinHeights;
    if (!minSizes.isEmpty()) {
        masterMinHeights.resize(masterCount);
        for (int i = 0; i < masterCount; ++i) {
            masterMinHeights[i] = (i < minSizes.size()) ? minSizes[i].height() : 0;
        }
        stackMinHeights.resize(stackCount);
        for (int i = 0; i < stackCount; ++i) {
            int idx = masterCount + i;
            stackMinHeights[i] = (idx < minSizes.size()) ? minSizes[idx].height() : 0;
        }
    }

    // Calculate zone heights with gaps between vertically stacked zones
    const QVector<int> masterHeights = masterMinHeights.isEmpty()
        ? distributeWithGaps(area.height(), masterCount, innerGap)
        : distributeWithMinSizes(area.height(), masterCount, innerGap, masterMinHeights);

    // Generate master zones (left side, stacked vertically)
    int currentY = area.y();
    for (int i = 0; i < masterCount; ++i) {
        zones.append(QRect(area.x(), currentY, masterWidth, masterHeights[i]));
        currentY += masterHeights[i] + innerGap;
    }

    // Generate stack zones (right side, stacked vertically)
    if (stackCount > 0) {
        const QVector<int> stackHeights = stackMinHeights.isEmpty()
            ? distributeWithGaps(area.height(), stackCount, innerGap)
            : distributeWithMinSizes(area.height(), stackCount, innerGap, stackMinHeights);
        const int stackX = area.x() + masterWidth + innerGap;

        currentY = area.y();
        for (int i = 0; i < stackCount; ++i) {
            zones.append(QRect(stackX, currentY, stackWidth, stackHeights[i]));
            currentY += stackHeights[i] + innerGap;
        }
    }

    return zones;
}

} // namespace PlasmaZones
