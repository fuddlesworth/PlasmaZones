// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

/**
 * @brief Shared easing curve evaluation matching C++ EasingCurve::evaluate().
 *
 * Single source of truth for all QML easing calculations (CurveThumbnail,
 * EasingPreview, etc.) — keeps the bezier/elastic/bounce formulas in one
 * place instead of duplicated across components.
 */

/**
 * Evaluate elastic-out at time t (0-1).
 * Matches C++ EasingCurve::evaluateElasticOut().
 */
function evaluateElasticOut(t, amp, per) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    if (per <= 0) per = 0.3;
    var a = Math.max(1, amp);
    var s = per / (2 * Math.PI) * Math.asin(1 / a);
    return a * Math.pow(2, -10 * t) * Math.sin((t - s) * 2 * Math.PI / per) + 1;
}

/**
 * Evaluate bounce-out at time t (0-1).
 * Matches C++ EasingCurve::evaluateBounceOut().
 */
function evaluateBounceOut(t, amp, bounces) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    var r = 0.5;
    var n = Math.max(1, Math.min(8, bounces));
    var S = (1 - Math.pow(r, n)) / (1 - r);
    var d = 1 / (1 + S);
    if (t < d) {
        var u = t / d;
        return u * u;
    }
    var tAcc = d;
    for (var k = 0; k < n; k++) {
        var dk = d * Math.pow(r, k);
        if (t < tAcc + dk || k === n - 1) {
            var u2 = (t - tAcc) / dk;
            var ht = Math.pow(r, 2 * (k + 1));
            var dip = 1 - 4 * (u2 - 0.5) * (u2 - 0.5);
            return 1 - ht * amp * dip;
        }
        tAcc += dk;
    }
    return 1;
}

/**
 * Parse a named curve string into its components.
 * Returns { name, amp, per, bounces } or null if not a named curve.
 */
function parseNamedCurve(curveStr) {
    if (curveStr.indexOf("elastic") < 0 && curveStr.indexOf("bounce") < 0)
        return null;
    var colonIdx = curveStr.indexOf(":");
    var name = colonIdx >= 0 ? curveStr.substring(0, colonIdx).trim() : curveStr.trim();
    var params = colonIdx >= 0 ? curveStr.substring(colonIdx + 1).trim() : "";
    var amp = 1, per = 0.3, bounces = 3;
    if (params) {
        var pp = params.split(",");
        if (pp.length >= 1) {
            var parsedAmp = parseFloat(pp[0]);
            if (isFinite(parsedAmp)) amp = parsedAmp;
        }
        if (pp.length >= 2) {
            if (name.indexOf("elastic") >= 0) {
                var parsedPer = parseFloat(pp[1]);
                if (isFinite(parsedPer)) per = parsedPer;
            } else {
                var parsedBounces = parseInt(pp[1]);
                if (isFinite(parsedBounces)) bounces = parsedBounces;
            }
        }
    }
    return { name: name, amp: amp, per: per, bounces: bounces };
}

/**
 * Evaluate any easing curve at time t (0-1).
 * Handles bezier "x1,y1,x2,y2", elastic, and bounce named curves.
 * Matches C++ EasingCurve::evaluate().
 */
function evaluate(t, curveStr) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;

    // Named curves (elastic/bounce)
    var named = parseNamedCurve(curveStr);
    if (named) {
        var name = named.name;
        var amp = named.amp;
        var per = named.per;
        var bounces = named.bounces;

        if (name === "elastic-out")
            return evaluateElasticOut(t, amp, per);
        if (name === "elastic-in")
            return 1 - evaluateElasticOut(1 - t, amp, per);
        if (name === "elastic-in-out") {
            if (t < 0.5)
                return (1 - evaluateElasticOut(1 - 2 * t, amp, per)) * 0.5;
            return evaluateElasticOut(2 * t - 1, amp, per) * 0.5 + 0.5;
        }
        if (name === "bounce-out")
            return evaluateBounceOut(t, amp, bounces);
        if (name === "bounce-in")
            return 1 - evaluateBounceOut(1 - t, amp, bounces);
        if (name === "bounce-in-out") {
            if (t < 0.5)
                return (1 - evaluateBounceOut(1 - 2 * t, amp, bounces)) * 0.5;
            return evaluateBounceOut(2 * t - 1, amp, bounces) * 0.5 + 0.5;
        }
        return t;
    }

    // Bezier: "x1,y1,x2,y2"
    var parts = curveStr.split(",");
    if (parts.length !== 4)
        return t;

    var x1 = parseFloat(parts[0]); if (!isFinite(x1)) x1 = 0.33;
    var y1 = parseFloat(parts[1]); if (!isFinite(y1)) y1 = 1;
    var x2 = parseFloat(parts[2]); if (!isFinite(x2)) x2 = 0.68;
    var y2 = parseFloat(parts[3]); if (!isFinite(y2)) y2 = 1;

    // Newton's method to solve bezier x(p) = t
    var p = t;
    for (var i = 0; i < 8; i++) {
        var mt = 1 - p;
        var bx = 3 * mt * mt * p * x1 + 3 * mt * p * p * x2 + p * p * p - t;
        var dbx = 3 * mt * mt * x1 + 6 * mt * p * (x2 - x1) + 3 * p * p * (1 - x2);
        if (Math.abs(dbx) < 1e-12)
            break;
        p -= bx / dbx;
        p = Math.max(0, Math.min(1, p));
    }
    var mt2 = 1 - p;
    return 3 * mt2 * mt2 * p * y1 + 3 * mt2 * p * p * y2 + p * p * p;
}
