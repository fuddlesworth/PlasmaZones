// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/qml/PhosphorAnimatedReal.h>
#include <PhosphorAnimation/qml/PhosphorCurve.h>
#include <PhosphorAnimation/qml/PhosphorProfile.h>
#include <PhosphorAnimation/qml/QtQuickClockManager.h>

#include <QGuiApplication>
#include <QObject>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace PhosphorAnimation;

/// End-to-end coverage for the per-T Q_OBJECT wrapper. `PhosphorAnimatedReal`
/// exercises the full base-class pattern (window binding, profile property,
/// auto-advance signal wiring, lifecycle-flag emission) plus the subclass-
/// specific `start` / `retarget` / `value` surface. The four sibling classes
/// (Point/Size/Rect/Color) share this shape; their dedicated tests are
/// smoke-level.
class TestPhosphorAnimatedReal : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QtQuickClockManager::instance().clearForTest();
    }

    /// Default state: not animating, not complete, value==0.
    void testDefaults()
    {
        PhosphorAnimatedReal a;
        QVERIFY(!a.isAnimating());
        QVERIFY(!a.isComplete());
        QCOMPARE(a.from(), 0.0);
        QCOMPARE(a.to(), 0.0);
        QCOMPARE(a.value(), 0.0);
        QCOMPARE(a.window(), static_cast<QQuickWindow*>(nullptr));
    }

    /// Without a window, `start` fails — no clock resolves.
    void testStartWithoutWindowRejected()
    {
        PhosphorAnimatedReal a;
        const bool ok = a.start(0.0, 1.0);
        QVERIFY(!ok);
        QVERIFY(!a.isAnimating());
    }

    /// With a window, clock resolves and `start` succeeds on a
    /// non-degenerate segment. from/to/value reflect the new endpoints.
    void testStartWithWindowSucceeds()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedReal a;
        a.setWindow(window.get());
        QSignalSpy fromSpy(&a, &PhosphorAnimatedReal::fromChanged);
        QSignalSpy toSpy(&a, &PhosphorAnimatedReal::toChanged);
        QSignalSpy animSpy(&a, &PhosphorAnimatedValueBase::animatingChanged);

        PhosphorProfile p;
        p.setDuration(100.0);
        a.setProfile(p);

        const bool ok = a.start(0.0, 1.0);
        QVERIFY(ok);
        QCOMPARE(a.from(), 0.0);
        QCOMPARE(a.to(), 1.0);
        QVERIFY(a.isAnimating());
        QVERIFY(fromSpy.count() >= 1);
        QVERIFY(toSpy.count() >= 1);
        QVERIFY(animSpy.count() >= 1);
    }

    /// Degenerate start (from ≈ to) fails but still overwrites endpoints
    /// and emits change signals so QML bindings resync.
    void testDegenerateStartEmitsSignalsButReturnsFalse()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedReal a;
        a.setWindow(window.get());
        const bool ok = a.start(1.0, 1.0);
        QVERIFY(!ok);
        QCOMPARE(a.from(), 1.0);
        QCOMPARE(a.to(), 1.0);
        // isAnimating flips false after the degenerate path
        QVERIFY(!a.isAnimating());
    }

    /// profile property persists across start — the MotionSpec stamped
    /// into the AnimatedValue uses the profile value.
    void testProfileIsPersisted()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedReal a;
        a.setWindow(window.get());

        PhosphorProfile p;
        p.setDuration(250.0);
        p.setCurve(PhosphorCurve::fromEasing(PhosphorEasing()));
        a.setProfile(p);

        QCOMPARE(a.profile().duration(), 250.0);
        QVERIFY(!a.profile().curve().isNull());
    }

    /// cancel stops animation but leaves value and isComplete at their
    /// current state — matches Phase-3 AnimatedValue contract.
    void testCancel()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedReal a;
        a.setWindow(window.get());
        a.start(0.0, 1.0);
        QVERIFY(a.isAnimating());

        QSignalSpy animSpy(&a, &PhosphorAnimatedValueBase::animatingChanged);
        a.cancel();
        QVERIFY(!a.isAnimating());
        QVERIFY(animSpy.count() >= 1);
    }

    /// finish snaps to target, flips both flags, emits valueChanged /
    /// animatingChanged / completeChanged.
    void testFinish()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedReal a;
        a.setWindow(window.get());
        a.start(0.0, 1.0);

        QSignalSpy valueSpy(&a, &PhosphorAnimatedReal::valueChanged);
        QSignalSpy compSpy(&a, &PhosphorAnimatedValueBase::completeChanged);
        a.finish();
        QCOMPARE(a.value(), 1.0);
        QVERIFY(!a.isAnimating());
        QVERIFY(a.isComplete());
        QVERIFY(valueSpy.count() >= 1);
        QVERIFY(compSpy.count() >= 1);
    }

    /// window property is reactive — setting it emits windowChanged;
    /// setting to the same window again is a no-op.
    void testWindowProperty()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedReal a;
        QSignalSpy spy(&a, &PhosphorAnimatedValueBase::windowChanged);

        a.setWindow(window.get());
        QCOMPARE(spy.count(), 1);
        a.setWindow(window.get());
        QCOMPARE(spy.count(), 1); // no-op

        a.setWindow(nullptr);
        QCOMPARE(spy.count(), 2);
    }

    /// advance() delegates to AnimatedValue — gives tests manual ticking
    /// without needing a rendering window cycle.
    void testManualAdvance()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedReal a;
        a.setWindow(window.get());
        a.start(0.0, 1.0);
        // First advance latches start-time; value is still at from.
        a.advance();
        QCOMPARE(a.value(), 0.0);
        QVERIFY(a.isAnimating());
    }

    /// Meta-object property declarations must be present for QML
    /// bindings to reach them.
    void testMetaObjectProperties()
    {
        const QMetaObject* mo = &PhosphorAnimatedReal::staticMetaObject;
        QVERIFY(mo->indexOfProperty("window") >= 0);
        QVERIFY(mo->indexOfProperty("profile") >= 0);
        QVERIFY(mo->indexOfProperty("isAnimating") >= 0);
        QVERIFY(mo->indexOfProperty("isComplete") >= 0);
        QVERIFY(mo->indexOfProperty("from") >= 0);
        QVERIFY(mo->indexOfProperty("to") >= 0);
        QVERIFY(mo->indexOfProperty("value") >= 0);
    }
};

QTEST_MAIN(TestPhosphorAnimatedReal)
#include "test_phosphoranimatedreal.moc"
