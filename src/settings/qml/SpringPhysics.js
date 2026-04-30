// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

/**
 * @brief Shared spring physics evaluation matching C++ SpringAnimation::evaluate().
 *
 * Single source of truth for all QML spring calculations (SpringPreview,
 * CurveThumbnail, etc.) — keeps the damped harmonic oscillator formula
 * in one place instead of duplicated across components.
 */

/**
 * Evaluate spring position at time t (seconds). Returns 0→1 (may overshoot).
 * Matches C++ SpringAnimation::evaluate() in windowanimator.cpp.
 */
function evaluate(t, stiffness, dampingRatio, initialVelocity) {
    if (t <= 0)
        return 0;

    var omega = Math.sqrt(Math.max(1, stiffness));
    var zeta = dampingRatio;
    zeta = Math.max(0.01, zeta);
    var v0 = initialVelocity || 0;

    if (zeta < 1) {
        // Underdamped: oscillates
        var omegaD = omega * Math.sqrt(1 - zeta * zeta);
        var decay = Math.exp(-zeta * omega * t);
        var sinTerm = (zeta * omega - v0) / omegaD;
        return 1 - decay * (Math.cos(omegaD * t) + sinTerm * Math.sin(omegaD * t));
    } else if (Math.abs(zeta - 1) < 1e-9) {
        // Critically damped: fastest without oscillation
        var decayCrit = Math.exp(-omega * t);
        return 1 - decayCrit * (1 + (omega - v0) * t);
    } else {
        // Overdamped: no oscillation, slower
        var disc = Math.sqrt(zeta * zeta - 1);
        var s1 = -omega * (zeta + disc);
        var s2 = -omega * (zeta - disc);
        // Guard near-degenerate eigenvalues (zeta barely > 1): fall back to
        // critically damped formula to avoid division by near-zero (s2 - s1).
        if (Math.abs(s2 - s1) < 1e-6) {
            var decayFb = Math.exp(-omega * t);
            return 1 - decayFb * (1 + (omega - v0) * t);
        }
        var c2 = (v0 - s1) / (s2 - s1);
        var c1 = 1 - c2;
        return 1 - (c1 * Math.exp(s1 * t) + c2 * Math.exp(s2 * t));
    }
}

/**
 * Check if the spring has settled within epsilon at time t (seconds).
 * Matches C++ SpringAnimation::isSettled().
 */
function isSettled(t, stiffness, dampingRatio, epsilon) {
    if (t <= 0)
        return false;
    var pos = evaluate(t, stiffness, dampingRatio, 0);
    var dt = 0.001;
    var vel = (evaluate(t + dt, stiffness, dampingRatio, 0) - pos) / dt;
    return Math.abs(pos - 1) < epsilon && Math.abs(vel) < epsilon * 10;
}

/**
 * Estimate settle time via sampling — requires 3 consecutive settled samples
 * to avoid false positives from underdamped springs briefly crossing epsilon.
 * Matches C++ SpringAnimation::estimatedDuration().
 */
function estimateSettleTime(stiffness, dampingRatio, epsilon) {
    var maxT = 10;
    var dt = 0.005;
    var requiredConsecutive = 3;
    var consecutive = 0;
    for (var t = dt; t <= maxT; t += dt) {
        if (isSettled(t, stiffness, dampingRatio, epsilon)) {
            if (++consecutive >= requiredConsecutive)
                return t - (requiredConsecutive - 1) * dt;
        } else {
            consecutive = 0;
        }
    }
    return maxT;
}
