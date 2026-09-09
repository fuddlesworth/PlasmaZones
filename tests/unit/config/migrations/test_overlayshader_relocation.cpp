// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

#include <PhosphorRules/ActionParams.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "core/types/overlayshadertree.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

/// v7 → v8: overlay shader assignments lift out of the layout-settings
/// sidecar into the config's Overlays/OverlayShaderTree blob
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
        return groupFromConfig(root).value(ConfigDefaults::overlayShaderTreeKey()).toObject();
    }

    /// The overlay group object inside a config root. Walks the group's
    /// dot-path segments rather than hardcoding the nesting, so a group rename
    /// moves these fixtures with it instead of silently pointing them at a
    /// subtree that no longer holds the data.
    static QJsonObject groupFromConfig(const QJsonObject& root)
    {
        QJsonObject obj = root;
        const QStringList segments = ConfigDefaults::overlaysGroup().split(QLatin1Char('.'));
        for (const QString& segment : segments)
            obj = obj.value(segment).toObject();
        return obj;
    }

    /// @p root with the overlay group replaced by @p group — the write half of
    /// groupFromConfig, rebuilding the same dot-path.
    static QJsonObject withGroup(const QJsonObject& root, const QJsonObject& group)
    {
        const QStringList segments = ConfigDefaults::overlaysGroup().split(QLatin1Char('.'));
        QList<QJsonObject> ancestors;
        QJsonObject cursor = root;
        for (const QString& segment : segments) {
            ancestors.append(cursor);
            cursor = cursor.value(segment).toObject();
        }
        cursor = group;
        for (int i = segments.size() - 1; i >= 0; --i) {
            QJsonObject parent = ancestors.at(i);
            parent.insert(segments.at(i), cursor);
            cursor = parent;
        }
        return cursor;
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
        // raw-bytes bail, not just content equality: the stripped sidecar no
        // longer contains the key bytes, so the second run never reaches the
        // JSON parse at all).
        //
        // The field names below are deliberately literals rather than
        // OverlayShaderTree's JsonField constants. Half the fixtures here are
        // SIDECAR objects, whose "shaderId"/"shaderParams" are the migration's
        // own pinned spellings and only coincidentally match the tree node's
        // fields — the migration keeps kSidecarShaderId and kNodeShaderId
        // separate for that reason, and using the tree's constants for the
        // sidecar would tie the two together in exactly the way it avoids.
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QCOMPARE(readBytes(ConfigDefaults::configFilePath()), afterFirstBytes);

        // A retry against a STALE sidecar (pass 2 failed scenario): the
        // already-lifted tree entry wins — a since-edited assignment must
        // not be clobbered by the sidecar's old copy.
        QJsonObject group = groupFromConfig(afterFirst);
        QJsonObject tree = group.value(ConfigDefaults::overlayShaderTreeKey()).toObject();
        QJsonObject overrides = tree.value(QStringLiteral("overrides")).toObject();
        overrides.insert(layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("neon-city")}});
        tree.insert(QStringLiteral("overrides"), overrides);
        group.insert(ConfigDefaults::overlayShaderTreeKey(), tree);
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), withGroup(afterFirst, group)));
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

    /// The marker records WHICH ids were merged, not merely that a merge
    /// happened, so an assignment reaching the sidecar after the first run
    /// (a restored backup, a layout file copied from another machine, a
    /// reappearing system layout) is still lifted rather than stripped away
    /// silently. A bare "already lifted" flag would drop it.
    void testLift_assignmentArrivingAfterTheMarkerIsStillLifted()
    {
        IsolatedConfigGuard guard;
        QVERIFY(seedFixture());
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QVERIFY(treeFromConfig(readJson(ConfigDefaults::configFilePath()))
                    .value(QStringLiteral("overrides"))
                    .toObject()
                    .contains(layoutA()));

        // A layout the first run never saw turns up in the sidecar afterwards.
        const QString newcomer = QStringLiteral("{eeee0000-0000-0000-0000-000000000000}");
        QVERIFY(
            writeJson(ConfigDefaults::layoutSettingsFilePath(),
                      QJsonObject{{newcomer, QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("aurora")}}}}));

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        const QJsonObject overrides =
            treeFromConfig(readJson(ConfigDefaults::configFilePath())).value(QStringLiteral("overrides")).toObject();
        QCOMPARE(overrides.value(newcomer).toObject().value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("aurora"));
        // The first run's entry is untouched, and the newcomer's sidecar copy
        // is stripped like any other.
        QVERIFY(overrides.contains(layoutA()));
        QVERIFY(!readJson(ConfigDefaults::layoutSettingsFilePath()).contains(newcomer));
    }

    /// An entry already in the tree wins over the sidecar's copy even when the
    /// marker has never seen that id — the config is authoritative, so a
    /// value the user edited is not overwritten by the stale sidecar it came
    /// from. Distinct from the marker test below: this drives the
    /// already-present check with an UNMARKED config, which is the only shape
    /// that reaches it. A fixture whose marker is already stamped
    /// short-circuits the whole merge loop and proves the marker instead.
    void testLift_existingTreeEntryWinsOverTheSidecar()
    {
        IsolatedConfigGuard guard;
        // Config carries an override for layoutA and a baseline, with NO
        // marker: the state a hand-edited config, or one written by a newer
        // settings app that never migrated, is in.
        QJsonObject overrides{{layoutA(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("neon-city")}}}};
        QJsonObject tree{
            {QStringLiteral("baseline"), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("glow")}}},
            {QStringLiteral("overrides"), overrides}};
        QJsonObject group{{ConfigDefaults::overlayShaderTreeKey(), tree}};
        QJsonObject root{{QStringLiteral("_version"), 8}};
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), withGroup(root, group)));

        // Sidecar disagrees about layoutA and carries a genuinely new layoutB.
        QVERIFY(writeJson(ConfigDefaults::layoutSettingsFilePath(),
                          QJsonObject{
                              {layoutA(),
                               QJsonObject{{QStringLiteral("zonePadding"), 8},
                                           {QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")}}},
                              {layoutB(), QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("aurora")}}},
                          }));

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));

        const QJsonObject after = treeFromConfig(readJson(ConfigDefaults::configFilePath()));
        const QJsonObject afterOverrides = after.value(QStringLiteral("overrides")).toObject();
        // The edited copy survives — this is the assertion that fails if the
        // already-present check is deleted.
        QCOMPARE(afterOverrides.value(layoutA()).toObject().value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("neon-city"));
        // ...and the loop still ran, so the guard is not masking a dead merge.
        QCOMPARE(afterOverrides.value(layoutB()).toObject().value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("aurora"));
        // A pre-existing baseline is not discarded by the lift.
        QCOMPARE(after.value(QStringLiteral("baseline")).toObject().value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("glow"));
        // Unrelated sidecar keys survive the strip.
        const QJsonObject sidecar = readJson(ConfigDefaults::layoutSettingsFilePath());
        QCOMPARE(sidecar.value(layoutA()).toObject().value(QStringLiteral("zonePadding")).toInt(), 8);
        QVERIFY(!sidecar.value(layoutA()).toObject().contains(QStringLiteral("shaderId")));
    }

    /// QUuid::fromString accepts an unbraced spelling while every reader asks
    /// with the braced one, so the lift must key the tree on the canonical
    /// form or the entry would sit there matching nothing.
    void testLift_unbracedSidecarKeyIsNormalized()
    {
        IsolatedConfigGuard guard;
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}}));
        QVERIFY(writeJson(ConfigDefaults::layoutSettingsFilePath(),
                          QJsonObject{{QStringLiteral("ffff0000-0000-0000-0000-000000000000"),
                                       QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("aurora")}}}}));

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        const QJsonObject overrides =
            treeFromConfig(readJson(ConfigDefaults::configFilePath())).value(QStringLiteral("overrides")).toObject();
        QVERIFY2(overrides.contains(QStringLiteral("{ffff0000-0000-0000-0000-000000000000}")),
                 "unbraced sidecar key was not normalized to the braced form the resolver uses");
        QVERIFY2(!overrides.contains(QStringLiteral("ffff0000-0000-0000-0000-000000000000")),
                 "the unbraced spelling survived alongside the braced one");
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
        const QJsonObject root = readJson(ConfigDefaults::configFilePath());
        QJsonObject group = groupFromConfig(root);
        group.remove(ConfigDefaults::overlayShaderTreeKey());
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), withGroup(root, group)));

        // The retry must strip the stale sidecar WITHOUT re-lifting the
        // removed assignment (the group's SidecarLifted marker gates the
        // merge to at most one run).
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QVERIFY(treeFromConfig(readJson(ConfigDefaults::configFilePath())).isEmpty());
        QVERIFY(!readJson(ConfigDefaults::layoutSettingsFilePath()).contains(layoutA()));
    }

    /// The `!sidecarDirty` bail between the raw-bytes scan and the lift. The
    /// scan only proves the key NAMES appear somewhere in the bytes, so a
    /// sidecar that spells one as a VALUE reaches the parse, produces no
    /// liftable entry and no strippable key, and must leave both files exactly
    /// as they were. Without this slot the branch is unreachable in the suite
    /// and deleting it (falling through to the lift and the unconditional
    /// re-strip write) stays green.
    void testLift_shaderKeyNamesAppearingOnlyAsValuesAreNotAStrip()
    {
        IsolatedConfigGuard guard;
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}}));
        // "shaderId" and "shaderParams" both appear in the bytes, neither as a key.
        const QJsonObject sidecar{
            {layoutA(),
             QJsonObject{{QStringLiteral("note"), QStringLiteral("shaderId and shaderParams moved to the config")}}}};
        QVERIFY(writeJson(ConfigDefaults::layoutSettingsFilePath(), sidecar));
        const QByteArray configBefore = readBytes(ConfigDefaults::configFilePath());
        const QByteArray sidecarBefore = readBytes(ConfigDefaults::layoutSettingsFilePath());

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));

        QCOMPARE(readBytes(ConfigDefaults::configFilePath()), configBefore);
        QCOMPARE(readBytes(ConfigDefaults::layoutSettingsFilePath()), sidecarBefore);
    }

    /// The `params.isObject() && !params.toObject().isEmpty()` guard on the
    /// lifted node, on both of its false arms. An empty object and a non-object
    /// must both lift the shaderId with NO parameters key, and must still be
    /// stripped from the sidecar. Neither arm has a fixture otherwise, so
    /// dropping either half of the guard stays green.
    void testLift_emptyAndNonObjectShaderParamsLiftWithoutParameters()
    {
        IsolatedConfigGuard guard;
        const QString layoutC = QStringLiteral("{cccc1111-0000-0000-0000-000000000000}");
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}}));
        QVERIFY(writeJson(ConfigDefaults::layoutSettingsFilePath(),
                          QJsonObject{
                              {layoutA(),
                               QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("aurora")},
                                           {QStringLiteral("shaderParams"), QJsonObject{}}}},
                              {layoutC,
                               QJsonObject{{QStringLiteral("shaderId"), QStringLiteral("cosmic-flow")},
                                           {QStringLiteral("shaderParams"), QStringLiteral("not-an-object")}}},
                          }));

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));

        const QJsonObject overrides =
            treeFromConfig(readJson(ConfigDefaults::configFilePath())).value(QStringLiteral("overrides")).toObject();
        const QJsonObject nodeA = overrides.value(layoutA()).toObject();
        QCOMPARE(nodeA.value(QStringLiteral("shaderId")).toString(), QStringLiteral("aurora"));
        QVERIFY2(!nodeA.contains(QStringLiteral("parameters")),
                 "an empty shaderParams object was lifted as an empty parameters node");
        const QJsonObject nodeC = overrides.value(layoutC).toObject();
        QCOMPARE(nodeC.value(QStringLiteral("shaderId")).toString(), QStringLiteral("cosmic-flow"));
        QVERIFY2(!nodeC.contains(QStringLiteral("parameters")), "a non-object shaderParams reached the lifted node");

        const QJsonObject sidecar = readJson(ConfigDefaults::layoutSettingsFilePath());
        QVERIFY(!sidecar.contains(layoutA()));
        QVERIFY(!sidecar.contains(layoutC));
    }

    /// The rules half of the relocation. A rule written against the old
    /// shape, where the overlay shader was a property of the layout and the
    /// action carried only the shader, is rewritten onto the tree's node
    /// shape with the global-default node made explicit (`layoutId: ""`),
    /// which is the faithful translation: "this context, whichever layout"
    /// becomes "this context, every layout". Everything else on the action
    /// and on the rule survives verbatim; a rule already carrying a node is
    /// left alone; a sibling action of another type is not touched; and the
    /// second run is byte-identical, so the rewrite is a one-time migration
    /// rather than a rewrite on every start. Runs with NO sidecar present,
    /// which pins that the sidecar's early return cannot skip it.
    void testLift_oldShapeOverlayRulesBecomeGlobalNodeRules()
    {
        namespace PWR = PhosphorRules;
        IsolatedConfigGuard guard;
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}}));

        const auto makeRule = [](const QString& name, const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 500;
            r.match =
                PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
            r.actions = actions;
            return r;
        };
        PWR::RuleAction oldShape;
        oldShape.type = QString(PWR::ActionType::OverrideOverlayShader);
        oldShape.params.insert(QString(PWR::ActionParam::EffectId), QStringLiteral("cosmic-flow"));
        oldShape.params.insert(QString(PWR::ActionParam::Params), QJsonObject{{QStringLiteral("speed"), 1.5}});
        PWR::RuleAction sibling;
        sibling.type = QString(PWR::ActionType::OverrideOverlayStyle);
        sibling.params.insert(QString(PWR::ActionParam::Value), QString(PWR::OverlayStyleToken::Preview));
        PWR::RuleAction alreadyNode;
        alreadyNode.type = QString(PWR::ActionType::OverrideOverlayShader);
        alreadyNode.params.insert(QString(PWR::ActionParam::LayoutId), layoutA());
        alreadyNode.params.insert(QString(PWR::ActionParam::EffectId), QStringLiteral("neon-city"));

        PWR::RuleSet seed;
        QVERIFY(seed.addRule(makeRule(QStringLiteral("old"), {oldShape, sibling})));
        QVERIFY(seed.addRule(makeRule(QStringLiteral("node"), {alreadyNode})));
        QVERIFY(seed.saveToFile(ConfigDefaults::rulesFilePath()));

        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));

        const auto loaded = PWR::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->count(), 2);
        for (const PWR::Rule& rule : loaded->rules()) {
            if (rule.name == QStringLiteral("old")) {
                QCOMPARE(rule.actions.size(), 2);
                const QJsonObject params = rule.actions.at(0).params;
                QVERIFY2(params.contains(QString(PWR::ActionParam::LayoutId)),
                         "the old-shape rule was not rewritten onto the node shape");
                QVERIFY2(params.value(QString(PWR::ActionParam::LayoutId)).toString().isEmpty(),
                         "the old-shape rule was pinned to a layout rather than the global node");
                QCOMPARE(params.value(QString(PWR::ActionParam::EffectId)).toString(), QStringLiteral("cosmic-flow"));
                QCOMPARE(params.value(QString(PWR::ActionParam::Params))
                             .toObject()
                             .value(QStringLiteral("speed"))
                             .toDouble(),
                         1.5);
                // The sibling action is another type and is not the rewrite's
                // business, so it must not have grown a node.
                QVERIFY(!rule.actions.at(1).params.contains(QString(PWR::ActionParam::LayoutId)));
                QCOMPARE(rule.actions.at(1).params.value(QString(PWR::ActionParam::Value)).toString(),
                         QString(PWR::OverlayStyleToken::Preview));
                QVERIFY(rule.enabled);
                QCOMPARE(rule.priority, 500);
            } else {
                QCOMPARE(rule.name, QStringLiteral("node"));
                QCOMPARE(rule.actions.at(0).params.value(QString(PWR::ActionParam::LayoutId)).toString(), layoutA());
                QCOMPARE(rule.actions.at(0).params.value(QString(PWR::ActionParam::EffectId)).toString(),
                         QStringLiteral("neon-city"));
            }
        }

        // One-time: nothing is left in the old shape, so the second run finds
        // nothing to rewrite and does not touch the file.
        const QByteArray after = readBytes(ConfigDefaults::rulesFilePath());
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QCOMPARE(readBytes(ConfigDefaults::rulesFilePath()), after);
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
        //
        // The bytes must CONTAIN a shader key, or the cheap raw-bytes scan
        // bails before the parse is ever attempted and this asserts nothing
        // about the parse-error arm. Both bails return true leaving both files
        // untouched, so the byte comparison alone cannot tell them apart —
        // ignoreMessage is what pins which one ran, and it FAILS if the
        // warning never arrives.
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), QJsonObject{{QStringLiteral("_version"), 8}}));
        const QByteArray corruptWithKey = QByteArrayLiteral("{\"a\":{\"shaderId\": broken");
        QVERIFY(writeRaw(ConfigDefaults::layoutSettingsFilePath(), corruptWithKey));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("relocation skipping unparseable")));
        QVERIFY(ConfigMigration::relocateOverlayShaderAssignments(ConfigDefaults::configFilePath()));
        QCOMPARE(readBytes(ConfigDefaults::layoutSettingsFilePath()), corruptWithKey);

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
