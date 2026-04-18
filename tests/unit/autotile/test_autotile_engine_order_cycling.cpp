// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>

#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;

/**
 * @brief Tests that window order is preserved across autotile screen removal
 *        and re-addition (the "cycling" path).
 *
 * Exercises the pattern used by Daemon::updateAutotileScreens():
 *   1. Capture tiledWindowOrder() for screens leaving autotile
 *   2. Remove screens via setAutotileScreens()
 *   3. Seed order via setInitialWindowOrder() for screens re-entering
 *   4. Re-add screens via setAutotileScreens()
 *   5. Verify order is preserved after windows are re-inserted
 */
class TestAutotileEngineOrderCycling : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    /// Add windows to a screen's PhosphorTiles::TilingState (simulates KWin windowAdded events)
    void addWindowsToScreen(AutotileEngine& engine, const QString& screenId, const QStringList& windowIds)
    {
        PhosphorTiles::TilingState* state = engine.stateForScreen(screenId);
        QVERIFY(state);
        for (const QString& id : windowIds) {
            state->addWindow(id);
        }
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    // =========================================================================
    // Core cycling test: remove screen from autotile, re-add, verify order
    // =========================================================================

    void testOrderPreserved_acrossCycle()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        const QStringList originalOrder = {
            QStringLiteral("win-A"),
            QStringLiteral("win-B"),
            QStringLiteral("win-C"),
        };

        // Step 1: Enable autotile on screen, add windows
        engine.setAutotileScreens({screen});
        addWindowsToScreen(engine, screen, originalOrder);

        // Verify initial order
        QCOMPARE(engine.tiledWindowOrder(screen), originalOrder);

        // Step 2: Capture order BEFORE removing (mirrors updateAutotileScreens)
        QStringList capturedOrder = engine.tiledWindowOrder(screen);
        QVERIFY(!capturedOrder.isEmpty());

        // Step 3: Remove screen from autotile (simulates cycling to snapping)
        engine.setAutotileScreens({});
        QVERIFY(!engine.isEnabled());

        // PhosphorTiles::TilingState is destroyed — tiledWindowOrder returns empty
        QVERIFY(engine.tiledWindowOrder(screen).isEmpty());

        // Step 4: Seed the captured order, then re-add screen (simulates cycling back)
        engine.setInitialWindowOrder(screen, capturedOrder);
        engine.setAutotileScreens({screen});
        QVERIFY(engine.isEnabled());

        // Step 5: Re-insert windows (simulates KWin re-tiling after mode switch)
        addWindowsToScreen(engine, screen, originalOrder);

        // The pending initial order should have guided insertion
        QCOMPARE(engine.tiledWindowOrder(screen), originalOrder);
    }

    // =========================================================================
    // Edge case: capture from screen with no windows
    // =========================================================================

    void testCapture_emptyScreen_returnsEmpty()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("HDMI-1");

        engine.setAutotileScreens({screen});

        // No windows added — capture should be empty
        QStringList order = engine.tiledWindowOrder(screen);
        QVERIFY(order.isEmpty());
    }

    // =========================================================================
    // Edge case: seed with empty order is a no-op
    // =========================================================================

    void testSeed_emptyOrder_isNoOp()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");

        // setInitialWindowOrder with empty list should not crash or create state
        engine.setInitialWindowOrder(screen, {});

        // Screen is not in autotile — no state
        QVERIFY(engine.tiledWindowOrder(screen).isEmpty());
    }

    // =========================================================================
    // Multi-screen: cycling one screen preserves another
    // =========================================================================

    void testMultiScreen_cycleOne_preservesOther()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        const QStringList order1 = {QStringLiteral("win-1"), QStringLiteral("win-2")};
        const QStringList order2 = {QStringLiteral("win-3"), QStringLiteral("win-4")};

        // Both screens in autotile
        engine.setAutotileScreens({screen1, screen2});
        addWindowsToScreen(engine, screen1, order1);
        addWindowsToScreen(engine, screen2, order2);

        // Capture order for screen1 before removing it
        QStringList captured1 = engine.tiledWindowOrder(screen1);

        // Remove screen1 only — screen2 stays
        engine.setAutotileScreens({screen2});

        // Screen2 windows should be untouched
        QCOMPARE(engine.tiledWindowOrder(screen2), order2);

        // Re-add screen1 with seeded order
        engine.setInitialWindowOrder(screen1, captured1);
        engine.setAutotileScreens({screen1, screen2});
        addWindowsToScreen(engine, screen1, order1);

        QCOMPARE(engine.tiledWindowOrder(screen1), order1);
        QCOMPARE(engine.tiledWindowOrder(screen2), order2);
    }

    // =========================================================================
    // Seed is ignored if PhosphorTiles::TilingState already has windows
    // =========================================================================

    void testSeed_ignoredWhenWindowsExist()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        const QStringList existingOrder = {QStringLiteral("win-X"), QStringLiteral("win-Y")};
        const QStringList seedOrder = {QStringLiteral("win-Y"), QStringLiteral("win-X")};

        engine.setAutotileScreens({screen});
        addWindowsToScreen(engine, screen, existingOrder);

        // Seed should be ignored because state already has windows
        engine.setInitialWindowOrder(screen, seedOrder);

        QCOMPARE(engine.tiledWindowOrder(screen), existingOrder);
    }

    // =========================================================================
    // Query for non-autotile screen returns empty
    // =========================================================================

    void testTiledWindowOrder_unknownScreen_returnsEmpty()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        QVERIFY(engine.tiledWindowOrder(QStringLiteral("nonexistent")).isEmpty());
    }
};

QTEST_MAIN(TestAutotileEngineOrderCycling)
#include "test_autotile_engine_order_cycling.moc"
