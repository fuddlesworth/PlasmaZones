// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GridAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <cmath>

namespace PlasmaZones {

// Self-registration: Grid layout (alphabetical priority 50)
namespace {
AlgorithmRegistrar<GridAlgorithm> s_gridRegistrar(DBus::AutotileAlgorithm::Grid, 50);
}

GridAlgorithm::GridAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString GridAlgorithm::name() const
{
    return i18n("Grid");
}

QString GridAlgorithm::description() const
{
    return i18n("Equal-sized grid layout");
}

QString GridAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-grid");
}

QVector<QRect> GridAlgorithm::calculateZones(const TilingParams& params) const
{
    const int windowCount = params.windowCount;
    const auto& screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const auto& outerGaps = params.outerGaps;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    const QRect area = innerRect(screenGeometry, outerGaps);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Calculate grid dimensions to keep cells as square as possible
    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(windowCount))));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(windowCount) / cols));

    const auto& minSizes = params.minSizes;

    // Build per-column minimum widths (max of minWidth for all windows in each column)
    // and per-row minimum heights (max of minHeight for all windows in each row)
    QVector<int> colMinWidths(cols, 0);
    QVector<int> rowMinHeights(rows, 0);
    if (!minSizes.isEmpty()) {
        // Full rows (all except possibly the last)
        const int fullRows = (windowCount % cols == 0) ? rows : rows - 1;
        for (int i = 0; i < windowCount; ++i) {
            const int row = i / cols;
            const int col = i % cols;
            if (row < fullRows) {
                // Standard grid position
                colMinWidths[col] = std::max(colMinWidths[col], minWidthAt(minSizes, i));
                rowMinHeights[row] = std::max(rowMinHeights[row], minHeightAt(minSizes, i));
            } else {
                // Last (sparse) row — only contributes to row height, not column widths
                rowMinHeights[row] = std::max(rowMinHeights[row], minHeightAt(minSizes, i));
            }
        }
    }

    // Calculate column widths and row heights with gaps, respecting min sizes
    const QVector<int> columnWidths = colMinWidths.isEmpty()
        ? distributeWithGaps(area.width(), cols, innerGap)
        : distributeWithMinSizes(area.width(), cols, innerGap, colMinWidths);
    const QVector<int> rowHeights = rowMinHeights.isEmpty()
        ? distributeWithGaps(area.height(), rows, innerGap)
        : distributeWithMinSizes(area.height(), rows, innerGap, rowMinHeights);

    // Pre-compute column X positions
    QVector<int> colX(cols);
    colX[0] = area.x();
    for (int c = 1; c < cols; ++c) {
        colX[c] = colX[c - 1] + columnWidths[c - 1] + innerGap;
    }

    // Pre-compute row Y positions
    QVector<int> rowY(rows);
    rowY[0] = area.y();
    for (int r = 1; r < rows; ++r) {
        rowY[r] = rowY[r - 1] + rowHeights[r - 1] + innerGap;
    }

    // Generate zones row by row
    for (int i = 0; i < windowCount; ++i) {
        const int row = i / cols;
        const int col = i % cols;

        // Check if this is the last row and it has fewer windows
        const int windowsInThisRow = (row == rows - 1) ? (windowCount - row * cols) : cols;

        if (windowsInThisRow < cols && col == 0) {
            // Last row with fewer windows: re-distribute width with per-window min sizes
            QVector<int> lastRowMinWidths;
            if (!minSizes.isEmpty()) {
                lastRowMinWidths.resize(windowsInThisRow);
                for (int j = 0; j < windowsInThisRow; ++j) {
                    lastRowMinWidths[j] = minWidthAt(minSizes, row * cols + j);
                }
            }
            const QVector<int> lastRowWidths = lastRowMinWidths.isEmpty()
                ? distributeWithGaps(area.width(), windowsInThisRow, innerGap)
                : distributeWithMinSizes(area.width(), windowsInThisRow, innerGap, lastRowMinWidths);
            int lastRowX = area.x();
            for (int j = 0; j < windowsInThisRow; ++j) {
                zones.append(QRect(lastRowX, rowY[row], lastRowWidths[j], rowHeights[row]));
                lastRowX += lastRowWidths[j] + innerGap;
            }
            break; // All remaining windows in last row handled
        }

        if (windowsInThisRow == cols) {
            zones.append(QRect(colX[col], rowY[row], columnWidths[col], rowHeights[row]));
        }
    }

    return zones;
}

} // namespace PlasmaZones
