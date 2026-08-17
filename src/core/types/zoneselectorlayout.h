// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/interfaces/settings_interfaces.h"
#include <QList>
#include <QRect>
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
    int cardPadding = 26; // Extra vertical space for card chrome (showCardBackground: true)
    // No preview-top-offset field: LayoutCard anchors the preview at
    // Kirigami.Units.gridUnit, which scales with the user's font metrics, so no
    // C++ constant can state it. Nothing outside QML needs it — cards are
    // hit-tested against their rendered geometry (selector.cpp).
    int cardSidePadding = 18; // Extra horizontal space for card chrome (kept equal to paddingSide by compute)
    int paddingSide = 18;
    int cellWidth = 0; // Full card cell width (indicatorWidth + cardSidePadding * 2)
    int cellHeight = 0; // Full card cell height (indicatorHeight + labelSpace + cardPadding)
    int columns = 1;
    int rows = 1; // Visible rows (may be limited by maxRows)
    int totalRows = 1; // Total rows (for scroll content height)
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

/// Per-card preview width for one strip card, from the column's work-area
/// share. THE parity chokepoint between the C++ bar width and the rendered
/// cards: ZoneSelectorStripCard.qml and ZoneSelectorContent.qml's insert-bar
/// arithmetic mirror this formula (indicator width scaled by the fraction,
/// non-positive/oversized fractions fall back to full width, 8 px floor so a
/// sliver column stays visible). Change it in all three places or the bar
/// mis-sizes against the row it wraps. Return-value asymmetry to note: this
/// returns the PREVIEW width (callers add cardSidePadding * 2 separately),
/// while the QML twins return the full CELL width with the padding already
/// folded in.
///
/// @p indicatorExtent is the card extent ALONG THE STRIP, so the horizontal
/// arm passes indicatorWidth and the vertical arm passes indicatorHeight. It
/// is not named for either one, because a "width" that is a height on half
/// the call sites is how the two arms drift.
inline int stripCardPreviewWidth(int indicatorExtent, qreal widthFraction)
{
    const qreal f = (widthFraction > 0.0 && widthFraction <= 1.0) ? widthFraction : 1.0;
    return std::max(8, qRound(indicatorExtent * f));
}

/**
 * @brief Compute zone selector layout dimensions from config and screen
 *
 * Determines indicator sizes, grid layout, container dimensions, and bar size
 * based on size mode (Auto/Manual), layout mode (Grid/Horizontal/Vertical),
 * and screen constraints.
 *
 * @param stripFractions Strip mode only: one work-area width share per card,
 * in card order. Non-empty, the horizontal content width sums the per-card
 * widths (stripCardPreviewWidth per entry) instead of assuming a uniform
 * cell, so the bar hugs the variable-width card row, and the fraction list is
 * the authority on card count (layoutCount is superseded — the two cannot
 * disagree). Empty = uniform cells (layout mode, or an empty strip's single
 * hittable cell — that whole-bar target lives in the CALLER's empty-strip
 * branch, selector_strip.cpp, not here).
 */
inline ZoneSelectorLayout computeZoneSelectorLayout(const ZoneSelectorConfig& config, const QRect& screenGeom,
                                                    int layoutCount, const QList<qreal>& stripFractions = {},
                                                    bool stripVerticalAxis = false)
{
    ZoneSelectorLayout layout;
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
    // Derived, not a coincidence of two literals: the card chrome stays in
    // lockstep with the container-padding half.
    layout.cardSidePadding = layout.paddingSide;

    // Card chrome: showCardBackground adds top margin + internal padding around
    // the preview in LayoutCard.qml.  Account for this in per-card cell size.
    //
    // Both are AXIS-NEUTRAL and stand for either arm, which is why neither
    // arm below recomputes them. On the vertical strip arm cellWidth is the
    // cross extent it derives scrollContentWidth from, and cellHeight is the
    // along-strip extent of a FULL-share card — the same value
    // stripCardPreviewWidth(indicatorHeight, 1.0) plus that arm's per-card
    // chrome yields. Only the per-card variable share differs there, and the
    // arm accumulates that itself.
    layout.cellWidth = layout.indicatorWidth + layout.cardSidePadding * 2;
    layout.cellHeight = layout.indicatorHeight + layout.labelSpace + layout.cardPadding;

    // Step 1: Apply maxRows setting (Grid only, all size modes)
    int visibleRows = layout.rows;
    if (layoutMode == ZoneSelectorLayoutMode::Grid && layout.rows > maxRows) {
        visibleRows = maxRows;
    }

    // Step 2: Screen-based clamping (all size modes)
    const int screenH = screenGeom.height();
    const int screenW = screenGeom.width();
    const int maxContentH = std::max(0, screenH - layout.containerPadding - 2 * layout.containerTopMargin);
    const int maxContentW = std::max(0, screenW - layout.containerPadding - 2 * layout.containerSideMargin);
    const int rowUnitH = layout.cellHeight + layout.indicatorSpacing;
    if (rowUnitH > 0) {
        const int maxFittingRows = std::max(1, (maxContentH + layout.indicatorSpacing) / rowUnitH);
        if (visibleRows > maxFittingRows) {
            visibleRows = maxFittingRows;
        }
    }

    layout.rows = visibleRows;
    layout.needsScrolling = (layout.totalRows > visibleRows);

    if (!stripFractions.isEmpty() && stripVerticalAxis) {
        // Vertical strip: the same cards stacked DOWN the popup. The card
        // count becomes the row count, the fractions accumulate into the
        // content HEIGHT, and one card's cross extent is the whole width.
        // Sizing this arm as a horizontal row (the pre-#923 behaviour, when
        // the axis never reached this function) stuffs a tall stack into a
        // one-cell-high container: the tail cards clip away with no vertical
        // scrollbar, and because the hit-test reads rendered rects back they
        // come back empty and unhittable.
        const int cardCount = static_cast<int>(stripFractions.size());
        layout.rows = cardCount;
        layout.columns = 1;
        layout.totalRows = cardCount;
        int cardsExtent = 0;
        for (const qreal f : stripFractions) {
            cardsExtent += stripCardPreviewWidth(layout.indicatorHeight, f) + layout.labelSpace + layout.cardPadding;
        }
        layout.scrollContentHeight = cardsExtent + (cardCount - 1) * layout.indicatorSpacing;
        layout.scrollContentWidth = layout.indicatorWidth + layout.cardSidePadding * 2;

        layout.contentWidth = layout.scrollContentWidth;
        layout.contentHeight = layout.scrollContentHeight;
        // The flags come from the UNCLAMPED comparison so a degenerate work
        // area (maxContent* == 0 on a screen a few pixels tall) still
        // reports the overflow to QML instead of a false "fits". Only the
        // clamp itself stays gated on a positive bound — clamping to 0
        // would zero the popup outright.
        layout.needsScrolling = layout.contentHeight > qMax(0, maxContentH);
        if (maxContentH > 0 && layout.contentHeight > maxContentH) {
            layout.contentHeight = maxContentH;
        }
        layout.needsHorizontalScrolling = layout.contentWidth > qMax(0, maxContentW);
        if (maxContentW > 0 && layout.contentWidth > maxContentW) {
            layout.contentWidth = maxContentW;
        }

        layout.containerWidth = layout.contentWidth + layout.containerPadding;
        layout.containerHeight = layout.contentHeight + layout.containerPadding;
        layout.barHeight = layout.containerTopMargin + layout.containerHeight;
        layout.barWidth = layout.containerSideMargin + layout.containerWidth + layout.containerSideMargin;
        return layout;
    }

    if (!stripFractions.isEmpty()) {
        // Variable-width strip cards: each card is its column's real share
        // of the screen (chrome and spacing stay per-card constants). The
        // fraction list supersedes layoutCount as the card count so the
        // struct cannot come out self-inconsistent when the two callers'
        // derivations ever diverge.
        layout.columns = static_cast<int>(stripFractions.size());
        int cardsWidth = 0;
        for (const qreal f : stripFractions) {
            cardsWidth += stripCardPreviewWidth(layout.indicatorWidth, f) + layout.cardSidePadding * 2;
        }
        layout.scrollContentWidth =
            cardsWidth + (static_cast<int>(stripFractions.size()) - 1) * layout.indicatorSpacing;
    } else {
        layout.scrollContentWidth = layout.columns * layout.cellWidth + (layout.columns - 1) * layout.indicatorSpacing;
    }
    layout.scrollContentHeight =
        layout.totalRows * layout.cellHeight + (layout.totalRows - 1) * layout.indicatorSpacing;

    layout.contentWidth = layout.scrollContentWidth;
    layout.contentHeight = visibleRows * layout.cellHeight + (visibleRows - 1) * layout.indicatorSpacing;

    // Unclamped comparison for the flag, positive-bound gate for the clamp
    // — the vertical arm's rationale, applied to this arm's one clamp.
    layout.needsHorizontalScrolling = layout.contentWidth > qMax(0, maxContentW);
    if (maxContentW > 0 && layout.contentWidth > maxContentW) {
        layout.contentWidth = maxContentW;
    }

    layout.containerWidth = layout.contentWidth + layout.containerPadding;
    layout.containerHeight = layout.contentHeight + layout.containerPadding;
    layout.barHeight = layout.containerTopMargin + layout.containerHeight;
    layout.barWidth = layout.containerSideMargin + layout.containerWidth + layout.containerSideMargin;

    return layout;
}

// The QScreen convenience overload was removed: it had no callers (both
// production sites resolve a QRect through ScreenManager) and it could not
// forward stripFractions, so any future caller on a strip screen would have
// silently fallen back to uniform-cell sizing.

} // namespace PlasmaZones
