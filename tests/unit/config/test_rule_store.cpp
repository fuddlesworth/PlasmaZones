// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_store.cpp
 * @brief Unit tests for RuleStore — the daemon-side
 *        rules.json store.
 *
 * Covers: round-trip persistence, malformed-rule drop on load, the
 * canonicalize-on-save contract, the mutation API, and the rulesChanged
 * signal.
 */

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <PhosphorRules/RuleStore.h>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>

using namespace PhosphorRules;
namespace PWR = PhosphorRules;

class TestRuleStore : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString storePath() const
    {
        return m_dir.path() + QStringLiteral("/rules.json");
    }

    // A valid context rule that round-trips cleanly.
    PWR::Rule makeRule(const QString& screenId, int priorityHint = -1)
    {
        PWR::Rule rule =
            PWR::ContextRuleBridge::makeAssignmentRule(screenId, screenId, 0, QString(), QStringLiteral("snapping"),
                                                       QStringLiteral("{11111111-2222-3333-4444-555555555555}"),
                                                       QString(), PWR::ContextRuleBridge::kContextBandBase);
        if (priorityHint >= 0) {
            rule.priority = priorityHint;
        }
        return rule;
    }

    void writeRaw(const QString& json)
    {
        QFile f(storePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(json.toUtf8());
    }

private Q_SLOTS:

    void init()
    {
        // Fresh store file per test.
        QFile::remove(storePath());
    }

    // ─── Construction / empty load ────────────────────────────────────────

    void testMissingFile_startsEmpty()
    {
        RuleStore store(storePath());
        QCOMPARE(store.count(), 0);
        QVERIFY(store.ruleSet().isEmpty());
    }

    // ─── Round-trip ───────────────────────────────────────────────────────

    void testRoundTrip_preservesRules()
    {
        const PWR::Rule a = makeRule(QStringLiteral("DP-1"));
        const PWR::Rule b = makeRule(QStringLiteral("HDMI-1"));

        {
            RuleStore store(storePath());
            QVERIFY(store.addRule(a));
            QVERIFY(store.addRule(b));
            QCOMPARE(store.count(), 2);
        }

        // A fresh store reading the same file sees both rules intact.
        RuleStore reloaded(storePath());
        QCOMPARE(reloaded.count(), 2);
        QVERIFY(reloaded.contains(a.id));
        QVERIFY(reloaded.contains(b.id));

        const auto loadedA = reloaded.ruleSet().ruleById(a.id);
        QVERIFY(loadedA.has_value());
        QCOMPARE(loadedA->priority, a.priority);
        QCOMPARE(loadedA->match, a.match);
        QCOMPARE(loadedA->actions, a.actions);
    }

    void testSavedFile_isVersion4()
    {
        RuleStore store(storePath());
        QVERIFY(store.addRule(makeRule(QStringLiteral("DP-1"))));

        QFile f(storePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 4);
        QVERIFY(root.value(QStringLiteral("rules")).isArray());
    }

    // ─── Malformed-rule drop ──────────────────────────────────────────────

    void testMalformedRule_droppedOnLoad()
    {
        // One valid rule, one rule with an unregistered action type, one
        // entry that is not an object. The store must load with just the
        // valid rule.
        const PWR::Rule good = makeRule(QStringLiteral("DP-1"));
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 4);
        QJsonArray rules;
        rules.append(good.toJson());
        // Malformed: action type that is not registered.
        QJsonObject badRule;
        badRule.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        badRule.insert(QStringLiteral("enabled"), true);
        badRule.insert(QStringLiteral("priority"), 100);
        badRule.insert(QStringLiteral("match"), good.match.toJson());
        QJsonArray badActions;
        QJsonObject badAction;
        badAction.insert(QStringLiteral("type"), QStringLiteral("noSuchActionTypeXYZ"));
        badActions.append(badAction);
        badRule.insert(QStringLiteral("actions"), badActions);
        rules.append(badRule);
        // Non-object entry.
        rules.append(QStringLiteral("not-an-object"));
        root.insert(QStringLiteral("rules"), rules);
        writeRaw(QString::fromUtf8(QJsonDocument(root).toJson()));

        RuleStore store(storePath());
        // Only the good rule survives.
        QCOMPARE(store.count(), 1);
        QVERIFY(store.contains(good.id));
    }

    void testMalformedFile_startsEmpty()
    {
        writeRaw(QStringLiteral("{ this is not valid json"));
        RuleStore store(storePath());
        QCOMPARE(store.count(), 0);
    }

    void testWrongVersion_startsEmpty()
    {
        // RuleSet::fromJson refuses a non-v4 document — the store
        // treats that like a malformed file and starts empty.
        writeRaw(QStringLiteral("{\"_version\":3,\"rules\":[]}"));
        RuleStore store(storePath());
        QCOMPARE(store.count(), 0);
    }

    // ─── Canonicalize-on-save ─────────────────────────────────────────────

    void testCanonicalizeOnSave()
    {
        // A hand-written file with extra unknown keys and a non-canonical
        // shape must be rewritten to the canonical form on the next save.
        const PWR::Rule rule = makeRule(QStringLiteral("DP-1"));
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 4);
        root.insert(QStringLiteral("strayTopLevelKey"), QStringLiteral("noise"));
        QJsonArray rules;
        rules.append(rule.toJson());
        root.insert(QStringLiteral("rules"), rules);
        writeRaw(QString::fromUtf8(QJsonDocument(root).toJson()));

        RuleStore store(storePath());
        QCOMPARE(store.count(), 1);
        // Any mutation triggers a save → the file is rewritten canonically.
        QVERIFY(store.addRule(makeRule(QStringLiteral("HDMI-1"))));

        QFile f(storePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
        // The stray top-level key is gone; only canonical keys remain.
        QVERIFY(!saved.contains(QStringLiteral("strayTopLevelKey")));
        QCOMPARE(saved.keys(), (QStringList{QStringLiteral("_version"), QStringLiteral("rules")}));
    }

    // ─── Mutation API ─────────────────────────────────────────────────────

    void testAddRule_rejectsDuplicateId()
    {
        RuleStore store(storePath());
        const PWR::Rule rule = makeRule(QStringLiteral("DP-1"));
        QVERIFY(store.addRule(rule));
        // Same id again → rejected.
        QVERIFY(!store.addRule(rule));
        QCOMPARE(store.count(), 1);
    }

    void testUpdateRule()
    {
        RuleStore store(storePath());
        PWR::Rule rule = makeRule(QStringLiteral("DP-1"), 310);
        QVERIFY(store.addRule(rule));

        rule.priority = 999;
        QVERIFY(store.updateRule(rule));
        QCOMPARE(store.ruleSet().ruleById(rule.id)->priority, 999);

        // Unknown id → rejected.
        PWR::Rule unknown = makeRule(QStringLiteral("HDMI-1"));
        QVERIFY(!store.updateRule(unknown));
    }

    void testRemoveRule()
    {
        RuleStore store(storePath());
        const PWR::Rule rule = makeRule(QStringLiteral("DP-1"));
        QVERIFY(store.addRule(rule));
        QVERIFY(store.removeRule(rule.id));
        QCOMPARE(store.count(), 0);
        // Removing again → false.
        QVERIFY(!store.removeRule(rule.id));
    }

    void testSetRuleEnabled()
    {
        RuleStore store(storePath());
        PWR::Rule rule = makeRule(QStringLiteral("DP-1"));
        rule.enabled = true;
        QVERIFY(store.addRule(rule));

        QVERIFY(store.setRuleEnabled(rule.id, false));
        QCOMPARE(store.ruleSet().ruleById(rule.id)->enabled, false);

        // No-op (already false) → returns true, no change.
        QVERIFY(store.setRuleEnabled(rule.id, false));

        // Unknown id → false.
        QVERIFY(!store.setRuleEnabled(QUuid::createUuid(), true));
    }

    void testSetRulePriority()
    {
        RuleStore store(storePath());
        PWR::Rule rule = makeRule(QStringLiteral("DP-1"), 310);
        QVERIFY(store.addRule(rule));

        QVERIFY(store.setRulePriority(rule.id, 610));
        QCOMPARE(store.ruleSet().ruleById(rule.id)->priority, 610);

        QVERIFY(!store.setRulePriority(QUuid::createUuid(), 100));
    }

    void testSetAllRules()
    {
        RuleStore store(storePath());
        QVERIFY(store.addRule(makeRule(QStringLiteral("DP-1"))));

        const QList<PWR::Rule> fresh{makeRule(QStringLiteral("HDMI-1")), makeRule(QStringLiteral("HDMI-2"))};
        // Wrap in QVERIFY so a setAllRules persistence regression surfaces
        // here instead of only later when the on-disk file diverges from
        // the in-memory state. Mirrors the sibling mutation tests.
        QVERIFY(store.setAllRules(fresh));
        QCOMPARE(store.count(), 2);
        QVERIFY(store.contains(fresh.at(0).id));
        QVERIFY(store.contains(fresh.at(1).id));
    }

    // ─── rulesChanged signal ──────────────────────────────────────────────

    void testRulesChangedSignal()
    {
        RuleStore store(storePath());
        QSignalSpy spy(&store, &RuleStore::rulesChanged);
        QVERIFY(spy.isValid());

        QVERIFY(store.addRule(makeRule(QStringLiteral("DP-1"))));
        QCOMPARE(spy.count(), 1);
        // Successful mutations emit rulesChanged(persisted=true). Catching the
        // bool here pins the Pass-1 signal-signature change so a future
        // regression that drops the argument doesn't silently break the
        // effect's persistence-aware refetch debounce.
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        const QUuid firstId = store.ruleSet().rules().first().id;
        QVERIFY(store.setRuleEnabled(firstId, false));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        // No-op enabled change does not emit.
        QVERIFY(store.setRuleEnabled(firstId, false));
        QCOMPARE(spy.count(), 0);

        QVERIFY(store.removeRule(firstId));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        // Cover the remaining persistence-changing entry points so a
        // regression that dropped rulesChanged from any of them doesn't
        // sneak past the spy. updateRule + setRulePriority + setAllRules
        // each emit a single rulesChanged(true) on success.
        QVERIFY(store.addRule(makeRule(QStringLiteral("DP-3"))));
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        PWR::Rule updated = store.ruleSet().rules().first();
        updated.name = QStringLiteral("renamed");
        QVERIFY(store.updateRule(updated));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        QVERIFY(store.setRulePriority(updated.id, updated.priority + 1));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        const QList<PWR::Rule> replacement{makeRule(QStringLiteral("DP-9"))};
        QVERIFY(store.setAllRules(replacement));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }
};

QTEST_MAIN(TestRuleStore)
#include "test_rule_store.moc"
