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

/// v6 → v7: overlay shader assignments lift out of the layout-settings
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

    static void writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(obj).toJson());
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

    /// A v7-stamped config root plus a sidecar carrying shader keys for
    /// layoutA (with params), an empty-shader entry for layoutB, and one
    /// clean entry.
    void seedFixture()
    {
        writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 7}});
        const QJsonObject params{{QStringLiteral("intensity"), 0.5}};
        writeJson(ConfigDefaults::layoutSettingsFilePath(),
                  QJsonObject{
                      {layoutA(),
                       QJsonObject{{QStringLiteral("zonePadding"), 8},
                                   {QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")},
                                   {QStringLiteral("shaderParams"), params}}},
                      {layoutB(), QJsonObject{{QStringLiteral("shaderId"), QString()}}},
                      {QStringLiteral("{cccc0000-0000-0000-0000-000000000000}"),
                       QJsonObject{{QStringLiteral("zonePadding"), 4}}},
                  });
    }

private Q_SLOTS:
    void testLift_movesShaderEntriesAndStripsSidecar()
    {
        IsolatedConfigGuard guard;
        seedFixture();

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));

        const QJsonObject tree = treeFromConfig(readJson(ConfigDefaults::configFilePath()));
        const QJsonObject overrides = tree.value(QStringLiteral("overrides")).toObject();
        const QJsonObject nodeA = overrides.value(layoutA()).toObject();
        QCOMPARE(nodeA.value(QStringLiteral("shaderId")).toString(), QStringLiteral("cosmic-flow"));
        QCOMPARE(nodeA.value(QStringLiteral("parameters")).toObject().value(QStringLiteral("intensity")).toDouble(),
                 0.5);
        // The empty-shader entry meant "no shader" — stripped, never lifted.
        QVERIFY(!overrides.contains(layoutB()));
        // No baseline is synthesised.
        QVERIFY(!tree.contains(QStringLiteral("baseline")));

        // Sidecar: shader keys gone, unrelated keys intact, the emptied
        // layoutB entry pruned.
        const QJsonObject sidecar = readJson(ConfigDefaults::layoutSettingsFilePath());
        QVERIFY(!sidecar.value(layoutA()).toObject().contains(QStringLiteral("shaderId")));
        QVERIFY(!sidecar.value(layoutA()).toObject().contains(QStringLiteral("shaderParams")));
        QCOMPARE(sidecar.value(layoutA()).toObject().value(QStringLiteral("zonePadding")).toInt(), 8);
        QVERIFY(!sidecar.contains(layoutB()));
        // The tree parses through the runtime value type.
        const OverlayShaderTree parsed = OverlayShaderTree::fromJson(tree);
        QCOMPARE(parsed.resolve(layoutA()).shaderId, QStringLiteral("cosmic-flow"));
    }

    void testLift_isIdempotentAndKeepsEditedTreeEntry()
    {
        IsolatedConfigGuard guard;
        seedFixture();
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        const QJsonObject afterFirst = readJson(ConfigDefaults::configFilePath());

        // Second run: nothing left to move, no writes, identical output.
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QCOMPARE(readJson(ConfigDefaults::configFilePath()), afterFirst);

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
        writeJson(ConfigDefaults::configFilePath(), root);
        // Restore the stale sidecar copy.
        writeJson(ConfigDefaults::layoutSettingsFilePath(),
                  QJsonObject{{layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}}});

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

    void testLift_missingSidecarIsNoOpSuccess()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 7}});
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QVERIFY(treeFromConfig(readJson(ConfigDefaults::configFilePath())).isEmpty());
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
