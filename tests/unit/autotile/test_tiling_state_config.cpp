// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"
#include "config/configdefaults.h"

using namespace PlasmaZones;

namespace {

/**
 * @brief Add N windows named "win0" .. "win{N-1}" to the given PhosphorTiles::TilingState.
 */
void addNumberedWindows(PhosphorTiles::TilingState& state, int count)
{
    for (int i = 0; i < count; ++i) {
        state.addWindow(QStringLiteral("win%1").arg(i));
    }
}

} // anonymous namespace

/**
 * @brief Unit tests for PhosphorTiles::TilingState configuration properties.
 *
 * Tests cover:
 * - Master count (default, set/get, clamping, signals, isMaster, master/stack lists)
 * - Split ratio (default, set/get, clamping, increase/decrease, signals)
 * - Per-window floating state (set, toggle, tiled count, lists, signals)
 */
class TestTilingStateConfig : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // masterCount
    // ═══════════════════════════════════════════════════════════════════════════

    void testMasterCount_default()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QCOMPARE(state.masterCount(), ConfigDefaults::autotileMasterCount());
    }

    void testMasterCount_setAndGet()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        addNumberedWindows(state, 5);

        state.setMasterCount(3);
        QCOMPARE(state.masterCount(), 3);
    }

    void testMasterCount_clampToMin()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        state.setMasterCount(0);
        QCOMPARE(state.masterCount(), PhosphorTiles::AutotileDefaults::MinMasterCount);

        state.setMasterCount(-5);
        QCOMPARE(state.masterCount(), PhosphorTiles::AutotileDefaults::MinMasterCount);
    }

    void testMasterCount_clampToMax()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        addNumberedWindows(state, 3);

        // masterCount clamps to MaxMasterCount (absolute limit), not window count.
        // Algorithms clamp operationally when calculating zones.
        state.setMasterCount(10);
        QCOMPARE(state.masterCount(), PhosphorTiles::AutotileDefaults::MaxMasterCount);
    }

    void testMasterCount_clampToMaxConstant()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        addNumberedWindows(state, 20);

        // With many windows, should clamp to MaxMasterCount
        state.setMasterCount(100);
        QCOMPARE(state.masterCount(), PhosphorTiles::AutotileDefaults::MaxMasterCount);
    }

    void testMasterCount_signal()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        QSignalSpy masterSpy(&state, &PhosphorTiles::TilingState::masterCountChanged);
        QSignalSpy stateSpy(&state, &PhosphorTiles::TilingState::stateChanged);

        state.setMasterCount(2);
        QCOMPARE(masterSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testMasterCount_noSignalOnSameValue()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QSignalSpy masterSpy(&state, &PhosphorTiles::TilingState::masterCountChanged);
        // Default is 1, setting to 1 again should not emit
        state.setMasterCount(1);
        QCOMPARE(masterSpy.count(), 0);
    }

    void testIsMaster_basic()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setMasterCount(1);

        QVERIFY(state.isMaster(QStringLiteral("A")));
        QVERIFY(!state.isMaster(QStringLiteral("B")));
        QVERIFY(!state.isMaster(QStringLiteral("C")));
    }

    void testIsMaster_floatingExcluded()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.setFloating(QStringLiteral("A"), true);

        // A is floating, so B becomes master
        QVERIFY(!state.isMaster(QStringLiteral("A")));
        QVERIFY(state.isMaster(QStringLiteral("B")));
    }

    void testMasterWindows_and_stackWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.addWindow(QStringLiteral("D"));
        state.setMasterCount(2);

        QCOMPARE(state.masterWindows(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
        QCOMPARE(state.stackWindows(), QStringList({QStringLiteral("C"), QStringLiteral("D")}));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // splitRatio
    // ═══════════════════════════════════════════════════════════════════════════

    void testSplitRatio_default()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QCOMPARE(state.splitRatio(), ConfigDefaults::autotileSplitRatio());
    }

    void testSplitRatio_setAndGet()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.7);
        QVERIFY(qFuzzyCompare(state.splitRatio(), 0.7));
    }

    void testSplitRatio_clampMin()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.01); // Below MinSplitRatio (0.1)
        QVERIFY(qFuzzyCompare(state.splitRatio(), PhosphorTiles::AutotileDefaults::MinSplitRatio));
    }

    void testSplitRatio_clampMax()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.99); // Above MaxSplitRatio (0.9)
        QVERIFY(qFuzzyCompare(state.splitRatio(), PhosphorTiles::AutotileDefaults::MaxSplitRatio));
    }

    void testSplitRatio_increase()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        state.increaseSplitRatio(0.1);
        QVERIFY(qFuzzyCompare(state.splitRatio(), 0.6));
    }

    void testSplitRatio_decrease()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.5);
        state.decreaseSplitRatio(0.1);
        QVERIFY(qFuzzyCompare(state.splitRatio(), 0.4));
    }

    void testSplitRatio_signal()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QSignalSpy ratioSpy(&state, &PhosphorTiles::TilingState::splitRatioChanged);
        QSignalSpy stateSpy(&state, &PhosphorTiles::TilingState::stateChanged);

        state.setSplitRatio(0.7);
        QCOMPARE(ratioSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testSplitRatio_noSignalOnSameValue()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        // Default comes from ConfigDefaults::autotileSplitRatio()
        QSignalSpy ratioSpy(&state, &PhosphorTiles::TilingState::splitRatioChanged);
        state.setSplitRatio(ConfigDefaults::autotileSplitRatio());
        QCOMPARE(ratioSpy.count(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating state
    // ═══════════════════════════════════════════════════════════════════════════

    void testFloating_setAndCheck()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QVERIFY(!state.isFloating(QStringLiteral("win1")));
        state.setFloating(QStringLiteral("win1"), true);
        QVERIFY(state.isFloating(QStringLiteral("win1")));
        state.setFloating(QStringLiteral("win1"), false);
        QVERIFY(!state.isFloating(QStringLiteral("win1")));
    }

    void testFloating_untrackedWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));

        // Setting floating on untracked window should be ignored
        state.setFloating(QStringLiteral("nonexistent"), true);
        QVERIFY(!state.isFloating(QStringLiteral("nonexistent")));
    }

    void testFloating_toggle()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        bool result = state.toggleFloating(QStringLiteral("win1"));
        QVERIFY(result);
        QVERIFY(state.isFloating(QStringLiteral("win1")));

        result = state.toggleFloating(QStringLiteral("win1"));
        QVERIFY(!result);
        QVERIFY(!state.isFloating(QStringLiteral("win1")));
    }

    void testFloating_toggleUntracked()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));

        // Toggle on untracked should return false and do nothing
        bool result = state.toggleFloating(QStringLiteral("nonexistent"));
        QVERIFY(!result);
    }

    void testFloating_tiledWindowCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        QCOMPARE(state.windowCount(), 3);
        QCOMPARE(state.tiledWindowCount(), 3);

        state.setFloating(QStringLiteral("B"), true);
        QCOMPARE(state.windowCount(), 3); // Total unchanged
        QCOMPARE(state.tiledWindowCount(), 2); // One floating

        state.setFloating(QStringLiteral("C"), true);
        QCOMPARE(state.tiledWindowCount(), 1);
    }

    void testFloating_tiledWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("B"), true);

        QCOMPARE(state.tiledWindows(), QStringList({QStringLiteral("A"), QStringLiteral("C")}));
    }

    void testFloating_floatingWindowsList()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("A"), true);
        state.setFloating(QStringLiteral("C"), true);

        QStringList floating = state.floatingWindows();
        QCOMPARE(floating.size(), 2);
        QVERIFY(floating.contains(QStringLiteral("A")));
        QVERIFY(floating.contains(QStringLiteral("C")));
    }

    void testFloating_signal()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QSignalSpy floatSpy(&state, &PhosphorTiles::TilingState::floatingChanged);
        QSignalSpy countSpy(&state, &PhosphorTiles::TilingState::windowCountChanged);
        QSignalSpy stateSpy(&state, &PhosphorTiles::TilingState::stateChanged);

        state.setFloating(QStringLiteral("win1"), true);
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(countSpy.count(), 1); // Tiled count changed
        QCOMPARE(stateSpy.count(), 1);

        // Verify signal arguments
        auto args = floatSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("win1"));
        QCOMPARE(args.at(1).toBool(), true);
    }

    void testFloating_noSignalOnSameValue()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        QSignalSpy floatSpy(&state, &PhosphorTiles::TilingState::floatingChanged);
        // Already not floating, setting to false should be no-op
        state.setFloating(QStringLiteral("win1"), false);
        QCOMPARE(floatSpy.count(), 0);
    }
};

QTEST_MAIN(TestTilingStateConfig)
#include "test_tiling_state_config.moc"
