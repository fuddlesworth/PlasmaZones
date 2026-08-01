// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * Parameter validation for the TILING-FAMILY context actions: the autotile
 * knobs, the gap overrides, the tab indicator, and the scrolling knobs.
 *
 * Split out of test_ruleaction.cpp, which owns the JSON contract itself
 * (round-tripping, type registration, stray-key rejection) and the
 * action-domain pins. These four families share one shape instead: each is a
 * `value`-keyed action whose whole contract is a numeric range or a closed
 * token vocabulary, and between them they are the bulk of the vocabulary, so
 * they carry their own suite rather than crowding the contract tests.
 */

#include <PhosphorRules/RuleAction.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QTest>

using namespace PhosphorRules;

class TestRuleActionTilingParams : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── autotile parameter actions (context-domain) ──

    void testTilingParamActions_range()
    {
        // SetMaxWindows: 1..12, integer count.
        {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetMaxWindows));
            o.insert(QStringLiteral("value"), 0); // below 1 rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 13); // above 12 rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 4.5); // non-integral rejected (not truncated)
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 4); // in range accepted
            QVERIFY(RuleAction::fromJson(o).has_value());
        }
        // SetSplitRatio: wire is the [0.1, 0.9] ratio.
        {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetSplitRatio));
            o.insert(QStringLiteral("value"), 0.05); // below 0.1 rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 0.95); // above 0.9 rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), QStringLiteral("0.5")); // non-numeric rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 0.6); // in range accepted
            QVERIFY(RuleAction::fromJson(o).has_value());
        }
        // SetMasterCount: 1..5, integer count.
        {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetMasterCount));
            o.insert(QStringLiteral("value"), 0); // below 1 rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 6); // above 5 rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 2.5); // non-integral rejected (not truncated)
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 2); // in range accepted
            QVERIFY(RuleAction::fromJson(o).has_value());
        }
        // SetInsertPosition: closed enum vocabulary.
        {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetInsertPosition));
            o.insert(QStringLiteral("value"), QStringLiteral("middle")); // unknown token rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (const QLatin1StringView token :
                 {InsertPositionToken::End, InsertPositionToken::AfterFocused, InsertPositionToken::AsMaster}) {
                o.insert(QStringLiteral("value"), QString::fromLatin1(token));
                QVERIFY2(RuleAction::fromJson(o).has_value(), token.data());
            }
        }
        // SetOverflowBehavior: closed enum vocabulary.
        {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetOverflowBehavior));
            o.insert(QStringLiteral("value"), QStringLiteral("cap")); // unknown token rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (const QLatin1StringView token : {OverflowBehaviorToken::Float, OverflowBehaviorToken::Unlimited}) {
                o.insert(QStringLiteral("value"), QString::fromLatin1(token));
                QVERIFY2(RuleAction::fromJson(o).has_value(), token.data());
            }
        }
        // SetDragBehavior: closed enum vocabulary.
        {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetDragBehavior));
            o.insert(QStringLiteral("value"), QStringLiteral("swap")); // unknown token rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (const QLatin1StringView token : {DragBehaviorToken::Float, DragBehaviorToken::Reorder}) {
                o.insert(QStringLiteral("value"), QString::fromLatin1(token));
                QVERIFY2(RuleAction::fromJson(o).has_value(), token.data());
            }
        }
        // SetAlgorithmParam: requires a non-empty algorithm token; the params blob
        // is free-form (validated against the algorithm schema at apply time).
        {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetAlgorithmParam));
            QVERIFY(!RuleAction::fromJson(o).has_value()); // no algorithm → rejected
            o.insert(QStringLiteral("algorithm"), QString()); // empty → rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("algorithm"), QStringLiteral("bsp"));
            QVERIFY(RuleAction::fromJson(o).has_value()); // algorithm alone → accepted
            QJsonObject blob;
            blob.insert(QStringLiteral("ratio"), 0.7);
            o.insert(QStringLiteral("params"), blob); // params allowed alongside
            const auto reloaded = RuleAction::fromJson(o);
            QVERIFY(reloaded.has_value());
            QCOMPARE(
                reloaded->params.value(QStringLiteral("params")).toObject().value(QStringLiteral("ratio")).toDouble(),
                0.7);
            // An unexpected key is still rejected (strict allowedKeys = {algorithm, params}).
            o.insert(QStringLiteral("bogus"), 1);
            QVERIFY(!RuleAction::fromJson(o).has_value());
        }
    }

    // ── gap actions (context-domain) ──

    void testGapNumberActions_range()
    {
        for (const QLatin1StringView type :
             {ActionType::SetInnerGap, ActionType::SetOuterGap, ActionType::SetOuterGapTop,
              ActionType::SetOuterGapBottom, ActionType::SetOuterGapLeft, ActionType::SetOuterGapRight}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            o.insert(QStringLiteral("value"), -5);
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 600); // > kMaxGap (500)
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 0);
            QVERIFY2(RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 12);
            const auto reloaded = RuleAction::fromJson(o);
            QVERIFY2(reloaded.has_value(), type.data());
            QCOMPARE(reloaded->params.value(QStringLiteral("value")).toInt(), 12);
        }
    }

    void testGapPerSideToggle_requiresBool()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetUsePerSideOuterGap));
        o.insert(QStringLiteral("value"), QStringLiteral("yes"));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("value"), false);
        QVERIFY(RuleAction::fromJson(o).has_value());
    }

    /// Every tab-indicator action pinned to its OWN slot, plus the bounds and
    /// vocabularies that are the only load-time defence for a hand-edited
    /// rules.json.
    ///
    /// The slot pins are the point. Sixteen near-identical registrations were
    /// added in one commit, each a copy-paste of the last, and a
    /// `constantSlot` left pointing at the previous action's slot passes every
    /// bounds assertion while silently writing the wrong slot forever. That is
    /// the hazard testScrollingParamActions_range already calls out for its
    /// own pair; this covers the sixteen that came after it.
    void testTabIndicatorActions_slotsBoundsAndVocabularies()
    {
        const auto rejectsMissingValue = [](QLatin1StringView type) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
        };
        /// Load @p value for @p type, assert it is accepted, pin its slot, and
        /// round-trip it.
        const auto acceptsWithSlot = [](QLatin1StringView type, const QJsonValue& value, QLatin1StringView slot) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            o.insert(QStringLiteral("value"), value);
            const auto loaded = RuleAction::fromJson(o);
            QVERIFY2(loaded.has_value(), type.data());
            QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(slot));
            const auto roundTripped = RuleAction::fromJson(loaded->toJson());
            QVERIFY2(roundTripped.has_value(), type.data());
            QCOMPARE(*roundTripped, *loaded);
        };
        const auto rejects = [](QLatin1StringView type, const QJsonValue& value) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            o.insert(QStringLiteral("value"), value);
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
        };

        // ── the three bools ──
        for (const auto& pair : QList<QPair<QLatin1StringView, QLatin1StringView>>{
                 {ActionType::SetTabIndicatorEnabled, ActionSlot::TabIndicatorEnabled},
                 {ActionType::SetTabIndicatorHideWhenSingleTab, ActionSlot::TabIndicatorHideWhenSingleTab},
                 {ActionType::SetTabIndicatorPlaceWithinColumn, ActionSlot::TabIndicatorPlaceWithinColumn}}) {
            rejectsMissingValue(pair.first);
            rejects(pair.first, QStringLiteral("yes")); // non-bool
            acceptsWithSlot(pair.first, true, pair.second);
            acceptsWithSlot(pair.first, false, pair.second); // both polarities matter
        }

        // ── gap: the SIGNED range, the one most likely to be "fixed" to a
        // zero floor by someone who does not know a negative gap is niri's way
        // of drawing the indicator over the window ──
        rejectsMissingValue(ActionType::SetTabIndicatorGap);
        rejects(ActionType::SetTabIndicatorGap, QStringLiteral("5"));
        rejects(ActionType::SetTabIndicatorGap, MinTabIndicatorGap - 1);
        rejects(ActionType::SetTabIndicatorGap, MaxTabIndicatorGap + 1);
        acceptsWithSlot(ActionType::SetTabIndicatorGap, MinTabIndicatorGap, ActionSlot::TabIndicatorGap);
        acceptsWithSlot(ActionType::SetTabIndicatorGap, -12, ActionSlot::TabIndicatorGap);
        acceptsWithSlot(ActionType::SetTabIndicatorGap, MaxTabIndicatorGap, ActionSlot::TabIndicatorGap);

        // ── width: floors at 1, so 0 must be REJECTED (a zero-floored helper
        // would wrongly admit it) ──
        rejectsMissingValue(ActionType::SetTabIndicatorWidth);
        rejects(ActionType::SetTabIndicatorWidth, 0);
        rejects(ActionType::SetTabIndicatorWidth, MaxTabIndicatorWidth + 1);
        acceptsWithSlot(ActionType::SetTabIndicatorWidth, MinTabIndicatorWidth, ActionSlot::TabIndicatorWidth);
        acceptsWithSlot(ActionType::SetTabIndicatorWidth, MaxTabIndicatorWidth, ActionSlot::TabIndicatorWidth);

        // ── length: a [0.05, 1.0] fraction ──
        rejectsMissingValue(ActionType::SetTabIndicatorLength);
        rejects(ActionType::SetTabIndicatorLength, 0.0);
        rejects(ActionType::SetTabIndicatorLength, 1.5);
        acceptsWithSlot(ActionType::SetTabIndicatorLength, MinTabIndicatorLengthRatio, ActionSlot::TabIndicatorLength);
        acceptsWithSlot(ActionType::SetTabIndicatorLength, MaxTabIndicatorLengthRatio, ActionSlot::TabIndicatorLength);

        // ── gaps between tabs: genuinely zero-floored, unlike its siblings ──
        rejectsMissingValue(ActionType::SetTabIndicatorGapsBetweenTabs);
        rejects(ActionType::SetTabIndicatorGapsBetweenTabs, -1);
        acceptsWithSlot(ActionType::SetTabIndicatorGapsBetweenTabs, 0, ActionSlot::TabIndicatorGapsBetweenTabs);
        acceptsWithSlot(ActionType::SetTabIndicatorGapsBetweenTabs, MaxTabIndicatorGap,
                        ActionSlot::TabIndicatorGapsBetweenTabs);

        // ── corner radius: floors at the pill SENTINEL, not at zero ──
        rejectsMissingValue(ActionType::SetTabIndicatorCornerRadius);
        rejects(ActionType::SetTabIndicatorCornerRadius, TabIndicatorCornerRadiusPill - 1);
        rejects(ActionType::SetTabIndicatorCornerRadius, MaxTabIndicatorCornerRadius + 1);
        acceptsWithSlot(ActionType::SetTabIndicatorCornerRadius, TabIndicatorCornerRadiusPill,
                        ActionSlot::TabIndicatorCornerRadius);
        acceptsWithSlot(ActionType::SetTabIndicatorCornerRadius, 0, ActionSlot::TabIndicatorCornerRadius);

        // ── the two closed vocabularies ──
        rejectsMissingValue(ActionType::SetTabIndicatorStyle);
        rejects(ActionType::SetTabIndicatorStyle, QStringLiteral("pills")); // unknown token
        acceptsWithSlot(ActionType::SetTabIndicatorStyle, QString::fromLatin1(TabIndicatorStyleToken::Chips),
                        ActionSlot::TabIndicatorStyle);
        acceptsWithSlot(ActionType::SetTabIndicatorStyle, QString::fromLatin1(TabIndicatorStyleToken::Bar),
                        ActionSlot::TabIndicatorStyle);

        rejectsMissingValue(ActionType::SetTabIndicatorPosition);
        rejects(ActionType::SetTabIndicatorPosition, QStringLiteral("middle"));
        for (QLatin1StringView token : {TabIndicatorPositionToken::Left, TabIndicatorPositionToken::Right,
                                        TabIndicatorPositionToken::Top, TabIndicatorPositionToken::Bottom}) {
            acceptsWithSlot(ActionType::SetTabIndicatorPosition, QString::fromLatin1(token),
                            ActionSlot::TabIndicatorPosition);
        }

        // ── the six colours: HEX ONLY. The accent sentinel must be REJECTED —
        // no consumer on either the context or the per-window path resolves it,
        // so accepting it would land an unparseable colour on the overlay. This
        // is the overlay-colour contract testOverlayColorActions_hexOnlyNoAccent
        // pins for its own family. ──
        for (const auto& pair : QList<QPair<QLatin1StringView, QLatin1StringView>>{
                 {ActionType::SetTabIndicatorActiveColor, ActionSlot::TabIndicatorActiveColor},
                 {ActionType::SetTabIndicatorInactiveColor, ActionSlot::TabIndicatorInactiveColor},
                 {ActionType::SetTabIndicatorUrgentColor, ActionSlot::TabIndicatorUrgentColor},
                 {ActionType::TabColorActive, ActionSlot::TabColorActive},
                 {ActionType::TabColorInactive, ActionSlot::TabColorInactive},
                 {ActionType::TabColorUrgent, ActionSlot::TabColorUrgent}}) {
            rejectsMissingValue(pair.first);
            rejects(pair.first, QStringLiteral("accent"));
            rejects(pair.first, QStringLiteral("red")); // named colours are not hex
            rejects(pair.first, QStringLiteral("3DAEE9")); // missing the #
            acceptsWithSlot(pair.first, QStringLiteral("#FF3DAEE9"), pair.second);
            acceptsWithSlot(pair.first, QStringLiteral("#3DAEE9"), pair.second);
        }
    }

    void testScrollingParamActions_range()
    {
        // The nine scrolling actions are the only load-time defence for a
        // hand-edited rules.json — pin bounds and the four closed token
        // vocabularies exactly like the tiling family above. Every accepted
        // payload also round-trips through toJson→fromJson, so a descriptor
        // that validates on load but serialises a shape it cannot read back
        // (silently dropping the rule on the next daemon start) fails here.

        // A payload with no `value` at all must be rejected by every action in
        // the family — each declares allowedKeys = {Value} with value required.
        const auto rejectsMissingValue = [](QLatin1StringView type) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
        };

        // SetScrollDefaultColumnWidth / OpenColumnWidth: [0.05, 1.0] fraction.
        for (QLatin1StringView type : {ActionType::SetScrollDefaultColumnWidth, ActionType::OpenColumnWidth}) {
            rejectsMissingValue(type);
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            o.insert(QStringLiteral("value"), 0.02); // below 0.05 rejected
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 1.5); // above 1.0 rejected
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), QStringLiteral("wide")); // non-numeric rejected
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 0.05); // inclusive lower bound accepted
            QVERIFY2(RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 1.0); // inclusive upper bound accepted
            const auto accepted = RuleAction::fromJson(o);
            QVERIFY2(accepted.has_value(), type.data());
            // Slot pin: these two actions are the pair most likely to be
            // cross-wired by a copy-pasted constantSlot — a swap passes
            // every bounds assertion above but not this.
            const QString expectedSlot = (type == ActionType::SetScrollDefaultColumnWidth)
                ? QString(ActionSlot::ScrollDefaultColumnWidth)
                : QString(ActionSlot::OpenColumnWidth);
            QCOMPARE(ActionRegistry::instance().slotFor(*accepted), expectedSlot);
            const auto roundTripped = RuleAction::fromJson(accepted->toJson());
            QVERIFY2(roundTripped.has_value(), type.data());
            QCOMPARE(*roundTripped, *accepted);
        }
        // SetCenterFocusedColumn: closed token vocabulary.
        {
            rejectsMissingValue(ActionType::SetCenterFocusedColumn);
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetCenterFocusedColumn));
            o.insert(QStringLiteral("value"), QStringLiteral("sometimes")); // unknown token rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (QLatin1StringView token : {CenterFocusedColumnToken::Never, CenterFocusedColumnToken::Always,
                                            CenterFocusedColumnToken::OnOverflow}) {
                o.insert(QStringLiteral("value"), QString::fromLatin1(token));
                const auto loaded = RuleAction::fromJson(o);
                QVERIFY2(loaded.has_value(), token.data());
                QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::CenterFocusedColumn));
                const auto roundTripped = RuleAction::fromJson(loaded->toJson());
                QVERIFY2(roundTripped.has_value(), token.data());
                QCOMPARE(*roundTripped, *loaded);
            }
        }
        // SetScrollDefaultColumnDisplay: normal | tabbed.
        {
            rejectsMissingValue(ActionType::SetScrollDefaultColumnDisplay);
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetScrollDefaultColumnDisplay));
            o.insert(QStringLiteral("value"), QStringLiteral("stacked")); // unknown token rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (QLatin1StringView token : {ColumnDisplayToken::Normal, ColumnDisplayToken::Tabbed}) {
                o.insert(QStringLiteral("value"), QString::fromLatin1(token));
                const auto loaded = RuleAction::fromJson(o);
                QVERIFY2(loaded.has_value(), token.data());
                QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::ScrollDefaultColumnDisplay));
                const auto roundTripped = RuleAction::fromJson(loaded->toJson());
                QVERIFY2(roundTripped.has_value(), token.data());
                QCOMPARE(*roundTripped, *loaded);
            }
        }
        // OpenColumnPlacement: newColumn | consume.
        {
            rejectsMissingValue(ActionType::OpenColumnPlacement);
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::OpenColumnPlacement));
            o.insert(QStringLiteral("value"), QStringLiteral("sideways")); // unknown token rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), true); // bool rejected — the wire is a token string
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 1); // number rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (QLatin1StringView token : {ColumnPlacementToken::NewColumn, ColumnPlacementToken::Consume}) {
                o.insert(QStringLiteral("value"), QString::fromLatin1(token));
                const auto loaded = RuleAction::fromJson(o);
                QVERIFY2(loaded.has_value(), token.data());
                QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::OpenColumnPlacement));
                const auto roundTripped = RuleAction::fromJson(loaded->toJson());
                QVERIFY2(roundTripped.has_value(), token.data());
                QCOMPARE(*roundTripped, *loaded);
            }
        }
        // OpenTabbed: bool only. Both polarities matter — false is the
        // per-window veto of a context-level tabbed display, so it must
        // survive load as an explicit value rather than being read as absent.
        {
            rejectsMissingValue(ActionType::OpenTabbed);
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::OpenTabbed));
            o.insert(QStringLiteral("value"), QStringLiteral("yes")); // non-bool rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 1); // number rejected (no truthiness coercion)
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (const bool value : {true, false}) {
                o.insert(QStringLiteral("value"), value);
                const auto loaded = RuleAction::fromJson(o);
                QVERIFY(loaded.has_value());
                QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::OpenTabbed));
                const auto roundTripped = RuleAction::fromJson(loaded->toJson());
                QVERIFY(roundTripped.has_value());
                QCOMPARE(*roundTripped, *loaded);
                QCOMPARE(roundTripped->params.value(QStringLiteral("value")).toBool(!value), value);
            }
        }
        // SetScrollInsertPosition: the five position tokens; unknown rejected.
        {
            rejectsMissingValue(ActionType::SetScrollInsertPosition);
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(ActionType::SetScrollInsertPosition));
            o.insert(QStringLiteral("value"), QStringLiteral("middle")); // unknown token rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), true); // bool rejected
            QVERIFY(!RuleAction::fromJson(o).has_value());
            o.insert(QStringLiteral("value"), 3); // number rejected (tokens are not ordinals)
            QVERIFY(!RuleAction::fromJson(o).has_value());
            for (QLatin1StringView token :
                 {ScrollInsertPositionToken::RightOfActive, ScrollInsertPositionToken::LeftOfActive,
                  ScrollInsertPositionToken::First, ScrollInsertPositionToken::Last,
                  ScrollInsertPositionToken::IntoActiveColumn}) {
                o.insert(QStringLiteral("value"), QString::fromLatin1(token));
                const auto loaded = RuleAction::fromJson(o);
                QVERIFY2(loaded.has_value(), token.data());
                QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::ScrollInsertPosition));
                const auto roundTripped = RuleAction::fromJson(loaded->toJson());
                QVERIFY2(roundTripped.has_value(), token.data());
                QCOMPARE(*roundTripped, *loaded);
            }
        }
        // SetScrollDefaultWindowHeight / OpenWindowHeight: the same
        // [0.05, 1.0] fraction as the width pair, measured against the
        // work-area height. Mirrors the width loop's bounds and slot pin.
        for (QLatin1StringView type : {ActionType::SetScrollDefaultWindowHeight, ActionType::OpenWindowHeight}) {
            rejectsMissingValue(type);
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            o.insert(QStringLiteral("value"), 0.02); // below 0.05 rejected
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 50.0); // percent-magnitude payload rejected (wire is a fraction)
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), QStringLiteral("tall")); // non-numeric rejected
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 0.05); // inclusive lower bound accepted
            QVERIFY2(RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 1.0); // inclusive upper bound accepted
            QVERIFY2(RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 0.5);
            const auto loaded = RuleAction::fromJson(o);
            QVERIFY2(loaded.has_value(), type.data());
            // Slot pin: same cross-wiring risk as the width pair — a swapped
            // constantSlot passes every bounds assertion above but not this.
            const QString expectedSlot = (type == ActionType::SetScrollDefaultWindowHeight)
                ? QString(ActionSlot::ScrollDefaultWindowHeight)
                : QString(ActionSlot::OpenWindowHeight);
            QCOMPARE(ActionRegistry::instance().slotFor(*loaded), expectedSlot);
            const auto roundTripped = RuleAction::fromJson(loaded->toJson());
            QVERIFY2(roundTripped.has_value(), type.data());
            QCOMPARE(*roundTripped, *loaded);
        }
    }
};

QTEST_GUILESS_MAIN(TestRuleActionTilingParams)
#include "test_ruleaction_tilingparams.moc"
