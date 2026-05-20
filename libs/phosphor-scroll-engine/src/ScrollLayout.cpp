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

/// Index of the column that anchors the viewport: the focused column when it
/// is visible, otherwise the nearest visible column (searching right first,
/// since a mid-strip collapsed column shares the next column's stripX, then
/// left). Returns -1 when no column is visible. @p widths is from
/// resolveColumnMetrics; @p activeIndex must be a valid column index.
int viewportAnchorColumn(const QVector<qreal>& widths, int activeIndex)
{
    // Defensive bounds check: callers must pass an in-range activeIndex (the
    // contract is documented at the declaration), but the function operates
    // on widths.size() while computeViewportScroll's outer guard uses
    // columns.size() — they're equal by construction in resolveColumnMetrics
    // but the abstraction crosses a boundary, so guard explicitly here.
    if (activeIndex < 0 || activeIndex >= widths.size()) {
        return -1;
    }
    if (widths.at(activeIndex) > 0.0) {
        return activeIndex;
    }
    for (int ci = activeIndex + 1; ci < widths.size(); ++ci) {
        if (widths.at(ci) > 0.0) {
            return ci;
        }
    }
    for (int ci = activeIndex - 1; ci >= 0; --ci) {
        if (widths.at(ci) > 0.0) {
            return ci;
        }
    }
    return -1;
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

ScrollColumnMetrics resolveColumnMetrics(const ScrollScreenState& state, const QRectF& workArea,
                                         const ScrollLayoutConfig& config)
{
    // Columns pack left to right from strip-x 0, separated by the inner gap;
    // a column whose every tile is minimized collapses — zero width, no gap,
    // the cursor does not advance — so it keeps its slot in the column order
    // without leaving a gap. A collapsed column's stripX therefore coincides
    // with the next column's.
    const QVector<Column>& columns = state.columns();
    const qreal outer = qMax(config.outerGap, qreal(0.0));
    const qreal inner = qMax(config.innerGap, qreal(0.0));
    const qreal usableW = qMax(workArea.width() - 2.0 * outer, qreal(0.0));

    ScrollColumnMetrics metrics;
    metrics.widths = QVector<qreal>(columns.size(), 0.0);
    metrics.stripX = QVector<qreal>(columns.size(), 0.0);
    qreal cursor = 0.0;
    for (int ci = 0; ci < columns.size(); ++ci) {
        metrics.stripX[ci] = cursor;
        if (!columns.at(ci).hasVisibleTiles()) {
            continue; // collapsed column — keeps its slot, takes no strip space
        }
        const qreal width = resolveColumnWidth(columns.at(ci).width(), usableW);
        metrics.widths[ci] = width;
        cursor += width + inner;
    }
    return metrics;
}

QHash<QString, QRectF> resolveScrollLayout(const ScrollScreenState& state, const QRectF& workArea,
                                           const ScrollLayoutConfig& config, const ScrollColumnMetrics* metrics)
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
    const qreal usableH = qMax(workArea.height() - 2.0 * outer, qreal(0.0));

    // Column metrics are scroll-independent; reuse the caller's when supplied
    // (one relayout resolves them once for both viewport + geometry), else
    // resolve them here.
    ScrollColumnMetrics ownMetrics;
    if (!metrics) {
        ownMetrics = resolveColumnMetrics(state, workArea, config);
        metrics = &ownMetrics;
    }
    const QVector<qreal>& widths = metrics->widths;
    const QVector<qreal>& stripX = metrics->stripX;

    // The viewport's inner-left edge maps to this strip-x coordinate. It is
    // computed by computeViewportScroll() and stored on the state by the
    // daemon before resolving; resolveScrollLayout only consumes it.
    const qreal viewPos = state.scrollX();

    for (int ci = 0; ci < columns.size(); ++ci) {
        // Lay out only the visible (non-minimized) tiles. A minimized tile
        // keeps its place in the column order but contributes no geometry.
        QVector<const Tile*> visible;
        visible.reserve(columns.at(ci).tileCount());
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

qreal computeViewportScroll(const ScrollScreenState& state, const QRectF& workArea, const ScrollLayoutConfig& config,
                            const ScrollColumnMetrics* metrics)
{
    const QVector<Column>& columns = state.columns();
    const int activeIndex = state.activeColumnIndex();
    if (columns.isEmpty() || activeIndex < 0 || activeIndex >= columns.size()) {
        return 0.0; // empty strip — viewport at the origin
    }

    const qreal outer = qMax(config.outerGap, qreal(0.0));
    const qreal usableW = qMax(workArea.width() - 2.0 * outer, qreal(0.0));

    // Column metrics are scroll-independent; reuse the caller's when supplied,
    // else resolve them here (see resolveScrollLayout).
    ScrollColumnMetrics ownMetrics;
    if (!metrics) {
        ownMetrics = resolveColumnMetrics(state, workArea, config);
        metrics = &ownMetrics;
    }

    const int anchor = viewportAnchorColumn(metrics->widths, activeIndex);
    if (anchor < 0) {
        return state.scrollX(); // every column collapsed — leave the viewport put
    }

    const qreal colLeft = metrics->stripX.at(anchor);
    const qreal colWidth = metrics->widths.at(anchor);

    if (config.viewportMode == ScrollViewportMode::Centered) {
        // Center the anchor column in the working area.
        return colLeft + (colWidth - usableW) / 2.0;
    }

    // Fit: scroll the minimum needed to bring the anchor column fully
    // on-screen, and not at all when it already is.
    const qreal current = state.scrollX();
    if (colWidth >= usableW) {
        // Wider than the viewport — it cannot fit. Keep the current scroll
        // when the column already fills the viewport edge to edge, otherwise
        // scroll the minimum to close the dead space: clamp to the range that
        // keeps the viewport entirely within the column.
        return qBound(colLeft, current, colLeft + colWidth - usableW);
    }
    if (colLeft < current) {
        return colLeft; // off the left edge — scroll left just enough
    }
    if (colLeft + colWidth > current + usableW) {
        return colLeft + colWidth - usableW; // off the right edge — scroll right
    }
    return current; // already fully visible — do not move
}

QStringList scrollVisibleWindows(const QHash<QString, QRectF>& geometries, const QRectF& workArea)
{
    QStringList visible;
    for (auto it = geometries.cbegin(); it != geometries.cend(); ++it) {
        if (it.value().intersects(workArea)) {
            visible.append(it.key());
        }
    }
    // QHash iteration order is unspecified; sort for deterministic output so
    // callers using this list (logging, geometry-application scheduling,
    // tests) don't see flapping order across runs.
    visible.sort();
    return visible;
}

} // namespace PhosphorScrollEngine
