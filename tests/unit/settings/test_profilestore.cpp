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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <algorithm>

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

#include "config/configmigration.h"
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
            return true;
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

        // Resolving G overlays defaults ← R ← C ← G. Clear the staged pointer
        // first: G is already staged-active and clean, and re-activating the
        // clean active profile is deliberately a no-op.
        m_staged.clear();
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

        // G is rebound to R, and its resolved config is unchanged. Activate
        // from cold — clean re-activation of the staged profile is a no-op.
        QCOMPARE(storedParent(grand), root);
        m_staged.clear();
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

    /// The committed active pointer round-trips through index.json, and every
    /// successful write announces itself via committedActiveIdChanged so the
    /// page controller's cached copy can never go stale (removeProfile clears
    /// the pointer store-side when the active profile is deleted).
    void activePointerRoundTrip()
    {
        m_current = baseDefaults();
        const QString id = m_store->createProfile(QStringLiteral("P"), QString(), QString());
        QVERIFY(m_store->committedActiveId().isEmpty());

        QSignalSpy committedSpy(m_store, &ProfileStore::committedActiveIdChanged);
        m_store->writeActiveId(id);
        QCOMPARE(m_store->committedActiveId(), id);
        QCOMPARE(committedSpy.count(), 1);
        QCOMPARE(committedSpy.last().first().toString(), id);

        m_store->writeActiveId(QString());
        QVERIFY(m_store->committedActiveId().isEmpty());
        QCOMPARE(committedSpy.count(), 2);
        QVERIFY(committedSpy.last().first().toString().isEmpty());

        // Deleting the committed-active profile clears the pointer through the
        // same funnel, so the change is announced too.
        m_store->writeActiveId(id);
        QCOMPARE(committedSpy.count(), 3);
        QVERIFY(m_store->removeProfile(id));
        QCOMPARE(committedSpy.count(), 4);
        QVERIFY(committedSpy.last().first().toString().isEmpty());
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
        m_staged.clear(); // activate from cold — clean re-activation is a no-op
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

        // G's resolved rule set is unchanged after the rebind. Activate from
        // cold — clean re-activation of the staged profile is a no-op.
        m_staged.clear();
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
        // Fresh profile matches current settings → the row reads clean. The
        // modified flag is asserted through availableProfiles() because that
        // row is the ONLY production consumer of the state.
        auto activeRowModified = [this]() {
            const QVariantList rows = m_store->availableProfiles();
            QVariantMap active;
            for (const QVariant& row : rows) {
                if (row.toMap().value(QStringLiteral("active")).toBool()) {
                    active = row.toMap();
                }
            }
            return active.value(QStringLiteral("modified")).toBool();
        };
        QVERIFY(!activeRowModified());

        // Edit the live config away from the profile.
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 5}, {QStringLiteral("k2"), QStringLiteral("x")}};
        QVERIFY(activeRowModified());

        // Capture the current settings into the profile → clean again.
        QVERIFY(m_store->updateProfileFromCurrent(id));
        QVERIFY(!activeRowModified());
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
        QCOMPARE(rootRow.value(QStringLiteral("segments")).toStringList(),
                 (QStringList{QStringLiteral("Group a"), QStringLiteral("K1")}));
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

    /// A structured setting enumerates into one row per CHANGED leaf, the way a
    /// scalar setting does, rather than collapsing into one opaque blob row.
    /// The unchanged sibling leaf must contribute nothing, and an array element
    /// must be named by the identifying field it carries.
    void configDiffEnumeratesStructuredValues()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] = QJsonObject{
            // Both scalars keep their defaults, so neither may produce a row.
            {QStringLiteral("k1"), 1},
            {QStringLiteral("k2"), QStringLiteral("x")},
            {QStringLiteral("tree"),
             QJsonObject{{QStringLiteral("overrides"),
                          QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("window.move")},
                                                 {QStringLiteral("duration"), 250}}}}}},
        };
        const QString id = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        const QVariantList rows = m_store->configChanges(id);
        QCOMPARE(rows.size(), 1);
        const QVariantMap row = rows.first().toMap();
        // Each level is its own segment (so the view can nest them), humanized,
        // with the array element named by its `path` field, not its position.
        QCOMPARE(row.value(QStringLiteral("segments")).toStringList(),
                 (QStringList{QStringLiteral("Group a"), QStringLiteral("Tree"), QStringLiteral("Overrides"),
                              QStringLiteral("window.move"), QStringLiteral("Duration")}));
        QCOMPARE(row.value(QStringLiteral("after")).toInt(), 250);
    }

    /// A trigger stays whole: its modifier and mouse-button halves only mean
    /// something together, so the row carries the object for QML to name.
    void configDiffKeepsTriggersWhole()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] = QJsonObject{
            {QStringLiteral("k1"), 1},
            {QStringLiteral("k2"), QStringLiteral("x")},
            {QStringLiteral("trigger"),
             QJsonObject{{QStringLiteral("modifier"), 134217728}, {QStringLiteral("mouseButton"), 2}}},
        };
        const QString id = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        const QVariantList rows = m_store->configChanges(id);
        QCOMPARE(rows.size(), 1);
        const QVariantMap row = rows.first().toMap();
        QCOMPARE(row.value(QStringLiteral("segments")).toStringList(),
                 (QStringList{QStringLiteral("Group a"), QStringLiteral("Trigger")}));
        const QVariantMap after = row.value(QStringLiteral("after")).toMap();
        QCOMPARE(after.value(QStringLiteral("modifier")).toInt(), 134217728);
        QCOMPARE(after.value(QStringLiteral("mouseButton")).toInt(), 2);
    }

    /// setParent refuses to reparent a profile under its own descendant. The
    /// resolve walk and depth ordering carry cycle guards, but this is the door
    /// they guard — a cycle must never be constructible through the API.
    void reparentUnderDescendantRefused()
    {
        m_current = baseDefaults();
        const QString a = m_store->createProfile(QStringLiteral("A"), QString(), QString());
        const QString b = m_store->createProfile(QStringLiteral("B"), QString(), a);
        const QString c = m_store->createProfile(QStringLiteral("C"), QString(), b);

        // A under its grandchild, and under itself.
        QVERIFY(!m_store->setParent(a, c));
        QVERIFY(!m_store->setParent(a, a));
        // The tree is unchanged: A is still the root of the chain.
        QCOMPARE(storedParent(a), QString());
        QCOMPARE(storedParent(c), QUuid(b).toString());
    }

    /// A pathologically nested structured value stops recursing at the depth
    /// cap and lands as one row carrying the remaining subtree, instead of
    /// walking a hand-crafted file forever.
    void configDiffStopsAtDepthCap()
    {
        m_current = baseDefaults();
        // 9 levels of nesting, well past the cap of 6.
        QJsonObject leaf{{QStringLiteral("v"), 1}};
        QJsonValue nested = leaf;
        for (int i = 0; i < 9; ++i) {
            nested = QJsonObject{{QStringLiteral("n"), nested}};
        }
        QJsonObject groupA = baseDefaults().value(QStringLiteral("GroupA")).toObject();
        groupA.insert(QStringLiteral("deep"), nested);
        m_current[QStringLiteral("GroupA")] = groupA;

        const QString id = m_store->createProfile(QStringLiteral("Deep"), QString(), QString());
        const QVariantList rows = m_store->configChanges(id);
        QCOMPARE(rows.size(), 1);
        // The capped leaf carries the residual OBJECT, not a scalar — proof the
        // walk stopped rather than descending to the buried "v".
        QVERIFY(rows.first().toMap().value(QStringLiteral("after")).toMap().size() > 0);
    }

    /// Pin the profile envelope version to the config schema version.
    ///
    /// readProfileFile refuses any file stamped with a different version, and
    /// loadAll drops it — so a ConfigSchemaVersion bump makes every saved
    /// profile vanish unless the bump ships a profile-envelope migration
    /// alongside the config one. This pin turns that silent data loss into a
    /// loud failure at exactly the moment someone bumps the version: extend
    /// the migration chain to cover profile files, then update this pin.
    void profileFormatTracksConfigSchemaVersion()
    {
        QCOMPARE(ConfigSchemaVersion, 5);
    }

    /// When the settings store refuses the staged blob, activation aborts
    /// whole: no rules staged, no active pointer flipped.
    void refusedConfigApplyAbortsActivation()
    {
        m_current = baseDefaults();
        const Rule r1 = makeRule(QStringLiteral("a"), QStringLiteral("a.desktop"));
        m_currentRules = {r1};
        const QString id = m_store->createProfile(QStringLiteral("R"), QString(), QString());
        // createProfile stages the new profile as active; clear for the test.
        m_staged.clear();
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
        config.applyConfig = [](const QJsonObject&) {
            return false; // the store refuses (schema version mismatch)
        };
        config.stagedActiveId = [this]() {
            return m_staged;
        };
        config.setStagedActiveId = [this](const QString& v) {
            m_staged = v;
        };
        config.currentUserRules = [this]() {
            return m_currentRules;
        };
        config.applyUserRules = [this](const QList<Rule>& rules) {
            m_lastAppliedRules = rules;
        };
        config.formatVersion = 5;
        ProfileStore refusingStore(std::move(config));

        QVERIFY(!refusingStore.activateProfile(id));
        QVERIFY(m_staged.isEmpty());
        QVERIFY(m_lastAppliedRules.isEmpty());
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

    /// Deleting the profile that is BOTH staged and committed active clears
    /// both pointers: the staged one drives the UI, and a dangling committed
    /// one in index.json would be re-adopted as staged on next launch.
    void deleteActiveProfileClearsPointers()
    {
        m_current = baseDefaults();
        const QString id = m_store->createProfile(QStringLiteral("Active"), QString(), QString());
        QCOMPARE(m_staged, id); // createProfile stages the new profile
        QVERIFY(m_store->writeActiveId(id)); // and Save commits it
        QCOMPARE(m_store->committedActiveId(), id);

        QVERIFY(m_store->removeProfile(id));
        QVERIFY(m_staged.isEmpty());
        QVERIFY(m_store->committedActiveId().isEmpty());
    }

    /// An empty or whitespace-only name is refused: nothing is created and the
    /// failure surfaces as a toast.
    void emptyNameRefused()
    {
        m_current = baseDefaults();
        QSignalSpy toasts(m_store, &ProfileStore::toastRequested);
        QVERIFY(m_store->createProfile(QString(), QString(), QString()).isEmpty());
        QVERIFY(m_store->createProfile(QStringLiteral("   "), QString(), QString()).isEmpty());
        QCOMPARE(m_store->availableProfiles().size(), 0);
        QCOMPARE(toasts.count(), 2);
    }

    /// A colliding display name is uniquified with a numeric suffix rather than
    /// refused — profiles are keyed by uuid, so nothing is overwritten.
    void duplicateNameUniquified()
    {
        m_current = baseDefaults();
        const QString first = m_store->createProfile(QStringLiteral("Work"), QString(), QString());
        const QString second = m_store->createProfile(QStringLiteral("Work"), QString(), QString());
        QVERIFY(!second.isEmpty());
        QVERIFY(first != second);

        QStringList names;
        for (const QVariant& v : m_store->availableProfiles()) {
            names.append(v.toMap().value(QStringLiteral("name")).toString());
        }
        std::sort(names.begin(), names.end());
        QCOMPARE(names, (QStringList{QStringLiteral("Work"), QStringLiteral("Work (2)")}));
    }

    /// An unparseable profile file (or one whose root is not an object) is
    /// skipped on load and refused on import, instead of surfacing a phantom
    /// row or aborting the whole scan.
    void corruptFileSkipped()
    {
        const QString path = m_dir->path() + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QStringLiteral(".json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ not json at all ///");
        }
        QCOMPARE(m_store->availableProfiles().size(), 0);
        QVERIFY(m_store->importProfile(path).isEmpty());

        // A well-formed document with a non-object root is equally refused.
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("[1, 2, 3]");
        }
        QCOMPARE(m_store->availableProfiles().size(), 0);
        QVERIFY(m_store->importProfile(path).isEmpty());
    }

    /// Reverting a scalar override drops it from the delta entirely: the diff
    /// reads empty and the profile resolves back to the parent's value.
    void revertScalarConfigChange()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString id = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        const QVariantList rows = m_store->configChanges(id);
        QCOMPARE(rows.size(), 1);
        QVERIFY(m_store->revertConfigChange(id, rows.first().toMap()));

        QCOMPARE(m_store->configChanges(id).size(), 0);
        QVERIFY(storedConfig(id).isEmpty());
        // Resolved config falls back to the schema default.
        m_staged.clear();
        QVERIFY(m_store->activateProfile(id));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 1);
    }

    /// Reverting one leaf inside a structured value is surgical: the sibling
    /// leaf's override survives, and the reverted leaf reads as the parent's.
    void revertNestedLeafKeepsSiblings()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] = QJsonObject{
            {QStringLiteral("k1"), 1},
            {QStringLiteral("k2"), QStringLiteral("x")},
            {QStringLiteral("tree"),
             QJsonObject{{QStringLiteral("overrides"),
                          QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("window.move")},
                                                 {QStringLiteral("duration"), 250}},
                                     QJsonObject{{QStringLiteral("path"), QStringLiteral("desktop")},
                                                 {QStringLiteral("duration"), 400}}}}}},
        };
        const QString id = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        // Two leaves: one per override entry's duration.
        QVariantList rows = m_store->configChanges(id);
        QCOMPARE(rows.size(), 2);
        int moveRow = -1;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i)
                    .toMap()
                    .value(QStringLiteral("segments"))
                    .toStringList()
                    .contains(QStringLiteral("window.move"))) {
                moveRow = i;
            }
        }
        QVERIFY(moveRow >= 0);
        QVERIFY(m_store->revertConfigChange(id, rows.at(moveRow).toMap()));

        // The desktop override survives; the window.move entry is gone (its
        // parent value was "absent", so reverting removes the element).
        rows = m_store->configChanges(id);
        QCOMPARE(rows.size(), 1);
        QVERIFY(
            rows.first().toMap().value(QStringLiteral("segments")).toStringList().contains(QStringLiteral("desktop")));
        const QJsonArray overrides = storedConfig(id)
                                         .value(QStringLiteral("GroupA"))
                                         .toObject()
                                         .value(QStringLiteral("tree"))
                                         .toObject()
                                         .value(QStringLiteral("overrides"))
                                         .toArray();
        QCOMPARE(overrides.size(), 1);
        QCOMPARE(overrides.first().toObject().value(QStringLiteral("path")).toString(), QStringLiteral("desktop"));
    }

    /// Reverting an ADDED rule drops the upsert and its dead order slot; the
    /// diff reads empty again.
    void revertAddedRule()
    {
        const Rule r1 = makeRule(QStringLiteral("a"), QStringLiteral("a.desktop"));
        m_currentRules = {r1};
        const QString id = m_store->createProfile(QStringLiteral("R"), QString(), QString());
        QCOMPARE(m_store->ruleChanges(id).size(), 1);

        QVERIFY(m_store->revertRuleChange(id, r1.id.toString()));
        QCOMPARE(m_store->ruleChanges(id).size(), 0);
        QCOMPARE(storedRules(id).value(QStringLiteral("upserts")).toArray().size(), 0);
        QCOMPARE(storedRules(id).value(QStringLiteral("order")).toArray().size(), 0);
    }

    /// Reverting a REMOVED parent rule restores it: the child resolves the
    /// parent's rule again.
    void revertRemovedRule()
    {
        const Rule r1 = makeRule(QStringLiteral("a"), QStringLiteral("a.desktop"));
        m_currentRules = {r1};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        m_currentRules = {};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);
        QCOMPARE(m_store->ruleChanges(child).size(), 1);

        QVERIFY(m_store->revertRuleChange(child, r1.id.toString()));
        QCOMPARE(m_store->ruleChanges(child).size(), 0);
        m_staged.clear();
        QVERIFY(m_store->activateProfile(child));
        QCOMPARE(ruleIds(m_lastAppliedRules), (QList<QUuid>{r1.id}));
    }

    /// A rule with no name still gets a readable diff row: the wired
    /// ruleTitle closure's text when present, the generic fallback otherwise.
    void unnamedRuleGetsTitle()
    {
        const Rule unnamed = makeRule(QString(), QStringLiteral("a.desktop"));
        m_currentRules = {unnamed};
        const QString id = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        // Default store (no ruleTitle closure): generic fallback, never blank.
        QCOMPARE(m_store->ruleChanges(id).first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Unnamed rule"));

        // With the closure wired (the app wires RuleModel::titleFor), its
        // text wins.
        ProfileStore::Config config;
        config.profilesDir = [this]() {
            return m_dir->path();
        };
        config.defaultConfig = []() {
            return baseDefaults();
        };
        config.stagedActiveId = [this]() {
            return m_staged;
        };
        config.currentUserRules = [this]() {
            return m_currentRules;
        };
        config.ruleTitle = [](const Rule&) {
            return QStringLiteral("Window class: steam");
        };
        config.formatVersion = 5;
        ProfileStore titledStore(std::move(config));
        QCOMPARE(titledStore.ruleChanges(id).first().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Window class: steam"));
    }

    /// Reverting a CHANGED rule falls back to the parent's version, keeping the
    /// rule itself.
    void revertChangedRule()
    {
        const Rule r1 = makeRule(QStringLiteral("original"), QStringLiteral("a.desktop"));
        m_currentRules = {r1};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        Rule renamed = r1;
        renamed.name = QStringLiteral("renamed");
        m_currentRules = {renamed};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);
        QCOMPARE(m_store->ruleChanges(child).size(), 1);
        QCOMPARE(m_store->ruleChanges(child).first().toMap().value(QStringLiteral("change")).toString(),
                 QStringLiteral("changed"));

        QVERIFY(m_store->revertRuleChange(child, r1.id.toString()));
        QCOMPARE(m_store->ruleChanges(child).size(), 0);
        m_staged.clear();
        QVERIFY(m_store->activateProfile(child));
        QCOMPARE(m_lastAppliedRules.size(), 1);
        QCOMPARE(m_lastAppliedRules.first().name, QStringLiteral("original"));
    }

    /// renameProfile updates name and description in place, and a colliding
    /// name is uniquified the same way createProfile's is.
    void renameUpdatesRowAndUniquifies()
    {
        m_current = baseDefaults();
        m_store->createProfile(QStringLiteral("Work"), QString(), QString());
        const QString id = m_store->createProfile(QStringLiteral("Play"), QString(), QString());

        QVERIFY(m_store->renameProfile(id, QStringLiteral("Gaming"), QStringLiteral("evening machine")));
        QVariantMap renamed;
        for (const QVariant& v : m_store->availableProfiles()) {
            if (v.toMap().value(QStringLiteral("id")).toString() == id) {
                renamed = v.toMap();
            }
        }
        QCOMPARE(renamed.value(QStringLiteral("name")).toString(), QStringLiteral("Gaming"));
        QCOMPARE(renamed.value(QStringLiteral("description")).toString(), QStringLiteral("evening machine"));

        // Renaming onto an existing name is uniquified, not refused.
        QVERIFY(m_store->renameProfile(id, QStringLiteral("Work"), QString()));
        for (const QVariant& v : m_store->availableProfiles()) {
            if (v.toMap().value(QStringLiteral("id")).toString() == id) {
                renamed = v.toMap();
            }
        }
        QCOMPARE(renamed.value(QStringLiteral("name")).toString(), QStringLiteral("Work (2)"));

        // A rename to the current name and description is a successful no-op:
        // no file rewrite, no profilesChanged churn.
        QSignalSpy changes(m_store, &ProfileStore::profilesChanged);
        QVERIFY(m_store->renameProfile(id, QStringLiteral("Work (2)"), QString()));
        QCOMPARE(changes.count(), 0);
    }

    /// duplicateProfile mints a fresh id with a derived unique name, and the
    /// clone resolves to the same settings (identical row signature).
    void duplicateKeepsResolvedConfig()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 7}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString original = m_store->createProfile(QStringLiteral("Work"), QString(), QString());

        const QString clone = m_store->duplicateProfile(original);
        QVERIFY(!clone.isEmpty());
        QVERIFY(clone != original);

        QHash<QString, QVariantMap> rowById;
        for (const QVariant& v : m_store->availableProfiles()) {
            rowById.insert(v.toMap().value(QStringLiteral("id")).toString(), v.toMap());
        }
        QCOMPARE(rowById.size(), 2);
        QCOMPARE(rowById.value(clone).value(QStringLiteral("name")).toString(), QStringLiteral("Work (copy)"));
        // Same resolved cascade → same signature (the identicon the UI shows).
        QCOMPARE(rowById.value(clone).value(QStringLiteral("signature")).toString(),
                 rowById.value(original).value(QStringLiteral("signature")).toString());
    }

    /// Exporting a root profile and importing the file back yields a NEW
    /// profile (fresh id, uniquified name) that resolves to the same settings.
    void exportImportRoundTrip()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 7}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString original = m_store->createProfile(QStringLiteral("Work"), QString(), QString());

        const QString destPath = m_dir->path() + QStringLiteral("/exported-profile");
        QVERIFY(m_store->exportProfile(original, destPath));

        const QString imported = m_store->importProfile(destPath);
        QVERIFY(!imported.isEmpty());
        QVERIFY(imported != original);

        QHash<QString, QVariantMap> rowById;
        for (const QVariant& v : m_store->availableProfiles()) {
            rowById.insert(v.toMap().value(QStringLiteral("id")).toString(), v.toMap());
        }
        QCOMPARE(rowById.size(), 2);
        // Uniquified, never overwriting the original.
        QCOMPARE(rowById.value(imported).value(QStringLiteral("name")).toString(), QStringLiteral("Work (2)"));
        // Identical resolved settings → identical signature.
        QCOMPARE(rowById.value(imported).value(QStringLiteral("signature")).toString(),
                 rowById.value(original).value(QStringLiteral("signature")).toString());
    }

    /// A successful setParent re-flattens the delta so the profile's RESOLVED
    /// config (and rules) are unchanged under the new parent.
    void reparentKeepsResolvedConfig()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString parent = m_store->createProfile(QStringLiteral("P"), QString(), QString());

        // An unrelated root with its own overrides.
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 9}, {QStringLiteral("k2"), QStringLiteral("z")}};
        const QString moved = m_store->createProfile(QStringLiteral("M"), QString(), QString());

        QVERIFY(m_store->setParent(moved, parent));
        QCOMPARE(storedParent(moved), parent);

        // Resolved config is what it was before the reparent.
        m_staged.clear();
        QVERIFY(m_store->activateProfile(moved));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 9);
        QCOMPARE(groupAStr(m_lastApplied, QStringLiteral("k2")), QStringLiteral("z"));
    }

    /// A unicode display name survives the create → disk → reload round trip
    /// intact (files are keyed by uuid, so the name only ever lives as JSON).
    void unicodeNameRoundTrips()
    {
        m_current = baseDefaults();
        const QString name = QStringLiteral("Büro 日本語 🚀");
        const QString id = m_store->createProfile(name, QString(), QString());
        QVERIFY(!id.isEmpty());

        // Force a cold reload from disk through a second store over the same
        // directory, so the assertion covers the persisted form and not the
        // in-memory record cache.
        ProfileStore::Config config;
        config.profilesDir = [this]() {
            return m_dir->path();
        };
        config.defaultConfig = []() {
            return baseDefaults();
        };
        config.formatVersion = 5;
        ProfileStore reloaded(std::move(config));
        const QVariantList rows = reloaded.availableProfiles();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(), name);
    }
};

QTEST_MAIN(TestProfileStore)
#include "test_profilestore.moc"
