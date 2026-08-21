// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v3_to_v4_steam.cpp
 * @brief v3 → v4 migration tests for the premade Steam rule: the shape it is
 *        seeded with, the windows it does and does not match, and the
 *        `repairSeededSteamRule` fix-up that corrects a config converted
 *        before the rule was narrowed.
 *
 * Split out of test_migration_v3_to_v4.cpp; the shared config/rules JSON
 * helpers live in MigrationV3V4Fixture.h.
 */

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>

#include <optional>

#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/WindowQuery.h>

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "helpers/IsolatedConfigGuard.h"

#include "MigrationV3V4Fixture.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestMigrationV3ToV4Steam : public QObject, public MigrationV3V4Fixture
{
    Q_OBJECT

private:
    // Plain helpers, kept OUT of the Q_SLOTS section so moc does not register
    // them as test slots.

    /// The premade Steam rule's deterministic id, derived here from the SPEC
    /// rather than read back off the seed: UUIDv5 over the exclusion-migration
    /// namespace and the length-prefixed segment encoding ("<size>:<bytes>").
    /// Pinning it independently is the point — `repairSeededSteamRule` finds a
    /// stored rule by this id, so a change to the namespace or the segment
    /// string would silently orphan every existing user's rule while a test
    /// that re-derived the id from its own seed stayed green.
    static QUuid expectedSteamRuleId()
    {
        const QUuid kExclusionNamespace(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
        const QString segment = QStringLiteral("steam-default-exclude");
        return QUuid::createUuidV5(kExclusionNamespace, QString::number(segment.size()) + QLatin1Char(':') + segment);
    }

    /// The seeded premade-Steam rule, located by its deterministic id.
    static std::optional<PhosphorRules::Rule> seededSteamRule(const PhosphorRules::RuleSet& set)
    {
        return set.ruleById(expectedSteamRuleId());
    }

private Q_SLOTS:

    // ─── Premade Steam rule ───────────────────────────────────────────────
    // Every fresh install and every v3→v4 upgrade is seeded with the built-in
    // Steam fix: keep Steam's self-drawn `notificationtoasts_<N>_desktop`
    // top-levels out of placement. Everything else Steam opens — the library
    // window, Friends List, chat, Big Picture, Settings — and every
    // Steam-LAUNCHED GAME places like any other window.

    void testSteamDefaultRule_seeded()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertBareV3Config());

        const QJsonArray rules = rulesFromRules();
        QJsonObject steam;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("name")).toString() == QLatin1String("Steam notifications")) {
                steam = r;
            }
        }
        QVERIFY2(!steam.isEmpty(), "premade Steam rule must be seeded on a fresh/migrated v4 config");
        QVERIFY(steam.value(QStringLiteral("enabled")).toBool());

        // Golden id. The repair looks the stored rule up by exactly this
        // value, so it is part of the on-disk contract with every existing
        // user, not an implementation detail.
        QCOMPARE(steam.value(QStringLiteral("id")).toString(), expectedSteamRuleId().toString());

        // The match nests an Any{} group, so it is not a flat conjunction and
        // assignBandPrioritiesToZeroRules files it in the Advanced band
        // [500,600). The band follows the shape; the shape is chosen for
        // correctness (see the Any{} rationale in applySteamDefaultRuleShape),
        // not to land a particular number.
        const int steamPriority = steam.value(QStringLiteral("priority")).toInt();
        QVERIFY2(steamPriority >= 500 && steamPriority < 600, qPrintable(QString::number(steamPriority)));

        // ExcludePlacement, not the blanket Exclude: a toast has no business
        // being placed, but stripping its decorations and animations too was
        // never the point.
        QCOMPARE(actionTypes(steam), (QStringList{QStringLiteral("excludePlacement")}));

        // Match shape:
        //   All{ WindowClass endsWith "steam",
        //        Any{ Title contains "notificationtoasts",
        //             WindowClass contains "notificationtoasts" } }
        const QJsonObject match = steam.value(QStringLiteral("match")).toObject();
        QVERIFY(match.contains(QStringLiteral("all")));
        const QJsonArray all = match.value(QStringLiteral("all")).toArray();
        QCOMPARE(all.size(), 2);

        // endsWith, NOT contains. The field carries KWin's raw "resourceName
        // resourceClass" pair, and a Steam-launched game reports its own app
        // id in both halves, so `Contains "steam"` matched every game — see
        // steamRuleLeavesGamesAndOrdinaryWindowsAlone below, which pins that
        // regression against the real strings.
        QCOMPARE(matchLeafValueByOp(steam, QStringLiteral("windowClass"), QStringLiteral("endsWith")),
                 QStringLiteral("steam"));

        // The toast-name half is an Any{} over the same token on two fields,
        // because which field carries `notificationtoasts_<N>_desktop` is not
        // established (classically the WM_CLASS resourceName half; the retired
        // rule was written as though it were the caption). Pin both arms so a
        // future edit cannot quietly drop the one that turns out to be load-
        // bearing.
        QJsonObject anyGroup;
        for (const QJsonValue& v : all) {
            if (v.toObject().contains(QStringLiteral("any"))) {
                anyGroup = v.toObject();
            }
        }
        QVERIFY2(!anyGroup.isEmpty(), "the toast-name half must be an Any{} over title and class");
        const QJsonArray anyChildren = anyGroup.value(QStringLiteral("any")).toArray();
        QCOMPARE(anyChildren.size(), 2);
        QStringList anyFields;
        for (const QJsonValue& v : anyChildren) {
            const QJsonObject leaf = v.toObject();
            QCOMPARE(leaf.value(QStringLiteral("op")).toString(), QStringLiteral("contains"));
            QCOMPARE(leaf.value(QStringLiteral("value")).toString(), QStringLiteral("notificationtoasts"));
            anyFields.append(leaf.value(QStringLiteral("field")).toString());
        }
        anyFields.sort();
        QCOMPARE(anyFields, (QStringList{QStringLiteral("title"), QStringLiteral("windowClass")}));

        // No None{} guard any more: the rule names the windows it guards
        // rather than excluding everything that is not the library window.
        for (const QJsonValue& v : all) {
            QVERIFY2(!v.toObject().contains(QStringLiteral("none")),
                     "the retired title-negation guard must not come back");
        }

        // The rule is sliced into the placement-exclusion set the daemon and
        // effect consume — i.e. it actually participates in the gate. The
        // blanket-Exclude slice is empty: nothing seeded uses that action now.
        const auto set = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY(set.has_value());
        QCOMPARE(PhosphorRules::ExclusionRules::excludeRulesFrom(*set).count(), 0);
        QCOMPARE(PhosphorRules::ExclusionRules::excludePlacementRulesFrom(*set).count(), 1);
    }

    /// The regression the narrowing exists for, evaluated against the exact
    /// strings a live KWin session reports.
    ///
    /// `WindowQuery::windowClass` carries KWin's `windowClass()`, which is the
    /// raw `"resourceName resourceClass"` pair. Steam's own UI reports
    /// `"steamwebhelper steam"`, but a game launched THROUGH Steam reports its
    /// own app id in both halves — `"steam_app_2342813033
    /// steam_app_2342813033"` for World of Warcraft. The retired
    /// `Contains "steam"` leaf matched that, and the blanket `Exclude` action
    /// then left every Steam-launched game unmanaged and undecorated.
    void testSteamRuleLeavesGamesAndOrdinaryWindowsAlone()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertBareV3Config());

        const auto set = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY(set.has_value());
        const PhosphorRules::RuleSet placement = PhosphorRules::ExclusionRules::excludePlacementRulesFrom(*set);
        QCOMPARE(placement.count(), 1);
        const PhosphorRules::MatchExpression& match = placement.rules().first().match;

        const auto matches = [&match](const QString& windowClass, const QString& title) {
            PhosphorRules::WindowQuery q;
            q.windowClass = windowClass;
            q.title = title;
            return match.evaluate(q);
        };

        // The window the rule exists for.
        QVERIFY2(matches(QStringLiteral("steamwebhelper steam"), QStringLiteral("notificationtoasts_20993166_desktop")),
                 "a Steam notification toast must still be guarded");

        // The regression: a Steam-launched game must be left alone.
        QVERIFY2(
            !matches(QStringLiteral("steam_app_2342813033 steam_app_2342813033"), QStringLiteral("World of Warcraft")),
            "a Steam-launched game must never be excluded");

        // The row that actually falsifies endsWith-vs-contains. Every OTHER
        // negative here carries a toast-free name, so the Any{} half decides
        // them on its own and the class operator never has to hold — revert
        // this leaf to the retired `Contains "steam"` and they all stay green.
        // A game id both CONTAINS "steam" and carries the toast token, so only
        // the suffix anchor keeps it out.
        QVERIFY2(!matches(QStringLiteral("steam_app_2342813033 steam_app_2342813033"),
                          QStringLiteral("notificationtoasts_5_desktop")),
                 "endsWith must pin the class token: a steam_app_* id that also "
                 "carries the toast name must still be left alone");

        // Steam's other ordinary windows place like anything else.
        QVERIFY2(!matches(QStringLiteral("steamwebhelper steam"), QStringLiteral("Friends List")),
                 "the Friends List must place normally");

        // The toast token on the CLASS side is guarded too. Which field
        // carries `notificationtoasts_<N>_desktop` is not established, so the
        // rule accepts either; this pins the arm the title-only shape missed.
        //
        // The title is left DISENGAGED here, not set to an empty string —
        // that is the real late-caption state. The effect stamps the caption
        // only once it is non-empty and caches the exclusion verdict without a
        // captionChanged invalidation, so a title-only rule that resolves in
        // this state answers "not excluded" and stays pinned that way. The
        // class arm has no such window.
        PhosphorRules::WindowQuery noCaption;
        noCaption.windowClass = QStringLiteral("notificationtoasts_1_desktop steam");
        QVERIFY2(match.evaluate(noCaption),
                 "a toast whose class carries the token must be guarded before "
                 "any caption has been stamped");

        // The older X11 two-token spelling still resolves, so an upgrading
        // user on an older Steam keeps the guard.
        QVERIFY2(matches(QStringLiteral("steam Steam"), QStringLiteral("notificationtoasts_1_desktop")),
                 "the X11-era \"steam Steam\" class pair must still resolve");
    }

    /// An already-converted config carrying the RETIRED rule verbatim is
    /// repaired in place on the next startup, because the seeder only runs on
    /// the rebuild path and would never otherwise reach it.
    void testSteamRuleRepairedInAnAlreadyConvertedConfig()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertBareV3Config());

        // Put the retired shape back on disk under the seeded rule's fixed id,
        // exactly as a config converted before the narrowing carries it.
        const QString rulesPath = ConfigDefaults::rulesFilePath();
        auto setOpt = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(setOpt.has_value());
        PhosphorRules::RuleSet stale = *setOpt;
        const auto seeded = seededSteamRule(stale);
        QVERIFY(seeded.has_value());
        const QUuid steamId = seeded->id;
        PhosphorRules::Rule retired = *seeded;
        retired.name = QStringLiteral("Steam");
        // A real pre-narrowing config carries this rule in the ADVANCED band:
        // the retired shape nested a None{}, so it was never a flat
        // conjunction. Seeding the fixture at the current band would let a
        // regression that re-stamped the priority pass unnoticed.
        retired.priority = 517;
        retired.match = PhosphorRules::MatchExpression::makeAll(
            {PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::WindowClass,
                                                      PhosphorRules::Operator::Contains, QStringLiteral("steam")),
             PhosphorRules::MatchExpression::makeNone({PhosphorRules::MatchExpression::makeLeaf(
                 PhosphorRules::Field::Title, PhosphorRules::Operator::Equals, QStringLiteral("Steam"))})});
        retired.actions.clear();
        PhosphorRules::RuleAction blanket;
        blanket.type = QString(PhosphorRules::ActionType::Exclude);
        retired.actions.append(blanket);
        QVERIFY(stale.updateRule(retired));
        QVERIFY(stale.saveToFile(rulesPath));

        // Re-run the converted path.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const auto repairedSet = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(repairedSet.has_value());
        // Rewritten IN PLACE: the rule keeps its id, so a second startup finds
        // the corrected shape and does nothing, and the user's row does not
        // jump position in the Rules page.
        const auto repairedRule = repairedSet->ruleById(steamId);
        QVERIFY2(repairedRule.has_value(), "the repair must rewrite the rule, not replace it with a new id");
        QCOMPARE(PhosphorRules::ExclusionRules::excludeRulesFrom(*repairedSet).count(), 0);
        QCOMPARE(PhosphorRules::ExclusionRules::excludePlacementRulesFrom(*repairedSet).count(), 1);

        // The priority is carried across, not re-stamped. The fixture put the
        // rule in the Advanced band where a real pre-narrowing config has it.
        QCOMPARE(repairedRule->priority, 517);
        // The name is re-stamped, so the row says what it now guards.
        QCOMPARE(repairedRule->name, QStringLiteral("Steam notifications"));

        const PhosphorRules::MatchExpression& match =
            PhosphorRules::ExclusionRules::excludePlacementRulesFrom(*repairedSet).rules().first().match;

        // POSITIVE shape check. Without this the negative below is decided by
        // the title leaf alone, so a repair that stamped only half the match
        // would still pass.
        PhosphorRules::WindowQuery toast;
        toast.windowClass = QStringLiteral("steamwebhelper steam");
        toast.title = QStringLiteral("notificationtoasts_20993166_desktop");
        QVERIFY2(match.evaluate(toast), "the repaired rule must still guard a toast");

        PhosphorRules::WindowQuery game;
        game.windowClass = QStringLiteral("steam_app_2342813033 steam_app_2342813033");
        game.title = QStringLiteral("World of Warcraft");
        QVERIFY2(!match.evaluate(game), "the repaired rule must stop excluding Steam-launched games");

        // Idempotent. A third startup must find the corrected shape and do
        // nothing at all — asserted on the bytes, not in prose.
        QFile repaired(rulesPath);
        QVERIFY(repaired.open(QIODevice::ReadOnly));
        const QByteArray afterRepair = repaired.readAll();
        repaired.close();

        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        QFile again(rulesPath);
        QVERIFY(again.open(QIODevice::ReadOnly));
        const QByteArray afterSecondRun = again.readAll();
        again.close();
        QCOMPARE(afterSecondRun, afterRepair);
    }

    /// The repair carries the user's enabled flag and priority across. A user
    /// who turned the premade rule off meant it, and must not find it back on
    /// after an upgrade.
    void testSteamRuleRepairKeepsEnabledFlagAndPriority()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertBareV3Config());

        const QString rulesPath = ConfigDefaults::rulesFilePath();
        auto setOpt = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(setOpt.has_value());
        PhosphorRules::RuleSet stale = *setOpt;
        const auto seeded = seededSteamRule(stale);
        QVERIFY(seeded.has_value());
        const QUuid steamId = seeded->id;

        PhosphorRules::Rule retired = *seeded;
        retired.enabled = false;
        retired.priority = 543;
        retired.match = PhosphorRules::MatchExpression::makeAll(
            {PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::WindowClass,
                                                      PhosphorRules::Operator::Contains, QStringLiteral("steam")),
             PhosphorRules::MatchExpression::makeNone({PhosphorRules::MatchExpression::makeLeaf(
                 PhosphorRules::Field::Title, PhosphorRules::Operator::Equals, QStringLiteral("Steam"))})});
        retired.actions.clear();
        PhosphorRules::RuleAction blanket;
        blanket.type = QString(PhosphorRules::ActionType::Exclude);
        retired.actions.append(blanket);
        QVERIFY(stale.updateRule(retired));
        QVERIFY(stale.saveToFile(rulesPath));

        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const auto repairedSet = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(repairedSet.has_value());
        const auto repairedRule = repairedSet->ruleById(steamId);
        QVERIFY(repairedRule.has_value());

        // The shape WAS corrected...
        QCOMPARE(repairedRule->actions.size(), 1);
        QCOMPARE(repairedRule->actions.first().type, QString(PhosphorRules::ActionType::ExcludePlacement));
        // ...but the two things the user owns were left alone.
        QVERIFY2(!repairedRule->enabled, "a rule the user disabled must stay disabled");
        QCOMPARE(repairedRule->priority, 543);
    }

    /// A rule the user DELETED must not be resurrected. The repair matches by
    /// a fixed id, so a missing rule is nothing of ours to repair.
    void testSteamRuleRepairDoesNotResurrectADeletedRule()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertBareV3Config());

        const QString rulesPath = ConfigDefaults::rulesFilePath();
        auto setOpt = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(setOpt.has_value());
        PhosphorRules::RuleSet trimmed = *setOpt;
        const auto seeded = seededSteamRule(trimmed);
        QVERIFY(seeded.has_value());
        const QUuid steamId = seeded->id;
        const int countBefore = trimmed.count();
        QVERIFY(trimmed.removeRule(steamId));
        QVERIFY(trimmed.saveToFile(rulesPath));

        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const auto afterSet = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(afterSet.has_value());
        QVERIFY2(!afterSet->ruleById(steamId).has_value(), "a deleted premade rule must not come back");
        QCOMPARE(afterSet->count(), countBefore - 1);
    }

    /// A user who EDITED the seeded rule keeps their edit. The repair only
    /// reclaims rules that still carry the retired shape verbatim.
    void testSteamRuleRepairLeavesAUserEditAlone()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertBareV3Config());

        const QString rulesPath = ConfigDefaults::rulesFilePath();
        auto setOpt = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(setOpt.has_value());
        PhosphorRules::RuleSet edited = *setOpt;
        const auto seeded = seededSteamRule(edited);
        QVERIFY(seeded.has_value());
        const QUuid steamId = seeded->id;
        PhosphorRules::Rule mine = *seeded;
        // A plausible user edit: keep the id, narrow it to one's own liking.
        mine.match = PhosphorRules::MatchExpression::makeLeaf(
            PhosphorRules::Field::Title, PhosphorRules::Operator::Contains, QStringLiteral("Friends"));
        mine.actions.clear();
        PhosphorRules::RuleAction blanket;
        blanket.type = QString(PhosphorRules::ActionType::Exclude);
        mine.actions.append(blanket);
        const QJsonObject mineJson = mine.toJson();
        QVERIFY(edited.updateRule(mine));
        QVERIFY(edited.saveToFile(rulesPath));

        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const auto after = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(after.has_value());
        const auto survivor = after->ruleById(steamId);
        QVERIFY2(survivor.has_value(), "the user's edited rule must survive the repair");
        QCOMPARE(survivor->toJson(), mineJson);
    }

    /// The other half of the reclaim predicate. A user who kept the stock
    /// match but changed only the ACTION has still edited the rule, and
    /// `isRetiredSteamRuleShape` checks the action before it compares the
    /// match — without this, deleting that check leaves the whole suite green.
    void testSteamRuleRepairLeavesAnActionOnlyEditAlone()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertBareV3Config());

        const QString rulesPath = ConfigDefaults::rulesFilePath();
        auto setOpt = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(setOpt.has_value());
        PhosphorRules::RuleSet edited = *setOpt;
        const auto seeded = seededSteamRule(edited);
        QVERIFY(seeded.has_value());
        const QUuid steamId = seeded->id;

        PhosphorRules::Rule mine = *seeded;
        // The retired MATCH, verbatim...
        mine.match = PhosphorRules::MatchExpression::makeAll(
            {PhosphorRules::MatchExpression::makeLeaf(PhosphorRules::Field::WindowClass,
                                                      PhosphorRules::Operator::Contains, QStringLiteral("steam")),
             PhosphorRules::MatchExpression::makeNone({PhosphorRules::MatchExpression::makeLeaf(
                 PhosphorRules::Field::Title, PhosphorRules::Operator::Equals, QStringLiteral("Steam"))})});
        // ...but the user swapped the blanket Exclude for the scoped action
        // themselves. That is their edit, and it must be left alone.
        mine.actions.clear();
        PhosphorRules::RuleAction scoped;
        scoped.type = QString(PhosphorRules::ActionType::ExcludePlacement);
        mine.actions.append(scoped);
        const QJsonObject mineJson = mine.toJson();
        QVERIFY(edited.updateRule(mine));
        QVERIFY(edited.saveToFile(rulesPath));

        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const auto after = PhosphorRules::RuleSet::loadFromFile(rulesPath);
        QVERIFY(after.has_value());
        const auto survivor = after->ruleById(steamId);
        QVERIFY2(survivor.has_value(), "an action-only edit must survive the repair");
        QCOMPARE(survivor->toJson(), mineJson);
    }
};

QTEST_MAIN(TestMigrationV3ToV4Steam)
#include "test_migration_v3_to_v4_steam.moc"
