// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_profilestore.cpp
 * @brief Settings-profile persistence, inheritance resolution, and delta.
 *
 * ProfileStore is the store behind ProfilePageController::bridge. This test
 * drives it directly with stub closures (an in-memory "current config" and a
 * fixed "defaults" blob), so it exercises the delta/resolution engine without
 * a real PhosphorConfig::Store.
 *
 * Pinned behaviour:
 *   - A root profile stores only what differs from the defaults; activating it
 *     reproduces the captured config (diff → resolve round-trip).
 *   - A three-level chain stores each node's delta against its PARENT-resolved
 *     config, and resolving overlays the whole chain.
 *   - Deleting a middle node rebinds its children to its parent and re-flattens
 *     their deltas, leaving each child's resolved config unchanged.
 *   - A file stamped with a different schema version is refused (skipped on
 *     load, rejected on import).
 *   - The committed active pointer round-trips through index.json.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

#include "settings/profilestore.h"

using namespace PlasmaZones;
using namespace PhosphorRules;

namespace {

// A tiny fixed schema-defaults blob (stamped like Store::exportToJson).
QJsonObject baseDefaults()
{
    return QJsonObject{
        {QStringLiteral("GroupA"), QJsonObject{{QStringLiteral("k1"), 1}, {QStringLiteral("k2"), QStringLiteral("x")}}},
        {QStringLiteral("GroupB"), QJsonObject{{QStringLiteral("b"), false}}},
        {QStringLiteral("_version"), 5},
    };
}

int groupAInt(const QJsonObject& blob, const QString& key)
{
    return blob.value(QStringLiteral("GroupA")).toObject().value(key).toInt(-999);
}

// A minimal registry-valid user rule (a Float action, param-less) so it
// survives Rule::toJson → fromJson round-tripping through the profile file.
Rule makeRule(const QString& name, const QString& appId, int priority = 100)
{
    Rule r;
    r.id = QUuid::createUuid();
    r.name = name;
    r.priority = priority;
    r.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, appId);
    RuleAction floatAction;
    floatAction.type = QString(ActionType::Float);
    r.actions = {floatAction};
    return r;
}

QList<QUuid> ruleIds(const QList<Rule>& rules)
{
    QList<QUuid> ids;
    for (const Rule& r : rules) {
        ids.append(r.id);
    }
    return ids;
}

QString groupAStr(const QJsonObject& blob, const QString& key)
{
    return blob.value(QStringLiteral("GroupA")).toObject().value(key).toString();
}

} // namespace

class TestProfileStore : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir* m_dir = nullptr;
    ProfileStore* m_store = nullptr;
    QJsonObject m_current; // the stubbed "current live config"
    QJsonObject m_lastApplied; // captured from the applyConfig closure
    QString m_staged; // the stubbed staged active id
    QList<Rule> m_currentRules; // the stubbed live user rules
    QList<Rule> m_lastAppliedRules; // captured from the applyUserRules closure

    QUuid idOf(const QString& braced) const
    {
        return QUuid(braced);
    }

    // Read a profile file's stored rule-delta upserts / removedIds / order.
    QJsonObject storedRules(const QString& bracedId) const
    {
        const QString path =
            m_dir->path() + QLatin1Char('/') + QUuid(bracedId).toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("rules")).toObject();
    }

    // Read a profile file's stored config delta straight off disk.
    QJsonObject storedConfig(const QString& bracedId) const
    {
        const QString path =
            m_dir->path() + QLatin1Char('/') + QUuid(bracedId).toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("config")).toObject();
    }

    QString storedParent(const QString& bracedId) const
    {
        const QString path =
            m_dir->path() + QLatin1Char('/') + QUuid(bracedId).toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("parent")).toString();
    }

private Q_SLOTS:
    void init()
    {
        m_dir = new QTemporaryDir();
        QVERIFY(m_dir->isValid());
        m_current = baseDefaults();
        m_lastApplied = QJsonObject();
        m_staged.clear();
        m_currentRules.clear();
        m_lastAppliedRules.clear();

        ProfileStore::Config config;
        config.profilesDir = [this]() {
            return m_dir->path();
        };
        config.currentConfig = [this]() {
            return m_current;
        };
        config.defaultConfig = []() {
            return baseDefaults();
        };
        config.applyConfig = [this](const QJsonObject& blob) {
            m_lastApplied = blob;
        };
        config.stagedActiveId = [this]() {
            return m_staged;
        };
        config.setStagedActiveId = [this](const QString& id) {
            m_staged = id;
        };
        config.currentUserRules = [this]() {
            return m_currentRules;
        };
        config.applyUserRules = [this](const QList<Rule>& rules) {
            m_lastAppliedRules = rules;
        };
        config.formatVersion = 5;
        m_store = new ProfileStore(std::move(config));
    }

    void cleanup()
    {
        delete m_store;
        m_store = nullptr;
        delete m_dir;
        m_dir = nullptr;
    }

    /// A root profile stores only the delta vs defaults, and activating it
    /// reproduces the captured config.
    void rootDeltaRoundTrip()
    {
        // Current differs from defaults only in GroupA.k1 (1 → 2).
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};

        const QString id = m_store->createProfile(QStringLiteral("Root"), QString(), QString());
        QVERIFY(!id.isEmpty());

        // Only the changed key is stored.
        const QJsonObject delta = storedConfig(id);
        QCOMPARE(delta.value(QStringLiteral("GroupA")).toObject().value(QStringLiteral("k1")).toInt(), 2);
        QVERIFY(!delta.value(QStringLiteral("GroupA")).toObject().contains(QStringLiteral("k2")));
        QVERIFY(!delta.contains(QStringLiteral("GroupB")));

        // Move current away, then activate: the applied blob is the resolved profile.
        m_current = baseDefaults();
        QVERIFY(m_store->activateProfile(id));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 2);
        QCOMPARE(groupAStr(m_lastApplied, QStringLiteral("k2")), QStringLiteral("x"));
        // The resolved blob carries the version marker so it round-trips through the store.
        QCOMPARE(m_lastApplied.value(QStringLiteral("_version")).toInt(), 5);
    }

    /// Three-level chain: each node stores its delta against its parent, and
    /// resolving overlays the whole chain.
    void threeLevelChain()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 3}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString grand = m_store->createProfile(QStringLiteral("G"), QString(), child);

        // C differs from R only in k2; G differs from C only in k1.
        const QJsonObject cDelta = storedConfig(child);
        QCOMPARE(cDelta.value(QStringLiteral("GroupA")).toObject().value(QStringLiteral("k2")).toString(),
                 QStringLiteral("y"));
        QVERIFY(!cDelta.value(QStringLiteral("GroupA")).toObject().contains(QStringLiteral("k1")));

        const QJsonObject gDelta = storedConfig(grand);
        QCOMPARE(gDelta.value(QStringLiteral("GroupA")).toObject().value(QStringLiteral("k1")).toInt(), 3);
        QVERIFY(!gDelta.value(QStringLiteral("GroupA")).toObject().contains(QStringLiteral("k2")));

        // Resolving G overlays defaults ← R ← C ← G.
        QVERIFY(m_store->activateProfile(grand));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 3);
        QCOMPARE(groupAStr(m_lastApplied, QStringLiteral("k2")), QStringLiteral("y"));
    }

    /// Deleting a middle node rebinds its children to its parent and keeps their
    /// resolved config unchanged.
    void reparentOnDelete()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 3}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString grand = m_store->createProfile(QStringLiteral("G"), QString(), child);

        // Delete the middle node.
        QVERIFY(m_store->removeProfile(child));

        // G is rebound to R, and its resolved config is unchanged.
        QCOMPARE(storedParent(grand), root);
        QVERIFY(m_store->activateProfile(grand));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 3);
        QCOMPARE(groupAStr(m_lastApplied, QStringLiteral("k2")), QStringLiteral("y"));

        // Only R and G remain.
        QCOMPARE(m_store->availableProfiles().size(), 2);
    }

    /// A file stamped with a different schema version is skipped on load and
    /// refused on import.
    void versionMismatchRefused()
    {
        const QUuid id = QUuid::createUuid();
        const QString path =
            m_dir->path() + QLatin1Char('/') + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QJsonObject foreign{
            {QStringLiteral("_version"), 4},
            {QStringLiteral("id"), id.toString()},
            {QStringLiteral("name"), QStringLiteral("Old")},
            {QStringLiteral("parent"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("config"), QJsonObject{}},
        };
        QSaveFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(foreign).toJson());
        QVERIFY(f.commit());

        // Not surfaced by the list.
        QCOMPARE(m_store->availableProfiles().size(), 0);
        // Import of the same file is refused.
        QVERIFY(m_store->importProfile(path).isEmpty());
    }

    /// The committed active pointer round-trips through index.json.
    void activePointerRoundTrip()
    {
        m_current = baseDefaults();
        const QString id = m_store->createProfile(QStringLiteral("P"), QString(), QString());
        QVERIFY(m_store->committedActiveId().isEmpty());

        m_store->writeActiveId(id);
        QCOMPARE(m_store->committedActiveId(), id);

        m_store->writeActiveId(QString());
        QVERIFY(m_store->committedActiveId().isEmpty());
    }

    /// A root profile captures the current user rules; activating it resolves
    /// them back (same ids, in order).
    void rulesCaptureAndResolve()
    {
        const Rule r1 = makeRule(QStringLiteral("float-a"), QStringLiteral("a.desktop"));
        const Rule r2 = makeRule(QStringLiteral("float-b"), QStringLiteral("b.desktop"));
        m_currentRules = {r1, r2};

        const QString id = m_store->createProfile(QStringLiteral("Root"), QString(), QString());
        QVERIFY(!id.isEmpty());

        // Both rules were captured as upserts, in order.
        const QJsonObject rules = storedRules(id);
        QCOMPARE(rules.value(QStringLiteral("upserts")).toArray().size(), 2);
        QCOMPARE(rules.value(QStringLiteral("order")).toArray().size(), 2);

        // Move the live rules away, then activate: the applied rules are resolved.
        m_currentRules.clear();
        QVERIFY(m_store->activateProfile(id));
        QCOMPARE(ruleIds(m_lastAppliedRules), (QList<QUuid>{r1.id, r2.id}));
    }

    /// A child that differs from its parent only by a re-stamped priority stores
    /// NO rule delta (semantic equality ignores priority).
    void rulePriorityIgnored()
    {
        const Rule r1 = makeRule(QStringLiteral("float-a"), QStringLiteral("a.desktop"), 100);
        m_currentRules = {r1};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        // Same rule, only the priority differs (as renormalizePriorities would do).
        Rule r1Reprioritised = r1;
        r1Reprioritised.priority = 900;
        m_currentRules = {r1Reprioritised};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);

        // No upserts and no removals — the child inherits the rule unchanged.
        const QJsonObject rules = storedRules(child);
        QCOMPARE(rules.value(QStringLiteral("upserts")).toArray().size(), 0);
        QCOMPARE(rules.value(QStringLiteral("removedIds")).toArray().size(), 0);

        // Resolving the child still yields the rule.
        QVERIFY(m_store->activateProfile(child));
        QCOMPARE(ruleIds(m_lastAppliedRules), (QList<QUuid>{r1.id}));
    }

    /// Deleting a middle node keeps each child's resolved rule set unchanged.
    void ruleReparentOnDelete()
    {
        const Rule r1 = makeRule(QStringLiteral("a"), QStringLiteral("a.desktop"));
        m_currentRules = {r1};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        const Rule r2 = makeRule(QStringLiteral("b"), QStringLiteral("b.desktop"));
        m_currentRules = {r1, r2};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);

        const Rule r3 = makeRule(QStringLiteral("c"), QStringLiteral("c.desktop"));
        m_currentRules = {r1, r2, r3};
        const QString grand = m_store->createProfile(QStringLiteral("G"), QString(), child);

        QVERIFY(m_store->removeProfile(child));

        // G's resolved rule set is unchanged after the rebind.
        QVERIFY(m_store->activateProfile(grand));
        QCOMPARE(ruleIds(m_lastAppliedRules), (QList<QUuid>{r1.id, r2.id, r3.id}));
    }

    /// availableProfiles returns rows in depth-first (tree) order with a depth
    /// per row.
    void depthFirstOrdering()
    {
        m_current = baseDefaults();
        const QString rootId = m_store->createProfile(QStringLiteral("R"), QString(), QString());
        const QString c1 = m_store->createProfile(QStringLiteral("C1"), QString(), rootId);
        const QString c2 = m_store->createProfile(QStringLiteral("C2"), QString(), rootId);
        const QString g = m_store->createProfile(QStringLiteral("G"), QString(), c1);

        const QVariantList rows = m_store->availableProfiles();
        QStringList names;
        QList<int> depths;
        for (const QVariant& v : rows) {
            names.append(v.toMap().value(QStringLiteral("name")).toString());
            depths.append(v.toMap().value(QStringLiteral("depth")).toInt());
        }
        // Depth-first: R, then C1 and its subtree (G), then C2.
        QCOMPARE(names,
                 (QStringList{QStringLiteral("R"), QStringLiteral("C1"), QStringLiteral("G"), QStringLiteral("C2")}));
        QCOMPARE(depths, (QList<int>{0, 1, 2, 1}));
    }

    /// The active profile reports modified after a config edit and clean again
    /// after updateProfileFromCurrent.
    void modifiedStateAndUpdate()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString id = m_store->createProfile(QStringLiteral("Work"), QString(), QString());
        // Fresh profile matches current settings → not modified.
        QVERIFY(!m_store->activeProfileModified());

        // Edit the live config away from the profile.
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 5}, {QStringLiteral("k2"), QStringLiteral("x")}};
        QVERIFY(m_store->activeProfileModified());
        // The active row carries the modified flag.
        const QVariantList rows = m_store->availableProfiles();
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().toMap().value(QStringLiteral("active")).toBool());
        QVERIFY(rows.first().toMap().value(QStringLiteral("modified")).toBool());

        // Capture the current settings into the profile → clean again.
        QVERIFY(m_store->updateProfileFromCurrent(id));
        QVERIFY(!m_store->activeProfileModified());
        QCOMPARE(storedConfig(id).value(QStringLiteral("GroupA")).toObject().value(QStringLiteral("k1")).toInt(), 5);
    }

    /// configChanges reports each overridden key with the value it had in the
    /// parent (schema defaults at the root) and the value this profile sets.
    void configDiffAgainstParent()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString rootId = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        // Root profile: "before" is the schema default (1), not empty.
        const QVariantList rootRows = m_store->configChanges(rootId);
        QCOMPARE(rootRows.size(), 1);
        const QVariantMap rootRow = rootRows.first().toMap();
        QCOMPARE(rootRow.value(QStringLiteral("group")).toString(), QStringLiteral("Group a"));
        QCOMPARE(rootRow.value(QStringLiteral("key")).toString(), QStringLiteral("K1"));
        QCOMPARE(rootRow.value(QStringLiteral("before")).toInt(), 1);
        QCOMPARE(rootRow.value(QStringLiteral("after")).toInt(), 2);

        // Child profile: "before" is the PARENT's value, not the default.
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 5}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString childId = m_store->createProfile(QStringLiteral("C"), QString(), rootId);
        const QVariantList childRows = m_store->configChanges(childId);
        QCOMPARE(childRows.size(), 1);
        QCOMPARE(childRows.first().toMap().value(QStringLiteral("before")).toInt(), 2);
        QCOMPARE(childRows.first().toMap().value(QStringLiteral("after")).toInt(), 5);
    }

    /// ruleChanges classifies each rule difference against the parent.
    void ruleDiffAgainstParent()
    {
        const Rule r1 = makeRule(QStringLiteral("a"), QStringLiteral("a.desktop"));
        m_currentRules = {r1};
        const QString rootId = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        // Against the empty root baseline the rule is new.
        const QVariantList rootRows = m_store->ruleChanges(rootId);
        QCOMPARE(rootRows.size(), 1);
        QCOMPARE(rootRows.first().toMap().value(QStringLiteral("change")).toString(), QStringLiteral("added"));
        QCOMPARE(rootRows.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("a"));

        // The child drops r1 and adds r2.
        const Rule r2 = makeRule(QStringLiteral("b"), QStringLiteral("b.desktop"));
        m_currentRules = {r2};
        const QString childId = m_store->createProfile(QStringLiteral("C"), QString(), rootId);

        QHash<QString, QString> changeByName;
        for (const QVariant& v : m_store->ruleChanges(childId)) {
            changeByName.insert(v.toMap().value(QStringLiteral("name")).toString(),
                                v.toMap().value(QStringLiteral("change")).toString());
        }
        QCOMPARE(changeByName.size(), 2);
        QCOMPARE(changeByName.value(QStringLiteral("b")), QStringLiteral("added"));
        // Named as the PARENT knows it — the child no longer carries the rule.
        QCOMPARE(changeByName.value(QStringLiteral("a")), QStringLiteral("removed"));
    }

    /// The row signature is derived from the RESOLVED cascade: a child that
    /// overrides nothing hashes identically to its parent, and diverges as soon
    /// as it overrides something.
    void signatureFollowsCascade()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        m_store->createProfile(QStringLiteral("R"), QString(), QString());
        const QString rootId = m_staged; // createProfile stages the new profile

        // A child capturing the same live settings overrides nothing.
        m_store->createProfile(QStringLiteral("Same"), QString(), rootId);

        // A child that changes a value does override something.
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 9}, {QStringLiteral("k2"), QStringLiteral("x")}};
        m_store->createProfile(QStringLiteral("Diff"), QString(), rootId);

        QHash<QString, QString> signatureByName;
        for (const QVariant& v : m_store->availableProfiles()) {
            const QVariantMap row = v.toMap();
            signatureByName.insert(row.value(QStringLiteral("name")).toString(),
                                   row.value(QStringLiteral("signature")).toString());
        }

        QVERIFY(!signatureByName.value(QStringLiteral("R")).isEmpty());
        // Same resolved settings → same mark.
        QCOMPARE(signatureByName.value(QStringLiteral("Same")), signatureByName.value(QStringLiteral("R")));
        // Diverged cascade → different mark.
        QVERIFY(signatureByName.value(QStringLiteral("Diff")) != signatureByName.value(QStringLiteral("R")));
    }
};

QTEST_MAIN(TestProfileStore)
#include "test_profilestore.moc"
