// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationMath.h>

#include <QTest>

using PhosphorAnimation::AnimationConfig;
using PhosphorAnimation::Easing;

class TestAnimationMath : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── createSnapMotion / createSnapAnimation alias ───

    void testCreateSnapMotionDisabled()
    {
        AnimationConfig config;
        config.enabled = false;
        auto result = PhosphorAnimation::AnimationMath::createSnapMotion(QPointF(0, 0), QSizeF(100, 100),
                                                                         QRect(200, 200, 400, 300), config);
        QVERIFY(!result.has_value());
    }

    void testCreateSnapMotionDegenerateTarget()
    {
        AnimationConfig config;
        auto result = PhosphorAnimation::AnimationMath::createSnapMotion(QPointF(0, 0), QSizeF(100, 100),
                                                                         QRect(200, 200, 0, 300), config);
        QVERIFY(!result.has_value());
    }

    void testCreateSnapMotionBelowThreshold()
    {
        AnimationConfig config;
        config.minDistance = 50;
        auto result = PhosphorAnimation::AnimationMath::createSnapMotion(QPointF(100, 100), QSizeF(400, 300),
                                                                         QRect(101, 101, 400, 300), config);
        QVERIFY(!result.has_value());
    }

    void testCreateSnapMotionValid()
    {
        AnimationConfig config;
        config.duration = 200.0;
        config.minDistance = 5;
        auto result = PhosphorAnimation::AnimationMath::createSnapMotion(QPointF(0, 0), QSizeF(100, 100),
                                                                         QRect(300, 300, 500, 400), config);
        QVERIFY(result.has_value());
        QCOMPARE(result->targetGeometry, QRect(300, 300, 500, 400));
        QCOMPARE(result->duration, 200.0);
    }

    // ─── repaintBounds ───

    void testRepaintBoundsContainsStartAndTargetWithPadding()
    {
        Easing bezier; // default cubic
        const QPointF startPos(100, 100);
        const QSizeF startSize(200, 150);
        const QRect target(400, 400, 300, 250);
        const QMarginsF padding(10.0, 10.0, 10.0, 10.0);

        const QRectF bounds =
            PhosphorAnimation::AnimationMath::repaintBounds(startPos, startSize, target, bezier, padding);

        const QRectF startWithPadding(startPos.x() - 10.0, startPos.y() - 10.0, startSize.width() + 20.0,
                                      startSize.height() + 20.0);
        const QRectF targetWithPadding(target.x() - 10.0, target.y() - 10.0, target.width() + 20.0,
                                       target.height() + 20.0);

        QVERIFY(bounds.contains(startWithPadding));
        QVERIFY(bounds.contains(targetWithPadding));
    }

    void testRepaintBoundsSamplesElasticOvershoot()
    {
        // Elastic-out can push visually past the target during oscillation.
        // repaintBounds must sample the curve and extend the union beyond
        // the start/target rect — otherwise paint gets clipped.
        Easing elastic;
        elastic.type = Easing::Type::ElasticOut;
        elastic.amplitude = 1.5;
        elastic.period = 0.3;

        const QPointF startPos(0, 0);
        const QSizeF startSize(100, 100);
        const QRect target(200, 0, 100, 100);
        const QMarginsF padding(0, 0, 0, 0);

        const QRectF bounds =
            PhosphorAnimation::AnimationMath::repaintBounds(startPos, startSize, target, elastic, padding);
        const QRectF naive = QRectF(0, 0, 300, 100).adjusted(-2, -2, 2, 2);

        // Elastic overshoot should push the bounds beyond the naive union.
        // Accept either x < naive.x (backward overshoot) OR right > naive.right
        // since elastic can go either way depending on amplitude.
        QVERIFY(bounds.left() <= naive.left() || bounds.right() >= naive.right());
    }
};

QTEST_MAIN(TestAnimationMath)
#include "test_animationmath.moc"
