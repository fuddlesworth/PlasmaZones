// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtMath>
#include <chrono>
#include <QPointF>
#include <QSizeF>
#include <QRect>

namespace PlasmaZones {

/**
 * @brief Easing curve supporting cubic bezier, elastic, and bounce types
 *
 * Bezier: P0=(0,0) and P3=(1,1) are implicit, defined by P1=(x1,y1) and P2=(x2,y2).
 * Stored in config as "x1,y1,x2,y2" (bezier) or "elastic-out:1.0,0.3" (named).
 * Detection: if string contains a letter -> named curve; otherwise -> bezier.
 */
struct EasingCurve
{
    enum class Type {
        CubicBezier,
        ElasticIn,
        ElasticOut,
        ElasticInOut,
        BounceIn,
        BounceOut,
        BounceInOut
    };

    Type type = Type::CubicBezier;

    // Bezier control points (used when type == CubicBezier)
    qreal x1 = 0.33;
    qreal y1 = 1.0;
    qreal x2 = 0.68;
    qreal y2 = 1.0;

    // Parameters for elastic and bounce curves
    qreal amplitude = 1.0; // Elastic: overshoot intensity; Bounce: bounce height scale
    qreal period = 0.3; // Elastic only: oscillation period
    int bounces = 3; // Bounce only: number of bounces (1–8)

    /// Evaluate the easing curve at time x (0.0-1.0), returns eased value
    qreal evaluate(qreal x) const;

    /// Parse from config string; returns default OutCubic bezier on failure
    static EasingCurve fromString(const QString& str);

    /// Serialize to config string
    QString toString() const;

    bool operator==(const EasingCurve& other) const
    {
        if (type != other.type)
            return false;
        if (type == Type::CubicBezier) {
            return qFuzzyCompare(1.0 + x1, 1.0 + other.x1) && qFuzzyCompare(1.0 + y1, 1.0 + other.y1)
                && qFuzzyCompare(1.0 + x2, 1.0 + other.x2) && qFuzzyCompare(1.0 + y2, 1.0 + other.y2);
        }
        // Elastic types: compare amplitude and period
        if (type == Type::ElasticIn || type == Type::ElasticOut || type == Type::ElasticInOut) {
            return qFuzzyCompare(1.0 + amplitude, 1.0 + other.amplitude)
                && qFuzzyCompare(1.0 + period, 1.0 + other.period);
        }
        // Bounce types (BounceIn, BounceOut, BounceInOut): compare amplitude and bounce count
        return qFuzzyCompare(1.0 + amplitude, 1.0 + other.amplitude) && bounces == other.bounces;
    }
    bool operator!=(const EasingCurve& other) const
    {
        return !(*this == other);
    }

private:
    static qreal evaluateElasticOut(qreal t, qreal amp, qreal per);
    static qreal evaluateBounceOut(qreal t, qreal amp, int n);
};

/**
 * @brief Animation data for window snap transitions (translate + scale)
 *
 * Stores the start position/size and target geometry for smooth animations.
 * Timing uses std::chrono::milliseconds for frame-perfect animation.
 * Progress is cached once per frame to avoid inconsistencies between
 * position and size interpolation within a single paint cycle.
 */
struct WindowAnimation
{
    QPointF startPosition; ///< Visual top-left position before snap
    QSizeF startSize; ///< Visual size before snap (for scale interpolation)
    QRect targetGeometry; ///< Target geometry (for duplicate detection)
    std::chrono::milliseconds startTime{-1}; ///< presentTime when animation started (-1 = pending)
    qreal duration = 150.0; ///< Animation duration in milliseconds
    EasingCurve easing; ///< Easing curve for this animation
    qreal cachedProgress = 0.0; ///< Eased progress, updated once per frame

    /// Check if the animation has been initialized
    bool isValid() const
    {
        return startTime.count() >= 0;
    }

    /// Update cached progress from presentTime. Called once per frame.
    void updateProgress(std::chrono::milliseconds presentTime)
    {
        if (startTime.count() < 0) {
            // First frame — latch the start time to this presentTime
            startTime = presentTime;
            cachedProgress = 0.0;
            return;
        }
        if (duration <= 0.0) {
            cachedProgress = 1.0;
            return;
        }
        const qreal elapsed = qreal((presentTime - startTime).count());
        const qreal t = qMin(1.0, elapsed / duration);
        cachedProgress = easing.evaluate(t);
    }

    /// Eased progress. Usually in [0.0, 1.0], but elastic and bounce curves
    /// may overshoot (e.g. 1.05 or -0.02) by design.
    qreal progress() const
    {
        return cachedProgress;
    }

    /// Check if animation is complete based on presentTime
    bool isComplete(std::chrono::milliseconds presentTime) const
    {
        if (startTime.count() < 0 || duration <= 0.0) {
            return true;
        }
        return qreal((presentTime - startTime).count()) >= duration;
    }

    /// Absolute visual top-left position at the cached progress.
    QPointF currentVisualPosition() const
    {
        const qreal p = cachedProgress;
        const qreal tx = targetGeometry.x();
        const qreal ty = targetGeometry.y();
        return QPointF(startPosition.x() + (tx - startPosition.x()) * p,
                       startPosition.y() + (ty - startPosition.y()) * p);
    }

    /// Interpolated visual size at the cached progress.
    QSizeF currentVisualSize() const
    {
        const qreal p = cachedProgress;
        const qreal tw = targetGeometry.width();
        const qreal th = targetGeometry.height();
        return QSizeF(startSize.width() + (tw - startSize.width()) * p,
                      startSize.height() + (th - startSize.height()) * p);
    }

    /// True if the animation involves a size change (not just position).
    bool hasScaleChange() const
    {
        return qAbs(startSize.width() - targetGeometry.width()) > 1.0
            || qAbs(startSize.height() - targetGeometry.height()) > 1.0;
    }
};

/**
 * @brief Configuration for snap animations (compositor-agnostic)
 *
 * Extracted from WindowAnimator so any compositor plugin can use
 * the same configuration structure.
 */
struct AnimationConfig
{
    bool enabled = true;
    qreal duration = 150.0;
    EasingCurve easing;
    int minDistance = 0;
    int sequenceMode = 0; ///< 0=all at once, 1=one by one in zone order
    int staggerInterval = 30; ///< ms between each window start when cascading
};

} // namespace PlasmaZones
