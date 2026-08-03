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

/// Preset-list length cap. KEEP IN SYNC with the settings validator's
/// kMaxPresetEntries (src/config/settingsschema_p.h) — not reachable from
/// this library, so the bound is hand-mirrored like MinColumnWidthFraction's
/// mirrors. A layout has no zone-count limit, and an uncapped vocabulary is
/// copied into ScrollLayoutParams on every relayout and walked by the cycle
/// verbs.
constexpr int MaxTemplateEntries = 16;

bool fuzzyEq(qreal a, qreal b)
{
    return qAbs(a - b) < Eps;
}

/// Clamp to the 1.0 ceiling, DROP values below the floor, dedupe (within
/// Eps), sort ascending and cap the length. Dropping matches the settings
/// parser (parsePresets refuses sub-floor entries outright): clamping a thin
/// zone UP to the floor would invent a preset the template does not contain
/// and collapse two distinct thin bands into one floor-valued entry.
/// Ascending order is imposed because a template has no authored order (the
/// settings list keeps the user's typed order; only the cycle verbs are
/// order-sensitive).
QList<qreal> normalizeFractions(QList<qreal> values, qreal minFraction)
{
    QList<qreal> kept;
    kept.reserve(values.size());
    for (qreal v : values) {
        if (v < minFraction) {
            continue;
        }
        kept.append(qMin(v, 1.0));
    }
    std::sort(kept.begin(), kept.end());
    QList<qreal> out;
    out.reserve(kept.size());
    for (qreal v : kept) {
        if (out.isEmpty() || !fuzzyEq(out.last(), v)) {
            out.append(v);
        }
        if (out.size() == MaxTemplateEntries) {
            break;
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
    // Filter degenerate zones: non-finite coordinates (a NaN would violate
    // the sort's strict weak ordering below), sliver or non-positive extents,
    // and rects entirely outside the normalized space. Coordinates get a
    // one-Eps grace outside [0, 1] — the serializer clamps on write, but
    // D-Bus ingress and hand-edited files can carry float dust.
    QVector<QRectF> usable;
    usable.reserve(zones.size());
    for (const QRectF& z : zones) {
        if (!qIsFinite(z.x()) || !qIsFinite(z.y()) || !qIsFinite(z.width()) || !qIsFinite(z.height())) {
            continue;
        }
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

    // Greedy x-band grouping: a zone joins an EXISTING band only when BOTH
    // vertical edges align within Eps (the scan covers every band so an
    // interleaved zone list still groups correctly) — a partially
    // overlapping zone starts its own band from its own extent. Overlap
    // across bands is harmless here; the output is a width list, not a
    // partition of the screen.
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
            // Stacked zones: every member height below full column height is
            // a deliberate row shape. A ~full-height member (overlapping
            // tab-style zones sharing one extent) is not — appending it
            // could reduce the whole height vocabulary to [1.0] and make
            // every Preset height resolve full-column. For a genuine stack
            // the members sum to ~1.0, so no real member ever reaches the
            // filter.
            for (qreal h : b.heights) {
                if (h < 1.0 - Eps) {
                    heights.append(h);
                }
            }
        } else if (b.heights.first() < 1.0 - Eps) {
            // A solo partial-height zone is a deliberate height too; a solo
            // full-height zone is just a column and adds no height preset.
            heights.append(b.heights.first());
        }
    }
    vocab.columnWidths = normalizeFractions(std::move(widths), MinColumnWidthFraction);
    vocab.windowHeights = normalizeFractions(std::move(heights), MinWindowHeightFraction);
    // A vocabulary whose only width is the full work area (row-only layouts:
    // every zone spans x 0..1) defines no COLUMN vocabulary at all — pushing
    // it would make a Preset-kind default width degenerate the strip to one
    // full-width column per window. Treat it as "no usable template" so the
    // settings lists stay in force. A legitimate single-entry list (e.g. a
    // thirds template's [1/3]) is kept: it is a real uniform-width
    // vocabulary, and the cycle verb's refusal on it carries its own
    // feedback.
    if (vocab.columnWidths.size() == 1 && vocab.columnWidths.first() > 1.0 - Eps) {
        return {};
    }
    return vocab;
}

} // namespace PhosphorScrollEngine
