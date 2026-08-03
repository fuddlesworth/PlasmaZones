// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollTemplate.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include <QtGlobal>

#include <algorithm>

namespace PhosphorScrollEngine {

namespace {

/// Tolerance for edge alignment and dedupe, as a fraction of normalized
/// extent. Zone geometry arrives from editor drags and JSON round-trips, so
/// thirds land as 0.333333/0.333334 — a band grouping or width dedupe that
/// compared exactly would split those into distinct presets.
constexpr qreal Eps = 0.01;

bool fuzzyEq(qreal a, qreal b)
{
    return qAbs(a - b) < Eps;
}

/// Clamp, dedupe (within Eps) and sort ascending. Ascending matches the
/// settings preset convention, so nearestPresetWidthIdx and the cycle verb
/// behave identically on a template vocabulary.
QList<qreal> normalizeFractions(QList<qreal> values, qreal minFraction)
{
    for (qreal& v : values) {
        v = qBound(minFraction, v, 1.0);
    }
    std::sort(values.begin(), values.end());
    QList<qreal> out;
    out.reserve(values.size());
    for (qreal v : values) {
        if (out.isEmpty() || !fuzzyEq(out.last(), v)) {
            out.append(v);
        }
    }
    return out;
}

struct Band
{
    qreal x = 0.0;
    qreal right = 0.0;
    int members = 0;
    QList<qreal> heights;
};

} // namespace

ScrollTemplateVocabulary extractTemplateVocabulary(const QVector<QRectF>& zones)
{
    // Filter degenerate zones: sliver or non-positive extents, or rects
    // entirely outside the normalized space. Coordinates get a one-Eps grace
    // outside [0, 1] — the serializer clamps on write, but D-Bus ingress and
    // hand-edited files can carry float dust.
    QVector<QRectF> usable;
    usable.reserve(zones.size());
    for (const QRectF& z : zones) {
        if (z.width() <= Eps || z.height() <= Eps) {
            continue;
        }
        if (z.x() < -Eps || z.y() < -Eps || z.right() > 1.0 + Eps || z.bottom() > 1.0 + Eps) {
            continue;
        }
        usable.append(z);
    }
    if (usable.isEmpty()) {
        return {};
    }

    std::sort(usable.begin(), usable.end(), [](const QRectF& a, const QRectF& b) {
        return a.x() < b.x();
    });

    // Greedy x-band grouping: a zone joins the current band only when BOTH
    // vertical edges align within Eps — a partially overlapping zone starts
    // its own band from its own extent. Overlap across bands is harmless
    // here; the output is a width list, not a partition of the screen.
    QList<Band> bands;
    for (const QRectF& z : usable) {
        Band* band = nullptr;
        for (Band& b : bands) {
            if (fuzzyEq(z.x(), b.x) && fuzzyEq(z.right(), b.right)) {
                band = &b;
                break;
            }
        }
        if (!band) {
            bands.append(Band{z.x(), z.right(), 0, {}});
            band = &bands.last();
        }
        band->members += 1;
        band->heights.append(z.height());
    }

    ScrollTemplateVocabulary vocab;
    QList<qreal> widths;
    QList<qreal> heights;
    widths.reserve(bands.size());
    for (const Band& b : bands) {
        widths.append(b.right - b.x);
        if (b.members >= 2) {
            // Stacked zones: every member height is a deliberate row shape.
            heights.append(b.heights);
        } else if (b.heights.first() < 1.0 - Eps) {
            // A solo partial-height zone is a deliberate height too; a solo
            // full-height zone is just a column and adds no height preset.
            heights.append(b.heights.first());
        }
    }
    vocab.columnWidths = normalizeFractions(std::move(widths), MinColumnWidthFraction);
    vocab.windowHeights = normalizeFractions(std::move(heights), MinWindowHeightFraction);
    return vocab;
}

} // namespace PhosphorScrollEngine
