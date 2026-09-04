// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "core/types/overlayshadertree.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

/// v7 → v8: overlay shader assignments lift out of the layout-settings
/// sidecar into the config's Snapping.OverlayShaders/OverlayShaderTree blob
/// (relocateOverlayShaderAssignments), plus the OverlayShaderTree value
/// type's own JSON round-trip and resolve contracts.
class TestOverlayShaderRelocation : public QObject
{
    Q_OBJECT

private:
    static QJsonObject readJson(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    static QByteArray readBytes(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return f.readAll();
    }

    // [[nodiscard]] bool rather than QVERIFY-in-void: a QVERIFY failure in a
    // void helper returns from the HELPER only, letting the slot continue
    // against a missing fixture (see test_migration_v5_to_v6.cpp's note).
    [[nodiscard]] static bool writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
        return f.write(QJsonDocument(obj).toJson()) >= 0;
    }

    [[nodiscard]] static bool writeRaw(const QString& path, const QByteArray& bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
        return f.write(bytes) >= 0;
    }

    static QJsonObject treeFromConfig(const QJsonObject& root)
    {
        return root.value(QStringLiteral("Snapping"))
            .toObject()
            .value(QStringLiteral("OverlayShaders"))
            .toObject()
            .value(QStringLiteral("OverlayShaderTree"))
            .toObject();
    }

    static const QString& layoutA()
    {
        static const QString id = QStringLiteral("{aaaa0000-0000-0000-0000-000000000000}");
        return id;
    }
    static const QString& layoutB()
    {
        static const QString id = QStringLiteral("{bbbb0000-0000-0000-0000-000000000000}");
        return id;
    }

    /// A v8-stamped config root plus a sidecar carrying: shader keys for
    /// layoutA (with params), an empty-shader entry for layoutB, a clean
    /// entry, an "autotile:*" entry with a shader (the pre-v8 editor could
    /// stamp those), a params-only entry with no shaderId, and the store's
    /// own _version stamp.
    [[nodiscard]] bool seedFixture()
    {
        if (!writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}})) {
            return false;
        }
        const QJsonObject params{{QStringLiteral("intensity"), 0.5}};
        return writeJson(ConfigDefaults::layoutSettingsFilePath(),
                         QJsonObject{
                             {QStringLiteral("_version"), 1},
                             {layoutA(),
                              QJsonObject{{QStringLiteral("zonePadding"), 8},
                                          {QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")},
                                          {QStringLiteral("shaderParams"), params}}},
                             {layoutB(), QJsonObject{{QStringLiteral("shaderId"), QString()}}},
                             {QStringLiteral("{cccc0000-0000-0000-0000-000000000000}"),
                              QJsonObject{{QStringLiteral("zonePadding"), 4}}},
                             {QStringLiteral("autotile:master-stack"),
                              QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}},
                             {QStringLiteral("{dddd0000-0000-0000-0000-000000000000}"),
                              QJsonObject{{QStringLiteral("shaderParams"), params}}},
                         });
    }

private Q_SLOTS:
    void testLift_movesShaderEntriesAndStripsSidecar()
    {
        IsolatedConfigGuard guard;
        QVERIFY(seedFixture());

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));

        const QJsonObject tree = treeFromConfig(readJson(ConfigDefaults::configFilePath()));
        const QJsonObject overrides = tree.value(QStringLiteral("overrides")).toObject();
        const QJsonObject nodeA = overrides.value(layoutA()).toObject();
        QCOMPARE(nodeA.value(QStringLiteral("shaderId")).toString(), QStringLiteral("cosmic-flow"));
        QCOMPARE(nodeA.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("intensity")).toDouble(),
                 0.5);
        // The empty-shader entry meant "no shader" — stripped, never lifted.
        QVERIFY(!overrides.contains(layoutB()));
        // Non-UUID (autotile) keys and params-only entries are stripped
        // without lifting: an autotile override could never be resolved and
        // orphaned params are meaningless without a shader.
        QVERIFY(!overrides.contains(QStringLiteral("autotile:master-stack")));
        QVERIFY(!overrides.contains(QStringLiteral("{dddd0000-0000-0000-0000-000000000000}")));
        QCOMPARE(overrides.size(), 1);
        // No baseline is synthesised.
        QVERIFY(!tree.contains(QStringLiteral("baseline")));

        // Sidecar: shader keys gone, unrelated keys intact, the emptied
        // entries pruned, the store's _version stamp untouched.
        const QJsonObject sidecar = readJson(ConfigDefaults::layoutSettingsFilePath());
        QVERIFY(!sidecar.value(layoutA()).toObject().contains(QStringLiteral("shaderId")));
        QVERIFY(!sidecar.value(layoutA()).toObject().contains(QStringLiteral("shaderParams")));
        QCOMPARE(sidecar.value(layoutA()).toObject().value(QStringLiteral("zonePadding")).toInt(), 8);
        QVERIFY(!sidecar.contains(layoutB()));
        QVERIFY(!sidecar.contains(QStringLiteral("autotile:master-stack")));
        QVERIFY(!sidecar.contains(QStringLiteral("{dddd0000-0000-0000-0000-000000000000}")));
        QCOMPARE(sidecar.value(QStringLiteral("_version")).toInt(), 1);
        // The tree parses through the runtime value type.
        const OverlayShaderTree parsed = OverlayShaderTree::fromJson(tree);
        QCOMPARE(parsed.resolve(layoutA()).shaderId, QStringLiteral("cosmic-flow"));
    }

    void testLift_isIdempotentAndKeepsEditedTreeEntry()
    {
        IsolatedConfigGuard guard;
        QVERIFY(seedFixture());
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        const QByteArray afterFirstBytes = readBytes(ConfigDefaults::configFilePath());
        const QJsonObject afterFirst = readJson(ConfigDefaults::configFilePath());

        // Second run: nothing left to move, byte-identical config (pins the
        // sidecarDirty short-circuit, not just content equality).
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QCOMPARE(readBytes(ConfigDefaults::configFilePath()), afterFirstBytes);

        // A retry against a STALE sidecar (pass 2 failed scenario): the
        // already-lifted tree entry wins — a since-edited assignment must
        // not be clobbered by the sidecar's old copy.
        QJsonObject root = afterFirst;
        QJsonObject snapping = root.value(QStringLiteral("Snapping")).toObject();
        QJsonObject group = snapping.value(QStringLiteral("OverlayShaders")).toObject();
        QJsonObject tree = group.value(QStringLiteral("OverlayShaderTree")).toObject();
        QJsonObject overrides = tree.value(QStringLiteral("overrides")).toObject();
        overrides.insert(layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("neon-city")}});
        tree.insert(QStringLiteral("overrides"), overrides);
        group.insert(QStringLiteral("OverlayShaderTree"), tree);
        snapping.insert(QStringLiteral("OverlayShaders"), group);
        root.insert(QStringLiteral("Snapping"), snapping);
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), root));
        // Restore the stale sidecar copy.
        QVERIFY(writeJson(
            ConfigDefaults::layoutSettingsFilePath(),
            QJsonObject{{layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}}}));

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        const QJsonObject node = treeFromConfig(readJson(ConfigDefaults::configFilePath()))
                                     .value(QStringLiteral("overrides"))
                                     .toObject()
                                     .value(layoutA())
                                     .toObject();
        QCOMPARE(node.value(QStringLiteral("shaderId")).toString(), QStringLiteral("neon-city"));
        // The stale sidecar entry is still stripped.
        QVERIFY(!readJson(ConfigDefaults::layoutSettingsFilePath()).contains(layoutA()));
    }

    void testLift_removedOverrideIsNotResurrectedOnRetry()
    {
        IsolatedConfigGuard guard;
        QVERIFY(seedFixture());
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));

        // Simulate a failed sidecar strip: restore the stale sidecar copy...
        QVERIFY(writeJson(
            ConfigDefaults::layoutSettingsFilePath(),
            QJsonObject{{layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}}}));
        // ...and the user then REMOVING the lifted override via the UI
        // (which, with only this override and no baseline, sparse-deletes
        // the whole tree key but leaves the group's lifted marker).
        QJsonObject root = readJson(ConfigDefaults::configFilePath());
        QJsonObject snapping = root.value(QStringLiteral("Snapping")).toObject();
        QJsonObject group = snapping.value(QStringLiteral("OverlayShaders")).toObject();
        group.remove(QStringLiteral("OverlayShaderTree"));
        snapping.insert(QStringLiteral("OverlayShaders"), group);
        root.insert(QStringLiteral("Snapping"), snapping);
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), root));

        // The retry must strip the stale sidecar WITHOUT re-lifting the
        // removed assignment (the group's SidecarLifted marker gates the
        // merge to at most one run).
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QVERIFY(treeFromConfig(readJson(ConfigDefaults::configFilePath())).isEmpty());
        QVERIFY(!readJson(ConfigDefaults::layoutSettingsFilePath()).contains(layoutA()));
    }

    void testLift_missingSidecarIsNoOpSuccess()
    {
        IsolatedConfigGuard guard;
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}}));
        const QByteArray before = readBytes(ConfigDefaults::configFilePath());
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        // The config is not rewritten (no spurious empty-group write).
        QCOMPARE(readBytes(ConfigDefaults::configFilePath()), before);
        QVERIFY(treeFromConfig(readJson(ConfigDefaults::configFilePath())).isEmpty());
    }

    void testLift_missingConfigLeavesSidecarForRetry()
    {
        IsolatedConfigGuard guard;
        // Sidecar with a pending lift but NO config file (interrupted fresh
        // install): the relocation reports success, does nothing, and leaves
        // the sidecar intact so a later run can retry.
        const QJsonObject sidecar{
            {layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}}};
        QVERIFY(writeJson(ConfigDefaults::layoutSettingsFilePath(), sidecar));
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QVERIFY(!QFile::exists(ConfigDefaults::configFilePath()));
        QCOMPARE(readJson(ConfigDefaults::layoutSettingsFilePath()), sidecar);
    }

    void testLift_corruptFilesAreHandled()
    {
        IsolatedConfigGuard guard;
        // Unparseable sidecar: skipped with success, left untouched.
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}}));
        QVERIFY(writeRaw(ConfigDefaults::layoutSettingsFilePath(), QByteArrayLiteral("{not json")));
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QCOMPARE(readBytes(ConfigDefaults::layoutSettingsFilePath()), QByteArrayLiteral("{not json"));

        // Unparseable CONFIG with a pending lift: the relocation fails
        // WITHOUT stripping the sidecar (a strip-before-lift regression
        // would lose the assignment).
        const QJsonObject sidecar{
            {layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}}};
        QVERIFY(writeJson(ConfigDefaults::layoutSettingsFilePath(), sidecar));
        QVERIFY(writeRaw(ConfigDefaults::configFilePath(), QByteArrayLiteral("{not json")));
        QVERIFY(!ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QCOMPARE(readJson(ConfigDefaults::layoutSettingsFilePath()), sidecar);
    }

    void testLift_runsFromEnsureJsonConfig()
    {
        IsolatedConfigGuard guard;
        // End-to-end wiring: a v6-stamped config plus a fat sidecar, driven
        // through ensureJsonConfig (not the relocation function directly).
        // Deleting the finalize-path relocate calls must fail this test.
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 6}}));
        QVERIFY(writeJson(ConfigDefaults::layoutSettingsFilePath(),
                          QJsonObject{{layoutA(),
                                       QJsonObject{{QStringLiteral("zonePadding"), 8},
                                                   {QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}}}));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), ConfigSchemaVersion);
        const QJsonObject overrides = treeFromConfig(root).value(QStringLiteral("overrides")).toObject();
        QCOMPARE(overrides.value(layoutA()).toObject().value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("cosmic-flow"));
        const QJsonObject sidecar = readJson(ConfigDefaults::layoutSettingsFilePath());
        QVERIFY(!sidecar.value(layoutA()).toObject().contains(QStringLiteral("shaderId")));
        QCOMPARE(sidecar.value(layoutA()).toObject().value(QStringLiteral("zonePadding")).toInt(), 8);
    }

    // ── OverlayShaderTree value-type contracts ───────────────────────────

    void testTree_jsonRoundTripAndEquality()
    {
        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("cosmic-flow"), {{QStringLiteral("speed"), 1.5}}});
        tree.setOverride(layoutA(), {QStringLiteral("neon-city"), {}});
        tree.setOverride(layoutB(), {QString(), {}}); // explicit "no shader"

        const OverlayShaderTree back = OverlayShaderTree::fromJson(tree.toJson());
        QVERIFY(back == tree);
        QCOMPARE(back.baseline().shaderId, QStringLiteral("cosmic-flow"));
        QCOMPARE(back.baseline().parameters.value(QStringLiteral("speed")).toDouble(), 1.5);
        QVERIFY(back.hasOverride(layoutA()));
        // An engaged empty-id override round-trips: it suppresses the
        // baseline for that layout, so it must not collapse away. Its node
        // serializes empty, so fromJson keeps the KEY with an empty profile.
        QVERIFY(back.hasOverride(layoutB()));
        QVERIFY(back.resolve(layoutB()).shaderId.isEmpty());
    }

    void testTree_resolvePrecedence()
    {
        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("baseline-pack"), {}});
        tree.setOverride(layoutA(), {QStringLiteral("override-pack"), {{QStringLiteral("k"), 1}}});

        // Override wins wholly (id AND params); unknown layout falls back.
        QCOMPARE(tree.resolve(layoutA()).shaderId, QStringLiteral("override-pack"));
        QCOMPARE(tree.resolve(layoutA()).parameters.value(QStringLiteral("k")).toInt(), 1);
        QCOMPARE(tree.resolve(layoutB()).shaderId, QStringLiteral("baseline-pack"));
        // clearOverride restores baseline inheritance.
        QVERIFY(tree.clearOverride(layoutA()));
        QCOMPARE(tree.resolve(layoutA()).shaderId, QStringLiteral("baseline-pack"));
        QVERIFY(!tree.clearOverride(layoutA()));
    }
};

QTEST_MAIN(TestOverlayShaderRelocation)
#include "test_overlayshader_relocation.moc"
