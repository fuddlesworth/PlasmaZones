// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

function evaluate(t, curveStr) {
    if (!curveStr || curveStr.length === 0)
        return t;

    var named = parseNamedCurve(curveStr);
    if (named) {
        if (named.name === "elastic-in")
            return evaluateElasticIn(t, named.amp, named.per);
        if (named.name === "elastic-out")
            return evaluateElasticOut(t, named.amp, named.per);
        if (named.name === "elastic-in-out")
            return evaluateElasticInOut(t, named.amp, named.per);
        if (named.name === "bounce-in")
            return evaluateBounceIn(t, named.amp, named.bounces);
        if (named.name === "bounce-out")
            return evaluateBounceOut(t, named.amp, named.bounces);
        if (named.name === "bounce-in-out")
            return evaluateBounceInOut(t, named.amp, named.bounces);
        return t;
    }

    var parts = curveStr.split(",");
    if (parts.length !== 4)
        return t;
    var x1 = parseFloat(parts[0]);
    var y1 = parseFloat(parts[1]);
    var x2 = parseFloat(parts[2]);
    var y2 = parseFloat(parts[3]);
    return cubicBezier(t, x1, y1, x2, y2);
}

function parseNamedCurve(curveStr) {
    var idx = curveStr.indexOf(":");
    if (idx < 0)
        return null;
    var name = curveStr.substring(0, idx);
    var params = curveStr.substring(idx + 1).split(",");
    if (name.startsWith("elastic")) {
        return {
            name: name,
            amp: params.length > 0 ? parseFloat(params[0]) : 1.0,
            per: params.length > 1 ? parseFloat(params[1]) : 0.3,
            bounces: 0
        };
    }
    if (name.startsWith("bounce")) {
        return {
            name: name,
            amp: params.length > 0 ? parseFloat(params[0]) : 1.5,
            per: 0,
            bounces: params.length > 1 ? parseInt(params[1]) : 4
        };
    }
    return null;
}

function cubicBezier(t, x1, y1, x2, y2) {
    var cx = 3.0 * x1;
    var bx = 3.0 * (x2 - x1) - cx;
    var ax = 1.0 - cx - bx;
    var cy = 3.0 * y1;
    var by = 3.0 * (y2 - y1) - cy;
    var ay = 1.0 - cy - by;

    var guess = t;
    for (var i = 0; i < 8; i++) {
        var currentX = ((ax * guess + bx) * guess + cx) * guess;
        var dx = currentX - t;
        if (Math.abs(dx) < 1e-6)
            break;
        var deriv = (3.0 * ax * guess + 2.0 * bx) * guess + cx;
        if (Math.abs(deriv) < 1e-6)
            break;
        guess -= dx / deriv;
    }
    return ((ay * guess + by) * guess + cy) * guess;
}

function evaluateElasticOut(t, amplitude, period) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    var a = Math.max(amplitude, 1.0);
    var p = Math.max(period, 0.05);
    var s = p / (2 * Math.PI) * Math.asin(1.0 / a);
    return a * Math.pow(2, -10 * t) * Math.sin((t - s) * (2 * Math.PI) / p) + 1;
}

function evaluateElasticIn(t, amplitude, period) {
    return 1.0 - evaluateElasticOut(1.0 - t, amplitude, period);
}

function evaluateElasticInOut(t, amplitude, period) {
    if (t < 0.5)
        return evaluateElasticIn(t * 2, amplitude, period) * 0.5;
    return evaluateElasticOut(t * 2 - 1, amplitude, period) * 0.5 + 0.5;
}

function evaluateBounceOut(t, amplitude, bounces) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    var n = Math.max(bounces, 1);
    var a = Math.max(amplitude, 1.0);
    var invN = 1.0 / n;
    for (var i = 0; i < n; i++) {
        var lo = i * invN;
        var hi = (i + 1) * invN;
        if (t >= lo && t < hi) {
            var local = (t - lo) / (hi - lo);
            var scale = Math.pow(a, -(n - 1 - i));
            return 1.0 - scale * (1.0 - 4.0 * local * (1.0 - local));
        }
    }
    return 1.0;
}

function evaluateBounceIn(t, amplitude, bounces) {
    return 1.0 - evaluateBounceOut(1.0 - t, amplitude, bounces);
}

function evaluateBounceInOut(t, amplitude, bounces) {
    if (t < 0.5)
        return evaluateBounceIn(t * 2, amplitude, bounces) * 0.5;
    return evaluateBounceOut(t * 2 - 1, amplitude, bounces) * 0.5 + 0.5;
}
