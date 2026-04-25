// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>
#include <PhosphorAnimation/qml/PhosphorCurve.h>

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QtMath>
#include <QtQml/qqmlregistration.h>

#include <cmath>

namespace PhosphorAnimation {

/**
 * @brief QML value-type wrapper around `PhosphorAnimation::Profile`.
 *
 * Q_GADGET per Phase 4 decision O — compile-time snapshot shape for
 * `PhosphorMotionAnimation.profile: PhosphorProfile { … }` (decision R's
 * value-binding branch). The path-string branch goes through
 * `PhosphorProfileRegistry` and lands in a later sub-commit.
 *
 * ## Optional-field treatment in QML
 *
 * `Profile`'s fields are `std::optional<T>` so ProfileTree inheritance
 * can distinguish "unset, inherit" from "explicitly set to library
 * default" (see Profile.h's class doc). QML cannot represent
 * `std::optional<T>` directly, so the wrapper collapses the
 * distinction at the QML boundary:
 *
 *   - Reading a property returns the **effective** value (`Profile::
 *     effective*`). Unset fields read back as their library default.
 *   - Writing a property **engages** the optional — the field becomes
 *     "explicitly set". There is no QML-reachable way to reset a field
 *     back to unset (call `Profile::*.reset()` from C++ if needed).
 *
 * This matches how plugin authors typically use `PhosphorProfile`:
 * construct a compile-time literal with every field they care about,
 * hand it to `PhosphorMotionAnimation.profile`, and treat it as
 * immutable. ProfileTree inheritance is a C++-side concern accessed
 * through the core `ProfileTree` API, not through this wrapper.
 */
class PHOSPHORANIMATION_EXPORT PhosphorProfile
{
    Q_GADGET
    QML_VALUE_TYPE(phosphorProfile)
    QML_STRUCTURED_VALUE

    Q_PROPERTY(PhosphorCurve curve READ curve WRITE setCurve)
    Q_PROPERTY(qreal duration READ duration WRITE setDuration)
    Q_PROPERTY(int minDistance READ minDistance WRITE setMinDistance)
    Q_PROPERTY(SequenceMode sequenceMode READ sequenceMode WRITE setSequenceMode)
    Q_PROPERTY(int staggerInterval READ staggerInterval WRITE setStaggerInterval)
    Q_PROPERTY(QString presetName READ presetName WRITE setPresetName)

public:
    // Parallel to `PhosphorAnimation::SequenceMode` so QML enum
    // references work without touching the C++ scope. Integer values
    // match for `static_cast` round-trip — matches decision O's "do
    // not rename enum values" convention used by `PhosphorEasing::Type`.
    enum class SequenceMode : int {
        AllAtOnce = int(PhosphorAnimation::SequenceMode::AllAtOnce),
        Cascade = int(PhosphorAnimation::SequenceMode::Cascade),
    };
    Q_ENUM(SequenceMode)

    PhosphorProfile() = default;
    /// Implicit-conversion ctor from core-library value.
    PhosphorProfile(const Profile& value)
        : m_value(value)
    {
    }

    /// Read-only access to the underlying value. The non-const overload
    /// was deliberately removed: a mutable handle from QML let scripts
    /// bypass the setter clamps below by writing directly into the
    /// engaged-optional fields. Core-library mutators construct a fresh
    /// Profile and assign through the implicit-conversion ctor instead.
    const Profile& value() const
    {
        return m_value;
    }

    // ─── Property delegates ───
    //
    // Setters mirror the validation in `Profile::fromJson`: NaN/inf
    // and out-of-range values are silently rejected (the field stays
    // unset, so `effective*()` substitutes the library default).
    // QML scripts that pass garbage get the same fault-tolerant
    // behaviour as a malformed profile JSON file rather than landing
    // pathological values into a QQuickPropertyAnimation downstream.

    PhosphorCurve curve() const
    {
        return PhosphorCurve(m_value.curve);
    }
    void setCurve(const PhosphorCurve& c)
    {
        m_value.curve = c.curve();
    }

    qreal duration() const
    {
        return m_value.effectiveDuration();
    }
    void setDuration(qreal ms)
    {
        if (!std::isfinite(ms) || ms <= 0.0 || ms > Profile::MaxDurationMs) {
            m_value.duration.reset();
            return;
        }
        m_value.duration = ms;
    }

    int minDistance() const
    {
        return m_value.effectiveMinDistance();
    }
    void setMinDistance(int px)
    {
        if (px < 0) {
            m_value.minDistance.reset();
            return;
        }
        m_value.minDistance = px;
    }

    SequenceMode sequenceMode() const
    {
        return static_cast<SequenceMode>(static_cast<int>(m_value.effectiveSequenceMode()));
    }
    void setSequenceMode(SequenceMode mode)
    {
        m_value.sequenceMode = static_cast<PhosphorAnimation::SequenceMode>(static_cast<int>(mode));
    }

    int staggerInterval() const
    {
        return m_value.effectiveStaggerInterval();
    }
    void setStaggerInterval(int ms)
    {
        if (ms < 0 || ms > Profile::MaxStaggerIntervalMs) {
            m_value.staggerInterval.reset();
            return;
        }
        m_value.staggerInterval = ms;
    }

    QString presetName() const
    {
        return m_value.presetName.value_or(QString());
    }
    void setPresetName(const QString& name)
    {
        m_value.presetName = name;
    }

    // ─── Serialization ───

    /// Serialize to a JSON object via `Profile::toJson`. Only engaged
    /// fields are written, matching the inheritance-preserving JSON
    /// shape the C++ side produces.
    Q_INVOKABLE QJsonObject toJson() const
    {
        return m_value.toJson();
    }

    /// Parse from a JSON object. Missing keys become unset fields
    /// (read back as library defaults via `effective*`).
    ///
    /// Uses the process-wide `CurveRegistry` installed via
    /// `PhosphorCurve::setDefaultRegistry` when available so
    /// user-authored curves registered by `CurveLoader` are visible
    /// to this parse path. Falls back to a function-local static
    /// registry (built-ins only) when no default registry has been
    /// installed — this covers startup-before-composition-root and
    /// test harness paths where the daemon hasn't published a
    /// user-curve-aware registry yet. Consulting the published
    /// registry first matches the convention used by
    /// `PhosphorCurve::fromString`.
    Q_INVOKABLE static PhosphorProfile fromJson(const QJsonObject& obj)
    {
        static CurveRegistry sFallback;
        CurveRegistry* registry = PhosphorCurve::defaultRegistry();
        return PhosphorProfile(Profile::fromJson(obj, registry ? *registry : sFallback));
    }

    // ─── Equality ───

    bool operator==(const PhosphorProfile& other) const
    {
        return m_value == other.m_value;
    }
    bool operator!=(const PhosphorProfile& other) const
    {
        return !(*this == other);
    }

private:
    Profile m_value;
};

} // namespace PhosphorAnimation

Q_DECLARE_METATYPE(PhosphorAnimation::PhosphorProfile)
