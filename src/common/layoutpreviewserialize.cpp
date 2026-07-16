// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutpreviewserialize.h"

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QJsonArray>
#include <QVariantList>
#include <QtMath>

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
// PhosphorZones::LayoutCategory mirror — `isAutotile` is the canonical
// boolean, but QML consumers (LayoutCard, CategoryBadge) read a numeric
// `category` field (0 = Manual, 1 = Autotile) to match the
// LayoutCategory enum used elsewhere. Emit both so QML doesn't need to
// translate, and the C++ `isAutotile` consumer keeps its name.
constexpr QLatin1String Category{"category"};
constexpr QLatin1String IsSystem{"isSystem"};
constexpr QLatin1String Recommended{"recommended"};
constexpr QLatin1String AutoAssign{"autoAssign"};
constexpr QLatin1String AspectRatioClass{"aspectRatioClass"};
constexpr QLatin1String ReferenceAspectRatio{"referenceAspectRatio"};
constexpr QLatin1String SectionKey{"sectionKey"};
constexpr QLatin1String SectionLabel{"sectionLabel"};
constexpr QLatin1String SectionOrder{"sectionOrder"};

// Per-zone
constexpr QLatin1String X{"x"};
constexpr QLatin1String Y{"y"};
constexpr QLatin1String Width{"width"};
constexpr QLatin1String Height{"height"};
constexpr QLatin1String ZoneNumber{"zoneNumber"};

// Per-algorithm (flat under the same top-level object — see header comment).
constexpr QLatin1String SupportsMasterCount{"supportsMasterCount"};
constexpr QLatin1String SupportsSplitRatio{"supportsSplitRatio"};
constexpr QLatin1String ProducesOverlappingZones{"producesOverlappingZones"};
constexpr QLatin1String SupportsCustomParams{"supportsCustomParams"};
constexpr QLatin1String SupportsMemory{"supportsMemory"};
constexpr QLatin1String ReflowsOnResize{"reflowsOnResize"};
constexpr QLatin1String SupportsScriptState{"supportsScriptState"};
constexpr QLatin1String SupportsSingleWindow{"supportsSingleWindow"};
constexpr QLatin1String ReflowsOnFocus{"reflowsOnFocus"};
constexpr QLatin1String IsScripted{"isScripted"};
constexpr QLatin1String IsUserScript{"isUserScript"};
constexpr QLatin1String ZoneNumberDisplay{"zoneNumberDisplay"};
// Resolved master count the zones were computed with (LayoutPreview field, not
// AlgorithmMetadata — see the header note there). Emitted alongside the
// algorithm block because it is only meaningful for autotile entries.
constexpr QLatin1String MasterCount{"masterCount"};
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
    dst[K::ReflowsOnResize] = meta.reflowsOnResize;
    dst[K::SupportsScriptState] = meta.supportsScriptState;
    dst[K::SupportsSingleWindow] = meta.supportsSingleWindow;
    dst[K::ReflowsOnFocus] = meta.reflowsOnFocus;
    dst[K::IsScripted] = meta.isScripted;
    dst[K::IsUserScript] = meta.isUserScript;
    const QString zoneNumberDisplay = PhosphorLayout::zoneNumberDisplayToString(meta.zoneNumberDisplay);
    if (!zoneNumberDisplay.isEmpty()) {
        dst[K::ZoneNumberDisplay] = zoneNumberDisplay;
    }
}

QString aspectRatioClassTag(PhosphorLayout::AspectRatioClass cls)
{
    return PhosphorLayout::ScreenClassification::toString(cls);
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
    json[K::Category] = preview.isAutotile() ? 1 : 0;
    json[K::IsSystem] = preview.isSystem;
    json[K::Recommended] = preview.recommended;
    json[K::AutoAssign] = preview.autoAssign;
    json[K::AspectRatioClass] = aspectRatioClassTag(preview.aspectRatioClass);
    // Guard against NaN/Inf landing in the wire payload — the QML preview
    // renderer divides by referenceAspectRatio, which would yield Infinity or
    // NaN-positioned thumbnails. Drop the field on non-finite values so the
    // renderer falls back to its no-hint geometry.
    if (qIsFinite(preview.referenceAspectRatio) && preview.referenceAspectRatio > 0.0) {
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
        json[K::MasterCount] = preview.masterCount;
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
    map[K::Category] = preview.isAutotile() ? 1 : 0;
    map[K::IsSystem] = preview.isSystem;
    map[K::Recommended] = preview.recommended;
    map[K::AutoAssign] = preview.autoAssign;
    map[K::AspectRatioClass] = aspectRatioClassTag(preview.aspectRatioClass);
    // Guard against NaN/Inf landing in the wire payload — the QML preview
    // renderer divides by referenceAspectRatio, which would yield Infinity or
    // NaN-positioned thumbnails. Drop the field on non-finite values so the
    // renderer falls back to its no-hint geometry.
    if (qIsFinite(preview.referenceAspectRatio) && preview.referenceAspectRatio > 0.0) {
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
        map[K::MasterCount] = preview.masterCount;
    }

    return map;
}

} // namespace PlasmaZones
