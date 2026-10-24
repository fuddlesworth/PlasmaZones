// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_stack_decisions.cpp
 * @brief The overlap restack of a tile batch (tilinghandler/stackdecisions.h):
 *        each group takes its declared order at its lowest member's slot,
 *        against the stacking order as it is when the batch completes, and
 *        every other window keeps its place (F329, F497).
 */

#include "tilinghandler/stackdecisions.h"

#include <QString>
#include <QStringList>
#include <QTest>

using PlasmaZones::StackDecisions::overlapRaiseSequence;
using PlasmaZones::StackDecisions::SavedOrderReplay;
using PlasmaZones::StackDecisions::SavedOrderWindow;

Q_DECLARE_METATYPE(PlasmaZones::StackDecisions::SavedOrderWindow)

namespace {
/// The stacking order after raising @p sequence, in order, on @p current.
QStringList afterRaises(QStringList current, const QStringList& sequence)
{
    for (const QString& key : sequence) {
        current.removeAll(key);
        current.append(key);
    }
    return current;
}
QStringList keys(const char* spaced)
{
    return QString::fromLatin1(spaced).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}
} // namespace

class TestStackDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void groupTakesItsLowestMembersSlot()
    {
        const QStringList current = keys("a g2 b g1 c");
        const QStringList sequence = overlapRaiseSequence<QString>(current, {keys("g1 g2")});
        QCOMPARE(sequence, keys("g1 g2 b c"));
        QCOMPARE(afterRaises(current, sequence), keys("a g1 g2 b c"));
    }

    // A window raised by the user during the cascade is above the group's
    // slot when the batch completes, and stays on top.
    void userRaisedWindowStaysOnTop()
    {
        const QStringList current = keys("g1 f g2 t");
        const QStringList sequence = overlapRaiseSequence<QString>(current, {keys("g1 g2")});
        QCOMPARE(afterRaises(current, sequence), keys("g1 g2 f t"));
    }

    // A batch with no overlap group touches the stacking not at all.
    void noGroupsRaisesNothing()
    {
        QVERIFY(overlapRaiseSequence<QString>(keys("a b c"), {}).isEmpty());
    }

    void twoGroupsOnTwoScreens()
    {
        const QStringList current = keys("x h2 a g2 h1 b g1 y");
        const QStringList sequence = overlapRaiseSequence<QString>(current, {keys("g1 g2"), keys("h1 h2")});
        QCOMPARE(afterRaises(current, sequence), keys("x h1 h2 a g1 g2 b y"));
    }

    // A member that is gone is ignored; a group of one live member still
    // places it, and a group with none places nothing.
    void deadMembersAreSkipped()
    {
        const QStringList current = keys("a g1 b");
        QCOMPARE(afterRaises(current, overlapRaiseSequence<QString>(current, {keys("g1 gone")})), keys("a g1 b"));
        QVERIFY(overlapRaiseSequence<QString>(current, {keys("gone also-gone")}).isEmpty());
    }

    // Which batch replays and spends a screen's saved stacking order: only the
    // one of the context it was saved in. A sticky window in view from
    // another desktop does not make that desktop's batch the saved context's
    // while a window living on one desktop alone is still open (F444).
    void savedOrderReplay_data()
    {
        QTest::addColumn<QList<SavedOrderWindow>>("windows");
        QTest::addColumn<bool>("raise");
        QTest::addColumn<bool>("spent");

        const SavedOrderWindow homeInView{true, true, false};
        const SavedOrderWindow homeAway{true, false, false};
        const SavedOrderWindow stickyInView{true, true, true};
        const SavedOrderWindow stickyAway{true, false, true};
        const SavedOrderWindow closed{false, false, false};

        QTest::newRow("saved context") << QList<SavedOrderWindow>{homeInView, stickyInView} << true << true;
        QTest::newRow("other context, sticky in view")
            << QList<SavedOrderWindow>{homeAway, stickyInView} << false << false;
        QTest::newRow("other context, nothing in view")
            << QList<SavedOrderWindow>{homeAway, stickyAway} << false << false;
        QTest::newRow("only sticky windows, in view")
            << QList<SavedOrderWindow>{stickyInView, stickyAway} << true << true;
        QTest::newRow("only sticky windows, none in view") << QList<SavedOrderWindow>{stickyAway} << false << false;
        QTest::newRow("sticky in view, its home sibling closed")
            << QList<SavedOrderWindow>{closed, stickyInView} << true << true;
        QTest::newRow("all closed") << QList<SavedOrderWindow>{closed, closed} << false << true;
        QTest::newRow("empty order") << QList<SavedOrderWindow>{} << false << true;
    }

    void savedOrderReplay()
    {
        QFETCH(QList<SavedOrderWindow>, windows);
        QFETCH(bool, raise);
        QFETCH(bool, spent);
        const SavedOrderReplay replay = PlasmaZones::StackDecisions::savedOrderReplay(windows);
        QCOMPARE(replay.raise, raise);
        QCOMPARE(replay.spent, spent);
    }
};

QTEST_APPLESS_MAIN(TestStackDecisions)
#include "test_stack_decisions.moc"
