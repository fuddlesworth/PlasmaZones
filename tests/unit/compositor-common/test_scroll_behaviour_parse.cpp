// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scrollEffectBehaviour value parser's three-way contract
// (compositor/scrollbehaviourparse.h). The distinction between an ABSENT key
// (a legitimate publish, engaged empty set) and a MALFORMED one (disengaged
// optional) is what lets the axis arm keep its live membership on garbage
// while the behaviour toggles fall safe to off — a parse that conflated the
// two silently re-laid every vertical strip horizontally, with the only
// symptom on screen.

#include "compositor/scrollbehaviourparse.h"

#include <QDBusVariant>
#include <QTest>

using PlasmaZones::ScrollBehaviourParse::parseIdList;

class TestScrollBehaviourParse : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// ABSENT: an invalid QVariant is a legitimate publish with the key
    /// unresolved — engaged, empty, and no warning.
    void absentReadsAsEngagedEmpty()
    {
        QStringList warnings;
        const auto result = parseIdList(QVariant(), QLatin1String("verticalAxis"), warnings);
        QVERIFY(result.has_value());
        QVERIFY(result->isEmpty());
        QVERIFY(warnings.isEmpty());
    }

    /// MALFORMED: a present value that is not a string list disengages, so
    /// the caller can pick its per-key fallback, and it warns — a wire
    /// regression must not be indistinguishable from a legitimately-off
    /// session.
    void malformedDisengagesWithAWarning()
    {
        QStringList warnings;
        const auto result = parseIdList(QVariant(42), QLatin1String("verticalAxis"), warnings);
        QVERIFY(!result.has_value());
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(QLatin1String("verticalAxis")));
    }

    /// VALID: de-duplicated membership; a QDBusVariant wrapper (a signal
    /// delivered without a registered argument type) is unwrapped first.
    void validListParsesThroughTheWrapper()
    {
        QStringList warnings;
        const QStringList screens{QStringLiteral("S1"), QStringLiteral("S2"), QStringLiteral("S1")};
        const auto direct = parseIdList(QVariant(screens), QLatin1String("cropStraddlers"), warnings);
        QVERIFY(direct.has_value());
        QCOMPARE(*direct, (QSet<QString>{QStringLiteral("S1"), QStringLiteral("S2")}));

        const auto wrapped = parseIdList(QVariant::fromValue(QDBusVariant(QVariant(screens))),
                                         QLatin1String("cropStraddlers"), warnings);
        QVERIFY(wrapped.has_value());
        QCOMPARE(*wrapped, *direct);
        QVERIFY(warnings.isEmpty());
    }

    /// Empty screen ids are dropped WITH a warning, and the surviving ids
    /// still parse — a partial drop is not a rejection.
    void emptyIdsAreDroppedNotFatal()
    {
        QStringList warnings;
        const QStringList screens{QStringLiteral("S1"), QString(), QStringLiteral("S2")};
        const auto result = parseIdList(QVariant(screens), QLatin1String("focusFollowsMouse"), warnings);
        QVERIFY(result.has_value());
        QCOMPARE(*result, (QSet<QString>{QStringLiteral("S1"), QStringLiteral("S2")}));
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(QLatin1String("focusFollowsMouse")));
    }

    /// The scroll-cap block list is the one key holding WINDOW ids rather than
    /// screen ids, and it parses through the same path — which is the point of
    /// the parser not naming either. Pinned because its fail direction is the
    /// opposite of the axis key's: absent or malformed must read as "refuse
    /// nothing", since the caller turns membership into a REFUSED focus and a
    /// wire hiccup that refused everything would look like focus-follows-mouse
    /// being broken rather than off.
    void focusScrollBlockedWindowsParsesAndFailsOpen()
    {
        QStringList warnings;
        const QLatin1String key("focusScrollBlockedWindows");

        const auto absent = parseIdList(QVariant(), key, warnings);
        QVERIFY(absent.has_value());
        QVERIFY(absent->isEmpty());
        QVERIFY(warnings.isEmpty());

        const QStringList windows{QStringLiteral("konsole|1"), QStringLiteral("firefox|2")};
        const auto parsed = parseIdList(QVariant(windows), key, warnings);
        QVERIFY(parsed.has_value());
        QCOMPARE(*parsed, (QSet<QString>{QStringLiteral("konsole|1"), QStringLiteral("firefox|2")}));
        QVERIFY(warnings.isEmpty());

        // Malformed disengages, and the CALLER's value_or supplies the empty
        // set — so both unreadable directions end at "refuse nothing".
        const auto malformed = parseIdList(QVariant(42), key, warnings);
        QVERIFY(!malformed.has_value());
        QVERIFY(malformed.value_or(QSet<QString>()).isEmpty());
        QCOMPARE(warnings.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestScrollBehaviourParse)
#include "test_scroll_behaviour_parse.moc"
