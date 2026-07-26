// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtGlobal>

namespace PhosphorScrollEngine {

namespace {

/// A proportion resolves against the work extent PLUS one gap so that
/// proportions summing to 1 tile edge-to-edge with the gaps between them
/// (0.5 + 0.5 across a 1000px area with a 10px gap → 495 + 10 + 495).
int proportionalPx(qreal proportion, int workExtent, int gap)
{
    return qMax(1, qRound(proportion * (workExtent + gap)) - gap);
}

qreal presetAt(const QList<qreal>& presets, int idx)
{
    if (presets.isEmpty()) {
        return 0.5;
    }
    return presets.at(qBound(0, idx, presets.size() - 1));
}

} // namespace

int ScrollStrip::resolveColumnWidthPx(const ColumnWidth& width, const ScrollLayoutParams& params)
{
    const int workW = params.workArea.width();
    switch (width.kind) {
    case ColumnWidth::Proportion:
        return qMin(workW, proportionalPx(width.proportion, workW, params.gap));
    case ColumnWidth::Fixed:
        return qBound(1, width.fixedPx, workW);
    case ColumnWidth::Preset:
        return qMin(workW, proportionalPx(presetAt(params.presetColumnWidths, width.presetIdx), workW, params.gap));
    }
    return qMin(workW, proportionalPx(0.5, workW, params.gap));
}

int ScrollStrip::columnWidthPx(const Column& c, const ScrollLayoutParams& params) const
{
    if (c.isFullyMinimized()) {
        return 0;
    }
    int px = resolveColumnWidthPx(c.width, params);
    for (const Tile& tile : c.tiles) {
        if (!tile.minimized && tile.minWidth > px) {
            px = tile.minWidth;
        }
    }
    return qMin(px, params.workArea.width());
}

int ScrollStrip::columnStripX(int columnIndex, const ScrollLayoutParams& params) const
{
    int x = 0;
    const int end = qBound(0, columnIndex, m_columns.size());
    for (int i = 0; i < end; ++i) {
        const int w = columnWidthPx(m_columns.at(i), params);
        if (w > 0) {
            x += w + params.gap;
        }
    }
    return x;
}

int ScrollStrip::stripWidthPx(const ScrollLayoutParams& params) const
{
    int total = 0;
    for (const Column& c : m_columns) {
        const int w = columnWidthPx(c, params);
        if (w > 0) {
            total += (total > 0 ? params.gap : 0) + w;
        }
    }
    return total;
}

int ScrollStrip::viewXFor(const ScrollLayoutParams& params) const
{
    if (m_activeColumnIdx < 0) {
        return 0;
    }
    return columnStripX(m_activeColumnIdx, params) - m_viewAnchor;
}

int ScrollStrip::centeredAnchorFor(int columnIndex, const ScrollLayoutParams& params) const
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return 0;
    }
    const int colW = columnWidthPx(m_columns.at(columnIndex), params);
    return (params.workArea.width() - colW) / 2;
}

int ScrollStrip::keepOrRecenterAnchor(int oldViewX, const ScrollLayoutParams& params) const
{
    // Structural changes (minimize collapse, consume) keep the view where it
    // was UNLESS the centering policy pins the focused column to the middle:
    // Always / lone-column centering must survive a strip-width change, or
    // the focused column drifts off-center until the next focus move.
    const bool centerLone = params.alwaysCenterSingleColumn && m_columns.size() == 1;
    if (centerLone || params.centerFocusedColumn == CenterFocusedColumn::Always) {
        return centeredAnchorFor(m_activeColumnIdx, params);
    }
    return clampedAnchor(columnStripX(m_activeColumnIdx, params) - oldViewX, params);
}

int ScrollStrip::clampedAnchor(int anchor, const ScrollLayoutParams& params) const
{
    if (m_activeColumnIdx < 0) {
        return 0;
    }
    const int workW = params.workArea.width();
    const int stripW = stripWidthPx(params);
    const int stripX = columnStripX(m_activeColumnIdx, params);
    // anchor = stripX - viewX; viewX must stay within [0, max(0, stripW - workW)].
    const int maxViewX = qMax(0, stripW - workW);
    const int viewX = qBound(0, stripX - anchor, maxViewX);
    return stripX - viewX;
}

void ScrollStrip::reanchorAfterFocusChange(int prevIdx, int oldViewX, const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0) {
        m_viewAnchor = 0;
        return;
    }
    const int workW = params.workArea.width();
    const int colW = columnWidthPx(m_columns.at(m_activeColumnIdx), params);
    const int stripX = columnStripX(m_activeColumnIdx, params);

    const bool centerLoneColumn = params.alwaysCenterSingleColumn && m_columns.size() == 1;
    bool center = centerLoneColumn || params.centerFocusedColumn == CenterFocusedColumn::Always;

    if (!center && params.centerFocusedColumn == CenterFocusedColumn::OnOverflow && prevIdx >= 0
        && prevIdx < m_columns.size() && prevIdx != m_activeColumnIdx) {
        const int prevW = columnWidthPx(m_columns.at(prevIdx), params);
        if (colW + params.gap + prevW > workW) {
            center = true;
        }
    }

    if (center) {
        m_viewAnchor = centeredAnchorFor(m_activeColumnIdx, params);
        return;
    }

    // CenterFocusedColumn::Never — scroll the minimum amount that makes the
    // focused column fully visible, pinning it to the edge it entered from.
    int pos = stripX - oldViewX;
    if (colW >= workW) {
        // Wider than the viewport: pin the entering edge.
        pos = (prevIdx >= 0 && m_activeColumnIdx < prevIdx) ? workW - colW : 0;
    } else if (pos < 0) {
        pos = 0;
    } else if (pos + colW > workW) {
        pos = workW - colW;
    }
    m_viewAnchor = clampedAnchor(pos, params);
}

void ScrollStrip::updateViewForFocus(const ScrollLayoutParams& params)
{
    reanchorAfterFocusChange(m_activeColumnIdx, viewXFor(params), params);
}

bool ScrollStrip::centerActiveColumn(const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0) {
        return false;
    }
    const int centered = centeredAnchorFor(m_activeColumnIdx, params);
    if (m_viewAnchor == centered) {
        return false;
    }
    m_viewAnchor = centered;
    return true;
}

ResolvedStrip ScrollStrip::relayout(const ScrollLayoutParams& params) const
{
    ResolvedStrip out;
    out.stripWidth = stripWidthPx(params);
    out.viewX = viewXFor(params);

    const QRect area = params.workArea;
    const int gap = params.gap;
    int x = area.x() - out.viewX;

    for (int ci = 0; ci < m_columns.size(); ++ci) {
        const Column& col = m_columns.at(ci);
        const int colW = columnWidthPx(col, params);
        if (colW <= 0) {
            continue; // fully minimized — occupies no strip width
        }

        ResolvedColumn rc;
        rc.columnIndex = ci;
        rc.tabbed = col.display == ColumnDisplay::Tabbed;
        rc.rect = QRect(x, area.y(), colW, area.height());

        QVector<int> visible;
        visible.reserve(col.tiles.size());
        for (int ti = 0; ti < col.tiles.size(); ++ti) {
            if (!col.tiles.at(ti).minimized) {
                visible.append(ti);
            }
        }

        if (rc.tabbed) {
            // Only the active tile is laid out, at full column height; the
            // others share its rect but are reported hidden.
            int activeTi = qBound(0, col.activeTileIdx, col.tiles.size() - 1);
            if (col.tiles.at(activeTi).minimized && !visible.isEmpty()) {
                activeTi = visible.first();
            }
            const QRect full(x, area.y(), colW, area.height());
            for (int ti : visible) {
                ResolvedTile rt;
                rt.windowId = col.tiles.at(ti).windowId;
                rt.rect = full;
                rt.hidden = (ti != activeTi);
                rc.tiles.append(rt);
            }
        } else if (!visible.isEmpty()) {
            // Vertical stack: Fixed/Preset tiles take their px, Auto tiles
            // share the remainder proportionally to weight.
            const int n = visible.size();
            const int gapsTotal = gap * (n - 1);
            const int availH = qMax(n, area.height() - gapsTotal);

            QVector<int> heights(n, 0);
            qreal autoWeightTotal = 0;
            int fixedTotal = 0;
            for (int vi = 0; vi < n; ++vi) {
                const Tile& t = col.tiles.at(visible.at(vi));
                switch (t.height.kind) {
                case WindowHeight::Fixed:
                    heights[vi] = qBound(1, t.height.fixedPx, availH);
                    fixedTotal += heights[vi];
                    break;
                case WindowHeight::Preset:
                    heights[vi] = qMin(
                        availH,
                        proportionalPx(presetAt(params.presetWindowHeights, t.height.presetIdx), area.height(), gap));
                    fixedTotal += heights[vi];
                    break;
                case WindowHeight::Auto:
                    autoWeightTotal += qMax<qreal>(0.01, t.height.weight);
                    break;
                }
            }
            // A single-tile column always fills the column height.
            if (n == 1) {
                heights[0] = availH;
            } else {
                // Renormalize when the Fixed/Preset heights alone overflow
                // the column: scale them down proportionally so the last
                // window is not laid out past the bottom of the work area
                // (each tile was only clamped individually above). Auto
                // tiles keep a 1px floor below, so the Fixed/Preset budget
                // is availH minus one pixel per Auto tile — without the
                // reservation those floors would overflow the column again.
                int autoCount = 0;
                for (int vi = 0; vi < n; ++vi) {
                    if (col.tiles.at(visible.at(vi)).height.kind == WindowHeight::Auto) {
                        ++autoCount;
                    }
                }
                const int fixedBudget = qMax(0, availH - autoCount);
                if (fixedTotal > fixedBudget && fixedTotal > 0) {
                    int scaledTotal = 0;
                    for (int vi = 0; vi < n; ++vi) {
                        if (heights[vi] > 0) {
                            heights[vi] =
                                qMax(1, static_cast<int>(static_cast<qint64>(heights[vi]) * fixedBudget / fixedTotal));
                            scaledTotal += heights[vi];
                        }
                    }
                    fixedTotal = scaledTotal;
                }
                int autoAvail = qMax(0, availH - fixedTotal);
                qreal weightLeft = autoWeightTotal;
                for (int vi = 0; vi < n; ++vi) {
                    const Tile& t = col.tiles.at(visible.at(vi));
                    if (t.height.kind != WindowHeight::Auto) {
                        continue;
                    }
                    const qreal w = qMax<qreal>(0.01, t.height.weight);
                    const int h = (weightLeft > 0) ? qRound(autoAvail * (w / weightLeft)) : 0;
                    heights[vi] = qMax(1, h);
                    autoAvail -= heights[vi];
                    weightLeft -= w;
                }
            }
            // Min-height clamp after distribution (soft constraint; a window
            // that cannot fit at all is the engine's cue to float it).
            for (int vi = 0; vi < n; ++vi) {
                const Tile& t = col.tiles.at(visible.at(vi));
                if (t.minHeight > 0 && heights[vi] < t.minHeight) {
                    heights[vi] = qMin(t.minHeight, availH);
                }
            }

            int y = area.y();
            for (int vi = 0; vi < n; ++vi) {
                ResolvedTile rt;
                rt.windowId = col.tiles.at(visible.at(vi)).windowId;
                rt.rect = QRect(x, y, colW, heights[vi]);
                rc.tiles.append(rt);
                y += heights[vi] + gap;
            }
        }

        out.columns.append(rc);
        x += colW + gap;
    }
    return out;
}

} // namespace PhosphorScrollEngine
