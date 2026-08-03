// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrollingtemplateprojection.h"

#include <PhosphorScrollEngine/ScrollTemplate.h>
#include <PhosphorScrollEngine/ScrollTypes.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

#include <QVariantList>

namespace PlasmaZones {

QVariantMap scrollingTemplateOverrides(const PhosphorZones::Layout* templ, const QRect& fullGeometry,
                                       const QRect& availableGeometry)
{
    QVariantMap overrides;
    if (!templ) {
        return overrides;
    }
    // Fixed-geometry zones normalize against the same basis their own layout
    // resolution uses — the AVAILABLE geometry unless the layout opts into
    // the full screen (mirrors GeometryUtils' reference selection); an
    // invalid rect degrades normalizedGeometry to the stored relative
    // geometry, which only matters for Fixed-geometry zones.
    const QRectF referenceRect = templ->useFullScreenGeometry() ? QRectF(fullGeometry) : QRectF(availableGeometry);
    QVector<QRectF> zoneRects;
    zoneRects.reserve(templ->zones().size());
    for (const PhosphorZones::Zone* zone : templ->zones()) {
        if (zone) {
            zoneRects.append(zone->normalizedGeometry(referenceRect));
        }
    }
    const PhosphorScrollEngine::ScrollTemplateVocabulary vocab =
        PhosphorScrollEngine::extractTemplateVocabulary(zoneRects);
    if (!vocab.columnWidths.isEmpty()) {
        QVariantList widths;
        widths.reserve(vocab.columnWidths.size());
        for (qreal w : vocab.columnWidths) {
            widths.append(w);
        }
        overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::presetColumnWidths(), widths);
    }
    // Sibling guard, deliberately NOT nested under the widths one: the
    // extractor cannot produce heights without widths today, but encoding
    // that invariant as caller control flow would silently drop height
    // pushes if the extractor ever changed.
    if (!vocab.windowHeights.isEmpty()) {
        QVariantList heights;
        heights.reserve(vocab.windowHeights.size());
        for (qreal h : vocab.windowHeights) {
            heights.append(h);
        }
        overrides.insert(PhosphorScrollEngine::ScrollPerScreenKeys::presetWindowHeights(), heights);
    }
    return overrides;
}

} // namespace PlasmaZones
