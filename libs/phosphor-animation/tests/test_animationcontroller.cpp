// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationController.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <QList>
#include <QSet>
#include <QTest>

#include <chrono>
#include <functional>
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
    QList<int> startedHandlesOrdered;
    QSet<int> completedHandles;
    QList<int> completedHandlesOrdered;
    mutable int repaintCalls = 0; ///< mutable: onRepaintNeeded is const
    QSet<int> invalidHandles;

    /// Optional hook injected into onAnimationComplete so tests can
    /// exercise the re-entrancy contract without subclassing again.
    std::function<void(int, MockController&)> onCompleteCallback;

protected:
    void onAnimationStarted(int handle, const WindowMotion&) override
    {
        startedHandles.insert(handle);
        startedHandlesOrdered.append(handle);
    }
    void onAnimationComplete(int handle, const WindowMotion&) override
    {
        completedHandles.insert(handle);
        completedHandlesOrdered.append(handle);
        if (onCompleteCallback) {
            onCompleteCallback(handle, *this);
        }
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

    // ─── Handle validity gate on startAnimation ───

    void testStartAnimationRejectsInvalidHandleUpFront()
    {
        // isHandleValid gate lives in startAnimation so an adapter can
        // centralise "is this handle real?" logic once and trust the
        // controller not to register anything downstream. Previously a
        // null/stale handle would be registered and only pruned on the
        // next advance tick, wasting a hash slot and (in an adapter
        // without guards) firing onAnimationStarted with a dead handle.
        MockController c;
        c.setCurve(std::make_shared<Easing>());
        c.invalidHandles.insert(7);

        const bool started = c.startAnimation(7, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100));
        QVERIFY(!started);
        QVERIFY(!c.hasAnimation(7));
        QVERIFY(c.startedHandles.isEmpty());
    }

    // ─── Retarget semantics ───

    void testRetargetOverwritesMotionAndFiresStartAgain()
    {
        // Calling startAnimation twice for the same handle overwrites
        // the in-flight motion and fires onAnimationStarted a second
        // time. The consumer is responsible for passing the current
        // visual state as the new start so the transition is smooth.
        MockController c;
        c.setCurve(std::make_shared<Easing>());

        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));
        QVERIFY(c.hasAnimation(1));
        QCOMPARE(c.motionFor(1)->targetGeometry, QRect(300, 0, 100, 100));

        QVERIFY(c.startAnimation(1, QPointF(50, 50), QSizeF(100, 100), QRect(900, 0, 100, 100)));
        QVERIFY(c.hasAnimation(1));
        QCOMPARE(c.motionFor(1)->targetGeometry, QRect(900, 0, 100, 100));
        QCOMPARE(c.motionFor(1)->startPosition, QPointF(50, 50));

        // Two start events, no completion in between.
        QCOMPARE(c.startedHandlesOrdered.size(), 2);
        QVERIFY(c.completedHandles.isEmpty());
    }

    // ─── Re-entrancy contract on onAnimationComplete ───

    void testReentrantStartAnimationInsideCompleteHookSurvives()
    {
        // A hook that calls startAnimation for the same handle must not
        // have its newly-registered motion deleted by the controller's
        // post-hook cleanup. Prior to the fix this silently dropped the
        // re-registered motion.
        MockController c;
        c.setDuration(100.0);
        c.setCurve(std::make_shared<Easing>());

        bool reentered = false;
        c.onCompleteCallback = [&](int handle, MockController& self) {
            if (!reentered) {
                reentered = true;
                // Chain a new animation off the completion of the first.
                self.startAnimation(handle, QPointF(300, 0), QSizeF(100, 100), QRect(600, 0, 100, 100));
            }
        };

        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));

        // Latch, advance to mid, advance past completion (fires hook).
        c.advanceAnimations(std::chrono::milliseconds(0));
        c.advanceAnimations(std::chrono::milliseconds(150));

        QVERIFY(reentered);
        // The completion hook re-registered a motion for the same handle;
        // it must still be present after advanceAnimations returns.
        QVERIFY(c.hasAnimation(1));
        QCOMPARE(c.motionFor(1)->targetGeometry, QRect(600, 0, 100, 100));
    }

    void testReentrantClearInsideCompleteHookIsSafe()
    {
        // A hook that clears all animations must not wedge the outer
        // advance loop or touch any already-invalid iterator. The
        // snapshot-then-lookup design guarantees this.
        MockController c;
        c.setDuration(50.0);
        c.setCurve(std::make_shared<Easing>());

        c.onCompleteCallback = [&](int, MockController& self) {
            self.clear();
        };

        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));
        QVERIFY(c.startAnimation(2, QPointF(0, 0), QSizeF(100, 100), QRect(500, 0, 100, 100)));

        c.advanceAnimations(std::chrono::milliseconds(0));
        // Past duration: both complete on the same tick. The first
        // completion clear()s the map; the loop must then skip handle 2
        // gracefully (find() returns end()).
        c.advanceAnimations(std::chrono::milliseconds(100));

        QVERIFY(!c.hasActiveAnimations());
        QVERIFY(c.completedHandles.size() >= 1); // first handle fires, second is pre-empted
    }

    void testReentrantStartForNewHandleInsideCompleteHookSurvives()
    {
        // Hook starts a motion for a *different* handle. The outer loop
        // was iterating over a snapshot, so the new insertion does not
        // invalidate anything and the new motion remains registered.
        MockController c;
        c.setDuration(50.0);
        c.setCurve(std::make_shared<Easing>());

        c.onCompleteCallback = [&](int handle, MockController& self) {
            if (handle == 1) {
                self.startAnimation(99, QPointF(0, 0), QSizeF(100, 100), QRect(700, 0, 100, 100));
            }
        };

        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));
        c.advanceAnimations(std::chrono::milliseconds(0));
        c.advanceAnimations(std::chrono::milliseconds(100));

        QVERIFY(!c.hasAnimation(1));
        QVERIFY(c.hasAnimation(99));
        QCOMPARE(c.motionFor(99)->targetGeometry, QRect(700, 0, 100, 100));
    }

    // ─── Curve swap semantics ───

    void testSetCurveMidFlightDoesNotAffectExistingMotions()
    {
        // Motions carry their curve by shared_ptr in WindowMotion::curve,
        // so swapping the controller-level curve after startAnimation
        // leaves in-flight motions untouched. New motions get the new
        // curve; null is a valid value meaning linear progression.
        MockController c;
        c.setDuration(100.0);
        auto elastic = std::make_shared<Easing>();
        elastic->type = Easing::Type::ElasticOut;
        elastic->amplitude = 1.5;
        c.setCurve(elastic);

        QVERIFY(c.startAnimation(1, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));
        QCOMPARE(c.motionFor(1)->curve.get(), elastic.get());

        c.setCurve(nullptr); // Swap to "no curve" (= linear).
        // In-flight motion 1 retains its elastic curve.
        QCOMPARE(c.motionFor(1)->curve.get(), elastic.get());

        // New motion 2 picks up the null curve → progresses linearly.
        QVERIFY(c.startAnimation(2, QPointF(0, 0), QSizeF(100, 100), QRect(300, 0, 100, 100)));
        QVERIFY(c.motionFor(2)->curve == nullptr);
    }

    // ─── MinDistance clamp ───

    void testSetMinDistanceClampsUpperBound()
    {
        // Upper clamp prevents a pathological config value from silently
        // disabling every animation below e.g. 100000px movement.
        MockController c;
        c.setMinDistance(1'000'000);
        QCOMPARE(c.minDistance(), 10000);
    }
};

QTEST_MAIN(TestAnimationController)
#include "test_animationcontroller.moc"
