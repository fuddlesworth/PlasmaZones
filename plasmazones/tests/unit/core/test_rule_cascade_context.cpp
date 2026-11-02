// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_cascade_context.cpp
 * @brief Context-slot cascade proofs for the window-rule model.
 *
 * Split out from test_rule_cascade_fidelity.cpp. Where that suite pins the
 * engine-mode / layout assignment cascade, this one covers the non-assignment
 * context resolvers that share the same priority-wins, per-slot-composition
 * model: gaps (including the per-monitor override), orientation and
 * active-layout stamping (including the scrolling template's prefixed
 * ActiveLayout stamp), the screen shape (device-pixel size and aspect ratio:
 * the assignment it drives, every resolver's cache following a mode change,
 * the refusal of a negated shape leaf while the size is unknown, and a virtual
 * screen's fallback retry still asking about the virtual screen, for its shape
 * and its tiled-window count), autotile tiling params, scrolling context
 * params, and the exactContextEntry pair pinning the settings UI's
 * explicit-versus-resolved discriminator, including its deliberate blindness
 * to a rule's enabled flag.
 *
 * The overlay, lock, per-mode gap routing, negation-polarity and specificity
 * halves live in the sibling test_rule_cascade_overlay_lock.cpp, split off at
 * the overlay banner when this file passed the 1150-line ceiling. Both halves
 * share the harness in RuleCascadeFixture.h.
 */

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>
#include <limits>

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorZones/Zone.h>

#include "RuleCascadeFixture.h"

class TestRuleCascadeContext : public QObject, public RuleCascadeFixture
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Context-rule gap overrides ──────────────────────────────────────
    // Gaps are context-domain but, unlike engine-mode assignments, resolve
    // PER SLOT — so a zone-padding rule and a separate outer-gap rule on the
    // same context BOTH apply, and there is no engine-mode gate.

    void testContextGaps_perSlotComposition()
    {
        const auto intGapAction = [](QLatin1StringView type, int value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            return a;
        };
        const auto gapRule = [](const QString& name, int priority, const QString& screenId,
                                const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // Higher-priority rule sets ONLY zone padding; lower-priority rule sets
        // ONLY the outer gap. Different slots → both must apply (no shadowing),
        // and neither carries an engine-mode action.
        // A distinctive inner gap: 0 would be indistinguishable from a
        // default-constructed int if the slot were ever filled by accident.
        const PWR::Rule pad = gapRule(QStringLiteral("pad"), 400, QStringLiteral("DP-1"),
                                      {intGapAction(PWR::ActionType::SetInnerGap, 7)});
        const PWR::Rule gap = gapRule(QStringLiteral("gap"), 300, QStringLiteral("DP-1"),
                                      {intGapAction(PWR::ActionType::SetOuterGap, 12)});
        QVERIFY(f.store->setAllRules({pad, gap}));

        const PhosphorZones::ContextGapOverride resolved =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.innerGap.has_value());
        QCOMPARE(*resolved.innerGap, 7);
        QVERIFY(resolved.outerGap.has_value()); // separate slot — composes, not shadowed
        QCOMPARE(*resolved.outerGap, 12);
        QVERIFY(!resolved.usePerSideOuterGap.has_value());

        // An explicit ZERO is a real override, not an absent one: the optional
        // has to carry it through, or "no gaps on this screen" reads as unset
        // and the global default comes back instead.
        const PWR::Rule zeroPad = gapRule(QStringLiteral("zero-pad"), 400, QStringLiteral("DP-1"),
                                          {intGapAction(PWR::ActionType::SetInnerGap, 0)});
        QVERIFY(f.store->setAllRules({zeroPad, gap}));
        const PhosphorZones::ContextGapOverride zeroed =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(zeroed.innerGap.has_value());
        QCOMPARE(*zeroed.innerGap, 0);

        // A context the rules do not pin → no override (cascade falls through).
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());
    }

    // ─── ScreenOrientation is stamped onto context queries and gates rules ────
    // The orientation provider feeds "portrait" / "landscape" per screen; a rule
    // matching Field::ScreenOrientation must fire only on the screens the provider
    // reports that orientation for, proving the stamp reaches a non-assignment
    // (gap) resolver.
    void testContextOrientation_stampedAndGatesRule()
    {
        RegistryFixture f = makeRegistryFixture();
        // DP-1 is portrait, DP-2 is landscape; DP-3 has unknown geometry (nullopt).
        f.registry->setScreenOrientationProvider([](const QString& screenId) -> std::optional<QString> {
            if (screenId == QLatin1String("DP-1")) {
                return QStringLiteral("portrait");
            }
            if (screenId == QLatin1String("DP-2")) {
                return QStringLiteral("landscape");
            }
            return std::nullopt;
        });

        PWR::RuleAction gapAction;
        gapAction.type = QString(PWR::ActionType::SetInnerGap);
        gapAction.params.insert(QString(PWR::ActionParam::Value), 20);
        PWR::Rule r;
        r.id = QUuid::createUuid();
        r.name = QStringLiteral("portrait gap");
        r.enabled = true;
        r.priority = 400;
        r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenOrientation, PWR::Operator::Equals,
                                                 QStringLiteral("portrait"));
        r.actions = {gapAction};
        QVERIFY(f.store->setAllRules({r}));

        // Portrait screen → the orientation stamp matches → gap applies.
        const PhosphorZones::ContextGapOverride portrait =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(portrait.innerGap.has_value());
        QCOMPARE(*portrait.innerGap, 20);

        // Landscape screen → orientation token differs → rule inert.
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());
        // Unknown geometry → orientation empty → rule inert (no false match).
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-3"), 0, QString()).isEmpty());
    }

    // ─── Screen size drives the layout assignment and follows a mode change ───
    // The discussion #1145 case: one monitor whose split mode halves it, from a
    // full 32:9 mode to a 16:9 half, should get a different layout in each.
    // A ScreenAspectRatio rule must win the assignment over the monitor's plain
    // assignment rule through priority, and the cached assignment must follow a
    // live mode change with no rule-set revision bump (the size rides the key).
    void testContextScreenSize_drivesAssignmentAndFollowsModeChange()
    {
        // Declared BEFORE the fixture: the provider captures it by reference and
        // the registry holding that provider is torn down with the fixture, so
        // the size has to outlive it.
        std::optional<QSize> size = QSize(5120, 1440);
        RegistryFixture f = makeRegistryFixture();
        f.registry->setScreenPixelSizeProvider([&size](const QString& screenId) -> std::optional<QSize> {
            return screenId == QLatin1String("DP-1") ? size : std::nullopt;
        });

        const QString fullWidthLayout = QStringLiteral("{11111111-1111-1111-1111-111111111111}");
        const QString halfWidthLayout = QStringLiteral("{22222222-2222-2222-2222-222222222222}");
        const PWR::Rule monitorRule = shapeAssignmentRule(
            100, PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1")),
            fullWidthLayout);
        const PWR::Rule halfWidthRule = shapeAssignmentRule(
            500,
            PWR::MatchExpression::makeAll(
                {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1")),
                 PWR::MatchExpression::makeLeaf(PWR::Field::ScreenAspectRatio, PWR::Operator::Equals,
                                                QStringLiteral("16:9"))}),
            halfWidthLayout);
        QVERIFY(f.store->setAllRules({monitorRule, halfWidthRule}));
        const auto layoutOnDp1 = [&f]() {
            return f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).snappingLayout;
        };

        // Full width: 32:9, the aspect rule does not match, the monitor rule wins.
        QCOMPARE(layoutOnDp1(), fullWidthLayout);

        // The split mode halves the monitor. Same rule set, no revision bump:
        // only the size moved, and the cached verdict must follow it.
        size = QSize(2560, 1440);
        QCOMPARE(layoutOnDp1(), halfWidthLayout);

        // Unknown size, straight from the half-width verdict, so a stale cache
        // hit would answer the half-width layout here. The aspect ratio is
        // empty, the rule stays inert, and the monitor rule wins again.
        size = std::nullopt;
        QCOMPARE(layoutOnDp1(), fullWidthLayout);

        // Known again, then back to full width.
        size = QSize(2560, 1440);
        QCOMPARE(layoutOnDp1(), halfWidthLayout);
        size = QSize(5120, 1440);
        QCOMPARE(layoutOnDp1(), fullWidthLayout);

        // The numeric fields gate a non-assignment resolver too, through the
        // Int operators: a narrow-mode gap.
        PWR::Rule narrowGap = shapeContextRule(
            400, PWR::MatchExpression::makeLeaf(PWR::Field::ScreenWidth, PWR::Operator::LessThan, 3000),
            {shapeValueAction(PWR::ActionType::SetInnerGap, 7)});
        QVERIFY(f.store->setAllRules({narrowGap}));

        size = QSize(2560, 1440);
        const PhosphorZones::ContextGapOverride narrow =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(narrow.innerGap.has_value());
        QCOMPARE(*narrow.innerGap, 7);
        size = QSize(5120, 1440);
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString()).isEmpty());
        // A screen the provider does not know has no width, so the rule is inert.
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());

        // A HEIGHT-only change must reach the cache key too. Start from a
        // matching short screen, then grow only its height past the bound: a
        // key that folded the width alone would hand back the stale gap.
        PWR::Rule shortGap = shapeContextRule(
            400, PWR::MatchExpression::makeLeaf(PWR::Field::ScreenHeight, PWR::Operator::LessThan, 1300),
            {shapeValueAction(PWR::ActionType::SetInnerGap, 9)});
        QVERIFY(f.store->setAllRules({shortGap}));
        size = QSize(2560, 1080);
        const PhosphorZones::ContextGapOverride shortScreen =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(shortScreen.innerGap.has_value());
        QCOMPARE(*shortScreen.innerGap, 9);
        size = QSize(2560, 1440);
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString()).isEmpty());
        size = QSize(2560, 1080);
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString()).innerGap.has_value());
    }

    // ─── Every context resolver follows a size change ─────────────────────
    // One rule per resolver, all keyed on the same aspect leaf, then the size
    // is moved under them with no rule-set revision bump. The cached
    // resolvers (assignment, gap, lock, default assignment, OSD, drag selector
    // and overlay) can only follow through the size folded into their keys,
    // and the uncached tiling and scrolling param resolvers through their
    // per-call read. The steps include a HEIGHT-only change (2560x1440 to
    // 2560x1080), so a key folding the width alone would show up as a stale
    // verdict.
    void testContextScreenSize_everyResolverFollowsSizeChange()
    {
        std::optional<QSize> size = QSize(5120, 1440);
        RegistryFixture f = makeRegistryFixture();
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });
        f.registry->setScreenPixelSizeProvider([&size](const QString& screenId) -> std::optional<QSize> {
            return screenId == QLatin1String("DP-1") ? size : std::nullopt;
        });
        QVERIFY(f.store->setAllRules(oneShapeRulePerResolver(PWR::MatchExpression::makeLeaf(
            PWR::Field::ScreenAspectRatio, PWR::Operator::Equals, QStringLiteral("16:9")))));

        const struct
        {
            std::optional<QSize> size;
            bool applied;
            const char* label;
        } steps[] = {
            {QSize(5120, 1440), false, "32:9"},
            {QSize(2560, 1440), true, "16:9"},
            {QSize(2560, 1080), false, "height-only change to 21:9"},
            {QSize(2560, 1440), true, "16:9 again"},
            {std::nullopt, false, "unknown"},
            {QSize(1920, 1080), true, "another 16:9 mode"},
            {QSize(0, 1080), false, "zero width is unknown"},
            {QSize(1920, 1080), true, "16:9 after zero width"},
            {QSize(-1920, 1080), false, "negative width is unknown"},
        };
        for (const auto& step : steps) {
            size = step.size;
            verifyEveryResolver(f, step.applied, step.label);
            if (QTest::currentTestFailed()) {
                return;
            }
        }
    }

    // ─── A negated shape leaf waits for a known size ─────────────────────
    // While the size is unknown the width and height read back absent and the
    // aspect ratio empty, which makes a positive leaf false and would make a
    // NEGATED one match every screen. Every resolver must refuse a rule
    // negating a shape field the query cannot answer, and admit the same rule
    // once the size is known (where the negation is a legitimate "not 32:9").
    void testContextScreenSize_negatedLeafRefusedWhileSizeUnknown()
    {
        std::optional<QSize> size;
        RegistryFixture f = makeRegistryFixture();
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });
        f.registry->setScreenPixelSizeProvider([&size](const QString& screenId) -> std::optional<QSize> {
            return screenId == QLatin1String("DP-1") ? size : std::nullopt;
        });

        const PWR::MatchExpression notSuperUltrawide = PWR::MatchExpression::makeNone({PWR::MatchExpression::makeLeaf(
            PWR::Field::ScreenAspectRatio, PWR::Operator::Equals, QStringLiteral("32:9"))});
        QVERIFY(f.store->setAllRules(oneShapeRulePerResolver(notSuperUltrawide)));
        const struct
        {
            std::optional<QSize> size;
            bool applied;
            const char* label;
        } aspectSteps[] = {
            {std::nullopt, false, "unknown before any report"},
            {QSize(2560, 1440), true, "16:9 is not 32:9"},
            {QSize(5120, 1440), false, "32:9"},
            {std::nullopt, false, "unknown again"},
            {QSize(0, 1440), false, "zero width is unknown"},
            {QSize(2560, 1440), true, "16:9 after zero width"},
        };
        for (const auto& step : aspectSteps) {
            size = step.size;
            verifyEveryResolver(f, step.applied, step.label);
            if (QTest::currentTestFailed()) {
                return;
            }
        }

        // The numeric fields carry the same guard: "not wider than 3000".
        const PWR::MatchExpression notWide = PWR::MatchExpression::makeNone(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenWidth, PWR::Operator::GreaterThan, 3000)});
        QVERIFY(f.store->setAllRules(oneShapeRulePerResolver(notWide)));
        size = std::nullopt;
        verifyEveryResolver(f, false, "negated width, size unknown");
        if (QTest::currentTestFailed()) {
            return;
        }
        size = QSize(2560, 1440);
        verifyEveryResolver(f, true, "negated width, 2560 wide");
        if (QTest::currentTestFailed()) {
            return;
        }

        // And the orientation: unknown while neither the size nor the
        // orientation provider answers, admitted once either does.
        const PWR::MatchExpression notPortrait = PWR::MatchExpression::makeNone({PWR::MatchExpression::makeLeaf(
            PWR::Field::ScreenOrientation, PWR::Operator::Equals, QStringLiteral("portrait"))});
        QVERIFY(f.store->setAllRules(oneShapeRulePerResolver(notPortrait)));
        size = std::nullopt;
        verifyEveryResolver(f, false, "negated orientation, nothing known");
        if (QTest::currentTestFailed()) {
            return;
        }
        f.registry->setScreenOrientationProvider([](const QString& screenId) -> std::optional<QString> {
            return screenId == QLatin1String("DP-1") ? std::optional<QString>(QStringLiteral("landscape"))
                                                     : std::nullopt;
        });
        // No rule-set edit: the orientation token rides every key, so the
        // provider's first answer is a fresh entry rather than a cached refusal.
        verifyEveryResolver(f, true, "negated orientation, provider says landscape");
    }

    // ─── A known size decides the orientation ────────────────────────────
    // The orientation comes from the reported size whenever that size is
    // known, so a rotation reaches the rules as one change even if the
    // orientation provider (geometry-derived, and possibly a step behind)
    // disagrees. The provider answers only while the size is unknown, and a
    // non-positive size counts as unknown.
    void testContextScreenSize_knownSizeDecidesOrientation()
    {
        std::optional<QSize> size = QSize(1080, 1920);
        RegistryFixture f = makeRegistryFixture();
        f.registry->setScreenOrientationProvider([](const QString&) -> std::optional<QString> {
            return QStringLiteral("landscape");
        });
        f.registry->setScreenPixelSizeProvider([&size](const QString&) -> std::optional<QSize> {
            return size;
        });
        QVERIFY(f.store->setAllRules(
            {shapeContextRule(400,
                              PWR::MatchExpression::makeLeaf(PWR::Field::ScreenOrientation, PWR::Operator::Equals,
                                                             QStringLiteral("portrait")),
                              {shapeValueAction(PWR::ActionType::SetInnerGap, 11)})}));
        const auto portraitGap = [&f]() {
            return f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString()).innerGap.has_value();
        };

        // Taller than wide: portrait, whatever the provider says.
        QVERIFY(portraitGap());
        // A square screen is landscape, the same rule the daemon's provider uses.
        size = QSize(1920, 1920);
        QVERIFY(!portraitGap());
        // Unknown and non-positive sizes fall back to the provider's landscape.
        size = std::nullopt;
        QVERIFY(!portraitGap());
        size = QSize(0, 1920);
        QVERIFY(!portraitGap());
        size = QSize(1080, 1920);
        QVERIFY(portraitGap());
    }

    // ─── A virtual screen's fallback retry still asks about the virtual screen ─
    // A virtual screen with no assignment of its own inherits its physical
    // screen's through a fallback retry on the physical id. That retry swaps
    // the id rules are matched against, never the screen a shape condition
    // describes: a "32:9" rule with no screen pin must not apply to a 16:9
    // half of a 32:9 panel just because the retry ran on the panel's id, or
    // the layout would follow the panel while the gap and lock resolvers
    // (which have no retry) followed the half.
    void testContextScreenSize_virtualScreenRetryKeepsItsOwnShape()
    {
        const QString physical = QStringLiteral("DP-1");
        const QString virtualHalf = QStringLiteral("DP-1/vs:0");
        RegistryFixture f = makeRegistryFixture();
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });
        f.registry->setScreenPixelSizeProvider(
            [physical, virtualHalf](const QString& screenId) -> std::optional<QSize> {
                if (screenId == physical) {
                    return QSize(5120, 1440);
                }
                if (screenId == virtualHalf) {
                    return QSize(2560, 1440);
                }
                return std::nullopt;
            });

        const QString panelLayout = QStringLiteral("{33333333-3333-3333-3333-333333333333}");
        const QString halfLayout = QStringLiteral("{44444444-4444-4444-4444-444444444444}");
        const QString monitorLayout = QStringLiteral("{55555555-5555-5555-5555-555555555555}");
        // A super-ultrawide rule with no screen pin, at the top priority.
        const PWR::Rule panelRule =
            shapeAssignmentRule(600,
                                PWR::MatchExpression::makeLeaf(PWR::Field::ScreenAspectRatio, PWR::Operator::Equals,
                                                               QStringLiteral("32:9")),
                                panelLayout);
        QVERIFY(f.store->setAllRules({panelRule}));

        // Positive control: the panel itself is 32:9 and takes the rule.
        QCOMPARE(f.registry->assignmentEntryForScreen(physical, 0, QString()).snappingLayout, panelLayout);
        // The 16:9 half misses on its own id, retries on the physical id, and
        // must still be judged as 16:9: the default answers.
        QCOMPARE(f.registry->assignmentEntryForScreen(virtualHalf, 0, QString()).snappingLayout,
                 QStringLiteral("{provider-snap-default}"));
        QCOMPARE(f.registry->assignmentIdForScreen(virtualHalf, 0, QString()),
                 QStringLiteral("{provider-snap-default}"));

        // The retry still inherits what the physical screen PINS: a plain
        // monitor rule reaches the half, and a 16:9 rule pinned to the physical
        // id matches there too, because its shape leaf is asked about the half.
        const PWR::Rule monitorRule = shapeAssignmentRule(
            100, PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, physical), monitorLayout);
        QVERIFY(f.store->setAllRules({panelRule, monitorRule}));
        QCOMPARE(f.registry->assignmentEntryForScreen(virtualHalf, 0, QString()).snappingLayout, monitorLayout);
        QCOMPARE(f.registry->assignmentEntryForScreen(physical, 0, QString()).snappingLayout, panelLayout);

        const PWR::Rule pinnedHalfRule = shapeAssignmentRule(
            500,
            PWR::MatchExpression::makeAll(
                {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, physical),
                 PWR::MatchExpression::makeLeaf(PWR::Field::ScreenAspectRatio, PWR::Operator::Equals,
                                                QStringLiteral("16:9"))}),
            halfLayout);
        QVERIFY(f.store->setAllRules({panelRule, monitorRule, pinnedHalfRule}));
        QCOMPARE(f.registry->assignmentEntryForScreen(virtualHalf, 0, QString()).snappingLayout, halfLayout);
        QCOMPARE(f.registry->assignmentEntryForScreen(physical, 0, QString()).snappingLayout, panelLayout);
    }

    // ─── A virtual screen's fallback retry keeps its own tiled-window count ──
    // The count gets the shape's treatment: read once for the virtual screen and
    // handed to the retry on the physical id. On a split monitor the tiling
    // engines track the virtual screens, so the physical id has no count of its
    // own, and a retry that asked for it would see "unknown". A positive count
    // leaf would then never match and a negated one would match every count.
    // Every public entry point that runs the fallback chain is driven here:
    // the assignment entry and id, the rules-visible active layout, and
    // hasMatchingAssignmentRule, which is private and observed through
    // isContextActiveLayoutSuppressed in the closing phase.
    void testTiledWindowCount_virtualScreenRetryKeepsItsOwnCount()
    {
        const QString physical = QStringLiteral("DP-1");
        const QString virtualHalf = QStringLiteral("DP-1/vs:0");
        // Declared BEFORE the fixture: the providers capture them by reference
        // and the registry holding them is torn down with the fixture.
        std::optional<int> virtualCount = 3;
        bool suppressDefault = false;
        QStringList askedAbout;
        RegistryFixture f = makeRegistryFixture();
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });
        f.registry->setDefaultAssignmentSuppressedProvider([&suppressDefault]() {
            return suppressDefault;
        });
        f.registry->setTiledWindowCountProvider(
            [&virtualCount, &askedAbout, virtualHalf](const QString& screenId, int,
                                                      const QString&) -> std::optional<int> {
                askedAbout.append(screenId);
                return screenId == virtualHalf ? virtualCount : std::nullopt;
            });

        const QString countLayout = QStringLiteral("{99999999-9999-9999-9999-999999999999}");
        const PWR::MatchExpression pinnedToPhysical =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, physical);
        const PWR::MatchExpression moreThanOne =
            PWR::MatchExpression::makeLeaf(PWR::Field::TiledWindowCount, PWR::Operator::GreaterThan, 1);
        const PWR::MatchExpression atMostOne = PWR::MatchExpression::makeNone({moreThanOne});

        // A positive count rule pinned to the physical id: the half has three
        // tiled windows, so the retry must match on the half's count.
        QVERIFY(f.store->setAllRules(
            {shapeAssignmentRule(500, PWR::MatchExpression::makeAll({pinnedToPhysical, moreThanOne}), countLayout)}));
        QCOMPARE(f.registry->assignmentEntryForScreen(virtualHalf, 0, QString()).snappingLayout, countLayout);
        QCOMPARE(f.registry->assignmentIdForScreen(virtualHalf, 0, QString()), countLayout);
        QCOMPARE(f.registry->rulesVisibleActiveLayoutId(virtualHalf, 0, QString()), countLayout);
        QVERIFY2(!askedAbout.contains(physical), qPrintable(askedAbout.join(QLatin1Char(','))));

        // Each public entry point reads the count once and threads that one
        // reading through its retries and its inner resolvers. A layer that
        // read it again would ask twice here, and on a retry would ask about
        // the physical id.
        const auto asksOnce = [&askedAbout, virtualHalf](const auto& call) {
            askedAbout.clear();
            call();
            return askedAbout == QStringList{virtualHalf};
        };
        QVERIFY(asksOnce([&f, virtualHalf]() {
            f.registry->assignmentEntryForScreen(virtualHalf, 0, QString());
        }));
        QVERIFY(asksOnce([&f, virtualHalf]() {
            f.registry->assignmentIdForScreen(virtualHalf, 0, QString());
        }));
        QVERIFY(asksOnce([&f, virtualHalf]() {
            f.registry->rulesVisibleActiveLayoutId(virtualHalf, 0, QString());
        }));

        // The negated form: "at most one tiled window". Three is more than one,
        // so the rule must not match, and the default answers. A count read for
        // the physical id would be unknown, and the negation would match.
        QVERIFY(f.store->setAllRules(
            {shapeAssignmentRule(500, PWR::MatchExpression::makeAll({pinnedToPhysical, atMostOne}), countLayout)}));
        QCOMPARE(f.registry->assignmentEntryForScreen(virtualHalf, 0, QString()).snappingLayout,
                 QStringLiteral("{provider-snap-default}"));
        QCOMPARE(f.registry->assignmentIdForScreen(virtualHalf, 0, QString()),
                 QStringLiteral("{provider-snap-default}"));
        QCOMPARE(f.registry->rulesVisibleActiveLayoutId(virtualHalf, 0, QString()),
                 QStringLiteral("{provider-snap-default}"));

        // Positive control for the negated rule: with one tiled window it
        // matches, through the same retry, with no rule-set revision bump.
        virtualCount = 1;
        QCOMPARE(f.registry->assignmentEntryForScreen(virtualHalf, 0, QString()).snappingLayout, countLayout);
        QCOMPARE(f.registry->rulesVisibleActiveLayoutId(virtualHalf, 0, QString()), countLayout);
        QVERIFY2(!askedAbout.contains(physical), qPrintable(askedAbout.join(QLatin1Char(','))));

        // hasMatchingAssignmentRule. With the default suppressed, an
        // algorithm-only rule yields no active layout id, so
        // isContextActiveLayoutSuppressed falls through to asking whether any
        // assignment rule matches: a match keeps the context live, a miss
        // leaves it suppressed. The rule is pinned to the physical id, so only
        // the retry can match it.
        suppressDefault = true;
        const auto algorithmOnlyRule = [](const PWR::MatchExpression& match) {
            PWR::RuleAction algorithm;
            algorithm.type = QString(PWR::ActionType::SetTilingAlgorithm);
            algorithm.params.insert(QString(PWR::ActionParam::Algorithm), QStringLiteral("bsp"));
            return shapeContextRule(500, match, {algorithm});
        };
        QVERIFY(
            f.store->setAllRules({algorithmOnlyRule(PWR::MatchExpression::makeAll({pinnedToPhysical, moreThanOne}))}));
        virtualCount = 3;
        QCOMPARE(f.registry->assignmentIdForScreen(virtualHalf, 0, QString()), QString());
        QVERIFY(!f.registry->isContextActiveLayoutSuppressed(virtualHalf, 0, QString()));
        virtualCount = 1;
        QVERIFY(f.registry->isContextActiveLayoutSuppressed(virtualHalf, 0, QString()));

        QVERIFY(
            f.store->setAllRules({algorithmOnlyRule(PWR::MatchExpression::makeAll({pinnedToPhysical, atMostOne}))}));
        virtualCount = 3;
        QVERIFY(f.registry->isContextActiveLayoutSuppressed(virtualHalf, 0, QString()));
        virtualCount = 1;
        QVERIFY(!f.registry->isContextActiveLayoutSuppressed(virtualHalf, 0, QString()));
        QVERIFY2(!askedAbout.contains(physical), qPrintable(askedAbout.join(QLatin1Char(','))));
    }

    // ─── A connector-name caller is asked about once, and a VS id as given ──
    // tiledWindowCount asks the provider again under the stable identifier when
    // a connector name gets no answer, because the tiling engines track screens
    // by stable id. A connector with no distinct stable form (the offscreen
    // platform's screens have no EDID, so idForName hands the name back) must
    // not be asked twice, and a virtual-screen id, which is never a connector
    // name, must never be rewritten.
    void testTiledWindowCount_unknownScreenIsAskedOnlyAsGiven()
    {
        QStringList askedAbout;
        RegistryFixture f = makeRegistryFixture();
        f.registry->setTiledWindowCountProvider(
            [&askedAbout](const QString& screenId, int, const QString&) -> std::optional<int> {
                askedAbout.append(screenId);
                return std::nullopt;
            });

        const QString connector = QStringLiteral("NO-SUCH-OUTPUT");
        QCOMPARE(PhosphorScreens::ScreenIdentity::idForName(connector), connector);
        f.registry->assignmentIdForScreen(connector, 0, QString());
        QCOMPARE(askedAbout, QStringList{connector});

        askedAbout.clear();
        const QString virtualId = QStringLiteral("NO-SUCH-OUTPUT/vs:0");
        f.registry->assignmentIdForScreen(virtualId, 0, QString());
        QCOMPARE(askedAbout, QStringList{virtualId});
    }

    // ─── A desktop-pinned assignment and a shape rule compete on priority ──
    // A shape rule is a non-dimension context rule: it carries no desktop and
    // wins or loses against a desktop-pinned assignment purely on priority,
    // in both orders. Without both orders a resolver that always preferred
    // the narrower context (or always the shape rule) would pass.
    void testContextScreenSize_desktopPinVersusShapeRuleByPriority()
    {
        RegistryFixture f = makeRegistryFixture();
        f.registry->setScreenPixelSizeProvider([](const QString&) -> std::optional<QSize> {
            return QSize(2560, 1440);
        });
        const QString desktopLayout = QStringLiteral("{66666666-6666-6666-6666-666666666666}");
        const QString shapeLayout = QStringLiteral("{77777777-7777-7777-7777-777777777777}");
        const PWR::MatchExpression sixteenByNine = PWR::MatchExpression::makeLeaf(
            PWR::Field::ScreenAspectRatio, PWR::Operator::Equals, QStringLiteral("16:9"));
        const auto desktopRule = [&desktopLayout](int priority) {
            return CRB::makeAssignmentRule(QString(), QStringLiteral("DP-1"), 2, QString(), QStringLiteral("snapping"),
                                           desktopLayout, QString(), priority, QString());
        };

        // Desktop pin above the shape rule: desktop 2 takes the pin, desktop 1
        // (no pin) takes the shape rule.
        QVERIFY(f.store->setAllRules({desktopRule(500), shapeAssignmentRule(400, sixteenByNine, shapeLayout)}));
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 2, QString()).snappingLayout,
                 desktopLayout);
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 1, QString()).snappingLayout,
                 shapeLayout);

        // Shape rule above the desktop pin: it wins on desktop 2 as well.
        QVERIFY(f.store->setAllRules({desktopRule(400), shapeAssignmentRule(500, sixteenByNine, shapeLayout)}));
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 2, QString()).snappingLayout,
                 shapeLayout);
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 1, QString()).snappingLayout,
                 shapeLayout);
    }

    // ─── ActiveLayout is stamped onto the non-assignment resolvers, gates rules,
    //     and does NOT recurse ──────────────────────────────────────────────────
    // The gap/lock/overlay resolvers stamp the screen's resolved active-layout id
    // (via assignmentIdForScreen). A rule matching Field::ActiveLayout must fire
    // only when that id matches — and the resolver must NOT recurse (reaching
    // assignmentIdForScreen must never re-enter the gap resolver). Unbounded
    // recursion here would hang or blow the stack, so the test completing is
    // evidence against that specific failure. It says nothing about a bounded
    // re-entry, which is not observable from here.
    void testContextActiveLayout_stampedAndGatesRule()
    {
        RegistryFixture f = makeRegistryFixture();
        // No per-screen assignment rule; the active layout comes from the global
        // default provider (exercising the non-rule-set input path that the cache
        // key must fold in).
        const QString layoutId = QStringLiteral("{11111111-1111-1111-1111-111111111111}");
        f.registry->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });

        const auto gapRuleForLayout = [](const QString& id) {
            PWR::RuleAction gapAction;
            gapAction.type = QString(PWR::ActionType::SetInnerGap);
            gapAction.params.insert(QString(PWR::ActionParam::Value), 15);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("active-layout gap");
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, id);
            r.actions = {gapAction};
            return r;
        };

        // The screen's active layout (from the default provider) matches → gap fires.
        QVERIFY(f.store->setAllRules({gapRuleForLayout(layoutId)}));
        const PhosphorZones::ContextGapOverride resolved =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.innerGap.has_value());
        QCOMPARE(*resolved.innerGap, 15);

        // A rule pinned to a DIFFERENT layout id is inert on this screen.
        QVERIFY(f.store->setAllRules({gapRuleForLayout(QStringLiteral("{22222222-2222-2222-2222-222222222222}"))}));
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString()).isEmpty());
    }

    // ─── The scrolling TEMPLATE rides the ActiveLayout stamp, prefixed ────────
    // A Scrolling context stamps "scrolling:<templateUuid>" (parity with
    // autotile's "autotile:<algo>"), so a rule can target one specific
    // template; the bare sentinel now means "scrolling with no template".
    void testContextActiveLayout_scrollingTemplateStamp()
    {
        // The store is declared BEFORE the fixture so it outlives the registry
        // that borrows it: locals are destroyed in reverse, and a registry torn
        // down while still holding a pointer into a dead store is a dangling
        // read in whatever the destructor touches.
        PhosphorZones::ScrollingTemplateStore store;
        RegistryFixture f = makeRegistryFixture();
        f.registry->setScrollingTemplateStore(&store);
        PhosphorZones::ScrollingTemplate templ;
        templ.name = QStringLiteral("Template");
        templ.presetColumnWidths = {0.5};
        const QString templId = store.saveTemplate(templ).toString();

        const auto gapRuleForActiveLayout = [](const QString& id) {
            PWR::RuleAction gapAction;
            gapAction.type = QString(PWR::ActionType::SetInnerGap);
            gapAction.params.insert(QString(PWR::ActionParam::Value), 21);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("template gap");
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, id);
            r.actions = {gapAction};
            return r;
        };
        const auto innerGapOn = [&f](const QString& screenId) {
            return f.registry->resolveContextGaps(screenId, 0, QString());
        };

        // Scrolling context, no template yet: the BARE sentinel matches.
        // The gap rules are added/removed individually — the ASSIGNMENT lives
        // in the same rule store, so a setAllRules would wipe it.
        f.registry->assignLayoutById(QStringLiteral("DP-1"), 0, QString(),
                                     QString(PhosphorLayout::LayoutId::ScrollingId));
        const PWR::Rule bareRule = gapRuleForActiveLayout(QString(PhosphorLayout::LayoutId::ScrollingId));
        QVERIFY(f.store->addRule(bareRule));
        const PhosphorZones::ContextGapOverride onBare = innerGapOn(QStringLiteral("DP-1"));
        QVERIFY(onBare.innerGap.has_value());
        QCOMPARE(*onBare.innerGap, 21);

        // Assign the template: the stamp becomes the PREFIXED form — the bare
        // rule stops matching, the prefixed rule fires. Every write here goes
        // through the rule store, so the gap cache's revision compare
        // refreshes it on its own.
        f.registry->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), templId);
        QVERIFY(!innerGapOn(QStringLiteral("DP-1")).innerGap.has_value());
        QVERIFY(f.store->removeRule(bareRule.id));
        const PWR::Rule prefixedRule = gapRuleForActiveLayout(PhosphorLayout::LayoutId::makeScrollingId(templId));
        QVERIFY(f.store->addRule(prefixedRule));
        const PhosphorZones::ContextGapOverride onTemplate = innerGapOn(QStringLiteral("DP-1"));
        QVERIFY(onTemplate.innerGap.has_value());
        QCOMPARE(*onTemplate.innerGap, 21);

        // Clearing the template restores the bare stamp: the prefixed rule
        // goes inert again.
        f.registry->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), QString());
        QVERIFY(!innerGapOn(QStringLiteral("DP-1")).innerGap.has_value());

        // Inertness alone would also hold if the stamp had gone empty rather
        // than back to the bare sentinel, so re-add the bare rule and require
        // it to fire again. This is what pins the revert to the SENTINEL.
        QVERIFY(f.store->addRule(bareRule));
        const PhosphorZones::ContextGapOverride onCleared = innerGapOn(QStringLiteral("DP-1"));
        QVERIFY(onCleared.innerGap.has_value());
        QCOMPARE(*onCleared.innerGap, 21);
    }

    // ─── An ActiveLayout-referencing rule must NOT drive the assignment ───────
    // The assignment resolver leaves Field::ActiveLayout unstamped (it IS the
    // resolver's output — stamping would recurse). An assignment rule whose match
    // references ActiveLayout is therefore structurally excluded from the
    // assignment path. This matters most for a NEGATED predicate: a positive
    // `ActiveLayout Equals X` leaf never matches the unstamped placeholder, but a
    // `None{ActiveLayout Equals X}` ("active layout is NOT X") would spuriously
    // match the empty placeholder and force a wrong assignment without the guard.
    // The resolve completing is likewise evidence only against UNBOUNDED
    // recursion (which would hang or blow the stack), not against re-entry.
    void testActiveLayoutRuleExcludedFromAssignment()
    {
        RegistryFixture f = makeRegistryFixture();
        // Snap default so a leaked autotile assignment is unambiguously visible.
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        // Positive control: an assignment rule pinned by ScreenId DOES drive the
        // assignment — proving the harness observes assignment-driving, so the
        // negative assertions below are meaningful (not vacuously always-Snapping).
        PWR::Rule screenPinned;
        screenPinned.id = QUuid::createUuid();
        screenPinned.name = QStringLiteral("screen-pinned-autotile");
        screenPinned.enabled = true;
        screenPinned.priority = 500;
        screenPinned.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        screenPinned.actions =
            CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QStringLiteral("dwindle"), QString());
        QVERIFY(f.store->setAllRules({screenPinned}));
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).mode,
                 PhosphorZones::AssignmentEntry::Autotile);

        // Build the same autotile assignment rule but match on ActiveLayout, once
        // positively and once negated. Neither may drive the assignment: the
        // resolver must fall through to the Snapping gated default.
        const QString someLayout = QStringLiteral("{11111111-1111-1111-1111-111111111111}");
        const auto activeLayoutAssignmentRule = [&](const PWR::MatchExpression& match) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("active-layout assignment");
            r.enabled = true;
            r.priority = 500;
            r.match = match;
            r.actions =
                CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QStringLiteral("dwindle"), QString());
            return r;
        };

        // Positive ActiveLayout leaf — excluded (also never matched the placeholder).
        QVERIFY(f.store->setAllRules({activeLayoutAssignmentRule(
            PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout))}));
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).mode,
                 PhosphorZones::AssignmentEntry::Snapping);

        // Negated ActiveLayout predicate — the regression case. Without the
        // referencesAnyField guard this None{} matches the empty placeholder and
        // forces Autotile; with it, the rule is excluded and Snapping stands.
        QVERIFY(f.store->setAllRules({activeLayoutAssignmentRule(PWR::MatchExpression::makeNone(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout)}))}));
        const PhosphorZones::AssignmentEntry negatedEntry =
            f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString());
        QCOMPARE(negatedEntry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(negatedEntry.snappingLayout, QStringLiteral("{provider-snap-default}"));
    }

    // ─── ActiveLayout exclusion on the SECOND no-stamp resolver ──────────────
    // resolveContextDefaultAssignment is the other assignment-cascade resolver
    // that leaves ActiveLayout unstamped, so it applies the same referencesAnyField
    // exclusion. A DefaultLayoutAssignment rule matched by None{ActiveLayout Equals
    // X} would, without the guard, match the empty placeholder and force the default
    // through (defeating the global suppress); with the guard it is excluded.
    void testActiveLayoutRuleExcludedFromDefaultAssignment()
    {
        RegistryFixture f = makeRegistryFixture();
        // Global default: suppress the synthesized default assignment everywhere.
        f.registry->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });

        const auto defaultAssignmentRule = [](const PWR::MatchExpression& match, bool allow) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("default-assignment");
            r.enabled = true;
            r.priority = 500;
            r.match = match;
            PWR::RuleAction action;
            action.type = QString(PWR::ActionType::DefaultLayoutAssignment);
            action.params.insert(QString(PWR::ActionParam::Value), allow);
            r.actions.append(action);
            return r;
        };

        // Positive control: a ScreenId-pinned allow rule DOES override the global
        // suppress (proves the harness observes default-assignment overrides, so the
        // negatives below are meaningful rather than vacuously always-suppressed).
        QVERIFY(f.store->setAllRules({defaultAssignmentRule(
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1")),
            true)}));
        QVERIFY(!f.registry->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 0, QString()));

        const QString someLayout = QStringLiteral("{11111111-1111-1111-1111-111111111111}");

        // Negated ActiveLayout predicate — the regression case. Without the guard the
        // None{} matches the empty placeholder and forces the default through
        // (isSuppressed → false); with it, the rule is excluded and the global
        // suppress stands (isSuppressed → true).
        QVERIFY(f.store->setAllRules(
            {defaultAssignmentRule(PWR::MatchExpression::makeNone({PWR::MatchExpression::makeLeaf(
                                       PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout)}),
                                   true)}));
        QVERIFY(f.registry->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 0, QString()));

        // Positive ActiveLayout leaf — also excluded (and never matched the placeholder).
        QVERIFY(f.store->setAllRules({defaultAssignmentRule(
            PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout), true)}));
        QVERIFY(f.registry->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 0, QString()));
    }

    // ─── exactContextEntry discriminates EXPLICIT from RESOLVED ──────────────
    // exactContextEntry is the settings UI's explicit-vs-resolved discriminator:
    // it reports only what THIS exact (screen, desktop, activity) tuple has a
    // rule for, and never what the tuple merely inherits. The Monitors page
    // relies on the distinction — re-pinning a cascade default as if it were
    // explicit would freeze an inherited value into a rule the user never
    // authored. The cascade resolver (assignmentEntryForScreen) is the foil in
    // each case below: it answers, and exactContextEntry must not.
    void testExactContextEntry_explicitOnly_notCascadeOrDefault()
    {
        RegistryFixture f = makeRegistryFixture();
        // A global snap default, so every unpinned context still RESOLVES to
        // something — otherwise the negative assertions would pass vacuously.
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        // One explicit context rule on the MONITOR axis: (DP-1, no desktop, no
        // activity), so its match is a bare ScreenId leaf that every desktop on
        // DP-1 inherits.
        const PWR::Rule assign = CRB::makeAssignmentRule(
            QStringLiteral("layout DP-1"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"),
            QStringLiteral("{explicit-layout}"), QString(), 301, QString());
        QVERIFY(f.store->setAllRules({assign}));

        // (1) The exact tuple → the stored entry comes back.
        const PhosphorZones::AssignmentEntry exact =
            f.registry->exactContextEntry(QStringLiteral("DP-1"), 0, QString());
        QCOMPARE(exact.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(exact.snappingLayout, QStringLiteral("{explicit-layout}"));
        QVERIFY(f.registry->hasExplicitAssignment(QStringLiteral("DP-1"), 0, QString()));

        // (2) A tuple that only INHERITS the value through the cascade. Desktop
        // 5 on the same screen resolves to the monitor rule's layout, but
        // nothing is stored AT (DP-1, 5, "") — the canonical desktop-axis shape
        // that tuple names is a different match than the bare ScreenId leaf —
        // so exactContextEntry must report an empty entry.
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 5, QString()).snappingLayout,
                 QStringLiteral("{explicit-layout}"));
        const PhosphorZones::AssignmentEntry inherited =
            f.registry->exactContextEntry(QStringLiteral("DP-1"), 5, QString());
        QVERIFY(!inherited.isValid());
        QVERIFY(inherited.snappingLayout.isEmpty());
        QVERIFY(inherited.tilingAlgorithm.isEmpty());
        QVERIFY(!f.registry->hasExplicitAssignment(QStringLiteral("DP-1"), 5, QString()));

        // (3) A tuple that resolves only through the global DEFAULT tier — a
        // different screen entirely. The resolver answers with the provider's
        // id; exactContextEntry stays empty.
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 0, QString()).snappingLayout,
                 QStringLiteral("{provider-snap-default}"));
        const PhosphorZones::AssignmentEntry defaulted =
            f.registry->exactContextEntry(QStringLiteral("DP-2"), 0, QString());
        QVERIFY(!defaulted.isValid());
        QVERIFY(defaulted.snappingLayout.isEmpty());
        QVERIFY(!f.registry->hasExplicitAssignment(QStringLiteral("DP-2"), 0, QString()));
    }

    // exactContextEntry is deliberately BLIND to the rule's enabled flag: it
    // reports stored intent, not the effective cascade result, because the
    // settings UI must keep rendering a pin the user switched off. The cascade
    // resolver is the opposite — the evaluator skips disabled rules — so the
    // two must DISAGREE for a disabled pin. That disagreement is the contract.
    void testExactContextEntry_disabledRuleStillReportsStoredEntry()
    {
        RegistryFixture f = makeRegistryFixture();
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        PWR::Rule assign = CRB::makeAssignmentRule(QStringLiteral("layout DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                                   QStringLiteral("snapping"), QStringLiteral("{explicit-layout}"),
                                                   QString(), 301, QString());
        assign.enabled = false;
        QVERIFY(f.store->setAllRules({assign}));

        // Stored intent survives the disable.
        const PhosphorZones::AssignmentEntry exact =
            f.registry->exactContextEntry(QStringLiteral("DP-1"), 0, QString());
        QCOMPARE(exact.snappingLayout, QStringLiteral("{explicit-layout}"));

        // The cascade does not: the disabled rule is skipped and the global
        // default tier answers instead.
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).snappingLayout,
                 QStringLiteral("{provider-snap-default}"));
    }

    // ─── Context autotile-parameter resolution (max / split / master) ────────
    // resolveContextTilingParams is a per-slot read: independent
    // SetMaxWindows / SetSplitRatio / SetMasterCount rules compose, and an
    // unpinned screen resolves to an all-unset (empty) params struct.
    void testContextTilingParams_perSlotComposition()
    {
        const auto valueAction = [](QLatin1StringView type, const QVariant& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), QJsonValue::fromVariant(value));
            return a;
        };
        const auto tilingRule = [&](const QString& name, int priority, const QString& screenId,
                                    const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // Separate rules fill separate slots — all compose (per-slot read).
        const PWR::Rule mw = tilingRule(QStringLiteral("mw"), 400, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetMaxWindows, 3)});
        const PWR::Rule sr = tilingRule(QStringLiteral("sr"), 300, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetSplitRatio, 0.6)});
        const PWR::Rule mc = tilingRule(QStringLiteral("mc"), 200, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetMasterCount, 2)});
        // Insert position carries a wire token → resolves to the AutotileInsertPosition int.
        const PWR::Rule ip =
            tilingRule(QStringLiteral("ip"), 100, QStringLiteral("DP-1"),
                       {valueAction(PWR::ActionType::SetInsertPosition, QString(PWR::InsertPositionToken::AsMaster))});
        // Overflow behavior carries a wire token → AutotileOverflowBehavior int.
        const PWR::Rule ob = tilingRule(
            QStringLiteral("ob"), 50, QStringLiteral("DP-1"),
            {valueAction(PWR::ActionType::SetOverflowBehavior, QString(PWR::OverflowBehaviorToken::Unlimited))});
        // Drag behavior carries a wire token → AutotileDragBehavior int.
        const PWR::Rule db =
            tilingRule(QStringLiteral("db"), 25, QStringLiteral("DP-1"),
                       {valueAction(PWR::ActionType::SetDragBehavior, QString(PWR::DragBehaviorToken::Reorder))});
        // SetAlgorithmParam carries a target algorithm token + a free-form params blob.
        PWR::RuleAction apAction;
        apAction.type = QString(PWR::ActionType::SetAlgorithmParam);
        apAction.params.insert(QString(PWR::ActionParam::Algorithm), QStringLiteral("bsp"));
        QJsonObject apParams;
        apParams.insert(QStringLiteral("ratio"), 0.7);
        apAction.params.insert(QString(PWR::ActionParam::Params), apParams);
        const PWR::Rule ap = tilingRule(QStringLiteral("ap"), 10, QStringLiteral("DP-1"), {apAction});
        QVERIFY(f.store->setAllRules({mw, sr, mc, ip, ob, db, ap}));

        const PhosphorZones::ContextTilingParams p =
            f.registry->resolveContextTilingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(p.maxWindows.has_value());
        QCOMPARE(*p.maxWindows, 3);
        QVERIFY(p.splitRatio.has_value());
        QCOMPARE(*p.splitRatio, 0.6);
        QVERIFY(p.masterCount.has_value());
        QCOMPARE(*p.masterCount, 2);
        QVERIFY(p.insertPosition.has_value());
        QCOMPARE(*p.insertPosition, 2); // "asMaster" → AutotileInsertPosition::AsMaster (2)
        QVERIFY(p.overflowBehavior.has_value());
        QCOMPARE(*p.overflowBehavior, 1); // "unlimited" → AutotileOverflowBehavior::Unlimited (1)
        QVERIFY(p.dragBehavior.has_value());
        QCOMPARE(*p.dragBehavior, 1); // "reorder" → AutotileDragBehavior::Reorder (1)
        QCOMPARE(p.algorithmParamTarget, QStringLiteral("bsp"));
        QCOMPARE(p.algorithmParams.value(QStringLiteral("ratio")).toDouble(), 0.7);

        // A screen the rules do not pin → all-unset (the daemon then leaves the
        // config-derived override map untouched for that screen).
        QVERIFY(f.registry->resolveContextTilingParams(QStringLiteral("DP-2"), 0, QString()).isEmpty());
    }

    // ── Context scrolling-parameter resolution (width / centering / display / axis) ──
    // resolveContextScrollingParams is a per-slot read like its tiling sibling:
    // independent SetScrollDefaultColumnWidth / SetCenterFocusedColumn /
    // SetScrollDefaultColumnDisplay rules compose, and an unpinned screen resolves
    // to an all-unset (empty) params struct.
    void testContextScrollingParams_perSlotComposition()
    {
        const auto valueAction = [](QLatin1StringView type, const QVariant& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), QJsonValue::fromVariant(value));
            return a;
        };
        const auto scrollRule = [&](const QString& name, int priority, const QString& screenId,
                                    const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // Separate rules fill separate slots — all compose (per-slot read).
        const PWR::Rule cw = scrollRule(QStringLiteral("cw"), 400, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetScrollDefaultColumnWidth, 0.75)});
        // Centering carries a wire token → the centering int.
        const PWR::Rule cf = scrollRule(
            QStringLiteral("cf"), 300, QStringLiteral("DP-1"),
            {valueAction(PWR::ActionType::SetCenterFocusedColumn, QString(PWR::CenterFocusedColumnToken::OnOverflow))});
        // Column display carries a wire token → the display int.
        const PWR::Rule cd = scrollRule(
            QStringLiteral("cd"), 200, QStringLiteral("DP-1"),
            {valueAction(PWR::ActionType::SetScrollDefaultColumnDisplay, QString(PWR::ColumnDisplayToken::Tabbed))});
        // The strip axis carries a wire token → the Scrolling.StripAxis
        // config int (the INTENT space, not the engine's ScrollAxis).
        const PWR::Rule ax =
            scrollRule(QStringLiteral("ax"), 100, QStringLiteral("DP-1"),
                       {valueAction(PWR::ActionType::SetScrollStripAxis, QString(PWR::StripAxisToken::Vertical))});
        QVERIFY(f.store->setAllRules({cw, cf, cd, ax}));

        const PhosphorZones::ContextScrollingParams p =
            f.registry->resolveContextScrollingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(p.defaultColumnWidth.has_value());
        QCOMPARE(*p.defaultColumnWidth, 0.75);
        QVERIFY(p.centerFocusedColumn.has_value());
        QCOMPARE(*p.centerFocusedColumn, 2); // "onOverflow" → 2
        QVERIFY(p.defaultColumnDisplay.has_value());
        QCOMPARE(*p.defaultColumnDisplay, 1); // "tabbed" → 1
        QVERIFY(p.stripAxis.has_value());
        QCOMPARE(*p.stripAxis, 2); // "vertical" → 2

        // A screen the rules do not pin → all-unset (the engine then keeps its
        // config-derived parameters for that screen).
        QVERIFY(f.registry->resolveContextScrollingParams(QStringLiteral("DP-2"), 0, QString()).isEmpty());

        // The focus-follows-mouse scroll cap, pinned ALONE on its own screen
        // rather than folded into the composed rule above. The resolver reaches
        // its slots through a filtered walk whose admission set is written by
        // hand, and a slot missing from that set is skipped with the whole rule
        // it rides: a cap folded in beside four admitted siblings would resolve
        // on THEIR admission and prove nothing. A rule whose only action is the
        // cap is the shape the settings UI produces for a single-action rule,
        // and it is the shape that fails when the set is wrong.
        const PWR::Rule capOnly = scrollRule(QStringLiteral("cap"), 500, QStringLiteral("DP-3"),
                                             {valueAction(PWR::ActionType::SetScrollFocusFollowsMouseMaxScroll, 0.4)});
        QVERIFY(f.store->setAllRules({capOnly}));
        const PhosphorZones::ContextScrollingParams capParams =
            f.registry->resolveContextScrollingParams(QStringLiteral("DP-3"), 0, QString());
        QVERIFY(capParams.focusFollowsMouseMaxScroll.has_value());
        QCOMPARE(*capParams.focusFollowsMouseMaxScroll, 0.4);
        QVERIFY(!capParams.isEmpty());
    }

    // ─── Context scrolling params: the tab LABEL FONT slots ────────────────
    // The five font slots are the only part of the tab-indicator family whose
    // resolved value can legitimately be EMPTY, and the empty case is the one
    // carrying the meaning: it is a rule walking one screen back to the system
    // font after a global family was picked. So what is pinned here is not
    // "the value arrives" but "the value arrives ENGAGED while holding an
    // empty string". An isEmpty() tidy in the resolver's readString would
    // leave the optional disengaged and the screen would silently keep the
    // global family.
    void testContextScrollingParams_tabIndicatorFontSlots()
    {
        // Re-declared rather than shared with the sibling above: both helpers
        // are function-local there. That is this file's own precedent
        // (intGapAction and valueAction each appear twice already).
        const auto valueAction = [](QLatin1StringView type, const QVariant& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), QJsonValue::fromVariant(value));
            return a;
        };
        const auto scrollRule = [&](const QString& name, int priority, const QString& screenId,
                                    const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // A font-ONLY rule. Nothing else about the indicator is touched, which
        // is what makes the hasTabIndicatorOverrides() assertion mean
        // something: the font slots have to count towards it on their own.
        const PWR::Rule fr =
            scrollRule(QStringLiteral("font"), 500, QStringLiteral("DP-1"),
                       {valueAction(PWR::ActionType::SetTabIndicatorFontFamily, QStringLiteral("Noto Sans")),
                        valueAction(PWR::ActionType::SetTabIndicatorFontWeight, 400),
                        valueAction(PWR::ActionType::SetTabIndicatorFontItalic, true),
                        // FALSE, not absent. A bool slot has to survive as a
                        // VALUE, or "turn underline off for this screen" is
                        // indistinguishable from "say nothing about underline".
                        valueAction(PWR::ActionType::SetTabIndicatorFontUnderline, false),
                        valueAction(PWR::ActionType::SetTabIndicatorFontStrikeout, true)});
        QVERIFY(f.store->setAllRules({fr}));

        const PhosphorZones::ContextScrollingParams p =
            f.registry->resolveContextScrollingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(!p.isEmpty());
        QVERIFY(p.hasTabIndicatorOverrides());
        QVERIFY(p.tabIndicatorFontFamily.has_value());
        QCOMPARE(*p.tabIndicatorFontFamily, QStringLiteral("Noto Sans"));
        QVERIFY(p.tabIndicatorFontWeight.has_value());
        QCOMPARE(*p.tabIndicatorFontWeight, 400);
        QVERIFY(p.tabIndicatorFontItalic.has_value());
        QCOMPARE(*p.tabIndicatorFontItalic, true);
        QVERIFY(p.tabIndicatorFontUnderline.has_value());
        QCOMPARE(*p.tabIndicatorFontUnderline, false);
        QVERIFY(p.tabIndicatorFontStrikeout.has_value());
        QCOMPARE(*p.tabIndicatorFontStrikeout, true);

        // ── the empty family ──
        // QString(), never QVariant(): QJsonValue::fromVariant turns a bare
        // QVariant into Null, the descriptor REJECTS Null, and the whole rule
        // would then be dropped at the store — this pin would quietly become a
        // no-op row that still passed.
        const PWR::Rule er = scrollRule(QStringLiteral("empty"), 400, QStringLiteral("DP-2"),
                                        {valueAction(PWR::ActionType::SetTabIndicatorFontFamily, QString())});
        QVERIFY(f.store->setAllRules({fr, er}));
        const PhosphorZones::ContextScrollingParams e =
            f.registry->resolveContextScrollingParams(QStringLiteral("DP-2"), 0, QString());
        QVERIFY(e.hasTabIndicatorOverrides());
        QVERIFY(e.tabIndicatorFontFamily.has_value()); // ENGAGED…
        QVERIFY(e.tabIndicatorFontFamily->isEmpty()); // …and holding the empty string.

        // A screen no rule pins resolves to all-unset, so the daemon leaves
        // that screen's font on the global setting.
        const PhosphorZones::ContextScrollingParams none =
            f.registry->resolveContextScrollingParams(QStringLiteral("DP-3"), 0, QString());
        QVERIFY(none.isEmpty());
        QVERIFY(!none.hasTabIndicatorOverrides());

        // An out-of-range weight never reaches a screen. NOTE THE MECHANISM,
        // because it is not the one it looks like: Rule::isValid() runs every
        // action of a rule through the registry, so setAllRules drops this rule
        // whole at the STORE boundary and the resolver's own 100..900 bound is
        // never consulted. Both guards agree on the outcome the user sees —
        // the screen keeps the global weight — which is what this pins.
        const PWR::Rule br = scrollRule(QStringLiteral("bad"), 300, QStringLiteral("DP-4"),
                                        {valueAction(PWR::ActionType::SetTabIndicatorFontWeight, 9999)});
        QVERIFY(f.store->setAllRules({fr, er, br}));
        QVERIFY(!f.registry->resolveContextScrollingParams(QStringLiteral("DP-4"), 0, QString())
                     .tabIndicatorFontWeight.has_value());
    }

    // ─── Per-monitor gap rule overrides the baseline for that screen only ────
    // A per-monitor gap override is authored by the Appearance page as a NORMAL
    // (non-managed) screen-scoped rule: match `ScreenId == screen`, carrying the
    // gap actions. It must override the GLOBAL default (the managed, catch-all
    // baseline rule that holds the global gaps) for that monitor only — and the
    // managed catch-all baseline must itself stay EXCLUDED from the context
    // override, so an un-pinned monitor reports no override and falls through to
    // the global default tier.

    void testContextGaps_perScreenRuleOverridesBaseline()
    {
        const auto intGapAction = [](QLatin1StringView type, int value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            return a;
        };

        RegistryFixture f = makeRegistryFixture();

        // The managed, catch-all baseline rule that carries the GLOBAL default
        // gaps (inner = 4). It is pinned to lowest precedence and is the level-4
        // default tier, NOT a context override — resolveContextGaps excludes it.
        PWR::Rule baseline;
        baseline.id = ConfigDefaults::baselineGapRuleId();
        baseline.name = QStringLiteral("Default gaps");
        baseline.enabled = true;
        baseline.managed = true;
        baseline.priority = std::numeric_limits<int>::min();
        baseline.match = PWR::MatchExpression{}; // catch-all All{}
        baseline.actions = {intGapAction(PWR::ActionType::SetInnerGap, 4)};

        // A non-managed per-monitor gap override RULE for DP-1 (inner = 20). The
        // settings page authors per-monitor gaps as config now, but the rule
        // cascade still resolves a hand-authored gap rule keyed on a v5 id
        // namespaced under the baseline gap id — this pins that cascade behavior.
        PWR::Rule perScreen;
        perScreen.id = QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), QByteArrayLiteral("DP-1"));
        perScreen.name = QStringLiteral("Gaps (DP-1)");
        perScreen.enabled = true;
        perScreen.managed = false;
        perScreen.priority = 310; // context band, well above the baseline floor
        perScreen.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        perScreen.actions = {intGapAction(PWR::ActionType::SetInnerGap, 20)};

        QVERIFY(f.store->setAllRules({baseline, perScreen}));

        // DP-1 carries the per-monitor override → inner gap 20 surfaces as a
        // tier-1 context override (it beats the excluded baseline).
        const PhosphorZones::ContextGapOverride dp1 =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(dp1.innerGap.has_value());
        QCOMPARE(*dp1.innerGap, 20);

        // DP-2 has no per-monitor rule. The managed catch-all baseline is
        // EXCLUDED, so there is NO context override — the cascade falls through
        // to the global default tier (the baseline's value, surfaced elsewhere).
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());

        // The exclusion is keyed on MANAGED-and-catch-all, not on catch-all
        // alone: a user's own catch-all gap rule is an ordinary context override
        // and must surface on every screen, DP-2 included. Without this arm a
        // guard that dropped all catch-alls would look correct.
        PWR::Rule userCatchAll;
        userCatchAll.id = QUuid::createUuid();
        userCatchAll.name = QStringLiteral("My gaps everywhere");
        userCatchAll.enabled = true;
        userCatchAll.managed = false;
        // Below the per-monitor rule's 310, so the DP-1 arm below is about the
        // per-monitor override winning and not about specificity ordering
        // (which testPerScreenGapBeatsPerModeGap covers on its own).
        userCatchAll.priority = 300;
        userCatchAll.match = PWR::MatchExpression{}; // catch-all All{}
        userCatchAll.actions = {intGapAction(PWR::ActionType::SetInnerGap, 9)};
        QVERIFY(f.store->setAllRules({baseline, perScreen, userCatchAll}));

        const PhosphorZones::ContextGapOverride dp2User =
            f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString());
        QVERIFY(dp2User.innerGap.has_value());
        QCOMPARE(*dp2User.innerGap, 9);
        // DP-1 still prefers its per-monitor rule over the user catch-all.
        const PhosphorZones::ContextGapOverride dp1User =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(dp1User.innerGap.has_value());
        QCOMPARE(*dp1User.innerGap, 20);
    }

    // Plain members, not test slots: they take parameters.
private:
    static constexpr QLatin1StringView kShapeLayout{"{88888888-8888-8888-8888-888888888888}"};

    static PWR::RuleAction shapeValueAction(QLatin1StringView type, const QJsonValue& value)
    {
        PWR::RuleAction a;
        a.type = QString(type);
        a.params.insert(QString(PWR::ActionParam::Value), value);
        return a;
    }

    static PWR::Rule shapeContextRule(int priority, const PWR::MatchExpression& match,
                                      const QList<PWR::RuleAction>& actions)
    {
        PWR::Rule r;
        r.id = QUuid::createUuid();
        r.name = QStringLiteral("screen-shape rule");
        r.enabled = true;
        r.priority = priority;
        r.match = match;
        r.actions = actions;
        return r;
    }

    static PWR::Rule shapeAssignmentRule(int priority, const PWR::MatchExpression& match, const QString& layout)
    {
        return shapeContextRule(priority, match,
                                CRB::makeAssignmentActions(QStringLiteral("snapping"), layout, QString(), QString()));
    }

    /// One rule per context resolver, all sharing @p match and each carrying
    /// only that resolver's action, so a resolver whose filter or key is wrong
    /// fails on its own line in verifyEveryResolver.
    static QList<PWR::Rule> oneShapeRulePerResolver(const PWR::MatchExpression& match)
    {
        PWR::RuleAction shader;
        shader.type = QString(PWR::ActionType::OverrideOverlayShader);
        shader.params.insert(QString(PWR::ActionParam::EffectId), QStringLiteral("shape-glow"));
        return {
            shapeAssignmentRule(400, match, QString(kShapeLayout)),
            shapeContextRule(400, match, {shapeValueAction(PWR::ActionType::SetInnerGap, 13)}),
            shapeContextRule(400, match, {shapeValueAction(PWR::ActionType::LockContext, true)}),
            shapeContextRule(400, match, {shapeValueAction(PWR::ActionType::DefaultLayoutAssignment, true)}),
            shapeContextRule(400, match, {shapeValueAction(PWR::ActionType::SetOsdEnabled, false)}),
            shapeContextRule(400, match, {shapeValueAction(PWR::ActionType::SetDragSelectorEnabled, false)}),
            shapeContextRule(400, match, {shader}),
            shapeContextRule(400, match, {shapeValueAction(PWR::ActionType::SetMaxWindows, 3)}),
            shapeContextRule(400, match, {shapeValueAction(PWR::ActionType::SetScrollDefaultColumnWidth, 0.75)}),
        };
    }

    /// Require every context resolver on DP-1 to have applied (or not) the
    /// rule oneShapeRulePerResolver gave it. Fails through QVERIFY2, so the
    /// caller checks QTest::currentTestFailed() after each call.
    static void verifyEveryResolver(RegistryFixture& f, bool applied, const char* step)
    {
        const QString screen = QStringLiteral("DP-1");
        const auto where = [step](const char* resolver) {
            return QStringLiteral("%1 at step \"%2\"").arg(QLatin1String(resolver), QLatin1String(step));
        };
        QVERIFY2((f.registry->assignmentEntryForScreen(screen, 0, QString()).snappingLayout == kShapeLayout) == applied,
                 qPrintable(where("assignment")));
        QVERIFY2(f.registry->resolveContextGaps(screen, 0, QString()).innerGap.has_value() == applied,
                 qPrintable(where("gap")));
        QVERIFY2(f.registry->resolveContextLocked(screen, 0, QString()) == applied, qPrintable(where("lock")));
        QVERIFY2(f.registry->resolveContextDefaultAssignment(screen, 0, QString()).has_value() == applied,
                 qPrintable(where("default assignment")));
        QVERIFY2(f.registry->resolveContextOsdEnabled(screen, 0, QString()).has_value() == applied,
                 qPrintable(where("OSD")));
        QVERIFY2(f.registry->resolveContextDragSelectorEnabled(screen, 0, QString()).has_value() == applied,
                 qPrintable(where("drag selector")));
        QVERIFY2(f.registry->resolveContextOverlay(screen, 0, QString()).shaderId.has_value() == applied,
                 qPrintable(where("overlay")));
        QVERIFY2(f.registry->resolveContextTilingParams(screen, 0, QString()).maxWindows.has_value() == applied,
                 qPrintable(where("tiling params")));
        QVERIFY2(f.registry->resolveContextScrollingParams(screen, 0, QString()).defaultColumnWidth.has_value()
                     == applied,
                 qPrintable(where("scrolling params")));
    }
};

QTEST_MAIN(TestRuleCascadeContext)
#include "test_rule_cascade_context.moc"
