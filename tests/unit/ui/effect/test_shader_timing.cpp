// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_shader_timing.cpp
 * @brief Pins ShaderInternal::resolveTransitionLifetimeMs() and easeProgress().
 *
 * These two header-inline helpers are the whole of the compositor's shader
 * timing: one decides how long a transition lives, the other decides what
 * `iTime` it shows on a given frame. Both the per-window transition and the
 * desktop switch route through them, so a regression here retimes every
 * animation at once.
 *
 * resolveTransitionLifetimeMs is also the ONLY bound between a hand-edited
 * profile JSON (or a pathological spring) and a multi-minute animation holding
 * per-frame repaints — an undamped spring's settleTime() is 30 s, and a
 * `"duration": 600000` node is otherwise taken at face value. That clamp is
 * load-bearing and is pinned below.
 */

#include <QTest>

#include <cmath>

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <plasmazoneseffect/shader_internal.h>

using PhosphorAnimation::CurveState;
using PhosphorAnimation::Easing;
using PhosphorAnimation::Spring;
using PlasmaZones::ShaderInternal::clampProgressForCurve;
using PlasmaZones::ShaderInternal::easeProgress;
using PlasmaZones::ShaderInternal::resolveTransitionLifetimeMs;

namespace Limits = PhosphorAnimation::Limits;

class TestShaderTiming : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── resolveTransitionLifetimeMs ───

    // A null curve (reachable only before settings load) takes the nominal
    // duration through unchanged, so long as it is already in the envelope.
    void testLifetimeNullCurveKeepsNominal()
    {
        QCOMPARE(resolveTransitionLifetimeMs(300, nullptr), 300);
    }

    // A STATELESS curve derives nothing of its own — the nominal duration stands.
    void testLifetimeStatelessCurveKeepsNominal()
    {
        const Easing easing;
        QVERIFY(!easing.isStateful());
        QCOMPARE(resolveTransitionLifetimeMs(300, &easing), 300);
    }

    // Zero and negative nominals floor to the minimum rather than arming a
    // zero-length transition (whose progress divisor would be 0).
    void testLifetimeFloorsAtMinimum()
    {
        QCOMPARE(resolveTransitionLifetimeMs(0, nullptr), Limits::MinAnimationDurationMs);
        QCOMPARE(resolveTransitionLifetimeMs(-1, nullptr), Limits::MinAnimationDurationMs);
        QCOMPARE(resolveTransitionLifetimeMs(10, nullptr), Limits::MinAnimationDurationMs);
    }

    // A hand-edited `"duration": 600000` cannot arm a ten-minute animation.
    void testLifetimeClampsAbsurdNominalToMaximum()
    {
        QCOMPARE(resolveTransitionLifetimeMs(600000, nullptr), Limits::MaxAnimationDurationMs);
    }

    // A STATEFUL (spring) curve derives its lifetime from its own physics and
    // IGNORES the nominal entirely — settleTime() wins.
    void testLifetimeSpringOverridesNominal()
    {
        const Spring spring = Spring::snappy();
        QVERIFY(spring.isStateful());
        const int expected = qRound(spring.settleTime() * 1000.0);
        QCOMPARE(resolveTransitionLifetimeMs(300, &spring),
                 qBound(Limits::MinAnimationDurationMs, expected, Limits::MaxAnimationDurationMs));
        // The nominal is genuinely not consulted: a wildly different one resolves
        // to the same lifetime.
        QCOMPARE(resolveTransitionLifetimeMs(300, &spring), resolveTransitionLifetimeMs(1999, &spring));
    }

    // The guard that matters: an UNDAMPED spring (zeta = 0, inside Spring's own
    // qBound(0, zeta, 10) and reachable from the wire string "spring:12,0") never
    // converges. Its settleTime() is capped at Spring::MaxSettleSeconds (30 s),
    // and this clamp then cuts it to the 2 s envelope. Without it, a desktop
    // switch would hold the fullscreen-effect claim — and per-frame repaints —
    // for half a minute.
    void testLifetimeUndampedSpringClampsToMaximum()
    {
        const Spring undamped(12.0, 0.0);
        QVERIFY(undamped.settleTime() > 0.0);
        QVERIFY(std::isfinite(undamped.settleTime()));
        QCOMPARE(resolveTransitionLifetimeMs(300, &undamped), Limits::MaxAnimationDurationMs);
    }

    // A very stiff, well-damped spring settles FASTER than the minimum, and the
    // envelope floors it — the other end of the same clamp. With the 0.5% settling
    // band, settleTime = -ln(0.005) / (zeta*omega) = 5.298 / (zeta*omega), so
    // Spring(200, 0.6) settles in 44.2 ms, just under the 50 ms floor. The QVERIFY
    // is load-bearing: it fails loudly if a band change moves this spring back
    // above the floor, instead of letting the QCOMPARE pass for the wrong reason.
    void testLifetimeFastSpringFloorsAtMinimum()
    {
        const Spring stiff(200.0, 0.6);
        QVERIFY2(stiff.settleTime() * 1000.0 < Limits::MinAnimationDurationMs,
                 "test setup must settle below the floor or the clamp is untested");
        QCOMPARE(resolveTransitionLifetimeMs(300, &stiff), Limits::MinAnimationDurationMs);
    }

    // ─── easeProgress ───

    // A null curve is linear: iTime is the raw elapsed ratio.
    void testEaseNullCurveIsLinear()
    {
        CurveState state;
        QCOMPARE(easeProgress(nullptr, state, -1, 0, 0.25, /*stepCurve=*/true), 0.25);
    }

    // A stateless curve evaluates the linear point directly.
    void testEaseStatelessEvaluatesLinear()
    {
        const Easing easing;
        CurveState state;
        const qreal got = easeProgress(&easing, state, -1, 0, 0.5, /*stepCurve=*/true);
        QCOMPARE(got, easing.evaluate(0.5));
        // Endpoints are exact.
        QCOMPARE(easeProgress(&easing, state, -1, 0, 0.0, true), 0.0);
        QCOMPARE(easeProgress(&easing, state, -1, 0, 1.0, true), 1.0);
    }

    // The clamp POLICY, tested directly on the function that owns it.
    //
    // Not via easeProgress and a spring, which is where this test started: it used
    // `Spring::smooth()` (zeta = 1) and relied on semi-implicit Euler being unstable
    // enough to push a critically-damped spring past 1.0. Substepping made the
    // integrator unconditionally stable, so that premise evaporated — and the test's
    // own QVERIFY2 caught it rather than silently passing on a setup that no longer
    // exercised anything.
    //
    // With a stable integrator, a NON-overshooting curve cannot leave [0,1] on its
    // own, so the clamp is now purely defensive: it guards a third-party Curve
    // registered through CurveRegistry that misbehaves. Test it as what it is — a
    // pure policy function over a supplied value.
    void testClampPolicyBoundsNonOvershootingCurves()
    {
        const Easing easing; // OutCubic — overshoots() == false
        QVERIFY(!easing.overshoots());
        QCOMPARE(clampProgressForCurve(1.4, &easing), 1.0);
        QCOMPARE(clampProgressForCurve(-0.3, &easing), 0.0);
        QCOMPARE(clampProgressForCurve(0.5, &easing), 0.5); // in-range is untouched

        // A null curve means linear, and linear is bounded.
        QCOMPARE(clampProgressForCurve(1.4, nullptr), 1.0);
        QCOMPARE(clampProgressForCurve(-0.3, nullptr), 0.0);
    }

    // ...and an OVERSHOOTING curve keeps its overshoot. Same function, opposite
    // branch — this is the half that makes the bounce reach the shader at all.
    void testClampPolicyPassesOvershootingCurvesThrough()
    {
        const Spring bouncy(12.0, 0.5); // explicitly underdamped
        QVERIFY(bouncy.overshoots());
        QCOMPARE(clampProgressForCurve(1.4, &bouncy), 1.4);
        QCOMPARE(clampProgressForCurve(-0.3, &bouncy), -0.3);
    }

    // The overshoot is kept, but it is BOUNDED — a hand-edited profile or a
    // third-party registered curve cannot hand the shader an arbitrary iTime and
    // fling the window off-screen. The envelope is the same one the geometry
    // animator interpolates within, so the two cannot disagree.
    void testClampPolicyBoundsOvershootToTheEnvelope()
    {
        const Spring bouncy(12.0, 0.5);
        QVERIFY(bouncy.overshoots());
        QCOMPARE(clampProgressForCurve(9.0, &bouncy), Limits::MaxCurveProgress);
        QCOMPARE(clampProgressForCurve(-4.0, &bouncy), Limits::MinCurveProgress);
        // The envelope's own edges survive intact.
        QCOMPARE(clampProgressForCurve(Limits::MaxCurveProgress, &bouncy), Limits::MaxCurveProgress);
        QCOMPARE(clampProgressForCurve(Limits::MinCurveProgress, &bouncy), Limits::MinCurveProgress);
    }

    // The envelope must never fire for a curve the library itself produces, or it
    // would be silently reshaping bundled animations rather than backstopping bad
    // ones. A spring is the curve that comes closest: an UNDAMPED one peaks at
    // exactly MaxCurveProgress, which is where that number came from.
    void testEnvelopeNeverClipsAnUndampedSpring()
    {
        const Spring undamped(12.0, 0.0); // zeta = 0 — the theoretical worst case
        QVERIFY(undamped.overshoots());
        CurveState state;
        qreal peak = 0.0;
        for (int i = 0; i < 400; ++i) {
            undamped.step(0.016, state, 1.0);
            peak = qMax(peak, state.value);
        }
        QVERIFY2(peak > 1.9, qPrintable(QStringLiteral("undamped spring should approach 2.0, peaked at %1").arg(peak)));
        QVERIFY2(peak <= Limits::MaxCurveProgress + 1.0e-9,
                 qPrintable(QStringLiteral("undamped spring exceeded the envelope at %1 — the envelope is "
                                           "supposed to be the spring ceiling, so either the integrator or "
                                           "MaxCurveProgress is wrong")
                                .arg(peak)));
    }

    // An OVERSHOOTING curve is NOT clamped — the overshoot is the curve, and the
    // geometry animator bounces past its target on the same pick, so flattening
    // it here would make the shader and the geometry disagree.
    //
    // The damping is stated explicitly rather than taken from a preset: a QSKIP on
    // "preset turned out not to be underdamped" would let a retuned preset silently
    // stop testing the passthrough while the suite stayed green.
    void testEaseLetsOvershootThrough()
    {
        const Spring spring(12.0, 0.5); // explicitly underdamped
        QVERIFY2(spring.overshoots(), "test setup must be underdamped");
        CurveState state;
        qreal peak = 0.0;
        // Integrate at a steady 16 ms for two seconds and watch for a value > 1.
        for (qint64 t = 0; t <= 2000; t += 16) {
            const qreal p = easeProgress(&spring, state, t == 0 ? -1 : t - 16, t, 0.0, /*stepCurve=*/true);
            peak = qMax(peak, p);
        }
        QVERIFY2(peak > 1.0, "an underdamped spring must deliver its overshoot to iTime");
    }

    // stepCurve == false is a PEEK: it must not advance the integrator. The
    // backdrop predictor relies on this to read the drawn progress without
    // double-stepping the step that paintWindow owns.
    void testEasePeekDoesNotAdvanceIntegrator()
    {
        const Spring spring = Spring::snappy();
        CurveState state;
        easeProgress(&spring, state, -1, 0, 0.0, /*stepCurve=*/true); // seed lastPaint
        easeProgress(&spring, state, 0, 16, 0.0, /*stepCurve=*/true); // one real step
        const qreal afterStep = state.value;

        easeProgress(&spring, state, 16, 32, 0.0, /*stepCurve=*/false); // peek
        QCOMPARE(state.value, afterStep);
        easeProgress(&spring, state, 16, 999999, 0.0, /*stepCurve=*/false); // peek, huge gap
        QCOMPARE(state.value, afterStep);
    }

    // lastPaintTimeMs < 0 is the "no prior paint" sentinel and yields dt = 0, so
    // the first frame of a transition does not integrate a garbage delta.
    void testEaseFirstFrameHasZeroDelta()
    {
        const Spring spring = Spring::snappy();
        CurveState fresh;
        easeProgress(&spring, fresh, -1, 5000, 0.0, /*stepCurve=*/true);
        QCOMPARE(fresh.value, 0.0);
        QCOMPARE(fresh.velocity, 0.0);
    }

    // A frame hitch (suspend/resume, VT switch) must not teleport the spring:
    // Spring::step is an exact exponential integrator, unconditionally stable at
    // any dt, so the cap on MaxShaderTimeDeltaSeconds bounds how far a stall
    // JUMPS, not whether the integrator survives it — a minute-long stall
    // advances the spring no further than a 100 ms frame.
    //
    // Compared with a tolerance rather than exactly: the cap is a `float`
    // (0.1f), so the stalled path integrates the float-promoted
    // 0.10000000149... while the 100 ms path integrates a clean 0.1 double. The
    // point of the test is that the stall does not RUN AWAY, which a bit-exact
    // compare would obscure behind a last-digit mismatch.
    void testEaseCapsHugeFrameDelta()
    {
        const Spring spring = Spring::snappy();
        CurveState capped;
        CurveState huge;
        easeProgress(&spring, capped, 0, 100, 0.0, true); // exactly the cap
        easeProgress(&spring, huge, 0, 60000, 0.0, true); // a minute-long stall
        QVERIFY(qAbs(huge.value - capped.value) < 1e-6);
        QVERIFY(qAbs(huge.velocity - capped.velocity) < 1e-4);
        // And the integrator stayed sane rather than diverging.
        QVERIFY(std::isfinite(huge.value));
        QVERIFY(huge.value < 2.0);
    }
};

QTEST_MAIN(TestShaderTiming)
#include "test_shader_timing.moc"
