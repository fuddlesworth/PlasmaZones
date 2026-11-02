// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins SnapGapTracker, the daemon's record of the snapping gap-rule verdict
// each screen's snapped windows were laid out under. A verdict that moves
// within one desktop and activity is reported once, and nothing else is: not a
// repeat pass, not a desktop switch, not a screen that is new or gone, and not
// a screen another path has just laid out. Each case drives the repeat its
// rule exists for.

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariantMap>

#include "daemon/daemon/snapgaptracker.h"

using namespace PlasmaZones;
using Verdict = SnapGapTracker::Verdict;

namespace {

const QString Dp1 = QStringLiteral("DP-1");
const QString Dp2 = QStringLiteral("DP-2");
const QString InnerGapKey = QStringLiteral("InnerGap");

/// A stand-in for the daemon's resolver: each screen's verdict is whatever the
/// case last set, in desktop 1 of no activity unless the case moves it.
struct FakeVerdicts
{
    QHash<QString, Verdict> byScreen;

    void setInnerGap(const QString& screenId, int gap)
    {
        Verdict& verdict = byScreen[screenId];
        if (verdict.desktop == 0) {
            verdict.desktop = 1;
        }
        verdict.gaps.insert(InnerGapKey, gap);
    }

    void setDesktop(const QString& screenId, int desktop)
    {
        byScreen[screenId].desktop = desktop;
    }

    SnapGapTracker::Resolver resolver() const
    {
        return [this](const QString& screenId) {
            return byScreen.value(screenId);
        };
    }
};

} // namespace

class TestSnapGapTracker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void seededThenUnchanged_reportsNothing()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        verdicts.setInnerGap(Dp2, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1, Dp2}, verdicts.resolver());

        QVERIFY(tracker.takeChanges({Dp1, Dp2}, verdicts.resolver()).isEmpty());
        // The second pass with nothing moved is the repeat that must stay quiet.
        QVERIFY(tracker.takeChanges({Dp1, Dp2}, verdicts.resolver()).isEmpty());
    }

    void ruleEditMovingOneScreen_reportsThatScreenOnce()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        verdicts.setInnerGap(Dp2, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1, Dp2}, verdicts.resolver());

        verdicts.setInnerGap(Dp2, 20);
        QCOMPARE(tracker.takeChanges({Dp1, Dp2}, verdicts.resolver()), QStringList{Dp2});
        QVERIFY(tracker.takeChanges({Dp1, Dp2}, verdicts.resolver()).isEmpty());
    }

    void ruleRemovedToNoOverride_isAMove()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1}, verdicts.resolver());

        verdicts.byScreen[Dp1].gaps.clear();
        QCOMPARE(tracker.takeChanges({Dp1}, verdicts.resolver()), QStringList{Dp1});
    }

    void rebaseInSameContext_keepsAPendingMoveForTheNextTake()
    {
        // A rule edit is reconciled a turn late, and the daemon's own writes
        // diff (and so rebase) in between. That rebase must not swallow the move.
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1}, verdicts.resolver());

        verdicts.setInnerGap(Dp1, 20);
        tracker.rebase({Dp1}, verdicts.resolver());
        QVERIFY(tracker.recorded(Dp1).has_value());
        QCOMPARE(tracker.recorded(Dp1)->gaps.value(InnerGapKey).toInt(), 8);
        QCOMPARE(tracker.takeChanges({Dp1}, verdicts.resolver()), QStringList{Dp1});
    }

    void desktopSwitch_isNotAMove()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1}, verdicts.resolver());

        // Desktop 2 runs a different gap rule. The windows it shows were laid
        // out there, not under desktop 1's verdict.
        verdicts.setDesktop(Dp1, 2);
        verdicts.setInnerGap(Dp1, 20);
        tracker.rebase({Dp1}, verdicts.resolver());
        QVERIFY(tracker.takeChanges({Dp1}, verdicts.resolver()).isEmpty());

        // And a rule edit on the new desktop afterwards is one.
        verdicts.setInnerGap(Dp1, 4);
        QCOMPARE(tracker.takeChanges({Dp1}, verdicts.resolver()), QStringList{Dp1});
    }

    void contextSwitchSeenOnlyByTheTake_isNotAMove()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1}, verdicts.resolver());

        verdicts.byScreen[Dp1].activity = QStringLiteral("work");
        verdicts.setInnerGap(Dp1, 20);
        QVERIFY(tracker.takeChanges({Dp1}, verdicts.resolver()).isEmpty());
        QVERIFY(tracker.recorded(Dp1).has_value());
        QCOMPARE(tracker.recorded(Dp1)->activity, QStringLiteral("work"));
    }

    void removedScreen_losesItsRecord()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        verdicts.setInnerGap(Dp2, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1, Dp2}, verdicts.resolver());

        tracker.rebase({Dp1}, verdicts.resolver());
        QVERIFY(!tracker.recorded(Dp2).has_value());

        // The same id plugged back in under another verdict is new, not moved.
        verdicts.setInnerGap(Dp2, 20);
        QVERIFY(tracker.takeChanges({Dp1, Dp2}, verdicts.resolver()).isEmpty());
        QVERIFY(tracker.recorded(Dp2).has_value());
        QCOMPARE(tracker.recorded(Dp2)->gaps.value(InnerGapKey).toInt(), 20);
    }

    void takeDropsScreensItIsNotGiven()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        verdicts.setInnerGap(Dp2, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1, Dp2}, verdicts.resolver());

        verdicts.setInnerGap(Dp2, 20);
        QVERIFY(tracker.takeChanges({Dp1}, verdicts.resolver()).isEmpty());
        QVERIFY(!tracker.recorded(Dp2).has_value());
    }

    void newScreenInTake_isRecordedNotReported()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        verdicts.setInnerGap(Dp2, 20);
        SnapGapTracker tracker;
        tracker.rebase({Dp1}, verdicts.resolver());

        QVERIFY(tracker.takeChanges({Dp1, Dp2}, verdicts.resolver()).isEmpty());
        QVERIFY(tracker.recorded(Dp2).has_value());
    }

    void refresh_recordsScreensAnotherPathLaidOut()
    {
        // The assignment apply resnaps a screen under the current verdict, and
        // the reconcile's gap pass that follows must not restate it again.
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        verdicts.setInnerGap(Dp2, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1, Dp2}, verdicts.resolver());

        verdicts.setInnerGap(Dp1, 20);
        verdicts.setInnerGap(Dp2, 20);
        tracker.refresh({Dp1}, verdicts.resolver());
        QCOMPARE(tracker.takeChanges({Dp1, Dp2}, verdicts.resolver()), QStringList{Dp2});
    }

    void clear_forgetsEverything()
    {
        FakeVerdicts verdicts;
        verdicts.setInnerGap(Dp1, 8);
        SnapGapTracker tracker;
        tracker.rebase({Dp1}, verdicts.resolver());

        tracker.clear();
        QVERIFY(!tracker.recorded(Dp1).has_value());
        verdicts.setInnerGap(Dp1, 20);
        QVERIFY(tracker.takeChanges({Dp1}, verdicts.resolver()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSnapGapTracker)
#include "test_snap_gap_tracker.moc"
