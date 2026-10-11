// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WindowFlagEdges: what each KWin flag change edge runs. Before this table only
// four flags had an edge, and those ran the eviction alone, so a rule scoped on
// a flag that changed after the window opened kept matching the value it
// opened with, on both the daemon's and the effect's side.

#include "plasmazoneseffect/windowflagedges.h"

#include <QtTest>

using namespace PlasmaZones::WindowFlagEdges;

namespace {

void addEveryFlag()
{
    QTest::addColumn<Flag>("flag");
    QTest::newRow("keep above") << Flag::KeepAbove;
    QTest::newRow("keep below") << Flag::KeepBelow;
    QTest::newRow("skip taskbar") << Flag::SkipTaskbar;
    QTest::newRow("skip pager") << Flag::SkipPager;
    QTest::newRow("skip switcher") << Flag::SkipSwitcher;
    QTest::newRow("transient") << Flag::Transient;
    QTest::newRow("modal") << Flag::Modal;
    QTest::newRow("maximizable") << Flag::Maximizable;
    QTest::newRow("decoration") << Flag::Decoration;
}

} // namespace

class TestWindowFlagEdges : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Every flag is a rule field in the daemon's registry, so every edge
    /// refreshes it.
    void everyFlagPushes_data()
    {
        addEveryFlag();
    }
    void everyFlagPushes()
    {
        QFETCH(Flag, flag);
        QVERIFY(actionsFor(flag).pushMetadata);
    }

    /// The layer reconcile would put a SetWindowLayer rule back over the
    /// user's own keep-above or keep-below toggle.
    void layerFlagsNeverInvalidate()
    {
        QVERIFY(!actionsFor(Flag::KeepAbove).invalidateRules);
        QVERIFY(!actionsFor(Flag::KeepBelow).invalidateRules);
    }

    /// SetHideTitleBar writes the decoration flag it would then read.
    void decorationNeverInvalidates()
    {
        QVERIFY(!actionsFor(Flag::Decoration).invalidateRules);
    }

    /// The eviction keeps its four structural filters, so keep-below or
    /// skip-taskbar do not start evicting tiles.
    void structuralFiltersEvict_data()
    {
        QTest::addColumn<Flag>("flag");
        QTest::addColumn<bool>("evicts");
        QTest::newRow("keep above") << Flag::KeepAbove << true;
        QTest::newRow("keep below") << Flag::KeepBelow << false;
        QTest::newRow("skip taskbar") << Flag::SkipTaskbar << false;
        QTest::newRow("skip pager") << Flag::SkipPager << false;
        QTest::newRow("skip switcher") << Flag::SkipSwitcher << true;
        QTest::newRow("transient") << Flag::Transient << true;
        QTest::newRow("modal") << Flag::Modal << true;
        QTest::newRow("maximizable") << Flag::Maximizable << false;
        QTest::newRow("decoration") << Flag::Decoration << false;
    }
    void structuralFiltersEvict()
    {
        QFETCH(Flag, flag);
        QFETCH(bool, evicts);
        QCOMPARE(actionsFor(flag).reevaluateEligibility, evicts);
    }

    /// The rest drop the effect's cached verdicts so a rule scoped on them
    /// re-resolves at once.
    void ruleFieldsInvalidate_data()
    {
        QTest::addColumn<Flag>("flag");
        QTest::newRow("skip taskbar") << Flag::SkipTaskbar;
        QTest::newRow("skip pager") << Flag::SkipPager;
        QTest::newRow("skip switcher") << Flag::SkipSwitcher;
        QTest::newRow("transient") << Flag::Transient;
        QTest::newRow("modal") << Flag::Modal;
        QTest::newRow("maximizable") << Flag::Maximizable;
    }
    void ruleFieldsInvalidate()
    {
        QFETCH(Flag, flag);
        QVERIFY(actionsFor(flag).invalidateRules);
    }
};

QTEST_MAIN(TestWindowFlagEdges)
#include "test_window_flag_edges.moc"
