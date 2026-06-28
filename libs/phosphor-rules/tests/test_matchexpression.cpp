// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/MatchExpression.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTest>
#include <QVariant>
#include <QVariantList>

using namespace PhosphorRules;
using PhosphorProtocol::WindowType;

namespace {

WindowQuery firefoxQuery()
{
    WindowQuery q;
    q.appId = QStringLiteral("org.mozilla.firefox");
    q.windowClass = QStringLiteral("firefox");
    q.title = QStringLiteral("Mozilla Firefox — Settings");
    q.pid = 1234;
    q.windowType = WindowType::Normal;
    q.isFullscreen = false;
    q.screenId = QStringLiteral("DP-2");
    q.virtualDesktop = 2;
    q.activity = QStringLiteral("{work}");
    return q;
}

} // namespace

class TestMatchExpression : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Catch-all ──

    void testDefaultExpression_isCatchAll()
    {
        MatchExpression expr;
        QVERIFY(expr.isCatchAll());
        QVERIFY(expr.evaluate(firefoxQuery()));
        // Catch-all matches a windowless context query too.
        WindowQuery ctx;
        ctx.screenId = QStringLiteral("HDMI-1");
        QVERIFY(expr.evaluate(ctx));
        QVERIFY(expr.isContextOnly());
    }

    void testEmptyAll_isAlwaysTrue()
    {
        const auto expr = MatchExpression::makeAll({});
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    void testEmptyAny_isAlwaysFalse()
    {
        const auto expr = MatchExpression::makeAny({});
        QVERIFY(!expr.evaluate(firefoxQuery()));
    }

    void testEmptyNone_isAlwaysTrue()
    {
        const auto expr = MatchExpression::makeNone({});
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    // ── String operators ──

    void testEquals_caseInsensitive()
    {
        const auto expr = MatchExpression::makeLeaf(Field::WindowClass, Operator::Equals, QStringLiteral("FireFox"));
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    void testContains()
    {
        const auto expr = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("fox"));
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    void testContains_emptyPatternNeverMatches()
    {
        const auto expr = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QString());
        QVERIFY(!expr.evaluate(firefoxQuery()));
    }

    void testStartsWith_endsWith()
    {
        const auto starts = MatchExpression::makeLeaf(Field::WindowClass, Operator::StartsWith, QStringLiteral("fire"));
        const auto ends = MatchExpression::makeLeaf(Field::WindowClass, Operator::EndsWith, QStringLiteral("fox"));
        QVERIFY(starts.evaluate(firefoxQuery()));
        QVERIFY(ends.evaluate(firefoxQuery()));
    }

    void testRegex()
    {
        const auto expr =
            MatchExpression::makeLeaf(Field::Title, Operator::Regex, QStringLiteral("Settings|Preferences"));
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    void testRegex_copyEvaluatesCorrectly()
    {
        const auto expr = MatchExpression::makeLeaf(Field::Title, Operator::Regex, QStringLiteral("Settings"));
        // Evaluate the original, then a copy; both must produce the same
        // result. This proves copy-correctness of the cached regex program —
        // it does NOT (and cannot, from outside) prove the program is
        // physically shared rather than duplicated.
        QVERIFY(expr.evaluate(firefoxQuery()));
        MatchExpression copy = expr;
        QVERIFY(copy.evaluate(firefoxQuery()));
    }

    void testAppIdMatches()
    {
        const auto expr = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox"));
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    // ── Absent window field ──

    void testAbsentWindowField_neverMatches()
    {
        WindowQuery ctx;
        ctx.screenId = QStringLiteral("DP-2");
        const auto expr = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox"));
        QVERIFY(!expr.evaluate(ctx));
    }

    // ── Numeric / bool / windowType ──

    void testNumericComparison()
    {
        const auto gt = MatchExpression::makeLeaf(Field::Pid, Operator::GreaterThan, 1000);
        const auto lt = MatchExpression::makeLeaf(Field::Pid, Operator::LessThan, 1000);
        const auto eq = MatchExpression::makeLeaf(Field::Pid, Operator::Equals, 1234);
        QVERIFY(gt.evaluate(firefoxQuery()));
        QVERIFY(!lt.evaluate(firefoxQuery()));
        QVERIFY(eq.evaluate(firefoxQuery()));
    }

    void testBoolField()
    {
        const auto isFalse = MatchExpression::makeLeaf(Field::IsFullscreen, Operator::Equals, false);
        QVERIFY(isFalse.evaluate(firefoxQuery()));
    }

    void testTransientNotificationFields()
    {
        WindowQuery q;
        q.windowClass = QStringLiteral("plasmashell");
        q.isTransient = true;
        q.isNotification = false;

        QVERIFY(MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, true).evaluate(q));
        QVERIFY(!MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, false).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::IsNotification, Operator::Equals, false).evaluate(q));
        // Absent on a query that never set the flag — inert, never matches.
        WindowQuery noFlags;
        noFlags.appId = QStringLiteral("firefox");
        QVERIFY(!MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, true).evaluate(noFlags));
    }

    void testWindowSizeFields()
    {
        WindowQuery q;
        q.appId = QStringLiteral("firefox");
        q.width = 250;
        q.height = 600;

        // "Smaller than 300px wide" — the min-size exclusion gate as a rule.
        QVERIFY(MatchExpression::makeLeaf(Field::Width, Operator::LessThan, 300).evaluate(q));
        QVERIFY(!MatchExpression::makeLeaf(Field::Height, Operator::LessThan, 300).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::Width, Operator::GreaterThan, 100).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::Width, Operator::Equals, 250).evaluate(q));
        // String/regex operators are invalid on a numeric field.
        QVERIFY(!MatchExpression::makeLeaf(Field::Width, Operator::Contains, QStringLiteral("25")).isValid());
    }

    // Leaf evaluation for the KWin-property / geometry / placement-state fields
    // added in this PR — confirms each routes through the right operator branch
    // (bool Equals-only, numeric GT/LT/Equals incl. a negative coordinate, string
    // ops) and that an absent field stays inert (the windowless-context contract).
    void testNewMatchFields_evaluateLeaf()
    {
        WindowQuery q;
        q.appId = QStringLiteral("firefox");
        q.positionX = 1920;
        q.positionY = -40; // monitor stacked above the primary
        q.isModal = true;
        q.keepAbove = false;
        q.captionNormal = QStringLiteral("Save As");
        q.zone = QStringLiteral("{a1b2c3d4-0000-0000-0000-000000000001}");
        q.isFloating = true;
        q.isSnapped = false;
        q.isTiled = true;

        // Numeric position fields (including a negative coordinate).
        QVERIFY(MatchExpression::makeLeaf(Field::PositionX, Operator::GreaterThan, 1000).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::PositionX, Operator::Equals, 1920).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::PositionY, Operator::LessThan, 0).evaluate(q));

        // Bool state / accessory / placement fields — only Equals is meaningful.
        QVERIFY(MatchExpression::makeLeaf(Field::IsModal, Operator::Equals, true).evaluate(q));
        QVERIFY(!MatchExpression::makeLeaf(Field::IsModal, Operator::Equals, false).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::KeepAbove, Operator::Equals, false).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::IsFloating, Operator::Equals, true).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::IsSnapped, Operator::Equals, false).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::IsTiled, Operator::Equals, true).evaluate(q));
        QVERIFY(!MatchExpression::makeLeaf(Field::IsTiled, Operator::Equals, false).evaluate(q));

        // String fields.
        QVERIFY(
            MatchExpression::makeLeaf(Field::CaptionNormal, Operator::Contains, QStringLiteral("Save")).evaluate(q));
        QVERIFY(MatchExpression::makeLeaf(Field::Zone, Operator::Equals,
                                          QStringLiteral("{a1b2c3d4-0000-0000-0000-000000000001}"))
                    .evaluate(q));

        // Absent field → predicate inert (false), even though appId is present.
        WindowQuery bare;
        bare.appId = QStringLiteral("firefox");
        QVERIFY(!MatchExpression::makeLeaf(Field::IsModal, Operator::Equals, true).evaluate(bare));
        QVERIFY(!MatchExpression::makeLeaf(Field::IsTiled, Operator::Equals, true).evaluate(bare));
        QVERIFY(!MatchExpression::makeLeaf(Field::PositionX, Operator::GreaterThan, 0).evaluate(bare));
        QVERIFY(!MatchExpression::makeLeaf(Field::Zone, Operator::Equals,
                                           QStringLiteral("{a1b2c3d4-0000-0000-0000-000000000001}"))
                     .evaluate(bare));
    }

    void testWindowTypeField()
    {
        const auto expr =
            MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, static_cast<int>(WindowType::Normal));
        QVERIFY(expr.evaluate(firefoxQuery()));
        const auto dialog =
            MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, static_cast<int>(WindowType::Dialog));
        QVERIFY(!dialog.evaluate(firefoxQuery()));
    }

    void testModeField_contextLeaf()
    {
        // Mode is a context field — always present. A `Mode Equals "tiling"`
        // leaf matches a query in tiling mode and nothing else.
        const auto tilingLeaf = MatchExpression::makeLeaf(Field::Mode, Operator::Equals, QStringLiteral("tiling"));

        WindowQuery tiling;
        tiling.mode = QStringLiteral("tiling");
        QVERIFY(tilingLeaf.evaluate(tiling));

        WindowQuery snapping;
        snapping.mode = QStringLiteral("snapping");
        QVERIFY(!tilingLeaf.evaluate(snapping));

        // A floating window carries no mode (empty token) — the leaf is inert.
        WindowQuery floating; // mode defaults to empty
        QVERIFY(!tilingLeaf.evaluate(floating));
    }

    // ── Composites ──

    void testAll_andSemantics()
    {
        const auto expr = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-2")),
        });
        QVERIFY(expr.evaluate(firefoxQuery()));

        const auto fails = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("HDMI-9")),
        });
        QVERIFY(!fails.evaluate(firefoxQuery()));
    }

    void testAny_orSemantics()
    {
        const auto expr = MatchExpression::makeAny({
            MatchExpression::makeLeaf(Field::WindowClass, Operator::Equals, QStringLiteral("chrome")),
            MatchExpression::makeLeaf(Field::WindowClass, Operator::Equals, QStringLiteral("firefox")),
        });
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    void testNone_notAnySemantics()
    {
        const auto expr = MatchExpression::makeNone({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("eDP-1")),
        });
        QVERIFY(expr.evaluate(firefoxQuery())); // not on eDP-1

        const auto blocks = MatchExpression::makeNone({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-2")),
        });
        QVERIFY(!blocks.evaluate(firefoxQuery())); // is on DP-2
    }

    void testNestedComposite()
    {
        // appId matches code AND (windowType==dialog OR title regex) AND NOT(screen eDP-1)
        const auto expr = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
            MatchExpression::makeAny({
                MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, static_cast<int>(WindowType::Dialog)),
                MatchExpression::makeLeaf(Field::Title, Operator::Regex, QStringLiteral("Settings")),
            }),
            MatchExpression::makeNone({
                MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("eDP-1")),
            }),
        });
        QVERIFY(expr.evaluate(firefoxQuery()));
    }

    // ── isContextOnly ──

    void testIsContextOnly()
    {
        const auto ctxLeaf = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-2"));
        QVERIFY(ctxLeaf.isContextOnly());

        const auto winLeaf = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox"));
        QVERIFY(!winLeaf.isContextOnly());

        const auto ctxComposite = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-2")),
            MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 2),
        });
        QVERIFY(ctxComposite.isContextOnly());

        const auto mixedComposite = MatchExpression::makeAll({ctxLeaf, winLeaf});
        QVERIFY(!mixedComposite.isContextOnly());
    }

    // ── referencesAnyField ──

    void testReferencesAnyField()
    {
        const QSet<Field> typeFields = {Field::IsTransient, Field::WindowType, Field::IsModal};

        // A class-only leaf references none of the type fields — this is the
        // "firefox → dissolve matches a tooltip by class" case the animation
        // filter must NOT treat as deliberate type targeting.
        const auto classLeaf =
            MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox"));
        QVERIFY(!classLeaf.referencesAnyField(typeFields));

        // A direct type-field leaf references the set.
        const auto transientLeaf = MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, true);
        QVERIFY(transientLeaf.referencesAnyField(typeFields));

        // A type predicate nested inside a composite — including under a
        // `none{}` negation, like the user's PiP rule's `none:[windowType==2]`
        // — still counts as referencing the field.
        const auto nestedAll = MatchExpression::makeAll({
            classLeaf,
            MatchExpression::makeNone(
                {MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, static_cast<int>(WindowType::Dialog))}),
        });
        QVERIFY(nestedAll.referencesAnyField(typeFields));

        // A composite of only non-type predicates references nothing in the set.
        const auto titleAndClass = MatchExpression::makeAll({
            classLeaf,
            MatchExpression::makeLeaf(Field::Title, Operator::Contains, QStringLiteral("Settings")),
        });
        QVERIFY(!titleAndClass.referencesAnyField(typeFields));

        // An empty composite references nothing.
        QVERIFY(!MatchExpression::makeAll({}).referencesAnyField(typeFields));

        // An empty field set matches nothing — even a direct type leaf.
        QVERIFY(!transientLeaf.referencesAnyField({}));

        // An unrelated field is not matched by a disjoint set.
        QVERIFY(!transientLeaf.referencesAnyField({Field::IsNotification}));
    }

    // ── Validation ──

    void testValidation_rejectsStringOpOnNumericField()
    {
        const auto expr = MatchExpression::makeLeaf(Field::Pid, Operator::Contains, QStringLiteral("12"));
        QVERIFY(!expr.isValid());
    }

    void testValidation_rejectsAppIdMatchesOnNonAppId()
    {
        const auto expr = MatchExpression::makeLeaf(Field::WindowClass, Operator::AppIdMatches, QStringLiteral("x"));
        QVERIFY(!expr.isValid());
    }

    void testValidation_rejectsNumericCompareOnString()
    {
        const auto expr = MatchExpression::makeLeaf(Field::Title, Operator::GreaterThan, 5);
        QVERIFY(!expr.isValid());
    }

    void testValidation_rejectsBadRegex()
    {
        const auto expr = MatchExpression::makeLeaf(Field::Title, Operator::Regex, QStringLiteral("[unclosed"));
        QVERIFY(!expr.isValid());
    }

    void testValidation_acceptsValidLeaf()
    {
        const auto expr = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox"));
        QVERIFY(expr.isValid());
    }

    // ── JSON round-trip ──

    void testJson_leafRoundTrip()
    {
        const auto expr = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox"));
        const auto reloaded = MatchExpression::fromJson(expr.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, expr);
    }

    void testJson_compositeRoundTrip()
    {
        const auto expr = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("code")),
            MatchExpression::makeAny({
                MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, static_cast<int>(WindowType::Dialog)),
                MatchExpression::makeLeaf(Field::Title, Operator::Regex, QStringLiteral("Settings")),
            }),
        });
        const auto reloaded = MatchExpression::fromJson(expr.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, expr);
    }

    void testJson_catchAllRoundTrip()
    {
        MatchExpression expr; // catch-all All{}
        const auto reloaded = MatchExpression::fromJson(expr.toJson());
        QVERIFY(reloaded.has_value());
        QVERIFY(reloaded->isCatchAll());
    }

    void testJson_rejectsUnknownField()
    {
        QJsonObject o;
        o.insert(QStringLiteral("field"), QStringLiteral("bogus"));
        o.insert(QStringLiteral("op"), QStringLiteral("equals"));
        o.insert(QStringLiteral("value"), QStringLiteral("x"));
        QVERIFY(!MatchExpression::fromJson(o).has_value());
    }

    void testJson_rejectsUnknownOperator()
    {
        QJsonObject o;
        o.insert(QStringLiteral("field"), QStringLiteral("appId"));
        o.insert(QStringLiteral("op"), QStringLiteral("bogus"));
        o.insert(QStringLiteral("value"), QStringLiteral("x"));
        QVERIFY(!MatchExpression::fromJson(o).has_value());
    }

    void testJson_rejectsMultipleCompositeKeys()
    {
        QJsonObject o;
        o.insert(QStringLiteral("all"), QJsonArray{});
        o.insert(QStringLiteral("any"), QJsonArray{});
        QVERIFY(!MatchExpression::fromJson(o).has_value());
    }

    void testJson_rejectsInvalidLeaf()
    {
        QJsonObject o;
        o.insert(QStringLiteral("field"), QStringLiteral("pid"));
        o.insert(QStringLiteral("op"), QStringLiteral("contains"));
        o.insert(QStringLiteral("value"), QStringLiteral("12"));
        QVERIFY(!MatchExpression::fromJson(o).has_value());
    }

    void testJson_rejectsPathologicallyDeepNesting()
    {
        // Build a tree of `all{all{all{…}}}` nested one level past the cap.
        // The leaf at the deepest position is a valid context-equality leaf
        // so the rejection is provably about depth, not leaf validity.
        QJsonObject leaf;
        leaf.insert(QStringLiteral("field"), QStringLiteral("screenId"));
        leaf.insert(QStringLiteral("op"), QStringLiteral("equals"));
        leaf.insert(QStringLiteral("value"), QStringLiteral("DP-1"));

        QJsonObject current = leaf;
        // The cap is kMaxParseDepth = 32; build depth (kMaxParseDepth + 1) by
        // wrapping the leaf 33 times. Each wrap adds one composite level —
        // the parser sees depth 0..33 and must refuse at depth > 32.
        for (int i = 0; i < MatchExpression::kMaxParseDepth + 1; ++i) {
            QJsonObject wrapper;
            QJsonArray arr;
            arr.append(current);
            wrapper.insert(QStringLiteral("all"), arr);
            current = wrapper;
        }
        QVERIFY(!MatchExpression::fromJson(current).has_value());
    }

    void testJson_acceptsExactlyAtDepthCap()
    {
        // Sanity: a tree exactly at the cap must still load — the cap is
        // strictly `> kMaxParseDepth`, never an off-by-one rejection of a
        // legitimately deep but legal tree.
        QJsonObject leaf;
        leaf.insert(QStringLiteral("field"), QStringLiteral("screenId"));
        leaf.insert(QStringLiteral("op"), QStringLiteral("equals"));
        leaf.insert(QStringLiteral("value"), QStringLiteral("DP-1"));
        QJsonObject current = leaf;
        // Build a tree whose deepest composite sits at exactly the cap depth.
        for (int i = 0; i < MatchExpression::kMaxParseDepth; ++i) {
            QJsonObject wrapper;
            QJsonArray arr;
            arr.append(current);
            wrapper.insert(QStringLiteral("all"), arr);
            current = wrapper;
        }
        QVERIFY(MatchExpression::fromJson(current).has_value());
    }
};

QTEST_GUILESS_MAIN(TestMatchExpression)
#include "test_matchexpression.moc"
