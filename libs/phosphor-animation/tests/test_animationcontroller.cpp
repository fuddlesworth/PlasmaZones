// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

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
#include <functional>
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

namespace {

class TestClock final : public IMotionClock
{
public:
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
    void advanceMs(qreal ms)
    {
        m_now += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<qreal, std::milli>(ms));
    }

private:
    std::chrono::nanoseconds m_now{0};
};

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
    mutable int repaintCalls = 0;
    QSet<int> invalidHandles;

    std::function<void(int, MockController&)> onStartedCallback;
    std::function<void(int, MockController&)> onCompleteCallback;

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
    }
    void onAnimationComplete(int handle, const AnimatedValue<QRectF>&) override
    {
        completedHandles.insert(handle);
        completedHandlesOrdered.append(handle);
        if (onCompleteCallback) {
            onCompleteCallback(handle, *this);
        }
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

    void testInvalidHandlesArePrunedSilently()
    {
        TestClock clock;
        MockController c;
        configureLinearEasing(c, clock);
        QVERIFY(c.startAnimation(1, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QRectF(0, 0, 100, 100), QRectF(300, 0, 100, 100)));

        c.invalidHandles.insert(1);
        c.advanceAnimations();
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(2));
        QVERIFY(!c.completedHandles.contains(1));
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
};

QTEST_MAIN(TestAnimationController)
#include "test_animationcontroller.moc"
