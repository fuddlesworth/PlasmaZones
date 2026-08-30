// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v5_to_v6.cpp
 * @brief Unit tests for the v5 → v6 schema migration (config → config).
 *
 * v5 stored the four snapping zone colours as concrete colours gated by one
 * Snapping.Zones.Colors/UseSystem bool; while the bool was on, the settings
 * layer wrote palette-derived SNAPSHOTS into the keys. v6 turns the keys into
 * theme-fallback strings (EMPTY = "follow the system palette") and drops the
 * bool. The migration must:
 *   - convert the colour keys to the EXPLICIT empty sentinel when UseSystem
 *     was on (or absent, its v5 default): in config.json that is equivalent
 *     to removal (the sentinel is the schema default, pruned on the next
 *     sparse save), but in a sparse profile delta removal would mean
 *     "inherit from the parent" instead of "follow the palette";
 *   - keep the colour keys verbatim (normalised to #AARRGGBB) when UseSystem
 *     was off, materialising an ABSENT key to the frozen v5 default it
 *     resolved to (sparse persistence pruned default-equal picks);
 *   - coerce a non-bool UseSystem (an INI-era int 0/1) instead of silently
 *     falling back to the on default;
 *   - strip the UseSystem key either way, pruning a group only when it
 *     genuinely emptied, and leaving a non-object value at a group path
 *     alone;
 *   - stamp the literal 6 (the historical step's frozen output).
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
    // Returns success instead of QVERIFY-ing internally: a QVERIFY in a void
    // helper returns from the HELPER, not the test slot, and the slot would
    // then run against a missing fixture with a confusing diagnosis.
    [[nodiscard]] bool writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
        return f.write(QJsonDocument(obj).toJson()) >= 0;
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
    void testSystemOn_snapshotColorsBecomeExplicitSentinel()
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
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, labels)));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        // Full-chain run through ensureJsonConfig, so the file ends at the
        // CURRENT schema version (v7 stamps on top of the v6 output this
        // test is really about); the frozen v6 step output is asserted by
        // the direct-step case below.
        QCOMPARE(after.value(QStringLiteral("_version")).toInt(), 7);
        // Every colour was a palette snapshot: each key becomes the EXPLICIT
        // empty sentinel (delta-safe spelling of "follow the palette"); the
        // Labels group keeps the unrelated FontFamily key.
        const QJsonObject colorsOut = colorsAfter(after);
        QCOMPARE(colorsOut.value(QStringLiteral("Highlight")).toString(), QString());
        QVERIFY(colorsOut.contains(QStringLiteral("Highlight")));
        QVERIFY(colorsOut.contains(QStringLiteral("Inactive")));
        QVERIFY(colorsOut.contains(QStringLiteral("Border")));
        QVERIFY(!colorsOut.contains(QStringLiteral("UseSystem")));
        const QJsonObject labelsOut = labelsAfter(after);
        QVERIFY(labelsOut.contains(QStringLiteral("FontColor")));
        QCOMPARE(labelsOut.value(QStringLiteral("FontColor")).toString(), QString());
        QCOMPARE(labelsOut.value(QStringLiteral("FontFamily")).toString(), QStringLiteral("X"));
        // The sibling Behavior group is untouched.
        QVERIFY(after.value(QStringLiteral("Snapping")).toObject().contains(QStringLiteral("Behavior")));
    }

    void testSystemAbsent_defaultsToOnAndConverts()
    {
        IsolatedConfigGuard guard;
        QJsonObject colors;
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, {})));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        // An absent UseSystem meant "on" in v5, so the snapshot becomes the
        // explicit sentinel. The absent Inactive/Border keys stay absent:
        // with the palette owning them there is nothing to materialise.
        const QJsonObject colorsOut = colorsAfter(after);
        QCOMPARE(colorsOut.value(QStringLiteral("Highlight")).toString(), QString());
        QVERIFY(colorsOut.contains(QStringLiteral("Highlight")));
        QVERIFY(!colorsOut.contains(QStringLiteral("Inactive")));
        QVERIFY(!colorsOut.contains(QStringLiteral("Border")));
    }

    void testSystemOnOnly_groupGenuinelyEmptiedIsPruned()
    {
        IsolatedConfigGuard guard;
        // A Colors group holding ONLY the retired bool: stripping it empties
        // the group, which must be pruned rather than left as a husk.
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), true);
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, {})));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject zones =
            after.value(QStringLiteral("Snapping")).toObject().value(QStringLiteral("Zones")).toObject();
        QVERIFY(!zones.contains(QStringLiteral("Colors")));
        // The sibling Behavior group keeps Snapping alive.
        QVERIFY(after.value(QStringLiteral("Snapping")).toObject().contains(QStringLiteral("Behavior")));
    }

    void testSystemOff_keepsPinnedColors()
    {
        IsolatedConfigGuard guard;
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), false);
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        // A legacy KConfig comma form must be normalised, not dropped.
        colors.insert(QStringLiteral("Border"), QStringLiteral("82,148,226,200"));
        // Garbage cannot survive as a stored value: it snaps to the explicit
        // sentinel rather than painting black at resolve time.
        colors.insert(QStringLiteral("Inactive"), QStringLiteral("not-a-color"));
        QJsonObject labels;
        labels.insert(QStringLiteral("FontColor"), QStringLiteral("#ffddeeff"));
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, labels)));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(after.value(QStringLiteral("_version")).toInt(), 7);
        const QJsonObject colorsOut = colorsAfter(after);
        QCOMPARE(colorsOut.value(QStringLiteral("Highlight")).toString(), QStringLiteral("#80112233"));
        QCOMPARE(colorsOut.value(QStringLiteral("Border")).toString(), QStringLiteral("#c85294e2"));
        QVERIFY(colorsOut.contains(QStringLiteral("Inactive")));
        QCOMPARE(colorsOut.value(QStringLiteral("Inactive")).toString(), QString());
        QVERIFY(!colorsOut.contains(QStringLiteral("UseSystem")));
        QCOMPARE(labelsAfter(after).value(QStringLiteral("FontColor")).toString(), QStringLiteral("#ffddeeff"));
    }

    void testSystemOff_absentColorMaterialisesFrozenDefault()
    {
        IsolatedConfigGuard guard;
        // Sparse persistence stored a pick that equalled the shipped default
        // as key ABSENCE. Under v5 that absence still resolved to the
        // concrete constant with UseSystem off — the migration must
        // materialise the frozen v5 defaults or the deliberate opt-out
        // silently starts following the palette (the classic victim is an
        // opaque-white label font turning near-black on a light scheme).
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), false);
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, {})));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject colorsOut = colorsAfter(after);
        QCOMPARE(colorsOut.value(QStringLiteral("Highlight")).toString(), QStringLiteral("#800078d4"));
        QCOMPARE(colorsOut.value(QStringLiteral("Inactive")).toString(), QStringLiteral("#40808080"));
        QCOMPARE(colorsOut.value(QStringLiteral("Border")).toString(), QStringLiteral("#c8ffffff"));
        QCOMPARE(labelsAfter(after).value(QStringLiteral("FontColor")).toString(), QStringLiteral("#ffffffff"));
    }

    void testIniEraIntUseSystem_isCoercedNotDefaulted()
    {
        IsolatedConfigGuard guard;
        // The INI conversion turns "UseSystemColors=0" into a JSON INT 0, not
        // a bool. The step must coerce it (off), not silently take the on
        // default and convert the user's pins to the sentinel.
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), 0);
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), makeV5Config(colors, {})));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(colorsAfter(after).value(QStringLiteral("Highlight")).toString(), QStringLiteral("#80112233"));
    }

    void testNonObjectGroupValue_isLeftAlone()
    {
        IsolatedConfigGuard guard;
        // A hand-edited SCALAR where a group belongs is not the migration's
        // to delete: it converts nothing and must leave the value in place.
        QJsonObject zones;
        zones.insert(QStringLiteral("Colors"), QStringLiteral("oops-a-string"));
        QJsonObject snapping;
        snapping.insert(QStringLiteral("Zones"), zones);
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 5);
        root.insert(QStringLiteral("Snapping"), snapping);
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject zonesOut =
            after.value(QStringLiteral("Snapping")).toObject().value(QStringLiteral("Zones")).toObject();
        QCOMPARE(zonesOut.value(QStringLiteral("Colors")).toString(), QStringLiteral("oops-a-string"));
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
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject windowsOut = after.value(QStringLiteral("Windows")).toObject();
        // The "accent" token spells "follow the system accent" as the
        // EXPLICIT empty sentinel now (delta-safe: removal in a sparse
        // profile delta would mean "inherit"); a concrete pick stays
        // verbatim. The next sparse save prunes the default-equal empties
        // from config.json.
        QCOMPARE(windowsOut.value(QStringLiteral("BorderColorActive")).toString(), QString());
        QVERIFY(windowsOut.contains(QStringLiteral("BorderColorActive")));
        QCOMPARE(windowsOut.value(QStringLiteral("TintColor")).toString(), QString());
        QCOMPARE(windowsOut.value(QStringLiteral("BorderColorInactive")).toString(), QStringLiteral("#FF112233"));
        QCOMPARE(windowsOut.value(QStringLiteral("Width")).toInt(), 4);
    }

    void testAlreadyV6_versionGateIsNoOp()
    {
        IsolatedConfigGuard guard;
        // A v6 config whose Colors group holds a pinned colour and, oddly, a
        // stray UseSystem key: the chain runner's version gate must
        // short-circuit so nothing is rewritten.
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), true);
        colors.insert(QStringLiteral("Highlight"), QStringLiteral("#80112233"));
        QJsonObject root = makeV5Config(colors, {});
        root[QStringLiteral("_version")] = 6;
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject colorsOut = colorsAfter(after);
        QCOMPARE(colorsOut.value(QStringLiteral("Highlight")).toString(), QStringLiteral("#80112233"));
        QVERIFY(colorsOut.contains(QStringLiteral("UseSystem")));

        // The runner gates BEFORE dispatching, so the assertions above never
        // reach migrateV5ToV6's own in-function guard. Exercise that guard
        // directly: a _version >= 6 root handed straight to the step must
        // come back byte-identical, or the defense-in-depth idempotency the
        // guard exists for is unpinned (deleting it would leave the suite
        // green).
        QJsonObject direct = makeV5Config(colors, {});
        direct[QStringLiteral("_version")] = 6;
        const QJsonObject before = direct;
        ConfigMigration::migrateV5ToV6(direct);
        QCOMPARE(direct, before);
    }
};

QTEST_MAIN(TestMigrationV5ToV6)
#include "test_migration_v5_to_v6.moc"
