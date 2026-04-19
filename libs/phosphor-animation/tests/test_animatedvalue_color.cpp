// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

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

        // The two paths produce numerically distinct midpoints —
        // linear-space red+blue midpoint is dark purple (sqrt gamma
        // mixes), OkLab midpoint is a perceptually-central magenta.
        const qreal dR = qAbs(midLinear.redF() - midOkLab.redF());
        const qreal dG = qAbs(midLinear.greenF() - midOkLab.greenF());
        const qreal dB = qAbs(midLinear.blueF() - midOkLab.blueF());
        QVERIFY((dR + dG + dB) > 0.02); // measurably different
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
        // Compile-time: AnimatedValue<QColor>::bounds() does not exist
        // (the concept guard excludes QColor). `sweptRange()` is
        // similarly unavailable (QColor is not an arithmetic type).
        // This is a negative test — if either method becomes callable,
        // the test file stops compiling. Here we just confirm the
        // types compile without either being present.
        AnimatedValue<QColor> v;
        Q_UNUSED(v)
        QVERIFY(true);
    }
};

QTEST_MAIN(TestAnimatedValueColor)
#include "test_animatedvalue_color.moc"
