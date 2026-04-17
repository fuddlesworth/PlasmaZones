// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QScopedPointer>

#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"
#include "config/configdefaults.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for PhosphorTiles::TilingState focus tracking, serialization, clear, and utility methods.
 *
 * Tests cover:
 * - Focus tracking (default, set/get, untracked, clear, signals, focusedTiledIndex)
 * - Serialization roundtrip (toJson / fromJson)
 * - clear() method (reset, signals, no-op when default, preserves screenName)
 * - calculatedZones (set/get, default)
 * - windowIndex / containsWindow / windowPosition
 */
class TestTilingStateSerial : public QObject
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Serialization roundtrip
    // ═══════════════════════════════════════════════════════════════════════════

    void testSerialization_roundtrip()
    {
        PhosphorTiles::TilingState state(QStringLiteral("monitor1"));
        state.addWindow(QStringLiteral("A"));
        state.addWindow(QStringLiteral("B"));
        state.addWindow(QStringLiteral("C"));
        state.setFloating(QStringLiteral("B"), true);
        state.setFocusedWindow(QStringLiteral("A"));
        state.setMasterCount(2);
        state.setSplitRatio(0.7);

        QJsonObject json = state.toJson();

        QScopedPointer<PhosphorTiles::TilingState> restored(PhosphorTiles::TilingState::fromJson(json));
        QVERIFY(!restored.isNull());

        QCOMPARE(restored->screenId(), QStringLiteral("monitor1"));
        QCOMPARE(restored->windowCount(), 3);
        QCOMPARE(restored->windowOrder(), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QVERIFY(restored->isFloating(QStringLiteral("B")));
        QVERIFY(!restored->isFloating(QStringLiteral("A")));
        QCOMPARE(restored->focusedWindow(), QStringLiteral("A"));
        QCOMPARE(restored->masterCount(), 2);
        QVERIFY(qFuzzyCompare(restored->splitRatio(), 0.7));
    }

    void testSerialization_emptyState()
    {
        PhosphorTiles::TilingState state(QStringLiteral("empty"));
        QJsonObject json = state.toJson();

        QScopedPointer<PhosphorTiles::TilingState> restored(PhosphorTiles::TilingState::fromJson(json));
        QVERIFY(!restored.isNull());
        QCOMPARE(restored->screenId(), QStringLiteral("empty"));
        QCOMPARE(restored->windowCount(), 0);
        QCOMPARE(restored->masterCount(), ConfigDefaults::autotileMasterCount());
        QVERIFY(qFuzzyCompare(restored->splitRatio(), ConfigDefaults::autotileSplitRatio()));
    }

    void testSerialization_invalidJson()
    {
        // Missing screenName should return nullptr
        QJsonObject invalidJson;
        QScopedPointer<PhosphorTiles::TilingState> result(PhosphorTiles::TilingState::fromJson(invalidJson));
        QVERIFY(result.isNull());
    }

    void testSerialization_clampsBadValues()
    {
        QJsonObject json;
        json[QStringLiteral("screenName")] = QStringLiteral("test");
        json[QStringLiteral("windowOrder")] = QJsonArray({QStringLiteral("A"), QStringLiteral("B")});
        json[QStringLiteral("floatingWindows")] = QJsonArray();
        json[QStringLiteral("focusedWindow")] = QString();
        json[QStringLiteral("masterCount")] = 99; // Way too high
        json[QStringLiteral("splitRatio")] = 5.0; // Way too high

        QScopedPointer<PhosphorTiles::TilingState> restored(PhosphorTiles::TilingState::fromJson(json));
        QVERIFY(!restored.isNull());

        // masterCount should be clamped to MaxMasterCount (absolute limit, not window count)
        QCOMPARE(restored->masterCount(), PhosphorTiles::AutotileDefaults::MaxMasterCount);
        // splitRatio should be clamped to MaxSplitRatio (0.9)
        QVERIFY(qFuzzyCompare(restored->splitRatio(), PhosphorTiles::AutotileDefaults::MaxSplitRatio));
    }

    void testSerialization_invalidFloatingIgnored()
    {
        QJsonObject json;
        json[QStringLiteral("screenName")] = QStringLiteral("test");
        json[QStringLiteral("windowOrder")] = QJsonArray({QStringLiteral("A")});
        // Reference a window not in windowOrder
        json[QStringLiteral("floatingWindows")] = QJsonArray({QStringLiteral("nonexistent")});
        json[QStringLiteral("focusedWindow")] = QStringLiteral("alsoNonexistent");
        json[QStringLiteral("masterCount")] = 1;
        json[QStringLiteral("splitRatio")] = 0.5;

        QScopedPointer<PhosphorTiles::TilingState> restored(PhosphorTiles::TilingState::fromJson(json));
        QVERIFY(!restored.isNull());

        // Invalid floating window should be ignored
        QCOMPARE(restored->floatingWindows().size(), 0);
        // Invalid focused window should be ignored
        QVERIFY(restored->focusedWindow().isEmpty());
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

QTEST_MAIN(TestTilingStateSerial)
#include "test_tiling_state_serial.moc"
