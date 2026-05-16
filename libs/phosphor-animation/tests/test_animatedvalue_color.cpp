// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TestClock.h"

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>

#include <QColor>
#include <QTest>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

using PhosphorAnimation::AnimatedValue;
using PhosphorAnimation::ColorSpace;
using PhosphorAnimation::Easing;
using PhosphorAnimation::IMotionClock;
using PhosphorAnimation::MotionSpec;
using TestClock = PhosphorAnimation::Testing::TestClock;

// Detection concepts for the negative type-system assertions in
// `testNoGeometricBoundsForColor`. Named concepts evaluate in an
// unambiguous constraint-checking context (unlike ad-hoc `requires`
// expressions inline at the static_assert site, which can trigger
// GCC's eager instantiation of the constrained member).
template<typename T>
concept HasBounds = requires(const T& v) { v.bounds(); };
template<typename T>
concept HasSweptRange = requires(const T& v) { v.sweptRange(); };

namespace {

template<ColorSpace Space = ColorSpace::Linear>
MotionSpec<QColor> makeSpec(TestClock* clock, qreal durationMs = 100.0)
{
    MotionSpec<QColor> spec;
    // Use a linear-in-t bezier so cachedProgress at t=0.5 is ≈0.5.
    auto linear = std::make_shared<Easing>();
    linear->x1 = 0.0;
    linear->y1 = 0.0;
    linear->x2 = 1.0;
    linear->y2 = 1.0;
    spec.profile.curve = linear;
    spec.profile.duration = durationMs;
    spec.clock = clock;
    return spec;
}

// Force cachedProgress to exactly @p t by running advance() at the right
// simulated time. Returns the mid-lerp value for inspection.
template<ColorSpace Space>
QColor sampleAtProgress(AnimatedValue<QColor, Space>& v, TestClock& clock, qreal t)
{
    v.advance(); // latch at t=0
    clock.advanceMs(100.0 * t);
    v.advance();
    return v.value();
}

} // namespace

class TestAnimatedValueColor : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── sRGB component-wise would give grey; linear-space must not ───

    void testLinearSpaceMidpointOfRedAndGreenIsNotGrey()
    {
        // Midpoint of sRGB #FF0000 ↔ #00FF00 under sRGB component-wise
        // lerp is (0.5, 0.5, 0.0) — a muddy dark yellow-grey.
        // Linear-space lerp brightens the midpoint (both sources
        // become ~0.8 luma after sRGB→linear; their average is still
        // luminous when re-gamma'd). That's the visible correctness
        // that the default Linear mode delivers.
        TestClock clock;
        AnimatedValue<QColor> v;
        v.start(QColor(Qt::red), QColor(Qt::green), makeSpec(&clock));

        const QColor mid = sampleAtProgress(v, clock, 0.5);

        // sRGB component-wise midpoint of red & green is
        //   (0.5, 0.5, 0.0) → a grey-khaki.
        // Linear-space midpoint is brighter because linear (0.5) maps
        // back to sRGB ~0.735 (not 0.5). So the red+green components
        // should be distinctly brighter than 0.5.
        QVERIFY(mid.redF() > 0.65);
        QVERIFY(mid.greenF() > 0.65);
        QVERIFY(mid.blueF() < 0.1);
    }

    // ─── OkLab opt-in produces a different midpoint ───

    void testOkLabMidpointDiffersFromLinear()
    {
        TestClock clock1, clock2;

        AnimatedValue<QColor, ColorSpace::Linear> linear;
        linear.start(QColor(Qt::red), QColor(Qt::blue), makeSpec(&clock1));

        AnimatedValue<QColor, ColorSpace::OkLab> oklab;
        oklab.start(QColor(Qt::red), QColor(Qt::blue), makeSpec<ColorSpace::OkLab>(&clock2));

        const QColor midLinear = sampleAtProgress(linear, clock1, 0.5);
        const QColor midOkLab = sampleAtProgress(oklab, clock2, 0.5);

        // Physics-grounded assertion instead of a loose "channels differ
        // by at least 0.1" gate. A broken OkLab path that silently
        // degraded to linear RGB interpolation would still pass a loose
        // delta check because float rounding accumulates through the
        // OkLab chain's matrix multiplies — the loose gate doesn't
        // distinguish "OkLab produced a correct perceptual midpoint"
        // from "OkLab degraded to linear with bonus noise".
        //
        // The distinguishing property: linear-space lerp of red(1,0,0)
        // and blue(0,0,1) in linear RGB gives midpoint (0.5, 0, 0.5),
        // which is pure magenta — green is EXACTLY zero because no
        // cross-term in the lerp generates it. OkLab's perceptually-
        // uniform midpoint MUST have a substantial positive green
        // channel because the red↔blue perceptual path passes through
        // colour space that biases toward luminance-balanced midtones
        // (which requires green contribution). A degraded
        // implementation cannot fake this — green ≈ 0 is the linear
        // signature, green > 0.15 is the OkLab signature, and nothing
        // in between is reachable by the two algorithms.
        QVERIFY2(midLinear.greenF() < 0.02,
                 "Linear midpoint of red+blue MUST have ~zero green (no cross-term in linear RGB lerp)");
        QVERIFY2(midOkLab.greenF() > 0.15,
                 "OkLab midpoint of red+blue MUST have substantial green channel — a smaller value "
                 "means the OkLab path silently degraded to linear RGB interpolation");

        // Belt-and-suspenders: the two paths must ALSO produce
        // numerically distinct midpoints across all channels, not
        // merely on green. Red and blue channels should each diverge
        // by at least 0.05 (13/255) — the analytical gap is ≈0.16 on
        // red and ≈0.07 on blue, so 0.05 is a conservative floor
        // that still rejects accidental linear-RGB degradation.
        QVERIFY(qAbs(midLinear.redF() - midOkLab.redF()) > 0.05);
        QVERIFY(qAbs(midLinear.blueF() - midOkLab.blueF()) > 0.05);
    }

    // ─── Endpoints round-trip exactly ───

    void testEndpointsExact()
    {
        TestClock clock;
        AnimatedValue<QColor> v;
        v.start(QColor(255, 128, 64), QColor(32, 96, 200), makeSpec(&clock));

        // Start: value() should equal from after latching.
        v.advance();
        const QColor start = v.value();
        QCOMPARE(start.red(), 255);
        QCOMPARE(start.green(), 128);
        QCOMPARE(start.blue(), 64);

        // Completion: value() should equal to exactly.
        clock.advanceMs(150.0);
        v.advance();
        const QColor end = v.value();
        QCOMPARE(end.red(), 32);
        QCOMPARE(end.green(), 96);
        QCOMPARE(end.blue(), 200);
    }

    void testOkLabEndpointsExact()
    {
        TestClock clock;
        AnimatedValue<QColor, ColorSpace::OkLab> v;
        v.start(QColor(255, 128, 64), QColor(32, 96, 200), makeSpec<ColorSpace::OkLab>(&clock));

        v.advance();
        const QColor start = v.value();
        QCOMPARE(start.red(), 255);
        QCOMPARE(start.green(), 128);
        QCOMPARE(start.blue(), 64);

        clock.advanceMs(150.0);
        v.advance();
        const QColor end = v.value();
        QCOMPARE(end.red(), 32);
        QCOMPARE(end.green(), 96);
        QCOMPARE(end.blue(), 200);
    }

    // ─── Alpha lerps independently of gamma ───

    void testAlphaInterpolatesLinearly()
    {
        TestClock clock;
        AnimatedValue<QColor> v;
        QColor from(255, 0, 0, 0);
        QColor to(255, 0, 0, 255);
        v.start(from, to, makeSpec(&clock));

        const QColor mid = sampleAtProgress(v, clock, 0.5);
        // Alpha lerps linearly — ~128 at t=0.5, independent of gamma.
        QVERIFY(qAbs(mid.alpha() - 128) < 5);
    }

    // ─── No bounds() / sweptRange() on QColor ───

    void testNoGeometricBoundsForColor()
    {
        // Compile-time negative test: AnimatedValue<QColor>::bounds()
        // must not exist (the concept guard excludes QColor) and
        // `sweptRange()` must not exist (QColor is not arithmetic).
        // Expressed via named concepts evaluated in unambiguous
        // constraint-checking context — a regression that makes
        // either method callable on QColor fails the build at the
        // static_assert, not just the compile of a body that happened
        // to invoke the method. The previous `QVERIFY(true)` pattern
        // passed whether or not the methods existed.
        static_assert(!HasBounds<AnimatedValue<QColor>>, "bounds() must not be callable on AnimatedValue<QColor>");
        static_assert(!HasSweptRange<AnimatedValue<QColor>>,
                      "sweptRange() must not be callable on AnimatedValue<QColor>");
        static_assert(!HasBounds<AnimatedValue<QColor, ColorSpace::OkLab>>,
                      "bounds() must not be callable on AnimatedValue<QColor, OkLab>");
    }
};

QTEST_MAIN(TestAnimatedValueColor)
#include "test_animatedvalue_color.moc"
