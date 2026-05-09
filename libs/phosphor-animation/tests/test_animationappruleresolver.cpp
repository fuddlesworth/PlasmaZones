// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorAnimation/AnimationAppRule.h>
#include <PhosphorAnimation/AnimationAppRuleResolver.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QTest>

using PhosphorAnimationShaders::AnimationAppRule;
using PhosphorAnimationShaders::AnimationAppRuleList;
using PhosphorAnimationShaders::resolveAnimationDuration;
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
};

QTEST_MAIN(TestAnimationAppRuleResolver)
#include "test_animationappruleresolver.moc"
