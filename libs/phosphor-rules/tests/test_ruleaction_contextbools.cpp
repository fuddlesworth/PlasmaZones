// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The context-domain boolean family's fromJson round trips, split out of
// test_ruleaction.cpp when the fourth member (SetDragSelectorEnabled) pushed
// that file past the size ceiling. Each member must validate through the
// public fromJson boundary (not just the registry), require a bool `value`,
// and round-trip BOTH polarities losslessly — for the slot-carrying members
// the two are genuinely different verdicts (false suppresses, true FORCES
// past the global toggle), so an explicit true read back as absent would
// silently degrade the force-on half to "follow the globals".

#include <PhosphorRules/RuleAction.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

using namespace PhosphorRules;

class TestRuleActionContextBools : public QObject
{
    Q_OBJECT

private:
    /// Shared driver: the number-is-not-a-bool rejection plus the two-polarity
    /// round trip. @p expectedSlot, when non-empty, additionally pins the
    /// action's slot — a copy-pasted constantSlot would silently make two
    /// actions fight over one slot.
    void runContextBoolRoundTrip(QLatin1StringView type, const QString& expectedSlot)
    {
        // qPrintable(QString(type)) rather than type.data(): a
        // QLatin1StringView's data is not contractually NUL-terminated (it
        // happens to be today only because every ActionType constant is
        // built from a string literal).
        QJsonObject bad;
        bad.insert(QStringLiteral("type"), QString(type));
        bad.insert(QStringLiteral("value"), 1); // a number is not a bool
        QVERIFY2(!RuleAction::fromJson(bad).has_value(), qPrintable(QString(type)));

        // A MISSING `value` is refused through the same public boundary —
        // the hasBool validator's other half, pinned per family member
        // rather than only for one type at the registry tier.
        QJsonObject missing;
        missing.insert(QStringLiteral("type"), QString(type));
        QVERIFY2(!RuleAction::fromJson(missing).has_value(), qPrintable(QString(type)));

        for (const bool v : {true, false}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString(type));
            o.insert(QStringLiteral("value"), v);
            const auto action = RuleAction::fromJson(o);
            QVERIFY2(action.has_value(), qPrintable(QString(type)));
            if (!expectedSlot.isEmpty()) {
                QCOMPARE(ActionRegistry::instance().slotFor(*action), expectedSlot);
            }
            QCOMPARE(action->params.value(QStringLiteral("value")), QJsonValue(v));
            const auto roundTripped = RuleAction::fromJson(action->toJson());
            QVERIFY2(roundTripped.has_value(), qPrintable(QString(type)));
            QCOMPARE(roundTripped->params.value(QStringLiteral("value")), QJsonValue(v));
        }
    }

private Q_SLOTS:
    void testLockContext_fromJsonRoundTrip()
    {
        runContextBoolRoundTrip(ActionType::LockContext, QString());
    }

    void testDefaultLayoutAssignment_fromJsonRoundTrip()
    {
        runContextBoolRoundTrip(ActionType::DefaultLayoutAssignment, QString());
    }

    void testSetOsdEnabled_fromJsonRoundTrip()
    {
        runContextBoolRoundTrip(ActionType::SetOsdEnabled, QString(ActionSlot::OsdEnabled));
    }

    void testSetDragSelectorEnabled_fromJsonRoundTrip()
    {
        runContextBoolRoundTrip(ActionType::SetDragSelectorEnabled, QString(ActionSlot::DragSelectorEnabled));
        // Its own slot, never the OSD one.
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetDragSelectorEnabled));
        o.insert(QStringLiteral("value"), true);
        const auto action = RuleAction::fromJson(o);
        QVERIFY(action.has_value());
        QVERIFY(ActionRegistry::instance().slotFor(*action) != QString(ActionSlot::OsdEnabled));
    }
};

QTEST_GUILESS_MAIN(TestRuleActionContextBools)
#include "test_ruleaction_contextbools.moc"
