// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollLayout.h>

namespace PhosphorScrollEngine {

namespace {

qreal resolveColumnWidth(const ColumnWidth& width, qreal usableWidth)
{
    const qreal resolved = (width.kind == ColumnWidth::Kind::Proportion) ? width.value * usableWidth : width.value;
    return qMax(resolved, qreal(1.0));
}

/// Resolve a tile's concrete (non-Auto) height. Returns a value >= 0 for
/// Fixed/Preset tiles, or -1.0 for Auto tiles (and for out-of-range Preset
/// indices, which fall back to Auto).
qreal resolveConcreteTileHeight(const WindowHeight& height, qreal columnHeight, const QVector<qreal>& presets)
{
    switch (height.kind) {
    case WindowHeight::Kind::Fixed:
        return qMax(height.fixedPx, qreal(0.0));
    case WindowHeight::Kind::Preset:
        if (height.presetIndex >= 0 && height.presetIndex < presets.size()) {
            return qMax(presets.at(height.presetIndex) * columnHeight, qreal(0.0));
        }
        return -1.0;
    case WindowHeight::Kind::Auto:
        break;
    }
    return -1.0;
}

} // namespace

QHash<QString, QRectF> resolveScrollLayout(const ScrollScreenState& state, const QRectF& workArea,
                                           const ScrollLayoutConfig& config)
{
    QHash<QString, QRectF> geometries;
    const QVector<Column>& columns = state.columns();
    if (columns.isEmpty()) {
        return geometries;
    }

    const qreal outer = qMax(config.outerGap, qreal(0.0));
    const qreal inner = qMax(config.innerGap, qreal(0.0));
    const qreal usableX = workArea.x() + outer;
    const qreal usableY = workArea.y() + outer;
    const qreal usableW = qMax(workArea.width() - 2.0 * outer, qreal(0.0));
    const qreal usableH = qMax(workArea.height() - 2.0 * outer, qreal(0.0));

    // Column widths and strip-space X positions. Columns pack left to right
    // from strip-x 0, separated by the inner gap. A column whose every tile
    // is minimized collapses out of the strip — zero width, no gap, the
    // cursor does not advance — so it keeps its slot in the column order
    // (restored when one of its tiles is unminimized) without leaving a gap.
    QVector<qreal> widths(columns.size(), 0.0);
    QVector<qreal> stripX(columns.size(), 0.0);
    qreal cursor = 0.0;
    for (int ci = 0; ci < columns.size(); ++ci) {
        stripX[ci] = cursor;
        if (!columns.at(ci).hasVisibleTiles()) {
            continue; // collapsed column — keeps its slot, takes no strip space
        }
        const qreal width = resolveColumnWidth(columns.at(ci).width(), usableW);
        widths[ci] = width;
        cursor += width + inner;
    }

    // The viewport is anchored to the focused column: viewPos is the strip-x
    // coordinate that maps to the inner-left edge of the working area.
    const int activeIndex = state.activeColumnIndex();
    qreal viewPos = 0.0;
    if (activeIndex >= 0 && activeIndex < stripX.size()) {
        viewPos = stripX.at(activeIndex) + state.viewOffset();
    }

    for (int ci = 0; ci < columns.size(); ++ci) {
        // Lay out only the visible (non-minimized) tiles. A minimized tile
        // keeps its place in the column order but contributes no geometry.
        QVector<const Tile*> visible;
        for (const Tile& tile : columns.at(ci).tiles()) {
            if (!tile.minimized) {
                visible.append(&tile);
            }
        }
        const int tileCount = static_cast<int>(visible.size());
        if (tileCount == 0) {
            continue; // empty or fully-minimized column — nothing to place
        }

        const qreal columnX = usableX + stripX.at(ci) - viewPos;
        const qreal columnW = widths.at(ci);
        const qreal availableH = qMax(usableH - inner * qMax(tileCount - 1, 0), qreal(0.0));

        // First pass: resolve concrete (Fixed/Preset) heights; total the
        // Auto weights and count the Auto tiles.
        QVector<qreal> heights(tileCount, -1.0);
        qreal concreteTotal = 0.0;
        qreal weightSum = 0.0;
        int autoCount = 0;
        for (int ti = 0; ti < tileCount; ++ti) {
            const qreal concrete =
                resolveConcreteTileHeight(visible.at(ti)->height, usableH, config.presetWindowHeights);
            if (concrete >= 0.0) {
                heights[ti] = concrete;
                concreteTotal += concrete;
            } else {
                ++autoCount;
                weightSum += qMax(visible.at(ti)->height.weight, qreal(0.0));
            }
        }
        const qreal autoSpace = qMax(availableH - concreteTotal, qreal(0.0));

        // Second pass: size the Auto tiles from the leftover space and lay
        // every visible tile out top to bottom.
        qreal y = usableY;
        for (int ti = 0; ti < tileCount; ++ti) {
            qreal height = heights.at(ti);
            if (height < 0.0) {
                const qreal weight = qMax(visible.at(ti)->height.weight, qreal(0.0));
                height = (weightSum > 0.0) ? autoSpace * weight / weightSum : autoSpace / autoCount;
            }
            // Floor to a positive height, mirroring resolveColumnWidth — a
            // zero-height window rect (zero-weight Auto tile, or a column
            // whose concrete heights exhaust the space) is not usable
            // downstream.
            height = qMax(height, qreal(1.0));
            geometries.insert(visible.at(ti)->windowId, QRectF(columnX, y, columnW, height));
            y += height + inner;
        }
    }
    return geometries;
}

QStringList scrollVisibleWindows(const QHash<QString, QRectF>& geometries, const QRectF& workArea)
{
    QStringList visible;
    for (auto it = geometries.cbegin(); it != geometries.cend(); ++it) {
        if (it.value().intersects(workArea)) {
            visible.append(it.key());
        }
    }
    return visible;
}

} // namespace PhosphorScrollEngine
