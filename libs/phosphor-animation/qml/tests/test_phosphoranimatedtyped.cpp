// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Smoke-level coverage for the four non-Real per-T animated wrappers.
// Each wrapper shares the base-class plumbing; the dedicated
// test_phosphoranimatedreal.cpp exercises that path end-to-end. This
// file verifies the T-specific surface (property types, start / value
// round-trip, QML metatype registration) for Point / Size / Rect /
// Color plus the Color-specific ColorSpace property.

#include <PhosphorAnimation/qml/PhosphorAnimatedColor.h>
#include <PhosphorAnimation/qml/PhosphorAnimatedPoint.h>
#include <PhosphorAnimation/qml/PhosphorAnimatedRect.h>
#include <PhosphorAnimation/qml/PhosphorAnimatedSize.h>
#include <PhosphorAnimation/qml/QtQuickClockManager.h>

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

private Q_SLOTS:
    void init()
    {
        QtQuickClockManager::instance().clearForTest();
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
