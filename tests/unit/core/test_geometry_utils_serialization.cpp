// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_geometry_utils_serialization.cpp
 * @brief Unit tests for GeometryUtils::rectToJson() and serializeZoneAssignments()
 *
 * Tests cover:
 * - rectToJson with valid and empty/invalid QRects
 * - serializeZoneAssignments with empty, single, and multiple entries
 * - Entries with empty string fields
 * - Round-trip: serialize then parse back and verify all fields match
 */

#include <QTest>
#include <QRect>
#include <QVector>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/geometryutils.h"
#include "core/types.h"

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
        QCOMPARE(obj.value(QStringLiteral("x")).toInt(), 10);
        QCOMPARE(obj.value(QStringLiteral("y")).toInt(), 20);
        QCOMPARE(obj.value(QStringLiteral("width")).toInt(), 300);
        QCOMPARE(obj.value(QStringLiteral("height")).toInt(), 400);
    }

    void test_rectToJson_emptyRect()
    {
        // Default QRect is (0,0,0,0) which Qt considers invalid
        QRect rect;
        QString json = GeometryUtils::rectToJson(rect);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isObject());

        QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("x")).toInt(), 0);
        QCOMPARE(obj.value(QStringLiteral("y")).toInt(), 0);
        QCOMPARE(obj.value(QStringLiteral("width")).toInt(), 0);
        QCOMPARE(obj.value(QStringLiteral("height")).toInt(), 0);
    }

    void test_rectToJson_negativeCoordinates()
    {
        QRect rect(-50, -100, 200, 150);
        QString json = GeometryUtils::rectToJson(rect);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("x")).toInt(), -50);
        QCOMPARE(obj.value(QStringLiteral("y")).toInt(), -100);
        QCOMPARE(obj.value(QStringLiteral("width")).toInt(), 200);
        QCOMPARE(obj.value(QStringLiteral("height")).toInt(), 150);
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
        });

        QString json = GeometryUtils::serializeZoneAssignments(entries);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isArray());

        QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), 1);

        QJsonObject obj = arr[0].toObject();
        QCOMPARE(obj.value(QStringLiteral("windowId")).toString(), QStringLiteral("win-abc"));
        QCOMPARE(obj.value(QStringLiteral("sourceZoneId")).toString(), QStringLiteral("zone-src-1"));
        QCOMPARE(obj.value(QStringLiteral("targetZoneId")).toString(), QStringLiteral("zone-tgt-1"));
        QCOMPARE(obj.value(QStringLiteral("x")).toInt(), 100);
        QCOMPARE(obj.value(QStringLiteral("y")).toInt(), 200);
        QCOMPARE(obj.value(QStringLiteral("width")).toInt(), 500);
        QCOMPARE(obj.value(QStringLiteral("height")).toInt(), 600);
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
        });
        entries.append(ZoneAssignmentEntry{
            QStringLiteral("win-2"),
            QStringLiteral("src-B"),
            QStringLiteral("tgt-C"),
            {},
            QRect(960, 0, 960, 540),
        });
        entries.append(ZoneAssignmentEntry{
            QStringLiteral("win-3"),
            QStringLiteral("src-C"),
            QStringLiteral("tgt-A"),
            {},
            QRect(960, 540, 960, 540),
        });

        QString json = GeometryUtils::serializeZoneAssignments(entries);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());

        QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), 3);

        // Verify order is preserved
        QCOMPARE(arr[0].toObject().value(QStringLiteral("windowId")).toString(), QStringLiteral("win-1"));
        QCOMPARE(arr[1].toObject().value(QStringLiteral("windowId")).toString(), QStringLiteral("win-2"));
        QCOMPARE(arr[2].toObject().value(QStringLiteral("windowId")).toString(), QStringLiteral("win-3"));

        // Verify geometry of last entry
        QJsonObject third = arr[2].toObject();
        QCOMPARE(third.value(QStringLiteral("x")).toInt(), 960);
        QCOMPARE(third.value(QStringLiteral("y")).toInt(), 540);
        QCOMPARE(third.value(QStringLiteral("width")).toInt(), 960);
        QCOMPARE(third.value(QStringLiteral("height")).toInt(), 540);
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
        });

        QString json = GeometryUtils::serializeZoneAssignments(entries);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 1);

        QJsonObject obj = doc.array()[0].toObject();
        QCOMPARE(obj.value(QStringLiteral("windowId")).toString(), QString());
        QCOMPARE(obj.value(QStringLiteral("sourceZoneId")).toString(), QString());
        QCOMPARE(obj.value(QStringLiteral("targetZoneId")).toString(), QString());
        // Geometry should still be correct
        QCOMPARE(obj.value(QStringLiteral("x")).toInt(), 10);
        QCOMPARE(obj.value(QStringLiteral("y")).toInt(), 20);
        QCOMPARE(obj.value(QStringLiteral("width")).toInt(), 30);
        QCOMPARE(obj.value(QStringLiteral("height")).toInt(), 40);
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
        });
        original.append(ZoneAssignmentEntry{
            QStringLiteral("org.kde.dolphin|f5e6d7c8"),
            QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
            QStringLiteral("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"),
            {},
            QRect(960, 0, 960, 1080),
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

            QCOMPARE(obj.value(QStringLiteral("windowId")).toString(), expected.windowId);
            QCOMPARE(obj.value(QStringLiteral("sourceZoneId")).toString(), expected.sourceZoneId);
            QCOMPARE(obj.value(QStringLiteral("targetZoneId")).toString(), expected.targetZoneId);
            QCOMPARE(obj.value(QStringLiteral("x")).toInt(), expected.targetGeometry.x());
            QCOMPARE(obj.value(QStringLiteral("y")).toInt(), expected.targetGeometry.y());
            QCOMPARE(obj.value(QStringLiteral("width")).toInt(), expected.targetGeometry.width());
            QCOMPARE(obj.value(QStringLiteral("height")).toInt(), expected.targetGeometry.height());
        }
    }

    void test_rectToJson_roundTrip()
    {
        QRect original(123, 456, 789, 1011);
        QString json = GeometryUtils::rectToJson(original);

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QJsonObject obj = doc.object();

        QRect reconstructed(obj.value(QStringLiteral("x")).toInt(), obj.value(QStringLiteral("y")).toInt(),
                            obj.value(QStringLiteral("width")).toInt(), obj.value(QStringLiteral("height")).toInt());

        QCOMPARE(reconstructed, original);
    }
};

QTEST_GUILESS_MAIN(TestGeometryUtilsSerialization)
#include "test_geometry_utils_serialization.moc"
