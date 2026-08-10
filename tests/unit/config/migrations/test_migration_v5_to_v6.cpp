// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v5_to_v6.cpp
 * @brief Unit tests for the v5 → v6 schema migration (config → config).
 *
 * v5 stored the four snapping zone colours as concrete colours gated by one
 * Snapping.Zones.Colors/UseSystem bool; while the bool was on, the settings
 * layer wrote palette-derived SNAPSHOTS into the keys. v6 turns the keys into
 * theme-fallback strings (EMPTY / absent = "follow the system palette") and
 * drops the bool. The migration must:
 *   - remove the colour keys when UseSystem was on (or absent, its v5
 *     default), because their values were palette snapshots, not user picks;
 *   - keep the colour keys verbatim (normalised to #AARRGGBB) when UseSystem
 *     was off;
 *   - strip the UseSystem key either way, pruning emptied groups;
 *   - stamp _version = 6.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestMigrationV5ToV6 : public QObject
{
    Q_OBJECT

private:
    void writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(obj).toJson());
    }

    QJsonObject readJson(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    /// A v5 config carrying the zone-colour groups plus a sibling key in each
    /// parent so ancestor pruning can be observed precisely.
    QJsonObject makeV5Config(const QJsonObject& colors, const QJsonObject& labels)
    {
        QJsonObject zones;
        if (!colors.isEmpty()) {
            zones.insert(QStringLiteral("Colors"), colors);
        }
        if (!labels.isEmpty()) {
            zones.insert(QStringLiteral("Labels"), labels);
        }
        QJsonObject snapping;
        snapping.insert(QStringLiteral("Zones"), zones);
        QJsonObject behavior;
        behavior.insert(QStringLiteral("ToggleActivation"), true);
        snapping.insert(QStringLiteral("Behavior"), behavior);
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 5);
        root.insert(QStringLiteral("Snapping"), snapping);
        return root;
    }

    QJsonObject colorsAfter(const QJsonObject& root)
    {
        return root.value(QStringLiteral("Snapping"))
            .toObject()
            .value(QStringLiteral("Zones"))
            .toObject()
            .value(QStringLiteral("Colors"))
            .toObject();
    }

    QJsonObject labelsAfter(const QJsonObject& root)
    {
        return root.value(QStringLiteral("Snapping"))
            .toObject()
            .value(QStringLiteral("Zones"))
            .toObject()
            .value(QStringLiteral("Labels"))
            .toObject();
    }

private Q_SLOTS:
    void testSystemOn_dropsSnapshotColors()
    {
        IsolatedConfigGuard guard;
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), true);
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        colors.insert(QStringLiteral("Inactive"), QStringLiteral("#40223344"));
        colors.insert(QStringLiteral("Border"), QStringLiteral("#c8334455"));
        QJsonObject labels;
        labels.insert(QStringLiteral("FontColor"), QStringLiteral("#ffddeeff"));
        labels.insert(QStringLiteral("FontFamily"), QStringLiteral("X"));
        writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, labels));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(after.value(QStringLiteral("_version")).toInt(), ConfigSchemaVersion);
        // Every colour was a palette snapshot: the Colors group is emptied and
        // pruned outright; the Labels group loses FontColor but keeps the
        // unrelated FontFamily key.
        QVERIFY(colorsAfter(after).isEmpty());
        const QJsonObject labelsOut = labelsAfter(after);
        QVERIFY(!labelsOut.contains(QStringLiteral("FontColor")));
        QCOMPARE(labelsOut.value(QStringLiteral("FontFamily")).toString(), QStringLiteral("X"));
        // The sibling Behavior group is untouched.
        QVERIFY(after.value(QStringLiteral("Snapping")).toObject().contains(QStringLiteral("Behavior")));
    }

    void testSystemAbsent_defaultsToOnAndDrops()
    {
        IsolatedConfigGuard guard;
        QJsonObject colors;
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, {}));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        // An absent UseSystem meant "on" in v5, so the snapshot is dropped.
        QVERIFY(colorsAfter(after).isEmpty());
    }

    void testSystemOff_keepsPinnedColors()
    {
        IsolatedConfigGuard guard;
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), false);
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        // A legacy KConfig comma form must be normalised, not dropped.
        colors.insert(QStringLiteral("Border"), QStringLiteral("82,148,226,200"));
        // Garbage cannot survive as a stored value: it snaps to the sentinel
        // (removed) rather than painting black at resolve time.
        colors.insert(QStringLiteral("Inactive"), QStringLiteral("not-a-color"));
        QJsonObject labels;
        labels.insert(QStringLiteral("FontColor"), QStringLiteral("#ffddeeff"));
        writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, labels));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(after.value(QStringLiteral("_version")).toInt(), ConfigSchemaVersion);
        const QJsonObject colorsOut = colorsAfter(after);
        QCOMPARE(colorsOut.value(QStringLiteral("Highlight")).toString(), QStringLiteral("#80112233"));
        QCOMPARE(colorsOut.value(QStringLiteral("Border")).toString(), QStringLiteral("#c85294e2"));
        QVERIFY(!colorsOut.contains(QStringLiteral("Inactive")));
        QVERIFY(!colorsOut.contains(QStringLiteral("UseSystem")));
        QCOMPARE(labelsAfter(after).value(QStringLiteral("FontColor")).toString(), QStringLiteral("#ffddeeff"));
    }

    void testWindowsAccentToken_becomesEmptySentinel()
    {
        IsolatedConfigGuard guard;
        QJsonObject windows;
        windows.insert(QStringLiteral("BorderColorActive"), QStringLiteral("accent"));
        windows.insert(QStringLiteral("BorderColorInactive"), QStringLiteral("#FF112233"));
        windows.insert(QStringLiteral("TintColor"), QStringLiteral("accent"));
        // An unrelated sibling key that must survive untouched.
        windows.insert(QStringLiteral("Width"), 4);
        QJsonObject root = makeV5Config({}, {});
        root.insert(QStringLiteral("Windows"), windows);
        writeJson(ConfigDefaults::configFilePath(), root);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject windowsOut = after.value(QStringLiteral("Windows")).toObject();
        // The "accent" sentinel spells "follow the system accent" as the
        // empty sentinel now, which with sparse persistence means the key is
        // simply gone; a concrete pick stays verbatim.
        QVERIFY(!windowsOut.contains(QStringLiteral("BorderColorActive")));
        QVERIFY(!windowsOut.contains(QStringLiteral("TintColor")));
        QCOMPARE(windowsOut.value(QStringLiteral("BorderColorInactive")).toString(), QStringLiteral("#FF112233"));
        QCOMPARE(windowsOut.value(QStringLiteral("Width")).toInt(), 4);
    }

    void testAlreadyV6_versionGateIsNoOp()
    {
        IsolatedConfigGuard guard;
        // A v6 config whose Colors group holds a pinned colour and, oddly, a
        // stray UseSystem key: the version gate must short-circuit the step so
        // nothing is rewritten (defense-in-depth idempotency, mirroring the
        // earlier steps' gates).
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), true);
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        QJsonObject root = makeV5Config(colors, {});
        root[QStringLiteral("_version")] = 6;
        writeJson(ConfigDefaults::configFilePath(), root);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject colorsOut = colorsAfter(after);
        QCOMPARE(colorsOut.value(QStringLiteral("Highlight")).toString(), QStringLiteral("#80112233"));
        QVERIFY(colorsOut.contains(QStringLiteral("UseSystem")));
    }
};

QTEST_MAIN(TestMigrationV5ToV6)
#include "test_migration_v5_to_v6.moc"
