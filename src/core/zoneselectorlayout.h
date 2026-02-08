// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "settings_interfaces.h"
#include <QRect>
#include <QScreen>
#include <algorithm>
#include <cmath>

namespace PlasmaZones {

/**
 * @brief Computed layout dimensions for the zone selector popup
 *
 * Shared between OverlayService (QML window sizing) and WindowDragAdaptor
 * (trigger edge detection). Computed from ZoneSelectorConfig + screen geometry.
 */
struct ZoneSelectorLayout
{
    int indicatorWidth = 180;
    int indicatorHeight = 101;
    int indicatorSpacing = 18;
    int containerPadding = 36;
    int containerTopMargin = 10;
    int containerSideMargin = 10;
    int labelTopMargin = 8;
    int labelHeight = 20;
    int labelSpace = 28;
    int paddingSide = 18;
    int columns = 1;
    int rows = 1;          // Visible rows (may be limited by maxRows)
    int totalRows = 1;     // Total rows (for scroll content height)
    int contentWidth = 0;
    int contentHeight = 0;
    int scrollContentHeight = 0;
    int scrollContentWidth = 0;
    int containerWidth = 0;
    int containerHeight = 0;
    int barHeight = 0;
    int barWidth = 0;
    bool needsScrolling = false;
    bool needsHorizontalScrolling = false;
};

/**
 * @brief Compute zone selector layout dimensions from config and screen
 *
 * Determines indicator sizes, grid layout, container dimensions, and bar size
 * based on size mode (Auto/Manual), layout mode (Grid/Horizontal/Vertical),
 * and screen constraints.
 */
inline ZoneSelectorLayout computeZoneSelectorLayout(const ZoneSelectorConfig& config, QScreen* screen, int layoutCount)
{
    ZoneSelectorLayout layout;
    const QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    const qreal screenAspectRatio =
        screenGeom.height() > 0 ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);

    const auto sizeMode = static_cast<ZoneSelectorSizeMode>(config.sizeMode);
    const int maxRows = config.maxRows;

    if (sizeMode == ZoneSelectorSizeMode::Auto) {
        const int autoWidth = qBound(120, screenGeom.width() / 10, 280);
        layout.indicatorWidth = autoWidth;
        layout.indicatorHeight = qRound(static_cast<qreal>(layout.indicatorWidth) / screenAspectRatio);
    } else {
        layout.indicatorWidth = config.previewWidth;
        if (config.previewLockAspect) {
            layout.indicatorHeight = qRound(static_cast<qreal>(layout.indicatorWidth) / screenAspectRatio);
        } else {
            layout.indicatorHeight = config.previewHeight;
        }
    }

    const int safeLayoutCount = std::max(1, layoutCount);
    const auto layoutMode = static_cast<ZoneSelectorLayoutMode>(config.layoutMode);

    if (layoutMode == ZoneSelectorLayoutMode::Vertical) {
        layout.columns = 1;
        layout.rows = safeLayoutCount;
    } else if (layoutMode == ZoneSelectorLayoutMode::Grid) {
        layout.columns = std::max(1, config.gridColumns);
        layout.rows = static_cast<int>(std::ceil(static_cast<qreal>(safeLayoutCount) / layout.columns));
    } else {
        layout.columns = safeLayoutCount;
        layout.rows = 1;
    }

    layout.totalRows = layout.rows;
    layout.labelSpace = layout.labelTopMargin + layout.labelHeight;
    layout.paddingSide = layout.containerPadding / 2;

    // Step 1: Apply maxRows setting (Auto mode, Grid only)
    int visibleRows = layout.rows;
    if (sizeMode == ZoneSelectorSizeMode::Auto && layoutMode == ZoneSelectorLayoutMode::Grid && layout.rows > maxRows) {
        visibleRows = maxRows;
    }

    // Step 2: Screen-based clamping (all size modes)
    const int screenH = screenGeom.height();
    const int screenW = screenGeom.width();
    const int maxContentH = std::max(0, screenH - layout.containerPadding - 2 * layout.containerTopMargin);
    const int maxContentW = std::max(0, screenW - layout.containerPadding - 2 * layout.containerSideMargin);
    const int rowUnitH = layout.indicatorHeight + layout.labelSpace + layout.indicatorSpacing;
    if (rowUnitH > 0) {
        const int maxFittingRows = std::max(1, (maxContentH + layout.indicatorSpacing) / rowUnitH);
        if (visibleRows > maxFittingRows) {
            visibleRows = maxFittingRows;
        }
    }

    layout.rows = visibleRows;
    layout.needsScrolling = (layout.totalRows > visibleRows);

    layout.scrollContentWidth = layout.columns * layout.indicatorWidth + (layout.columns - 1) * layout.indicatorSpacing;
    layout.scrollContentHeight = layout.totalRows * (layout.indicatorHeight + layout.labelSpace)
        + (layout.totalRows - 1) * layout.indicatorSpacing;

    layout.contentWidth = layout.scrollContentWidth;
    layout.contentHeight =
        visibleRows * (layout.indicatorHeight + layout.labelSpace) + (visibleRows - 1) * layout.indicatorSpacing;

    if (layout.contentWidth > maxContentW && maxContentW > 0) {
        layout.contentWidth = maxContentW;
        layout.needsHorizontalScrolling = true;
    }

    layout.containerWidth = layout.contentWidth + layout.containerPadding;
    layout.containerHeight = layout.contentHeight + layout.containerPadding;
    layout.barHeight = layout.containerTopMargin + layout.containerHeight;
    layout.barWidth = layout.containerSideMargin + layout.containerWidth + layout.containerSideMargin;

    return layout;
}

} // namespace PlasmaZones
