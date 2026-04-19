// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>

#include <QPointF>
#include <QTest>
#include <QTransform>
#include <QtMath>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

using PhosphorAnimation::AnimatedValue;
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

MotionSpec<QTransform> makeSpec(TestClock* clock, qreal durationMs = 100.0)
{
    MotionSpec<QTransform> spec;
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

QTransform rotateTransform(qreal degrees)
{
    QTransform t;
    t.rotate(degrees);
    return t;
}

QTransform sampleAtProgress(AnimatedValue<QTransform>& v, TestClock& clock, qreal t)
{
    v.advance();
    clock.advanceMs(100.0 * t);
    v.advance();
    return v.value();
}

} // namespace

class TestAnimatedValueTransform : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Endpoints ───

    void testIdentityStart()
    {
        TestClock clock;
        AnimatedValue<QTransform> v;
        v.start(QTransform(), rotateTransform(90.0), makeSpec(&clock));
        v.advance();
        const QTransform start = v.value();
        // At t=0 we should be at identity (approximately).
        QVERIFY(qAbs(start.m11() - 1.0) < 1.0e-9);
        QVERIFY(qAbs(start.m22() - 1.0) < 1.0e-9);
        QVERIFY(qAbs(start.m12()) < 1.0e-9);
        QVERIFY(qAbs(start.m21()) < 1.0e-9);
    }

    void testTargetExactAtCompletion()
    {
        TestClock clock;
        AnimatedValue<QTransform> v;
        QTransform target = rotateTransform(90.0);
        v.start(QTransform(), target, makeSpec(&clock));
        v.advance();
        clock.advanceMs(150.0);
        v.advance();
        const QTransform end = v.value();
        // Completion snap — exact target (within float tolerance).
        QVERIFY(qAbs(end.m11() - target.m11()) < 1.0e-6);
        QVERIFY(qAbs(end.m22() - target.m22()) < 1.0e-6);
        QVERIFY(qAbs(end.m12() - target.m12()) < 1.0e-6);
        QVERIFY(qAbs(end.m21() - target.m21()) < 1.0e-6);
    }

    // ─── Rotation through 45° without shear ───

    void testRotationMidpointIsRotate45()
    {
        // Component-wise lerp of identity → rotate(90°) produces at
        // t=0.5 the matrix [[0.5, -0.5], [0.5, 0.5]] — that's not a
        // rotation (it has scale ≠ 1 and would squash). Polar-
        // decomposed lerp gives rotate(45°) = [[√2/2, -√2/2], [√2/2, √2/2]].
        TestClock clock;
        AnimatedValue<QTransform> v;
        v.start(QTransform(), rotateTransform(90.0), makeSpec(&clock));

        const QTransform mid = sampleAtProgress(v, clock, 0.5);
        const qreal expected = std::sqrt(2.0) / 2.0; // cos(45°) = sin(45°)

        // Under Qt's post-multiply row-vector convention,
        // rotate(θ) produces the matrix:
        //   [[cos, sin], [-sin, cos]]
        // So rotate(45°) has m11 = m12 = m22 = cos(45°) = √2/2
        // and m21 = -sin(45°) = -√2/2.
        QVERIFY(qAbs(mid.m11() - expected) < 1.0e-3);
        QVERIFY(qAbs(mid.m22() - expected) < 1.0e-3);
        QVERIFY(qAbs(mid.m12() - expected) < 1.0e-3); // +sin(45°)
        QVERIFY(qAbs(mid.m21() + expected) < 1.0e-3); // -sin(45°)
    }

    void testRotationMidpointIsNotComponentWiseLerp()
    {
        // The component-wise midpoint of identity and rotate(90°) has
        // the 2x2 matrix [[0.5, -0.5], [0.5, 0.5]] — a rotation by 45°
        // scaled by √2/2. The sum of diagonals is 1.0. A rotation by
        // exactly 45° has diagonals summing to ≈1.414. Asserting the
        // diagonal sum > 1.1 is a loose but reliable distinguishing
        // check: component-wise would fail, decomposed passes.
        TestClock clock;
        AnimatedValue<QTransform> v;
        v.start(QTransform(), rotateTransform(90.0), makeSpec(&clock));

        const QTransform mid = sampleAtProgress(v, clock, 0.5);
        QVERIFY(mid.m11() + mid.m22() > 1.1);
    }

    // ─── Translation interpolates linearly ───

    void testTranslationInterpolatesLinearly()
    {
        TestClock clock;
        AnimatedValue<QTransform> v;
        QTransform from = QTransform::fromTranslate(0, 0);
        QTransform to = QTransform::fromTranslate(200, 100);
        v.start(from, to, makeSpec(&clock));

        const QTransform mid = sampleAtProgress(v, clock, 0.5);
        QVERIFY(qAbs(mid.dx() - 100.0) < 1.0);
        QVERIFY(qAbs(mid.dy() - 50.0) < 1.0);
    }

    // ─── Scale interpolates linearly ───

    void testScaleInterpolatesLinearly()
    {
        TestClock clock;
        AnimatedValue<QTransform> v;
        QTransform from = QTransform::fromScale(1.0, 1.0);
        QTransform to = QTransform::fromScale(3.0, 2.0);
        v.start(from, to, makeSpec(&clock));

        const QTransform mid = sampleAtProgress(v, clock, 0.5);
        // Midway between sx=1 and sx=3 is 2; between sy=1 and sy=2 is 1.5.
        QVERIFY(qAbs(mid.m11() - 2.0) < 0.05);
        QVERIFY(qAbs(mid.m22() - 1.5) < 0.05);
    }

    // ─── Shortest-arc slerp ───

    void testShortestArcRotation()
    {
        // Going from 10° to 350° should go via 0° (the short way),
        // not via 180° (the long way). Shortest-arc slerp enforces
        // this. Check the midpoint: short-way midpoint is at 0°
        // (or 360°, same rotation), long-way midpoint would be at 180°.
        TestClock clock;
        AnimatedValue<QTransform> v;
        v.start(rotateTransform(10.0), rotateTransform(350.0), makeSpec(&clock));

        const QTransform mid = sampleAtProgress(v, clock, 0.5);
        // Short-way midpoint is rotate(0°) ≈ identity: m11 ≈ 1, m12 ≈ 0.
        // Long-way midpoint would be rotate(180°): m11 ≈ -1, m12 ≈ 0.
        QVERIFY(mid.m11() > 0.9); // positive ≈ 1 confirms short-way
    }
};

QTEST_MAIN(TestAnimatedValueTransform)
#include "test_animatedvalue_transform.moc"
