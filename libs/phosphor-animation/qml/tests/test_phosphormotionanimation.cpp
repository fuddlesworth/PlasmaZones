// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/Spring.h>
#include <PhosphorAnimation/qml/PhosphorCurve.h>
#include <PhosphorAnimation/qml/PhosphorMotionAnimation.h>
#include <PhosphorAnimation/qml/PhosphorProfile.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <QEasingCurve>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>

#include <QtMath>

using namespace PhosphorAnimation;

class TestPhosphorMotionAnimation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        PhosphorProfileRegistry::instance().clear();
    }

    // ─── Profile dispatch (decision R) ───

    /// Default-constructed: resolvedProfile defaults, duration is
    /// Profile::DefaultDuration rounded to int ms (150).
    void testDefaults()
    {
        PhosphorMotionAnimation a;
        QCOMPARE(a.duration(), qRound(Profile::DefaultDuration));
        QVERIFY(!a.resolvedProfile().duration.has_value()); // No explicit duration set
    }

    /// PhosphorProfile value branch — snapshot installs verbatim,
    /// no registry indirection.
    void testProfileValueSnapshot()
    {
        PhosphorMotionAnimation a;
        PhosphorProfile p;
        p.setDuration(300.0);

        QSignalSpy spy(&a, &PhosphorMotionAnimation::profileChanged);
        a.setProfile(QVariant::fromValue(p));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(a.duration(), 300);
    }

    /// Path-string branch — resolves through the registry.
    void testProfilePathBranchResolvesViaRegistry()
    {
        Profile registered;
        registered.duration = 400.0;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("x"), registered);

        PhosphorMotionAnimation a;
        a.setProfile(QStringLiteral("x"));
        QCOMPARE(a.duration(), 400);
    }

    /// Path-string for a missing path — resolves to defaults, but
    /// live-rebinds when the path is later registered.
    void testProfilePathLiveRebinds()
    {
        PhosphorMotionAnimation a;
        a.setProfile(QStringLiteral("later"));
        QCOMPARE(a.duration(), qRound(Profile::DefaultDuration));

        Profile p;
        p.duration = 200.0;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("later"), p);
        QCOMPARE(a.duration(), 200);
    }

    /// profilesReloaded bulk-signal triggers re-resolve of the bound
    /// path.
    void testProfileReloadRebinds()
    {
        PhosphorMotionAnimation a;
        a.setProfile(QStringLiteral("bulk"));

        QHash<QString, Profile> all;
        Profile p;
        p.duration = 600.0;
        all.insert(QStringLiteral("bulk"), p);
        PhosphorProfileRegistry::instance().reloadAll(all);

        QCOMPARE(a.duration(), 600);
    }

    /// Switching from a path binding to a value snapshot disconnects
    /// the registry signal — subsequent registry writes to the OLD
    /// path must not affect the animation.
    void testSwitchFromPathToValueDisconnects()
    {
        Profile initial;
        initial.duration = 100.0;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("old"), initial);

        PhosphorMotionAnimation a;
        a.setProfile(QStringLiteral("old"));
        QCOMPARE(a.duration(), 100);

        PhosphorProfile snapshot;
        snapshot.setDuration(500.0);
        a.setProfile(QVariant::fromValue(snapshot));
        QCOMPARE(a.duration(), 500);

        // Updating the old registered path must not affect us now.
        Profile updated;
        updated.duration = 999.0;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("old"), updated);
        QCOMPARE(a.duration(), 500); // still the snapshot's value
    }

    /// Garbage QVariant input falls through to defaults rather than
    /// crashing.
    void testGarbageProfileFallsBackToDefault()
    {
        PhosphorMotionAnimation a;
        a.setProfile(QVariant::fromValue(42)); // int — unrecognised
        QCOMPARE(a.duration(), qRound(Profile::DefaultDuration));
    }

    // ─── Easing curve verification ───
    //
    // Note: we cannot construct a standalone QEasingCurve::BezierSpline
    // and call valueForProgress() in this test binary. Qt 6.10 on
    // aarch64 / GCC 15 has a heap-corruption bug in the standalone
    // QEasingCurve BezierSpline evaluator (QTBUG-132321). The
    // production path (QQuickPropertyAnimation::setEasing inside a
    // QML scene) is unaffected because the Qt Quick animation job
    // evaluates the spline through its own QAnimationJobUtils code
    // path. We test the sampling geometry and boundary conditions
    // here; full curve-accuracy testing lives in pa_test_easing
    // which exercises Curve::evaluate() directly without QEasingCurve.

    /// The default fallback curve (OutCubic) must be monotonically
    /// increasing and satisfy f(0)=0, f(1)=1. The BezierSpline
    /// approximation in `applyResolvedEasing` samples at 1/3 and 2/3
    /// within each of `PhosphorMotionAnimation::kBezierSplineSegments`
    /// segments — verify those sample points are monotonic and
    /// within [0,1].
    void testBezierSplineSamplingGeometry()
    {
        const auto curve = defaultFallbackCurve();

        // Boundary conditions.
        QCOMPARE(curve->evaluate(0.0), 0.0);
        QCOMPARE(curve->evaluate(1.0), 1.0);

        // Replicate applyResolvedEasing()'s sampling to verify
        // the control-point geometry is valid for a monotonic curve.
        // Uses the same public constant the production code uses so
        // a future retune of the segment count exercises this test
        // without a manual touch.
        constexpr int kSegments = PhosphorMotionAnimation::kBezierSplineSegments;
        qreal prevY = 0.0;

        for (int i = 0; i < kSegments; ++i) {
            const qreal t0 = static_cast<qreal>(i) / kSegments;
            const qreal t1 = static_cast<qreal>(i + 1) / kSegments;
            const qreal tMid1 = t0 + (t1 - t0) / 3.0;
            const qreal tMid2 = t0 + 2.0 * (t1 - t0) / 3.0;

            const qreal y1 = curve->evaluate(tMid1);
            const qreal y2 = curve->evaluate(tMid2);
            const qreal yEnd = curve->evaluate(t1);

            // All sampled Y values must be in [0, 1].
            QVERIFY2(y1 >= 0.0 && y1 <= 1.0,
                     qPrintable(QStringLiteral("c1 y=%1 out of range at segment %2").arg(y1, 0, 'f', 6).arg(i)));
            QVERIFY2(y2 >= 0.0 && y2 <= 1.0,
                     qPrintable(QStringLiteral("c2 y=%1 out of range at segment %2").arg(y2, 0, 'f', 6).arg(i)));
            QVERIFY2(yEnd >= 0.0 && yEnd <= 1.0,
                     qPrintable(QStringLiteral("end y=%1 out of range at segment %2").arg(yEnd, 0, 'f', 6).arg(i)));

            // OutCubic is monotonically increasing — each segment
            // end must be >= the previous segment end.
            QVERIFY2(yEnd >= prevY - 1e-12,
                     qPrintable(QStringLiteral("monotonicity violated: segment %1 end=%2 < prev=%3")
                                    .arg(i)
                                    .arg(yEnd, 0, 'f', 6)
                                    .arg(prevY, 0, 'f', 6)));
            prevY = yEnd;
        }

        // Final segment end must be 1.0.
        QCOMPARE(prevY, 1.0);
    }

    /// Regression guard for the Qt 6.11 `QEasingCurve::BezierSpline` /
    /// `QQuickPropertyAnimation::setEasing` heap-corruption bug (see
    /// commit `1f09962d`). The production code caps the sampling at
    /// `kBezierSplineSegments`; this test installs a spring profile
    /// (which takes the parametric-sampling branch, not the
    /// cubic-bezier fast path) and asserts the resulting easing's
    /// cubic-spline representation stays well below Qt's boundary.
    ///
    /// A regression here would re-open the heap-corruption window — any
    /// change that raises `kBezierSplineSegments` to 11 or more must
    /// also adjust Qt, not just this cap. The production `static_assert`
    /// in phosphormotionanimation.cpp is the belt; this test is the
    /// suspenders, covering the runtime install path the assert can't.
    void testParametricCurveStaysUnderQtSegmentBoundary()
    {
        // Build a spring profile. Spring curves dispatch to the
        // parametric-sampling branch (they are NOT the CubicBezier
        // fast-path shape).
        Profile p;
        p.curve = std::make_shared<Spring>(12.0, 0.6);
        p.duration = 250.0;
        PhosphorProfileRegistry::instance().registerProfile(QStringLiteral("test.spring"), p);

        PhosphorMotionAnimation a;
        a.setProfile(QStringLiteral("test.spring"));

        // The installed QEasingCurve's cubic-spline serialisation
        // encodes each segment as three QPointFs (c1, c2, end), so
        // `size() / 3` is the segment count. Must stay strictly under
        // Qt's 11-segment heap-corruption boundary.
        const QVector<QPointF> spline = a.easing().toCubicSpline();
        const int segmentCount = static_cast<int>(spline.size() / 3);
        QCOMPARE(segmentCount, PhosphorMotionAnimation::kBezierSplineSegments);
        QVERIFY2(segmentCount < 11,
                 qPrintable(QStringLiteral("installed easing has %1 segments — at or above Qt 6.11's "
                                           "setEasing heap-corruption boundary (11)")
                                .arg(segmentCount)));
    }

    /// Default-constructed animation installs the library-default
    /// curve (OutCubic). Verify the resolved profile reflects this.
    void testDefaultResolvedProfileHasNoCurve()
    {
        PhosphorMotionAnimation a;
        // The resolved profile is default-constructed — no explicit
        // curve, so applyResolvedEasing() uses defaultFallbackCurve().
        QVERIFY(!a.resolvedProfile().curve);
    }

    /// Setting a profile with a custom duration updates both the
    /// duration and the resolved profile accordingly.
    void testProfileCurveUpdatesResolvedProfile()
    {
        PhosphorMotionAnimation a;

        PhosphorProfile p;
        p.setDuration(500.0);
        a.setProfile(QVariant::fromValue(p));

        // Duration must have changed.
        QCOMPARE(a.duration(), 500);
        // Resolved profile must carry the new duration.
        QVERIFY(a.resolvedProfile().duration.has_value());
        QCOMPARE(qRound(a.resolvedProfile().duration.value()), 500);
    }

    // ─── Meta ───

    void testMetaObjectProperties()
    {
        const QMetaObject* mo = &PhosphorMotionAnimation::staticMetaObject;
        QVERIFY(mo->indexOfProperty("profile") >= 0);
    }
};

QTEST_MAIN(TestPhosphorMotionAnimation)
#include "test_phosphormotionanimation.moc"
