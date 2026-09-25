// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "config/configmigration.h"
#include "config/configdefaults.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestConfigMigrationV2ToV3 : public QObject
{
    Q_OBJECT

private:
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
    // v2 → v3: per-mode disable list split
    //
    // v2 stored a single `Snapping.Behavior.Display.{DisabledMonitors,
    // DisabledDesktops, DisabledActivities}` whose value silently gated both
    // snap and autotile despite the snapping-prefixed group name. v3 splits
    // each into per-mode lists under a mode-neutral `Display` group.
    //
    // The migration must duplicate every v2 entry into BOTH the snapping and
    // autotile v3 lists — that's the only safe interpretation when the v2
    // schema didn't distinguish modes (the user wanted PZ off there, period).
    // =========================================================================

    /// Each v2 disable list is copied verbatim into both v3 per-mode lists.
    /// The v2 keys are removed from Snapping.Behavior.Display, but the
    /// surrounding non-disable keys (ShowOnAllMonitors, FilterByAspectRatio)
    /// stay put — the migration is targeted, not a wholesale group move.
    void testMigrateV2ToV3_duplicatesDisableListsToBothModes()
    {
        QJsonObject root;
        root[QStringLiteral("_version")] = 2;

        QJsonObject display;
        display[QStringLiteral("ShowOnAllMonitors")] = true;
        display[QStringLiteral("FilterByAspectRatio")] = false;
        display[QStringLiteral("DisabledMonitors")] = QStringLiteral("DP-1,HDMI-A-1");
        display[QStringLiteral("DisabledDesktops")] = QStringLiteral("DP-1/2,HDMI-A-1/3");
        display[QStringLiteral("DisabledActivities")] = QStringLiteral("DP-1/uuid-foo");

        QJsonObject behavior;
        behavior[QStringLiteral("Display")] = display;
        QJsonObject snapping;
        snapping[QStringLiteral("Behavior")] = behavior;
        root[QStringLiteral("Snapping")] = snapping;

        ConfigMigration::migrateV2ToV3(root);

        // Version stamped to 3.
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 3);

        // Each disable list is copied to both per-mode v3 keys, identical bytes.
        const QJsonObject v3Display = root.value(QStringLiteral("Display")).toObject();
        QCOMPARE(v3Display.value(QStringLiteral("SnappingDisabledMonitors")).toString(),
                 QStringLiteral("DP-1,HDMI-A-1"));
        QCOMPARE(v3Display.value(QStringLiteral("AutotileDisabledMonitors")).toString(),
                 QStringLiteral("DP-1,HDMI-A-1"));
        QCOMPARE(v3Display.value(QStringLiteral("SnappingDisabledDesktops")).toString(),
                 QStringLiteral("DP-1/2,HDMI-A-1/3"));
        QCOMPARE(v3Display.value(QStringLiteral("AutotileDisabledDesktops")).toString(),
                 QStringLiteral("DP-1/2,HDMI-A-1/3"));
        QCOMPARE(v3Display.value(QStringLiteral("SnappingDisabledActivities")).toString(),
                 QStringLiteral("DP-1/uuid-foo"));
        QCOMPARE(v3Display.value(QStringLiteral("AutotileDisabledActivities")).toString(),
                 QStringLiteral("DP-1/uuid-foo"));

        // Disabled* keys are gone from v2's Snapping.Behavior.Display, but the
        // sibling keys (ShowOnAllMonitors, FilterByAspectRatio) survive — the
        // migration is targeted, not a group-wide move.
        const QJsonObject postBehavior =
            root.value(QStringLiteral("Snapping")).toObject().value(QStringLiteral("Behavior")).toObject();
        const QJsonObject postDisplay = postBehavior.value(QStringLiteral("Display")).toObject();
        QVERIFY(!postDisplay.contains(QStringLiteral("DisabledMonitors")));
        QVERIFY(!postDisplay.contains(QStringLiteral("DisabledDesktops")));
        QVERIFY(!postDisplay.contains(QStringLiteral("DisabledActivities")));
        QCOMPARE(postDisplay.value(QStringLiteral("ShowOnAllMonitors")).toBool(), true);
        QCOMPARE(postDisplay.value(QStringLiteral("FilterByAspectRatio")).toBool(), false);
    }

    /// A v2 config with empty disable lists must NOT pollute the v3 root with
    /// empty keys — the migration only writes v3 entries when the v2 source
    /// is non-empty. Avoids growing every existing user's config file with
    /// noise on first launch after the upgrade.
    void testMigrateV2ToV3_emptyListsProduceNoV3Keys()
    {
        QJsonObject root;
        root[QStringLiteral("_version")] = 2;
        // No Snapping.Behavior.Display at all — clean install with no disabled state.
        ConfigMigration::migrateV2ToV3(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 3);
        QVERIFY(!root.contains(QStringLiteral("Display")));
    }

    /// A v2 config whose Snapping.Behavior.Display held ONLY disable lists
    /// (no ShowOnAllMonitors, no FilterByAspectRatio) must end up with
    /// Snapping.Behavior.Display removed entirely — the migration trims any
    /// container that becomes empty after the disable keys move out.
    void testMigrateV2ToV3_collapsesEmptyContainers()
    {
        QJsonObject root;
        root[QStringLiteral("_version")] = 2;

        QJsonObject display;
        display[QStringLiteral("DisabledMonitors")] = QStringLiteral("DP-1");
        QJsonObject behavior;
        behavior[QStringLiteral("Display")] = display;
        QJsonObject snapping;
        snapping[QStringLiteral("Behavior")] = behavior;
        root[QStringLiteral("Snapping")] = snapping;

        ConfigMigration::migrateV2ToV3(root);

        // Snapping.Behavior.Display had only the migrated keys, so the whole
        // chain collapses up to root.
        QVERIFY(!root.contains(QStringLiteral("Snapping")));
        // The migrated value lives in v3.
        QCOMPARE(root.value(QStringLiteral("Display"))
                     .toObject()
                     .value(QStringLiteral("SnappingDisabledMonitors"))
                     .toString(),
                 QStringLiteral("DP-1"));
    }

    /// A v2 config whose disable list has a non-string type (hand-edited
    /// array, number, null) must be cleaned up during migration — the v2 key
    /// is dropped unconditionally, no v3 entry is synthesized from the
    /// malformed data, and nothing leaks past the v3 stamp as orphaned
    /// "looks like v2" data.
    void testMigrateV2ToV3_dropsNonStringDisableValues()
    {
        QJsonObject root;
        root[QStringLiteral("_version")] = 2;

        QJsonObject display;
        // Hand-edited array — not the v2 wire format (which is a comma-joined string).
        QJsonArray badArray;
        badArray.append(QStringLiteral("DP-1"));
        badArray.append(QStringLiteral("HDMI-1"));
        display[QStringLiteral("DisabledMonitors")] = badArray;
        // null literal.
        display[QStringLiteral("DisabledDesktops")] = QJsonValue::Null;
        // Number.
        display[QStringLiteral("DisabledActivities")] = 42;

        QJsonObject behavior;
        behavior[QStringLiteral("Display")] = display;
        QJsonObject snapping;
        snapping[QStringLiteral("Behavior")] = behavior;
        root[QStringLiteral("Snapping")] = snapping;

        ConfigMigration::migrateV2ToV3(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 3);

        // No v3 entries were synthesized — non-string values are dropped.
        QVERIFY(!root.contains(QStringLiteral("Display")));

        // The v2 keys are gone from Snapping.Behavior.Display so they don't
        // linger as orphaned "looks like v2" data on a v3-stamped config.
        // The whole chain collapses because the only keys in Display were
        // the three malformed ones.
        QVERIFY(!root.contains(QStringLiteral("Snapping")));
    }

    /// End-to-end check that ensureJsonConfig() chains v1→v4 when given a v1
    /// file with disabled-monitor data. The v2→v3 step duplicates the single
    /// legacy list into BOTH the snap and autotile lists; the v3→v4 step then
    /// converts every entry into a DisableEngine context rule in
    /// rules.json and REMOVES the config.json Display.*Disabled* keys.
    void testMigrateV1ToV4_chainConvertsDisableListsToRules()
    {
        IsolatedConfigGuard guard;
        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QFile f(ConfigDefaults::configFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(
            "{\"_version\":1,"
            "\"Display\":{\"DisabledMonitors\":\"DP-1,HDMI-1\"}}");
        f.close();

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);

        // The Display.*Disabled* keys are gone from config.json — rules
        // .json supersedes them.
        const QJsonObject display = root.value(QStringLiteral("Display")).toObject();
        QVERIFY(!display.contains(QStringLiteral("SnappingDisabledMonitors")));
        QVERIFY(!display.contains(QStringLiteral("AutotileDisabledMonitors")));

        // Two monitors × two modes = four DisableEngine monitor rules.
        const QJsonObject wr = readJsonConfig(ConfigDefaults::rulesFilePath());
        int disableMonitorRules = 0;
        for (const QJsonValue& v : wr.value(QStringLiteral("rules")).toArray()) {
            const QJsonObject r = v.toObject();
            for (const QJsonValue& av : r.value(QStringLiteral("actions")).toArray()) {
                if (av.toObject().value(QStringLiteral("type")).toString() == QLatin1String("disableEngine")) {
                    ++disableMonitorRules;
                    break;
                }
            }
        }
        QCOMPARE(disableMonitorRules, 4);
    }
};

QTEST_MAIN(TestConfigMigrationV2ToV3)
#include "test_configmigration_v2tov3.moc"
