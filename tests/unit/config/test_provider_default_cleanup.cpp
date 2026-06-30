// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_provider_default_cleanup.cpp
 * @brief Unit tests for pruning the retired provider-default rule from rules.json.
 *
 * Rule precedence is pure `priority` — the synthesized provider-default
 * catch-all assignment rule is retired (the gated default resolver is the sole
 * global-default source). The cleanup needs no schema bump: it runs from
 * finalizeV4Conversion's idempotent cleanup branch
 * (pruneRetiredProviderDefaultRule). A current-version config.json fixture plus
 * a pre-existing rules.json (so finalizeV4Conversion takes its cleanup-only
 * branch, mirroring a real upgrade) is run through
 * ConfigMigration::ensureJsonConfig; the test asserts:
 *   - the provider-default catch-all rule is deleted from rules.json,
 *   - every other rule survives untouched (id, priority, actions),
 *   - a rules.json with no provider-default rule is left intact,
 *   - the cleanup is idempotent (running twice is a clean no-op),
 *   - config.json stays stamped at the current schema version.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUuid>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configmigration.h"
#include "../helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace PWR = PhosphorRules;
namespace CRB = PhosphorRules::ContextRuleBridge;

class TestProviderDefaultCleanup : public QObject
{
    Q_OBJECT

private:
    void writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(obj).toJson());
    }

    QJsonObject readJson(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    /// The deterministic id the retired `makeProviderDefaultRule` stamped — the
    /// "provider-default" family with an empty (screen, desktop, activity) tuple.
    /// pruneRetiredProviderDefaultRule removes exactly this id.
    QUuid providerDefaultId() const
    {
        return QUuid::createUuidV5(
            CRB::detail::namespaceUuid(),
            CRB::detail::contextIdentityKey(QLatin1StringView("provider-default"), QString(), 0, QString()));
    }

    /// Build a provider-default-shaped rule: an unmanaged catch-all assignment
    /// at the deterministic provider-default id. Built via the surviving
    /// factory + an id override, since makeProviderDefaultRule itself is gone.
    PWR::Rule makeProviderDefaultLike() const
    {
        PWR::Rule rule =
            CRB::makeAssignmentRule(QStringLiteral("Default"), QString(), 0, QString(), QStringLiteral("snapping"),
                                    QStringLiteral("{global-layout}"), QString(), 0);
        rule.id = providerDefaultId();
        return rule;
    }

    /// Persist @p rules to rules.json in the RuleSet on-disk format.
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

    /// A current-version config root — drives the chain into
    /// finalizeV4Conversion's cleanup-only branch (rules.json already exists).
    QJsonObject baseCurrentConfig() const
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), PlasmaZones::ConfigSchemaVersion);
        return root;
    }

private Q_SLOTS:

    // ─── Provider-default rule is removed, others survive ──────────────────

    void testProviderDefaultRemoved_othersSurvive()
    {
        IsolatedConfigGuard guard;

        // A real per-screen assignment rule that MUST survive the migration,
        // alongside the provider-default catch-all that must be removed.
        const PWR::Rule screenRule = CRB::makeAssignmentRule(
            QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"),
            QStringLiteral("{dp1-layout}"), QString(), CRB::kContextBandBase + 1);
        seedRules({makeProviderDefaultLike(), screenRule});

        writeJson(ConfigDefaults::configFilePath(), baseCurrentConfig());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const PWR::RuleSet after = loadRules();
        // The provider-default rule is gone.
        QVERIFY(!after.ruleById(providerDefaultId()).has_value());
        // The per-screen rule survives, priority and actions intact.
        const auto survivor = after.ruleById(screenRule.id);
        QVERIFY(survivor.has_value());
        QCOMPARE(survivor->priority, CRB::kContextBandBase + 1);
        QCOMPARE(survivor->actions.size(), screenRule.actions.size());
        QCOMPARE(after.rules().size(), 1);

        // Config stamped at the current schema version.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
    }

    // ─── No provider-default present → rules.json left intact ──────────────

    void testNoProviderDefault_rulesUntouched()
    {
        IsolatedConfigGuard guard;

        const PWR::Rule screenRule = CRB::makeAssignmentRule(QStringLiteral("DP-2"), QStringLiteral("DP-2"), 0,
                                                             QString(), QStringLiteral("autotile"), QString(),
                                                             QStringLiteral("bsp"), CRB::kContextBandBase + 2);
        seedRules({screenRule});

        writeJson(ConfigDefaults::configFilePath(), baseCurrentConfig());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const PWR::RuleSet after = loadRules();
        QCOMPARE(after.rules().size(), 1);
        const auto survivor = after.ruleById(screenRule.id);
        QVERIFY(survivor.has_value());
        QCOMPARE(survivor->priority, CRB::kContextBandBase + 2);
    }

    // ─── Idempotency — running twice is a clean no-op ──────────────────────

    void testIdempotency_runTwiceIsNoOp()
    {
        IsolatedConfigGuard guard;

        const PWR::Rule screenRule = CRB::makeAssignmentRule(
            QStringLiteral("DP-3"), QStringLiteral("DP-3"), 0, QString(), QStringLiteral("snapping"),
            QStringLiteral("{dp3-layout}"), QString(), CRB::kContextBandBase + 3);
        seedRules({makeProviderDefaultLike(), screenRule});
        writeJson(ConfigDefaults::configFilePath(), baseCurrentConfig());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QByteArray firstRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(!firstRun.isEmpty());

        // Reset the process-level migration guard so the second call re-runs the
        // full logic against the cleaned config — which must be a clean no-op.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QByteArray secondRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QCOMPARE(secondRun, firstRun);

        const PWR::RuleSet after = loadRules();
        QVERIFY(!after.ruleById(providerDefaultId()).has_value());
        QVERIFY(after.ruleById(screenRule.id).has_value());
        QCOMPARE(after.rules().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestProviderDefaultCleanup)
#include "test_provider_default_cleanup.moc"
