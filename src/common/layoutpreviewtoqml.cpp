// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutpreviewtoqml.h"

#include "../core/constants.h"
#include "../core/enums.h"

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QVariantList>

namespace PlasmaZones {

QVariantMap layoutPreviewToQmlMap(const PhosphorLayout::LayoutPreview& preview)
{
    QVariantMap map;
    map[QStringLiteral("id")] = preview.id;

    // QML expects "name", not "displayName" — legacy from the
    // UnifiedLayoutEntry::name field that LayoutAdaptor::getLayoutList
    // ships today.
    map[QStringLiteral("name")] = preview.displayName;

    if (!preview.description.isEmpty()) {
        map[QStringLiteral("description")] = preview.description;
    }

    map[QStringLiteral("zoneCount")] = preview.zoneCount;
    map[QStringLiteral("isAutotile")] = preview.isAutotile;
    map[QStringLiteral("recommended")] = preview.recommended;
    map[QStringLiteral("autoAssign")] = preview.autoAssign;

    // QML expects the aspect-ratio class as a STRING tag
    // (ScreenClassification::toString — "any" / "standard" / "ultrawide" /
    // "super-ultrawide" / "portrait"), not the enum int.
    map[QStringLiteral("aspectRatioClass")] =
        ScreenClassification::toString(static_cast<AspectRatioClass>(preview.aspectRatioClass));

    if (preview.referenceAspectRatio > 0.0) {
        map[QStringLiteral("referenceAspectRatio")] = preview.referenceAspectRatio;
    }

    // Each zone gets a nested "relativeGeometry" object — ZonePreview.qml
    // keys off `modelData.relativeGeometry.x` etc. and falls back to
    // the rect directly only when relativeGeometry is missing. zoneNumber
    // stays flat alongside.
    QVariantList zones;
    zones.reserve(preview.zones.size());
    for (int i = 0; i < preview.zones.size(); ++i) {
        const QRectF& r = preview.zones.at(i);
        QVariantMap zoneMap;
        QVariantMap relGeo;
        relGeo[QStringLiteral("x")] = r.x();
        relGeo[QStringLiteral("y")] = r.y();
        relGeo[QStringLiteral("width")] = r.width();
        relGeo[QStringLiteral("height")] = r.height();
        zoneMap[QStringLiteral("relativeGeometry")] = relGeo;
        if (i < preview.zoneNumbers.size()) {
            zoneMap[QStringLiteral("zoneNumber")] = preview.zoneNumbers.at(i);
        }
        zones.append(zoneMap);
    }
    map[QStringLiteral("zones")] = zones;

    // Autotile algorithm metadata — QML expects the capability flags as
    // FLAT top-level keys on the entry (legacy from
    // UnifiedLayoutEntry::supportsMasterCount etc.), not nested under
    // an "algorithm" sub-object.
    if (preview.algorithm.has_value()) {
        const auto& algo = preview.algorithm.value();
        map[QStringLiteral("supportsMasterCount")] = algo.supportsMasterCount;
        map[QStringLiteral("supportsSplitRatio")] = algo.supportsSplitRatio;
        map[QStringLiteral("producesOverlappingZones")] = algo.producesOverlappingZones;
        map[QStringLiteral("supportsCustomParams")] = algo.supportsCustomParams;
        if (algo.memory) {
            map[QStringLiteral("memory")] = true;
        }
        if (!algo.zoneNumberDisplay.isEmpty()) {
            map[QStringLiteral("zoneNumberDisplay")] = algo.zoneNumberDisplay;
        }
        map[QStringLiteral("isSystem")] = algo.isSystemEntry();
    }

    return map;
}

} // namespace PlasmaZones
