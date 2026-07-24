// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v3_to_v4_animations.cpp
 * @brief v3 → v4 migration tests for the animation folds — the
 *        `Animations.AnimationAppRules` array folding into
 *        `OverrideAnimation{Shader,Timing}` rules, and the
 *        `Animations.WindowFiltering.{Applications,WindowClasses}` lists
 *        folding into `ExcludeAnimations` rules.
 *
 * Split out of test_migration_v3_to_v4.cpp; the shared config/rules JSON
 * helpers live in MigrationV3V4Fixture.h.
 */

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>

#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "config/configmigration.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/RuleSet.h>

#include "MigrationV3V4Fixture.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestMigrationV3ToV4Animations : public QObject, public MigrationV3V4Fixture
{
    Q_OBJECT

    // ─── Animation App Rules → Rules ────────────────────────────────
    //
    // The v3 `Animations.AnimationAppRules` array folds into rules.json
    // as `OverrideAnimation{Shader,Timing}` actions on a
    // `WindowClass Contains <pattern>` matcher. Below covers the conversion
    // shape, the drop-on-malformed contract, the engaged-blocking / inherit
    // sentinels, and idempotent rule-id derivation.

private:
    /// Build a v3 config carrying ONLY animation app rules (no Display.* keys,
    /// no Snapping defaults). Keeps animation-specific tests narrow — they
    /// don't have to filter the disable-rule + seed-rule noise out.
    ///
    /// The v4 group / key names are pinned as inline `QStringLiteral`
    /// literals — the test's role is to be an INDEPENDENT WITNESS of the
    /// frozen v4 on-disk wire format. If a future maintainer violates the
    /// freeze policy (forbidden by the `Legacy` struct's freeze-policy
    /// comment block in configkeys.h) and renames
    /// `Legacy::v4AnimationAppRulesKey` from "AnimationAppRules", the
    /// accessor-based form would silently update in lockstep with
    /// production; inline-literal pins catch the drift. Mirrors the
    /// sibling `makeV3Config` fixture's pattern for Display.* v3 keys.
    QJsonObject makeV3ConfigWithAnimationRules(const QJsonArray& animationRules)
    {
        QJsonObject root;
        root.insert(ConfigKeys::versionKey(), 3);
        if (!animationRules.isEmpty()) {
            QJsonObject animations;
            animations.insert(QStringLiteral("AnimationAppRules"), animationRules);
            root.insert(QStringLiteral("Animations"), animations);
        }
        return root;
    }

    QJsonObject makeShaderRule(const QString& classPattern, const QString& eventPath, const QString& effectId,
                               const QJsonObject& shaderParams = {})
    {
        QJsonObject o;
        o.insert(QStringLiteral("classPattern"), classPattern);
        o.insert(QStringLiteral("eventPath"), eventPath);
        o.insert(QStringLiteral("kind"), QStringLiteral("shader"));
        o.insert(QStringLiteral("effectId"), effectId);
        if (!shaderParams.isEmpty()) {
            o.insert(QStringLiteral("shaderParams"), shaderParams);
        }
        return o;
    }

    QJsonObject makeTimingRule(const QString& classPattern, const QString& eventPath, const QString& curve,
                               int durationMs)
    {
        QJsonObject o;
        o.insert(QStringLiteral("classPattern"), classPattern);
        o.insert(QStringLiteral("eventPath"), eventPath);
        o.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        if (!curve.isEmpty()) {
            o.insert(QStringLiteral("curve"), curve);
        }
        if (durationMs > 0) {
            o.insert(QStringLiteral("durationMs"), durationMs);
        }
        return o;
    }

    /// All rules carrying an OverrideAnimation* action (either shader or
    /// timing). The animation rule set is otherwise independent of the
    /// assignment / disable cascade, so most tests want this filtered view.
    QList<QJsonObject> animationRulesFromRules()
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rulesFromRules()) {
            const QJsonObject rule = v.toObject();
            for (const QJsonValue& a : rule.value(QStringLiteral("actions")).toArray()) {
                const QString type = a.toObject().value(QStringLiteral("type")).toString();
                if (type == QLatin1String("overrideAnimationShader")
                    || type == QLatin1String("overrideAnimationTiming")) {
                    out.append(rule);
                    break;
                }
            }
        }
        return out;
    }

    /// First animation action on @p rule, or an empty object if none.
    QJsonObject animationAction(const QJsonObject& rule)
    {
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject a = v.toObject();
            const QString type = a.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("overrideAnimationShader") || type == QLatin1String("overrideAnimationTiming")) {
                return a;
            }
        }
        return {};
    }

    QJsonObject makeV3ConfigWithAnimationExclusions(const QString& apps, const QString& windowClasses)
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 3);
        QJsonObject filtering;
        if (!apps.isNull()) {
            filtering.insert(QStringLiteral("Applications"), apps);
        }
        if (!windowClasses.isNull()) {
            filtering.insert(QStringLiteral("WindowClasses"), windowClasses);
        }
        if (!filtering.isEmpty()) {
            QJsonObject animations;
            animations.insert(QStringLiteral("WindowFiltering"), filtering);
            root.insert(QStringLiteral("Animations"), animations);
        }
        return root;
    }

    /// Animation-exclude rules are the only ExcludeAnimations-action shape the
    /// migration produces, with exactly one action, so filtering for the exact
    /// single-action list {excludeAnimations} cleanly isolates them. (Exact
    /// match, not "contains": if the migration ever emits a second action
    /// alongside it, this filter must be revisited rather than silently passing.)
    QList<QJsonObject> animationExclusionRules(const QJsonArray& rules)
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (actionTypes(r) != QStringList{QStringLiteral("excludeAnimations")}) {
                continue;
            }
            out.append(r);
        }
        return out;
    }

private Q_SLOTS:

    void testAnimationAppRules_shaderAndTimingKindsBecomeRules()
    {
        IsolatedConfigGuard guard;
        QJsonObject shaderParams;
        shaderParams.insert(QStringLiteral("amplitude"), 0.3);
        QJsonArray src;
        src.append(makeShaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"),
                                  shaderParams));
        src.append(makeTimingRule(QStringLiteral("Code"), QStringLiteral("window.close"),
                                  QStringLiteral("0.2,0.0,0.2,1.0"), 250));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 2);

        // Locate by action type — list ordering is asserted in a separate test.
        QJsonObject shaderRule;
        QJsonObject timingRule;
        for (const QJsonObject& rule : animRules) {
            const QJsonObject action = animationAction(rule);
            if (action.value(QStringLiteral("type")).toString() == QLatin1String("overrideAnimationShader")) {
                shaderRule = rule;
            } else {
                timingRule = rule;
            }
        }
        QVERIFY(!shaderRule.isEmpty());
        QVERIFY(!timingRule.isEmpty());

        // Action wire shape: `event`, `effectId`, `curve`, `durationMs`
        // live FLAT on the action object alongside `type` — there is no
        // outer wrapper named `params`. The shader case carries an INNER
        // `params` object that holds the effect-specific tunables
        // (amplitude, etc.); that nested key is the shader effect's own
        // payload, distinct from the action's flat wire params.

        // Shader rule shape.
        QCOMPARE(matchLeafValueByOp(shaderRule, QStringLiteral("windowClass"), QStringLiteral("contains")),
                 QStringLiteral("firefox"));
        QCOMPARE(shaderRule.value(QStringLiteral("enabled")).toBool(), true);
        QVERIFY2(shaderRule.value(QStringLiteral("priority")).toInt() > 0,
                 "animation app rules get a positive descending-by-list-order priority");
        const QJsonObject shaderAction = animationAction(shaderRule);
        QCOMPARE(shaderAction.value(QStringLiteral("event")).toString(), QStringLiteral("window.open"));
        QCOMPARE(shaderAction.value(QStringLiteral("effectId")).toString(), QStringLiteral("dissolve"));
        QCOMPARE(shaderAction.value(QStringLiteral("params")).toObject().value(QStringLiteral("amplitude")).toDouble(),
                 0.3);

        // Timing rule shape.
        QCOMPARE(matchLeafValueByOp(timingRule, QStringLiteral("windowClass"), QStringLiteral("contains")),
                 QStringLiteral("Code"));
        QCOMPARE(timingRule.value(QStringLiteral("enabled")).toBool(), true);
        QVERIFY(timingRule.value(QStringLiteral("priority")).toInt() > 0);
        const QJsonObject timingAction = animationAction(timingRule);
        QCOMPARE(timingAction.value(QStringLiteral("event")).toString(), QStringLiteral("window.close"));
        QCOMPARE(timingAction.value(QStringLiteral("curve")).toString(), QStringLiteral("0.2,0.0,0.2,1.0"));
        QCOMPARE(timingAction.value(QStringLiteral("durationMs")).toInt(), 250);
    }

    void testAnimationAppRules_priorityIsDescendingByListOrder()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // Three valid entries — first should get the highest priority,
        // last should get priority 1 (descending by list order, lowest is 1).
        src.append(makeShaderRule(QStringLiteral("first"), QStringLiteral("window.open"), QStringLiteral("popup")));
        src.append(makeShaderRule(QStringLiteral("second"), QStringLiteral("window.open"), QStringLiteral("fade")));
        src.append(makeShaderRule(QStringLiteral("third"), QStringLiteral("window.open"), QStringLiteral("blur")));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QMap<QString, int> priorityByPattern;
        for (const QJsonObject& rule : animationRulesFromRules()) {
            const QString pattern = matchLeafValueByOp(rule, QStringLiteral("windowClass"), QStringLiteral("contains"));
            priorityByPattern[pattern] = rule.value(QStringLiteral("priority")).toInt();
        }
        QCOMPARE(priorityByPattern.size(), 3);
        // count == 3 → first=3, second=2, third=1 (always > 0).
        QCOMPARE(priorityByPattern[QStringLiteral("first")], 3);
        QCOMPARE(priorityByPattern[QStringLiteral("second")], 2);
        QCOMPARE(priorityByPattern[QStringLiteral("third")], 1);
    }

    void testAnimationAppRules_engagedEmptyEffectIdPreserved()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // Engaged-blocking sentinel: empty effectId means "no animation for
        // this app on this event" — must round-trip into the migrated rule
        // as an explicit empty-string effectId, NOT a missing key.
        src.append(makeShaderRule(QStringLiteral("krita"), QStringLiteral("window.open"), QString()));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 1);
        // Inlined action params — `effectId` lives directly on the action
        // object alongside `type` and `event`, not nested under `params`.
        const QJsonObject action = animationAction(animRules.first());
        QVERIFY2(action.contains(QStringLiteral("effectId")),
                 "engaged-blocking effectId must be present as the empty string, not absent");
        QCOMPARE(action.value(QStringLiteral("effectId")).toString(), QString());
    }

    void testAnimationAppRules_durationAndCurveSentinelsOmitted()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // durationMs <= 0 means "inherit per-event default" — the output
        // must omit the key entirely so the resolver falls through. Same
        // for an empty `curve`. Three sentinel shapes must all coalesce:
        //   - absent key       (the original / most common shape)
        //   - explicit `0`     (a user toggling between inherit and a
        //                       concrete value via the editor)
        //   - explicit negative (a hand-edited file or an editor bug)
        //
        // The production check is `durationMs > 0`, so a regression that
        // flipped it to `>= 0` or to `!= 0` would silently emit the key for
        // the explicit-zero / negative forms — invisible without a positive
        // test here.
        QJsonObject inheritAbsent;
        inheritAbsent.insert(QStringLiteral("classPattern"), QStringLiteral("vlc"));
        inheritAbsent.insert(QStringLiteral("eventPath"), QStringLiteral("window.close"));
        inheritAbsent.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        // No `curve`, no `durationMs` — both inherit.
        src.append(inheritAbsent);

        QJsonObject inheritExplicitZero;
        inheritExplicitZero.insert(QStringLiteral("classPattern"), QStringLiteral("mpv"));
        inheritExplicitZero.insert(QStringLiteral("eventPath"), QStringLiteral("window.close"));
        inheritExplicitZero.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        inheritExplicitZero.insert(QStringLiteral("durationMs"), 0); // explicit 0 → inherit
        src.append(inheritExplicitZero);

        QJsonObject inheritExplicitNegative;
        inheritExplicitNegative.insert(QStringLiteral("classPattern"), QStringLiteral("krita"));
        inheritExplicitNegative.insert(QStringLiteral("eventPath"), QStringLiteral("window.close"));
        inheritExplicitNegative.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        inheritExplicitNegative.insert(QStringLiteral("durationMs"), -50); // explicit negative → inherit
        src.append(inheritExplicitNegative);

        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 3);
        for (const QJsonObject& rule : animRules) {
            const QJsonObject action = animationAction(rule);
            QCOMPARE(action.value(QStringLiteral("event")).toString(), QStringLiteral("window.close"));
            QVERIFY2(!action.contains(QStringLiteral("durationMs")),
                     "durationMs <= 0 is the inherit sentinel — must be absent from the migrated params "
                     "(verified for absent-key, explicit 0, and explicit negative shapes)");
            QVERIFY2(!action.contains(QStringLiteral("curve")),
                     "empty curve is the inherit sentinel — must be absent from the migrated params");
        }
    }

    void testAnimationAppRules_malformedEntriesDropped()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // Empty classPattern → dropped.
        src.append(makeShaderRule(QString(), QStringLiteral("window.open"), QStringLiteral("x")));
        // Empty eventPath → dropped.
        src.append(makeShaderRule(QStringLiteral("ok"), QString(), QStringLiteral("y")));
        // Unknown kind → dropped (silent coercion would be dangerous — an
        // unknown kind silently coerced to Shader produces an engaged-empty
        // rule that disables animations for matching windows).
        QJsonObject unknownKind;
        unknownKind.insert(QStringLiteral("classPattern"), QStringLiteral("gimp"));
        unknownKind.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        unknownKind.insert(QStringLiteral("kind"), QStringLiteral("xyzzy"));
        src.append(unknownKind);
        // Non-object entry → dropped (the source array element isn't an object).
        src.append(QJsonValue(QStringLiteral("not-an-object")));
        // Fully empty {} entry → all required keys missing → dropped at the
        // classPattern/eventPath emptiness gate.
        src.append(QJsonObject{});
        // classPattern is a number (non-string) → toString() returns ""
        // → dropped at the emptiness gate.
        QJsonObject nonStringPattern;
        nonStringPattern.insert(QStringLiteral("classPattern"), 42);
        nonStringPattern.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        nonStringPattern.insert(QStringLiteral("kind"), QStringLiteral("shader"));
        nonStringPattern.insert(QStringLiteral("effectId"), QStringLiteral("x"));
        src.append(nonStringPattern);
        // kind is missing entirely → unknown-kind branch → dropped.
        QJsonObject missingKind;
        missingKind.insert(QStringLiteral("classPattern"), QStringLiteral("inkscape"));
        missingKind.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        src.append(missingKind);
        // One valid entry survives.
        src.append(makeShaderRule(QStringLiteral("survivor"), QStringLiteral("window.open"), QStringLiteral("pop")));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 1);
        QCOMPARE(matchLeafValueByOp(animRules.first(), QStringLiteral("windowClass"), QStringLiteral("contains")),
                 QStringLiteral("survivor"));
    }

    void testAnimationAppRules_absentSourceProducesNoAnimationRules()
    {
        IsolatedConfigGuard guard;
        // A v3 config with NO Animations.AnimationAppRules — the migration
        // must produce zero animation-action rules (other rules unaffected).
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());

        QVERIFY(ConfigMigration::ensureJsonConfig());
        QCOMPARE(animationRulesFromRules().size(), 0);
        // And no stash key persists.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        QVERIFY(!cfg.contains(QStringLiteral("_v4AnimationRulesStash")));
    }

    void testAnimationAppRules_coexistWithAssignmentAndDisableRules()
    {
        // The animation rules and the assignment/disable rules target
        // disjoint slot namespaces, so a fixture combining both must produce
        // BOTH families in rules.json — and neither family clobbers
        // the other. This test is the load-bearing assertion for the
        // strip-after-rebuild path: the fixture populates
        // `_v4AnimationRulesStash`, so the post-conversion stash-absence
        // check actually verifies the rebuild branch stripped it (a
        // never-populated stash is trivially absent and verifies nothing).
        IsolatedConfigGuard guard;
        QJsonObject cfg = makeV3Config();
        QJsonArray animRules;
        animRules.append(
            makeShaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        // Inline literals — independent-witness pin matching the
        // `makeV3ConfigWithAnimationRules` fixture pattern. The sibling
        // `makeV3ConfigWithAnimationRules` fixture's docstring documents
        // the rationale: a future freeze-policy violation that renames
        // `Legacy::v4AnimationAppRulesKey` away from "AnimationAppRules"
        // must surface as test breakage, not silently update through
        // the same accessor production uses.
        QJsonObject animations;
        animations.insert(QStringLiteral("AnimationAppRules"), animRules);
        cfg.insert(QStringLiteral("Animations"), animations);
        writeJson(ConfigDefaults::configFilePath(), cfg);
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // Animation family — exactly one rule emitted.
        const QList<QJsonObject> animRulesOut = animationRulesFromRules();
        QCOMPARE(animRulesOut.size(), 1);
        const QJsonObject animRule = animRulesOut.first();
        // Animation rule sits at priority 1 (count == 1 → priority 1).
        QCOMPARE(animRule.value(QStringLiteral("priority")).toInt(), 1);

        // Assignment cascade — every fixture-pinned level (306/304/303/301)
        // must still produce an assignment rule (setEngineMode + NOT
        // disableEngine), matching testCascadePriorities_exactValues. A
        // regression where an animation rule collided with an assignment
        // rule's id would drop the assignment from the rebuilt set; counting
        // every pinned level guards that.
        const QJsonArray allRules = rulesFromRules();
        QVERIFY(hasAssignmentAtPriority(allRules, 306));
        QVERIFY(hasAssignmentAtPriority(allRules, 304));
        QVERIFY(hasAssignmentAtPriority(allRules, 303));
        QVERIFY(hasAssignmentAtPriority(allRules, 301));

        // Disable family — count must match testDisableListRules exactly
        // (fixture: 1 + 2 + 1 + 1 = 5). Drift here means an animation rule
        // clobbered a disable rule.
        QCOMPARE(disableRules(allRules).size(), 5);

        // Animation rule id is distinct from every other rule's id — disjoint
        // slot namespaces are nothing without disjoint identities (a clash on
        // (id) would overwrite a sibling rule inside RuleSet::addRule).
        const QString animRuleId = animRule.value(QStringLiteral("id")).toString();
        QVERIFY(!animRuleId.isEmpty());
        int collisions = 0;
        for (const QJsonValue& v : allRules) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == animRuleId && r != animRule) {
                ++collisions;
            }
        }
        QCOMPARE(collisions, 0);

        // The migration's scratch key MUST be stripped from config.json after
        // the rebuild branch consumed it. This fixture populates the stash
        // (animRules.size() > 0), so a regression that stopped stripping
        // would fail here — distinguishing this from the matching assertion
        // in testFullConversion_producesRules where the fixture never
        // populates the stash to begin with.
        const QJsonObject cfgAfter = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationRulesStash")),
                 "the rebuild branch must strip _v4AnimationRulesStash after consuming it");
    }

    void testAnimationAppRules_idempotentRuleIds()
    {
        // Re-running the migration against a v3 fixture must yield
        // byte-identical animation rules — the rule id is derived from
        // (classPattern, eventPath, kind) via a fixed v5-UUID namespace, so
        // a repeat run produces the same UUID and the RuleSet round-trip
        // stays stable. This is the conversion's idempotency at the rule
        // level (the file-level idempotency is asserted by
        // testIdempotency_runTwiceIsNoOp above; this test scopes the assertion
        // tightly to the animation-rule output to catch a future drift in
        // the bridge's namespace UUID or segment-encoding).
        IsolatedConfigGuard guard;
        QJsonArray src;
        src.append(makeShaderRule(QStringLiteral("kitty"), QStringLiteral("window.open"), QStringLiteral("pop-in")));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString firstId = animationRulesFromRules().first().value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());

        // Golden assertion: re-derive the expected id inline against the
        // SPEC of the namespace UUID + length-prefixed segment encoding.
        // The migration owns both ends of the v5-UUID derivation, so the
        // idempotency check above (same inputs → same id) cannot catch a
        // namespace-UUID or encoder change — both sides of that compare
        // drift together. Duplicating the namespace literal and the
        // segment-encoding format here means a deliberate change to either
        // forces a deliberate update to this test.
        const QUuid kExpectedNamespace(QStringLiteral("{b3f2c1a0-7d4e-5f6a-8b9c-0d1e2f3a4b5c}"));
        // Length-prefixed concatenation: <segment-size>:<segment-bytes> per
        // segment, no separator. Spelled out as three explicit segments so a
        // future input change here doesn't require recomputing digit lengths
        // by hand.
        //
        //   segment 1 → classPattern "kitty"           → "5:kitty"
        //   segment 2 → eventPath    "window.open"     → "11:window.open"
        //   segment 3 → kind         "shader"          → "6:shader"
        const QString kExpectedKey =
            QStringLiteral("5:kitty") + QStringLiteral("11:window.open") + QStringLiteral("6:shader");
        const QString expectedId = QUuid::createUuidV5(kExpectedNamespace, kExpectedKey).toString();
        QCOMPARE(firstId, expectedId);

        // Wipe rules.json (force the rebuild path on next call) and
        // re-stage the same v3 inputs.
        QFile::remove(ConfigDefaults::rulesFilePath());
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString secondId = animationRulesFromRules().first().value(QStringLiteral("id")).toString();
        QCOMPARE(secondId, firstId);
    }

    void testAnimationAppRules_cleanupOnlyBranchStripsStashes()
    {
        // The cleanup-only branch of finalizeV4Conversion runs when
        // rules.json already exists as a valid v4 rule set but the
        // previous run failed before stripping config.json's scratch keys.
        // A regression that stops stripping the stash on the cleanup retry
        // would leave inert _v4*Stash keys on disk forever — the rebuild
        // path is gated off (rules.json exists), so the main path
        // tested by other cases never re-runs to clean up.
        //
        // Fixture: a v4-stamped config.json that carries both stash keys,
        // plus an existing v4 rules.json (so rulesAlreadyConverted
        // returns true). ensureJsonConfigImpl's "Already at OR above current
        // version" branch still calls finalizeV4Conversion on every startup,
        // which takes the cleanup-only
        // sub-branch when the rule store already parses as v4 — strip both
        // stashes, leave rules.json byte-identical, never recreate
        // assignments.json or its .migrated quarantine artifact.
        IsolatedConfigGuard guard;

        QJsonObject cfg;
        cfg.insert(QStringLiteral("_version"), 4);
        // Plausible stash content for both keys — content is irrelevant to
        // the stripping logic; presence is what the predicate keys off.
        QJsonObject disableStash;
        disableStash.insert(QStringLiteral("snappingMonitors"), QStringLiteral("DP-1"));
        cfg.insert(QStringLiteral("_v4DisableStash"), disableStash);
        QJsonArray animStash;
        animStash.append(
            makeShaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        cfg.insert(QStringLiteral("_v4AnimationRulesStash"), animStash);
        // The exclusion stash uses the same Object-with-string-fields shape as
        // the disable stash. Plausible content for cleanup-branch coverage —
        // the actual conversion of these into Exclude rules is covered by
        // the dedicated testExclusions_* cases below.
        QJsonObject exclusionStash;
        exclusionStash.insert(QStringLiteral("Applications"), QStringLiteral("firefox"));
        exclusionStash.insert(QStringLiteral("WindowClasses"), QStringLiteral("kitty"));
        cfg.insert(QStringLiteral("_v4ExclusionStash"), exclusionStash);
        // Plausible animation-exclusion stash content too. Same shape as the
        // snapping exclusion stash (object with Applications / WindowClasses
        // string fields).
        QJsonObject animationExclusionStash;
        animationExclusionStash.insert(QStringLiteral("Applications"), QStringLiteral("vlc"));
        animationExclusionStash.insert(QStringLiteral("WindowClasses"), QStringLiteral("mpv"));
        cfg.insert(QStringLiteral("_v4AnimationExclusionStash"), animationExclusionStash);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        // Pre-existing rules.json: produced via the production save
        // path so the file shape always matches what `loadFromFile` expects
        // (hand-written JSON would silently drift if the library tightens
        // its load contract, flipping rulesAlreadyConverted to false
        // and silently rebuilding instead of taking the cleanup branch).
        PhosphorRules::RuleSet emptySet;
        QDir().mkpath(QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath());
        QVERIFY(emptySet.saveToFile(ConfigDefaults::rulesFilePath()));
        const QByteArray rulesBefore = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(!rulesBefore.isEmpty());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // Both stash keys gone from config.json.
        const QJsonObject cfgAfter = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4DisableStash")),
                 "cleanup-only branch must strip _v4DisableStash");
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationRulesStash")),
                 "cleanup-only branch must strip _v4AnimationRulesStash");
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4ExclusionStash")),
                 "cleanup-only branch must strip _v4ExclusionStash");
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationExclusionStash")),
                 "cleanup-only branch must strip _v4AnimationExclusionStash");

        // rules.json is byte-identical — the cleanup branch must NOT
        // rebuild and overwrite user-edited rules.
        const QByteArray rulesAfter = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QCOMPARE(rulesAfter, rulesBefore);

        // Probe that the CLEANUP-ONLY branch was actually taken (not just
        // that the post-conditions happen to hold). The rebuild path writes
        // or quarantines assignments.json; the cleanup branch never touches
        // it. With no assignments.json staged in the fixture, the absence
        // of any assignments artifact after migration distinguishes the two
        // branches.
        QVERIFY2(!QFile::exists(assignmentsPath()), "cleanup-only branch must not recreate assignments.json");
        QVERIFY2(!QFile::exists(assignmentsPath() + QStringLiteral(".migrated")),
                 "cleanup-only branch must not produce an assignments.json.migrated artifact");
    }

    // ─── Animation exclusions fold ────────────────────────────────────────
    // The legacy `Animations.WindowFiltering.{Applications,WindowClasses}`
    // lists fold into `ExcludeAnimations`-action rules with
    // `DesktopFile Contains <pattern>` (applications) or
    // `WindowClass Contains <pattern>` (window classes) leaves — the
    // same match semantics the legacy effect-side bridge produced for
    // the animation pipeline, so an upgrading user's "no animations for
    // firefox" rule keeps the same matching behaviour.

    void testAnimationExclusions_applicationsBecomeDesktopFileContainsRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QStringLiteral("firefox,konsole"), QString()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> rules = animationExclusionRules(rulesFromRules());
        QCOMPARE(rules.size(), 2);
        QStringList patterns;
        for (const QJsonObject& r : rules) {
            QCOMPARE(matchLeafValueByOp(r, QStringLiteral("desktopFile"), QStringLiteral("contains")).isEmpty(), false);
            patterns.append(matchLeafValueByOp(r, QStringLiteral("desktopFile"), QStringLiteral("contains")));
        }
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox") << QStringLiteral("konsole"));
    }

    void testAnimationExclusions_windowClassesBecomeWindowClassContainsRules()
    {
        // Unlike the snapping-side fold (which collapsed both lists into
        // `AppId AppIdMatches` rules to mirror the daemon-bridge semantics),
        // the animation-side fold preserves the effect-bridge split:
        // WindowClasses entries produce `WindowClass Contains` leaves,
        // NOT `DesktopFile Contains`.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QString(), QStringLiteral("kitty,org.kde.dolphin")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> rules = animationExclusionRules(rulesFromRules());
        QCOMPARE(rules.size(), 2);
        QStringList patterns;
        for (const QJsonObject& r : rules) {
            patterns.append(matchLeafValueByOp(r, QStringLiteral("windowClass"), QStringLiteral("contains")));
        }
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("kitty") << QStringLiteral("org.kde.dolphin"));
    }

    void testAnimationExclusions_emptyAndWhitespacePatternsDropped()
    {
        IsolatedConfigGuard guard;
        writeJson(
            ConfigDefaults::configFilePath(),
            makeV3ConfigWithAnimationExclusions(QStringLiteral(",firefox,  ,,konsole,"), QStringLiteral("   ,,")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(animationExclusionRules(rulesFromRules()).size(), 2);
    }

    void testAnimationExclusions_idempotentRuleIds()
    {
        // Counterpart to `testExclusions_idempotentRuleIds` — the
        // animation-side fold uses a DIFFERENT segment encoding (4 segments
        // including the action-type discriminator instead of the snapping
        // side's 3). The discriminator is what lets a future user-authored
        // Exclude rule and a migrated ExcludeAnimations rule share the same
        // (field, op, pattern) tuple without collapsing to the same id.
        // Pin the namespace + 4-segment shape inline so a future refactor
        // of either piece (namespace literal, Field/Operator enum value
        // renumbering, ActionType wire-string rename) has to update this
        // test deliberately rather than drift silently in both producer
        // and check.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QString(), QStringLiteral("firefox")));
        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString firstId =
            animationExclusionRules(rulesFromRules()).first().value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());

        // The namespace UUID is shared with the snapping-side fold — same
        // `appendExclusionRulesFromStash` / `appendAnimationExclusionRulesFromStash`
        // namespace constant in `configmigration.cpp::exclusionMigrationNamespace`.
        const QUuid kExpectedNamespace(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
        // Segment encoding: <size>:<bytes> per segment, no separator.
        // The 4-segment shape for an animation WindowClass rule:
        //   field   = static_cast<int>(Field::WindowClass) = 1     → "1:1"
        //   op      = static_cast<int>(Operator::Contains) = 1     → "1:1"
        //   pattern = "firefox"                                    → "7:firefox"
        //   action  = "excludeAnimations" (17 bytes)               → "17:excludeAnimations"
        const QString kExpectedKey = QStringLiteral("1:1") + QStringLiteral("1:1") + QStringLiteral("7:firefox")
            + QStringLiteral("17:excludeAnimations");
        const QString expectedId = QUuid::createUuidV5(kExpectedNamespace, kExpectedKey).toString();
        QCOMPARE(firstId, expectedId);

        // Round-trip: wipe rules.json + re-stage same v3 input.
        // Re-running the migration on identical input must produce the
        // same id so `RuleStore::addRule`'s id-collision check
        // collapses the second run to a no-op instead of duplicating.
        QFile::remove(ConfigDefaults::rulesFilePath());
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QString(), QStringLiteral("firefox")));
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString secondId =
            animationExclusionRules(rulesFromRules()).first().value(QStringLiteral("id")).toString();
        QCOMPARE(secondId, firstId);
    }

    void testAnimationExclusions_groupAndStashStrippedAfterFinalize()
    {
        // Both the v3 `Animations.WindowFiltering.{Applications,WindowClasses}`
        // leaf keys AND the `_v4AnimationExclusionStash` scratch root key
        // MUST be gone from config.json after a successful conversion.
        // The Animations group itself stays (it carries other v4 settings);
        // only the two leaf keys under WindowFiltering are stripped.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QStringLiteral("firefox"), QStringLiteral("kitty")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject cfgAfter = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationExclusionStash")),
                 "finalizeV4Conversion must strip the _v4AnimationExclusionStash scratch key");
        // The Animations.WindowFiltering group either disappears entirely
        // (no other keys lived there in this fixture) or survives without
        // the two leaf keys. Both are acceptable; the assertion just
        // requires the leaf keys not survive.
        const QJsonObject animations = cfgAfter.value(QStringLiteral("Animations")).toObject();
        const QJsonObject filtering = animations.value(QStringLiteral("WindowFiltering")).toObject();
        QVERIFY2(!filtering.contains(QStringLiteral("Applications")),
                 "migrateV3ToV4 must remove Animations.WindowFiltering.Applications");
        QVERIFY2(!filtering.contains(QStringLiteral("WindowClasses")),
                 "migrateV3ToV4 must remove Animations.WindowFiltering.WindowClasses");
    }

    void testAnimationExclusions_absentSourceProducesNoRules()
    {
        IsolatedConfigGuard guard;
        // No Animations.WindowFiltering group at all.
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationExclusions(QString(), QString()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(animationExclusionRules(rulesFromRules()).size(), 0);
    }
};

QTEST_MAIN(TestMigrationV3ToV4Animations)
#include "test_migration_v3_to_v4_animations.moc"
