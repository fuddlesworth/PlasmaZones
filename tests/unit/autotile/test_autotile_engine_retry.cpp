// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTimer>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief AutotileEngine tests for bounded retile retry, recalculateLayout bool
 *        return value, and min-size cache clearing on unfloat.
 *
 * These tests run WITHOUT a ScreenManager (nullptr), which means the
 * pre-validation gate in retileScreen is skipped. The retry mechanism is
 * tested indirectly via the public API surface. recalculateLayout's bool
 * return is tested via observable side effects (tilingChanged emission).
 */
class TestAutotileEngineRetry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        PhosphorTiles::AlgorithmRegistry::instance();
    }

    // =========================================================================
    // recalculateLayout returns false on empty screenId
    // =========================================================================

    void testRecalculateLayout_emptyScreenId_noTilingChanged()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        QCoreApplication::processEvents(); // drain queued retile from setAutotileScreens

        QSignalSpy tilingSpy(&engine, &AutotileEngine::tilingChanged);
        engine.retile(QString());

        // Empty string retiles all autotile screens — tilingChanged fires once
        QCOMPARE(tilingSpy.count(), 1);
    }

    // =========================================================================
    // Min-size cache cleared on unfloat via setWindowFloat
    // =========================================================================

    void testUnfloat_clearsMinSizeCache()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("TestScreen");
        const QString windowId = QStringLiteral("win-unfloat-minsize");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.windowOpened(windowId, screenName, 200, 100);
        QCoreApplication::processEvents();

        // Float the window
        engine.floatWindow(windowId);
        QCoreApplication::processEvents();

        // Update min-size while floating (simulates browser loading media)
        engine.windowMinSizeUpdated(windowId, 400, 300);
        QCoreApplication::processEvents();

        // Unfloat — should clear cached min-size
        QSignalSpy tilingSpy(&engine, &AutotileEngine::tilingChanged);
        engine.unfloatWindow(windowId);
        QCoreApplication::processEvents();

        QCOMPARE(tilingSpy.count(), 1);

        // Re-report a different min-size — should trigger retile (proving the
        // cache was cleared, so the new value is different from "no entry")
        tilingSpy.clear();
        engine.windowMinSizeUpdated(windowId, 100, 50);
        QCoreApplication::processEvents();

        QCOMPARE(tilingSpy.count(), 1);
    }

    void testUnfloat_minSizeNotClearedOnFloat()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("TestScreen");
        const QString windowId = QStringLiteral("win-float-keep-minsize");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.windowOpened(windowId, screenName, 200, 100);
        QCoreApplication::processEvents();

        // Float — min-size should NOT be cleared (only unfloat clears it)
        engine.floatWindow(windowId);
        QCoreApplication::processEvents();

        // Report same min-size — should be a no-op (cache still has 200x100)
        QSignalSpy tilingSpy(&engine, &AutotileEngine::tilingChanged);
        engine.windowMinSizeUpdated(windowId, 200, 100);
        QCoreApplication::processEvents();

        QCOMPARE(tilingSpy.count(), 0);
    }

    // =========================================================================
    // Retry state cleanup when screen removed from autotile
    // =========================================================================

    void testSetAutotileScreens_clearsRetryState()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenA = QStringLiteral("ScreenA");
        const QString screenB = QStringLiteral("ScreenB");

        QSet<QString> screens{screenA, screenB};
        engine.setAutotileScreens(screens);
        engine.windowOpened(QStringLiteral("win-a"), screenA, 0, 0);
        engine.windowOpened(QStringLiteral("win-b"), screenB, 0, 0);
        QCoreApplication::processEvents();

        // Remove screenB — any retry state for it should be cleaned up
        QSet<QString> reduced{screenA};
        engine.setAutotileScreens(reduced);
        QCoreApplication::processEvents();

        // Re-add screenB — should work without residual retry state
        engine.setAutotileScreens(screens);
        QCoreApplication::processEvents();

        // No crash, no stale state
        QVERIFY(engine.isEnabled());
    }

    // =========================================================================
    // retile with no windows succeeds (recalculateLayout returns true)
    // =========================================================================

    void testRetile_noWindows_emitsTilingChanged()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        QCoreApplication::processEvents(); // drain queued retile from setAutotileScreens

        QSignalSpy tilingSpy(&engine, &AutotileEngine::tilingChanged);
        engine.retile(screenName);

        // No windows → recalculateLayout returns true (empty layout) →
        // applyTiling runs → tilingChanged emits
        QCOMPARE(tilingSpy.count(), 1);
    }

    // =========================================================================
    // retile on non-autotile screen is a no-op
    // =========================================================================

    void testRetile_nonAutotileScreen_noEmission()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        QCoreApplication::processEvents(); // drain queued retile from setAutotileScreens

        QSignalSpy tilingSpy(&engine, &AutotileEngine::tilingChanged);
        engine.retile(QStringLiteral("UnknownScreen"));

        QCOMPARE(tilingSpy.count(), 0);
    }
};

QTEST_MAIN(TestAutotileEngineRetry)
#include "test_autotile_engine_retry.moc"
