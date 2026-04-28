// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Spring.h>
#include <PhosphorAnimationQml/phosphoranimationqml_export.h>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

namespace PhosphorAnimation {

/**
 * @brief QML value-type wrapper around `PhosphorAnimation::Spring`.
 *
 * Q_GADGET per Phase 4 decision O — value semantics. Usable as a
 * property shape inside `PhosphorProfile { curve: PhosphorSpring { omega: 14; zeta: 0.6 } }`.
 *
 * Parameter bounds (omega ∈ [0.1, 200], zeta ∈ [0.0, 10.0]) are enforced
 * inside the C++ `Spring` constructor; writes through these setters
 * clamp identically. Preset factories (`snappy` / `smooth` / `bouncy`)
 * are re-exposed as Q_INVOKABLE static helpers so QML authors can
 * reference the same named tunings C++ consumers use.
 */
class PHOSPHORANIMATIONQML_EXPORT PhosphorSpring
{
    Q_GADGET
    QML_VALUE_TYPE(phosphorSpring)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(qreal omega READ omega WRITE setOmega)
    Q_PROPERTY(qreal zeta READ zeta WRITE setZeta)

public:
    PhosphorSpring() = default;
    /// Implicit-conversion ctor for core-library code.
    PhosphorSpring(const Spring& value)
        : m_value(value)
    {
    }
    PhosphorSpring(qreal omega, qreal zeta)
        : m_value(omega, zeta)
    {
    }

    /// Read-only access to the underlying value. The non-const overload
    /// was deliberately removed: a mutable handle from QML let scripts
    /// bypass the setter clamps in `setOmega` / `setZeta` by writing
    /// directly into the underlying `Spring` fields. Core-library
    /// mutators construct a fresh `Spring` and assign.
    const Spring& value() const
    {
        return m_value;
    }

    // ─── Property delegates ───

    qreal omega() const
    {
        return m_value.omega;
    }
    void setOmega(qreal v)
    {
        // Re-construct through the Spring ctor so bounds-clamping is
        // shared with the C++ path. Direct field writes bypass the
        // clamp which would let a settings UI sneak a pathological
        // value past the contract.
        m_value = Spring(v, m_value.zeta);
    }
    qreal zeta() const
    {
        return m_value.zeta;
    }
    void setZeta(qreal v)
    {
        m_value = Spring(m_value.omega, v);
    }

    // ─── Serialization ───

    /// Canonical wire format from `PhosphorAnimation::Spring::toString`.
    Q_INVOKABLE QString toString() const
    {
        return m_value.toString();
    }

    /// Parse `"spring:omega,zeta"` or `"omega,zeta"`. Invalid input
    /// yields a default-constructed `Spring` (omega=12, zeta=0.8).
    Q_INVOKABLE static PhosphorSpring fromString(const QString& str)
    {
        return PhosphorSpring(Spring::fromString(str));
    }

    // ─── Presets ───

    /// Responsive, slight overshoot. Good default for window snap.
    Q_INVOKABLE static PhosphorSpring snappy()
    {
        return PhosphorSpring(Spring::snappy());
    }
    /// Critically damped. No overshoot, firm approach.
    Q_INVOKABLE static PhosphorSpring smooth()
    {
        return PhosphorSpring(Spring::smooth());
    }
    /// Visible bounce. Good for attention-grabbing feedback.
    Q_INVOKABLE static PhosphorSpring bouncy()
    {
        return PhosphorSpring(Spring::bouncy());
    }

    // ─── Equality ───

    bool operator==(const PhosphorSpring& other) const
    {
        return m_value == other.m_value;
    }
    bool operator!=(const PhosphorSpring& other) const
    {
        return !(*this == other);
    }

private:
    Spring m_value;
};

} // namespace PhosphorAnimation

Q_DECLARE_METATYPE(PhosphorAnimation::PhosphorSpring)
