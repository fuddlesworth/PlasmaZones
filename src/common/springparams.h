// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

namespace PlasmaZones {

/**
 * @brief Spring physics parameters for damped harmonic oscillator animations
 *
 * Shared between src/core (daemon config layer) and kwin-effect (compositor runtime).
 *
 * Models: x(t) = e^(-zeta*omega*t) * (A*cos(omega_d*t) + B*sin(omega_d*t))
 * where omega = sqrt(stiffness), zeta = dampingRatio, omega_d = omega*sqrt(1-zeta^2)
 *
 * This struct holds the persistent configuration fields. The kwin-effect's
 * SpringAnimation inherits from this and adds runtime-only fields (initialVelocity)
 * plus physics evaluation methods (evaluate, isSettled, estimatedDuration).
 */
struct SpringParams
{
    qreal dampingRatio = 1.0; ///< 0.1-10.0: <1 underdamped (bouncy), =1 critical, >1 overdamped
    qreal stiffness = 800.0; ///< 1-2000: spring stiffness (higher = faster)
    qreal epsilon = 0.0001; ///< 0.00001-0.1: convergence threshold (lower = longer)

    bool operator==(const SpringParams& other) const
    {
        return qFuzzyCompare(1.0 + dampingRatio, 1.0 + other.dampingRatio)
            && qFuzzyCompare(1.0 + stiffness, 1.0 + other.stiffness)
            && qFuzzyCompare(1.0 + epsilon, 1.0 + other.epsilon);
    }
};

} // namespace PlasmaZones
