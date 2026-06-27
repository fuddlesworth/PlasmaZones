// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorRules;
using namespace PhosphorRules::TestHelpers;

class TestRuleSet : public QObject
{
    Q_OBJECT

private:
    static Rule simpleRule(const QString& name)
    {
        return makeRule(name, 100, MatchExpression{}, {floatAction()});
    }

private Q_SLOTS:

    // ── Revision counter ──

    void testRevisionStartsAtZero()
    {
        RuleSet set;
        QCOMPARE(set.revision(), quint64(0));
    }

    void testRevisionBumpsOnMutation()
    {
        RuleSet set;
        const Rule r = simpleRule(QStringLiteral("a"));
        QVERIFY(set.addRule(r));
        QCOMPARE(set.revision(), quint64(1));

        Rule updated = r;
        updated.name = QStringLiteral("a-renamed");
        QVERIFY(set.updateRule(updated));
        QCOMPARE(set.revision(), quint64(2));

        QVERIFY(set.removeRule(r.id));
        QCOMPARE(set.revision(), quint64(3));

        // The set is already empty after the removeRule above — clear() is a
        // no-op and must NOT bump the revision (a bump would needlessly
        // invalidate the RuleEvaluator's match cache).
        set.clear();
        QCOMPARE(set.revision(), quint64(3));

        // A clear() that actually drops rules does bump.
        QVERIFY(set.addRule(simpleRule(QStringLiteral("b"))));
        QCOMPARE(set.revision(), quint64(4));
        set.clear();
        QCOMPARE(set.revision(), quint64(5));
    }

    void testRevisionMonotonicEvenOnFailedAdd()
    {
        RuleSet set;
        const Rule r = simpleRule(QStringLiteral("a"));
        QVERIFY(set.addRule(r));
        const quint64 rev = set.revision();
        // Re-adding the same id fails — no revision bump.
        QVERIFY(!set.addRule(r));
        QCOMPARE(set.revision(), rev);
    }

    /// setRules() is intentionally pessimistic: a setRules(currentList) call
    /// that produces a structurally identical post-validation list still
    /// bumps the revision. The cache-invalidation pessimization is the
    /// header-documented contract — the round-trip
    /// `addRule(X)` → `setRules(originalList)` would otherwise let a stale
    /// downstream cache survive a real edit. Pinning the bump here protects
    /// the contract from a future "optimization" that skips the bump on
    /// equality.
    void testSetRulesAlwaysBumpsEvenOnIdenticalList()
    {
        RuleSet set;
        const Rule r = simpleRule(QStringLiteral("a"));
        QVERIFY(set.addRule(r));
        const quint64 rev = set.revision();

        // setRules with the exact same list still bumps.
        const QList<Rule> snapshot = set.rules();
        const int accepted = set.setRules(snapshot);
        QCOMPARE(accepted, 1);
        QVERIFY2(set.revision() > rev, "setRules must bump even when the post-validation list is identical");
    }

    // ── CRUD ──

    void testAddAndQuery()
    {
        RuleSet set;
        const Rule r = simpleRule(QStringLiteral("a"));
        QVERIFY(set.addRule(r));
        QCOMPARE(set.count(), 1);
        const auto found = set.ruleById(r.id);
        QVERIFY(found.has_value());
        QCOMPARE(found->name, QStringLiteral("a"));
    }

    void testUpdateNonexistentFails()
    {
        RuleSet set;
        QVERIFY(!set.updateRule(simpleRule(QStringLiteral("ghost"))));
    }

    void testRemoveNonexistentFails()
    {
        RuleSet set;
        QVERIFY(!set.removeRule(QUuid::createUuid()));
    }

    void testSetRulesDropsInvalid()
    {
        RuleSet set;
        Rule bad = simpleRule(QStringLiteral("bad"));
        bad.id = QUuid(); // null id -> invalid
        const int accepted = set.setRules({simpleRule(QStringLiteral("ok")), bad});
        QCOMPARE(accepted, 1);
        QCOMPARE(set.count(), 1);
    }

    // ── JSON ──

    void testJson_versionStamped()
    {
        RuleSet set;
        set.addRule(simpleRule(QStringLiteral("a")));
        const QJsonObject o = set.toJson();
        QCOMPARE(o.value(QStringLiteral("_version")).toInt(), RuleSet::SchemaVersion);
    }

    void testJson_roundTrip()
    {
        RuleSet set;
        set.addRule(makeRule(QStringLiteral("a"), 300, MatchExpression{}, {engineMode(QStringLiteral("autotile"))}));
        set.addRule(makeRule(QStringLiteral("b"), 100,
                             MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("term")),
                             {floatAction()}));

        const auto reloaded = RuleSet::fromJson(set.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, set);
        // A freshly loaded set is at revision 0.
        QCOMPARE(reloaded->revision(), quint64(0));
    }

    void testJson_refusesNonV4()
    {
        QJsonObject o;
        o.insert(QStringLiteral("_version"), 3);
        o.insert(QStringLiteral("rules"), QJsonArray{});
        QVERIFY(!RuleSet::fromJson(o).has_value());

        o.insert(QStringLiteral("_version"), 5);
        QVERIFY(!RuleSet::fromJson(o).has_value());
    }

    void testJson_refusesMissingVersion()
    {
        QJsonObject o;
        o.insert(QStringLiteral("rules"), QJsonArray{});
        QVERIFY(!RuleSet::fromJson(o).has_value());
    }

    void testJson_dropsMalformedRuleButLoadsSet()
    {
        QJsonObject o;
        o.insert(QStringLiteral("_version"), 4);
        QJsonArray rules;
        rules.append(simpleRule(QStringLiteral("good")).toJson());
        rules.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("not-a-uuid")}}); // malformed
        o.insert(QStringLiteral("rules"), rules);

        const auto reloaded = RuleSet::fromJson(o);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->count(), 1);
    }

    void testJson_dropsDuplicateIds()
    {
        const Rule r = simpleRule(QStringLiteral("dup"));
        QJsonObject o;
        o.insert(QStringLiteral("_version"), 4);
        QJsonArray rules;
        rules.append(r.toJson());
        rules.append(r.toJson()); // same id twice
        o.insert(QStringLiteral("rules"), rules);

        const auto reloaded = RuleSet::fromJson(o);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->count(), 1);
    }

    // ── File I/O ──

    void testFileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("rules.json"));

        RuleSet set;
        set.addRule(makeRule(QStringLiteral("a"), 200, MatchExpression{}, {floatAction()}));
        QVERIFY(set.saveToFile(path));

        const auto loaded = RuleSet::loadFromFile(path);
        QVERIFY(loaded.has_value());
        QCOMPARE(*loaded, set);
    }

    void testLoadMissingFileFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("does-not-exist.json"));
        QVERIFY(!RuleSet::loadFromFile(path).has_value());
    }

    void testLoadMalformedJsonFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bad.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not json");
        f.close();
        QVERIFY(!RuleSet::loadFromFile(path).has_value());
    }

    void testSaveCanonicalizes()
    {
        // Save -> load -> save must produce byte-identical files (no schema
        // drift in the canonical form).
        QTemporaryDir dir;
        const QString pathA = dir.filePath(QStringLiteral("a.json"));
        const QString pathB = dir.filePath(QStringLiteral("b.json"));

        RuleSet set;
        set.addRule(makeRule(QStringLiteral("x"), 50, MatchExpression{}, {floatAction()}));
        QVERIFY(set.saveToFile(pathA));

        const auto loaded = RuleSet::loadFromFile(pathA);
        QVERIFY(loaded.has_value());
        QVERIFY(loaded->saveToFile(pathB));

        QFile fa(pathA);
        QFile fb(pathB);
        QVERIFY(fa.open(QIODevice::ReadOnly));
        QVERIFY(fb.open(QIODevice::ReadOnly));
        QCOMPARE(fa.readAll(), fb.readAll());
    }

    void testJson_keepsRuleWithSemanticValidationIssue()
    {
        // Manually-edited config safety net: a context-domain action paired
        // with a window-property match (the silently-never-fires combination)
        // is kept on load, not silently dropped. The settings UI uses the
        // same Rule::validationIssues() walk to badge the offending
        // rule so the user can see why it never fires and fix it.
        const Rule bad =
            makeRule(QStringLiteral("hand-edited bad rule"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile"))});
        QVERIFY(bad.isValid()); // structurally valid — the issue is semantic
        QCOMPARE(bad.validationIssues().size(), 1);

        QJsonObject o;
        o.insert(QStringLiteral("_version"), 4);
        QJsonArray rules;
        rules.append(bad.toJson());
        rules.append(simpleRule(QStringLiteral("ok")).toJson());
        o.insert(QStringLiteral("rules"), rules);

        const auto reloaded = RuleSet::fromJson(o);
        QVERIFY(reloaded.has_value());
        // Both rules survive — the semantic check warns but does not drop.
        QCOMPARE(reloaded->count(), 2);
        const auto loadedBad = reloaded->ruleById(bad.id);
        QVERIFY(loadedBad.has_value());
        // The flagged combination is still observable on the loaded rule, so
        // a UI consumer can re-run the same check to badge it.
        const auto issues = loadedBad->validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::ContextActionWithWindowMatch);
    }
};

QTEST_GUILESS_MAIN(TestRuleSet)
#include "test_ruleset.moc"
