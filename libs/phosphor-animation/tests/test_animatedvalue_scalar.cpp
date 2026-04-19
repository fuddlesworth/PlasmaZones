// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TestClock.h"

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
using TestClock = PhosphorAnimation::Testing::TestClock;

namespace {

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

    /// A second finish() on an already-completed animation must NOT
    /// re-fire onValueChanged or onComplete — they were contracted to
    /// fire "exactly once" at natural completion. Regression test for
    /// the double-fire bug where the early-return guard
    /// `if (!m_isAnimating && !m_isComplete)` only caught the never-
    /// started case and let a completed-then-finished() animation
    /// re-run the full callback sequence.
    void testFinishOnAlreadyCompletedIsIdempotent()
    {
        TestClock clock;
        AnimatedValue<qreal> v;
        int valueChangedCount = 0;
        int completeCount = 0;
        auto spec = makeSpec(&clock, std::make_shared<Easing>(), 100.0);
        spec.onValueChanged = [&](const qreal&) {
            ++valueChangedCount;
        };
        spec.onComplete = [&] {
            ++completeCount;
        };
        v.start(0.0, 100.0, spec);

        // Progress to natural completion.
        v.advance();
        clock.advanceMs(200.0);
        v.advance();
        QVERIFY(v.isComplete());
        QCOMPARE(completeCount, 1);
        const int baseValueChanged = valueChangedCount;

        // finish() on a completed animation must be a no-op.
        v.finish();
        QCOMPARE(completeCount, 1);
        QCOMPARE(valueChangedCount, baseValueChanged);
        QCOMPARE(v.value(), 100.0);
    }

    /// finish() before start() is also a no-op (the "never started" case
    /// the original guard was meant to catch).
    void testFinishBeforeStartIsNoOp()
    {
        AnimatedValue<qreal> v;
        int completeCount = 0;
        MotionSpec<qreal> unusedSpec;
        unusedSpec.onComplete = [&] {
            ++completeCount;
        };
        // Note: the spec is never installed — v.start() was never called.
        v.finish();
        QCOMPARE(completeCount, 0);
        QVERIFY(!v.isAnimating());
        QVERIFY(!v.isComplete());
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

    void testRetargetPreserveVelocitySubPixelNewDistanceDegrades()
    {
        // Regression: `newDistance > 0.0` gate alone let sub-pixel
        // retarget distances pass — `(velocity * oldDistance) /
        // newDistance` then exploded to astronomical rescaled
        // velocities on a drag-snap workflow that landed the new
        // target a fraction of a pixel from the current visual
        // position. The fix raises the gate to `kRetargetDistanceEpsilon`
        // (0.5 px) — below that, velocity degrades to 0 (the
        // PreservePosition fallback) so the spring settles smoothly
        // instead of oscillating or force-completing on a runaway rate.
        TestClock clock;
        AnimatedValue<qreal> v;
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Spring>(Spring::snappy()), 500.0));

        v.advance();
        for (int i = 0; i < 5; ++i) {
            clock.advanceMs(16.0);
            v.advance();
        }
        const qreal midValue = v.value();
        QVERIFY(v.velocity() > 0.0);

        // Retarget to within 0.1 px of the current visual value —
        // well below the 0.5 px epsilon. Velocity must degrade to 0
        // rather than rescale by (oldDist / 0.1) ≈ 1000× which would
        // produce a catastrophic new velocity.
        v.retarget(midValue + 0.1, RetargetPolicy::PreserveVelocity);
        QCOMPARE(v.velocity(), 0.0);
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
        // A pathological stateful curve that never converges would run
        // forever without the safety cap. Source the cap from the
        // public accessor so a future change to the constant flips the
        // failure mode clearly ("safety cap moved") instead of
        // obliquely ("spring failed to converge").
        TestClock clock;
        AnimatedValue<qreal> v;
        // Near-zero-damping spring — effectively undamped, oscillates
        // forever in analytical terms. The safety cap terminates it.
        v.start(0.0, 100.0, makeSpec(&clock, std::make_shared<Spring>(Spring(1.0, 0.0)), 500.0));
        v.advance();
        // Jump just past the cap in one step.
        const qreal capMs = std::chrono::duration<qreal, std::milli>(AnimatedValue<qreal>::safetyCap()).count();
        clock.advanceMs(capMs + 1'000.0);
        v.advance();
        QVERIFY(v.isComplete());
        QCOMPARE(v.value(), 100.0);
    }
};

QTEST_MAIN(TestAnimatedValueScalar)
#include "test_animatedvalue_scalar.moc"
