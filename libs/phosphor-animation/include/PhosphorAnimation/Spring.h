// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QtGlobal>

namespace PhosphorAnimation {

/**
 * @brief Damped harmonic oscillator (physics spring).
 *
 * Parameterized in the industry-standard UI form:
 *
 * - `omega` (ω₀) — angular natural frequency in rad/s. Higher = stiffer,
 *   faster response. Typical UI range: 4–30 rad/s. Bounds: [0.1, 200].
 * - `zeta` (ζ) — damping ratio (dimensionless).
 *     - 0.0        undamped (oscillates forever; never used in UI)
 *     - 0.0–1.0    underdamped (bouncy, overshoots target)
 *     - 1.0        critically damped (fastest non-oscillatory approach)
 *     - > 1.0      overdamped (slow, no overshoot, no oscillation)
 *   Bounds: [0.0, 10.0].
 *
 * Matches Framer Motion / SwiftUI conventions (they expose response +
 * damping ratio, which converts trivially: `omega = 2π / response`).
 *
 * ## Evaluation models
 *
 * - `evaluate(t)` — parametric: maps t∈[0,1] to real time [0, settleTime()]
 *   and returns the analytical step-response position. Overshoot preserved
 *   by design (elastic feel). Useful for fixed-duration animations that
 *   want spring feel without full physics integration.
 *
 * - `step(dt, state, target)` — stateful: semi-implicit Euler integration.
 *   Preserves velocity across calls — retarget mid-flight is natural
 *   (change `target`, velocity carries through, no visible discontinuity).
 *   This is the API AnimatedValue<T> (Phase 3) will use.
 *
 * ## Serialized string form
 *
 *   `"spring:omega,zeta"`    — e.g., `"spring:12.0,0.8"`
 *
 * ## Stability notes
 *
 * Semi-implicit Euler is stable for `dt < 1 / (5·omega)`. At omega=30
 * that's ~6.6 ms — well below 16 ms at 60 Hz. For extreme stiffness or
 * variable frame rates, callers may need to substep internally.
 */
class PHOSPHORANIMATION_EXPORT Spring final : public Curve
{
public:
    Spring() = default;
    Spring(qreal omega, qreal zeta);

    Spring(const Spring&) = default;
    Spring& operator=(const Spring&) = default;
    Spring(Spring&&) = default;
    Spring& operator=(Spring&&) = default;

    // ─────── Curve overrides ───────

    QString typeId() const override;
    qreal evaluate(qreal t) const override;
    void step(qreal dt, CurveState& state, qreal target) const override;
    bool isStateful() const override
    {
        return true;
    }
    qreal settleTime() const override;
    QString toString() const override;
    std::unique_ptr<Curve> clone() const override;

    // ─────── Spring-specific API ───────

    /// Parse from `"spring:omega,zeta"` or `"omega,zeta"`. Returns the
    /// default Spring on parse failure. Parameters are clamped to valid
    /// ranges rather than rejected.
    static Spring fromString(const QString& str);

    // ─────── Presets ───────

    /// Responsive, slight overshoot. Good default for window snap.
    static Spring snappy();
    /// Critically damped. No overshoot, firm approach.
    static Spring smooth();
    /// Visible bounce. Good for attention-grabbing feedback.
    static Spring bouncy();

    // ─────── Parameters ───────

    /// Angular natural frequency in rad/s. Clamped to [0.1, 200] on parse.
    qreal omega = 12.0;
    /// Damping ratio (dimensionless). Clamped to [0.0, 10.0] on parse.
    qreal zeta = 0.8;

    // ─────── Value-type equality ───────

    bool operator==(const Spring& other) const;
    bool operator!=(const Spring& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorAnimation
