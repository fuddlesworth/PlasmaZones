// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Spring.h>

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTest>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

using PhosphorAnimation::AnimatedValue;
using PhosphorAnimation::Easing;
using PhosphorAnimation::IMotionClock;
using PhosphorAnimation::MotionSpec;
using PhosphorAnimation::RetargetPolicy;
using PhosphorAnimation::Spring;

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
    const void* epochIdentity() const override
    {
        return IMotionClock::steadyClockEpoch();
    }

    void advanceMs(qreal ms)
    {
        m_now += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<qreal, std::milli>(ms));
    }

private:
    std::chrono::nanoseconds m_now{0};
};

template<typename T>
MotionSpec<T> makeSpec(TestClock* clock, std::shared_ptr<const PhosphorAnimation::Curve> curve,
                       qreal durationMs = 100.0)
{
    MotionSpec<T> spec;
    spec.profile.curve = std::move(curve);
    spec.profile.duration = durationMs;
    spec.clock = clock;
    return spec;
}

} // namespace

class TestAnimatedValueGeometry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── QPointF ───

    void testPointFStartsAtFrom()
    {
        TestClock clock;
        AnimatedValue<QPointF> v;
        QVERIFY(v.start(QPointF(10, 20), QPointF(310, 220), makeSpec<QPointF>(&clock, std::make_shared<Easing>())));
        QCOMPARE(v.value(), QPointF(10, 20));
    }

    void testPointFMidProgression()
    {
        TestClock clock;
        AnimatedValue<QPointF> v;
        auto linear = std::make_shared<Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        v.start(QPointF(0, 0), QPointF(100, 200), makeSpec<QPointF>(&clock, linear));
        v.advance(); // latch
        clock.advanceMs(50.0);
        v.advance();

        // At roughly mid-progress, we should be roughly halfway.
        const QPointF p = v.value();
        QVERIFY(p.x() > 20.0 && p.x() < 80.0);
        QVERIFY(p.y() > 40.0 && p.y() < 160.0);
    }

    void testPointFBoundsCoversStartAndTarget()
    {
        TestClock clock;
        AnimatedValue<QPointF> v;
        v.start(QPointF(10, 10), QPointF(500, 200), makeSpec<QPointF>(&clock, std::make_shared<Easing>()));
        const QRectF b = v.bounds();
        QVERIFY(b.contains(QPointF(10, 10)));
        QVERIFY(b.contains(QPointF(500, 200)));
    }

    // ─── QSizeF ───

    void testSizeFLerp()
    {
        TestClock clock;
        AnimatedValue<QSizeF> v;
        auto linear = std::make_shared<Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        v.start(QSizeF(100, 100), QSizeF(500, 300), makeSpec<QSizeF>(&clock, linear));
        v.advance();
        clock.advanceMs(50.0);
        v.advance();

        const QSizeF s = v.value();
        QVERIFY(s.width() > 150.0 && s.width() < 450.0);
        QVERIFY(s.height() > 120.0 && s.height() < 280.0);
    }

    void testSizeFBoundsAtExplicitAnchor()
    {
        // QSizeF has no inherent position — the API forces the caller
        // to name an anchor via `boundsAt(QPointF)`. A plain
        // `bounds()` call would not compile (enforced by
        // detail::PositionalGeometric), which is the whole point of
        // the split: the origin-at-(0, 0) trap is unrepresentable.
        TestClock clock;
        AnimatedValue<QSizeF> v;
        v.start(QSizeF(100, 100), QSizeF(500, 300), makeSpec<QSizeF>(&clock, std::make_shared<Easing>()));

        const QRectF atOrigin = v.boundsAt(QPointF(0, 0));
        QVERIFY(atOrigin.contains(QRectF(0, 0, 100, 100)));
        QVERIFY(atOrigin.contains(QRectF(0, 0, 500, 300)));

        const QRectF atPopup = v.boundsAt(QPointF(500, 300));
        QVERIFY(atPopup.contains(QRectF(500, 300, 100, 100)));
        QVERIFY(atPopup.contains(QRectF(500, 300, 500, 300)));
        // Anchor stays where the caller put it — no silent origin
        // fallback.
        QCOMPARE(atPopup.topLeft(), QPointF(500, 300));
    }

    void testSizeFSweptSizeReturnsEnvelopePair()
    {
        // sweptSize() is the raw endpoint-pair accessor — pairs with
        // sweptRange() for scalars. Consumers doing their own anchor
        // math use this when the anchor is unknown at the moment
        // bounds are needed (e.g., a reflowing layout that hasn't
        // decided the final origin yet).
        TestClock clock;
        AnimatedValue<QSizeF> v;
        v.start(QSizeF(100, 100), QSizeF(500, 300), makeSpec<QSizeF>(&clock, std::make_shared<Easing>()));

        const auto [lo, hi] = v.sweptSize();
        QCOMPARE(lo, QSizeF(100, 100));
        QCOMPARE(hi, QSizeF(500, 300));
    }

    // ─── QRectF ───

    void testRectFLerp()
    {
        TestClock clock;
        AnimatedValue<QRectF> v;
        auto linear = std::make_shared<Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        v.start(QRectF(0, 0, 100, 100), QRectF(200, 200, 400, 400), makeSpec<QRectF>(&clock, linear));
        v.advance();
        clock.advanceMs(50.0);
        v.advance();

        const QRectF r = v.value();
        // Mid-progress rect should overlap both start and target but
        // match neither exactly.
        QVERIFY(r != QRectF(0, 0, 100, 100));
        QVERIFY(r != QRectF(200, 200, 400, 400));
        QVERIFY(r.x() > 50.0 && r.x() < 150.0);
        QVERIFY(r.width() > 150.0 && r.width() < 350.0);
    }

    void testRectFBoundsCoversStartAndTarget()
    {
        TestClock clock;
        AnimatedValue<QRectF> v;
        v.start(QRectF(0, 0, 100, 100), QRectF(500, 200, 300, 250),
                makeSpec<QRectF>(&clock, std::make_shared<Easing>()));
        const QRectF b = v.bounds();
        QVERIFY(b.contains(QRectF(0, 0, 100, 100)));
        QVERIFY(b.contains(QRectF(500, 200, 300, 250)));
    }

    void testRectFBoundsSamplesElasticOvershoot()
    {
        TestClock clock;
        AnimatedValue<QRectF> v;
        auto elastic = std::make_shared<Easing>();
        elastic->type = Easing::Type::ElasticOut;
        elastic->amplitude = 1.5;
        elastic->period = 0.3;

        v.start(QRectF(0, 0, 100, 100), QRectF(200, 0, 100, 100), makeSpec<QRectF>(&clock, elastic));
        const QRectF b = v.bounds();
        // Naive union: (0, 0, 300, 100). Elastic overshoot pushes
        // beyond the target (right) or before the start (left).
        const QRectF naive(0, 0, 300, 100);
        QVERIFY(b.left() <= naive.left() || b.right() >= naive.right());
    }

    // ─── QRectF completion ───

    void testRectFCompletesAtTarget()
    {
        TestClock clock;
        AnimatedValue<QRectF> v;
        const QRectF target(500, 500, 200, 200);
        v.start(QRectF(0, 0, 100, 100), target, makeSpec<QRectF>(&clock, std::make_shared<Easing>(), 100.0));
        v.advance();
        clock.advanceMs(150.0);
        v.advance();
        QVERIFY(v.isComplete());
        QCOMPARE(v.value(), target);
    }

    // ─── QRectF retarget ───

    void testRectFRetargetPreservesVisualRect()
    {
        TestClock clock;
        AnimatedValue<QRectF> v;
        v.start(QRectF(0, 0, 100, 100), QRectF(500, 500, 200, 200),
                makeSpec<QRectF>(&clock, std::make_shared<Easing>()));
        v.advance();
        clock.advanceMs(40.0);
        v.advance();

        const QRectF mid = v.value();
        QVERIFY(v.retarget(QRectF(1000, 1000, 300, 300), RetargetPolicy::PreservePosition));
        QCOMPARE(v.value(), mid); // no visual jump
    }
};

QTEST_MAIN(TestAnimatedValueGeometry)
#include "test_animatedvalue_geometry.moc"
