// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

namespace PhosphorAnimation {

/**
 * @brief QML value-type wrapper around `PhosphorAnimation::Easing`.
 *
 * Q_GADGET per Phase 4 decision O — value semantics (stack-allocated,
 * copy-constructible, snapshot at bind time). Used as a property shape
 * inside `PhosphorProfile { curve: PhosphorEasing { type: ... } }` and
 * as a structured literal for `PhosphorMotionAnimation.profile`.
 *
 * Every parameter is mirrored to a Q_PROPERTY delegating to the
 * underlying `Easing` value. The enum is re-exported as `Type` so QML
 * can reference `PhosphorEasing.CubicBezier` etc. without knowing the
 * underlying nested enum.
 *
 * ## Serialization
 *
 * `fromString` / `toString` round-trip through the C++ `Easing` wire
 * format — no QML-specific form. A config that has a curve string can
 * be reconstituted with `PhosphorEasing.fromString(str)` in QML.
 */
class PHOSPHORANIMATION_EXPORT PhosphorEasing
{
    Q_GADGET
    QML_VALUE_TYPE(phosphorEasing)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(Type type READ type WRITE setType)
    Q_PROPERTY(qreal x1 READ x1 WRITE setX1)
    Q_PROPERTY(qreal y1 READ y1 WRITE setY1)
    Q_PROPERTY(qreal x2 READ x2 WRITE setX2)
    Q_PROPERTY(qreal y2 READ y2 WRITE setY2)
    Q_PROPERTY(qreal amplitude READ amplitude WRITE setAmplitude)
    Q_PROPERTY(qreal period READ period WRITE setPeriod)
    Q_PROPERTY(int bounces READ bounces WRITE setBounces)

public:
    // Parallel to `PhosphorAnimation::Easing::Type` so QML enum references
    // work without the caller having to know about the nested C++ enum.
    // Integer values kept identical for `static_cast` round-trip.
    enum class Type : int {
        CubicBezier = int(Easing::Type::CubicBezier),
        ElasticIn = int(Easing::Type::ElasticIn),
        ElasticOut = int(Easing::Type::ElasticOut),
        ElasticInOut = int(Easing::Type::ElasticInOut),
        BounceIn = int(Easing::Type::BounceIn),
        BounceOut = int(Easing::Type::BounceOut),
        BounceInOut = int(Easing::Type::BounceInOut),
    };
    Q_ENUM(Type)

    PhosphorEasing() = default;
    /// Implicit-conversion ctor so core-library code can hand a bare
    /// `Easing` value into the QML boundary without a wrapping call.
    PhosphorEasing(const Easing& value)
        : m_value(value)
    {
    }

    /// Access the underlying value-type for core-library consumers.
    const Easing& value() const
    {
        return m_value;
    }
    Easing& value()
    {
        return m_value;
    }

    // ─── Property delegates ───

    Type type() const
    {
        return static_cast<Type>(static_cast<int>(m_value.type));
    }
    void setType(Type t)
    {
        m_value.type = static_cast<Easing::Type>(static_cast<int>(t));
    }

    qreal x1() const
    {
        return m_value.x1;
    }
    void setX1(qreal v)
    {
        m_value.x1 = v;
    }
    qreal y1() const
    {
        return m_value.y1;
    }
    void setY1(qreal v)
    {
        m_value.y1 = v;
    }
    qreal x2() const
    {
        return m_value.x2;
    }
    void setX2(qreal v)
    {
        m_value.x2 = v;
    }
    qreal y2() const
    {
        return m_value.y2;
    }
    void setY2(qreal v)
    {
        m_value.y2 = v;
    }

    qreal amplitude() const
    {
        return m_value.amplitude;
    }
    void setAmplitude(qreal v)
    {
        m_value.amplitude = v;
    }
    qreal period() const
    {
        return m_value.period;
    }
    void setPeriod(qreal v)
    {
        m_value.period = v;
    }
    int bounces() const
    {
        return m_value.bounces;
    }
    void setBounces(int v)
    {
        m_value.bounces = v;
    }

    // ─── Serialization ───

    /// Canonical wire format from `PhosphorAnimation::Easing::toString`.
    Q_INVOKABLE QString toString() const
    {
        return m_value.toString();
    }

    /// Parse the same wire format. Invalid input yields a default-
    /// constructed (OutCubic bezier) `PhosphorEasing`, matching the
    /// core library's fault-tolerant parse contract.
    Q_INVOKABLE static PhosphorEasing fromString(const QString& str)
    {
        return PhosphorEasing(Easing::fromString(str));
    }

    // ─── Equality ───

    bool operator==(const PhosphorEasing& other) const
    {
        return m_value == other.m_value;
    }
    bool operator!=(const PhosphorEasing& other) const
    {
        return !(*this == other);
    }

private:
    Easing m_value;
};

} // namespace PhosphorAnimation

Q_DECLARE_METATYPE(PhosphorAnimation::PhosphorEasing)
