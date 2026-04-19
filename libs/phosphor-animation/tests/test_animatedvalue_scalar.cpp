// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/Spring.h>

#include <QTest>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

using PhosphorAnimation::AnimatedValue;
using PhosphorAnimation::Easing;
using PhosphorAnimation::IMotionClock;
using PhosphorAnimation::MotionSpec;
using PhosphorAnimation::Profile;
using PhosphorAnimation::RetargetPolicy;
using PhosphorAnimation::Spring;

namespace {

/// Consumer-controlled clock — test advances time in explicit steps.
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
        ++m_requestFrameCalls;
    }

    void advanceMs(qreal ms)
    {
        m_now += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<qreal, std::milli>(ms));
    }
    int requestFrameCalls() const
    {
        return m_requestFrameCalls;
    }
    void resetRequestFrameCounter()
    {
        m_requestFrameCalls = 0;
    }

private:
    std::chrono::nanoseconds m_now{0};
    int m_requestFrameCalls = 0;
};

// Helper: build a MotionSpec<qreal> with sane defaults.
MotionSpec<qreal> makeSpec(TestClock* clock, std::shared_ptr<const PhosphorAnimation::Curve> curve,
                           qreal durationMs = 100.0)
{
    MotionSpec<qreal> spec;
    spec.profile.curve = std::move(curve);
    spec.profile.duration = durationMs;
    spec.clock = clock;
    return spec;
}

} // namespace

class TestAnimatedValueScalar : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Move-only ───

    void testMoveOnly()
    {
        static_assert(!std::is_copy_constructible_v<AnimatedValue<qreal>>);
        static_assert(!std::is_copy_assignable_v<AnimatedValue<qreal>>);
        static_assert(std::is_move_constructible_v<AnimatedValue<qreal>>);
        static_assert(std::is_move_assignable_v<AnimatedValue<qreal>>);
    }

    // ─── Start validation ───

    void testStartRejectsNullClock()
    {
        AnimatedValue<qreal> v;
        MotionSpec<qreal> spec;
        spec.profile.curve = std::make_shared<Easing>();
        spec.profile.duration = 100.0;
        // spec.clock = nullptr (default)
        QVERIFY(!v.start(0.0, 100.0, spec));
        QVERIFY(!v.isAnimating());
    }

    void testStartRejectsDegenerateMotion()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        QVERIFY(!v.start(42.0, 42.0, makeSpec(&clock, std::make_shared<Easing>())));
        QVERIFY(!v.isAnimating());
        QVERIFY(v.isComplete());
        QCOMPARE(v.value(), 42.0);
    }

    void testStartAcceptsValidMotion()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        QVERIFY(v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Easing>())));
        QVERIFY(v.isAnimating());
        QVERIFY(!v.isComplete());
        QCOMPARE(v.value(), 0.0);
        // requestFrame() fires on start so the paint loop ticks.
        QCOMPARE(clock.requestFrameCalls(), 1);
    }

    // ─── advance() lifecycle with stateless curve ───

    void testAdvanceLatchesStartTimeAndProgressesToCompletion()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        // Use a linear-ish bezier so progression is simple to reason about.
        auto easing = std::make_shared<Easing>();
        easing->x1 = 0.0;
        easing->y1 = 0.0;
        easing->x2 = 1.0;
        easing->y2 = 1.0;
        v.start(0.0, 100.0, makeSpec(&clock, easing, 100.0));

        // First advance: latches startTime, value stays at from.
        v.advance();
        QCOMPARE(v.value(), 0.0);
        QVERIFY(v.isAnimating());

        // Halfway: somewhere between 0 and 100 (linear-ish).
        clock.advanceMs(50.0);
        v.advance();
        QVERIFY(v.value() > 10.0);
        QVERIFY(v.value() < 90.0);

        // Past duration: snaps to target, fires completion.
        clock.advanceMs(60.0);
        v.advance();
        QCOMPARE(v.value(), 100.0);
        QVERIFY(!v.isAnimating());
        QVERIFY(v.isComplete());
    }

    void testOnValueChangedFiresEveryTick()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        int changedCount = 0;
        qreal lastValue = -1.0;
        auto spec = makeSpec(&clock, std::make_shared<Easing>());
        spec.onValueChanged = [&](const qreal& x) {
            ++changedCount;
            lastValue = x;
        };
        v.start(0.0, 100.0, spec);

        v.advance(); // latch — fires onValueChanged(from)
        QCOMPARE(changedCount, 1);
        QCOMPARE(lastValue, 0.0);

        clock.advanceMs(50.0);
        v.advance();
        QCOMPARE(changedCount, 2);
        QVERIFY(lastValue > 0.0);

        clock.advanceMs(60.0);
        v.advance();
        QCOMPARE(changedCount, 3);
        QCOMPARE(lastValue, 100.0); // completion fires one final onValueChanged(to)
    }

    void testOnCompleteFiresExactlyOnce()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        int completeCount = 0;
        auto spec = makeSpec(&clock, std::make_shared<Easing>());
        spec.onComplete = [&] {
            ++completeCount;
        };
        v.start(0.0, 100.0, spec);

        v.advance();
        clock.advanceMs(150.0); // past duration
        v.advance();
        QCOMPARE(completeCount, 1);

        // Further advance() calls are no-ops (not animating).
        v.advance();
        v.advance();
        QCOMPARE(completeCount, 1);
    }

    // ─── cancel() / finish() ───

    void testCancelStopsWithoutComplete()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        int completeCount = 0;
        auto spec = makeSpec(&clock, std::make_shared<Easing>());
        spec.onComplete = [&] {
            ++completeCount;
        };
        v.start(0.0, 100.0, spec);
        v.advance();
        clock.advanceMs(50.0);
        v.advance();

        const qreal midValue = v.value();
        v.cancel();
        QVERIFY(!v.isAnimating());
        QVERIFY(!v.isComplete());
        QCOMPARE(v.value(), midValue); // value stays where cancellation caught it
        QCOMPARE(completeCount, 0); // cancel is explicitly not completion
    }

    void testFinishSnapsToTargetAndFiresComplete()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        int completeCount = 0;
        auto spec = makeSpec(&clock, std::make_shared<Easing>());
        spec.onComplete = [&] {
            ++completeCount;
        };
        v.start(0.0, 100.0, spec);

        v.finish();
        QCOMPARE(v.value(), 100.0);
        QVERIFY(!v.isAnimating());
        QVERIFY(v.isComplete());
        QCOMPARE(completeCount, 1);
    }

    // ─── Spring (stateful) progression ───

    void testStatefulSpringProgressesAndSettles()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        // snappy() = omega 12, zeta 0.8 — underdamped, settles quickly.
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Spring>(Spring::snappy()), 500.0));

        v.advance(); // latch
        // Step the spring in 16 ms increments up to completion.
        for (int i = 0; i < 120 && v.isAnimating(); ++i) {
            clock.advanceMs(16.0);
            v.advance();
        }
        QVERIFY(v.isComplete());
        QCOMPARE(v.value(), 100.0);
    }

    // ─── Retarget — PreservePosition (no jump) ───

    void testRetargetPreservePositionIsContinuous()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Easing>()));
        v.advance();
        clock.advanceMs(50.0);
        v.advance();

        const qreal beforeRetarget = v.value();
        QVERIFY(beforeRetarget > 0.0);
        QVERIFY(beforeRetarget < 100.0);

        QVERIFY(v.retarget(500.0, RetargetPolicy::PreservePosition));
        // Immediately after retarget, value() should be the same — no jump.
        QCOMPARE(v.value(), beforeRetarget);
        QCOMPARE(v.from(), beforeRetarget);
        QCOMPARE(v.to(), 500.0);
    }

    // ─── Retarget — PreserveVelocity on stateful curve ───

    void testRetargetPreserveVelocityStatefulCarriesVelocity()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Spring>(Spring::snappy()), 500.0));

        v.advance();
        // Step spring for ~80 ms so state.velocity is non-zero.
        for (int i = 0; i < 5; ++i) {
            clock.advanceMs(16.0);
            v.advance();
        }
        const qreal midValue = v.value();
        const qreal midVelocity = v.velocity();
        QVERIFY(midVelocity > 0.0);

        // Retarget to a new target with same distance scale — velocity
        // should carry without scaling.
        const qreal newDistance = 800.0 - midValue;
        const qreal oldDistance = 100.0 - 0.0;
        const qreal expectedNewVelocity = midVelocity * oldDistance / newDistance;

        v.retarget(800.0, RetargetPolicy::PreserveVelocity);
        // After retarget, state.value is reset to 0; velocity is the
        // re-projected world-rate on the new segment.
        QCOMPARE(v.value(), midValue); // no visual jump
        const qreal newVelocity = v.velocity();
        QVERIFY(qAbs(newVelocity - expectedNewVelocity) < 1.0e-6);
    }

    // ─── Retarget — ResetVelocity ───

    void testRetargetResetVelocityZeroesRate()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Spring>(Spring::snappy()), 500.0));
        v.advance();
        for (int i = 0; i < 3; ++i) {
            clock.advanceMs(16.0);
            v.advance();
        }
        QVERIFY(v.velocity() > 0.0);

        v.retarget(800.0, RetargetPolicy::ResetVelocity);
        QCOMPARE(v.velocity(), 0.0);
    }

    // ─── Retarget — PreserveVelocity downgrade on stateless ───

    void testRetargetPreserveVelocityStatelessDowngrades()
    {
        // On a stateless curve, PreserveVelocity degrades to
        // PreservePosition with a debug log. Position is continuous,
        // velocity is zero (no physical velocity on parametric).
        TestClock clock;
        AnimatedValue<qreal> v;
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Easing>()));
        v.advance();
        clock.advanceMs(40.0);
        v.advance();

        const qreal midValue = v.value();
        QVERIFY(v.retarget(500.0, RetargetPolicy::PreserveVelocity));
        QCOMPARE(v.value(), midValue); // position continuous
        QCOMPARE(v.velocity(), 0.0); // no velocity on stateless
    }

    // ─── Retarget — degenerate new-distance zero ───

    void testRetargetToCurrentValueIsNoOp()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Easing>()));
        v.advance();
        clock.advanceMs(40.0);
        v.advance();
        const qreal mid = v.value();

        // Retarget to the same point we're at — motion collapses.
        QVERIFY(!v.retarget(mid, RetargetPolicy::PreservePosition));
        QVERIFY(!v.isAnimating());
        QVERIFY(v.isComplete());
        QCOMPARE(v.value(), mid);
    }

    // ─── Swept range (scalar) ───

    void testSweptRangeLinear()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Easing>()));
        const auto [lo, hi] = v.sweptRange();
        QCOMPARE(lo, 0.0);
        QCOMPARE(hi, 100.0);
    }

    void testSweptRangeIncludesElasticOvershoot()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        auto elastic = std::make_shared<Easing>();
        elastic->type = Easing::Type::ElasticOut;
        elastic->amplitude = 1.5;
        elastic->period = 0.3;
        v.start(0.0, 100.0, makeSpec(&clock, elastic));
        const auto [lo, hi] = v.sweptRange();
        // Elastic overshoot can push above the target or below the start.
        QVERIFY(hi > 100.0 || lo < 0.0);
    }

    // ─── Safety-cap guard ───

    void testSafetyCapBreaksRunaway()
    {
        // A pathological stateful curve that never converges would
        // run forever without the 60 s safety cap.
        TestClock clock;
        AnimatedValue<qreal> v;
        // Near-zero-damping spring — effectively undamped, oscillates
        // forever in analytical terms. The safety cap terminates it.
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Spring>(Spring(1.0, 0.0)), 500.0));
        v.advance();
        // Jump 61 s in one step.
        clock.advanceMs(61'000.0);
        v.advance();
        QVERIFY(v.isComplete());
        QCOMPARE(v.value(), 100.0);
    }
};

QTEST_MAIN(TestAnimatedValueScalar)
#include "test_animatedvalue_scalar.moc"
