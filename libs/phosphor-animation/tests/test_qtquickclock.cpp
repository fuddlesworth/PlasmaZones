// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/QtQuickClock.h>

#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <type_traits>

using PhosphorAnimation::IMotionClock;
using PhosphorAnimation::QtQuickClock;

class TestQtQuickClock : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Compile-time shape ───

    void testSatisfiesIMotionClockInterface()
    {
        static_assert(std::is_base_of_v<IMotionClock, QtQuickClock>);
        static_assert(!std::is_copy_constructible_v<QtQuickClock>);
        static_assert(!std::is_move_constructible_v<QtQuickClock>);
    }

    // ─── Construction with null window (test / teardown mode) ───

    void testNullWindowConstructsSafely()
    {
        QtQuickClock clock(nullptr);
        QVERIFY(clock.window() == nullptr);
        QCOMPARE(clock.now(), std::chrono::nanoseconds{0});
        QCOMPARE(clock.refreshRate(), 0.0);
        clock.requestFrame(); // no-op; must not crash
    }

    // ─── Construction with a real QQuickWindow ───

    void testBoundWindowExposesRefreshRate()
    {
        QQuickWindow window;
        QtQuickClock clock(&window);
        QVERIFY(clock.window() == &window);
        // refreshRate() returns 0 when no screen is attached yet — the
        // usual state before the window is shown. The contract is
        // "zero = unknown", not "zero = invalid".
        QVERIFY(clock.refreshRate() >= 0.0);
    }

    void testRequestFrameCallsUpdate()
    {
        // QQuickWindow::update() posts a scheduled paint. We can't
        // observe it directly in headless tests, but requestFrame()
        // must not crash when invoked on a real window.
        QQuickWindow window;
        QtQuickClock clock(&window);
        clock.requestFrame();
        QVERIFY(true);
    }

    // ─── Polymorphic dispatch ───

    void testDispatchesThroughIMotionClockBase()
    {
        QtQuickClock clock(nullptr);
        IMotionClock& iface = clock;
        QCOMPARE(iface.now(), std::chrono::nanoseconds{0});
        QCOMPARE(iface.refreshRate(), 0.0);
    }

    // ─── Window destruction ───

    void testWindowDestructionLeavesClockInSafeState()
    {
        // QPointer guards the window reference — after destruction
        // refreshRate() returns 0 and requestFrame() is a no-op.
        // now() stays at whatever was last latched.
        auto window = std::make_unique<QQuickWindow>();
        QtQuickClock clock(window.get());
        window.reset();

        QVERIFY(clock.window() == nullptr);
        QCOMPARE(clock.refreshRate(), 0.0);
        clock.requestFrame(); // must not crash
    }
};

QTEST_MAIN(TestQtQuickClock)
#include "test_qtquickclock.moc"
