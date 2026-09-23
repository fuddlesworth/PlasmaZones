// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_adaptor.cpp
 * @brief Behaviour of RuleAdaptor, the org.plasmazones.Rules surface.
 *
 * The contract-sync test covers the XML's SHAPE only. What it cannot check is
 * whether the slots honour their DocStrings, and RuleAdaptor is the adaptor
 * where that gap costs the most: every refusal it makes is a refusal to
 * OVERWRITE the persisted rule set, so a guard that silently stops working
 * loses user data rather than a feature. This file pins:
 *
 *  1. A payload whose every rule is unusable is rejected outright, and the
 *     store keeps what it already had. This is the data-loss guard — without
 *     it, setAllRules would commit an empty list and return true.
 *  2. The same holds for a rule that PARSES but would not survive the commit
 *     filter (a registered action with an unusable param), which is the shape
 *     the two filters could diverge on.
 *  3. An oversize payload is refused at the wire boundary, before any parse.
 *  4. A _version that is present and disagrees with this build's schema is
 *     refused rather than laundered into a file stamped as current.
 *  5. All three id-taking slots reject a malformed UUID instead of acting on
 *     the null id.
 *  6. The happy path really does commit and read back, so the refusals above
 *     discriminate rather than describing a store that never accepts anything.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>

#include "dbus/ruleadaptor.h"

#include <memory>

using namespace PlasmaZones;
using PhosphorRules::Rule;
using PhosphorRules::RuleSet;
using PhosphorRules::RuleStore;

class TestRuleAdaptor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        // A fresh directory PER TEST FUNCTION, not one for the whole fixture:
        // RuleStore loads its file in the constructor, so a shared rules.json
        // would carry one test's committed set into the next one's baseline.
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_store = new RuleStore(m_dir->filePath(QStringLiteral("rules.json")));
        m_parent = new QObject(nullptr);
        m_adaptor = new RuleAdaptor(m_store, m_parent);
    }

    void cleanup()
    {
        m_adaptor->detach();
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_store;
        m_store = nullptr;
        m_dir.reset();
    }

    // The whole point of the adaptor: a valid payload lands, persists, and
    // reads back. Every refusal test below is only meaningful against this.
    void testSetAllRules_roundTrips()
    {
        QSignalSpy changedSpy(m_adaptor, &RuleAdaptor::rulesChanged);

        const Rule first = makeRule(QStringLiteral("konsole"));
        const Rule second = makeRule(QStringLiteral("dolphin"));
        QVERIFY(m_adaptor->setAllRules(payloadFor({first, second})));
        QCOMPARE(m_store->ruleSet().count(), 2);
        QCOMPARE(changedSpy.count(), 1);
        // persisted == true: the store wrote the file, not merely its memory.
        QCOMPARE(changedSpy.first().at(0).toBool(), true);

        const QJsonDocument doc = QJsonDocument::fromJson(m_adaptor->getAllRules().toUtf8());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value(QLatin1String("_version")).toInt(-1), RuleSet::SchemaVersion);
        const QJsonArray rules = doc.object().value(QLatin1String("rules")).toArray();
        QCOMPARE(rules.size(), 2);
        QCOMPARE(rules.at(0).toObject().value(QLatin1String("id")).toString(), first.id.toString());
        QCOMPARE(rules.at(1).toObject().value(QLatin1String("id")).toString(), second.id.toString());
    }

    // The data-loss guard. A non-empty payload whose rules are ALL unusable is
    // a rejected payload (whole-set schema skew), not a request to clear: the
    // adaptor must refuse it and leave the persisted set alone. Deleting the
    // guard makes this test fail with an emptied store and a true return.
    void testSetAllRules_refusesAllMalformedWithoutWipingStore()
    {
        seedTwoRules();

        // Malformed three different ways so the refusal does not hinge on one
        // loader branch: no id, a non-object `match`, and no actions at all.
        QJsonArray malformed;
        {
            QJsonObject noId = makeRule(QStringLiteral("konsole")).toJson();
            noId.remove(QLatin1String("id"));
            malformed.append(noId);

            QJsonObject badMatch = makeRule(QStringLiteral("dolphin")).toJson();
            badMatch[QLatin1String("match")] = QStringLiteral("not-an-object");
            malformed.append(badMatch);

            QJsonObject noActions = makeRule(QStringLiteral("kate")).toJson();
            noActions[QLatin1String("actions")] = QJsonArray();
            malformed.append(noActions);
        }

        QSignalSpy changedSpy(m_adaptor, &RuleAdaptor::rulesChanged);
        QVERIFY(!m_adaptor->setAllRules(payloadFor(malformed)));
        QCOMPARE(m_store->ruleSet().count(), 2);
        QCOMPARE(changedSpy.count(), 0);
    }

    // The divergence the guard has to be immune to: a rule the LOADER would
    // take but the COMMIT filter (RuleSet::setRules, which drops on isValid())
    // would not. Today the two agree, so this payload is dropped one step
    // earlier than the name suggests — the assertion is the same either way,
    // and it stops holding the moment they stop agreeing, which is exactly
    // when a guard counting the wrong population would wave a wipe through.
    void testSetAllRules_refusesValidateFailingRuleWithoutWipingStore()
    {
        seedTwoRules();

        // setEngineMode is registered, so the type resolves; its descriptor
        // requires a non-empty `mode`, so the action cannot be committed.
        Rule invalid = makeRule(QStringLiteral("konsole"));
        PhosphorRules::RuleAction action;
        action.type = QString(PhosphorRules::ActionType::SetEngineMode);
        action.params.insert(QString(PhosphorRules::ActionParam::Mode), QString());
        invalid.actions = {action};
        QVERIFY2(!invalid.isValid(), "fixture must be a rule the commit filter rejects, or this test proves nothing");

        QSignalSpy changedSpy(m_adaptor, &RuleAdaptor::rulesChanged);
        QVERIFY(!m_adaptor->setAllRules(payloadFor({invalid})));
        QCOMPARE(m_store->ruleSet().count(), 2);
        QCOMPARE(changedSpy.count(), 0);
    }

    // An EMPTY rules array is the genuine clear and must still work — the
    // guard above keys on "non-empty payload, nothing accepted", so this is
    // the arm that keeps it from swallowing a legitimate wipe.
    void testSetAllRules_emptyArrayClears()
    {
        seedTwoRules();

        QJsonObject payload;
        payload[QLatin1String("_version")] = RuleSet::SchemaVersion;
        payload[QLatin1String("rules")] = QJsonArray();
        QVERIFY(m_adaptor->setAllRules(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))));
        QCOMPARE(m_store->ruleSet().count(), 0);
    }

    // The wire-boundary cap: refused on size alone, before the parse that
    // would allocate a UTF-8 copy of the payload.
    void testSetAllRules_refusesOversizePayload()
    {
        seedTwoRules();

        // Structurally VALID JSON, so the only thing that can reject it is the
        // cap. The padding rides in a rule name, which the loader would happily
        // accept at any length.
        Rule fat = makeRule(QStringLiteral("konsole"));
        fat.name = QString(2 * 1024 * 1024, QLatin1Char('x'));
        const QString payload = payloadFor({fat});
        QVERIFY(payload.size() > 1 * 1024 * 1024);

        QVERIFY(!m_adaptor->setAllRules(payload));
        QCOMPARE(m_store->ruleSet().count(), 2);
    }

    // A payload stamped with a schema this build does not read must not be
    // re-persisted under the daemon's own version, which would launder a
    // cross-version downgrade into a file claiming to be current. An ABSENT
    // version stays tolerated: the bare shape is a documented caller spelling.
    void testSetAllRules_refusesMismatchedVersionButToleratesAbsent()
    {
        seedTwoRules();

        QJsonObject skewed;
        skewed[QLatin1String("_version")] = RuleSet::SchemaVersion + 1;
        skewed[QLatin1String("rules")] = QJsonArray{makeRule(QStringLiteral("kate")).toJson()};
        QVERIFY(!m_adaptor->setAllRules(QString::fromUtf8(QJsonDocument(skewed).toJson(QJsonDocument::Compact))));
        QCOMPARE(m_store->ruleSet().count(), 2);

        QJsonObject bare;
        bare[QLatin1String("rules")] = QJsonArray{makeRule(QStringLiteral("kate")).toJson()};
        QVERIFY(m_adaptor->setAllRules(QString::fromUtf8(QJsonDocument(bare).toJson(QJsonDocument::Compact))));
        QCOMPARE(m_store->ruleSet().count(), 1);
    }

    // Malformed JSON and a valid-but-non-object document leave through
    // different branches of the shared parse helper; neither may touch the
    // store.
    void testMutators_refuseMalformedJson()
    {
        seedTwoRules();

        QVERIFY(!m_adaptor->setAllRules(QStringLiteral("{not json")));
        QVERIFY(!m_adaptor->setAllRules(QStringLiteral("[1, 2, 3]")));
        QVERIFY(!m_adaptor->addRule(QStringLiteral("{not json")));
        QVERIFY(!m_adaptor->addRule(QStringLiteral("[1, 2, 3]")));
        QVERIFY(!m_adaptor->updateRule(QStringLiteral("{not json")));
        QVERIFY(!m_adaptor->updateRule(QStringLiteral("[1, 2, 3]")));

        QCOMPARE(m_store->ruleSet().count(), 2);
    }

    // The three id-taking slots parse their argument as a QUuid. A malformed
    // string yields the NULL uuid, which no rule carries — without the
    // explicit check each call would reach the store and merely fail to find
    // anything, which is the same answer for the wrong reason. Assert the
    // refusal against a store that DOES hold rules, so a slot acting on the
    // null id could not be mistaken for an empty-store miss.
    void testIdSlots_rejectMalformedUuid()
    {
        seedTwoRules();
        const QString bad = QStringLiteral("not-a-uuid");

        QVERIFY(!m_adaptor->removeRule(bad));
        QVERIFY(!m_adaptor->setRuleEnabled(bad, false));
        QVERIFY(!m_adaptor->setRulePriority(bad, 7));
        QVERIFY(!m_adaptor->removeRule(QString()));

        QCOMPARE(m_store->ruleSet().count(), 2);

        // Positive control: the same slots DO act on a well-formed id, so the
        // refusals above discriminate the uuid check rather than reporting a
        // store that rejects everything.
        const QUuid live = m_store->ruleSet().rules().at(0).id;
        QVERIFY(m_adaptor->setRulePriority(live.toString(), 7));
        QCOMPARE(m_store->ruleSet().ruleById(live)->priority, 7);
        QVERIFY(m_adaptor->removeRule(live.toString()));
        QCOMPARE(m_store->ruleSet().count(), 1);
    }

    // Shutdown contract shared with the sibling adaptors: a late D-Bus call
    // after detach() answers safely instead of dereferencing the store.
    void testDetached_slotsAnswerSafely()
    {
        seedTwoRules();
        m_adaptor->detach();

        QVERIFY(m_adaptor->getAllRules().isEmpty());
        QVERIFY(!m_adaptor->setAllRules(payloadFor({makeRule(QStringLiteral("kate"))})));
        QVERIFY(!m_adaptor->addRule(QString::fromUtf8(
            QJsonDocument(makeRule(QStringLiteral("kate")).toJson()).toJson(QJsonDocument::Compact))));
        QVERIFY(!m_adaptor->removeRule(QUuid::createUuid().toString()));
        m_adaptor->reloadRules(); // must not crash
        m_adaptor->resetManagedDefaults(); // must not crash

        // detach() also severs the relay, so a store that outlives the
        // adaptor's interest cannot keep pushing onto a dead wire.
        QSignalSpy changedSpy(m_adaptor, &RuleAdaptor::rulesChanged);
        m_store->setAllRules({makeRule(QStringLiteral("dolphin"))});
        QCOMPARE(changedSpy.count(), 0);
    }

private:
    /// A minimal rule the loader and the commit filter both accept: a leaf
    /// appId match plus the parameterless window-domain `exclude` action.
    static Rule makeRule(const QString& appId)
    {
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = appId;
        rule.match = PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::AppId,
                                                              PhosphorRules::Operator::Equals, appId);
        PhosphorRules::RuleAction action;
        action.type = QString(PhosphorRules::ActionType::Exclude);
        rule.actions = {action};
        return rule;
    }

    static QString payloadFor(const QList<Rule>& rules)
    {
        QJsonArray arr;
        for (const Rule& rule : rules) {
            arr.append(rule.toJson());
        }
        return payloadFor(arr);
    }

    static QString payloadFor(const QJsonArray& rules)
    {
        QJsonObject payload;
        payload[QLatin1String("_version")] = RuleSet::SchemaVersion;
        payload[QLatin1String("rules")] = rules;
        return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    /// Two committed rules, so every refusal below is asserted against a store
    /// that has something to lose.
    void seedTwoRules()
    {
        QVERIFY(m_adaptor->setAllRules(
            payloadFor({makeRule(QStringLiteral("konsole")), makeRule(QStringLiteral("dolphin"))})));
        QCOMPARE(m_store->ruleSet().count(), 2);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    RuleStore* m_store = nullptr;
    QObject* m_parent = nullptr;
    RuleAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestRuleAdaptor)
#include "test_rule_adaptor.moc"
