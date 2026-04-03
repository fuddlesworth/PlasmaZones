// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "../../../src/config/configbackend_json.h"
#include "../../../src/config/configdefaults.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestJsonConfigBackend : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // Basic read/write
    // =========================================================================

    void testReadWriteString()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            g->writeString(QStringLiteral("Key"), QStringLiteral("Hello"));
        }
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QCOMPARE(g->readString(QStringLiteral("Key")), QStringLiteral("Hello"));
        }
    }

    void testReadWriteInt()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            g->writeInt(QStringLiteral("Count"), 42);
        }
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QCOMPARE(g->readInt(QStringLiteral("Count")), 42);
        }
    }

    void testReadWriteBool()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            g->writeBool(QStringLiteral("Enabled"), true);
            g->writeBool(QStringLiteral("Disabled"), false);
        }
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QCOMPARE(g->readBool(QStringLiteral("Enabled")), true);
            QCOMPARE(g->readBool(QStringLiteral("Disabled")), false);
        }
    }

    void testReadWriteDouble()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            g->writeDouble(QStringLiteral("Opacity"), 0.75);
        }
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QCOMPARE(g->readDouble(QStringLiteral("Opacity")), 0.75);
        }
    }

    void testReadWriteColor()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        QColor original(128, 64, 255, 200);
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            g->writeColor(QStringLiteral("Highlight"), original);
        }
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QColor read = g->readColor(QStringLiteral("Highlight"));
            QCOMPARE(read.red(), original.red());
            QCOMPARE(read.green(), original.green());
            QCOMPARE(read.blue(), original.blue());
            QCOMPARE(read.alpha(), original.alpha());
        }
    }

    void testReadColorCommaFormat()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        // Manually write comma format (as if hand-edited)
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            g->writeString(QStringLiteral("Color"), QStringLiteral("255,128,0,200"));
        }
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QColor c = g->readColor(QStringLiteral("Color"));
            QCOMPARE(c.red(), 255);
            QCOMPARE(c.green(), 128);
            QCOMPARE(c.blue(), 0);
            QCOMPARE(c.alpha(), 200);
        }
    }

    // =========================================================================
    // Default values
    // =========================================================================

    void testReadDefaultsForMissingKeys()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        auto g = backend->group(QStringLiteral("NonExistent"));
        QCOMPARE(g->readString(QStringLiteral("Missing"), QStringLiteral("fallback")), QStringLiteral("fallback"));
        QCOMPARE(g->readInt(QStringLiteral("Missing"), 99), 99);
        QCOMPARE(g->readBool(QStringLiteral("Missing"), true), true);
        QCOMPARE(g->readDouble(QStringLiteral("Missing"), 1.5), 1.5);
    }

    // =========================================================================
    // JSON native types — trigger list round-trip
    // =========================================================================

    void testWriteStringWithJsonArray_storesNative()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        const QString jsonStr = QStringLiteral("[{\"modifier\":2,\"mouseButton\":0}]");
        {
            auto g = backend->group(QStringLiteral("Activation"));
            g->writeString(QStringLiteral("Triggers"), jsonStr);
        }
        // readString should return compact JSON
        {
            auto g = backend->group(QStringLiteral("Activation"));
            QString read = g->readString(QStringLiteral("Triggers"));
            // Parse both to compare structure (formatting may differ)
            QJsonDocument expected = QJsonDocument::fromJson(jsonStr.toUtf8());
            QJsonDocument actual = QJsonDocument::fromJson(read.toUtf8());
            QCOMPARE(actual, expected);
        }
    }

    void testWriteStringWithJsonObject_storesNative()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        const QString jsonStr = QStringLiteral("{\"bsp\":{\"splitRatio\":0.5}}");
        {
            auto g = backend->group(QStringLiteral("Autotiling"));
            g->writeString(QStringLiteral("PerAlgorithm"), jsonStr);
        }
        {
            auto g = backend->group(QStringLiteral("Autotiling"));
            QString read = g->readString(QStringLiteral("PerAlgorithm"));
            QJsonDocument expected = QJsonDocument::fromJson(jsonStr.toUtf8());
            QJsonDocument actual = QJsonDocument::fromJson(read.toUtf8());
            QCOMPARE(actual, expected);
        }
    }

    // =========================================================================
    // Persistence (sync + reparse)
    // =========================================================================

    void testSyncAndReparse()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("Persist"));
            g->writeString(QStringLiteral("Key"), QStringLiteral("Value"));
        }
        backend->sync();

        // Reparse from disk
        backend->reparseConfiguration();
        {
            auto g = backend->group(QStringLiteral("Persist"));
            QCOMPARE(g->readString(QStringLiteral("Key")), QStringLiteral("Value"));
        }
    }

    void testSyncCreatesFile()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("Test"));
            g->writeString(QStringLiteral("Key"), QStringLiteral("Value"));
        }
        backend->sync();
        QVERIFY(QFile::exists(ConfigDefaults::configFilePath()));
    }

    void testSyncWritesValidJson()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("Test"));
            g->writeInt(QStringLiteral("Number"), 42);
            g->writeBool(QStringLiteral("Flag"), true);
        }
        backend->sync();

        QFile f(ConfigDefaults::configFilePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
    }

    // =========================================================================
    // Group operations
    // =========================================================================

    void testGroupList()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("Alpha"));
            g->writeString(QStringLiteral("K"), QStringLiteral("V"));
        }
        {
            auto g = backend->group(QStringLiteral("Beta"));
            g->writeInt(QStringLiteral("N"), 1);
        }
        QStringList groups = backend->groupList();
        QVERIFY(groups.contains(QStringLiteral("Alpha")));
        QVERIFY(groups.contains(QStringLiteral("Beta")));
    }

    void testDeleteGroup()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("Doomed"));
            g->writeString(QStringLiteral("K"), QStringLiteral("V"));
        }
        QVERIFY(backend->groupList().contains(QStringLiteral("Doomed")));
        backend->deleteGroup(QStringLiteral("Doomed"));
        QVERIFY(!backend->groupList().contains(QStringLiteral("Doomed")));
    }

    void testHasKeyAndDeleteKey()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("G"));
            g->writeString(QStringLiteral("Exists"), QStringLiteral("Yes"));
            QVERIFY(g->hasKey(QStringLiteral("Exists")));
            QVERIFY(!g->hasKey(QStringLiteral("Nope")));
        }
        {
            auto g = backend->group(QStringLiteral("G"));
            g->deleteKey(QStringLiteral("Exists"));
            QVERIFY(!g->hasKey(QStringLiteral("Exists")));
        }
    }

    // =========================================================================
    // Root-level keys
    // =========================================================================

    void testRootStringReadWrite()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        backend->writeRootString(QStringLiteral("RenderingBackend"), QStringLiteral("vulkan"));
        QCOMPARE(backend->readRootString(QStringLiteral("RenderingBackend")), QStringLiteral("vulkan"));
    }

    void testRemoveRootKey()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        backend->writeRootString(QStringLiteral("Key"), QStringLiteral("Value"));
        backend->removeRootKey(QStringLiteral("Key"));
        QCOMPARE(backend->readRootString(QStringLiteral("Key"), QStringLiteral("gone")), QStringLiteral("gone"));
    }

    // =========================================================================
    // Per-screen groups
    // =========================================================================

    void testPerScreenGroupReadWrite()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("ZoneSelector:eDP-1"));
            g->writeInt(QStringLiteral("Position"), 3);
            g->writeInt(QStringLiteral("MaxRows"), 5);
        }
        {
            auto g = backend->group(QStringLiteral("ZoneSelector:eDP-1"));
            QCOMPARE(g->readInt(QStringLiteral("Position")), 3);
            QCOMPARE(g->readInt(QStringLiteral("MaxRows")), 5);
        }
    }

    void testPerScreenGroupInGroupList()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("AutotileScreen:HDMI-1"));
            g->writeString(QStringLiteral("Algorithm"), QStringLiteral("bsp"));
        }
        QStringList groups = backend->groupList();
        QVERIFY(groups.contains(QStringLiteral("AutotileScreen:HDMI-1")));
    }

    void testDeletePerScreenGroup()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("SnappingScreen:DP-1"));
            g->writeBool(QStringLiteral("Enabled"), true);
        }
        backend->deleteGroup(QStringLiteral("SnappingScreen:DP-1"));
        QVERIFY(!backend->groupList().contains(QStringLiteral("SnappingScreen:DP-1")));
    }

    // =========================================================================
    // readJsonConfigFromDisk (flat map for WindowTrackingAdaptor)
    // =========================================================================

    void testReadJsonConfigFromDisk_flatMap()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("Behavior"));
            g->writeString(QStringLiteral("DefaultLayoutId"), QStringLiteral("abc-123"));
        }
        backend->sync();

        auto map = PlasmaZones::readJsonConfigFromDisk();
        QCOMPARE(map.value(QStringLiteral("Behavior/DefaultLayoutId")).toString(), QStringLiteral("abc-123"));
    }

    // =========================================================================
    // Bool string compatibility
    // =========================================================================

    void testReadBoolFromString()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("G"));
            g->writeString(QStringLiteral("A"), QStringLiteral("true"));
            g->writeString(QStringLiteral("B"), QStringLiteral("false"));
            g->writeString(QStringLiteral("C"), QStringLiteral("yes"));
            g->writeString(QStringLiteral("D"), QStringLiteral("0"));
        }
        {
            auto g = backend->group(QStringLiteral("G"));
            // Note: writeString with "true"/"false" may store as native JSON string,
            // but readBool should handle both native bool and string representations
            QCOMPARE(g->readBool(QStringLiteral("A")), true);
            QCOMPARE(g->readBool(QStringLiteral("B")), false);
            QCOMPARE(g->readBool(QStringLiteral("C")), true);
            QCOMPARE(g->readBool(QStringLiteral("D")), false);
        }
    }

    void testReadBoolFromDouble_onlyZeroAndOne()
    {
        IsolatedConfigGuard guard;
        // Write raw JSON with numeric booleans (simulates hand-edited config)
        const QString path = ConfigDefaults::configFilePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"G":{"zero":0,"one":1,"half":0.5}})");
        f.close();

        auto backend = PlasmaZones::createDefaultConfigBackend();
        auto g = backend->group(QStringLiteral("G"));
        QCOMPARE(g->readBool(QStringLiteral("zero")), false);
        QCOMPARE(g->readBool(QStringLiteral("one")), true);
        // 0.5 is not a valid boolean representation — should return default
        QCOMPARE(g->readBool(QStringLiteral("half"), false), false);
        QCOMPARE(g->readBool(QStringLiteral("half"), true), true);
    }

    // =========================================================================
    // Corrupt JSON recovery
    // =========================================================================

    void testCorruptJsonFile_loadsEmpty()
    {
        IsolatedConfigGuard guard;
        // Write corrupt JSON to disk
        const QString path = ConfigDefaults::configFilePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{this is not valid json!!!");
        f.close();

        // Backend should load with empty root (not crash or throw)
        auto backend = PlasmaZones::createDefaultConfigBackend();
        auto g = backend->group(QStringLiteral("TestGroup"));
        QCOMPARE(g->readString(QStringLiteral("Missing"), QStringLiteral("default")), QStringLiteral("default"));
    }

    void testCorruptJsonFile_reparseRecovery()
    {
        IsolatedConfigGuard guard;
        const QString path = ConfigDefaults::configFilePath();
        QDir().mkpath(QFileInfo(path).absolutePath());

        // Write valid JSON first
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"TestGroup":{"Key":"Value"}})");
        }

        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QCOMPARE(g->readString(QStringLiteral("Key")), QStringLiteral("Value"));
        }

        // Now corrupt the file on disk
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("CORRUPT!");
        }

        // reparseConfiguration should reset to empty (not crash)
        backend->reparseConfiguration();
        {
            auto g = backend->group(QStringLiteral("TestGroup"));
            QCOMPARE(g->readString(QStringLiteral("Key"), QStringLiteral("gone")), QStringLiteral("gone"));
        }
    }

    // =========================================================================
    // Per-screen helpers
    // =========================================================================

    void testIsPerScreenPrefix()
    {
        QVERIFY(PlasmaZones::isPerScreenPrefix(QStringLiteral("ZoneSelector:eDP-1")));
        QVERIFY(PlasmaZones::isPerScreenPrefix(QStringLiteral("AutotileScreen:HDMI-1")));
        QVERIFY(PlasmaZones::isPerScreenPrefix(QStringLiteral("SnappingScreen:DP-2")));
        // Assignment groups are NOT per-screen
        QVERIFY(!PlasmaZones::isPerScreenPrefix(QStringLiteral("Assignment:eDP-1:Desktop:1")));
        QVERIFY(!PlasmaZones::isPerScreenPrefix(QStringLiteral("General")));
    }

    void testPrefixCategoryRoundTrip()
    {
        QCOMPARE(PlasmaZones::prefixToCategory(QStringLiteral("AutotileScreen")), QStringLiteral("Autotile"));
        QCOMPARE(PlasmaZones::prefixToCategory(QStringLiteral("SnappingScreen")), QStringLiteral("Snapping"));
        QCOMPARE(PlasmaZones::prefixToCategory(QStringLiteral("ZoneSelector")), QStringLiteral("ZoneSelector"));

        QCOMPARE(PlasmaZones::categoryToPrefix(QStringLiteral("Autotile")), QStringLiteral("AutotileScreen"));
        QCOMPARE(PlasmaZones::categoryToPrefix(QStringLiteral("Snapping")), QStringLiteral("SnappingScreen"));
        QCOMPARE(PlasmaZones::categoryToPrefix(QStringLiteral("ZoneSelector")), QStringLiteral("ZoneSelector"));
    }
};

QTEST_MAIN(TestJsonConfigBackend)
#include "test_configbackend_json.moc"
