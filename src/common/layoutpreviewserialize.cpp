// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutpreviewserialize.h"

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QJsonArray>
#include <QVariantList>

namespace PlasmaZones {

namespace {

// ─── Keys ─────────────────────────────────────────────────────────────────
// QLatin1String instances are referenced uniformly from both the JSON and
// QVariantMap writers so there's exactly one place the wire spelling lives.
namespace K {
constexpr QLatin1String Id{"id"};
constexpr QLatin1String DisplayName{"displayName"};
constexpr QLatin1String Description{"description"};
constexpr QLatin1String ZoneCount{"zoneCount"};
constexpr QLatin1String Zones{"zones"};
constexpr QLatin1String IsAutotile{"isAutotile"};
constexpr QLatin1String IsSystem{"isSystem"};
constexpr QLatin1String Recommended{"recommended"};
constexpr QLatin1String AutoAssign{"autoAssign"};
constexpr QLatin1String AspectRatioClass{"aspectRatioClass"};
constexpr QLatin1String ReferenceAspectRatio{"referenceAspectRatio"};
constexpr QLatin1String SectionKey{"sectionKey"};
constexpr QLatin1String SectionLabel{"sectionLabel"};
constexpr QLatin1String SectionOrder{"sectionOrder"};
constexpr QLatin1String Algorithm{"algorithm"};

// Per-zone
constexpr QLatin1String X{"x"};
constexpr QLatin1String Y{"y"};
constexpr QLatin1String Width{"width"};
constexpr QLatin1String Height{"height"};
constexpr QLatin1String ZoneNumber{"zoneNumber"};

// Per-algorithm
constexpr QLatin1String SupportsMasterCount{"supportsMasterCount"};
constexpr QLatin1String SupportsSplitRatio{"supportsSplitRatio"};
constexpr QLatin1String ProducesOverlappingZones{"producesOverlappingZones"};
constexpr QLatin1String SupportsCustomParams{"supportsCustomParams"};
constexpr QLatin1String SupportsMemory{"supportsMemory"};
constexpr QLatin1String IsScripted{"isScripted"};
constexpr QLatin1String IsUserScript{"isUserScript"};
constexpr QLatin1String IsSystemEntry{"isSystemEntry"};
constexpr QLatin1String ZoneNumberDisplay{"zoneNumberDisplay"};
} // namespace K

QJsonObject zoneJson(const QRectF& r, int zoneNumber)
{
    QJsonObject obj;
    obj[K::X] = r.x();
    obj[K::Y] = r.y();
    obj[K::Width] = r.width();
    obj[K::Height] = r.height();
    obj[K::ZoneNumber] = zoneNumber;
    return obj;
}

QVariantMap zoneMap(const QRectF& r, int zoneNumber)
{
    QVariantMap map;
    map[K::X] = r.x();
    map[K::Y] = r.y();
    map[K::Width] = r.width();
    map[K::Height] = r.height();
    map[K::ZoneNumber] = zoneNumber;
    return map;
}

template<typename Container>
void writeAlgorithmFlat(Container& dst, const PhosphorLayout::AlgorithmMetadata& meta)
{
    dst[K::SupportsMasterCount] = meta.supportsMasterCount;
    dst[K::SupportsSplitRatio] = meta.supportsSplitRatio;
    dst[K::ProducesOverlappingZones] = meta.producesOverlappingZones;
    dst[K::SupportsCustomParams] = meta.supportsCustomParams;
    dst[K::SupportsMemory] = meta.supportsMemory;
    dst[K::IsScripted] = meta.isScripted;
    dst[K::IsUserScript] = meta.isUserScript;
    if (!meta.zoneNumberDisplay.isEmpty()) {
        dst[K::ZoneNumberDisplay] = meta.zoneNumberDisplay;
    }
}

QString aspectRatioClassTag(int cls)
{
    return PhosphorLayout::ScreenClassification::toString(static_cast<PhosphorLayout::AspectRatioClass>(cls));
}

} // namespace

QJsonObject toJson(const PhosphorLayout::LayoutPreview& preview)
{
    QJsonObject json;
    json[K::Id] = preview.id;
    json[K::DisplayName] = preview.displayName;
    if (!preview.description.isEmpty()) {
        json[K::Description] = preview.description;
    }
    json[K::ZoneCount] = preview.zoneCount;
    json[K::IsAutotile] = preview.isAutotile();
    json[K::IsSystem] = preview.isSystem;
    json[K::Recommended] = preview.recommended;
    json[K::AutoAssign] = preview.autoAssign;
    json[K::AspectRatioClass] = aspectRatioClassTag(preview.aspectRatioClass);
    if (preview.referenceAspectRatio > 0.0) {
        json[K::ReferenceAspectRatio] = preview.referenceAspectRatio;
    }

    QJsonArray zones;
    for (int i = 0; i < preview.zones.size(); ++i) {
        const int zn = i < preview.zoneNumbers.size() ? preview.zoneNumbers.at(i) : 0;
        zones.append(zoneJson(preview.zones.at(i), zn));
    }
    json[K::Zones] = zones;

    if (!preview.sectionKey.isEmpty()) {
        json[K::SectionKey] = preview.sectionKey;
        json[K::SectionLabel] = preview.sectionLabel;
        json[K::SectionOrder] = preview.sectionOrder;
    }

    if (preview.algorithm.has_value()) {
        writeAlgorithmFlat(json, preview.algorithm.value());
    }

    return json;
}

QVariantList toVariantList(const QVector<PhosphorLayout::LayoutPreview>& previews)
{
    QVariantList list;
    list.reserve(previews.size());
    for (const auto& preview : previews) {
        list.append(toVariantMap(preview));
    }
    return list;
}

QVariantMap toVariantMap(const PhosphorLayout::LayoutPreview& preview)
{
    QVariantMap map;
    map[K::Id] = preview.id;
    map[K::DisplayName] = preview.displayName;
    if (!preview.description.isEmpty()) {
        map[K::Description] = preview.description;
    }
    map[K::ZoneCount] = preview.zoneCount;
    map[K::IsAutotile] = preview.isAutotile();
    map[K::IsSystem] = preview.isSystem;
    map[K::Recommended] = preview.recommended;
    map[K::AutoAssign] = preview.autoAssign;
    map[K::AspectRatioClass] = aspectRatioClassTag(preview.aspectRatioClass);
    if (preview.referenceAspectRatio > 0.0) {
        map[K::ReferenceAspectRatio] = preview.referenceAspectRatio;
    }

    QVariantList zones;
    zones.reserve(preview.zones.size());
    for (int i = 0; i < preview.zones.size(); ++i) {
        const int zn = i < preview.zoneNumbers.size() ? preview.zoneNumbers.at(i) : 0;
        zones.append(zoneMap(preview.zones.at(i), zn));
    }
    map[K::Zones] = zones;

    if (!preview.sectionKey.isEmpty()) {
        map[K::SectionKey] = preview.sectionKey;
        map[K::SectionLabel] = preview.sectionLabel;
        map[K::SectionOrder] = preview.sectionOrder;
    }

    if (preview.algorithm.has_value()) {
        writeAlgorithmFlat(map, preview.algorithm.value());
    }

    return map;
}

} // namespace PlasmaZones
