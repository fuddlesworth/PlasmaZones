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
#include "../../../src/config/configbackends.h"
#include "../../../src/config/settingsschema.h"
#include "../helpers/IsolatedConfigGuard.h"

#include <PhosphorConfig/Schema.h>

#include <QSet>

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

        // Assignment groups must NOT be in config.json (they're extracted to assignments.json)
        QJsonObject configRoot = readJsonConfig(ConfigDefaults::configFilePath());
        QVERIFY2(!configRoot.contains(QStringLiteral("PerScreen")), "Assignment groups should not be under PerScreen");
        const QString groupName = QStringLiteral("Assignment:eDP-1:Desktop:1:Activity:abc-123");
        QVERIFY2(!configRoot.contains(groupName), "Assignment group should not remain in config.json");

        // They should be in assignments.json
        QJsonObject assignRoot = readJsonConfig(ConfigDefaults::assignmentsFilePath());
        QVERIFY2(assignRoot.contains(groupName), "Assignment group should be in assignments.json");

        QJsonObject assignment = assignRoot.value(groupName).toObject();
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

        // Assignment groups are now in assignments.json, not config.json
        auto backend = PlasmaZones::createAssignmentsBackend();
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
    // Round-trip with PhosphorConfig::JsonBackend
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
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

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
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

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
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

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

    void testV1JsonMigration_preservesPerScreenGroups()
    {
        IsolatedConfigGuard guard;
        // Create a v1 JSON config with PerScreen data — migration must preserve it
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        QJsonObject activation;
        activation[QStringLiteral("SnappingEnabled")] = true;
        root[QStringLiteral("Activation")] = activation;

        QJsonObject perScreen;
        QJsonObject zsCategory;
        QJsonObject edp;
        edp[QStringLiteral("Position")] = 3;
        edp[QStringLiteral("MaxRows")] = 5;
        zsCategory[QStringLiteral("eDP-1")] = edp;
        perScreen[QStringLiteral("ZoneSelector")] = zsCategory;
        root[QStringLiteral("PerScreen")] = perScreen;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject migrated = readJsonConfig(ConfigDefaults::configFilePath());
        QCOMPARE(migrated.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);

        // PerScreen data must survive migration untouched
        QJsonObject migratedPerScreen = migrated.value(QStringLiteral("PerScreen")).toObject();
        QJsonObject migratedZs = migratedPerScreen.value(QStringLiteral("ZoneSelector")).toObject();
        QJsonObject migratedEdp = migratedZs.value(QStringLiteral("eDP-1")).toObject();
        QCOMPARE(migratedEdp.value(QStringLiteral("Position")).toInt(), 3);
        QCOMPARE(migratedEdp.value(QStringLiteral("MaxRows")).toInt(), 5);

        // v1 groups should still be removed
        QVERIFY(!migrated.contains(QStringLiteral("Activation")));
    }

    // =========================================================================
    // Round-trip with PhosphorConfig::JsonBackend
    // =========================================================================

    void testV1JsonMigration_unifiedSnapInterval()
    {
        IsolatedConfigGuard guard;
        // v1 config with only SnapInterval (no per-axis SnapIntervalX/Y).
        // Migration must propagate Interval → IntervalX and IntervalY so the
        // v2 load code (which reads IntervalX/IntervalY directly) picks it up.
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        QJsonObject editor;
        editor[QStringLiteral("SnapInterval")] = 0.05;
        root[QStringLiteral("Editor")] = editor;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject migrated = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject editorMigrated = migrated.value(QStringLiteral("Editor")).toObject();
        QJsonObject editorSnapping = editorMigrated.value(QStringLiteral("Snapping")).toObject();
        QCOMPARE(editorSnapping.value(QStringLiteral("IntervalX")).toDouble(), 0.05);
        QCOMPARE(editorSnapping.value(QStringLiteral("IntervalY")).toDouble(), 0.05);
    }

    void testV1JsonMigration_perAxisSnapIntervalPreserved()
    {
        IsolatedConfigGuard guard;
        // v1 config with both SnapInterval and per-axis overrides.
        // The per-axis values should take priority over the unified one.
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        QJsonObject editor;
        editor[QStringLiteral("SnapInterval")] = 0.05;
        editor[QStringLiteral("SnapIntervalX")] = 0.1;
        editor[QStringLiteral("SnapIntervalY")] = 0.2;
        root[QStringLiteral("Editor")] = editor;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject migrated = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject editorMigrated = migrated.value(QStringLiteral("Editor")).toObject();
        QJsonObject editorSnapping = editorMigrated.value(QStringLiteral("Snapping")).toObject();
        QCOMPARE(editorSnapping.value(QStringLiteral("IntervalX")).toDouble(), 0.1);
        QCOMPARE(editorSnapping.value(QStringLiteral("IntervalY")).toDouble(), 0.2);
    }

    void testMigrationChain_versionZeroTreatedAsV1()
    {
        IsolatedConfigGuard guard;
        // A hand-edited config with _version: 0 should still get migrated
        QJsonObject root;
        root[QStringLiteral("_version")] = 0;

        QJsonObject activation;
        activation[QStringLiteral("SnappingEnabled")] = true;
        root[QStringLiteral("Activation")] = activation;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject migrated = readJsonConfig(ConfigDefaults::configFilePath());
        QCOMPARE(migrated.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);

        // Should have migrated Activation → Snapping
        QJsonObject snapping = migrated.value(QStringLiteral("Snapping")).toObject();
        QCOMPARE(snapping.value(QStringLiteral("Enabled")).toBool(), true);
        QVERIFY(!migrated.contains(QStringLiteral("Activation")));
    }

    void testMigrateV1ToV2_windowTrackingExtractedToSessionJson()
    {
        IsolatedConfigGuard guard;
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        // Simulate a v1 config with WindowTracking data (ephemeral session state)
        QJsonObject windowTracking;
        windowTracking[QStringLiteral("ActiveLayoutId")] = QStringLiteral("test-layout-id");
        QJsonObject assignments;
        assignments[QStringLiteral("0x12345")] = QStringLiteral("zone-uuid-1");
        windowTracking[QStringLiteral("WindowZoneAssignmentsFull")] = assignments;
        root[ConfigDefaults::windowTrackingGroup()] = windowTracking;

        // Also add a normal settings group to ensure it stays in config.json
        QJsonObject activation;
        activation[QStringLiteral("SnappingEnabled")] = true;
        root[QStringLiteral("Activation")] = activation;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // WindowTracking should be removed from config.json
        QJsonObject migrated = readJsonConfig(ConfigDefaults::configFilePath());
        QVERIFY(!migrated.contains(ConfigDefaults::windowTrackingGroup()));

        // session.json should exist with the WindowTracking group
        QVERIFY(QFile::exists(ConfigDefaults::sessionFilePath()));
        QJsonObject session = readJsonConfig(ConfigDefaults::sessionFilePath());
        QVERIFY(session.contains(ConfigDefaults::windowTrackingGroup()));
        QJsonObject sessionWt = session.value(ConfigDefaults::windowTrackingGroup()).toObject();
        QCOMPARE(sessionWt.value(QStringLiteral("ActiveLayoutId")).toString(), QStringLiteral("test-layout-id"));
        QJsonObject sessionAssignments = sessionWt.value(QStringLiteral("WindowZoneAssignmentsFull")).toObject();
        QCOMPARE(sessionAssignments.value(QStringLiteral("0x12345")).toString(), QStringLiteral("zone-uuid-1"));
    }

    void testMigrateV1ToV2_noWindowTracking_noSessionJson()
    {
        IsolatedConfigGuard guard;
        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        // v1 config without WindowTracking — session.json should NOT be created
        QJsonObject activation;
        activation[QStringLiteral("SnappingEnabled")] = true;
        root[QStringLiteral("Activation")] = activation;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QVERIFY(!QFile::exists(ConfigDefaults::sessionFilePath()));
    }

    void testINIColorHeuristic_preservesNonColorIntLists()
    {
        // iniMapToJson has a heuristic: 3 or 4 comma-separated ints in 0..255
        // are treated as an r,g,b[,a] color and converted to hex. The
        // docstring explicitly calls out this assumption; guard against a
        // future setting whose wire format happens to fit the pattern but
        // isn't a color. For now the only such shape in the config tree is
        // trigger lists (which are JSON, not comma-separated), but this
        // test locks the behavior so a regression surfaces loudly.
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Appearance]\n"
                                    "HighlightColor=82,148,226,255\n"
                                    "\n"
                                    "[Ordering]\n"
                                    // NOT a color: just a comma list of small
                                    // ints (e.g. a layout order by index).
                                    // Must round-trip as a string, not a hex.
                                    "SnappingLayoutOrder=1,2,3\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QJsonObject ordering = root.value(QStringLiteral("Ordering")).toObject();
        const QJsonValue order = ordering.value(QStringLiteral("SnappingLayoutOrder"));
        QVERIFY2(order.isString(),
                 "Non-color int list must remain a string after migration — "
                 "color heuristic over-fired on comma-separated small ints.");
        QCOMPARE(order.toString(), QStringLiteral("1,2,3"));
    }

    void testSchemaCoversEveryMigrationDestinationKey()
    {
        // Cross-check: every leaf key that migrateV1ToV2 writes into the v2
        // tree must be declared in the v2 settings schema. Catches silent
        // drift between the migration's destination names and the live
        // schema — e.g. if someone renames a key in ConfigDefaults and only
        // updates one side, this test surfaces the mismatch at build time
        // instead of at user-config-load time.
        //
        // The schema uses dot-path group names ("Snapping.Behavior.ZoneSpan")
        // while migrateV1ToV2 produces nested JSON objects. Flatten both to
        // a QSet<QString> of "Group.Path/Key" entries for comparison.
        IsolatedConfigGuard guard;

        // Use an INI covering every v1 group the migration handles. The
        // exact values don't matter — we only care about which destination
        // keys get produced.
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Activation]\n"
                                    "SnappingEnabled=true\n"
                                    "DragActivationTriggers=[{\"modifier\":2,\"mouseButton\":0}]\n"
                                    "ToggleActivation=false\n"
                                    "ZoneSpanEnabled=true\n"
                                    "ZoneSpanModifier=4\n"
                                    "ZoneSpanTriggers=[]\n"
                                    "SnapAssistFeatureEnabled=true\n"
                                    "SnapAssistEnabled=false\n"
                                    "SnapAssistTriggers=[]\n"
                                    "\n"
                                    "[Display]\n"
                                    "ShowOnAllMonitors=true\n"
                                    "DisabledMonitors=a\n"
                                    "DisabledDesktops=b\n"
                                    "DisabledActivities=c\n"
                                    "ShowNumbers=true\n"
                                    "FlashOnSwitch=true\n"
                                    "ShowOsdOnLayoutSwitch=true\n"
                                    "ShowNavigationOsd=true\n"
                                    "OsdStyle=0\n"
                                    "OverlayDisplayMode=0\n"
                                    "\n"
                                    "[Behavior]\n"
                                    "FilterLayoutsByAspectRatio=true\n"
                                    "KeepOnResolutionChange=true\n"
                                    "MoveNewToLastZone=true\n"
                                    "RestoreSizeOnUnsnap=true\n"
                                    "StickyWindowHandling=0\n"
                                    "RestoreWindowsToZonesOnLogin=true\n"
                                    "DefaultLayoutId=\n"
                                    "\n"
                                    "[Appearance]\n"
                                    "UseSystemColors=true\n"
                                    "HighlightColor=1,2,3,255\n"
                                    "InactiveColor=4,5,6,255\n"
                                    "BorderColor=7,8,9,255\n"
                                    "ActiveOpacity=0.1\n"
                                    "InactiveOpacity=0.2\n"
                                    "BorderWidth=1\n"
                                    "BorderRadius=2\n"
                                    "LabelFontColor=0,0,0,255\n"
                                    "LabelFontFamily=Sans\n"
                                    "LabelFontSizeScale=1.0\n"
                                    "LabelFontWeight=50\n"
                                    "LabelFontItalic=false\n"
                                    "LabelFontUnderline=false\n"
                                    "LabelFontStrikeout=false\n"
                                    "EnableBlur=false\n"
                                    "\n"
                                    "[Zones]\n"
                                    "Padding=4\n"
                                    "OuterGap=8\n"
                                    "UsePerSideOuterGap=false\n"
                                    "OuterGapTop=1\n"
                                    "OuterGapBottom=2\n"
                                    "OuterGapLeft=3\n"
                                    "OuterGapRight=4\n"
                                    "AdjacentThreshold=5\n"
                                    "PollIntervalMs=100\n"
                                    "MinimumZoneSizePx=50\n"
                                    "MinimumZoneDisplaySizePx=60\n"
                                    "\n"
                                    "[Exclusions]\n"
                                    "ExcludeTransientWindows=true\n"
                                    "MinimumWindowWidth=100\n"
                                    "MinimumWindowHeight=100\n"
                                    "Applications=a\n"
                                    "WindowClasses=b\n"
                                    "\n"
                                    "[ZoneSelector]\n"
                                    "Enabled=true\n"
                                    "TriggerDistance=20\n"
                                    "Position=0\n"
                                    "LayoutMode=0\n"
                                    "PreviewWidth=100\n"
                                    "PreviewHeight=100\n"
                                    "PreviewLockAspect=true\n"
                                    "GridColumns=2\n"
                                    "SizeMode=0\n"
                                    "MaxRows=5\n"
                                    "\n"
                                    "[Autotiling]\n"
                                    "AutotileEnabled=true\n"
                                    "DefaultAutotileAlgorithm=bsp\n"
                                    "AutotileSplitRatio=0.5\n"
                                    "AutotileSplitRatioStep=0.05\n"
                                    "AutotileMasterCount=1\n"
                                    "AutotileMaxWindows=8\n"
                                    "AutotileInsertPosition=0\n"
                                    "AutotileFocusNewWindows=true\n"
                                    "AutotileFocusFollowsMouse=false\n"
                                    "AutotileRespectMinimumSize=true\n"
                                    "AutotileStickyWindowHandling=0\n"
                                    "LockedScreens=\n"
                                    "AutotileUseSystemBorderColors=false\n"
                                    "AutotileBorderColor=1,2,3,255\n"
                                    "AutotileInactiveBorderColor=4,5,6,255\n"
                                    "AutotileHideTitleBars=false\n"
                                    "AutotileShowBorder=true\n"
                                    "AutotileBorderWidth=1\n"
                                    "AutotileBorderRadius=1\n"
                                    "AutotileInnerGap=4\n"
                                    "AutotileOuterGap=4\n"
                                    "AutotileUsePerSideOuterGap=false\n"
                                    "AutotileOuterGapTop=1\n"
                                    "AutotileOuterGapBottom=1\n"
                                    "AutotileOuterGapLeft=1\n"
                                    "AutotileOuterGapRight=1\n"
                                    "AutotileSmartGaps=false\n"
                                    "\n"
                                    "[AutotileShortcuts]\n"
                                    "ToggleShortcut=Meta+T\n"
                                    "FocusMasterShortcut=Meta+M\n"
                                    "SwapMasterShortcut=Meta+S\n"
                                    "IncMasterRatioShortcut=Meta+L\n"
                                    "DecMasterRatioShortcut=Meta+H\n"
                                    "IncMasterCountShortcut=Meta+I\n"
                                    "DecMasterCountShortcut=Meta+D\n"
                                    "RetileShortcut=Meta+R\n"
                                    "\n"
                                    "[Animations]\n"
                                    "AnimationsEnabled=true\n"
                                    "AnimationDuration=200\n"
                                    "AnimationEasingCurve=easeOutCubic\n"
                                    "AnimationMinDistance=10\n"
                                    "AnimationSequenceMode=0\n"
                                    "AnimationStaggerInterval=0\n"
                                    "\n"
                                    "[GlobalShortcuts]\n"
                                    "OpenEditorShortcut=Meta+E\n"
                                    "\n"
                                    "[Editor]\n"
                                    "EditorDuplicateShortcut=Ctrl+D\n"
                                    "EditorSplitHorizontalShortcut=Ctrl+H\n"
                                    "EditorSplitVerticalShortcut=Ctrl+V\n"
                                    "EditorFillShortcut=Ctrl+F\n"
                                    "GridSnappingEnabled=true\n"
                                    "EdgeSnappingEnabled=true\n"
                                    "SnapIntervalX=0.05\n"
                                    "SnapIntervalY=0.05\n"
                                    "SnapOverrideModifier=67108864\n"
                                    "FillOnDropEnabled=false\n"
                                    "FillOnDropModifier=0\n"
                                    "\n"
                                    "[Ordering]\n"
                                    "SnappingLayoutOrder=a,b\n"
                                    "TilingAlgorithmOrder=a,b\n"
                                    "\n"
                                    "[Rendering]\n"
                                    "RenderingBackend=vulkan\n"
                                    "\n"
                                    "[Shaders]\n"
                                    "EnableShaderEffects=true\n"
                                    "ShaderFrameRate=60\n"
                                    "EnableAudioVisualizer=false\n"
                                    "AudioSpectrumBarCount=32\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());

        // Build the set of declared "Group.Path/Key" pairs from the schema.
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        QSet<QString> declared;
        for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
            for (const PhosphorConfig::KeyDef& def : it.value()) {
                declared.insert(it.key() + QLatin1Char('/') + def.key);
            }
        }

        // Walk the migrated JSON tree, collecting every scalar leaf as
        // "Group.Path/Key". Migration-only reserved keys (_version,
        // PerScreen container — resolver-owned) are filtered out.
        //
        // Some root keys live outside the schema by design:
        //   - TilingQuickLayoutSlots, Updates: managed outside Settings.
        //   - Rendering.Backend: declared in the schema. Verify.
        QSet<QString> produced;
        QStringList stack; // path prefix as dot-path segments
        std::function<void(const QJsonObject&)> collect = [&](const QJsonObject& obj) {
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isObject()) {
                    stack.append(it.key());
                    collect(it.value().toObject());
                    stack.removeLast();
                } else {
                    const QString groupPath = stack.join(QLatin1Char('.'));
                    produced.insert(groupPath + QLatin1Char('/') + it.key());
                }
            }
        };
        // Skip reserved root keys and non-schema top-level groups.
        const QSet<QString> skipRoots = {
            QStringLiteral("_version"), QStringLiteral("PerScreen"),
            QStringLiteral("General"),  QStringLiteral("TilingQuickLayoutSlots"),
            QStringLiteral("Updates"),  QStringLiteral("WindowTracking"),
        };
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (skipRoots.contains(it.key())) {
                continue;
            }
            if (it.value().isObject()) {
                stack.append(it.key());
                collect(it.value().toObject());
                stack.removeLast();
            }
        }

        // Every produced key must exist in the schema.
        QStringList missing;
        for (const QString& key : std::as_const(produced)) {
            if (!declared.contains(key)) {
                missing.append(key);
            }
        }
        if (!missing.isEmpty()) {
            missing.sort();
            QFAIL(qPrintable(QStringLiteral("Migration produces keys that aren't declared in buildSettingsSchema() "
                                            "— schema/migration drift:\n  ")
                             + missing.join(QStringLiteral("\n  "))));
        }
    }

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
