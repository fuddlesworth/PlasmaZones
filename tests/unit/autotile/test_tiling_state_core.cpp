// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QScopedPointer>

#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"
#include "config/configdefaults.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for PhosphorTiles::TilingState focus tracking, script-state
 *        sanitizing, clear, and utility methods.
 *
 * Tests cover:
 * - Focus tracking (default, set/get, untracked, clear, signals, focusedTiledIndex)
 * - sanitizeScriptState, the script trust boundary (non-finite, byte/depth/key/array caps)
 * - clear() method (reset, signals, no-op when default, preserves screenName)
 * - calculatedZones (set/get, default)
 * - windowIndex / containsWindow / windowPosition
 */
class TestTilingStateCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // Focus tracking
    // ═══════════════════════════════════════════════════════════════════════════

    void testFocusedWindow_default()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(state.focusedWindow().isEmpty());
    }

    void testFocusedWindow_setAndGet()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(state.focusedWindow(), QStringLiteral("A"));

        state.setFocusedWindow(QStringLiteral("B"));
        QCOMPARE(state.focusedWindow(), QStringLiteral("B"));
    }

    void testFocusedWindow_untrackedIgnored()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        // Setting focus to untracked window should be ignored
        state.setFocusedWindow(QStringLiteral("nonexistent"));
        QCOMPARE(state.focusedWindow(), QStringLiteral("A"));
    }

    void testFocusedWindow_clearFocus()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        // Setting empty string clears focus
        state.setFocusedWindow(QString());
        QVERIFY(state.focusedWindow().isEmpty());
    }

    void testFocusedWindow_signal()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QSignalSpy focusSpy(&state, &PhosphorTiles::TilingState::focusedWindowChanged);
        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(focusSpy.count(), 1);
    }

    void testFocusedWindow_noSignalOnSameValue()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));

        QSignalSpy focusSpy(&state, &PhosphorTiles::TilingState::focusedWindowChanged);
        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(focusSpy.count(), 0);
    }

    void testFocusedTiledIndex_basic()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));

        state.setFocusedWindow(QStringLiteral("B"));
        QCOMPARE(state.focusedTiledIndex(), 1);

        state.setFocusedWindow(QStringLiteral("A"));
        QCOMPARE(state.focusedTiledIndex(), 0);
    }

    void testFocusedTiledIndex_noFocus()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QCOMPARE(state.focusedTiledIndex(), -1);
    }

    void testFocusedTiledIndex_floatingFocused()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.setFocusedWindow(QStringLiteral("A"));
        state.setFloating(QStringLiteral("A"), true);

        // Focused window is floating, so focusedTiledIndex returns -1
        QCOMPARE(state.focusedTiledIndex(), -1);
    }

    void testFocusedTiledIndex_skipsFloating()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B")); // will be floating
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("B"), true);

        // Tiled windows: [A, C], C is at tiled index 1
        state.setFocusedWindow(QStringLiteral("C"));
        QCOMPARE(state.focusedTiledIndex(), 1);
    }

    // sanitizeScriptState drops non-finite (NaN/±Inf) numeric leaves while keeping
    // the finite siblings — the script trust boundary's NaN guard.
    void testScriptState_sanitizeDropsNonFinite()
    {
        QJsonObject bag;
        bag[QStringLiteral("good")] = 1.5;
        bag[QStringLiteral("inf")] = QJsonValue(qInf());
        bag[QStringLiteral("ninf")] = QJsonValue(-qInf());
        bag[QStringLiteral("nan")] = QJsonValue(qQNaN());

        const QJsonObject cleaned = PhosphorTiles::TilingState::sanitizeScriptState(bag);
        QVERIFY(cleaned.contains(QStringLiteral("good")));
        QVERIFY(qFuzzyCompare(cleaned.value(QStringLiteral("good")).toDouble(), 1.5));
        QVERIFY(!cleaned.contains(QStringLiteral("inf")));
        QVERIFY(!cleaned.contains(QStringLiteral("ninf")));
        QVERIFY(!cleaned.contains(QStringLiteral("nan")));
    }

    // sanitizeScriptState drops the entire bag when its serialized size exceeds the
    // byte cap (ScriptStateMaxBytes = 64 KiB).
    void testScriptState_sanitizeByteCapDropsBag()
    {
        QJsonObject bag;
        bag[QStringLiteral("blob")] = QString(70 * 1024, QLatin1Char('x')); // > 64 KiB
        const QJsonObject cleaned = PhosphorTiles::TilingState::sanitizeScriptState(bag);
        QVERIFY(cleaned.isEmpty());
    }

    // sanitizeScriptState prunes nesting deeper than ScriptStateMaxDepth (16) while
    // preserving the shallow keys above the cap.
    void testScriptState_sanitizeDepthCapPrunes()
    {
        // Build a chain nested well past the depth cap under a sibling shallow key.
        QJsonObject deep{{QStringLiteral("leaf"), 1.0}};
        for (int i = 0; i < 20; ++i) {
            deep = QJsonObject{{QStringLiteral("n"), deep}};
        }
        QJsonObject bag;
        bag[QStringLiteral("shallow")] = 2.0;
        bag[QStringLiteral("deep")] = deep;

        const QJsonObject cleaned = PhosphorTiles::TilingState::sanitizeScriptState(bag);
        // The shallow sibling survives; the over-deep chain is pruned to a bounded
        // depth rather than retained in full or dropping the whole bag.
        QVERIFY(cleaned.contains(QStringLiteral("shallow")));
        int depth = 0;
        QJsonObject cursor = cleaned.value(QStringLiteral("deep")).toObject();
        while (cursor.contains(QStringLiteral("n"))) {
            cursor = cursor.value(QStringLiteral("n")).toObject();
            ++depth;
        }
        QVERIFY(depth <= PhosphorTiles::AutotileDefaults::ScriptStateMaxDepth);
    }

    // sanitizeScriptState truncates an object with more keys than ScriptStateMaxKeys
    // (4096) rather than dropping the whole bag or retaining all keys. Keys/values
    // are kept tiny so the result stays under the 64 KiB byte cap.
    void testScriptState_sanitizeKeyCapTruncates()
    {
        QJsonObject bag;
        for (int i = 0; i < 4200; ++i) { // > 4096 cap
            bag[QStringLiteral("k%1").arg(i)] = i;
        }
        const QJsonObject cleaned = PhosphorTiles::TilingState::sanitizeScriptState(bag);
        // Truncated to the cap, but not dropped entirely (byte cap not hit).
        QVERIFY(!cleaned.isEmpty());
        QVERIFY(cleaned.size() <= PhosphorTiles::AutotileDefaults::ScriptStateMaxKeys);
        QVERIFY(cleaned.size() < bag.size());
    }

    // The array branch counts elements against the same key budget, so a flat array
    // longer than ScriptStateMaxKeys is truncated rather than materialized in full.
    void testScriptState_sanitizeArrayElementBudget()
    {
        QJsonArray big;
        for (int i = 0; i < 5000; ++i) { // > 4096 cap
            big.append(i);
        }
        QJsonObject bag;
        bag[QStringLiteral("arr")] = big;
        const QJsonObject cleaned = PhosphorTiles::TilingState::sanitizeScriptState(bag);
        QVERIFY(cleaned.contains(QStringLiteral("arr")));
        const QJsonArray out = cleaned.value(QStringLiteral("arr")).toArray();
        QVERIFY(out.size() <= PhosphorTiles::AutotileDefaults::ScriptStateMaxKeys);
        QVERIFY(out.size() < big.size());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // clear()
    // ═══════════════════════════════════════════════════════════════════════════

    void testClear_resetsAll()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.setFloating(QStringLiteral("A"), true);
        state.setFocusedWindow(QStringLiteral("B"));
        state.setMasterCount(2);
        state.setSplitRatio(0.8);

        state.clear();

        QCOMPARE(state.windowCount(), 0);
        QCOMPARE(state.tiledWindowCount(), 0);
        QVERIFY(state.windowOrder().isEmpty());
        QVERIFY(state.floatingWindows().isEmpty());
        QVERIFY(state.focusedWindow().isEmpty());
        QCOMPARE(state.masterCount(), ConfigDefaults::autotileMasterCount());
        QVERIFY(qFuzzyCompare(state.splitRatio(), ConfigDefaults::autotileSplitRatio()));
    }

    void testClear_emitsSignals()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.setFocusedWindow(QStringLiteral("A"));
        state.setMasterCount(3);
        state.setSplitRatio(0.8);

        QSignalSpy countSpy(&state, &PhosphorTiles::TilingState::windowCountChanged);
        QSignalSpy focusSpy(&state, &PhosphorTiles::TilingState::focusedWindowChanged);
        QSignalSpy masterSpy(&state, &PhosphorTiles::TilingState::masterCountChanged);
        QSignalSpy ratioSpy(&state, &PhosphorTiles::TilingState::splitRatioChanged);
        QSignalSpy stateSpy(&state, &PhosphorTiles::TilingState::stateChanged);

        state.clear();

        QCOMPARE(countSpy.count(), 1);
        // Per-field signal emission: only signals for fields that actually
        // changed from defaults are emitted.
        QCOMPARE(focusSpy.count(), 1);
        QCOMPARE(masterSpy.count(), 1);
        QCOMPARE(ratioSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testClear_noSignalIfAlreadyDefault()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        // State is already at defaults — clear() returns early, no signals

        QSignalSpy stateSpy(&state, &PhosphorTiles::TilingState::stateChanged);
        state.clear();
        QCOMPARE(stateSpy.count(), 0);
    }

    void testClear_preservesScreenName()
    {
        PhosphorTiles::TilingState state(QStringLiteral("myScreen"));
        state.addWindow(QStringLiteral("A"));
        state.clear();

        // Screen name is immutable
        QCOMPARE(state.screenId(), QStringLiteral("myScreen"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Calculated zones
    // ═══════════════════════════════════════════════════════════════════════════

    void testCalculatedZones_setAndGet()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVector<QRect> zones = {QRect(0, 0, 960, 1080), QRect(960, 0, 960, 1080)};

        state.setCalculatedZones(zones);
        QCOMPARE(state.calculatedZones(), zones);
    }

    void testCalculatedZones_defaultEmpty()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(state.calculatedZones().isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // windowIndex / containsWindow / windowPosition
    // ═══════════════════════════════════════════════════════════════════════════

    void testWindowIndex_basic()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));

        QCOMPARE(state.windowIndex(QStringLiteral("A")), 0);
        QCOMPARE(state.windowIndex(QStringLiteral("B")), 1);
        QCOMPARE(state.windowIndex(QStringLiteral("C")), -1);
    }

    void testWindowPosition_aliasForIndex()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("X"));

        QCOMPARE(state.windowPosition(QStringLiteral("X")), state.windowIndex(QStringLiteral("X")));
        QCOMPARE(state.windowPosition(QStringLiteral("nope")), -1);
    }

    void testContainsWindow_basic()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("A"));

        QVERIFY(state.containsWindow(QStringLiteral("A")));
        QVERIFY(!state.containsWindow(QStringLiteral("B")));
    }
};

QTEST_MAIN(TestTilingStateCore)
#include "test_tiling_state_core.moc"
