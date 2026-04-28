// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/qml/QtQuickClockManager.h>

#include <QGuiApplication>
#include <QObject>
#include <QQuickWindow>
#include <QTest>

#include <memory>

using namespace PhosphorAnimation;

class TestQtQuickClockManager : public QObject
{
    Q_OBJECT

private:
    /// Per-test manager. Each test method gets a freshly default-
    /// constructed instance via init() — no shared global state.
    /// Replaces the prior `QtQuickClockManager::instance()` Meyers
    /// singleton (Phase A3 of the architecture refactor).
    std::unique_ptr<QtQuickClockManager> m_manager;

private Q_SLOTS:
    void init()
    {
        m_manager = std::make_unique<QtQuickClockManager>();
    }

    void cleanup()
    {
        m_manager.reset();
    }

    /// Verify that the published default-manager handle round-trips —
    /// composition roots use this pair to expose their owned manager
    /// to QML.
    void testDefaultManagerHandleRoundTrips()
    {
        QCOMPARE(QtQuickClockManager::defaultManager(), nullptr);

        QtQuickClockManager::setDefaultManager(m_manager.get());
        QCOMPARE(QtQuickClockManager::defaultManager(), m_manager.get());

        QtQuickClockManager::setDefaultManager(nullptr);
        QCOMPARE(QtQuickClockManager::defaultManager(), nullptr);
    }

    /// A null `QQuickWindow*` must return a null clock. Production
    /// code hits this path when an Item has not yet been parented into
    /// a window.
    void testNullWindowReturnsNull()
    {
        QCOMPARE(m_manager->clockFor(nullptr), static_cast<IMotionClock*>(nullptr));
    }

    /// The load-bearing invariant: two `clockFor` calls with the same
    /// window return the same clock pointer. Without this, every
    /// AnimatedValue constructed against the same window would drive
    /// its own QtQuickClock instance and multiply beforeRendering
    /// subscriptions (the Phase-3 QtQuickClock class doc warns against
    /// this).
    void testOneClockPerWindow()
    {
        auto window = std::make_unique<QQuickWindow>();
        IMotionClock* first = m_manager->clockFor(window.get());
        IMotionClock* second = m_manager->clockFor(window.get());
        QVERIFY(first != nullptr);
        QCOMPARE(first, second);
        QCOMPARE(m_manager->entryCount(), 1);
    }

    /// Two different windows get two different clocks.
    void testDistinctWindowsGetDistinctClocks()
    {
        auto windowA = std::make_unique<QQuickWindow>();
        auto windowB = std::make_unique<QQuickWindow>();
        IMotionClock* clockA = m_manager->clockFor(windowA.get());
        IMotionClock* clockB = m_manager->clockFor(windowB.get());
        QVERIFY(clockA != nullptr);
        QVERIFY(clockB != nullptr);
        QVERIFY(clockA != clockB);
        QCOMPARE(m_manager->entryCount(), 2);
    }

    /// releaseClockFor evicts the entry — test-only teardown and
    /// future destroyed-signal wiring both rely on this.
    void testReleaseClockFor()
    {
        auto window = std::make_unique<QQuickWindow>();
        m_manager->clockFor(window.get());
        QCOMPARE(m_manager->entryCount(), 1);

        m_manager->releaseClockFor(window.get());
        QCOMPARE(m_manager->entryCount(), 0);
    }

    /// Epoch identity must match IMotionClock::steadyClockEpoch — a
    /// rebind from a CompositorClock onto the QML-side clock must be
    /// accepted by AnimatedValue::rebindClock. Confirms the manager
    /// hands out QtQuickClocks (which declare steady-clock epoch)
    /// rather than some other implementation.
    void testClockEpochIsSteady()
    {
        auto window = std::make_unique<QQuickWindow>();
        IMotionClock* clock = m_manager->clockFor(window.get());
        QVERIFY(clock);
        QCOMPARE(clock->epochIdentity(), IMotionClock::steadyClockEpoch());
    }

    /// Regression: `clockFor` used to ignore the `emplace` return value,
    /// which silently leaked a constructed QtQuickClock (+ its
    /// `beforeRendering` subscription) if a concurrent caller beat us
    /// to the insert. The fix routes through `try_emplace` so the
    /// losing caller's clock is deterministically torn down and the
    /// winner's pointer is returned.
    ///
    /// A true concurrent-insert race on GUI-owned windows is hard to
    /// provoke under the thread-ownership assert, so this test exercises
    /// the same invariant through a different path: a single-thread
    /// call must ALWAYS return the same IMotionClock* for the same
    /// window regardless of how many calls are made. Combined with
    /// `entryCount()` staying at 1, we pin that the insert path is
    /// idempotent at the pointer-identity level. This is the
    /// observable shape of "the fix works" at the public API.
    void testClockForReturnsSamePointerAcrossManyCalls()
    {
        auto window = std::make_unique<QQuickWindow>();
        IMotionClock* first = m_manager->clockFor(window.get());
        QVERIFY(first);

        // A few hundred iterations: if the fix regressed and the
        // loser-path leaked fresh clocks, entryCount would still read
        // 1 (the map has the same key) but the returned pointer would
        // eventually differ from `first` under a threaded racecondition.
        // Even on a single thread, we pin the simple invariant.
        for (int i = 0; i < 100; ++i) {
            IMotionClock* again = m_manager->clockFor(window.get());
            QCOMPARE(again, first);
        }
        QCOMPARE(m_manager->entryCount(), 1);
    }

    /// `clockFor` after a `releaseClockFor` teardown must construct a
    /// fresh clock, not hand out a dangling pointer from the evicted
    /// entry. Covers the address-reuse path where Qt recycles the
    /// raw `QQuickWindow*` and also the lookup-miss-after-eviction
    /// path the `try_emplace` fix preserves.
    void testClockForAfterReleaseReconstructs()
    {
        auto window = std::make_unique<QQuickWindow>();
        IMotionClock* first = m_manager->clockFor(window.get());
        QVERIFY(first);

        m_manager->releaseClockFor(window.get());
        QCOMPARE(m_manager->entryCount(), 0);

        // Re-request — must construct a new clock. We can't
        // meaningfully compare pointers (allocator may coincidentally
        // reuse the address), but we CAN assert that entryCount went
        // back to 1 and the returned pointer is non-null.
        IMotionClock* second = m_manager->clockFor(window.get());
        QVERIFY(second);
        QCOMPARE(m_manager->entryCount(), 1);
    }

    /// Regression guard for the eager-eviction-on-destroyed contract
    /// (commit thread for `QtQuickClockManager`'s SingleShotConnection
    /// + `releaseClockFor` lambda). When a QQuickWindow is destroyed,
    /// the manager's `destroyed`-hook MUST fire synchronously on the
    /// GUI thread and evict the map entry — otherwise:
    ///   1. The entry leaks for the manager's lifetime (process-long).
    ///   2. A subsequent `clockFor` for an address-reused QQuickWindow*
    ///      would hand out a stale clock pointing at the dead window.
    ///
    /// The `destroyed` signal fires inside `~QObject` so we don't need
    /// to spin the event loop — the eviction is direct-connection,
    /// synchronous on the destroyer's thread (the GUI thread for any
    /// QQuickWindow). Reading `entryCount()` immediately after the
    /// `delete` (or `unique_ptr::reset()`) is the correct assertion.
    void testWindowDestroyEvictsClock()
    {
        auto window = std::make_unique<QQuickWindow>();
        IMotionClock* clock = m_manager->clockFor(window.get());
        QVERIFY(clock);
        QCOMPARE(m_manager->entryCount(), 1);

        // Destroy the window — `~QObject` emits `destroyed`, which is
        // wired to `releaseClockFor` via DirectConnection so the
        // eviction runs synchronously on this stack frame.
        window.reset();

        QCOMPARE(m_manager->entryCount(), 0);
    }
};

QTEST_MAIN(TestQtQuickClockManager)
#include "test_qtquickclockmanager.moc"
