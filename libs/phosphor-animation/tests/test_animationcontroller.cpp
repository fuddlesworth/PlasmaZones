// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationController.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <QSet>
#include <QTest>

#include <chrono>
#include <memory>

using PhosphorAnimation::AnimationController;
using PhosphorAnimation::Easing;
using PhosphorAnimation::Spring;
using PhosphorAnimation::WindowMotion;

namespace {

/// Mock controller using a plain int as the handle type. Records hook
/// invocations so tests can assert lifecycle ordering without a real
/// compositor. The controller is otherwise the production class — only
/// the virtual hooks are observed.
class MockController : public AnimationController<int>
{
public:
    QSet<int> startedHandles;
    QSet<int> completedHandles;
    mutable int repaintCalls = 0; ///< mutable: onRepaintNeeded is const
    QSet<int> invalidHandles;

protected:
    void onAnimationStarted(int handle, const WindowMotion&) override
    {
        startedHandles.insert(handle);
    }
    void onAnimationComplete(int handle, const WindowMotion&) override
    {
        completedHandles.insert(handle);
    }
    void onRepaintNeeded(int /*handle*/, const QRectF&) const override
    {
        ++repaintCalls;
    }
    bool isHandleValid(int handle) const override
    {
        return !invalidHandles.contains(handle);
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
        MockController c;
        c.setEnabled(false);
        c.setDuration(250.0);
        c.setMinDistance(7);
        auto curve = std::make_shared<Easing>();
        c.setCurve(curve);

        QCOMPARE(c.isEnabled(), false);
        QCOMPARE(c.duration(), 250.0);
        QCOMPARE(c.minDistance(), 7);
        QVERIFY(c.curve() == curve);
    }

    void testSetMinDistanceClampsNegativesToZero()
    {
        MockController c;
        c.setMinDistance(-100);
        QCOMPARE(c.minDistance(), 0);
    }

    // ─── startAnimation gating ───

    void testStartAnimationSkippedWhenDisabled()
    {
        MockController c;
        c.setEnabled(false);
        c.setCurve(std::make_shared<Easing>());

        const bool started = c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100));
        QVERIFY(!started);
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.startedHandles.isEmpty());
    }

    void testStartAnimationSkippedWhenGeometryRejected()
    {
        MockController c;
        c.setCurve(std::make_shared<Easing>());

        // Degenerate target — createSnapMotion returns nullopt.
        const bool started = c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(0, 0, 0, 100));
        QVERIFY(!started);
        QVERIFY(!c.hasAnimation(1));
    }

    void testStartAnimationFiresHookAndStoresMotion()
    {
        MockController c;
        c.setCurve(std::make_shared<Easing>());

        const bool started = c.startAnimation(42, QPointF(0, 0), QSizeF(100, 100), QRect(500, 0, 100, 100));
        QVERIFY(started);
        QVERIFY(c.hasAnimation(42));
        QVERIFY(c.hasActiveAnimations());
        QVERIFY(c.startedHandles.contains(42));
    }

    // ─── Polymorphic curve ───

    void testCurveDispatchPolymorphic()
    {
        MockController c;
        // Spring drives the controller through the same setter as Easing
        // — that's the Phase-2 polymorphic upgrade in action.
        c.setCurve(std::make_shared<Spring>(Spring::snappy()));
        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));

        const auto* motion = c.motionFor(1);
        QVERIFY(motion != nullptr);
        QVERIFY(motion->curve != nullptr);
        QCOMPARE(motion->curve->typeId(), QStringLiteral("spring"));
    }

    // ─── Per-frame progression ───

    void testAdvanceCachesProgressAndCompletes()
    {
        MockController c;
        c.setDuration(100.0);
        c.setCurve(std::make_shared<Easing>());
        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));

        // First tick latches startTime, progress = 0.
        c.advanceAnimations(std::chrono::milliseconds(0));
        QVERIFY(c.hasAnimation(1));
        QVERIFY(c.completedHandles.isEmpty());

        // Halfway tick progresses but stays alive.
        c.advanceAnimations(std::chrono::milliseconds(50));
        QVERIFY(c.hasAnimation(1));

        // Past the duration completes + removes + fires hooks.
        c.advanceAnimations(std::chrono::milliseconds(150));
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.completedHandles.contains(1));
        QVERIFY(c.repaintCalls >= 1);
    }

    void testInvalidHandlesArePruned()
    {
        MockController c;
        c.setCurve(std::make_shared<Easing>());
        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));

        c.invalidHandles.insert(1);
        c.advanceAnimations(std::chrono::milliseconds(0));

        // Handle 1 was invalid — pruned without firing onAnimationComplete.
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(2));
        QVERIFY(!c.completedHandles.contains(1));
    }

    // ─── State queries ───

    void testCurrentVisualPositionFallback()
    {
        MockController c;
        // No motion registered → returns the supplied fallback.
        QCOMPARE(c.currentVisualPosition(99, QPointF(123, 456)), QPointF(123, 456));
        QCOMPARE(c.currentVisualSize(99, QSizeF(789, 321)), QSizeF(789, 321));
    }

    void testIsAnimatingToTarget()
    {
        MockController c;
        c.setCurve(std::make_shared<Easing>());
        const QRect target(500, 0, 100, 100);
        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), target));

        QVERIFY(c.isAnimatingToTarget(1, target));
        QVERIFY(!c.isAnimatingToTarget(1, QRect(999, 999, 100, 100)));
        QVERIFY(!c.isAnimatingToTarget(2, target));
    }

    void testRemoveAndClear()
    {
        MockController c;
        c.setCurve(std::make_shared<Easing>());
        c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100));
        c.startAnimation(2, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100));

        c.removeAnimation(1);
        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(2));

        c.clear();
        QVERIFY(!c.hasActiveAnimations());
    }

    // ─── animationBounds + scheduleRepaints integration ───

    void testScheduleRepaintsFiresPerActiveAnimation()
    {
        MockController c;
        c.setCurve(std::make_shared<Easing>());
        c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100));
        c.startAnimation(2, QPointF(0, 0), QSizeF(100, 100), QRect(500, 0, 100, 100));

        const int before = c.repaintCalls;
        c.scheduleRepaints();
        QCOMPARE(c.repaintCalls - before, 2);
    }

    void testAnimationBoundsCoversStartAndTarget()
    {
        MockController c;
        c.setCurve(std::make_shared<Easing>());
        QVERIFY(c.startAnimation(1, QPointF(10, 10), QSizeF(100, 100), QRect(500, 200, 100, 100)));

        const QRectF bounds = c.animationBounds(1);
        QVERIFY(bounds.contains(QRectF(10, 10, 100, 100).adjusted(-1, -1, 1, 1)));
        QVERIFY(bounds.contains(QRectF(500, 200, 100, 100).adjusted(-1, -1, 1, 1)));
    }
};

QTEST_MAIN(TestAnimationController)
#include "test_animationcontroller.moc"
