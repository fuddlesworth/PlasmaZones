// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

function evaluate(t, omega, zeta) {
    if (t <= 0) return 0;
    if (zeta < 1.0) {
        var wd = omega * Math.sqrt(1.0 - zeta * zeta);
        var decay = Math.exp(-zeta * omega * t);
        return 1.0 - decay * (Math.cos(wd * t) + (zeta * omega / wd) * Math.sin(wd * t));
    } else if (zeta === 1.0) {
        var decay2 = Math.exp(-omega * t);
        return 1.0 - decay2 * (1.0 + omega * t);
    } else {
        var s1 = -omega * (zeta + Math.sqrt(zeta * zeta - 1.0));
        var s2 = -omega * (zeta - Math.sqrt(zeta * zeta - 1.0));
        var c2 = -s1 / (s2 - s1);
        var c1 = 1.0 - c2;
        return 1.0 - c1 * Math.exp(s1 * t) - c2 * Math.exp(s2 * t);
    }
}

function isSettled(t, omega, zeta, epsilon) {
    var v = evaluate(t, omega, zeta);
    return Math.abs(v - 1.0) < epsilon;
}

function estimateSettleTime(omega, zeta, epsilon) {
    var dt = 0.01;
    var consecutive = 0;
    for (var t = 0; t < 30.0; t += dt) {
        if (isSettled(t, omega, zeta, epsilon)) {
            consecutive++;
            if (consecutive >= 3)
                return t - 2 * dt;
        } else {
            consecutive = 0;
        }
    }
    return 30.0;
}
