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

    // =========================================================================
    // Dot-path group operations
    // =========================================================================

    void testDotPathDeleteGroup_prunesEmptyParents()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        // Write a value into a deeply nested dot-path group
        {
            auto g = backend->group(QStringLiteral("A.B.C"));
            g->writeString(QStringLiteral("Key"), QStringLiteral("Value"));
        }
        backend->sync();

        // Verify it was written
        {
            auto g = backend->group(QStringLiteral("A.B.C"));
            QCOMPARE(g->readString(QStringLiteral("Key")), QStringLiteral("Value"));
        }

        // Delete the leaf group
        backend->deleteGroup(QStringLiteral("A.B.C"));
        backend->sync();

        // The leaf group should be gone
        QVERIFY(!backend->groupList().contains(QStringLiteral("A.B.C")));

        // Empty parents should be pruned — "A.B" and "A" should also be gone
        QVERIFY(!backend->groupList().contains(QStringLiteral("A.B")));
        QVERIFY(!backend->groupList().contains(QStringLiteral("A")));
    }

    void testDotPathDeleteGroup_preservesSiblings()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        // Write values into sibling dot-path groups
        {
            auto g = backend->group(QStringLiteral("Parent.Child1"));
            g->writeString(QStringLiteral("Key1"), QStringLiteral("V1"));
        }
        {
            auto g = backend->group(QStringLiteral("Parent.Child2"));
            g->writeString(QStringLiteral("Key2"), QStringLiteral("V2"));
        }
        backend->sync();

        // Delete one child
        backend->deleteGroup(QStringLiteral("Parent.Child1"));

        // Sibling should survive
        {
            auto g = backend->group(QStringLiteral("Parent.Child2"));
            QCOMPARE(g->readString(QStringLiteral("Key2")), QStringLiteral("V2"));
        }
        // Parent should survive (not empty — still has Child2)
        QVERIFY(backend->groupList().contains(QStringLiteral("Parent")));
        QVERIFY(backend->groupList().contains(QStringLiteral("Parent.Child2")));
    }

    void testGroupList_returnsNestedDotPaths()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        // Write into nested groups
        {
            auto g = backend->group(QStringLiteral("Snapping"));
            g->writeBool(QStringLiteral("Enabled"), true);
        }
        {
            auto g = backend->group(QStringLiteral("Snapping.Behavior.ZoneSpan"));
            g->writeBool(QStringLiteral("Enabled"), true);
        }
        {
            auto g = backend->group(QStringLiteral("Snapping.Gaps"));
            g->writeInt(QStringLiteral("Inner"), 8);
        }
        backend->sync();

        const QStringList groups = backend->groupList();

        // Should contain the intermediate and leaf groups
        QVERIFY(groups.contains(QStringLiteral("Snapping")));
        QVERIFY(groups.contains(QStringLiteral("Snapping.Behavior")));
        QVERIFY(groups.contains(QStringLiteral("Snapping.Behavior.ZoneSpan")));
        QVERIFY(groups.contains(QStringLiteral("Snapping.Gaps")));
    }

    // =========================================================================
    // Dot-path depth limit
    // =========================================================================

    void testDotPathDepthLimit_withinLimit()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        // 8 segments is at the limit — should work fine
        const QString deepGroup = QStringLiteral("A.B.C.D.E.F.G.H");
        {
            auto g = backend->group(deepGroup);
            g->writeString(QStringLiteral("Key"), QStringLiteral("value"));
        }
        {
            auto g = backend->group(deepGroup);
            QCOMPARE(g->readString(QStringLiteral("Key")), QStringLiteral("value"));
        }
    }

    void testDotPathDepthLimit_exceedsLimit_rejectsWrite()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        // 10 segments exceeds MaxDotPathDepth=8 — should be rejected (no-op)
        const QString tooDeep = QStringLiteral("A.B.C.D.E.F.G.H.I.J");
        {
            auto g = backend->group(tooDeep);
            g->writeString(QStringLiteral("Key"), QStringLiteral("value"));
        }
        // The write should have been rejected — nothing stored at any path
        {
            auto g = backend->group(QStringLiteral("A.B.C.D.E.F.G.H"));
            QCOMPARE(g->readString(QStringLiteral("Key"), QStringLiteral("missing")), QStringLiteral("missing"));
        }
        {
            auto g = backend->group(tooDeep);
            QCOMPARE(g->readString(QStringLiteral("Key"), QStringLiteral("missing")), QStringLiteral("missing"));
        }
    }

    // =========================================================================
    // Dot-path sync + reparse round-trip
    // =========================================================================

    void testDotPathSyncReparse_roundTrip()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        // Write to a dot-path group
        {
            auto g = backend->group(QStringLiteral("Snapping.Behavior.ZoneSpan"));
            g->writeBool(QStringLiteral("Enabled"), true);
            g->writeString(QStringLiteral("Triggers"), QStringLiteral("[{\"modifier\":1,\"mouseButton\":0}]"));
        }
        backend->sync();

        // Reparse from disk
        backend->reparseConfiguration();

        // Verify values survived the round-trip
        {
            auto g = backend->group(QStringLiteral("Snapping.Behavior.ZoneSpan"));
            QCOMPARE(g->readBool(QStringLiteral("Enabled"), false), true);
            QVERIFY(g->readString(QStringLiteral("Triggers")).contains(QStringLiteral("modifier")));
        }
    }

    // =========================================================================
    // readString handles non-string JSON types
    // =========================================================================

    void testReadString_boolValue_convertsToString()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        {
            auto g = backend->group(QStringLiteral("Test"));
            g->writeBool(QStringLiteral("Flag"), true);
        }
        {
            auto g = backend->group(QStringLiteral("Test"));
            // readString on a bool should return "true", not the default
            QCOMPARE(g->readString(QStringLiteral("Flag"), QStringLiteral("fallback")), QStringLiteral("true"));
        }
    }

    void testReadString_numberValue_convertsToString()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        {
            auto g = backend->group(QStringLiteral("Test"));
            g->writeInt(QStringLiteral("Count"), 42);
        }
        {
            auto g = backend->group(QStringLiteral("Test"));
            QCOMPARE(g->readString(QStringLiteral("Count"), QStringLiteral("fallback")), QStringLiteral("42"));
        }
    }
    // =========================================================================
    // writeString: non-JSON bracket string stays as string
    // =========================================================================

    void testWriteString_bracketNonJson_staysAsString()
    {
        IsolatedConfigGuard guard;
        auto backend = PlasmaZones::createDefaultConfigBackend();

        // "[Main Monitor]" starts with '[' but is not valid JSON — must stay as string
        const QString value = QStringLiteral("[Main Monitor]");
        {
            auto g = backend->group(QStringLiteral("Test"));
            g->writeString(QStringLiteral("Monitor"), value);
        }
        {
            auto g = backend->group(QStringLiteral("Test"));
            QCOMPARE(g->readString(QStringLiteral("Monitor")), value);
        }
    }
};

QTEST_MAIN(TestJsonConfigBackend)
#include "test_configbackend_json.moc"
