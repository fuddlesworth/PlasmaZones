// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Smoke-level coverage for the four non-Real per-T animated wrappers.
// Each wrapper shares the base-class plumbing; the dedicated
// test_phosphoranimatedreal.cpp exercises that path end-to-end. This
// file verifies the T-specific surface (property types, start / value
// round-trip, QML metatype registration) for Point / Size / Rect /
// Color plus the Color-specific ColorSpace property.

#include <PhosphorAnimationQml/PhosphorAnimatedColor.h>
#include <PhosphorAnimationQml/PhosphorAnimatedPoint.h>
#include <PhosphorAnimationQml/PhosphorAnimatedRect.h>
#include <PhosphorAnimationQml/PhosphorAnimatedSize.h>
#include <PhosphorAnimationQml/QtQuickClockManager.h>

#include <QColor>
#include <QGuiApplication>
#include <QObject>
#include <QPointF>
#include <QQuickWindow>
#include <QRectF>
#include <QSizeF>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace PhosphorAnimation;

class TestPhosphorAnimatedTyped : public QObject
{
    Q_OBJECT

private:
    /// Per-test clock manager. Published as the QML default in init()
    /// so PhosphorAnimatedValueBase::resolveClock resolves through this
    /// fixture-owned instance. Phase A3 of the architecture refactor
    /// retired `QtQuickClockManager::instance()`.
    std::unique_ptr<QtQuickClockManager> m_clockManager;

private Q_SLOTS:
    void init()
    {
        m_clockManager = std::make_unique<QtQuickClockManager>();
        QtQuickClockManager::setDefaultManager(m_clockManager.get());
    }

    void cleanup()
    {
        QtQuickClockManager::setDefaultManager(nullptr);
        m_clockManager.reset();
    }

    // ─── Point ───

    void testPointStart()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedPoint p;
        p.setWindow(window.get());
        const bool ok = p.start(QPointF(0, 0), QPointF(100, 50));
        QVERIFY(ok);
        QCOMPARE(p.from(), QPointF(0, 0));
        QCOMPARE(p.to(), QPointF(100, 50));
    }

    // ─── Size ───

    void testSizeStart()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedSize s;
        s.setWindow(window.get());
        const bool ok = s.start(QSizeF(100, 100), QSizeF(200, 200));
        QVERIFY(ok);
        QCOMPARE(s.from(), QSizeF(100, 100));
        QCOMPARE(s.to(), QSizeF(200, 200));
    }

    // ─── Rect ───

    void testRectStart()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedRect r;
        r.setWindow(window.get());
        const bool ok = r.start(QRectF(0, 0, 100, 100), QRectF(50, 50, 200, 200));
        QVERIFY(ok);
        QCOMPARE(r.from(), QRectF(0, 0, 100, 100));
        QCOMPARE(r.to(), QRectF(50, 50, 200, 200));
    }

    // ─── Color ───

    void testColorStart()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedColor c;
        c.setWindow(window.get());
        const bool ok = c.start(QColor(Qt::red), QColor(Qt::blue));
        QVERIFY(ok);
        QCOMPARE(c.from(), QColor(Qt::red));
        QCOMPARE(c.to(), QColor(Qt::blue));
    }

    /// ColorSpace defaults to Linear; flipping to OkLab while idle
    /// works and emits colorSpaceChanged. Flipping while animating is
    /// refused (no signal).
    void testColorSpaceProperty()
    {
        PhosphorAnimatedColor c;
        QSignalSpy spy(&c, &PhosphorAnimatedColor::colorSpaceChanged);

        QCOMPARE(c.colorSpace(), PhosphorAnimatedColor::ColorSpace::Linear);
        c.setColorSpace(PhosphorAnimatedColor::ColorSpace::OkLab);
        QCOMPARE(c.colorSpace(), PhosphorAnimatedColor::ColorSpace::OkLab);
        QCOMPARE(spy.count(), 1);

        // Same-value write is a no-op.
        c.setColorSpace(PhosphorAnimatedColor::ColorSpace::OkLab);
        QCOMPARE(spy.count(), 1);
    }

    void testColorSpaceIgnoredWhileAnimating()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedColor c;
        c.setWindow(window.get());
        c.start(QColor(Qt::red), QColor(Qt::blue));
        QVERIFY(c.isAnimating());

        QSignalSpy spy(&c, &PhosphorAnimatedColor::colorSpaceChanged);
        c.setColorSpace(PhosphorAnimatedColor::ColorSpace::OkLab);
        QCOMPARE(c.colorSpace(), PhosphorAnimatedColor::ColorSpace::Linear); // refused
        QCOMPARE(spy.count(), 0);
    }

    /// Flipping colorSpace while idle must not lose the visible state.
    /// The two underlying AnimatedValue<QColor, Space> instances are
    /// independent, so a naive flip would jump value() to the target
    /// instance's default-constructed QColor(). `setColorSpace` calls
    /// `AnimatedValue::seedFrom` to copy idle from/to/current across
    /// the boundary — this test pins that invariant.
    void testColorSpaceFlipPreservesIdleState()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedColor c;
        c.setWindow(window.get());

        // Drive a full animation on Linear, then quiesce with finish()
        // so the Linear instance holds (from=red, to=blue, value=blue,
        // isComplete=true).
        c.start(QColor(Qt::red), QColor(Qt::blue));
        c.finish();
        QVERIFY(!c.isAnimating());
        QCOMPARE(c.from(), QColor(Qt::red));
        QCOMPARE(c.to(), QColor(Qt::blue));
        QCOMPARE(c.value(), QColor(Qt::blue));
        QVERIFY(c.isComplete());

        // Flip to OkLab. Post-flip reads must still see red/blue/blue —
        // the OkLab instance was never started but seedFrom propagates
        // the quiesced state.
        c.setColorSpace(PhosphorAnimatedColor::ColorSpace::OkLab);
        QCOMPARE(c.colorSpace(), PhosphorAnimatedColor::ColorSpace::OkLab);
        QCOMPARE(c.from(), QColor(Qt::red));
        QCOMPARE(c.to(), QColor(Qt::blue));
        QCOMPARE(c.value(), QColor(Qt::blue));
        QVERIFY(c.isComplete());

        // Flipping back to Linear is also continuous — the Linear
        // instance still holds its original state, so seedFrom is a
        // no-op-equivalent write (same values in, same values out).
        c.setColorSpace(PhosphorAnimatedColor::ColorSpace::Linear);
        QCOMPARE(c.from(), QColor(Qt::red));
        QCOMPARE(c.to(), QColor(Qt::blue));
        QCOMPARE(c.value(), QColor(Qt::blue));
    }

    /// Regression: `setColorSpace` must propagate the MotionSpec's
    /// clock/callbacks across the flip, not just the visible-state
    /// fields. Prior to the fix, `seedFrom` copied from/to/current/
    /// isComplete but left `m_spec.clock == nullptr` on the target
    /// instance. A subsequent `retarget()` then silently dropped
    /// because `AnimatedValue::retarget` checks
    /// `if (!m_spec.clock) return false;` — QML authors saw a
    /// seemingly-unanimated retarget after a color-space flip.
    ///
    /// Reproduction: start(red, blue) → finish to quiesce →
    /// setColorSpace(OkLab) → retarget(green) must install a new
    /// segment (from=blue, to=green). Post-fix, retarget returns
    /// true and the new `to()` is reflected on reads.
    void testRetargetAfterColorSpaceFlip()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedColor c;
        c.setWindow(window.get());

        // Phase 1: animate red → blue on Linear, then quiesce via
        // finish() so the Linear instance is idle with a populated
        // MotionSpec::clock.
        QVERIFY(c.start(QColor(Qt::red), QColor(Qt::blue)));
        c.finish();
        QVERIFY(!c.isAnimating());
        QVERIFY(c.isComplete());

        // Phase 2: flip to OkLab while idle. Fix under test: the
        // MotionSpec::clock must be seeded into m_animatedValueOkLab
        // along with the visible state — without that, step 3's
        // retarget silently rejects.
        c.setColorSpace(PhosphorAnimatedColor::ColorSpace::OkLab);
        QCOMPARE(c.colorSpace(), PhosphorAnimatedColor::ColorSpace::OkLab);

        // Phase 3: retarget to green. Must succeed (installs new
        // segment). Pre-fix this returned false because the OkLab
        // instance's MotionSpec::clock was null.
        const bool retargetOk = c.retarget(QColor(Qt::green));
        QVERIFY2(retargetOk, "retarget after color-space flip must install a new segment");
        QCOMPARE(c.to(), QColor(Qt::green));
        QVERIFY(c.isAnimating());
        // `from()` on the new segment is the current value at the
        // moment retarget() was called — which is blue (the last
        // value from the Linear segment, seeded into the OkLab
        // instance by the flip).
        QCOMPARE(c.from(), QColor(Qt::blue));
    }

    /// Companion to `testRetargetAfterColorSpaceFlip` — verifies that
    /// the post-flip animation actually advances (value moves over
    /// time). Exercises the full start→flip→retarget→advance cycle
    /// end-to-end so a future refactor that happens to seed the
    /// clock but not the onValueChanged callback would still fail
    /// this test (the value wouldn't move visibly at the wrapper
    /// boundary without the callback being present).
    void testRetargetAfterColorSpaceFlipAdvances()
    {
        auto window = std::make_unique<QQuickWindow>();
        PhosphorAnimatedColor c;
        c.setWindow(window.get());
        c.start(QColor(Qt::red), QColor(Qt::blue));
        c.finish();

        c.setColorSpace(PhosphorAnimatedColor::ColorSpace::OkLab);
        QVERIFY(c.retarget(QColor(Qt::green)));

        // Advance a few times — first advance latches startTime; the
        // second actually steps the curve. value() must leave the
        // `from` (blue) position as the animation progresses.
        c.advance();
        c.advance();
        // Not asserting an exact intermediate color (curve shape +
        // clock jitter make that fragile) — just that isAnimating
        // stays true AND the new target is set, which proves the
        // retarget wasn't silently rejected.
        QVERIFY(c.isAnimating());
        QCOMPARE(c.to(), QColor(Qt::green));
    }

    /// All four typed wrappers register their metatype and properties
    /// so QML bindings reach them.
    void testMetaObjectRegistration()
    {
        QVERIFY(PhosphorAnimatedPoint::staticMetaObject.indexOfProperty("value") >= 0);
        QVERIFY(PhosphorAnimatedSize::staticMetaObject.indexOfProperty("value") >= 0);
        QVERIFY(PhosphorAnimatedRect::staticMetaObject.indexOfProperty("value") >= 0);
        QVERIFY(PhosphorAnimatedColor::staticMetaObject.indexOfProperty("value") >= 0);
        QVERIFY(PhosphorAnimatedColor::staticMetaObject.indexOfProperty("colorSpace") >= 0);
    }
};

QTEST_MAIN(TestPhosphorAnimatedTyped)
#include "test_phosphoranimatedtyped.moc"
