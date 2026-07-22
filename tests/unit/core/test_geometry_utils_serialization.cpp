// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_geometry_utils_serialization.cpp
 * @brief Unit tests for GeometryUtils::rectToJson(), serializeZoneAssignments(),
 *        and deserializeZoneAssignments()
 *
 * Tests cover:
 * - rectToJson with valid and empty/invalid QRects
 * - serializeZoneAssignments with empty, single, and multiple entries
 * - Entries with empty string fields
 * - Round-trip: serialize then deserialize and verify all fields match
 * - deserializeZoneAssignments dropping incomplete entries, clamping negative
 *   wire desktops, and reporting malformed JSON via errorString
 */

#include <QTest>
#include <QRect>
#include <QVector>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/utils/geometryutils.h"
#include "core/types/types.h"

using namespace PlasmaZones;

class TestGeometryUtilsSerialization : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void test_rectToJson_validRect()
    {
        QRect rect(10, 20, 300, 400);
        QString json = GeometryUtils::rectToJson(rect);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isObject());

        QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QLatin1String("x")).toInt(), 10);
        QCOMPARE(obj.value(QLatin1String("y")).toInt(), 20);
        QCOMPARE(obj.value(QLatin1String("width")).toInt(), 300);
        QCOMPARE(obj.value(QLatin1String("height")).toInt(), 400);
    }

    void test_rectToJson_emptyRect()
    {
        // Default QRect() has zero width/height (Qt considers it invalid);
        // rectToJson serializes those extents as 0.
        QRect rect;
        QString json = GeometryUtils::rectToJson(rect);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isObject());

        QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QLatin1String("x")).toInt(), 0);
        QCOMPARE(obj.value(QLatin1String("y")).toInt(), 0);
        QCOMPARE(obj.value(QLatin1String("width")).toInt(), 0);
        QCOMPARE(obj.value(QLatin1String("height")).toInt(), 0);
    }

    void test_rectToJson_negativeCoordinates()
    {
        QRect rect(-50, -100, 200, 150);
        QString json = GeometryUtils::rectToJson(rect);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QLatin1String("x")).toInt(), -50);
        QCOMPARE(obj.value(QLatin1String("y")).toInt(), -100);
        QCOMPARE(obj.value(QLatin1String("width")).toInt(), 200);
        QCOMPARE(obj.value(QLatin1String("height")).toInt(), 150);
    }

    void test_serializeZoneAssignments_empty()
    {
        QVector<ZoneAssignmentEntry> entries;
        QString result = GeometryUtils::serializeZoneAssignments(entries);
        QCOMPARE(result, QStringLiteral("[]"));
    }

    void test_serializeZoneAssignments_single()
    {
        QVector<ZoneAssignmentEntry> entries;
        entries.append(ZoneAssignmentEntry{
            QStringLiteral("win-abc"),
            QStringLiteral("zone-src-1"),
            QStringLiteral("zone-tgt-1"),
            {},
            QRect(100, 200, 500, 600),
            QString(),
        });

        QString json = GeometryUtils::serializeZoneAssignments(entries);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isArray());

        QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), 1);

        QJsonObject obj = arr[0].toObject();
        QCOMPARE(obj.value(QLatin1String("windowId")).toString(), QStringLiteral("win-abc"));
        QCOMPARE(obj.value(QLatin1String("sourceZoneId")).toString(), QStringLiteral("zone-src-1"));
        QCOMPARE(obj.value(QLatin1String("targetZoneId")).toString(), QStringLiteral("zone-tgt-1"));
        QCOMPARE(obj.value(QLatin1String("x")).toInt(), 100);
        QCOMPARE(obj.value(QLatin1String("y")).toInt(), 200);
        QCOMPARE(obj.value(QLatin1String("width")).toInt(), 500);
        QCOMPARE(obj.value(QLatin1String("height")).toInt(), 600);
    }

    void test_serializeZoneAssignments_multiple()
    {
        QVector<ZoneAssignmentEntry> entries;
        entries.append(ZoneAssignmentEntry{
            QStringLiteral("win-1"),
            QStringLiteral("src-A"),
            QStringLiteral("tgt-B"),
            {},
            QRect(0, 0, 960, 1080),
            QString(),
        });
        entries.append(ZoneAssignmentEntry{
            QStringLiteral("win-2"),
            QStringLiteral("src-B"),
            QStringLiteral("tgt-C"),
            {},
            QRect(960, 0, 960, 540),
            QString(),
        });
        entries.append(ZoneAssignmentEntry{
            QStringLiteral("win-3"),
            QStringLiteral("src-C"),
            QStringLiteral("tgt-A"),
            {},
            QRect(960, 540, 960, 540),
            QString(),
        });

        QString json = GeometryUtils::serializeZoneAssignments(entries);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());

        QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), 3);

        // Verify order is preserved
        QCOMPARE(arr[0].toObject().value(QLatin1String("windowId")).toString(), QStringLiteral("win-1"));
        QCOMPARE(arr[1].toObject().value(QLatin1String("windowId")).toString(), QStringLiteral("win-2"));
        QCOMPARE(arr[2].toObject().value(QLatin1String("windowId")).toString(), QStringLiteral("win-3"));

        // Verify geometry of last entry
        QJsonObject third = arr[2].toObject();
        QCOMPARE(third.value(QLatin1String("x")).toInt(), 960);
        QCOMPARE(third.value(QLatin1String("y")).toInt(), 540);
        QCOMPARE(third.value(QLatin1String("width")).toInt(), 960);
        QCOMPARE(third.value(QLatin1String("height")).toInt(), 540);
    }

    void test_serializeZoneAssignments_emptyStrings()
    {
        QVector<ZoneAssignmentEntry> entries;
        entries.append(ZoneAssignmentEntry{
            QString(),
            QString(),
            QString(),
            {},
            QRect(10, 20, 30, 40),
            QString(),
        });

        QString json = GeometryUtils::serializeZoneAssignments(entries);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 1);

        QJsonObject obj = doc.array()[0].toObject();
        QCOMPARE(obj.value(QLatin1String("windowId")).toString(), QString());
        QCOMPARE(obj.value(QLatin1String("sourceZoneId")).toString(), QString());
        QCOMPARE(obj.value(QLatin1String("targetZoneId")).toString(), QString());
        // Geometry should still be correct
        QCOMPARE(obj.value(QLatin1String("x")).toInt(), 10);
        QCOMPARE(obj.value(QLatin1String("y")).toInt(), 20);
        QCOMPARE(obj.value(QLatin1String("width")).toInt(), 30);
        QCOMPARE(obj.value(QLatin1String("height")).toInt(), 40);
    }

    void test_serializeZoneAssignments_roundTrip()
    {
        // Build original entries
        QVector<ZoneAssignmentEntry> original;
        original.append(ZoneAssignmentEntry{
            QStringLiteral("org.kde.konsole|a1b2c3d4"),
            QStringLiteral("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"),
            QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
            {},
            QRect(0, 0, 960, 1080),
            QString(),
        });
        original.append(ZoneAssignmentEntry{
            QStringLiteral("org.kde.dolphin|f5e6d7c8"),
            QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
            QStringLiteral("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"),
            {},
            QRect(960, 0, 960, 1080),
            QString(),
        });

        // Serialize
        QString json = GeometryUtils::serializeZoneAssignments(original);

        // Parse back
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());
        QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), original.size());

        // Verify each entry round-trips correctly
        for (int i = 0; i < original.size(); ++i) {
            QJsonObject obj = arr[i].toObject();
            const ZoneAssignmentEntry& expected = original[i];

            QCOMPARE(obj.value(QLatin1String("windowId")).toString(), expected.windowId);
            QCOMPARE(obj.value(QLatin1String("sourceZoneId")).toString(), expected.sourceZoneId);
            QCOMPARE(obj.value(QLatin1String("targetZoneId")).toString(), expected.targetZoneId);
            QCOMPARE(obj.value(QLatin1String("x")).toInt(), expected.targetGeometry.x());
            QCOMPARE(obj.value(QLatin1String("y")).toInt(), expected.targetGeometry.y());
            QCOMPARE(obj.value(QLatin1String("width")).toInt(), expected.targetGeometry.width());
            QCOMPARE(obj.value(QLatin1String("height")).toInt(), expected.targetGeometry.height());
        }
    }

    void test_serializeZoneAssignments_virtualDesktop()
    {
        // A pinned desktop (>0) must survive the wire format so the batch
        // commit on the receiving side records the assignment on the window's
        // own desktop; 0 (current-desktop default) is omitted from the JSON.
        ZoneAssignmentEntry pinned;
        pinned.windowId = QStringLiteral("win-1");
        pinned.targetZoneId = QStringLiteral("tgt-A");
        pinned.targetGeometry = QRect(0, 0, 100, 100);
        pinned.virtualDesktop = 2;

        ZoneAssignmentEntry unpinned;
        unpinned.windowId = QStringLiteral("win-2");
        unpinned.targetZoneId = QStringLiteral("tgt-B");
        unpinned.targetGeometry = QRect(100, 0, 100, 100);

        QString json = GeometryUtils::serializeZoneAssignments({pinned, unpinned});

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());
        QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), 2);
        QCOMPARE(arr[0].toObject().value(QLatin1String("virtualDesktop")).toInt(), 2);
        QVERIFY(!arr[1].toObject().contains(QLatin1String("virtualDesktop")));
    }

    void test_deserializeZoneAssignments_roundTrip()
    {
        // The full serializer/deserializer pair: every field including the
        // pinned desktop must survive the wire in both directions. This is
        // the parse handleBatchedResnap runs on the receiving side, so it is
        // the load-bearing half of the cross-desktop leak fix.
        ZoneAssignmentEntry pinned;
        pinned.windowId = QStringLiteral("win-1");
        pinned.sourceZoneId = QStringLiteral("src-A");
        pinned.targetZoneId = QStringLiteral("tgt-A");
        pinned.targetZoneIds = {QStringLiteral("tgt-A"), QStringLiteral("tgt-B")};
        pinned.targetGeometry = QRect(10, 20, 300, 400);
        pinned.targetScreenId = QStringLiteral("DP-1");
        pinned.virtualDesktop = 2;

        ZoneAssignmentEntry unpinned;
        unpinned.windowId = QStringLiteral("win-2");
        unpinned.targetZoneId = QStringLiteral("tgt-C");
        unpinned.targetGeometry = QRect(100, 0, 100, 100);

        QString errorString;
        const QVector<ZoneAssignmentEntry> parsed = GeometryUtils::deserializeZoneAssignments(
            GeometryUtils::serializeZoneAssignments({pinned, unpinned}), &errorString);

        QVERIFY(errorString.isEmpty());
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed[0].windowId, pinned.windowId);
        QCOMPARE(parsed[0].sourceZoneId, pinned.sourceZoneId);
        QCOMPARE(parsed[0].targetZoneId, pinned.targetZoneId);
        QCOMPARE(parsed[0].targetZoneIds, pinned.targetZoneIds);
        QCOMPARE(parsed[0].targetGeometry, pinned.targetGeometry);
        QCOMPARE(parsed[0].targetScreenId, pinned.targetScreenId);
        QCOMPARE(parsed[0].virtualDesktop, 2);
        // Unpinned entry: both optional keys are omitted on the wire and parse
        // back to their defaults (no screen stamp, current desktop).
        QCOMPARE(parsed[1].targetScreenId, QString());
        QCOMPARE(parsed[1].virtualDesktop, 0);
        QCOMPARE(parsed[1].targetGeometry, unpinned.targetGeometry);
    }

    void test_deserializeZoneAssignments_dropsInvalidAndClampsDesktop()
    {
        // Entries missing a windowId or targetZoneId are dropped; a negative
        // wire desktop clamps to 0 (the current-desktop default).
        const QString json = QStringLiteral(
            "["
            "{\"windowId\":\"\",\"targetZoneId\":\"z\",\"x\":0,\"y\":0,\"width\":1,\"height\":1},"
            "{\"windowId\":\"w\",\"targetZoneId\":\"\",\"x\":0,\"y\":0,\"width\":1,\"height\":1},"
            "{\"windowId\":\"w\",\"targetZoneId\":\"z\",\"x\":0,\"y\":0,\"width\":1,\"height\":1,"
            "\"virtualDesktop\":-3}"
            "]");

        QString errorString;
        const QVector<ZoneAssignmentEntry> parsed = GeometryUtils::deserializeZoneAssignments(json, &errorString);

        QVERIFY(errorString.isEmpty());
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed[0].windowId, QStringLiteral("w"));
        QCOMPARE(parsed[0].virtualDesktop, 0);
    }

    void test_deserializeZoneAssignments_malformedJson()
    {
        QString errorString;
        QVERIFY(GeometryUtils::deserializeZoneAssignments(QStringLiteral("not json"), &errorString).isEmpty());
        QVERIFY(!errorString.isEmpty());

        // A non-array payload is also rejected with a diagnostic.
        QVERIFY(
            GeometryUtils::deserializeZoneAssignments(QStringLiteral("{\"windowId\":\"w\"}"), &errorString).isEmpty());
        QVERIFY(!errorString.isEmpty());

        // An empty array is a legitimate empty batch, not an error.
        QVERIFY(GeometryUtils::deserializeZoneAssignments(QStringLiteral("[]"), &errorString).isEmpty());
        QVERIFY(errorString.isEmpty());
    }

    void test_rectToJson_roundTrip()
    {
        QRect original(123, 456, 789, 1011);
        QString json = GeometryUtils::rectToJson(original);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QJsonObject obj = doc.object();

        QRect reconstructed(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                            obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());

        QCOMPARE(reconstructed, original);
    }
};

QTEST_GUILESS_MAIN(TestGeometryUtilsSerialization)
#include "test_geometry_utils_serialization.moc"
