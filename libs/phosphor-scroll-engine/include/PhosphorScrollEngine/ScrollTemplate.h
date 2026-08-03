// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorscrollengine_export.h>

#include <QList>
#include <QRectF>
#include <QVector>

namespace PhosphorScrollEngine {

/**
 * @brief Preset vocabulary extracted from a layout template's zones.
 *
 * The daemon resolves a scrolling screen's assigned template layout, projects
 * its zones to normalized 0–1 rects (Zone::normalizedGeometry) and hands them
 * to extractTemplateVocabulary; each NON-EMPTY result list replaces the
 * matching settings-configured preset list wholesale for that screen
 * (ScrollLayoutParams::presetColumnWidths / presetWindowHeights). A template
 * with widths but no height entries therefore yields a MIXED vocabulary:
 * template widths beside the settings height list. The input is deliberately plain QRectF so this
 * library takes no dependency on the zones library — the semantics that live
 * here (MinColumnWidthFraction clamping, ascending preset order) are this
 * library's contract, not the layout model's.
 */
struct ScrollTemplateVocabulary
{
    /// Distinct column-width fractions, ascending, deduped, clamped to
    /// [MinColumnWidthFraction, 1.0]. Empty means "no usable template" —
    /// the caller must fall back to the settings preset list rather than
    /// pushing an empty vocabulary (an empty preset list breaks cycling).
    QList<qreal> columnWidths;
    /// Distinct window-height fractions, ascending, deduped, clamped to
    /// [MinWindowHeightFraction, 1.0]. May be empty (template has no
    /// stacked or partial-height zones): settings fallback applies.
    QList<qreal> windowHeights;
};

/**
 * @brief Derive the preset vocabulary from a template layout's zones.
 *
 * Zones are read left to right and grouped into x-bands (both vertical edges
 * aligned within tolerance): each band's width becomes a column-width preset,
 * and the heights of zones stacked inside a band — plus any deliberate
 * partial-height solo zone — become window-height presets. Zone fractions sum
 * to ~1.0 across the screen, which already matches the gap-aware proportion
 * contract (proportionalPx), so no gap adjustment happens here.
 *
 * Degenerate zones (non-positive or sliver extents, out-of-range coords) are
 * dropped; if nothing survives, the result has empty columnWidths and the
 * caller treats the layout as "no template".
 */
PHOSPHORSCROLLENGINE_EXPORT ScrollTemplateVocabulary extractTemplateVocabulary(const QVector<QRectF>& zones);

} // namespace PhosphorScrollEngine
