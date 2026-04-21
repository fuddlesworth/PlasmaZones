// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>
#include <PhosphorAnimation/qml/PhosphorEasing.h>
#include <PhosphorAnimation/qml/PhosphorSpring.h>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace PhosphorAnimation {

class CurveRegistry;

/**
 * @brief Opaque QML value-type wrapper around `shared_ptr<const Curve>`.
 *
 * Q_GADGET per Phase 4 decision O. Holds a polymorphic curve reference
 * without exposing the Curve hierarchy to QML — plugin authors who
 * only need to pass a curve through the system (curve → Profile →
 * PhosphorMotionAnimation) can use `PhosphorCurve` as an opaque token,
 * while authors that need specific parameters construct
 * `PhosphorEasing` / `PhosphorSpring` directly.
 *
 * Serialization round-trips through `CurveRegistry` — a `PhosphorCurve`
 * constructed from a string resolves whatever curve type the registry
 * knows about (including third-party curves registered via
 * `CurveRegistry::registerFactory`, which is how Phase 4's
 * `CurveLoader` extends the known set from user-authored JSON).
 *
 * ## Immutability
 *
 * The wrapped `shared_ptr<const Curve>` is immutable — consistent with
 * the Curve contract that subclasses must not be mutated after
 * construction. Mutating needs a rebuild (`PhosphorCurve::fromEasing(...)`,
 * `fromSpring(...)`, or `fromString(...)`) which replaces the stored
 * pointer.
 */
class PHOSPHORANIMATION_EXPORT PhosphorCurve
{
    Q_GADGET
    QML_VALUE_TYPE(phosphorCurve)
    // No QML_STRUCTURED_VALUE — PhosphorCurve is opaque; the caller
    // constructs it via factory functions (fromEasing/fromSpring/
    // fromString) rather than a struct literal.

    Q_PROPERTY(QString typeId READ typeId)

public:
    PhosphorCurve() = default;
    explicit PhosphorCurve(std::shared_ptr<const Curve> curve)
        : m_curve(std::move(curve))
    {
    }

    /// The wrapped pointer. May be null on a default-constructed handle.
    std::shared_ptr<const Curve> curve() const
    {
        return m_curve;
    }

    /// Stable curve type-id string ("cubic-bezier", "spring",
    /// "elastic-out", …). Empty when the handle is null.
    QString typeId() const
    {
        return m_curve ? m_curve->typeId() : QString();
    }

    /// Serialize to wire format. Empty when null.
    Q_INVOKABLE QString toString() const
    {
        return m_curve ? m_curve->toString() : QString();
    }

    /// Null-handle check for QML code: `if (!curve.isNull()) ...`.
    Q_INVOKABLE bool isNull() const
    {
        return !m_curve;
    }

    // ─── Registry wiring ───

    /// Set the process-wide default CurveRegistry used by `fromString`.
    /// Must be called by the composition root (daemon / editor) before
    /// any QML code invokes `fromString`. The pointer must outlive all
    /// QML usage (typically a member of the daemon). When null,
    /// `fromString` returns a null-handle PhosphorCurve.
    static void setDefaultRegistry(CurveRegistry* registry);

    // ─── Factory helpers ───

    /// Parse via `CurveRegistry` — handles every curve type the
    /// registry knows, including user-authored curves registered by
    /// `CurveLoader`. Returns a null-handle `PhosphorCurve` on parse
    /// failure (check via `isNull()`).
    Q_INVOKABLE static PhosphorCurve fromString(const QString& str);

    /// Wrap an `Easing` value as a polymorphic `PhosphorCurve`.
    /// Allocates a `shared_ptr<Easing>` copy of the value.
    Q_INVOKABLE static PhosphorCurve fromEasing(const PhosphorEasing& easing);

    /// Wrap a `Spring` value as a polymorphic `PhosphorCurve`.
    Q_INVOKABLE static PhosphorCurve fromSpring(const PhosphorSpring& spring);

    // ─── Equality ───

    /// Equality compares via `Curve::equals` so curves with
    /// floating-point parameters compare tightly rather than through
    /// the string form.
    bool operator==(const PhosphorCurve& other) const
    {
        if (m_curve == other.m_curve) {
            return true;
        }
        if (!m_curve || !other.m_curve) {
            return false;
        }
        return m_curve->equals(*other.m_curve);
    }
    bool operator!=(const PhosphorCurve& other) const
    {
        return !(*this == other);
    }

private:
    static CurveRegistry* s_registry;
    std::shared_ptr<const Curve> m_curve;
};

} // namespace PhosphorAnimation

Q_DECLARE_METATYPE(PhosphorAnimation::PhosphorCurve)
