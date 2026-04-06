// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>

#include "../../../src/config/configmigration.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configbackend_json.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestConfigMigration : public QObject
{
    Q_OBJECT

private:
    void writeIniFile(const QString& path, const QString& content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << content;
    }

    QJsonObject readJsonConfig(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

private Q_SLOTS:

    // =========================================================================
    // No migration needed
    // =========================================================================

    void testFreshInstall_noFiles()
    {
        IsolatedConfigGuard guard;
        // No old config, no new config
        QVERIFY(ConfigMigration::ensureJsonConfig());
        // No JSON file created (fresh install gets defaults on first save)
        QVERIFY(!QFile::exists(ConfigDefaults::configFilePath()));
    }

    void testJsonAlreadyAtCurrentVersion()
    {
        IsolatedConfigGuard guard;
        // Create JSON config at current schema version — should be a no-op
        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QFile f(ConfigDefaults::configFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("{\"_version\":%1}").arg(PlasmaZones::ConfigSchemaVersion).toUtf8());
        f.close();

        QVERIFY(ConfigMigration::ensureJsonConfig());
    }

    void testJsonAtOlderVersion_runsMigration()
    {
        IsolatedConfigGuard guard;
        // Create JSON config at v1 — migration chain should upgrade to current version
        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QFile f(ConfigDefaults::configFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"_version\":1}");
        f.close();

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
    }

    // =========================================================================
    // Migration
    // =========================================================================

    void testMigrateBasicSettings()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Activation]\n"
                                    "SnappingEnabled=true\n"
                                    "ToggleActivation=false\n"
                                    "\n"
                                    "[Display]\n"
                                    "ShowOnAllMonitors=true\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // JSON config should exist
        QVERIFY(QFile::exists(ConfigDefaults::configFilePath()));

        // Old file should be renamed to .bak
        QVERIFY(!QFile::exists(ConfigDefaults::legacyConfigFilePath()));
        QVERIFY(QFile::exists(ConfigDefaults::legacyConfigFilePath() + QStringLiteral(".bak")));

        // Verify content
        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);

        QJsonObject snapping = root.value(QStringLiteral("Snapping")).toObject();
        QCOMPARE(snapping.value(QStringLiteral("Enabled")).toBool(), true);

        QJsonObject behavior = snapping.value(QStringLiteral("Behavior")).toObject();
        QCOMPARE(behavior.value(QStringLiteral("ToggleActivation")).toBool(), false);

        QJsonObject behaviorDisplay = behavior.value(QStringLiteral("Display")).toObject();
        QCOMPARE(behaviorDisplay.value(QStringLiteral("ShowOnAllMonitors")).toBool(), true);
    }

    void testMigrateColors()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Appearance]\n"
                                    "HighlightColor=82,148,226,255\n"
                                    "BorderColor=255,0,0,128\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject snapping = root.value(QStringLiteral("Snapping")).toObject();
        QJsonObject appearance = snapping.value(QStringLiteral("Appearance")).toObject();
        QJsonObject colors = appearance.value(QStringLiteral("Colors")).toObject();

        // Colors should be converted to hex
        QString highlight = colors.value(QStringLiteral("Highlight")).toString();
        QVERIFY2(highlight.startsWith(QLatin1Char('#')),
                 qPrintable(QStringLiteral("Expected hex color, got: ") + highlight));

        // Verify the hex parses back to the original color
        QColor c(highlight);
        QCOMPARE(c.red(), 82);
        QCOMPARE(c.green(), 148);
        QCOMPARE(c.blue(), 226);
        QCOMPARE(c.alpha(), 255);
    }

    void testMigrateJsonTriggers()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Activation]\n"
                                    "DragActivationTriggers=[{\"modifier\":2,\"mouseButton\":0}]\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject snapping = root.value(QStringLiteral("Snapping")).toObject();
        QJsonObject behavior = snapping.value(QStringLiteral("Behavior")).toObject();

        // Should be stored as native JSON array, not a string
        QJsonValue triggers = behavior.value(QStringLiteral("Triggers"));
        QVERIFY2(triggers.isArray(), "Triggers should be a native JSON array after migration");

        QJsonArray arr = triggers.toArray();
        QCOMPARE(arr.size(), 1);
        QCOMPARE(arr[0].toObject().value(QStringLiteral("modifier")).toInt(), 2);
        QCOMPARE(arr[0].toObject().value(QStringLiteral("mouseButton")).toInt(), 0);
    }

    void testMigratePerScreenGroups()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[ZoneSelector:eDP-1]\n"
                                    "Position=3\n"
                                    "MaxRows=5\n"
                                    "\n"
                                    "[AutotileScreen:HDMI-1]\n"
                                    "Algorithm=bsp\n"
                                    "\n"
                                    "[SnappingScreen:DP-2]\n"
                                    "SnapAssistEnabled=true\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject perScreen = root.value(QStringLiteral("PerScreen")).toObject();

        // ZoneSelector
        QJsonObject zs = perScreen.value(QStringLiteral("ZoneSelector")).toObject();
        QJsonObject edp = zs.value(QStringLiteral("eDP-1")).toObject();
        QCOMPARE(edp.value(QStringLiteral("Position")).toInt(), 3);
        QCOMPARE(edp.value(QStringLiteral("MaxRows")).toInt(), 5);

        // Autotile
        QJsonObject at = perScreen.value(QStringLiteral("Autotile")).toObject();
        QJsonObject hdmi = at.value(QStringLiteral("HDMI-1")).toObject();
        QCOMPARE(hdmi.value(QStringLiteral("Algorithm")).toString(), QStringLiteral("bsp"));

        // Snapping
        QJsonObject sn = perScreen.value(QStringLiteral("Snapping")).toObject();
        QJsonObject dp2 = sn.value(QStringLiteral("DP-2")).toObject();
        QCOMPARE(dp2.value(QStringLiteral("SnapAssistEnabled")).toBool(), true);
    }

    void testMigrateAssignmentGroups_notPerScreen()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Assignment:eDP-1:Desktop:1:Activity:abc-123]\n"
                                    "Mode=0\n"
                                    "SnappingLayout={uuid-here}\n"
                                    "TilingAlgorithm=bsp\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());

        // Assignment groups must NOT end up under PerScreen
        QVERIFY2(!root.contains(QStringLiteral("PerScreen")), "Assignment groups should not be under PerScreen");

        // They should be a regular top-level group
        const QString groupName = QStringLiteral("Assignment:eDP-1:Desktop:1:Activity:abc-123");
        QVERIFY2(root.contains(groupName), "Assignment group should be at root level");

        QJsonObject assignment = root.value(groupName).toObject();
        QCOMPARE(assignment.value(QStringLiteral("Mode")).toInt(), 0);
        QCOMPARE(assignment.value(QStringLiteral("TilingAlgorithm")).toString(), QStringLiteral("bsp"));
    }

    void testMigrateAssignmentGroups_readableByBackend()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Assignment:eDP-1:Desktop:1]\n"
                                    "Mode=1\n"
                                    "TilingAlgorithm=dwindle\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        auto backend = PlasmaZones::createDefaultConfigBackend();
        // groupList should contain the assignment group
        QStringList groups = backend->groupList();
        QVERIFY(groups.contains(QStringLiteral("Assignment:eDP-1:Desktop:1")));

        // Reading via group() should work
        auto g = backend->group(QStringLiteral("Assignment:eDP-1:Desktop:1"));
        QCOMPARE(g->readInt(QStringLiteral("Mode")), 1);
        QCOMPARE(g->readString(QStringLiteral("TilingAlgorithm")), QStringLiteral("dwindle"));
    }

    void testMigrateNumericValues()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Zones]\n"
                                    "Padding=8\n"
                                    "OuterGap=4\n"
                                    "\n"
                                    "[Appearance]\n"
                                    "ActiveOpacity=0.3\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject snapping = root.value(QStringLiteral("Snapping")).toObject();
        QJsonObject gaps = snapping.value(QStringLiteral("Gaps")).toObject();
        QCOMPARE(gaps.value(QStringLiteral("Inner")).toInt(), 8);
        QCOMPARE(gaps.value(QStringLiteral("Outer")).toInt(), 4);

        QJsonObject appearance = snapping.value(QStringLiteral("Appearance")).toObject();
        QJsonObject opacity = appearance.value(QStringLiteral("Opacity")).toObject();
        QCOMPARE(opacity.value(QStringLiteral("Active")).toDouble(), 0.3);
    }

    void testMigrateRootLevelKeys()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("RenderingBackend=vulkan\n"
                                    "\n"
                                    "[Activation]\n"
                                    "SnappingEnabled=true\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        // RenderingBackend should be under "Rendering" group as "Backend"
        QJsonObject rendering = root.value(QStringLiteral("Rendering")).toObject();
        QCOMPARE(rendering.value(QStringLiteral("Backend")).toString(), QStringLiteral("vulkan"));
    }

    void testMigrateRenderingBackendFromGeneralGroup()
    {
        IsolatedConfigGuard guard;
        // QSettings maps [General]/RenderingBackend identically to ungrouped
        // RenderingBackend. Both must end up under the "Rendering" group.
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[General]\n"
                                    "RenderingBackend=vulkan\n"
                                    "\n"
                                    "[Activation]\n"
                                    "SnappingEnabled=true\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject rendering = root.value(QStringLiteral("Rendering")).toObject();
        QCOMPARE(rendering.value(QStringLiteral("Backend")).toString(), QStringLiteral("vulkan"));

        // Must NOT be under General
        QJsonObject general = root.value(QStringLiteral("General")).toObject();
        QVERIFY2(!general.contains(QStringLiteral("RenderingBackend")),
                 "RenderingBackend should not remain under General after migration");
    }

    // =========================================================================
    // Idempotency
    // =========================================================================

    void testMigrationDoesNotRunTwice()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Activation]\n"
                                    "SnappingEnabled=true\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());
        QVERIFY(QFile::exists(ConfigDefaults::configFilePath()));

        // Second call should be a no-op
        QVERIFY(ConfigMigration::ensureJsonConfig());
    }

    // =========================================================================
    // Round-trip with JsonConfigBackend
    // =========================================================================

    // =========================================================================
    // v1 JSON migration (not from INI)
    // =========================================================================

    void testV1JsonMigration_withActualData()
    {
        IsolatedConfigGuard guard;
        // Create a v1 JSON config with actual data (not just _version:1)
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        QJsonObject activation;
        activation[QStringLiteral("SnappingEnabled")] = true;
        activation[QStringLiteral("ToggleActivation")] = false;
        root[QStringLiteral("Activation")] = activation;

        QJsonObject display;
        display[QStringLiteral("ShowOnAllMonitors")] = true;
        root[QStringLiteral("Display")] = display;

        QJsonObject editor;
        editor[QStringLiteral("EditorDuplicateShortcut")] = QStringLiteral("Ctrl+D");
        editor[QStringLiteral("GridSnappingEnabled")] = true;
        editor[QStringLiteral("FillOnDropEnabled")] = false;
        root[QStringLiteral("Editor")] = editor;

        // Write the v1 JSON to disk
        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(JsonConfigBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        // Run migration
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // Verify v2 structure
        QJsonObject migrated = readJsonConfig(ConfigDefaults::configFilePath());
        QCOMPARE(migrated.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);

        // Activation → Snapping.Behavior + Snapping (Enabled)
        QJsonObject snapping = migrated.value(QStringLiteral("Snapping")).toObject();
        QCOMPARE(snapping.value(QStringLiteral("Enabled")).toBool(), true);
        QJsonObject behavior = snapping.value(QStringLiteral("Behavior")).toObject();
        QCOMPARE(behavior.value(QStringLiteral("ToggleActivation")).toBool(), false);

        // Display → Snapping.Behavior.Display
        QJsonObject behaviorDisplay = behavior.value(QStringLiteral("Display")).toObject();
        QCOMPARE(behaviorDisplay.value(QStringLiteral("ShowOnAllMonitors")).toBool(), true);

        // Editor → Editor.Shortcuts + Editor.Snapping + Editor.FillOnDrop
        QJsonObject editorMigrated = migrated.value(QStringLiteral("Editor")).toObject();
        QJsonObject editorShortcuts = editorMigrated.value(QStringLiteral("Shortcuts")).toObject();
        QCOMPARE(editorShortcuts.value(QStringLiteral("Duplicate")).toString(), QStringLiteral("Ctrl+D"));
        QJsonObject editorSnapping = editorMigrated.value(QStringLiteral("Snapping")).toObject();
        QCOMPARE(editorSnapping.value(QStringLiteral("GridEnabled")).toBool(), true);
        QJsonObject editorFillOnDrop = editorMigrated.value(QStringLiteral("FillOnDrop")).toObject();
        QCOMPARE(editorFillOnDrop.value(QStringLiteral("Enabled")).toBool(), false);

        // v1 groups should be removed
        QVERIFY(!migrated.contains(QStringLiteral("Activation")));
        QVERIFY(!migrated.contains(QStringLiteral("Display")));
    }

    void testDoubleMigration_idempotent()
    {
        IsolatedConfigGuard guard;
        // Create v1 JSON, migrate once, migrate again — should be unchanged
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;
        QJsonObject activation;
        activation[QStringLiteral("SnappingEnabled")] = true;
        root[QStringLiteral("Activation")] = activation;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(JsonConfigBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        // First migration
        QVERIFY(ConfigMigration::ensureJsonConfig());
        QJsonObject afterFirst = readJsonConfig(ConfigDefaults::configFilePath());

        // Second migration — should be a no-op
        QVERIFY(ConfigMigration::ensureJsonConfig());
        QJsonObject afterSecond = readJsonConfig(ConfigDefaults::configFilePath());

        QCOMPARE(afterFirst, afterSecond);
    }

    void testMigrateShortcuts_v1ToV2()
    {
        IsolatedConfigGuard guard;
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        QJsonObject globalShortcuts;
        globalShortcuts[QStringLiteral("OpenEditorShortcut")] = QStringLiteral("Meta+E");
        globalShortcuts[QStringLiteral("NextLayoutShortcut")] = QStringLiteral("Meta+N");
        root[QStringLiteral("GlobalShortcuts")] = globalShortcuts;

        QJsonObject autotileShortcuts;
        autotileShortcuts[QStringLiteral("ToggleShortcut")] = QStringLiteral("Meta+T");
        root[QStringLiteral("AutotileShortcuts")] = autotileShortcuts;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(JsonConfigBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject migrated = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject shortcuts = migrated.value(QStringLiteral("Shortcuts")).toObject();
        QJsonObject global = shortcuts.value(QStringLiteral("Global")).toObject();
        QCOMPARE(global.value(QStringLiteral("OpenEditor")).toString(), QStringLiteral("Meta+E"));
        QCOMPARE(global.value(QStringLiteral("NextLayout")).toString(), QStringLiteral("Meta+N"));

        QJsonObject tiling = shortcuts.value(QStringLiteral("Tiling")).toObject();
        QCOMPARE(tiling.value(QStringLiteral("Toggle")).toString(), QStringLiteral("Meta+T"));

        // v1 groups should be removed
        QVERIFY(!migrated.contains(QStringLiteral("GlobalShortcuts")));
        QVERIFY(!migrated.contains(QStringLiteral("AutotileShortcuts")));
    }

    // =========================================================================
    // Round-trip with JsonConfigBackend
    // =========================================================================

    void testMigratedConfigReadableByBackend()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Activation]\n"
                                    "SnappingEnabled=true\n"
                                    "\n"
                                    "[Appearance]\n"
                                    "HighlightColor=82,148,226,255\n"
                                    "ActiveOpacity=0.3\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        auto backend = PlasmaZones::createDefaultConfigBackend();
        {
            auto g = backend->group(QStringLiteral("Snapping"));
            QCOMPARE(g->readBool(QStringLiteral("Enabled")), true);
        }
        {
            auto g = backend->group(QStringLiteral("Snapping.Appearance.Colors"));
            QColor c = g->readColor(QStringLiteral("Highlight"));
            QCOMPARE(c.red(), 82);
            QCOMPARE(c.green(), 148);
            QCOMPARE(c.blue(), 226);
        }
        {
            auto g = backend->group(QStringLiteral("Snapping.Appearance.Opacity"));
            QCOMPARE(g->readDouble(QStringLiteral("Active")), 0.3);
        }
    }
};

QTEST_MAIN(TestConfigMigration)
#include "test_configmigration.moc"
