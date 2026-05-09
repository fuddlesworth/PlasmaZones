// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorAnimation/AnimationAppRule.h>
#include <PhosphorAnimation/AnimationAppRuleResolver.h>
#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QTest>

using PhosphorAnimation::CurveRegistry;
using PhosphorAnimation::Profile;
using PhosphorAnimationShaders::AnimationAppRule;
using PhosphorAnimationShaders::AnimationAppRuleList;
using PhosphorAnimationShaders::resolveAnimationDuration;
using PhosphorAnimationShaders::resolveAnimationMotionProfile;
using PhosphorAnimationShaders::resolveAnimationShaderProfile;
using PhosphorAnimationShaders::ShaderProfile;
using PhosphorAnimationShaders::ShaderProfileTree;

class TestAnimationAppRuleResolver : public QObject
{
    Q_OBJECT

private:
    static AnimationAppRule shaderRule(const QString& pattern, const QString& event, const QString& effectId,
                                       const QVariantMap& params = {})
    {
        AnimationAppRule r;
        r.classPattern = pattern;
        r.eventPath = event;
        r.kind = AnimationAppRule::Kind::Shader;
        r.effectId = effectId;
        r.shaderParams = params;
        return r;
    }

    static AnimationAppRule timingRule(const QString& pattern, const QString& event, int durationMs,
                                       const QString& curve = {})
    {
        AnimationAppRule r;
        r.classPattern = pattern;
        r.eventPath = event;
        r.kind = AnimationAppRule::Kind::Timing;
        r.durationMs = durationMs;
        r.curve = curve;
        return r;
    }

    static ShaderProfileTree treeWithBaseline(const QString& effectId, const QVariantMap& params = {})
    {
        ShaderProfileTree tree;
        ShaderProfile baseline;
        baseline.effectId = effectId;
        if (!params.isEmpty()) {
            baseline.parameters = params;
        }
        tree.setBaseline(baseline);
        return tree;
    }

private Q_SLOTS:

    // ── Shader cascade ────────────────────────────────────────────────

    void testShader_ruleHit_returnsRuleEffectIdIgnoringTree()
    {
        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        const auto tree = treeWithBaseline(QStringLiteral("popin"));

        const auto profile =
            resolveAnimationShaderProfile(rules, tree, QStringLiteral("Firefox"), QStringLiteral("window.open"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("dissolve"));
    }

    void testShader_ruleHit_surfacesRuleParams()
    {
        QVariantMap ruleParams;
        ruleParams.insert(QStringLiteral("scale"), 0.5);
        ruleParams.insert(QStringLiteral("blur"), 12);
        AnimationAppRuleList rules;
        rules.append(
            shaderRule(QStringLiteral("term"), QStringLiteral("window.open"), QStringLiteral("zoom"), ruleParams));

        const auto profile = resolveAnimationShaderProfile(
            rules, ShaderProfileTree{}, QStringLiteral("konsole-terminal"), QStringLiteral("window.open"));
        QCOMPARE(profile.effectiveParameters(), ruleParams);
    }

    void testShader_emptyEffectIdRule_blocksTreeFallthrough()
    {
        // A rule with an explicitly-empty effectId is the "block per-event
        // default for matching windows" sentinel. The tree's populated
        // baseline must NOT leak through.
        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("noanim"), QStringLiteral("window.close"), QString()));
        const auto tree = treeWithBaseline(QStringLiteral("dissolve"));

        const auto profile =
            resolveAnimationShaderProfile(rules, tree, QStringLiteral("noanim-app"), QStringLiteral("window.close"));
        QVERIFY(profile.effectId.has_value());
        QVERIFY(profile.effectId->isEmpty());
    }

    void testShader_classMiss_fallsThroughToTree()
    {
        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        const auto tree = treeWithBaseline(QStringLiteral("popin"));

        const auto profile =
            resolveAnimationShaderProfile(rules, tree, QStringLiteral("Spotify"), QStringLiteral("window.open"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("popin"));
    }

    void testShader_eventMiss_fallsThroughToTree()
    {
        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        const auto tree = treeWithBaseline(QStringLiteral("popin"));

        // Same class, different event — rule.eventPath is exact-match.
        const auto profile =
            resolveAnimationShaderProfile(rules, tree, QStringLiteral("Firefox"), QStringLiteral("window.close"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("popin"));
    }

    void testShader_emptyWindowClass_shortCircuitsToTree()
    {
        // No window class → no rule can match. Substring of an empty
        // pattern would otherwise also match every empty class, but the
        // resolver short-circuits the rule walk so the tree wins.
        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        const auto tree = treeWithBaseline(QStringLiteral("popin"));

        const auto profile = resolveAnimationShaderProfile(rules, tree, QString(), QStringLiteral("window.open"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("popin"));
    }

    void testShader_emptyRules_returnsTreeResolution()
    {
        const auto tree = treeWithBaseline(QStringLiteral("popin"));
        const auto profile = resolveAnimationShaderProfile(AnimationAppRuleList{}, tree, QStringLiteral("Firefox"),
                                                           QStringLiteral("window.open"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("popin"));
    }

    void testShader_emptyEverything_returnsEmptyProfile()
    {
        const auto profile = resolveAnimationShaderProfile(AnimationAppRuleList{}, ShaderProfileTree{},
                                                           QStringLiteral("Firefox"), QStringLiteral("window.open"));
        // Tree resolves to empty when there's no baseline and no leaf — that
        // surfaces as engaged-nullopt fields, the documented "no shader"
        // baseline state. effectiveEffectId() returns the empty string.
        QVERIFY(profile.effectiveEffectId().isEmpty());
    }

    void testShader_timingRule_doesNotMatchShaderAxis()
    {
        // A Timing-kind rule for the same (class, event) must not be
        // surfaced as a shader hit — resolveShader filters by Kind.
        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), 800));
        const auto tree = treeWithBaseline(QStringLiteral("popin"));

        const auto profile =
            resolveAnimationShaderProfile(rules, tree, QStringLiteral("Firefox"), QStringLiteral("window.open"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("popin"));
    }

    void testShader_firstShaderRuleWins_ordering()
    {
        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("fire"), QStringLiteral("window.open"), QStringLiteral("first")));
        rules.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("second")));

        const auto profile = resolveAnimationShaderProfile(rules, ShaderProfileTree{}, QStringLiteral("Firefox"),
                                                           QStringLiteral("window.open"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("first"));
    }

    // ── Duration cascade ──────────────────────────────────────────────

    void testDuration_ruleHit_overridesDefault()
    {
        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), 800));
        QCOMPARE(resolveAnimationDuration(rules, QStringLiteral("Firefox"), QStringLiteral("window.open"), 200), 800);
    }

    void testDuration_ruleZero_isInheritSentinel()
    {
        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), 0));
        QCOMPARE(resolveAnimationDuration(rules, QStringLiteral("Firefox"), QStringLiteral("window.open"), 200), 200);
    }

    void testDuration_ruleNegative_isInheritSentinel()
    {
        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), -1));
        QCOMPARE(resolveAnimationDuration(rules, QStringLiteral("Firefox"), QStringLiteral("window.open"), 200), 200);
    }

    void testDuration_classMiss_returnsDefault()
    {
        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), 800));
        QCOMPARE(resolveAnimationDuration(rules, QStringLiteral("Spotify"), QStringLiteral("window.open"), 200), 200);
    }

    void testDuration_eventMiss_returnsDefault()
    {
        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), 800));
        QCOMPARE(resolveAnimationDuration(rules, QStringLiteral("Firefox"), QStringLiteral("window.close"), 200), 200);
    }

    void testDuration_emptyWindowClass_returnsDefault()
    {
        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), 800));
        QCOMPARE(resolveAnimationDuration(rules, QString(), QStringLiteral("window.open"), 200), 200);
    }

    void testDuration_shaderRule_doesNotMatchTimingAxis()
    {
        // A Shader-kind rule for the same (class, event) must not surface
        // through resolveTiming — kind filtering keeps the axes
        // independent.
        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        QCOMPARE(resolveAnimationDuration(rules, QStringLiteral("Firefox"), QStringLiteral("window.open"), 200), 200);
    }

    // ── Motion-profile cascade ────────────────────────────────────────

    void testMotionProfile_emptyRules_returnsBaseUnchanged()
    {
        CurveRegistry registry;
        Profile base;
        base.duration = 250.0;
        base.curve = registry.create(QStringLiteral("0.42,0.0,0.58,1.0"));

        const auto out = resolveAnimationMotionProfile(AnimationAppRuleList{}, base, QStringLiteral("Firefox"),
                                                       QStringLiteral("window.move"), registry);
        QCOMPARE(out, base);
    }

    void testMotionProfile_classMiss_returnsBaseUnchanged()
    {
        CurveRegistry registry;
        Profile base;
        base.duration = 250.0;
        base.curve = registry.create(QStringLiteral("0.42,0.0,0.58,1.0"));

        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.move"), 800,
                                QStringLiteral("0.0,0.0,1.0,1.0")));

        const auto out = resolveAnimationMotionProfile(rules, base, QStringLiteral("Spotify"),
                                                       QStringLiteral("window.move"), registry);
        QCOMPARE(out, base);
    }

    void testMotionProfile_emptyWindowClass_returnsBaseUnchanged()
    {
        CurveRegistry registry;
        Profile base;
        base.duration = 250.0;

        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.move"), 800));

        const auto out = resolveAnimationMotionProfile(rules, base, QString(), QStringLiteral("window.move"), registry);
        QCOMPARE(out, base);
    }

    void testMotionProfile_curveOverride_replacesBaseCurve()
    {
        CurveRegistry registry;
        Profile base;
        base.curve = registry.create(QStringLiteral("0.42,0.0,0.58,1.0"));
        const auto baseCurve = base.curve;
        QVERIFY(baseCurve != nullptr);

        AnimationAppRuleList rules;
        rules.append(
            timingRule(QStringLiteral("firefox"), QStringLiteral("window.move"), 0, QStringLiteral("0.0,0.0,1.0,1.0")));

        const auto out = resolveAnimationMotionProfile(rules, base, QStringLiteral("Firefox"),
                                                       QStringLiteral("window.move"), registry);
        QVERIFY(out.curve != nullptr);
        QVERIFY(out.curve != baseCurve);
    }

    void testMotionProfile_durationOverride_replacesBaseDuration()
    {
        CurveRegistry registry;
        Profile base;
        base.duration = 250.0;

        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.move"), 800));

        const auto out = resolveAnimationMotionProfile(rules, base, QStringLiteral("Firefox"),
                                                       QStringLiteral("window.move"), registry);
        QCOMPARE(out.effectiveDuration(), 800.0);
    }

    void testMotionProfile_zeroDuration_keepsBaseDuration()
    {
        CurveRegistry registry;
        Profile base;
        base.duration = 250.0;

        AnimationAppRuleList rules;
        // durationMs == 0 is the inherit sentinel.
        rules.append(
            timingRule(QStringLiteral("firefox"), QStringLiteral("window.move"), 0, QStringLiteral("0.0,0.0,1.0,1.0")));

        const auto out = resolveAnimationMotionProfile(rules, base, QStringLiteral("Firefox"),
                                                       QStringLiteral("window.move"), registry);
        QCOMPARE(out.effectiveDuration(), 250.0);
    }

    void testMotionProfile_emptyCurve_keepsBaseCurve()
    {
        CurveRegistry registry;
        Profile base;
        base.curve = registry.create(QStringLiteral("0.42,0.0,0.58,1.0"));
        const auto baseCurve = base.curve;

        AnimationAppRuleList rules;
        // Empty curve string = "use per-event default" sentinel; only
        // the duration override should apply.
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.move"), 800, QString()));

        const auto out = resolveAnimationMotionProfile(rules, base, QStringLiteral("Firefox"),
                                                       QStringLiteral("window.move"), registry);
        QCOMPARE(out.curve, baseCurve);
        QCOMPARE(out.effectiveDuration(), 800.0);
    }

    void testMotionProfile_malformedCurve_keepsBaseCurve()
    {
        // A typo in the rule's curve field must NOT silently swap the
        // user's configured global curve for OutCubic — tryCreate
        // returns nullptr on failure so the resolver keeps base.curve.
        CurveRegistry registry;
        Profile base;
        base.curve = registry.create(QStringLiteral("0.42,0.0,0.58,1.0"));
        const auto baseCurve = base.curve;
        base.duration = 250.0;

        AnimationAppRuleList rules;
        rules.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.move"), 0,
                                QStringLiteral("not-a-curve-spec")));

        const auto out = resolveAnimationMotionProfile(rules, base, QStringLiteral("Firefox"),
                                                       QStringLiteral("window.move"), registry);
        QCOMPARE(out.curve, baseCurve);
        QCOMPARE(out.effectiveDuration(), 250.0);
    }

    void testMotionProfile_shaderRule_doesNotMatchTimingAxis()
    {
        // A Shader-kind rule for the same (class, event) must not surface
        // through resolveTiming — keep the axes independent.
        CurveRegistry registry;
        Profile base;
        base.duration = 250.0;

        AnimationAppRuleList rules;
        rules.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.move"), QStringLiteral("dissolve")));

        const auto out = resolveAnimationMotionProfile(rules, base, QStringLiteral("Firefox"),
                                                       QStringLiteral("window.move"), registry);
        QCOMPARE(out, base);
    }
};

QTEST_MAIN(TestAnimationAppRuleResolver)
#include "test_animationappruleresolver.moc"
