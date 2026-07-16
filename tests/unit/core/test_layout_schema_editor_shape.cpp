// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_schema_editor_shape.cpp
 * @brief Contract guard: the JSON that reaches the schema gate passes it.
 *
 * Two producers feed the same gate, and this file pins both.
 *
 * EditorController::saveLayout builds its JSON BY HAND from
 * ZoneManager::zones() — it never calls Layout::toJson — and hands it to the
 * daemon over D-Bus, to createLayoutFromJson for a new layout and to
 * updateLayout for an existing one. Both are schema-gated by
 * LayoutRegistry::isLayoutJsonValid, so any key the editor emits in a shape the
 * schema does not accept fails the save and drops the user's authored layout.
 * The hand-built envelope is what the makeZone / makeEditorLayout helpers below
 * model, and the aspectRatioClass cases exercise it.
 *
 * Layout::toJson is the OTHER producer: it is what LayoutRegistry persists and
 * re-reads through the same gate at startup. The fixed-zone cases build their
 * payload from it rather than by hand, because fixed zones are where the two
 * ends of the round-trip disagree about what the derived relativeGeometry
 * should be, and a hand-written rect would only pin what this file believes the
 * serializer does.
 *
 * Nothing else in the suite exercises either JSON shape against the gate, which
 * is how a schema that accepted only the string form of `aspectRatioClass`
 * shipped green while the editor emitted the int form.
 *
 * Not covered here: the editor's own fixed-zone save shape
 * (ZoneManager::syncRelativeFromFixed, `fixedPx / effectiveScreenSizeF()`),
 * which derives relativeGeometry by a different route than Layout::toJson does.
 *
 * `aspectRatioClass` has two forms on the wire, and
 * ScreenClassification::fromJsonValue deliberately reads both:
 *   - the canonical string ("ultrawide"), emitted by Layout::toJson, which is
 *     what the registry persists and re-reads at startup, and
 *   - the raw int (2), emitted by EditorController::saveLayout.
 * The schema is the contract both must satisfy, so this test pins both.
 */

#include <memory>

#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QTest>
#include <QUuid>

#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneJsonKeys.h>

using PhosphorLayout::AspectRatioClass;
namespace Keys = PhosphorZones::ZoneJsonKeys;

namespace {

/// One relative-mode zone carrying the keys EditorController::saveLayout
/// writes for every zone: an id, a name, a zoneNumber and a relativeGeometry
/// of four doubles. saveLayout also always writes an `appearance` block, which
/// the schema lets through unconstrained, so it is left off here — the
/// fixed-zone test below covers the real emitted shape end to end.
QJsonObject makeZone()
{
    QJsonObject relGeo;
    relGeo[Keys::X] = 0.0;
    relGeo[Keys::Y] = 0.0;
    relGeo[Keys::Width] = 0.5;
    relGeo[Keys::Height] = 1.0;

    QJsonObject zone;
    zone[Keys::Id] = QUuid::createUuid().toString();
    zone[Keys::Name] = QStringLiteral("Left");
    zone[Keys::ZoneNumber] = 1;
    zone[Keys::RelativeGeometry] = relGeo;
    return zone;
}

/// The layout envelope saveLayout builds before it appends the optional keys.
QJsonObject makeEditorLayout()
{
    QJsonObject layout;
    layout[Keys::Id] = QUuid::createUuid().toString();
    layout[Keys::Name] = QStringLiteral("Test Layout");

    QJsonArray zones;
    zones.append(makeZone());
    layout[Keys::Zones] = zones;
    return layout;
}

} // namespace

class TestLayoutSchemaEditorShape : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The int form, emitted by EditorController::saveLayout for every
    /// non-Any aspect ratio the user can pick in the editor. This is the case
    /// that regressed: a string-only schema rejected the save outright.
    void editorIntFormValidates_data();
    void editorIntFormValidates();

    /// The canonical string form, emitted by Layout::toJson. This is what the
    /// registry writes to disk, so it must survive the startup re-read.
    void canonicalStringFormValidates_data();
    void canonicalStringFormValidates();

    /// Any is serialized by omission on both sides — saveLayout skips the key
    /// when the class is 0, so the optional key must stay optional.
    void omittedKeyValidates();

    /// The widened key accepts the two real wire forms, not anything at all.
    /// Without this the oneOf could degrade to a rubber stamp unnoticed.
    void offContractFormsRejected_data();
    void offContractFormsRejected();

    /// A fixed-geometry zone authored against the full screen, then recalculated
    /// against the smaller available area the daemon uses by default. This is
    /// the shape that reaches disk and the shape the editor re-emits, so
    /// Layout::toJson has to produce it inside the schema's 0-1 range.
    void fixedZoneOverflowingRecalcGeometryValidates();

    /// A fixed zone at a negative offset — the format documents these as
    /// supported (Zone::fromJson) and computeAbsoluteGeometry honours them.
    /// The derived relativeGeometry still has to land inside the schema's
    /// range or the gate drops the whole layout on the next read.
    void fixedZoneNegativeOffsetValidates();

    /// The load-bearing invariant, end to end: a document the schema gate
    /// ACCEPTS must never serialize into one the gate REJECTS. Anything else
    /// is a layout that saves fine and is gone at the next startup.
    void acceptedFixedGeometryDocSurvivesSaveRoundTrip_data();
    void acceptedFixedGeometryDocSurvivesSaveRoundTrip();

    /// A fixed zone SMALLER than the screen, serialized after the zone set was
    /// replaced wholesale (the D-Bus updateLayout shape: clearZones + re-add).
    /// Replacing zones does not change which screen the layout is shown on, so
    /// the emitted relativeGeometry must still mean the zone's real position on
    /// that screen.
    void fixedZoneSmallerThanScreenKeepsScreenReference();
};

void TestLayoutSchemaEditorShape::editorIntFormValidates_data()
{
    QTest::addColumn<int>("aspectRatioClass");

    // Every non-Any enumerator. Any is covered by omittedKeyValidates.
    QTest::newRow("standard") << static_cast<int>(AspectRatioClass::Standard);
    QTest::newRow("ultrawide") << static_cast<int>(AspectRatioClass::Ultrawide);
    QTest::newRow("super-ultrawide") << static_cast<int>(AspectRatioClass::SuperUltrawide);
    QTest::newRow("portrait") << static_cast<int>(AspectRatioClass::Portrait);
}

void TestLayoutSchemaEditorShape::editorIntFormValidates()
{
    QFETCH(int, aspectRatioClass);

    QJsonObject layout = makeEditorLayout();
    layout[Keys::AspectRatioClassKey] = aspectRatioClass;

    QVERIFY2(PhosphorZones::LayoutRegistry::isLayoutJsonValid(layout, QStringLiteral("editor int form")),
             "EditorController::saveLayout emits aspectRatioClass as an int; the schema must accept it "
             "or authoring a layout with any non-Any aspect ratio loses the whole layout.");
}

void TestLayoutSchemaEditorShape::canonicalStringFormValidates_data()
{
    QTest::addColumn<QString>("aspectRatioClass");

    // Derived from the enum via toString rather than hand-typed, so a rename of
    // a canonical string cannot leave this test asserting a stale spelling.
    for (const AspectRatioClass cls : {AspectRatioClass::Any, AspectRatioClass::Standard, AspectRatioClass::Ultrawide,
                                       AspectRatioClass::SuperUltrawide, AspectRatioClass::Portrait}) {
        const QString name = PhosphorLayout::ScreenClassification::toString(cls);
        QTest::newRow(qUtf8Printable(name)) << name;
    }
}

void TestLayoutSchemaEditorShape::canonicalStringFormValidates()
{
    QFETCH(QString, aspectRatioClass);

    QJsonObject layout = makeEditorLayout();
    layout[Keys::AspectRatioClassKey] = aspectRatioClass;

    QVERIFY2(PhosphorZones::LayoutRegistry::isLayoutJsonValid(layout, QStringLiteral("canonical string form")),
             "Layout::toJson emits the canonical string, and that is the form the registry persists; "
             "rejecting it would make an on-disk layout vanish on the next startup.");
}

void TestLayoutSchemaEditorShape::omittedKeyValidates()
{
    const QJsonObject layout = makeEditorLayout();
    QVERIFY(!layout.contains(Keys::AspectRatioClassKey));
    QVERIFY(PhosphorZones::LayoutRegistry::isLayoutJsonValid(layout, QStringLiteral("aspectRatioClass omitted")));
}

void TestLayoutSchemaEditorShape::offContractFormsRejected_data()
{
    QTest::addColumn<QJsonValue>("aspectRatioClass");

    // Ints outside the enum range. fromJsonValue maps these to Any, but the
    // schema is the boundary check and should not admit them in the first place.
    QTest::newRow("int below range") << QJsonValue(-1);
    QTest::newRow("int above range") << QJsonValue(static_cast<int>(AspectRatioClass::Portrait) + 1);
    // Neither string nor integer: the two subschemas must both miss.
    QTest::newRow("bool") << QJsonValue(true);
    QTest::newRow("null") << QJsonValue(QJsonValue::Null);
    QTest::newRow("object") << QJsonValue(QJsonObject{});
}

void TestLayoutSchemaEditorShape::offContractFormsRejected()
{
    QFETCH(QJsonValue, aspectRatioClass);

    QJsonObject layout = makeEditorLayout();
    layout[Keys::AspectRatioClassKey] = aspectRatioClass;

    QVERIFY(!PhosphorZones::LayoutRegistry::isLayoutJsonValid(layout, QStringLiteral("off-contract form")));
}

void TestLayoutSchemaEditorShape::fixedZoneOverflowingRecalcGeometryValidates()
{
    // The default path, end to end. The editor authors fixed pixels against the
    // full screen (targetScreenSize reports screen->geometry()), so a full-height
    // zone on a 2160px panel is 2160px tall. The daemon then recalculates against
    // the AVAILABLE area whenever useFullScreenGeometry is off, which is the
    // default, so the reference shrinks to 2126px once a panel is up.
    PhosphorZones::Layout layout(QStringLiteral("Fixed Full Screen"));
    auto* zone = new PhosphorZones::Zone();
    zone->setName(QStringLiteral("Full"));
    zone->setZoneNumber(1);
    zone->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
    zone->setFixedGeometry(QRectF(0, 0, 3840, 2160));
    layout.addZone(zone);
    layout.recalculateZoneGeometries(QRectF(0, 0, 3840, 2126));

    const QJsonObject json = layout.toJson();

    // relativeGeometry is derived, and the only reference that keeps it inside
    // 0-1 for a zone whose pixels overflow the recalc rect is the fixed-zone
    // bounding box. Dividing by the recalc height instead yields 2160/2126 =
    // 1.016, which the schema rejects — and it rejects the WHOLE layout, so a
    // save after any recalc makes the layout vanish from the picker at the next
    // startup and makes every later updateLayout refuse the payload.
    const QJsonObject relGeo = json[Keys::Zones].toArray().first().toObject()[Keys::RelativeGeometry].toObject();
    QVERIFY2(relGeo[Keys::Height].toDouble() <= 1.0,
             "Layout::toJson must normalize a fixed zone against a reference that contains it.");

    QVERIFY2(PhosphorZones::LayoutRegistry::isLayoutJsonValid(json, QStringLiteral("fixed zone overflowing recalc")),
             "LayoutRegistry::saveLayout persists exactly this object and re-reads it through the same schema "
             "gate on the next startup; rejecting it silently drops the user's layout.");

    // The pixels are what a fixed zone actually renders from, and they must
    // survive the round-trip untouched — normalizing for the schema must not
    // reach back into the geometry.
    std::unique_ptr<PhosphorZones::Layout> reloaded(PhosphorZones::Layout::fromJson(json));
    QVERIFY(reloaded);
    QCOMPARE(reloaded->zoneCount(), 1);
    QCOMPARE(reloaded->zone(0)->geometryMode(), PhosphorZones::ZoneGeometryMode::Fixed);
    QCOMPARE(reloaded->zone(0)->fixedGeometry(), QRectF(0, 0, 3840, 2160));
}

void TestLayoutSchemaEditorShape::fixedZoneNegativeOffsetValidates()
{
    // Zone::fromJson documents fixed X/Y as pixel offsets with negative values
    // allowed for off-screen positioning, and computeAbsoluteGeometry adds them
    // to the screen origin verbatim, so a negative offset genuinely renders
    // off-screen-left. The bounding box is anchored at the screen origin and
    // only tracks extents, so it never sees the negative origin: the reference
    // stays the recalc rect and x normalizes to -100/3840 = -0.026. The schema
    // pins x at minimum 0, so the WHOLE layout is rejected on the next read.
    PhosphorZones::Layout layout(QStringLiteral("Fixed Off-Screen"));
    auto* zone = new PhosphorZones::Zone();
    zone->setName(QStringLiteral("Bleed"));
    zone->setZoneNumber(1);
    zone->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
    zone->setFixedGeometry(QRectF(-100, 0, 2000, 1080));
    layout.addZone(zone);
    layout.recalculateZoneGeometries(QRectF(0, 0, 3840, 2160));

    const QJsonObject json = layout.toJson();

    const QJsonObject relGeo = json[Keys::Zones].toArray().first().toObject()[Keys::RelativeGeometry].toObject();
    QVERIFY2(relGeo[Keys::X].toDouble() >= 0.0,
             "Layout::toJson must emit a relativeGeometry inside the schema's range for a negative fixed offset.");

    QVERIFY2(PhosphorZones::LayoutRegistry::isLayoutJsonValid(json, QStringLiteral("fixed zone negative offset")),
             "The registry persists exactly this object and re-reads it through the same schema gate; rejecting it "
             "silently drops a layout the format explicitly allows.");

    // The pixels carry the truth for a fixed zone, so the off-screen offset must
    // survive untouched — normalizing for the schema must not reach back into
    // the geometry and quietly move the zone on-screen.
    std::unique_ptr<PhosphorZones::Layout> reloaded(PhosphorZones::Layout::fromJson(json));
    QVERIFY(reloaded);
    QCOMPARE(reloaded->zoneCount(), 1);
    QCOMPARE(reloaded->zone(0)->fixedGeometry(), QRectF(-100, 0, 2000, 1080));
}

void TestLayoutSchemaEditorShape::acceptedFixedGeometryDocSurvivesSaveRoundTrip_data()
{
    QTest::addColumn<QJsonObject>("fixedGeometry");

    // The schema constrains relativeGeometry and never mentions fixedGeometry,
    // so every one of these rides through the gate untouched on the way in. Each
    // then has to come back out inside the schema's range.
    const auto geo = [](double x, double y, double w, double h) {
        return QJsonObject{{Keys::X, x}, {Keys::Y, y}, {Keys::Width, w}, {Keys::Height, h}};
    };
    QTest::newRow("negative x bleeds off-screen left") << geo(-960, 0, 4800, 2160);
    QTest::newRow("negative y bleeds off-screen top") << geo(0, -540, 3840, 2700);
    QTest::newRow("overflows the screen") << geo(0, 0, 7680, 4320);
    QTest::newRow("negative width") << geo(0, 0, -100, 1080);
    QTest::newRow("negative height") << geo(0, 0, 1920, -100);
    QTest::newRow("zero extent") << geo(0, 0, 0, 0);
    // A JSON null reads back as 0.0, and a NaN cannot be spelled in JSON at
    // all, so the reachable non-finite case is the absent key.
    QTest::newRow("empty payload") << QJsonObject{};
}

void TestLayoutSchemaEditorShape::acceptedFixedGeometryDocSurvivesSaveRoundTrip()
{
    QFETCH(QJsonObject, fixedGeometry);

    // Build the doc the way a hostile-but-accepted payload arrives: a valid
    // relativeGeometry (so the gate lets it in) plus a fixedGeometry the gate
    // does not police.
    QJsonObject zone = makeZone();
    zone[Keys::GeometryMode] = static_cast<int>(PhosphorZones::ZoneGeometryMode::Fixed);
    zone[Keys::FixedGeometry] = fixedGeometry;
    QJsonObject doc = makeEditorLayout();
    doc[Keys::Zones] = QJsonArray{zone};

    QVERIFY2(PhosphorZones::LayoutRegistry::isLayoutJsonValid(doc, QStringLiteral("ingress")),
             "Precondition: the gate accepts this doc, so the invariant below is the one that matters.");

    // Ingress -> in-memory -> persist, which is what the daemon does on every
    // save, and then the startup re-read.
    std::unique_ptr<PhosphorZones::Layout> layout(PhosphorZones::Layout::fromJson(doc));
    QVERIFY(layout);
    layout->recalculateZoneGeometries(QRectF(0, 0, 3840, 2160));

    QVERIFY2(PhosphorZones::LayoutRegistry::isLayoutJsonValid(layout->toJson(), QStringLiteral("persisted")),
             "A doc the gate accepted serialized into one it rejects: the daemon writes a layout it will refuse to "
             "load, so it is gone from the picker at the next startup.");
}

void TestLayoutSchemaEditorShape::fixedZoneSmallerThanScreenKeepsScreenReference()
{
    // The D-Bus updateLayout shape. LayoutAdaptor::updateLayout replaces the
    // zone set wholesale (clearZones, then add the incoming zones) and then
    // serializes the result — both for the layoutChanged broadcast and for the
    // layoutModified -> saveLayout write that follows. Swapping the zone set
    // does not move the layout to a different screen, so the derived
    // relativeGeometry must still mean "where this zone sits on that screen".
    PhosphorZones::Layout layout(QStringLiteral("Fixed Quadrant"));
    auto* seed = new PhosphorZones::Zone();
    seed->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
    seed->setFixedGeometry(QRectF(0, 0, 1900, 1080));
    layout.addZone(seed);
    layout.recalculateZoneGeometries(QRectF(0, 0, 3840, 2160));

    layout.clearZones();
    auto* zone = new PhosphorZones::Zone();
    zone->setName(QStringLiteral("Quadrant"));
    zone->setZoneNumber(1);
    zone->setGeometryMode(PhosphorZones::ZoneGeometryMode::Fixed);
    zone->setFixedGeometry(QRectF(0, 0, 1900, 1080));
    layout.addZone(zone);

    const QJsonObject json = layout.toJson();
    const QJsonObject relGeo = json[Keys::Zones].toArray().first().toObject()[Keys::RelativeGeometry].toObject();

    // Normalizing against the fixed-zone bounding box instead of the screen
    // reports this 1900x1080 zone as (0, 0, 1, 1) — "full screen" — which is
    // what every consumer that reads only relativeGeometry then renders.
    QCOMPARE(relGeo[Keys::Width].toDouble(), 1900.0 / 3840.0);
    QCOMPARE(relGeo[Keys::Height].toDouble(), 1080.0 / 2160.0);
}

QTEST_MAIN(TestLayoutSchemaEditorShape)
#include "test_layout_schema_editor_shape.moc"
