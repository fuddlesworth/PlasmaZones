// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/ScrollScreenState.h>
#include <phosphorscrollengine_export.h>

#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PhosphorScrollEngine {

/// Tuning for resolving a ScrollScreenState into pixel geometry.
struct ScrollLayoutConfig
{
    /// Margin between the working-area edges and the strip (logical px).
    qreal outerGap = 0.0;
    /// Gap between adjacent columns, and between tiles within a column.
    qreal innerGap = 0.0;
    /// Preset tile heights as fractions [0..1] of the column content height,
    /// indexed by WindowHeight::presetIndex. An out-of-range index falls
    /// back to Auto sizing.
    QVector<qreal> presetWindowHeights;
};

/// Resolve the scrollable-tiling strip into absolute per-window geometry.
///
/// The strip is an unbounded horizontal canvas; the returned rectangles are
/// in @p workArea's coordinate space and MAY fall partly or wholly outside
/// it (off-screen columns) — that is expected and intentional. The viewport
/// is anchored to the focused column: at viewOffset 0 the focused column's
/// left edge sits at the inner-left edge of the working area.
///
/// Returns windowId → frame rectangle for every tiled window. Floating
/// windows are not part of the strip and are not placed.
PHOSPHORSCROLLENGINE_EXPORT QHash<QString, QRectF>
resolveScrollLayout(const ScrollScreenState& state, const QRectF& workArea, const ScrollLayoutConfig& config);

/// Window IDs from @p geometries whose rectangle intersects @p workArea —
/// i.e. the windows currently (partly) visible. Useful for limiting real
/// geometry application to the visible range.
PHOSPHORSCROLLENGINE_EXPORT QStringList scrollVisibleWindows(const QHash<QString, QRectF>& geometries,
                                                             const QRectF& workArea);

} // namespace PhosphorScrollEngine
