// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorAnimation/AnimationAppRule.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using PhosphorAnimationShaders::AnimationAppRule;
using PhosphorAnimationShaders::AnimationAppRuleList;

class TestAnimationAppRule : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Single-rule round-trip ──────────────────────────────────────

    void testShaderRule_roundTrip_preservesEffectIdAndParams()
    {
        AnimationAppRule r;
        r.classPattern = QStringLiteral("firefox");
        r.eventPath = QStringLiteral("window.open");
        r.kind = AnimationAppRule::Kind::Shader;
        r.effectId = QStringLiteral("popin");
        r.shaderParams.insert(QStringLiteral("scaleFrom"), 0.8);
        r.shaderParams.insert(QStringLiteral("overshoot"), 0.1);

        const auto roundTripped = AnimationAppRule::fromJson(r.toJson());
        QVERIFY(roundTripped.has_value());
        QCOMPARE(*roundTripped, r);
    }

    void testTimingRule_roundTrip_preservesCurveAndDuration()
    {
        AnimationAppRule r;
        r.classPattern = QStringLiteral("Spotify");
        r.eventPath = QStringLiteral("window.minimize");
        r.kind = AnimationAppRule::Kind::Timing;
        r.curve = QStringLiteral("0.33,1.00,0.68,1.00");
        r.durationMs = 450;

        const auto roundTripped = AnimationAppRule::fromJson(r.toJson());
        QVERIFY(roundTripped.has_value());
        QCOMPARE(*roundTripped, r);
    }

    void testShaderRule_emptyEffectId_isPreserved()
    {
        // The engaged-blocking sentinel: empty effectId means "no
        // animation for this app." Must round-trip as an empty string,
        // not be silently dropped.
        AnimationAppRule r;
        r.classPattern = QStringLiteral("firefox");
        r.eventPath = QStringLiteral("window.open");
        r.kind = AnimationAppRule::Kind::Shader;
        r.effectId = QString();

        const auto json = r.toJson();
        QVERIFY(json.contains(QLatin1String("effectId")));
        QCOMPARE(json.value(QLatin1String("effectId")).toString(), QString());

        const auto roundTripped = AnimationAppRule::fromJson(json);
        QVERIFY(roundTripped.has_value());
        QCOMPARE(roundTripped->effectId, QString());
        QCOMPARE(*roundTripped, r);
    }

    void testShaderRule_emptyParams_omitsKeyButRoundTrips()
    {
        // Compactness: empty shaderParams shouldn't bloat the on-disk
        // JSON with an empty object. fromJson treats missing as empty.
        AnimationAppRule r;
        r.classPattern = QStringLiteral("dolphin");
        r.eventPath = QStringLiteral("window.close");
        r.kind = AnimationAppRule::Kind::Shader;
        r.effectId = QStringLiteral("dissolve");

        const auto json = r.toJson();
        QVERIFY(!json.contains(QLatin1String("shaderParams")));

        const auto roundTripped = AnimationAppRule::fromJson(json);
        QVERIFY(roundTripped.has_value());
        QCOMPARE(*roundTripped, r);
    }

    void testTimingRule_zeroDuration_omitsKey()
    {
        AnimationAppRule r;
        r.classPattern = QStringLiteral("foo");
        r.eventPath = QStringLiteral("window.open");
        r.kind = AnimationAppRule::Kind::Timing;
        r.curve = QStringLiteral("ease-out");
        r.durationMs = 0; // "use per-event default"

        const auto json = r.toJson();
        QVERIFY(!json.contains(QLatin1String("durationMs")));

        const auto roundTripped = AnimationAppRule::fromJson(json);
        QVERIFY(roundTripped.has_value());
        QCOMPARE(*roundTripped, r);
    }

    void testFromJson_strict_dropsUnknownKind()
    {
        // Direct callers of rule-level fromJson now get the same
        // strict drop-on-malformed contract the list-level loader has.
        QJsonObject obj{
            {QStringLiteral("classPattern"), QStringLiteral("firefox")},
            {QStringLiteral("eventPath"), QStringLiteral("window.open")},
            {QStringLiteral("kind"), QStringLiteral("not-a-kind")},
        };
        QVERIFY(!AnimationAppRule::fromJson(obj).has_value());
    }

    void testFromJson_strict_dropsEmptyClassPattern()
    {
        QJsonObject obj{
            {QStringLiteral("classPattern"), QString()},
            {QStringLiteral("eventPath"), QStringLiteral("window.open")},
            {QStringLiteral("kind"), QStringLiteral("shader")},
        };
        QVERIFY(!AnimationAppRule::fromJson(obj).has_value());
    }

    // ── List ordering and mutations ────────────────────────────────

    void testList_appendInsertsInOrder()
    {
        AnimationAppRuleList list;
        AnimationAppRule a;
        a.classPattern = QStringLiteral("firefox");
        a.eventPath = QStringLiteral("window.open");
        AnimationAppRule b;
        b.classPattern = QStringLiteral("dolphin");
        b.eventPath = QStringLiteral("window.open");

        list.append(a);
        list.append(b);

        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).classPattern, QStringLiteral("firefox"));
        QCOMPARE(list.at(1).classPattern, QStringLiteral("dolphin"));
    }

    void testList_appendRejectsEmptyPattern()
    {
        // Defence in depth: append() must refuse empty patterns so
        // they can't silently match every window.
        AnimationAppRuleList list;
        AnimationAppRule r;
        r.classPattern = QString();
        r.eventPath = QStringLiteral("window.open");
        list.append(r);
        QCOMPARE(list.size(), 0);
    }

    void testList_appendRejectsEmptyEventPath()
    {
        AnimationAppRuleList list;
        AnimationAppRule r;
        r.classPattern = QStringLiteral("firefox");
        r.eventPath = QString();
        list.append(r);
        QCOMPARE(list.size(), 0);
    }

    void testList_move_reordersInPlace()
    {
        AnimationAppRuleList list;
        AnimationAppRule a;
        a.classPattern = QStringLiteral("a");
        a.eventPath = QStringLiteral("window.open");
        AnimationAppRule b;
        b.classPattern = QStringLiteral("b");
        b.eventPath = QStringLiteral("window.open");
        AnimationAppRule c;
        c.classPattern = QStringLiteral("c");
        c.eventPath = QStringLiteral("window.open");
        list.append(a);
        list.append(b);
        list.append(c);

        list.move(0, 2);

        QCOMPARE(list.at(0).classPattern, QStringLiteral("b"));
        QCOMPARE(list.at(1).classPattern, QStringLiteral("c"));
        QCOMPARE(list.at(2).classPattern, QStringLiteral("a"));
    }

    void testList_move_outOfRange_isNoOp()
    {
        AnimationAppRuleList list;
        AnimationAppRule a;
        a.classPattern = QStringLiteral("a");
        a.eventPath = QStringLiteral("window.open");
        list.append(a);

        list.move(0, 5); // out of range
        list.move(-1, 0); // negative

        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).classPattern, QStringLiteral("a"));
    }

    // ── Resolver: first-match per axis, substring CI matching ──────

    void testResolveShader_substringCaseInsensitive()
    {
        AnimationAppRuleList list;
        AnimationAppRule r;
        r.classPattern = QStringLiteral("firefox");
        r.eventPath = QStringLiteral("window.open");
        r.kind = AnimationAppRule::Kind::Shader;
        r.effectId = QStringLiteral("popin");
        list.append(r);

        QVERIFY(list.resolveShader(QStringLiteral("FireFox"), QStringLiteral("window.open")).has_value());
        QVERIFY(list.resolveShader(QStringLiteral("org.mozilla.firefox"), QStringLiteral("window.open")).has_value());
        QVERIFY(!list.resolveShader(QStringLiteral("dolphin"), QStringLiteral("window.open")).has_value());
    }

    void testResolveShader_eventPathMustMatchExactly()
    {
        AnimationAppRuleList list;
        AnimationAppRule r;
        r.classPattern = QStringLiteral("firefox");
        r.eventPath = QStringLiteral("window.open");
        r.kind = AnimationAppRule::Kind::Shader;
        list.append(r);

        QVERIFY(list.resolveShader(QStringLiteral("firefox"), QStringLiteral("window.open")).has_value());
        QVERIFY(!list.resolveShader(QStringLiteral("firefox"), QStringLiteral("window.close")).has_value());
        QVERIFY(!list.resolveShader(QStringLiteral("firefox"), QStringLiteral("window")).has_value());
    }

    void testResolveShader_firstMatchWins()
    {
        AnimationAppRuleList list;
        AnimationAppRule first;
        first.classPattern = QStringLiteral("fox");
        first.eventPath = QStringLiteral("window.open");
        first.kind = AnimationAppRule::Kind::Shader;
        first.effectId = QStringLiteral("popin");
        AnimationAppRule second;
        second.classPattern = QStringLiteral("firefox");
        second.eventPath = QStringLiteral("window.open");
        second.kind = AnimationAppRule::Kind::Shader;
        second.effectId = QStringLiteral("dissolve");
        list.append(first);
        list.append(second);

        // "firefox" matches both patterns (substring "fox" and substring
        // "firefox"); first one wins.
        const auto resolved = list.resolveShader(QStringLiteral("firefox"), QStringLiteral("window.open"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->effectId, QStringLiteral("popin"));
    }

    void testResolveShader_skipsTimingKindRules()
    {
        // Timing rules in the list don't satisfy a shader resolution.
        AnimationAppRuleList list;
        AnimationAppRule timing;
        timing.classPattern = QStringLiteral("firefox");
        timing.eventPath = QStringLiteral("window.open");
        timing.kind = AnimationAppRule::Kind::Timing;
        timing.curve = QStringLiteral("ease-out");
        timing.durationMs = 200;
        list.append(timing);

        QVERIFY(!list.resolveShader(QStringLiteral("firefox"), QStringLiteral("window.open")).has_value());
    }

    void testResolveTiming_independentFromShaderResolve()
    {
        // Mixed-kind list: shader and timing rules for the same
        // (pattern, event) coexist; each resolver finds its own.
        AnimationAppRuleList list;
        AnimationAppRule shaderRule;
        shaderRule.classPattern = QStringLiteral("firefox");
        shaderRule.eventPath = QStringLiteral("window.open");
        shaderRule.kind = AnimationAppRule::Kind::Shader;
        shaderRule.effectId = QStringLiteral("popin");
        AnimationAppRule timingRule;
        timingRule.classPattern = QStringLiteral("firefox");
        timingRule.eventPath = QStringLiteral("window.open");
        timingRule.kind = AnimationAppRule::Kind::Timing;
        timingRule.durationMs = 500;
        list.append(shaderRule);
        list.append(timingRule);

        const auto shader = list.resolveShader(QStringLiteral("firefox"), QStringLiteral("window.open"));
        QVERIFY(shader.has_value());
        QCOMPARE(shader->effectId, QStringLiteral("popin"));

        const auto timing = list.resolveTiming(QStringLiteral("firefox"), QStringLiteral("window.open"));
        QVERIFY(timing.has_value());
        QCOMPARE(timing->durationMs, 500);
    }

    void testResolveShader_emptyEffectIdSentinel_returnsRule()
    {
        // Empty effectId is the engaged-blocking sentinel — the
        // resolver MUST return the rule (so the caller can apply
        // "no animation for this app") rather than skipping it as if
        // it were missing.
        AnimationAppRuleList list;
        AnimationAppRule r;
        r.classPattern = QStringLiteral("firefox");
        r.eventPath = QStringLiteral("window.open");
        r.kind = AnimationAppRule::Kind::Shader;
        r.effectId = QString();
        list.append(r);

        const auto resolved = list.resolveShader(QStringLiteral("firefox"), QStringLiteral("window.open"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->effectId, QString());
    }

    void testResolve_emptyInputs_returnNullopt()
    {
        AnimationAppRuleList list;
        AnimationAppRule r;
        r.classPattern = QStringLiteral("firefox");
        r.eventPath = QStringLiteral("window.open");
        list.append(r);

        QVERIFY(!list.resolveShader(QString(), QStringLiteral("window.open")).has_value());
        QVERIFY(!list.resolveShader(QStringLiteral("firefox"), QString()).has_value());
        QVERIFY(!list.resolveTiming(QString(), QStringLiteral("window.open")).has_value());
        QVERIFY(!list.resolveTiming(QStringLiteral("firefox"), QString()).has_value());
    }

    // ── List JSON round-trip + malformed-input handling ────────────

    void testListJson_roundTripPreservesOrder()
    {
        AnimationAppRuleList list;
        for (int i = 0; i < 3; ++i) {
            AnimationAppRule r;
            r.classPattern = QStringLiteral("app%1").arg(i);
            r.eventPath = QStringLiteral("window.open");
            r.kind = AnimationAppRule::Kind::Shader;
            r.effectId = QStringLiteral("effect%1").arg(i);
            list.append(r);
        }
        const auto roundTripped = AnimationAppRuleList::fromJson(list.toJson());
        QCOMPARE(roundTripped, list);
    }

    void testListJson_dropsMalformedRules()
    {
        // Each malformed entry below carries a valid `kind` so the
        // strict kind-whitelist gate at the top of fromJson lets it
        // through to the empty-pattern / empty-eventPath drop. Without
        // a valid kind, every entry would be dropped at the kind check
        // and we'd lose JSON-path coverage of the empty-string guard.
        QJsonArray arr;
        // Valid rule.
        arr.append(QJsonObject{
            {QStringLiteral("classPattern"), QStringLiteral("ok")},
            {QStringLiteral("eventPath"), QStringLiteral("window.open")},
            {QStringLiteral("kind"), QStringLiteral("shader")},
            {QStringLiteral("effectId"), QStringLiteral("popin")},
        });
        // Empty pattern — must be dropped at the empty-pattern guard.
        arr.append(QJsonObject{
            {QStringLiteral("classPattern"), QString()},
            {QStringLiteral("eventPath"), QStringLiteral("window.open")},
            {QStringLiteral("kind"), QStringLiteral("shader")},
        });
        // Empty event path — same drop reason.
        arr.append(QJsonObject{
            {QStringLiteral("classPattern"), QStringLiteral("foo")},
            {QStringLiteral("eventPath"), QString()},
            {QStringLiteral("kind"), QStringLiteral("shader")},
        });
        // Non-object — must be skipped at the isObject() guard.
        arr.append(QJsonValue(42));

        const auto list = AnimationAppRuleList::fromJson(arr);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).classPattern, QStringLiteral("ok"));
    }

    void testListJson_unknownKindStringIsDropped()
    {
        // Unknown / malformed kind strings are dropped rather than
        // silently coerced to Shader. A coerced typo would become an
        // engaged-blocking shader rule that disables animations for
        // matching windows without the user's consent — a quiet
        // user-data corruption mode that the strict drop avoids.
        QJsonArray arr;
        arr.append(QJsonObject{
            {QStringLiteral("classPattern"), QStringLiteral("foo")},
            {QStringLiteral("eventPath"), QStringLiteral("window.open")},
            {QStringLiteral("kind"), QStringLiteral("garbage")},
            {QStringLiteral("effectId"), QStringLiteral("popin")},
        });
        const auto list = AnimationAppRuleList::fromJson(arr);
        QCOMPARE(list.size(), 0);
    }

    void testListJson_missingKindIsDropped()
    {
        // A `kind` field that's entirely absent (e.g. pre-discriminator
        // JSON that no shipped daemon ever emits) is dropped at the
        // list-level loader along with engaged-but-unknown values.
        // `kindFromString("")` returns nullopt, the strict whitelist
        // rejects, and the rule is omitted — consistent with the
        // unknown-kind drop above. Pre-discriminator JSON is not a
        // documented input shape; this test pins the strict-validate
        // behaviour.
        QJsonArray arr;
        arr.append(QJsonObject{
            {QStringLiteral("classPattern"), QStringLiteral("foo")},
            {QStringLiteral("eventPath"), QStringLiteral("window.open")},
            {QStringLiteral("effectId"), QStringLiteral("popin")},
        });
        const auto list = AnimationAppRuleList::fromJson(arr);
        QCOMPARE(list.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAnimationAppRule)
#include "test_animationapprule.moc"
