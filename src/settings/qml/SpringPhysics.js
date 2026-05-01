// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

/**
 * @brief Shared spring physics evaluation matching C++ Spring::evaluate().
 *
 * Single source of truth for all QML spring calculations (SpringPreview,
 * CurveThumbnail, etc.) — keeps the damped harmonic oscillator formula
 * in one place instead of duplicated across components.
 *
 * The C++ implementation is `Spring::evaluate()` in
 * `libs/phosphor-animation/src/spring.cpp`. Note the time-domain
 * difference: this JS treats `t` as REAL SECONDS (Date.now()-driven
 * preview timer), whereas the C++ side treats `t` as a normalised
 * [0,1] progress that it scales onto seconds via `settleTime()`. The
 * step-response math (the body of `evaluate` below) is identical for a
 * given `(omega, zeta)`; only the parameterisation of `t` differs.
 */

/**
 * Evaluate spring position at time t (seconds). Returns 0→1 (may overshoot).
 *
 * @param t real seconds since the spring was released
 * @param stiffness omega² — the C++ side stores `omega` directly; pass
 *        `omega * omega` here for parity
 * @param dampingRatio zeta ∈ [0, 10]; 0 = undamped, 1 = critical, >1 = over
 * @param initialVelocity optional v0; defaults to 0
 */
function evaluate(t, stiffness, dampingRatio, initialVelocity) {
    if (t <= 0)
        return 0;

    // Floor stiffness with a tiny epsilon (not 1) so omega = sqrt(stiffness)
    // can express the full C++ allowed range (omega ≥ 0.1, i.e.
    // stiffness ≥ 0.01). The earlier `Math.max(1, stiffness)` silently
    // pinned omega to a 1 rad/s floor, giving incorrect shapes for
    // low-stiffness presets.
    var omega = Math.sqrt(Math.max(1e-6, stiffness));
    // Mirror the C++ Spring's allowed zeta range [0, 10] verbatim. zeta=0
    // (truly undamped) is supported by the underdamped branch below
    // (sqrt(1 - 0) = 1, finite cosine/sine), so no additional clamp is
    // needed.
    var zeta = Math.max(0, Math.min(10, dampingRatio));
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
 * Matches C++ Spring::isSettled() in libs/phosphor-animation/src/spring.cpp.
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
 * Settle-time estimate, matching C++ `Spring::settleTime()` in
 * `libs/phosphor-animation/src/spring.cpp` analytically rather than by
 * sampling. The previous sampling-based implementation walked the spring
 * at 0.005s steps for up to 10s — ~2000 evaluate() calls per request,
 * with isSettled() invoking evaluate() twice per step (~4000 calls).
 * Each repaint of CurveThumbnail / SpringPreview triggered this, and the
 * C++ side already exposes the closed-form solution: switch to it.
 *
 * Underdamped (ζ < 1):     T = -ln(eps) / (ζ · ω)
 * Critically damped (ζ=1): T ≈ 5.834 / ω    (-W₋₁(-eps · e⁻¹) · 1/ω)
 * Overdamped (ζ > 1):      T = -ln(eps) / (ω · (ζ - √(ζ²-1)))
 *
 * `epsilon` defaults to 0.02 — same threshold the C++ side uses.
 */
function estimateSettleTime(stiffness, dampingRatio, epsilon) {
    // The critical-damped factor 5.834 is the analytical solution to
    // (1+ωt)·exp(-ωt) = 0.02 — it is BAKED to eps=0.02. The other
    // branches scale with -ln(eps), so a non-default eps would mix
    // inconsistent regimes at the critical-band blend. Hardcode 0.02
    // to match C++ Spring::settleTime() (libs/phosphor-animation/src/
    // spring.cpp:38) and document that the parameter is reserved.
    void epsilon; // currently unused; kept in the signature for future use.
    var omega = Math.sqrt(Math.max(1e-6, stiffness));
    var zeta = Math.max(0, Math.min(10, dampingRatio));
    var eps = 0.02;
    var maxT = 10;

    // Critical-damping epsilon — match C++ Spring::CriticalDampingEpsilon
    // (libs/phosphor-animation/src/spring.cpp:38). A wider band here would
    // produce a settle time on a different ζ partition than evaluate()
    // does, causing visible canvas-width stutter in SpringPreview.
    var critBand = 1e-3;

    var t;
    if (Math.abs(zeta - 1) < critBand) {
        t = 5.834 / omega;
    } else if (zeta < 1) {
        t = -Math.log(eps) / (zeta * omega);
    } else {
        t = -Math.log(eps) / (omega * (zeta - Math.sqrt(zeta * zeta - 1)));
    }
    if (!isFinite(t) || t < 0)
        t = maxT;
    return Math.min(t, maxT);
}
