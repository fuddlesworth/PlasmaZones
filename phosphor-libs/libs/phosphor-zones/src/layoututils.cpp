// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QColor>
#include <QJsonArray>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace PhosphorZones {

namespace LayoutUtils {

// ═══════════════════════════════════════════════════════════════════════════
// Zone conversion
// ═══════════════════════════════════════════════════════════════════════════

namespace {
QVariantMap zoneToVariantMap(Zone* zone, ZoneFields fields, const QRectF& referenceGeometry = QRectF(),
                             qreal screenScale = 0.0)
{
    using namespace ::PhosphorZones::ZoneJsonKeys;
    QVariantMap map;

    if (!zone) {
        return map;
    }

    // Always include core fields
    map[Id] = zone->id().toString();
    map[ZoneNumber] = zone->zoneNumber();

    // Relative geometry (0.0-1.0) for resolution-independent rendering
    // Use normalizedGeometry() to compute correct 0-1 coords for any geometry mode
    QRectF relGeo = zone->normalizedGeometry(referenceGeometry, screenScale);
    QVariantMap relGeoMap;
    relGeoMap[X] = relGeo.x();
    relGeoMap[Y] = relGeo.y();
    relGeoMap[Width] = relGeo.width();
    relGeoMap[Height] = relGeo.height();
    map[RelativeGeometry] = relGeoMap;

    // Per-zone geometry mode
    map[GeometryMode] = zone->geometryModeInt();
    if (zone->isFixedGeometry()) {
        QRectF fixedGeo = zone->fixedGeometry();
        QVariantMap fixedGeoMap;
        fixedGeoMap[X] = fixedGeo.x();
        fixedGeoMap[Y] = fixedGeo.y();
        fixedGeoMap[Width] = fixedGeo.width();
        fixedGeoMap[Height] = fixedGeo.height();
        map[FixedGeometry] = fixedGeoMap;
    }

    // Optional: Name
    if (fields.testFlag(ZoneField::Name)) {
        map[Name] = zone->name();
    }

    // Optional: Appearance properties (colors, opacities, border)
    if (fields.testFlag(ZoneField::Appearance)) {
        map[UseCustomColors] = zone->useCustomColors();

        // Colors as hex strings (ARGB format) for QML
        map[HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
        map[InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
        map[BorderColor] = zone->borderColor().name(QColor::HexArgb);

        // Opacity and border properties
        map[ActiveOpacity] = zone->activeOpacity();
        map[InactiveOpacity] = zone->inactiveOpacity();
        map[BorderWidth] = zone->borderWidth();
        map[BorderRadius] = zone->borderRadius();
    }

    return map;
}
} // namespace

QVariantList zonesToVariantList(Layout* layout, ZoneFields fields, const QRectF& referenceGeometry, qreal screenScale)
{
    QVariantList list;

    if (!layout) {
        return list;
    }

    // A caller's reference geometry and scale are one frame: the output it is
    // about to render on. Without a reference, use the layout's cached frame
    // instead, rect and scale together. The daemon recalculates ALL layouts on
    // startup and screen changes, so lastRecalcGeometry() is the rect of the last
    // pass, and a zero scale makes normalizedGeometry() divide a device-pixel
    // layout's fixed geometry by lastRecalcScale(), the newest scale a pass was
    // asked for. A caller scale is never paired with the cached rect, because
    // that rect belongs to whichever output was recalculated last.
    const bool callerFrame = referenceGeometry.width() > 0 && referenceGeometry.height() > 0;
    const QRectF& refGeo = callerFrame ? referenceGeometry : layout->lastRecalcGeometry();
    const qreal refScale = callerFrame ? screenScale : 0.0;

    const auto zones = layout->zones();
    for (Zone* zone : zones) {
        if (!zone) {
            continue;
        }
        list.append(zoneToVariantMap(zone, fields, refGeo, refScale));
    }

    return list;
}

// ═══════════════════════════════════════════════════════════════════════════
// Layout conversion
// ═══════════════════════════════════════════════════════════════════════════

QVariantMap layoutToVariantMap(Layout* layout, ZoneFields zoneFields)
{
    using namespace ::PhosphorZones::ZoneJsonKeys;
    QVariantMap map;

    if (!layout) {
        return map;
    }

    // Resolve the reference geometry once so both the zone list and the
    // aspectRatio derive from the same value (a stale lastRecalcGeometry
    // could otherwise leave them disagreeing). fixedZoneReferenceGeometry()
    // returns an empty rect for relative-only layouts; zonesToVariantList
    // treats that as "no override" and falls back to its own default.
    const QRectF refGeo = layout->fixedZoneReferenceGeometry();

    map[Id] = layout->id().toString();
    map[Name] = layout->name();
    map[Description] = layout->description();
    map[ZoneCount] = layout->zoneCount();
    map[Zones] = zonesToVariantList(layout, zoneFields, refGeo);
    map[Category] = static_cast<int>(LayoutCategory::Manual);
    map[AutoAssign] = layout->autoAssign();

    if (layout->hasFixedGeometryZones() && refGeo.height() > 0) {
        map[QLatin1String("referenceAspectRatio")] = refGeo.width() / refGeo.height();
    }

    return map;
}

// ═══════════════════════════════════════════════════════════════════════════
// Allow-list serialization
// ═══════════════════════════════════════════════════════════════════════════

void serializeAllowLists(QJsonObject& json, const QStringList& screens, const QList<int>& desktops,
                         const QStringList& activities)
{
    using namespace ::PhosphorZones::ZoneJsonKeys;

    if (!screens.isEmpty()) {
        json[AllowedScreens] = QJsonArray::fromStringList(screens);
    }
    if (!desktops.isEmpty()) {
        QJsonArray arr;
        for (int d : desktops) {
            arr.append(d);
        }
        json[AllowedDesktops] = arr;
    }
    if (!activities.isEmpty()) {
        json[AllowedActivities] = QJsonArray::fromStringList(activities);
    }
}

void deserializeAllowLists(const QJsonObject& json, QStringList& screens, QList<int>& desktops, QStringList& activities)
{
    using namespace ::PhosphorZones::ZoneJsonKeys;

    screens.clear();
    desktops.clear();
    activities.clear();

    if (json.contains(AllowedScreens)) {
        for (const auto& v : json[AllowedScreens].toArray()) {
            screens.append(v.toString());
        }
    }
    if (json.contains(AllowedDesktops)) {
        for (const auto& v : json[AllowedDesktops].toArray()) {
            desktops.append(v.toInt());
        }
    }
    if (json.contains(AllowedActivities)) {
        for (const auto& v : json[AllowedActivities].toArray()) {
            activities.append(v.toString());
        }
    }
}

void sortZonesByNumber(QVector<Zone*>& zones)
{
    std::stable_sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
        if (a->zoneNumber() != b->zoneNumber())
            return a->zoneNumber() < b->zoneNumber();
        return a->id() < b->id();
    });
}

QHash<QString, int> buildZonePositionMap(Layout* layout)
{
    QHash<QString, int> map;
    if (!layout) {
        return map;
    }
    QVector<Zone*> zones = layout->zones();
    sortZonesByNumber(zones);
    for (int i = 0; i < zones.size(); ++i) {
        map[zones[i]->id().toString()] = i + 1;
    }
    return map;
}

QHash<QString, int> buildGlobalZonePositionMap(const QList<Layout*>& layouts)
{
    QHash<QString, int> map;
    for (Layout* layout : layouts) {
        const QHash<QString, int> layoutMap = buildZonePositionMap(layout);
        for (auto it = layoutMap.constBegin(); it != layoutMap.constEnd(); ++it) {
            // The merge assumes zone UUIDs are unique across all layouts
            // — break that invariant and the resnap path silently uses
            // whatever position the later layout assigned, which can
            // place windows into the wrong zone. The registry's directory
            // load and import re-mint a copied id (reissueTakenZoneIds,
            // F552); assert in debug builds so an ingress that skips it is
            // caught immediately. Release builds
            // accept the last-write-wins semantic to avoid crashing on
            // legitimate-but-degenerate state.
            Q_ASSERT_X(!map.contains(it.key()) || map.value(it.key()) == it.value(), "buildGlobalZonePositionMap",
                       "duplicate zone UUID across layouts");
            map[it.key()] = it.value();
        }
    }
    return map;
}

bool layoutHoldsZones(const Layout* layout, const QStringList& zoneIds)
{
    if (!layout || zoneIds.isEmpty()) {
        return false;
    }
    return std::all_of(zoneIds.cbegin(), zoneIds.cend(), [layout](const QString& zoneId) {
        const QUuid id(zoneId);
        return !id.isNull() && layout->zoneById(id) != nullptr;
    });
}

bool contextLayoutHoldsZones(const IZoneLayoutRegistry* registry, const QString& screenId, int virtualDesktop,
                             const QString& activity, const QStringList& zoneIds)
{
    if (!registry) {
        return true;
    }
    // The layout in force for the context the zone is going back into, not
    // the one the zone came from: geometry resolution answers for whichever
    // layout holds the id, so it cannot tell the two apart.
    return layoutHoldsZones(registry->layoutForScreen(screenId, virtualDesktop, activity), zoneIds);
}

QStringList sameNumberedZones(const QStringList& zoneIds, const Layout* target, const QHash<QString, int>& positions)
{
    if (!target || zoneIds.isEmpty()) {
        return {};
    }
    if (layoutHoldsZones(target, zoneIds)) {
        return zoneIds;
    }
    // Position, not zone number: the 1-based index among the zones sorted by
    // number, the scheme the resnap buffer and the cross-desktop move use.
    const int position = positions.value(zoneIds.first(), 0);
    QVector<Zone*> zones = target->zones();
    if (position < 1 || position > zones.size()) {
        return {};
    }
    sortZonesByNumber(zones);
    return {zones.at(position - 1)->id().toString()};
}

int reissueTakenZoneIds(QJsonObject& layoutJson, const QList<Layout*>& loaded)
{
    using namespace ::PhosphorZones::ZoneJsonKeys;
    const QUuid layoutId = QUuid::fromString(layoutJson.value(Id).toString());
    QSet<QUuid> taken;
    for (const Layout* other : loaded) {
        if (!other || (!layoutId.isNull() && other->id() == layoutId)) {
            continue;
        }
        for (const Zone* zone : other->zones()) {
            taken.insert(zone->id());
        }
    }
    // A document whose id is not a UUID gets a fresh one from Layout::fromJson,
    // so its zone ids derive from a random namespace: from the null one, every
    // such document handed the same taken zone the same new id (F1052).
    const QUuid nameSpace = layoutId.isNull() ? QUuid::createUuid() : layoutId;
    QJsonArray zones = layoutJson.value(Zones).toArray();
    QSet<QUuid> seen;
    int reissued = 0;
    for (qsizetype i = 0; i < zones.size(); ++i) {
        QJsonObject zone = zones.at(i).toObject();
        const QUuid id = QUuid::fromString(zone.value(Id).toString());
        if (!id.isNull() && !taken.contains(id) && !seen.contains(id)) {
            seen.insert(id);
            continue;
        }
        // Name-based on (layout, old id, position), so a reload of the same
        // document lands on the same ids whichever copy of it wins. A derived
        // id can itself be taken, or equal one this document keeps or was
        // given above, so it is re-derived with a counter until it is free
        // (F1052): the first try is the plain name, which keeps every id this
        // gave before.
        //
        // A zone with no id (a hand-written layout; the schema requires only
        // its geometry) is derived the same way, from its position alone.
        // Left to Zone::fromJson it got a random id on every parse, so each
        // process, and each reload within one, named it differently: a rule
        // picking it in the settings app never matched the id the daemon
        // stamps. Counted as reissued, so a user file is written back with it.
        const QString name =
            (id.isNull() ? QStringLiteral("unset") : id.toString()) + QLatin1Char('#') + QString::number(i);
        QUuid fresh = QUuid::createUuidV5(nameSpace, name);
        for (int attempt = 1; taken.contains(fresh) || seen.contains(fresh); ++attempt) {
            fresh = QUuid::createUuidV5(nameSpace, name + QLatin1Char('#') + QString::number(attempt));
        }
        zone[Id] = fresh.toString();
        zones[i] = zone;
        seen.insert(fresh);
        ++reissued;
    }
    if (reissued > 0) {
        layoutJson[Zones] = zones;
    }
    return reissued;
}

} // namespace LayoutUtils

} // namespace PhosphorZones
