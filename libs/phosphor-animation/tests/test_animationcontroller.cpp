// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TestClock.h"

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/AnimationController.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/Spring.h>

#include <QList>
#include <QSet>
#include <QTest>

#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

using namespace std::chrono_literals;

using PhosphorAnimation::AnimatedValue;
using PhosphorAnimation::AnimationController;
using PhosphorAnimation::Easing;
using PhosphorAnimation::IMotionClock;
using PhosphorAnimation::Profile;
using PhosphorAnimation::RetargetPolicy;
using PhosphorAnimation::RetargetResult;
using PhosphorAnimation::Spring;
using PhosphorAnimation::StartResult;
using TestClock = PhosphorAnimation::Testing::TestClock;

namespace {

/// Mock controller using a plain int as the handle type.
class MockController : public AnimationController<int>
{
public:
    QSet<int> startedHandles;
    QList<int> startedHandlesOrdered;
    QSet<int> completedHandles;
    QList<int> completedHandlesOrdered;
    QList<int> retargetedHandlesOrdered;
    QList<int> replacedHandlesOrdered;
    QList<int> reapedHandlesOrdered;
    QList<int> abandonedHandlesOrdered;
    mutable int repaintCalls = 0;
    QSet<int> invalidHandles;

    std::function<void(int, MockController&)> onStartedCallback;
    std::function<void(int, MockController&)> onCompleteCallback;
    std::function<void(int, MockController&)> onReplacedCallback;

protected:
    void onAnimationStarted(int handle, const AnimatedValue<QRectF>&) override
    {
        startedHandles.insert(handle);
        startedHandlesOrdered.append(handle);
        if (onStartedCallback) {
            onStartedCallback(handle, *this);
        }
    }
    void onAnimationRetargeted(int handle, const AnimatedValue<QRectF>&) override
    {
        retargetedHandlesOrdered.append(handle);
    }
    void onAnimationReplaced(int handle, const AnimatedValue<QRectF>&) override
    {
        replacedHandlesOrdered.append(handle);
        if (onReplacedCallback) {
            onReplacedCallback(handle, *this);
        }
    }
    void onAnimationComplete(int handle, const AnimatedValue<QRectF>&) override
    {
        completedHandles.insert(handle);
        completedHandlesOrdered.append(handle);
        if (onCompleteCallback) {
            onCompleteCallback(handle, *this);
        }
    }
    void onAnimationReaped(int handle, const AnimatedValue<QRectF>&) override
    {
        reapedHandlesOrdered.append(handle);
    }
    void onAnimationAbandoned(int handle, const AnimatedValue<QRectF>&) override
    {
        abandonedHandlesOrdered.append(handle);
    }
    void onRepaintNeeded(int, const QRectF&) const override
    {
        ++repaintCalls;
    }
    bool isHandleValid(int handle) const override
    {
        return !invalidHandles.contains(handle);
    }
};

void configureLinearEasing(MockController& c, TestClock& clock, qreal durationMs = 100.0)
{
    c.setClock(&clock);
    auto linear = std::make_shared<Easing>();
    linear->x1 = 0.0;
    linear->y1 = 0.0;
    linear->x2 = 1.0;
    linear->y2 = 1.0;
    Profile p;
    p.curve = linear;
    p.duration = durationMs;
    c.setProfile(p);
}

/// Per-handle clock routing subclass. Routes handle 1 to `clockA`,
/// all other handles to the default clock set via `setClock()`.
class PerHandleController : public MockController
{
public:
    IMotionClock* clockA = nullptr;

protected:
    IMotionClock* clockForHandle(int handle) const override
    {
        if (handle == 1 && clockA) {
            return clockA;
        }
        return MockController::clockForHandle(handle);
    }
};

/// Routes handles 1 and 2 to `clockForOne` (test-fixture clock for
/// reap selection); every other handle falls through to the default.
class PerHandleClockController : public MockController
{
public:
    IMotionClock* clockForOne = nullptr;

protected:
    IMotionClock* clockForHandle(int handle) const override
    {
        if (handle == 1 || handle == 2) {
            return clockForOne;
        }
        return MockController::clockForHandle(handle);
    }
};

/// Extends `PerHandleClockController` with a dispatch hook for
/// reap-time re-entrancy tests. The base's `onAnimationReaped` only
/// records the reap; this subclass also invokes a caller-provided
/// callback so the test can re-enter the controller.
class ReapReentrantController : public PerHandleClockController
{
public:
    std::function<void(int, ReapReentrantController&)> onReapedCallback;

protected:
    void onAnimationReaped(int handle, const AnimatedValue<QRectF>& anim) override
    {
        PerHandleClockController::onAnimationReaped(handle, anim);
        if (onReapedCallback) {
            onReapedCallback(handle, *this);
        }
    }
};

/// Resolver that returns a clock on the first call (captured into the
/// spec at `startAnimation`) but nullptr on every subsequent call
/// (the per-tick re-resolution in `advanceAnimations`). Used to
/// verify that a null per-tick resolver result does not strand the
/// captured clock pointer.
class FlakyResolverController : public MockController
{
public:
    IMotionClock* startClock = nullptr;
    mutable int resolverCalls = 0;

protected:
    IMotionClock* clockForHandle(int) const override
    {
        ++resolverCalls;
        return resolverCalls == 1 ? startClock : nullptr;
    }
};

} // namespace

class TestAnimationController : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Configuration ───

    void testSettersAndGettersRoundtrip()
    {
        TestClock clock;
        MockController c;
        c.setClock(&clock);
        c.setEnabled(false);
        c.setDuration(250.0);
        c.setMinDistance(7);
        auto curve = std::make_shared<Easing>();
        c.setCurve(curve);

        QCOMPARE(c.isEnabled(), false);
        QCOMPARE(c.duration(), 250.0);
        QCOMPARE(c.minDistance(), 7);
        QVERIFY(c.curve() == curve);
        QVERIFY(c.clock() == &clock);
    }

    void testSetMinDistanceClampsNegativesToZero()
    {
        MockController c;
        c.setMinDistance(-100);
        QCOMPARE(c.minDistance(), 0);
    }

    void testSetMinDistanceClampsUpperBound()
    {
        MockController c;
        c.setMinDistance(1'000'000);
        QCOMPARE(c.minDistance(), 10000);
    }

    void testSetDurationClampsUpperBound()
    {
        MockController c;
        c.setDuration(1'000'000'000.0);
        QCOMPARE(c.duration(), 10000.0);
    }

    void testSetDurationClampsNegativesToZero()
    {
        MockController c;
        c.setDuration(-50.0);
        QCOMPARE(c.duration(), 0.0);
    }

    void testSetDurationRejectsNaN()
    {
        // `qBound(0.0, NaN, max)` returns NaN under libstdc++ — the
        // middle operand short-circuits the comparisons that `qBound`'s
        // `qMax(qMin(...))` lowering relies on. A corrupt settings
        // reload landing NaN in `Profile::duration` would otherwise
        // divide `elapsedMs / NaN` in `AnimatedValue`'s stateless
        // progression and poison every downstream paint.
        //
        // `clampProfile` gates non-finite inputs first and resets
        // `profile.duration` to `std::nullopt`, so `duration()` falls
        // back to `Profile::DefaultDuration`.
        MockController c;
        c.setDuration(std::nan(""));
        QCOMPARE(c.duration(), Profile::DefaultDuration);

        // Same for positive infinity.
        c.setDuration(std::numeric_limits<qreal>::infinity());
        QCOMPARE(c.duration(), Profile::DefaultDuration);
    }

    void testSetProfileRejectsNaNDuration()
    {
        // Same rejection as `setDuration(NaN)` but via the
        // `setProfile()` entry point — the shared `clampProfile` helper
        // runs on both paths so the gate is centralised.
        MockController c;
        Profile p;
        p.duration = std::nan("");
        c.setProfile(p);
        QCOMPARE(c.duration(), Profile::DefaultDuration);
    }

    void testZeroDurationMotionCompletesImmediately()
    {
        // A legitimate `duration = 0.0` means "no animation, jump to
        // target on the first real tick". `AnimatedValue`'s stateless
        // branch short-circuits to terminal-frame snap when
        // `durationMs <= 0.0`. Exercise the controller-level end-to-end
        // so the contract holds through the full lifecycle (one
        // `onAnimationStarted`, one `onAnimationComplete`, no leftover
        // entry). First `advanceAnimations` latches startTime only;
        // second advance runs the stateless branch and completes.
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 0.0);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        QCOMPARE(c.startedHandlesOrdered.size(), 1);
        // Latch startTime.
        c.advanceAnimations();
        // Progression tick — `durationMs <= 0.0` gates to terminal.
        clock.advanceMs(1.0);
        c.advanceAnimations();
        QCOMPARE(c.completedHandlesOrdered.size(), 1);
        QVERIFY(!c.hasAnimation(1));
    }

    // ─── startAnimation gating ───

    void testStartRejectedWhenNoClock()
    {
        MockController c;
        // No clock set.
        QVERIFY(!c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
    }

    void testStartRejectedWhenDisabled()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        c.setEnabled(false);
        QVERIFY(!c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(!c.hasAnimation(1));
    }

    void testStartRejectedOnDegenerateTarget()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(!c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(0, 0, 0, 100)));
        QVERIFY(!c.hasAnimation(1));
    }

    void testStartAcceptsValidTransition()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(42, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        QVERIFY(c.hasAnimation(42));
        QVERIFY(c.hasActiveAnimations());
        QVERIFY(c.startedHandles.contains(42));
    }

    // ─── Invalid-handle gate ───

    void testStartRejectsInvalidHandleUpFront()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        c.invalidHandles.insert(7);
        QVERIFY(!c.startAnimation(7, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(!c.hasAnimation(7));
        QVERIFY(c.startedHandles.isEmpty());
    }

    // ─── Progression + completion ───

    void testAdvanceProgressesAndCompletes()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 100.0);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));

        c.advanceAnimations(); // latch
        QVERIFY(c.hasAnimation(1));
        QVERIFY(c.completedHandles.isEmpty());

        clock.advanceMs(50.0);
        c.advanceAnimations();
        QVERIFY(c.hasAnimation(1));

        clock.advanceMs(60.0);
        c.advanceAnimations();
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.completedHandles.contains(1));
        QVERIFY(c.repaintCalls >= 1);
    }

    void testInvalidHandlesArePrunedViaAbandonedHook()
    {
        // Handles that flip invalid mid-flight are reaped by
        // `advanceAnimations` via `onAnimationAbandoned` — the fourth
        // terminating event (distinct from complete / replaced /
        // clock-reaped). The hook balances started-vs-terminated
        // event counts for subclasses that track lifecycle invariants
        // when a window / scene item is destroyed mid-animation.
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));

        c.invalidHandles.insert(1);
        c.advanceAnimations();
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(2));
        // Abandoned — NOT completed.
        QVERIFY(!c.completedHandles.contains(1));
        QVERIFY(c.abandonedHandlesOrdered.contains(1));
        QVERIFY(!c.abandonedHandlesOrdered.contains(2));
        // Event-count invariant: one terminating event per accepted
        // start on this handle (the abandoned one).
        QCOMPARE(c.abandonedHandlesOrdered.count(1), 1);
    }

    // ─── State queries ───

    void testCurrentValueFallback()
    {
        MockController c;
        QCOMPARE(c.currentValue(99, QRectF(1, 2, 3, 4)), QRectF(1, 2, 3, 4));
    }

    void testIsAnimatingToTarget()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        const QRectF target(500, 0, 100, 100);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), target));
        QVERIFY(c.isAnimatingToTarget(1, target));
        QVERIFY(!c.isAnimatingToTarget(1, QRectF(999, 999, 100, 100)));
        QVERIFY(!c.isAnimatingToTarget(2, target));
    }

    void testRemoveAndClear()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100));
        c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100));

        c.removeAnimation(1);
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(2));

        c.clear();
        QVERIFY(!c.hasActiveAnimations());
    }

    void testAnimationBoundsCoversEndpoints()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(10, 10, 100, 100), QRectF(500, 200, 100, 100)));
        const QRectF b = c.animationBounds(1);
        QVERIFY(b.contains(QRectF(10, 10, 100, 100)));
        QVERIFY(b.contains(QRectF(500, 200, 100, 100)));
    }

    void testScheduleRepaintsFiresPerActive()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100));
        c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100));

        const int before = c.repaintCalls;
        c.scheduleRepaints();
        QCOMPARE(c.repaintCalls - before, 2);
    }

    void testScheduleRepaintsOnEmptyIsNoOp()
    {
        MockController c;
        QVERIFY(!c.hasActiveAnimations());
        c.scheduleRepaints();
        QCOMPARE(c.repaintCalls, 0);
    }

    // ─── retarget (new Phase 3 capability) ───

    void testRetargetOnActiveAnimation()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        c.advanceAnimations();
        clock.advanceMs(40.0);
        c.advanceAnimations();
        const QRectF mid = c.currentValue(1, QRectF());

        QVERIFY(c.retarget(1, QRectF(900, 0, 100, 100), RetargetPolicy::PreservePosition));
        QCOMPARE(c.currentValue(1, QRectF()), mid); // no visual jump
        QVERIFY(c.isAnimatingToTarget(1, QRectF(900, 0, 100, 100)));
    }

    void testRetargetOnMissingHandleReturnsFalse()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(!c.retarget(99, QRectF(0, 0, 100, 100), RetargetPolicy::PreservePosition));
    }

    void testRetargetPreserveVelocityOnSpringCarriesRate()
    {
        TestClock clock;
        MockController c;
        c.setClock(&clock);
        Profile p;
        p.curve = std::make_shared<Spring>(Spring::snappy());
        p.duration = 500.0;
        c.setProfile(p);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        c.advanceAnimations();
        for (int i = 0; i < 5; ++i) {
            clock.advanceMs(16.0);
            c.advanceAnimations();
        }
        const AnimatedValue<QRectF>* anim = c.animationFor(1);
        QVERIFY(anim);
        const qreal midVelocity = anim->velocity();
        QVERIFY(midVelocity > 0.0);

        QVERIFY(c.retarget(1, QRectF(900, 0, 100, 100), RetargetPolicy::PreserveVelocity));
        anim = c.animationFor(1);
        QVERIFY(anim);
        QVERIFY(anim->velocity() > 0.0); // velocity carried (rescaled)
    }

    // ─── Re-entrancy contract ───

    void testReentrantStartInsideCompleteHookSurvives()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 100.0);

        bool reentered = false;
        c.onCompleteCallback = [&](int handle, MockController& self) {
            if (!reentered) {
                reentered = true;
                self.startAnimation(handle, QRectF(300, 0, 100, 100), QRectF(600, 0, 100, 100));
            }
        };

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        c.advanceAnimations();
        clock.advanceMs(150.0);
        c.advanceAnimations();

        QVERIFY(reentered);
        QVERIFY(c.hasAnimation(1));
        const AnimatedValue<QRectF>* anim = c.animationFor(1);
        QVERIFY(anim);
        QCOMPARE(anim->to(), QRectF(600, 0, 100, 100));
    }

    void testReentrantClearInsideCompleteHookIsSafe()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 50.0);

        c.onCompleteCallback = [&](int, MockController& self) {
            self.clear();
        };

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));

        c.advanceAnimations();
        clock.advanceMs(100.0);
        c.advanceAnimations();

        QVERIFY(!c.hasActiveAnimations());
        QCOMPARE(c.completedHandles.size(), 1);
        QVERIFY(c.completedHandles.contains(1) || c.completedHandles.contains(2));
    }

    void testReentrantStartForNewHandleInsideCompleteHookSurvives()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 50.0);

        c.onCompleteCallback = [&](int handle, MockController& self) {
            if (handle == 1) {
                self.startAnimation(99, QRectF(0, 0, 100, 100), QRectF(700, 0, 100, 100));
            }
        };

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        c.advanceAnimations();
        clock.advanceMs(100.0);
        c.advanceAnimations();

        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(99));
        QCOMPARE(c.animationFor(99)->to(), QRectF(700, 0, 100, 100));
    }

    // ─── Profile swap mid-flight: curves are per-animation-immutable ───

    void testSetProfileMidFlightDoesNotAffectInFlight()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 100.0);
        auto elastic = std::make_shared<Easing>();
        elastic->type = Easing::Type::ElasticOut;
        elastic->amplitude = 1.5;

        Profile p;
        p.curve = elastic;
        p.duration = 200.0;
        c.setProfile(p);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        const AnimatedValue<QRectF>* anim = c.animationFor(1);
        QVERIFY(anim);
        QVERIFY(anim->spec().profile.curve.get() == elastic.get());

        // Swap the controller's profile. In-flight motion keeps its own.
        Profile p2;
        p2.curve = std::make_shared<Easing>();
        p2.duration = 300.0;
        c.setProfile(p2);
        QVERIFY(anim->spec().profile.curve.get() == elastic.get());
    }

    // ─── No-clock advance is a no-op ───

    void testAdvanceNoClockIsNoOp()
    {
        MockController c;
        // No clock, so this is the degenerate path. Should not crash.
        c.advanceAnimations();
        QVERIFY(!c.hasActiveAnimations());
    }

    // ─── New hooks: onAnimationRetargeted ───

    void testRetargetFiresRetargetedHook()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        c.advanceAnimations();
        clock.advanceMs(20.0);
        c.advanceAnimations();

        QVERIFY(c.retargetedHandlesOrdered.isEmpty());
        QVERIFY(c.retarget(1, QRectF(600, 0, 100, 100), RetargetPolicy::PreservePosition));
        QCOMPARE(c.retargetedHandlesOrdered.size(), 1);
        QCOMPARE(c.retargetedHandlesOrdered.first(), 1);
    }

    void testRetargetOnMissingHandleDoesNotFireHook()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(!c.retarget(99, QRectF(0, 0, 100, 100), RetargetPolicy::PreservePosition));
        QVERIFY(c.retargetedHandlesOrdered.isEmpty());
    }

    // ─── New hooks: onAnimationReplaced ───

    void testStartOnExistingHandleFiresReplacedHook()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.replacedHandlesOrdered.isEmpty());

        QVERIFY(c.startAnimation(1, QRectF(50, 0, 100, 100), QRectF(800, 0, 100, 100)));
        QCOMPARE(c.replacedHandlesOrdered.size(), 1);
        QCOMPARE(c.replacedHandlesOrdered.first(), 1);
        // Started twice, replaced once, completed zero times so far —
        // event-count invariant holds: startedCount = completedCount +
        // replacedCount + activeCount.
        QCOMPARE(c.startedHandlesOrdered.size(), 2);
        QCOMPARE(c.completedHandles.size(), 0);
    }

    // ─── retarget degenerate reaps immediately with one onAnimationComplete ───

    void testRetargetToSameTargetReapsImmediatelyAndFiresCompleteOnce()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        const QRectF target(500, 0, 100, 100);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), target));
        c.advanceAnimations();
        clock.advanceMs(20.0);
        c.advanceAnimations();

        // Retarget to the current visual value → newDistance ≈ 0 →
        // degenerate-complete branch. The controller reaps the entry
        // synchronously inside retarget() and fires
        // onAnimationComplete exactly once; spec callbacks stay
        // silent (the degenerate path is symmetric with start()'s
        // degenerate path). No zombie entry persists into the next
        // advanceAnimations() tick.
        const QRectF current = c.currentValue(1, QRectF());
        QVERIFY(c.completedHandles.isEmpty());
        QVERIFY(!c.retarget(1, current, RetargetPolicy::PreservePosition));
        QVERIFY(!c.hasAnimation(1));
        QCOMPARE(c.completedHandles.size(), 1);
        QVERIFY(c.completedHandles.contains(1));

        // Second advance does not double-fire completion.
        c.advanceAnimations();
        QCOMPARE(c.completedHandlesOrdered.size(), 1);
    }

    // ─── SnapPolicy retargetPolicy propagates into the MotionSpec ───

    void testStartAnimationStampsDefaultPreserveVelocityRetargetPolicy()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        const AnimatedValue<QRectF>* anim = c.animationFor(1);
        QVERIFY(anim);
        QCOMPARE(anim->spec().retargetPolicy, RetargetPolicy::PreserveVelocity);
    }

    // ─── retarget-while-active doesn't fire retargeted when underlying rejects ───

    void testRetargetToSameTargetDoesNotFireRetargetedHook()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        c.advanceAnimations();
        clock.advanceMs(20.0);
        c.advanceAnimations();

        const QRectF current = c.currentValue(1, QRectF());
        QVERIFY(!c.retarget(1, current, RetargetPolicy::PreservePosition));
        QVERIFY(c.retargetedHandlesOrdered.isEmpty());
    }

    // ─── retarget gating: isEnabled() + isHandleValid() ───

    void testRetargetRejectedWhenDisabled()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        c.setEnabled(false);
        QCOMPARE(c.retargetWithResult(1, QRectF(900, 0, 100, 100), RetargetPolicy::PreservePosition),
                 RetargetResult::Disabled);
        QVERIFY(c.retargetedHandlesOrdered.isEmpty());
    }

    void testRetargetRejectedWhenHandleInvalid()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        c.invalidHandles.insert(1);
        QCOMPARE(c.retargetWithResult(1, QRectF(900, 0, 100, 100), RetargetPolicy::PreservePosition),
                 RetargetResult::HandleInvalid);
        QVERIFY(c.retargetedHandlesOrdered.isEmpty());
    }

    // ─── Enum-returning startAnimationWithResult discriminates failure modes ───

    void testStartAnimationWithResultReportsDisabled()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        c.setEnabled(false);
        QCOMPARE(c.startAnimationWithResult(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)),
                 StartResult::Disabled);
    }

    void testStartAnimationWithResultReportsHandleInvalid()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        c.invalidHandles.insert(7);
        QCOMPARE(c.startAnimationWithResult(7, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)),
                 StartResult::HandleInvalid);
    }

    void testStartAnimationWithResultReportsNoClock()
    {
        MockController c; // No clock configured.
        Profile profile;
        profile.curve = std::make_shared<Easing>();
        profile.duration = 200.0;
        c.setProfile(profile);
        QCOMPARE(c.startAnimationWithResult(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)), StartResult::NoClock);
    }

    void testStartAnimationWithResultReportsPolicyRejected()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        // Degenerate target (zero-sized newFrame) — SnapPolicy rejects.
        QCOMPARE(c.startAnimationWithResult(1, QRectF(0, 0, 100, 100), QRectF(0, 0, 0, 100)),
                 StartResult::PolicyRejected);
    }

    void testRetargetWithResultReportsUnknownHandle()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QCOMPARE(c.retargetWithResult(99, QRectF(0, 0, 100, 100), RetargetPolicy::PreservePosition),
                 RetargetResult::UnknownHandle);
    }

    void testRetargetWithResultReportsDegenerateReap()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        c.advanceAnimations();
        clock.advanceMs(20.0);
        c.advanceAnimations();
        const QRectF current = c.currentValue(1, QRectF());
        QCOMPARE(c.retargetWithResult(1, current, RetargetPolicy::PreservePosition), RetargetResult::DegenerateReap);
        QVERIFY(!c.hasAnimation(1));
        QCOMPARE(c.completedHandlesOrdered.size(), 1);
    }

    void testRetargetWithResultReportsAccepted()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QCOMPARE(c.retargetWithResult(1, QRectF(900, 0, 100, 100), RetargetPolicy::PreservePosition),
                 RetargetResult::Accepted);
    }

    // ─── clockForHandle override routes per-handle ───

    void testClockForHandleOverrideRoutesPerHandle()
    {
        TestClock defaultClock;
        TestClock outputClock;
        PerHandleController c;
        c.setClock(&defaultClock);
        c.clockA = &outputClock;
        Profile profile;
        profile.curve = std::make_shared<Easing>();
        profile.duration = 200.0;
        c.setProfile(profile);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));

        QCOMPARE(c.animationFor(1)->spec().clock, &outputClock);
        QCOMPARE(c.animationFor(2)->spec().clock, &defaultClock);
    }

    // ─── setProfile clamps duration + minDistance ───

    void testSetProfileClampsDurationUpperBound()
    {
        MockController c;
        Profile p;
        p.duration = 1'000'000.0;
        c.setProfile(p);
        QCOMPARE(c.duration(), 10000.0); // kMaxDurationMs
    }

    void testSetProfileClampsNegativeDurationToZero()
    {
        MockController c;
        Profile p;
        p.duration = -50.0;
        c.setProfile(p);
        QCOMPARE(c.duration(), 0.0);
    }

    void testSetProfileClampsMinDistanceUpperBound()
    {
        MockController c;
        Profile p;
        p.minDistance = 1'000'000;
        c.setProfile(p);
        QCOMPARE(c.profile().effectiveMinDistance(), 10000); // kMaxMinDistancePx
    }

    // ─── setProfile + setMinDistance share a single source of truth ───

    void testSetMinDistanceReflectsInProfile()
    {
        MockController c;
        c.setMinDistance(42);
        QCOMPARE(c.minDistance(), 42);
        QCOMPARE(c.profile().effectiveMinDistance(), 42);
        // Explicitly engaged — the engaged optional means "caller has an
        // opinion", which is the invariant ProfileTree inheritance relies on.
        QVERIFY(c.profile().minDistance.has_value());
        QCOMPARE(*c.profile().minDistance, 42);
    }

    void testSetProfileMinDistanceReflectsInMinDistance()
    {
        MockController c;
        Profile p;
        p.minDistance = 13;
        c.setProfile(p);
        QCOMPARE(c.minDistance(), 13);
    }

    void testProfileMinDistanceRoutesIntoSnapPolicySkip()
    {
        // A position delta below profile.minDistance with no size change
        // must be rejected by startAnimation. Without the Profile→
        // SnapPolicy routing the old code ignored profile.minDistance and
        // used a separate m_minDistance field that defaulted to 0.
        TestClock clock;
        MockController c;
        c.setClock(&clock);
        Profile p;
        p.curve = std::make_shared<Easing>();
        p.duration = 100.0;
        p.minDistance = 200;
        c.setProfile(p);

        // 50px position delta, no size change — under the 200px threshold.
        const auto result = c.startAnimationWithResult(1, QRectF(0, 0, 100, 100), QRectF(50, 0, 100, 100));
        QCOMPARE(result, StartResult::PolicyRejected);
        QVERIFY(!c.hasAnimation(1));
    }

    void testSetMinDistanceRoutesIntoSnapPolicySkip()
    {
        // Same contract as above but via the setMinDistance convenience
        // setter — the two entry points must behave identically.
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        c.setMinDistance(200);

        const auto result = c.startAnimationWithResult(1, QRectF(0, 0, 100, 100), QRectF(50, 0, 100, 100));
        QCOMPARE(result, StartResult::PolicyRejected);
        QVERIFY(!c.hasAnimation(1));
    }

    // ─── reapAnimationsForClock (per-output teardown helper) ───

    void testReapAnimationsForClockRemovesOnlyMatchingAnimations()
    {
        TestClock clockA;
        TestClock clockB;
        PerHandleController c;
        c.setClock(&clockB);
        c.clockA = &clockA;
        Profile profile;
        profile.curve = std::make_shared<Easing>();
        profile.duration = 200.0;
        c.setProfile(profile);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100))); // clockA
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100))); // clockB
        QVERIFY(c.startAnimation(3, QRectF(0, 0, 100, 100), QRectF(700, 0, 100, 100))); // clockB

        QCOMPARE(c.reapAnimationsForClock(&clockA), 1);
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(2));
        QVERIFY(c.hasAnimation(3));

        QCOMPARE(c.reapAnimationsForClock(&clockB), 2);
        QVERIFY(!c.hasAnimation(2));
        QVERIFY(!c.hasAnimation(3));
    }

    void testReapAnimationsForClockWithNullIsNoOp()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QCOMPARE(c.reapAnimationsForClock(nullptr), 0);
        QVERIFY(c.hasAnimation(1));
    }

    void testReapAnimationsForClockDoesNotFireCompletionHook()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QCOMPARE(c.reapAnimationsForClock(&clock), 1);
        QVERIFY(!c.hasAnimation(1));
        // onAnimationComplete / onAnimationReplaced are NOT fired —
        // reap is a distinct terminating event with its own hook.
        QVERIFY(c.completedHandles.isEmpty());
        QVERIFY(c.replacedHandlesOrdered.isEmpty());
    }

    void testReapAnimationsForClockFiresOnAnimationReapedHook()
    {
        // Reap must fire exactly one onAnimationReaped per entry it
        // removes so telemetry/damage-region consumers can balance
        // their started-vs-terminated counters without special-casing
        // output teardown.
        TestClock clockA;
        TestClock clockB;
        PerHandleController c;
        c.setClock(&clockB);
        c.clockA = &clockA;
        Profile profile;
        profile.curve = std::make_shared<Easing>();
        profile.duration = 200.0;
        c.setProfile(profile);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100))); // clockA
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100))); // clockB
        QVERIFY(c.startAnimation(3, QRectF(0, 0, 100, 100), QRectF(700, 0, 100, 100))); // clockB

        QCOMPARE(c.reapAnimationsForClock(&clockA), 1);
        QCOMPARE(c.reapedHandlesOrdered, QList<int>{1});

        QCOMPARE(c.reapAnimationsForClock(&clockB), 2);
        // Handle 2 and 3 reaped; order depends on unordered_map bucketing
        // — just check both fired and the total count is right.
        QCOMPARE(c.reapedHandlesOrdered.size(), 3);
        QVERIFY(c.reapedHandlesOrdered.contains(2));
        QVERIFY(c.reapedHandlesOrdered.contains(3));

        // No natural-completion or replaced hooks fire.
        QVERIFY(c.completedHandles.isEmpty());
        QVERIFY(c.replacedHandlesOrdered.isEmpty());
    }

    void testReapAnimationsForClockWithNullDoesNotFireHook()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QCOMPARE(c.reapAnimationsForClock(nullptr), 0);
        QVERIFY(c.reapedHandlesOrdered.isEmpty());
    }

    // ─── Epoch gate fires before first advance (C4 regression) ───

    /// The epoch-compatibility gate in `rebindClock` must run whenever
    /// the current clock exists — not only after `m_startTime` has
    /// latched. A rebind between `start()` and the first `advance()`
    /// previously bypassed the gate and silently installed an
    /// incompatible clock, letting the next advance latch startTime
    /// against the foreign epoch and silently corrupt progress on any
    /// subsequent rebind back to a compatible clock.
    void testRebindClockGatesEpochBeforeFirstAdvance()
    {
        struct NullEpochClock final : public IMotionClock
        {
            std::chrono::nanoseconds now() const override
            {
                return {};
            }
            qreal refreshRate() const override
            {
                return 60.0;
            }
            void requestFrame() override
            {
            }
            // default epochIdentity() == nullptr → refuses rebind
        };

        TestClock steadyClock;
        NullEpochClock alienClock;

        AnimatedValue<qreal> v;
        PhosphorAnimation::MotionSpec<qreal> spec;
        auto linear = std::make_shared<Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        spec.profile.curve = linear;
        spec.profile.duration = 1000.0;
        spec.clock = &steadyClock;
        QVERIFY(v.start(0.0, 100.0, spec));
        QVERIFY(v.isAnimating());
        // Deliberately do NOT call advance() — m_startTime stays unset.

        // Rebind to the incompatible clock. Without the C4 fix this
        // would silently install alienClock as m_spec.clock; with the
        // fix, the gate refuses and the original clock stays captured.
        v.rebindClock(&alienClock);
        QCOMPARE(v.spec().clock, static_cast<IMotionClock*>(&steadyClock));
    }

    // ─── Mid-animation clock re-routing via advanceAnimations ───

    void testAdvanceAnimationsRebindsClockWhenResolverChanges()
    {
        // Simulate a handle migrating between outputs mid-animation.
        // The PerHandleController routes handle 1 via `clockA` when
        // non-null. Start on clockA, then swap clockA out — the next
        // advanceAnimations tick re-resolves and rebinds to the new
        // target (here, the default clock).
        TestClock defaultClock;
        TestClock migrationClock;
        PerHandleController c;
        c.setClock(&defaultClock);
        c.clockA = &migrationClock;
        Profile profile;
        profile.curve = std::make_shared<Easing>();
        profile.duration = 200.0;
        c.setProfile(profile);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        QCOMPARE(c.animationFor(1)->spec().clock, &migrationClock);

        c.advanceAnimations(); // latch on migrationClock
        migrationClock.advanceMs(50.0);
        c.advanceAnimations();

        // Simulate output migration: the resolver now returns
        // defaultClock for handle 1. The next advanceAnimations()
        // tick will re-resolve and rebind the animation's clock.
        c.clockA = nullptr;
        c.advanceAnimations();

        QVERIFY(c.hasAnimation(1));
        QCOMPARE(c.animationFor(1)->spec().clock, &defaultClock);
    }

    void testRebindClockNullCancelsAnimation()
    {
        TestClock clock;
        AnimatedValue<QRectF> v;
        PhosphorAnimation::MotionSpec<QRectF> spec;
        spec.profile.curve = std::make_shared<Easing>();
        spec.profile.duration = 100.0;
        spec.clock = &clock;

        QVERIFY(v.start(QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100), spec));
        QVERIFY(v.isAnimating());

        v.rebindClock(nullptr);
        QVERIFY(!v.isAnimating());
    }

    void testRebindClockSameClockIsNoOp()
    {
        TestClock clock;
        AnimatedValue<QRectF> v;
        PhosphorAnimation::MotionSpec<QRectF> spec;
        spec.profile.curve = std::make_shared<Easing>();
        spec.profile.duration = 100.0;
        spec.clock = &clock;

        QVERIFY(v.start(QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100), spec));
        v.rebindClock(&clock);
        QVERIFY(v.isAnimating());
        QCOMPARE(v.spec().clock, &clock);
    }

    /// Regression: per-output clocks latch `now()` from their own paint
    /// cycles, so two steady_clock-backed clocks can return different
    /// absolute values at the same wall instant. A rebind that doesn't
    /// rebase latched timestamps would leave `m_startTime` (against the
    /// old clock's advanced `now`) potentially ahead of the new clock's
    /// current `now`, producing a negative `elapsed` on the next
    /// stateless advance — the curve would sample `evaluate(t<0)` and
    /// the animation would visually rewind for one tick.
    ///
    /// This test starts an animation on clockA (advanced), rebinds to
    /// clockB (never advanced — sits at t=0, far behind clockA), then
    /// advances clockB by a plausible frame duration. The value must
    /// not regress relative to the pre-rebind reading.
    void testRebindClockToBackDatedClockPreservesElapsed()
    {
        TestClock clockA;
        TestClock clockB;
        AnimatedValue<QRectF> v;
        PhosphorAnimation::MotionSpec<QRectF> spec;
        spec.profile.curve = std::make_shared<Easing>();
        spec.profile.duration = 200.0;
        spec.clock = &clockA;

        // Advance clockA well past clockB before start, so clockA's
        // `now()` and clockB's `now()` differ by 500 ms at the rebind
        // point. clockB stays at 0.
        clockA.advanceMs(500.0);

        QVERIFY(v.start(QRectF(0, 0, 100, 100), QRectF(1000, 0, 100, 100), spec));
        v.advance(); // latches startTime on clockA at 500 ms
        clockA.advanceMs(50.0);
        v.advance(); // elapsed 50 ms, somewhere mid-animation on clockA

        const qreal midX = v.value().x();
        QVERIFY(midX > 0.0);
        QVERIFY(midX < 1000.0);

        // Rebind to clockB — its `now()` sits ~550 ms behind clockA's
        // (clockA latched m_startTime at 500 ms, then advanced 50 ms
        // to 550 ms; clockB is at 0). Without the timestamp rebase,
        // the next advance would compute elapsed = 0 - 550 = -550 ms
        // and sample evaluate(-2.75).
        v.rebindClock(&clockB);

        // First advance on the new clock: no elapsed change, no value
        // change. State is preserved.
        //
        // Tolerance of 0.5 px is load-bearing: the pre-rebind value
        // reflects 50 ms of elapsed progression on a 200 ms animation
        // across 1000 px of travel; a 5 ms first-tick dt (1 % of the
        // segment's total budget) would move the value by about 25 px.
        // A 5 px upper bound would have accepted that regression; 0.5
        // px catches a first-tick dt larger than ~0.1 ms — the range
        // where "rebase did not preserve elapsed" becomes observable.
        v.advance();
        const qreal afterRebindX = v.value().x();
        QVERIFY2(afterRebindX >= midX - 0.1, "value must not regress across rebind");
        QVERIFY2(qAbs(afterRebindX - midX) < 0.5,
                 "first tick after rebind must land within dt≈0 of pre-rebind value (rebase preserved elapsed)");

        // Continue progressing on clockB — value must monotonically
        // advance past midX as elapsed grows.
        clockB.advanceMs(50.0);
        v.advance();
        QVERIFY2(v.value().x() > midX, "post-rebind progression must continue forward, not rewind to a smaller t");
    }

    /// Regression: the two-argument `retarget(handle, newFrame)` overload
    /// must honour `MotionSpec::retargetPolicy` (stamped by
    /// `startAnimation` from the controller's `setRetargetPolicy`). The
    /// three-argument overload always takes an explicit policy; the
    /// two-argument overload's whole point is to let a settings UI pick
    /// the behaviour once rather than threading the enum through every
    /// call site.
    ///
    /// Concretely: configure `setRetargetPolicy(ResetVelocity)`, start a
    /// Spring animation (stateful — velocity is meaningful), advance
    /// until the spring carries non-zero velocity, then call the
    /// two-argument retarget. Velocity must be zeroed (ResetVelocity
    /// semantics), not carried (PreserveVelocity — the previous
    /// hard-coded controller default).
    void testControllerRetargetHonoursSpecRetargetPolicy()
    {
        TestClock clock;
        MockController c;
        c.setClock(&clock);
        c.setRetargetPolicy(RetargetPolicy::ResetVelocity);

        Profile profile;
        profile.curve = std::make_shared<Spring>(Spring::snappy());
        profile.duration = 500.0;
        c.setProfile(profile);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(1000, 0, 100, 100)));
        QCOMPARE(c.animationFor(1)->spec().retargetPolicy, RetargetPolicy::ResetVelocity);

        c.advanceAnimations(); // latch
        for (int i = 0; i < 5; ++i) {
            clock.advanceMs(16.0);
            c.advanceAnimations();
        }
        QVERIFY2(c.animationFor(1)->velocity() > 0.0, "spring must build velocity before retarget");

        // Two-argument retarget — no explicit policy. Must pick up
        // ResetVelocity from the stamped spec, not fall back to the
        // old PreserveVelocity default.
        QVERIFY(c.retarget(1, QRectF(2000, 0, 100, 100)));
        QCOMPARE(c.animationFor(1)->velocity(), 0.0);
    }

    /// In-flight animations must keep the `RetargetPolicy` captured on
    /// their `MotionSpec` at start time — a subsequent
    /// `setRetargetPolicy(newPolicy)` on the controller affects only
    /// future `startAnimation` calls, never animations already running.
    /// Matches the config-reload immutability contract that
    /// `setProfile` also honours (see Phase 3 decision K). Without this
    /// guarantee, a settings-UI update mid-animation would mutate the
    /// physical semantics of a currently-ticking animation — the
    /// architectural equivalent of swapping the curve out from under
    /// a Spring mid-flight.
    void testInFlightRetargetPolicyImmuneToControllerPolicySwap()
    {
        TestClock clock;
        MockController c;
        c.setClock(&clock);
        c.setRetargetPolicy(RetargetPolicy::ResetVelocity);

        Profile profile;
        profile.curve = std::make_shared<Spring>(Spring::snappy());
        profile.duration = 500.0;
        c.setProfile(profile);

        // Start with ResetVelocity stamped onto the spec.
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(1000, 0, 100, 100)));
        QCOMPARE(c.animationFor(1)->spec().retargetPolicy, RetargetPolicy::ResetVelocity);

        // Swap the controller's default. The NEW value applies to
        // subsequent `startAnimation` calls only.
        c.setRetargetPolicy(RetargetPolicy::PreserveVelocity);
        QCOMPARE(c.retargetPolicy(), RetargetPolicy::PreserveVelocity);

        // In-flight spec must retain its captured ResetVelocity.
        QCOMPARE(c.animationFor(1)->spec().retargetPolicy, RetargetPolicy::ResetVelocity);

        // Drive spring velocity, then exercise the two-argument retarget
        // which reads the per-animation stamped policy. ResetVelocity
        // must still zero the velocity despite the controller-level
        // policy having flipped to PreserveVelocity.
        c.advanceAnimations();
        for (int i = 0; i < 5; ++i) {
            clock.advanceMs(16.0);
            c.advanceAnimations();
        }
        QVERIFY2(c.animationFor(1)->velocity() > 0.0, "spring must build velocity before retarget");

        QVERIFY(c.retarget(1, QRectF(2000, 0, 100, 100)));
        QCOMPARE(c.animationFor(1)->velocity(), 0.0);

        // Now start a SECOND animation after the policy swap — its spec
        // must capture the NEW PreserveVelocity default, proving the
        // swap reached future starts.
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(1000, 0, 100, 100)));
        QCOMPARE(c.animationFor(2)->spec().retargetPolicy, RetargetPolicy::PreserveVelocity);
    }

    // ─── Regression: C1 UAF via in-place move-assign on displace ───

    /// When `startAnimation` is called on a handle that already has an
    /// in-flight animation, the controller must preserve object identity
    /// of the existing `AnimatedValue` entry (via move-assignment) rather
    /// than erase+emplace. Object identity is load-bearing for the
    /// spec-callback reentrancy contract — without it, a spec-level
    /// callback on the in-flight animation that re-enters via
    /// `startAnimation(same_handle, ...)` would destroy `*this` under
    /// `advance()`'s call stack and UAF on the next line.
    ///
    /// This test captures the pointer to the pre-displace entry and
    /// verifies the post-displace entry lives at the same address —
    /// that's the observable effect of the move-assign-over-erase fix.
    void testDisplaceReusesExistingSlotForObjectIdentity()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 100.0);

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        const AnimatedValue<QRectF>* beforeDisplace = c.animationFor(1);
        QVERIFY(beforeDisplace);

        // Second start on the same handle — the displace path. With the
        // move-assign fix, the map slot is reused; old erase+emplace
        // would have produced a new heap slot (different address).
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        const AnimatedValue<QRectF>* afterDisplace = c.animationFor(1);
        QVERIFY(afterDisplace);

        QCOMPARE(afterDisplace, beforeDisplace);
        QCOMPARE(c.replacedHandlesOrdered.size(), 1);
        QCOMPARE(c.replacedHandlesOrdered.first(), 1);
        QCOMPARE(afterDisplace->to(), QRectF(500, 0, 100, 100));
    }

    // ─── Regression: C3 re-entrant remove during onAnimationReplaced ───

    /// When `onAnimationReplaced` removes the newly-installed entry
    /// before `onAnimationStarted` can fire, the start call returns
    /// `StartResult::AcceptedThenRemoved` (distinct from `Accepted`).
    /// Callers doing started-vs-terminated event accounting see the
    /// replace event plus the distinct status, and can balance their
    /// counts without inspecting hook ordering.
    void testStartAcceptedThenRemovedOnReentrantReplaceRemoval()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock, 100.0);

        // Seed an existing animation for handle 1 so the next start
        // triggers the replace branch.
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(200, 0, 100, 100)));
        const int startedBefore = c.startedHandlesOrdered.size();

        // Wire the replace hook to remove the freshly-installed entry
        // out from under the controller. The controller then finds no
        // entry to fire onAnimationStarted against; it must report the
        // transition via AcceptedThenRemoved.
        bool replaceFired = false;
        c.onReplacedCallback = [&](int handle, MockController& self) {
            QCOMPARE(handle, 1);
            replaceFired = true;
            self.removeAnimation(handle);
        };

        const StartResult r = c.startAnimationWithResult(1, QRectF(0, 0, 100, 100), QRectF(400, 0, 100, 100));
        QCOMPARE(r, StartResult::AcceptedThenRemoved);
        QVERIFY(replaceFired);
        QVERIFY(!c.hasAnimation(1));
        // Replace fired, started did not.
        QVERIFY(c.replacedHandlesOrdered.contains(1));
        QCOMPARE(c.startedHandlesOrdered.size(), startedBefore);
    }

    // ─── Regression: H1 rebindClock refuses across incompatible epochs ───

    /// Two clocks with different `epochIdentity()` (or either null) must
    /// NOT be rebound against one another — the rebase math assumes a
    /// shared monotonic time base, and a mismatched epoch would produce
    /// a meaningless delta that corrupts `m_startTime`.
    ///
    /// Exercises `AnimatedValue::rebindClock` directly (bypassing the
    /// controller's per-tick guard) to test the belt-and-suspenders
    /// check inside the primitive. The pre-rebind clock pointer must
    /// remain captured in the spec after the refused migration.
    void testRebindClockRefusedOnEpochMismatch()
    {
        /// Clock with a null epoch identity — incompatible with any
        /// other clock for rebind purposes (the default IMotionClock
        /// behaviour when a third-party subclass doesn't opt in).
        struct NullEpochClock final : public IMotionClock
        {
            std::chrono::nanoseconds m_now{0};
            std::chrono::nanoseconds now() const override
            {
                return m_now;
            }
            qreal refreshRate() const override
            {
                return 60.0;
            }
            void requestFrame() override
            {
            }
            // epochIdentity() defaults to nullptr — the base-class
            // "unknown / incompatible" sentinel.
        };

        // TestClock declares steadyClockEpoch(); NullEpochClock returns
        // nullptr (the default). The mismatch (sentinel vs. null) is
        // exactly what the guard is meant to catch.
        TestClock steadyClock;
        NullEpochClock alienClock;
        alienClock.m_now = std::chrono::nanoseconds{5'000'000'000LL};
        steadyClock.advanceMs(500.0);

        AnimatedValue<qreal> v;
        PhosphorAnimation::MotionSpec<qreal> spec;
        auto linear = std::make_shared<Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        spec.profile.curve = linear;
        spec.profile.duration = 1000.0;
        spec.clock = &steadyClock;
        QVERIFY(v.start(0.0, 100.0, spec));
        v.advance(); // latch startTime against steadyClock
        steadyClock.advanceMs(100.0);
        v.advance();

        const qreal preRebindValue = v.value();

        // Attempt to rebind to the null-epoch clock. TestClock returns
        // the steady-clock sentinel; NullEpochClock returns nullptr.
        // The check "identities match AND non-null" fails on both the
        // null side and the mismatch side — the rebind must refuse and
        // keep the captured steadyClock.
        v.rebindClock(&alienClock);
        QCOMPARE(v.spec().clock, static_cast<IMotionClock*>(&steadyClock));

        // Progress must still make sense on the original clock.
        steadyClock.advanceMs(100.0);
        v.advance();
        QVERIFY2(v.value() > preRebindValue, "progress must continue on the captured clock after refused rebind");
    }

    // ─── L5a: Rebind to a clock past the segment end completes naturally ───

    /// When a handle migrates between outputs mid-animation, the new
    /// clock may already sit past the segment's notional end. The
    /// rebase preserves `elapsed` (so the animation isn't artificially
    /// advanced to completion), but the next advance() on the new clock
    /// sees `dt = newNow - lastTickTime` which accumulates real time
    /// until natural completion. This test verifies that the rebind
    /// does not corrupt state and the animation completes cleanly.
    void testRebindClockPastSegmentEndCompletesOnNextAdvance()
    {
        TestClock clockA;
        TestClock clockB;
        // Seed clockB far ahead of clockA — simulates the "output B's
        // last-latched presentTime is well beyond output A's" case on
        // a mixed-refresh setup after a few frames of A painting.
        clockB.advanceMs(10'000.0);

        AnimatedValue<qreal> v;
        PhosphorAnimation::MotionSpec<qreal> spec;
        auto linear = std::make_shared<Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        spec.profile.curve = linear;
        spec.profile.duration = 100.0;
        spec.clock = &clockA;
        QVERIFY(v.start(0.0, 100.0, spec));
        v.advance(); // latch startTime against clockA
        clockA.advanceMs(50.0); // half-way through the segment
        v.advance();
        QVERIFY(v.isAnimating());
        QVERIFY(!v.isComplete());

        // Rebind to clockB. Both are steady-epoch. The rebase shifts
        // m_startTime by (clockB.now() - clockA.now()) — a large
        // positive delta. The next advance() computes dt = clockB.now()
        // - m_lastTickTime (also rebased), so dt ≈ 0 on the first
        // post-rebind tick. Progress is preserved.
        v.rebindClock(&clockB);
        QCOMPARE(v.spec().clock, static_cast<IMotionClock*>(&clockB));
        v.advance();
        // After rebind + advance with ~zero dt, we're still at ~50%.
        const qreal postRebindValue = v.value();
        QVERIFY2(postRebindValue >= 49.0 && postRebindValue <= 51.0,
                 "post-rebind value must be preserved at ~segment midpoint");

        // Advance clockB past the end of the segment (50 more ms from
        // latch point). Animation completes on the next advance().
        clockB.advanceMs(60.0);
        v.advance();
        QVERIFY(v.isComplete());
        QCOMPARE(v.value(), 100.0);
    }

    // ─── L5b: Re-entrant reap is safe ───

    /// `reapAnimationsForClock` iterates m_animations and fires
    /// `onAnimationReaped` for every entry captured on the reaped
    /// clock. Consumers are allowed to re-enter the controller from
    /// that hook (starting a new animation on an unrelated handle,
    /// for instance). The outer loop must tolerate the map mutation.
    void testReentrantStartInsideReapedHookIsSafe()
    {
        TestClock clockA; // the clock we'll reap — handles 1 and 2 bind here
        TestClock clockB; // survives the reap — handle 99 binds here (default)

        ReapReentrantController c;
        configureLinearEasing(c, clockB, 100.0);
        c.clockForOne = &clockA;
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        QVERIFY(c.startAnimation(99, QRectF(0, 0, 100, 100), QRectF(200, 0, 100, 100)));

        c.onReapedCallback = [&](int handle, ReapReentrantController& self) {
            // Only re-enter once, on the first reap, to avoid infinite
            // recursion if the new start immediately matches the reap
            // predicate.
            if (handle == 1 && !self.hasAnimation(500)) {
                self.startAnimation(500, QRectF(0, 0, 100, 100), QRectF(700, 0, 100, 100));
            }
        };

        const int reaped = c.reapAnimationsForClock(&clockA);
        QCOMPARE(reaped, 2); // handles 1 and 2 were on clockA
        QVERIFY(c.reapedHandlesOrdered.contains(1));
        QVERIFY(c.reapedHandlesOrdered.contains(2));
        // The re-entrant start inserted handle 500 during iteration.
        // It must be present post-reap, and handle 99 (on the default
        // clockB) must still be alive.
        QVERIFY(c.hasAnimation(500));
        QVERIFY(c.hasAnimation(99));
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(!c.hasAnimation(2));
    }

    // ─── L5c: Per-tick null resolver keeps captured clock ───

    /// `AnimationController::advanceAnimations` resolves the clock for
    /// each handle on every tick. When the resolver returns nullptr
    /// (XWayland bootstrap with no current output, QML item mid-
    /// reparenting), the animation must keep its captured clock and
    /// continue ticking rather than being stranded with a null
    /// pointer.
    void testPerTickNullResolverKeepsCapturedClock()
    {
        TestClock clockA;

        // FlakyResolverController: first clockForHandle call returns
        // clockA (captured into the spec), every subsequent call
        // returns nullptr (the per-tick re-resolution). Verifies that
        // the null post-start result does not strand the animation.
        FlakyResolverController c;
        c.startClock = &clockA;
        auto linear = std::make_shared<Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        Profile p;
        p.curve = linear;
        p.duration = 100.0;
        c.setProfile(p);
        // No default clock set — we rely entirely on the resolver.

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(100, 0, 100, 100)));
        // Verify the captured clock is clockA.
        QCOMPARE(c.animationFor(1)->spec().clock, static_cast<IMotionClock*>(&clockA));

        // Tick: resolver now returns null. Animation must keep clockA
        // in its spec (no stranded-null) and progress must continue.
        c.advanceAnimations();
        clockA.advanceMs(50.0);
        c.advanceAnimations();
        const AnimatedValue<QRectF>* anim = c.animationFor(1);
        QVERIFY(anim);
        QCOMPARE(anim->spec().clock, static_cast<IMotionClock*>(&clockA));
        QVERIFY(anim->isAnimating());
        QVERIFY(anim->value().x() > 0.0 && anim->value().x() < 100.0);

        // Complete the segment.
        clockA.advanceMs(60.0);
        c.advanceAnimations();
        QVERIFY(!c.hasAnimation(1)); // reaped on completion
        QVERIFY(c.completedHandles.contains(1));
    }

    // ─── Reap snapshot survives rehash-triggering insert ───

    /// `reapAnimationsForClock` previously iterated the map with a live
    /// iterator and mutated it via `erase` + fired the reap hook. A
    /// re-entrant `startAnimation(newHandle)` inside the hook can
    /// insert into the map and — crossing the load-factor threshold —
    /// trigger a rehash. Before the snapshot-pattern fix, the outer
    /// iterator was invalidated by the rehash and subsequent loop
    /// iterations were UB. Stage enough animations on the target
    /// clock that the re-entrant insert is guaranteed to cross the
    /// rehash threshold.
    void testReapManyAnimationsSurvivesRehashingInsert()
    {
        TestClock clockA;
        TestClock clockB;

        ReapReentrantController c;
        configureLinearEasing(c, clockB, 100.0);
        c.clockForOne = &clockA;

        // Populate many handles on the target clock. 32 is comfortably
        // above typical unordered_map initial bucket counts (libstdc++
        // starts at 1 / libc++ starts at a small prime), so the
        // load-factor threshold is crossed at least once as the map
        // grows — and the re-entrant insert below pushes past another
        // threshold during the reap.
        //
        // The base controller routes handles 1 and 2 to `clockForOne`
        // (clockA); to put more handles on clockA we override via the
        // hook's callback which captures the controller — but simpler:
        // just use the existing routing (handles 1 and 2) and add many
        // extras on the default clock to inflate map size.
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));
        for (int h = 10; h < 50; ++h) {
            QVERIFY(c.startAnimation(h, QRectF(0, 0, 100, 100), QRectF(200 + h, 0, 100, 100)));
        }

        // Hook inserts many new handles during the reap. Every insert
        // may rehash; the snapshot-then-find loop must stay stable.
        int nextNewHandle = 1000;
        c.onReapedCallback = [&](int /*reapedHandle*/, ReapReentrantController& self) {
            // Insert a batch of new animations per reap event. If the
            // reap loop uses a live iterator and any insert rehashes,
            // the next iteration's deref is UB.
            for (int k = 0; k < 8; ++k) {
                self.startAnimation(nextNewHandle++, QRectF(0, 0, 100, 100), QRectF(100, 0, 100, 100));
            }
        };

        const int reaped = c.reapAnimationsForClock(&clockA);
        QCOMPARE(reaped, 2); // handles 1 and 2 were on clockA
        QVERIFY(c.reapedHandlesOrdered.contains(1));
        QVERIFY(c.reapedHandlesOrdered.contains(2));
        // All original non-target-clock handles still alive.
        for (int h = 10; h < 50; ++h) {
            QVERIFY(c.hasAnimation(h));
        }
        // All re-entrantly-inserted handles survive (2 reaps × 8 inserts = 16).
        for (int h = 1000; h < 1016; ++h) {
            QVERIFY(c.hasAnimation(h));
        }
    }

    // ─── Cascade reap: reapAnimationsForClock(B) called from inside
    //     onAnimationReaped fired by reapAnimationsForClock(A) ───

    /// Multi-output teardown can cascade: output A's removal fires its
    /// reap, a telemetry / layering layer hooked on `onAnimationReaped`
    /// notices that output B is also being torn down in the same
    /// hotplug event and calls `reapAnimationsForClock(clockB)` from
    /// within the hook. The outer snapshot must tolerate the inner
    /// pass erasing entries it holds handles to, and the inner pass
    /// must not double-reap (or miss) anything.
    void testReentrantReapInsideReapedHookIsSafe()
    {
        TestClock clockA;
        TestClock clockB;
        TestClock clockC; // bystander — survives the cascade untouched

        ReapReentrantController c;
        configureLinearEasing(c, clockC, 100.0);
        c.clockForOne = &clockA; // handles 1 and 2 bind to clockA

        // Handles on the two reaped clocks:
        //   clockA → 1, 2 (via clockForOne routing)
        //   clockB → 3, 4 (installed via the per-animation spec below
        //                   — configureLinearEasing's default clock is
        //                   clockC so we spec clockB by hand)
        //   clockC → 99 (bystander)
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(500, 0, 100, 100)));

        // For handles 3 + 4 we want clockB — temporarily swap the
        // default via setClock so startAnimation captures clockB.
        c.setClock(&clockB);
        QVERIFY(c.startAnimation(3, QRectF(0, 0, 100, 100), QRectF(700, 0, 100, 100)));
        QVERIFY(c.startAnimation(4, QRectF(0, 0, 100, 100), QRectF(900, 0, 100, 100)));
        c.setClock(&clockC); // restore bystander default

        QVERIFY(c.startAnimation(99, QRectF(0, 0, 100, 100), QRectF(200, 0, 100, 100)));

        // Confirm spec captures — defensive: the test's premise is that
        // the two inner sets are on clocks A and B respectively.
        QCOMPARE(c.animationFor(1)->spec().clock, static_cast<IMotionClock*>(&clockA));
        QCOMPARE(c.animationFor(3)->spec().clock, static_cast<IMotionClock*>(&clockB));

        // First reap of A's clock triggers cascade into B's clock via
        // the hook. One-shot guard avoids unbounded recursion on the
        // inner pass's own fires.
        bool cascadeFired = false;
        c.onReapedCallback = [&](int /*handle*/, ReapReentrantController& self) {
            if (!cascadeFired) {
                cascadeFired = true;
                const int innerReaped = self.reapAnimationsForClock(&clockB);
                // Inner pass must find both B-bound handles and reap
                // them — not more, not fewer.
                QCOMPARE(innerReaped, 2);
            }
        };

        const int outerReaped = c.reapAnimationsForClock(&clockA);

        // Outer pass reaps its own 2 handles (1, 2). The inner pass
        // reaped B's 2 handles (3, 4) via the cascade. Bystander
        // handle 99 on clockC survives untouched.
        QCOMPARE(outerReaped, 2);
        QVERIFY(cascadeFired);
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(!c.hasAnimation(2));
        QVERIFY(!c.hasAnimation(3));
        QVERIFY(!c.hasAnimation(4));
        QVERIFY(c.hasAnimation(99));

        // Reap ordering: outer pass entries fire first, then the inner
        // pass entries (depth-first on the hook callback). Contents
        // must cover all four reaped handles exactly once — no doubles,
        // no misses.
        QCOMPARE(c.reapedHandlesOrdered.size(), 4);
        QVERIFY(c.reapedHandlesOrdered.contains(1));
        QVERIFY(c.reapedHandlesOrdered.contains(2));
        QVERIFY(c.reapedHandlesOrdered.contains(3));
        QVERIFY(c.reapedHandlesOrdered.contains(4));
    }

    // ─── Reap fires trailing onRepaintNeeded for damage coverage ───

    /// `reapAnimationsForClock` must fire `onRepaintNeeded` after each
    /// `onAnimationReaped` so adapters that rely on the controller's
    /// damage bookkeeping invalidate the stale paint region when an
    /// output is torn down. Symmetric with the natural-completion and
    /// degenerate-retarget paths.
    void testReapFiresRepaintNeededForDamageCoverage()
    {
        TestClock clockA;
        TestClock clockB;

        PerHandleClockController c;
        configureLinearEasing(c, clockB, 100.0);
        c.clockForOne = &clockA; // handles 1 and 2 → clockA

        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(400, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(600, 0, 100, 100)));
        QVERIFY(c.startAnimation(99, QRectF(0, 0, 100, 100), QRectF(200, 0, 100, 100)));

        // startAnimation itself triggers zero repaints (the hook fires
        // on damage-producing events, not on start). Reset the counter
        // so we measure only what reap emits.
        const int baselineRepaints = c.repaintCalls;

        const int reaped = c.reapAnimationsForClock(&clockA);
        QCOMPARE(reaped, 2);

        // Exactly one onRepaintNeeded per reaped entry. The bystander
        // handle on clockB is untouched.
        QCOMPARE(c.repaintCalls - baselineRepaints, 2);
        QVERIFY(c.hasAnimation(99));
    }

    // ─── Transform reflection (determinant sign change) falls back
    //      to component-wise lerp rather than producing singular
    //      matrices via polar decomposition ───

    void testTransformReflectionFallsBackToComponentWiseLerp()
    {
        // Polar decomposition cannot smoothly interpolate through a
        // determinant sign change. `Interpolate<QTransform>::lerp`
        // detects the case and falls back to component-wise lerp,
        // which is well-defined but non-rigid — the caller is expected
        // to split reflection animations into sub-segments.
        const QTransform from; // identity — det = +1
        QTransform to = QTransform::fromScale(-1.0, 1.0); // det = -1

        const QTransform mid = PhosphorAnimation::Interpolate<QTransform>::lerp(from, to, 0.5);
        // Component-wise midpoint: m11 = (1 + -1)/2 = 0 (not the
        // singular result of polar, which would pass through a
        // degenerate matrix with different semantics).
        QVERIFY(qAbs(mid.m11() - 0.0) < 1.0e-9);
        QVERIFY(qAbs(mid.m22() - 1.0) < 1.0e-9);
        // Endpoints remain exact.
        const QTransform atZero = PhosphorAnimation::Interpolate<QTransform>::lerp(from, to, 0.0);
        const QTransform atOne = PhosphorAnimation::Interpolate<QTransform>::lerp(from, to, 1.0);
        QCOMPARE(atZero.m11(), from.m11());
        QCOMPARE(atOne.m11(), to.m11());
    }
};

QTEST_MAIN(TestAnimationController)
#include "test_animationcontroller.moc"
