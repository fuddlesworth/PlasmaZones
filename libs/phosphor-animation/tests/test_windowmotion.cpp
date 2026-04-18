// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>
#include <PhosphorAnimation/WindowMotion.h>

#include <QTest>

#include <memory>

using namespace std::chrono_literals;

using PhosphorAnimation::Easing;
using PhosphorAnimation::Spring;
using PhosphorAnimation::WindowMotion;

class TestWindowMotion : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDefaultConstructionPending()
    {
        WindowMotion m;
        QVERIFY(!m.isValid()); // startTime < 0 until first updateProgress
        QCOMPARE(m.progress(), 0.0);
    }

    void testFirstUpdateLatchesStartTime()
    {
        WindowMotion m;
        m.duration = 100.0;
        m.updateProgress(std::chrono::milliseconds(500));
        QVERIFY(m.isValid());
        QCOMPARE(m.progress(), 0.0); // first frame is always t=0
    }

    void testProgressMonotonic()
    {
        WindowMotion m;
        m.duration = 100.0;
        m.startPosition = QPointF(0, 0);
        m.startSize = QSizeF(100, 100);
        m.targetGeometry = QRect(200, 0, 100, 100);

        m.updateProgress(std::chrono::milliseconds(0));
        const qreal p0 = m.progress();

        m.updateProgress(std::chrono::milliseconds(50));
        const qreal pHalf = m.progress();

        m.updateProgress(std::chrono::milliseconds(100));
        const qreal pEnd = m.progress();

        QVERIFY(p0 <= pHalf);
        QVERIFY(pHalf <= pEnd);
        QVERIFY(pEnd >= 0.99);
    }

    void testIsCompleteAfterDuration()
    {
        WindowMotion m;
        m.duration = 100.0;
        m.updateProgress(std::chrono::milliseconds(0));
        QVERIFY(!m.isComplete(std::chrono::milliseconds(50)));
        QVERIFY(m.isComplete(std::chrono::milliseconds(100)));
        QVERIFY(m.isComplete(std::chrono::milliseconds(500)));
    }

    void testZeroDurationCompletesImmediately()
    {
        WindowMotion m;
        m.duration = 0.0;
        m.updateProgress(std::chrono::milliseconds(0));
        QCOMPARE(m.progress(), 1.0);
        QVERIFY(m.isComplete(std::chrono::milliseconds(0)));
    }

    void testVisualPositionInterpolates()
    {
        WindowMotion m;
        m.duration = 100.0;
        m.startPosition = QPointF(100, 200);
        m.startSize = QSizeF(50, 50);
        m.targetGeometry = QRect(500, 400, 50, 50);

        m.updateProgress(std::chrono::milliseconds(0));
        QCOMPARE(m.currentVisualPosition(), QPointF(100, 200));

        // Force cached progress to exactly 0.5 (linear-like) — use bezier.
        m.cachedProgress = 0.5;
        const QPointF mid = m.currentVisualPosition();
        QVERIFY(qAbs(mid.x() - 300.0) < 0.01);
        QVERIFY(qAbs(mid.y() - 300.0) < 0.01);

        m.cachedProgress = 1.0;
        QCOMPARE(m.currentVisualPosition(), QPointF(500, 400));
    }

    void testVisualSizeInterpolates()
    {
        WindowMotion m;
        m.startSize = QSizeF(100, 100);
        m.targetGeometry = QRect(0, 0, 200, 300);

        m.cachedProgress = 0.5;
        const QSizeF mid = m.currentVisualSize();
        QVERIFY(qAbs(mid.width() - 150.0) < 0.01);
        QVERIFY(qAbs(mid.height() - 200.0) < 0.01);
    }

    void testHasScaleChange()
    {
        WindowMotion m;
        m.startSize = QSizeF(100, 100);
        m.targetGeometry = QRect(0, 0, 100, 100);
        QVERIFY(!m.hasScaleChange());

        m.targetGeometry = QRect(0, 0, 200, 100);
        QVERIFY(m.hasScaleChange());
    }

    void testNullCurveProgressesLinearly()
    {
        // A null curve is a valid state — the progress field falls back
        // to the raw normalized t. Useful for default-constructed
        // motion records and as a sentinel.
        WindowMotion m;
        m.duration = 100.0;
        m.curve = nullptr;
        m.updateProgress(std::chrono::milliseconds(0));
        m.updateProgress(std::chrono::milliseconds(50));
        QVERIFY(qAbs(m.progress() - 0.5) < 1e-6);
    }

    void testPolymorphicCurveDispatchesViaSharedPtr()
    {
        // Spring + Easing both drive WindowMotion through the same
        // shared_ptr<const Curve> field — that's the whole point of
        // the Phase-2 polymorphic upgrade.
        WindowMotion easingMotion;
        easingMotion.duration = 100.0;
        easingMotion.curve = std::make_shared<Easing>();

        WindowMotion springMotion;
        springMotion.duration = 100.0;
        springMotion.curve = std::make_shared<Spring>(Spring::snappy());

        easingMotion.updateProgress(std::chrono::milliseconds(0));
        springMotion.updateProgress(std::chrono::milliseconds(0));
        easingMotion.updateProgress(std::chrono::milliseconds(50));
        springMotion.updateProgress(std::chrono::milliseconds(50));

        // Both produce a numeric progress in roughly the [0, 1+] band.
        QVERIFY(easingMotion.progress() >= 0.0 && easingMotion.progress() <= 1.5);
        QVERIFY(springMotion.progress() >= 0.0 && springMotion.progress() <= 1.5);
    }
};

QTEST_MAIN(TestWindowMotion)
#include "test_windowmotion.moc"
