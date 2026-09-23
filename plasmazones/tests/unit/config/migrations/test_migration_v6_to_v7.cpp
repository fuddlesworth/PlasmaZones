// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v6_to_v7.cpp
 * @brief Unit tests for the v6 → v7 schema migration (config → config), and for
 *        the rules.json event-path rename that rides finalizeV4Conversion's
 *        idempotent cleanup path alongside it.
 *
 * v7 renames the two window-movement placement nodes (`snapIn` → `placeIn`,
 * `snapOut` → `placeOut`) and retires `window.movement.maximize`. The
 * migration must:
 *   - rewrite the `path` of each renamed override in the ShaderProfileTree
 *     blob under Animations, keeping its profile verbatim;
 *   - fold a `maximize` override into `placeIn` when no placeIn override will
 *     otherwise exist, and DROP it when one will (the placement node's own
 *     assignment is the more general statement);
 *   - leave a config with no Animations group, or a non-object tree value,
 *     alone apart from the stamp;
 *   - stamp the literal 7 (the historical step's frozen output).
 * Rule actions scoped to one of the retired events are renamed the same way
 * in rules.json, which the version chain does not cover, so that runs from the
 * same cleanup branch as the retired-rule prune and is idempotent.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUuid>

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/ActionParams.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace PWR = PhosphorRules;
namespace CRB = PhosphorRules::ContextRuleBridge;

namespace {
const QString kSnapIn = QStringLiteral("window.movement.snapIn");
const QString kSnapOut = QStringLiteral("window.movement.snapOut");
const QString kMaximize = QStringLiteral("window.movement.maximize");
const QString kPlaceIn = QStringLiteral("window.movement.placeIn");
const QString kPlaceOut = QStringLiteral("window.movement.placeOut");
const QString kUnrelated = QStringLiteral("window.appearance.open");
} // namespace

class TestMigrationV6ToV7 : public QObject
{
    Q_OBJECT

private:
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

    QJsonObject overrideEntry(const QString& path, const QString& effectId)
    {
        QJsonObject profile;
        profile.insert(QStringLiteral("effectId"), effectId);
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), path);
        entry.insert(QStringLiteral("profile"), profile);
        return entry;
    }

    /// A v6 config carrying a ShaderProfileTree with the given overrides, plus a
    /// sibling key in the Animations group so the group is observably kept.
    QJsonObject makeV6Config(const QJsonArray& overrides)
    {
        QJsonObject tree;
        tree.insert(QStringLiteral("baseline"), QJsonObject());
        tree.insert(QStringLiteral("overrides"), overrides);
        QJsonObject animations;
        animations.insert(QStringLiteral("ShaderProfileTree"), tree);
        animations.insert(QStringLiteral("Enabled"), true);
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 6);
        root.insert(QStringLiteral("Animations"), animations);
        return root;
    }

    QJsonArray overridesAfter(const QJsonObject& root)
    {
        return root.value(QStringLiteral("Animations"))
            .toObject()
            .value(QStringLiteral("ShaderProfileTree"))
            .toObject()
            .value(QStringLiteral("overrides"))
            .toArray();
    }

    /// Path → effectId map of an overrides array, for order-insensitive checks.
    QMap<QString, QString> pathsToEffects(const QJsonArray& overrides)
    {
        QMap<QString, QString> out;
        for (const QJsonValue& v : overrides) {
            const QJsonObject e = v.toObject();
            out.insert(e.value(QStringLiteral("path")).toString(),
                       e.value(QStringLiteral("profile")).toObject().value(QStringLiteral("effectId")).toString());
        }
        return out;
    }

    void seedRules(const QList<PWR::Rule>& rules)
    {
        PWR::RuleSet set;
        QCOMPARE(set.setRules(rules), static_cast<int>(rules.size()));
        QDir().mkpath(QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath());
        QVERIFY(set.saveToFile(ConfigDefaults::rulesFilePath()));
    }

    PWR::RuleSet loadRules()
    {
        const auto set = PWR::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        return set.value_or(PWR::RuleSet{});
    }

    /// A valid rule (a real assignment match, from the surviving factory) with an
    /// animation-shader action scoped to @p event appended. The factory derives
    /// the rule id from its context, so @p name doubles as the screen id to keep
    /// the three fixture rules distinct.
    PWR::Rule ruleWithShaderEvent(const QString& name, const QString& event)
    {
        PWR::Rule rule =
            CRB::makeAssignmentRule(name, name, 0, QString(), QStringLiteral("snapping"),
                                    QStringLiteral("{dp1-layout}"), QString(), CRB::kContextBandBase + 1, QString());
        PWR::RuleAction shader;
        shader.type = QString(PWR::ActionType::OverrideAnimationShader);
        shader.params.insert(QString(PWR::ActionParam::Event), event);
        shader.params.insert(QString(PWR::ActionParam::EffectId), QStringLiteral("window-morph"));
        rule.actions.append(shader);
        return rule;
    }

    QString shaderEventOf(const PWR::Rule& rule)
    {
        for (const PWR::RuleAction& a : rule.actions) {
            if (a.type == PWR::ActionType::OverrideAnimationShader) {
                return a.params.value(PWR::ActionParam::Event).toString();
            }
        }
        return {};
    }

private Q_SLOTS:
    void testRenamesPlacementNodesAndKeepsProfiles()
    {
        QJsonObject root = makeV6Config({overrideEntry(kSnapIn, QStringLiteral("slide")),
                                         overrideEntry(kSnapOut, QStringLiteral("fade")),
                                         overrideEntry(kUnrelated, QStringLiteral("genie"))});
        ConfigMigration::migrateV6ToV7(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 7);
        const QMap<QString, QString> after = pathsToEffects(overridesAfter(root));
        QCOMPARE(after.size(), 3);
        QCOMPARE(after.value(kPlaceIn), QStringLiteral("slide"));
        QCOMPARE(after.value(kPlaceOut), QStringLiteral("fade"));
        QCOMPARE(after.value(kUnrelated), QStringLiteral("genie"));
        QVERIFY(!after.contains(kSnapIn));
        QVERIFY(!after.contains(kSnapOut));
        // The sibling key in the group survives the rewrite.
        QVERIFY(root.value(QStringLiteral("Animations")).toObject().value(QStringLiteral("Enabled")).toBool());
    }

    void testMaximizeFoldsIntoPlaceInWhenAbsent()
    {
        QJsonObject root = makeV6Config({overrideEntry(kMaximize, QStringLiteral("pop"))});
        ConfigMigration::migrateV6ToV7(root);

        const QMap<QString, QString> after = pathsToEffects(overridesAfter(root));
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.value(kPlaceIn), QStringLiteral("pop"));
        QVERIFY(!after.contains(kMaximize));
    }

    void testMaximizeDroppedWhenPlacementAssignmentExists()
    {
        // snapIn (→ placeIn) is the more general statement and wins over the
        // maximize override, whichever order the two were stored in.
        QJsonObject root = makeV6Config(
            {overrideEntry(kMaximize, QStringLiteral("pop")), overrideEntry(kSnapIn, QStringLiteral("slide"))});
        ConfigMigration::migrateV6ToV7(root);

        const QMap<QString, QString> after = pathsToEffects(overridesAfter(root));
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.value(kPlaceIn), QStringLiteral("slide"));
    }

    void testNoAnimationsGroup_onlyStamps()
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 6);
        QJsonObject snapping;
        snapping.insert(QStringLiteral("Enabled"), true);
        root.insert(QStringLiteral("Snapping"), snapping);
        const QJsonObject before = root;
        ConfigMigration::migrateV6ToV7(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 7);
        QJsonObject expected = before;
        expected.insert(QStringLiteral("_version"), 7);
        QCOMPARE(root, expected);
    }

    void testNonObjectTreeLeftAlone()
    {
        QJsonObject animations;
        animations.insert(QStringLiteral("ShaderProfileTree"), QStringLiteral("garbage"));
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 6);
        root.insert(QStringLiteral("Animations"), animations);
        ConfigMigration::migrateV6ToV7(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 7);
        QCOMPARE(
            root.value(QStringLiteral("Animations")).toObject().value(QStringLiteral("ShaderProfileTree")).toString(),
            QStringLiteral("garbage"));
    }

    void testAlreadyV7_untouched()
    {
        QJsonObject root = makeV6Config({overrideEntry(kSnapIn, QStringLiteral("slide"))});
        root.insert(QStringLiteral("_version"), 7);
        const QJsonObject before = root;
        ConfigMigration::migrateV6ToV7(root);
        QCOMPARE(root, before);
    }

    void testEnsureJsonConfig_runsTheStepAndStampsCurrent()
    {
        IsolatedConfigGuard guard;
        QVERIFY(writeJson(ConfigDefaults::configFilePath(),
                          makeV6Config({overrideEntry(kSnapIn, QStringLiteral("slide"))})));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
        const QMap<QString, QString> after = pathsToEffects(overridesAfter(cfg));
        QCOMPARE(after.value(kPlaceIn), QStringLiteral("slide"));
        QVERIFY(!after.contains(kSnapIn));
    }

    void testRuleEventPathsRenamed_othersUntouched_idempotent()
    {
        IsolatedConfigGuard guard;
        const PWR::Rule snapRule = ruleWithShaderEvent(QStringLiteral("snap"), kSnapIn);
        const PWR::Rule maxRule = ruleWithShaderEvent(QStringLiteral("max"), kMaximize);
        const PWR::Rule openRule = ruleWithShaderEvent(QStringLiteral("open"), kUnrelated);
        seedRules({snapRule, maxRule, openRule});

        QJsonObject root;
        root.insert(QStringLiteral("_version"), PlasmaZones::ConfigSchemaVersion);
        QVERIFY(writeJson(ConfigDefaults::configFilePath(), root));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        PWR::RuleSet after = loadRules();
        QCOMPARE(after.rules().size(), 3);
        QCOMPARE(shaderEventOf(*after.ruleById(snapRule.id)), kPlaceIn);
        QCOMPARE(shaderEventOf(*after.ruleById(maxRule.id)), kPlaceIn);
        QCOMPARE(shaderEventOf(*after.ruleById(openRule.id)), kUnrelated);
        // Priority, enabled flag and the assignment action ride along.
        QCOMPARE(after.ruleById(snapRule.id)->priority, snapRule.priority);
        QCOMPARE(after.ruleById(snapRule.id)->actions.size(), snapRule.actions.size());

        // Second run: a clean no-op, nothing renamed twice and nothing lost.
        QVERIFY(ConfigMigration::ensureJsonConfig());
        after = loadRules();
        QCOMPARE(after.rules().size(), 3);
        QCOMPARE(shaderEventOf(*after.ruleById(snapRule.id)), kPlaceIn);
        QCOMPARE(shaderEventOf(*after.ruleById(openRule.id)), kUnrelated);
    }
};

QTEST_GUILESS_MAIN(TestMigrationV6ToV7)
#include "test_migration_v6_to_v7.moc"
