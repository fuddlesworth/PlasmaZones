// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
// `pragma Singleton` MUST appear before any `import` — Qt silently ignores
// it when placed below the imports, which leaves the qmldir entry as a
// plain `CurvePresets 1.0 …` line instead of `singleton CurvePresets 1.0
// …`. Consumers then access the QML *type* rather than its singleton
// instance, so `CurvePresets.curveDisplayName(...)` resolves to undefined
// and every page that touches presets crashes at startup with
// "TypeError: Property 'X' of object CurvePresets is not a function".
pragma Singleton

/**
 * @brief Centralized animation curve and spring presets.
 *
 * Single source of truth for all preset lists used across:
 *   - AnimationEventCard (inline preset combo)
 *   - CurveEditorDialog (full editor preset combo)
 *   - AnimationsPresetsPage (preset library)
 *
 * Easing presets include bezier curves in "x1,y1,x2,y2" format and named
 * curves like "elastic-out:1.00,0.30". Spring presets use the (omega, zeta)
 * shape from `PhosphorAnimation::Spring` — values mirror the C++
 * `Spring::snappy()`, `::smooth()`, `::bouncy()` factories.
 *
 * User-saved presets live alongside built-ins on disk (one Profile JSON per
 * file under `~/.local/share/plasmazones/profiles/`); the AnimationsPage
 * controller exposes them via `userPresets()` in Phase 5. This singleton
 * holds only the immutable built-ins.
 */
QtObject {
    // ── Timing mode constants ─────────────────────────────────────
    readonly property int timingModeEasing: 0
    readonly property int timingModeSpring: 1
    // ── Easing curve presets (style + direction matrix) ─────────────
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
    // ── Quick easing presets (out-direction "ready to use" list) ────
    // Curve strings are stable wire format consumed by both QML
    // EasingCurve.js and the C++ CurveRegistry.
    readonly property var quickPresets: [{
        "label": i18n("Standard (Cubic)"),
        "curve": "0.33,1.00,0.68,1.00"
    }, {
        "label": i18n("Gentle (Sine)"),
        "curve": "0.61,1.00,0.88,1.00"
    }, {
        "label": i18n("Smooth (Quad)"),
        "curve": "0.50,1.00,0.89,1.00"
    }, {
        "label": i18n("Snappy (Quart)"),
        "curve": "0.25,1.00,0.50,1.00"
    }, {
        "label": i18n("Sharp (Quint)"),
        "curve": "0.23,1.00,0.32,1.00"
    }, {
        "label": i18n("Aggressive (Expo)"),
        "curve": "0.16,1.00,0.30,1.00"
    }, {
        "label": i18n("Overshoot (Back)"),
        "curve": "0.18,0.89,0.32,1.28"
    }, {
        "label": i18n("Elastic"),
        "curve": "elastic-out:1.00,0.30"
    }, {
        "label": i18n("Bounce"),
        "curve": "bounce-out:1.00,3"
    }]
    // ── Spring presets — match C++ Spring::{snappy,smooth,bouncy} ───
    // Values pinned in `libs/phosphor-animation/src/spring.cpp`.
    readonly property var springPresets: [{
        "label": i18n("Snappy"),
        "omega": 12,
        "zeta": 0.8
    }, {
        "label": i18n("Smooth"),
        "omega": 8,
        "zeta": 1
    }, {
        "label": i18n("Bouncy"),
        "omega": 10,
        "zeta": 0.5
    }]

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

    /// Find quick preset index for a curve string (returns -1 if no match)
    function quickPresetIndex(curve) {
        for (var i = 0; i < quickPresets.length; i++) {
            if (quickPresets[i].curve === curve)
                return i;

        }
        return -1;
    }

    /// Find spring preset index for given (omega, zeta). Returns -1 if no
    /// match; loose tolerances match the slider step size.
    function springPresetIndex(omega, zeta) {
        for (var i = 0; i < springPresets.length; i++) {
            var p = springPresets[i];
            if (Math.abs(p.omega - omega) < 0.1 && Math.abs(p.zeta - zeta) < 0.01)
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
