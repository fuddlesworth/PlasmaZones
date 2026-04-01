// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtualscreen_staged.cpp
 * @brief Unit tests for virtual screen staged config flow and JSON serialization
 *
 * Tests cover:
 * 1. Staging pattern: store, query, retrieval (mirrors SettingsController API)
 * 2. Staging empty list represents removal
 * 3. JSON round-trip for D-Bus transport (VirtualScreenConfig <-> JSON)
 * 4. JSON deserialization of invalid/malformed input
 *
 * The SettingsController depends on D-Bus and Kirigami, so these tests
 * exercise the same data flow patterns at the data model level.
 */

#include <QTest>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

#include "core/virtualscreen.h"
#include "../helpers/VirtualScreenTestHelpers.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::makeDef;

class TestVirtualScreenStaged : public QObject
{
    Q_OBJECT

private:
    /// Simulate SettingsController::stageVirtualScreenConfig
    static void stageConfig(QHash<QString, QVariantList>& staged, const QString& physId, const QVariantList& screens)
    {
        staged.insert(physId, screens);
    }

    /// Simulate SettingsController::stageVirtualScreenRemoval
    static void stageRemoval(QHash<QString, QVariantList>& staged, const QString& physId)
    {
        staged.insert(physId, QVariantList()); // empty = remove
    }

    /// Simulate SettingsController::hasUnsavedVirtualScreenConfig
    static bool hasStaged(const QHash<QString, QVariantList>& staged, const QString& physId)
    {
        return staged.contains(physId);
    }

    /// Simulate SettingsController::getStagedVirtualScreenConfig
    static QVariantList getStaged(const QHash<QString, QVariantList>& staged, const QString& physId)
    {
        return staged.value(physId);
    }

    /// Serialize a VirtualScreenConfig to JSON (mirrors ScreenAdaptor::getVirtualScreenConfig)
    static QString configToJson(const VirtualScreenConfig& config)
    {
        QJsonObject root;
        root[QLatin1String("physicalScreenId")] = config.physicalScreenId;

        QJsonArray screensArr;
        for (const auto& vs : config.screens) {
            QJsonObject screenObj;
            screenObj[QLatin1String("id")] = vs.id;
            screenObj[QLatin1String("index")] = vs.index;
            screenObj[QLatin1String("displayName")] = vs.displayName;
            screenObj[QLatin1String("region")] = QJsonObject{{QLatin1String("x"), vs.region.x()},
                                                             {QLatin1String("y"), vs.region.y()},
                                                             {QLatin1String("width"), vs.region.width()},
                                                             {QLatin1String("height"), vs.region.height()}};
            screensArr.append(screenObj);
        }
        root[QLatin1String("screens")] = screensArr;

        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    /// Deserialize a VirtualScreenConfig from JSON (mirrors ScreenAdaptor::setVirtualScreenConfig)
    static VirtualScreenConfig configFromJson(const QString& physId, const QString& json)
    {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            return {};
        }

        QJsonObject root = doc.object();
        QJsonArray screensArr = root[QLatin1String("screens")].toArray();

        VirtualScreenConfig config;
        config.physicalScreenId = physId;

        for (const auto& entry : screensArr) {
            QJsonObject screenObj = entry.toObject();
            QJsonObject regionObj = screenObj[QLatin1String("region")].toObject();

            VirtualScreenDef def;
            def.index = screenObj[QLatin1String("index")].toInt();
            def.id = VirtualScreenId::make(physId, def.index);
            def.physicalScreenId = physId;
            def.displayName = screenObj[QLatin1String("displayName")].toString();
            def.region =
                QRectF(regionObj[QLatin1String("x")].toDouble(), regionObj[QLatin1String("y")].toDouble(),
                       regionObj[QLatin1String("width")].toDouble(), regionObj[QLatin1String("height")].toDouble());
            config.screens.append(def);
        }

        return config;
    }

private Q_SLOTS:

    // =========================================================================
    // Staged config flow
    // =========================================================================

    void testStage_storesData()
    {
        QHash<QString, QVariantList> staged;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        QVariantList screens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Left")},
                                            {QStringLiteral("x"), 0.0},
                                            {QStringLiteral("y"), 0.0},
                                            {QStringLiteral("width"), 0.5},
                                            {QStringLiteral("height"), 1.0}},
                                QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Right")},
                                            {QStringLiteral("x"), 0.5},
                                            {QStringLiteral("y"), 0.0},
                                            {QStringLiteral("width"), 0.5},
                                            {QStringLiteral("height"), 1.0}}};

        stageConfig(staged, physId, screens);

        QVERIFY(hasStaged(staged, physId));
        QCOMPARE(getStaged(staged, physId).size(), 2);
    }

    void testStage_hasStagedReturnsFalseBeforeStaging()
    {
        QHash<QString, QVariantList> staged;
        QVERIFY(!hasStaged(staged, QStringLiteral("nonexistent")));
    }

    void testStage_getStagedReturnsStagedData()
    {
        QHash<QString, QVariantList> staged;
        const QString physId = QStringLiteral("test:screen");

        QVariantList screens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Full")},
                                            {QStringLiteral("x"), 0.0},
                                            {QStringLiteral("y"), 0.0},
                                            {QStringLiteral("width"), 1.0},
                                            {QStringLiteral("height"), 1.0}}};

        stageConfig(staged, physId, screens);

        QVariantList retrieved = getStaged(staged, physId);
        QCOMPARE(retrieved.size(), 1);
        QVariantMap first = retrieved.first().toMap();
        QCOMPARE(first.value(QStringLiteral("displayName")).toString(), QStringLiteral("Full"));
    }

    void testStage_emptyListRepresentsRemoval()
    {
        QHash<QString, QVariantList> staged;
        const QString physId = QStringLiteral("test:removal");

        stageRemoval(staged, physId);

        QVERIFY(hasStaged(staged, physId));
        QVariantList retrieved = getStaged(staged, physId);
        QVERIFY2(retrieved.isEmpty(), "Staging an empty list must represent removal");
    }

    void testStage_overwritesPreviousStage()
    {
        QHash<QString, QVariantList> staged;
        const QString physId = QStringLiteral("test:overwrite");

        QVariantList twoScreens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Left")}},
                                   QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Right")}}};
        stageConfig(staged, physId, twoScreens);
        QCOMPARE(getStaged(staged, physId).size(), 2);

        QVariantList threeScreens = {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("A")}},
                                     QVariantMap{{QStringLiteral("displayName"), QStringLiteral("B")}},
                                     QVariantMap{{QStringLiteral("displayName"), QStringLiteral("C")}}};
        stageConfig(staged, physId, threeScreens);
        QCOMPARE(getStaged(staged, physId).size(), 3);
    }

    void testStage_multiplePhysicalScreensIndependent()
    {
        QHash<QString, QVariantList> staged;
        const QString physId1 = QStringLiteral("screen:1");
        const QString physId2 = QStringLiteral("screen:2");

        stageConfig(staged, physId1, {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("A")}}});
        stageConfig(staged, physId2,
                    {QVariantMap{{QStringLiteral("displayName"), QStringLiteral("B")}},
                     QVariantMap{{QStringLiteral("displayName"), QStringLiteral("C")}}});

        QCOMPARE(getStaged(staged, physId1).size(), 1);
        QCOMPARE(getStaged(staged, physId2).size(), 2);
    }

    // =========================================================================
    // JSON serialization round-trip (D-Bus transport format)
    // =========================================================================

    void testJsonRoundTrip_twoScreenConfig()
    {
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        VirtualScreenConfig original;
        original.physicalScreenId = physId;
        original.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1)));
        original.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1)));

        QString json = configToJson(original);
        VirtualScreenConfig loaded = configFromJson(physId, json);

        QCOMPARE(loaded.physicalScreenId, original.physicalScreenId);
        QCOMPARE(loaded.screens.size(), original.screens.size());

        for (int i = 0; i < original.screens.size(); ++i) {
            QCOMPARE(loaded.screens[i].id, original.screens[i].id);
            QCOMPARE(loaded.screens[i].physicalScreenId, original.screens[i].physicalScreenId);
            QCOMPARE(loaded.screens[i].displayName, original.screens[i].displayName);
            QCOMPARE(loaded.screens[i].index, original.screens[i].index);
            QVERIFY(qAbs(loaded.screens[i].region.x() - original.screens[i].region.x()) < 1e-9);
            QVERIFY(qAbs(loaded.screens[i].region.y() - original.screens[i].region.y()) < 1e-9);
            QVERIFY(qAbs(loaded.screens[i].region.width() - original.screens[i].region.width()) < 1e-9);
            QVERIFY(qAbs(loaded.screens[i].region.height() - original.screens[i].region.height()) < 1e-9);
        }
    }

    void testJsonRoundTrip_threeScreenConfig_floatPrecision()
    {
        const QString physId = QStringLiteral("LG:27GP850:ABC123");

        VirtualScreenConfig original;
        original.physicalScreenId = physId;
        original.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.333, 1)));
        original.screens.append(makeDef(physId, 1, QStringLiteral("Center"), QRectF(0.333, 0, 0.334, 1)));
        original.screens.append(makeDef(physId, 2, QStringLiteral("Right"), QRectF(0.667, 0, 0.333, 1)));

        QString json = configToJson(original);
        VirtualScreenConfig loaded = configFromJson(physId, json);

        QCOMPARE(loaded.screens.size(), 3);

        // Verify float precision survives JSON round-trip
        QVERIFY(qAbs(loaded.screens[0].region.width() - 0.333) < 1e-9);
        QVERIFY(qAbs(loaded.screens[1].region.x() - 0.333) < 1e-9);
        QVERIFY(qAbs(loaded.screens[1].region.width() - 0.334) < 1e-9);
        QVERIFY(qAbs(loaded.screens[2].region.x() - 0.667) < 1e-9);
        QVERIFY(qAbs(loaded.screens[2].region.width() - 0.333) < 1e-9);
    }

    void testJsonRoundTrip_emptyConfig()
    {
        const QString physId = QStringLiteral("test:empty");

        VirtualScreenConfig original;
        original.physicalScreenId = physId;
        // screens is empty

        QString json = configToJson(original);
        VirtualScreenConfig loaded = configFromJson(physId, json);

        QVERIFY(loaded.screens.isEmpty());
    }

    void testJsonDeserialization_invalidJson()
    {
        VirtualScreenConfig loaded = configFromJson(QStringLiteral("test"), QStringLiteral("{invalid json}"));
        QVERIFY(loaded.screens.isEmpty());
        QVERIFY(loaded.physicalScreenId.isEmpty());
    }

    void testJsonDeserialization_missingScreensKey()
    {
        QString json = QStringLiteral(R"({"physicalScreenId":"test"})");
        VirtualScreenConfig loaded = configFromJson(QStringLiteral("test"), json);
        QVERIFY(loaded.screens.isEmpty());
    }

    void testJsonDeserialization_emptyScreensArray()
    {
        QString json = QStringLiteral(R"({"physicalScreenId":"test","screens":[]})");
        VirtualScreenConfig loaded = configFromJson(QStringLiteral("test"), json);
        QVERIFY(loaded.screens.isEmpty());
    }

    void testJsonDeserialization_missingRegionFields_defaultsToZero()
    {
        // A screen entry with no region object should default to 0,0,0,0
        QString json = QStringLiteral(R"({
            "physicalScreenId": "test",
            "screens": [{"index": 0, "displayName": "Test"}]
        })");
        VirtualScreenConfig loaded = configFromJson(QStringLiteral("test"), json);
        QCOMPARE(loaded.screens.size(), 1);
        QVERIFY(qFuzzyIsNull(loaded.screens[0].region.x()));
        QVERIFY(qFuzzyIsNull(loaded.screens[0].region.y()));
        QVERIFY(qFuzzyIsNull(loaded.screens[0].region.width()));
        QVERIFY(qFuzzyIsNull(loaded.screens[0].region.height()));
    }

    // =========================================================================
    // VirtualScreenDef::isValid (boundary testing)
    // =========================================================================

    void testIsValid_normalDef()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1));
        QVERIFY(def.isValid());
    }

    void testIsValid_fullScreen()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Full"), QRectF(0, 0, 1.0, 1.0));
        QVERIFY(def.isValid());
    }

    void testIsValid_exceedingWidth()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(0.5, 0, 0.7, 1));
        QVERIFY(!def.isValid());
    }

    void testIsValid_exceedingHeight()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(0, 0.5, 1, 0.7));
        QVERIFY(!def.isValid());
    }

    void testIsValid_negativeX()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(-0.1, 0, 0.5, 1));
        QVERIFY(!def.isValid());
    }

    void testIsValid_zeroWidth()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(0, 0, 0, 1));
        QVERIFY(!def.isValid());
    }

    void testIsValid_emptyId()
    {
        VirtualScreenDef def;
        def.region = QRectF(0, 0, 0.5, 1);
        // id is empty
        QVERIFY(!def.isValid());
    }

    void testIsValid_negativeWidth()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(0.0, 0.0, -0.5, 1.0));
        QVERIFY(!def.isValid());
    }

    void testIsValid_negativeHeight()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(0.0, 0.0, 0.5, -1.0));
        QVERIFY(!def.isValid());
    }

    void testIsValid_zeroHeight()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(0.0, 0.0, 0.5, 0.0));
        QVERIFY(!def.isValid());
    }

    void testIsValid_negativeY()
    {
        VirtualScreenDef def = makeDef(QStringLiteral("phys"), 0, QStringLiteral("Bad"), QRectF(0.0, -0.1, 0.5, 1.0));
        QVERIFY(!def.isValid());
    }
};

QTEST_GUILESS_MAIN(TestVirtualScreenStaged)
#include "test_virtualscreen_staged.moc"
