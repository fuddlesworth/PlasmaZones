// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
pragma Singleton

/**
 * @brief Centralized animation curve and spring presets.
 *
 * Single source of truth for all preset lists used across:
 *   - AnimationEventCard (inline preset combo)
 *   - CurveEditorDialog (full editor preset combo)
 *
 * Easing presets include bezier curves in "x1,y1,x2,y2" format,
 * named curves like "elastic-out:1.00,0.30", and direction variants.
 */
QtObject {
    // ── Lookup helpers ─────────────────────────────────────────────

    // ── Shared preset data (loaded from data/curvepresets.json resource) ──
    readonly property var _sharedData: {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "qrc:/curvepresets.json", false);
        xhr.send();
        if (xhr.status === 200 || xhr.readyState === XMLHttpRequest.DONE)
            return JSON.parse(xhr.responseText);

        console.warn("CurvePresets: failed to load curvepresets.json from resource");
        return {
            "quickPresets": [],
            "springPresets": []
        };
    }
    // ── Timing mode constants ─────────────────────────────────────
    readonly property int timingModeEasing: 0
    readonly property int timingModeSpring: 1
    // ── Easing curve presets ────────────────────────────────────────
    // Each preset has: label, key, and curves for in/out/in-out directions
    readonly property var easingStyles: [{
        "label": i18n("Linear"),
        "key": "linear"
    }, {
        "label": i18n("Gentle (Sine)"),
        "key": "sine"
    }, {
        "label": i18n("Smooth (Quad)"),
        "key": "quad"
    }, {
        "label": i18n("Standard (Cubic)"),
        "key": "cubic"
    }, {
        "label": i18n("Snappy (Quart)"),
        "key": "quart"
    }, {
        "label": i18n("Sharp (Quint)"),
        "key": "quint"
    }, {
        "label": i18n("Aggressive (Expo)"),
        "key": "expo"
    }, {
        "label": i18n("Circular (Circ)"),
        "key": "circ"
    }, {
        "label": i18n("Overshoot (Back)"),
        "key": "back"
    }, {
        "label": i18n("Elastic"),
        "key": "elastic"
    }, {
        "label": i18n("Bounce"),
        "key": "bounce"
    }]
    readonly property var easingDirections: [{
        "label": i18n("Ease In"),
        "key": "in"
    }, {
        "label": i18n("Ease Out"),
        "key": "out"
    }, {
        "label": i18n("Ease In-Out"),
        "key": "in-out"
    }]
    // Curve values: style key → direction key → curve string
    readonly property var easingCurves: ({
        "linear": {
            "in": "0.00,0.00,1.00,1.00",
            "out": "0.00,0.00,1.00,1.00",
            "in-out": "0.00,0.00,1.00,1.00"
        },
        "sine": {
            "in": "0.12,0.00,0.39,0.00",
            "out": "0.61,1.00,0.88,1.00",
            "in-out": "0.37,0.00,0.63,1.00"
        },
        "quad": {
            "in": "0.11,0.00,0.50,0.00",
            "out": "0.50,1.00,0.89,1.00",
            "in-out": "0.45,0.00,0.55,1.00"
        },
        "cubic": {
            "in": "0.32,0.00,0.67,0.00",
            "out": "0.33,1.00,0.68,1.00",
            "in-out": "0.65,0.00,0.35,1.00"
        },
        "quart": {
            "in": "0.50,0.00,0.75,0.00",
            "out": "0.25,1.00,0.50,1.00",
            "in-out": "0.76,0.00,0.24,1.00"
        },
        "quint": {
            "in": "0.64,0.00,0.78,0.00",
            "out": "0.23,1.00,0.32,1.00",
            "in-out": "0.83,0.00,0.17,1.00"
        },
        "expo": {
            "in": "0.70,0.00,0.84,0.00",
            "out": "0.16,1.00,0.30,1.00",
            "in-out": "0.87,0.00,0.13,1.00"
        },
        "circ": {
            "in": "0.55,0.00,1.00,0.45",
            "out": "0.00,0.55,0.45,1.00",
            "in-out": "0.85,0.00,0.15,1.00"
        },
        "back": {
            "in": "0.36,0.00,0.66,-0.56",
            "out": "0.18,0.89,0.32,1.28",
            "in-out": "0.68,-0.55,0.27,1.55"
        },
        "elastic": {
            "in": "elastic-in:1.00,0.30",
            "out": "elastic-out:1.00,0.30",
            "in-out": "elastic-in-out:1.00,0.30"
        },
        "bounce": {
            "in": "bounce-in:1.00,3",
            "out": "bounce-out:1.00,3",
            "in-out": "bounce-in-out:1.00,3"
        }
    })
    // ── Quick preset list (loaded from shared data/curvepresets.json) ──
    // Labels are wrapped in i18n() for translation at display time.
    readonly property var quickPresets: {
        var raw = _sharedData.quickPresets || [];
        var result = [];
        for (var i = 0; i < raw.length; i++) result.push({
            "label": i18n(raw[i].label),
            "curve": raw[i].curve
        })
        return result;
    }
    // ── Spring presets (loaded from shared data/curvepresets.json) ──
    readonly property var springPresets: {
        var raw = _sharedData.springPresets || [];
        var result = [];
        for (var i = 0; i < raw.length; i++) result.push({
            "label": i18n(raw[i].label),
            "dampingRatio": raw[i].dampingRatio,
            "stiffness": raw[i].stiffness,
            "epsilon": raw[i].epsilon
        })
        return result;
    }

    /// Find the style index and direction index for a given curve string
    function findIndices(curve) {
        if (!curve || curve === "")
            return {
            "styleIndex": -1,
            "dirIndex": 1
        };

        var norm = normalizeCurve(curve);
        for (var si = 0; si < easingStyles.length; si++) {
            var key = easingStyles[si].key;
            if (!easingCurves[key])
                continue;

            for (var di = 0; di < easingDirections.length; di++) {
                var dirKey = easingDirections[di].key;
                if (normalizeCurve(easingCurves[key][dirKey]) === norm)
                    return {
                    "styleIndex": si,
                    "dirIndex": di
                };

            }
        }
        return {
            "styleIndex": -1,
            "dirIndex": 1
        };
    }

    /// Get the curve string for a style + direction selection
    function curveForSelection(styleIndex, dirIndex) {
        if (styleIndex < 0 || styleIndex >= easingStyles.length)
            return "";

        if (dirIndex < 0 || dirIndex >= easingDirections.length)
            return "";

        var key = easingStyles[styleIndex].key;
        var dirKey = easingDirections[dirIndex].key;
        return easingCurves[key] ? easingCurves[key][dirKey] : "";
    }

    /// Find quick preset index for a curve string (0 = Custom if no match)
    function quickPresetIndex(curve) {
        for (var i = 0; i < quickPresets.length; i++) {
            if (quickPresets[i].curve === curve)
                return i;

        }
        return -1;
    }

    /// Find spring preset index for given params (0 = Custom if no match)
    function springPresetIndex(damping, stiffness, epsilon) {
        for (var i = 0; i < springPresets.length; i++) {
            var p = springPresets[i];
            if (Math.abs(p.dampingRatio - damping) < 0.01 && Math.abs(p.stiffness - stiffness) < 1 && Math.abs(p.epsilon - epsilon) < 1e-05)
                return i;

        }
        return -1;
    }

    /// Human-readable name for a curve string
    function curveDisplayName(curve) {
        if (!curve)
            return i18n("Custom");

        var idx = findIndices(curve);
        if (idx.styleIndex >= 0)
            return easingStyles[idx.styleIndex].label;

        return i18n("Custom");
    }

    /// Normalize a curve string for comparison (trim decimals)
    function normalizeCurve(curve) {
        if (!curve || curve === "")
            return "";

        // Check for named curves (contains letters other than e/E used in scientific notation)
        if (/[a-df-zA-DF-Z]/.test(curve))
            return curve.trim();

        // Bezier: normalize to 2 decimal places
        var parts = curve.split(",");
        if (parts.length !== 4)
            return "";

        var nums = [];
        for (var j = 0; j < 4; j++) {
            var v = parseFloat(parts[j]);
            if (!isFinite(v))
                return "";

            nums.push(v.toFixed(2));
        }
        return nums.join(",");
    }

}
