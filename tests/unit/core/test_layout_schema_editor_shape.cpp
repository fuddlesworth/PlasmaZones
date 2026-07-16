// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_schema_editor_shape.cpp
 * @brief Contract guard: the editor's save payload passes the layout schema.
 *
 * EditorController::saveLayout serializes the whole layout into one JSON object
 * and hands it to the daemon over D-Bus — to createLayoutFromJson for a new
 * layout, to updateLayout for an existing one. Both of those are schema-gated by
 * LayoutRegistry::isLayoutJsonValid, so any key the editor emits in a shape the
 * schema does not accept fails the save and drops the user's authored layout.
 *
 * Nothing else in the suite exercises that editor-to-daemon JSON shape, which is
 * how a schema that accepted only the string form of `aspectRatioClass` shipped
 * green while the editor emitted the int form.
 *
 * `aspectRatioClass` has two forms on the wire, and
 * ScreenClassification::fromJsonValue deliberately reads both:
 *   - the canonical string ("ultrawide"), emitted by Layout::toJson, which is
 *     what the registry persists and re-reads at startup, and
 *   - the raw int (2), emitted by EditorController::saveLayout.
 * The schema is the contract both must satisfy, so this test pins both.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTest>
#include <QUuid>

#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ZoneJsonKeys.h>

using PhosphorLayout::AspectRatioClass;
namespace Keys = PhosphorZones::ZoneJsonKeys;

namespace {

/// One zone in the exact shape EditorController::saveLayout writes: an id, a
/// name, a zoneNumber and a relativeGeometry of four doubles.
QJsonObject makeZone()
{
    QJsonObject relGeo;
    relGeo[QLatin1String(Keys::X)] = 0.0;
    relGeo[QLatin1String(Keys::Y)] = 0.0;
    relGeo[QLatin1String(Keys::Width)] = 0.5;
    relGeo[QLatin1String(Keys::Height)] = 1.0;

    QJsonObject zone;
    zone[QLatin1String(Keys::Id)] = QUuid::createUuid().toString();
    zone[QLatin1String(Keys::Name)] = QStringLiteral("Left");
    zone[QLatin1String(Keys::ZoneNumber)] = 1;
    zone[QLatin1String(Keys::RelativeGeometry)] = relGeo;
    return zone;
}

/// The layout envelope saveLayout builds before it appends the optional keys.
QJsonObject makeEditorLayout()
{
    QJsonObject layout;
    layout[QLatin1String(Keys::Id)] = QUuid::createUuid().toString();
    layout[QLatin1String(Keys::Name)] = QStringLiteral("Test Layout");
    layout[QLatin1String(Keys::IsBuiltIn)] = false;

    QJsonArray zones;
    zones.append(makeZone());
    layout[QLatin1String(Keys::Zones)] = zones;
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
    layout[QLatin1String(Keys::AspectRatioClassKey)] = aspectRatioClass;

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
    layout[QLatin1String(Keys::AspectRatioClassKey)] = aspectRatioClass;

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
    layout[QLatin1String(Keys::AspectRatioClassKey)] = aspectRatioClass;

    QVERIFY(!PhosphorZones::LayoutRegistry::isLayoutJsonValid(layout, QStringLiteral("off-contract form")));
}

QTEST_MAIN(TestLayoutSchemaEditorShape)
#include "test_layout_schema_editor_shape.moc"
