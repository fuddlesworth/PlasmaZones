// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One parameter editor row (props in, signals out).
 *
 * Handles every parameter type (float / number / int / bool / enum /
 * color / image) with visibility-based switching — avoids the
 * Loader/onLoaded timing issues that bit the dialog-coupled predecessor.
 * `number` and `float` are interchangeable numeric types (shader effects
 * emit `float`, autotile algorithms emit `number`).
 *
 * The host owns the parameter map. Bind `paramData` to the parameter
 * schema, `currentValues` to the live value map, and react to
 * `valueChanged(id, value)`. Color and image picking are delegated to the
 * host via `requestColorPicker` / `requestImagePicker` so dialog ownership
 * stays at the parent level (avoids platform-FileDialog use-after-free
 * when the row is destroyed mid-event).
 *
 * Required:
 *   - `paramData`: var — `{ id, name, type, default?, min?, max?, step?,
 *      description?, group?, enumOptions? }` (canonical shape, matches
 *      ShaderRegistry, AnimationsPageController, and the ActionRow
 *      algorithm-param adapter output)
 *   - `currentValues`: var — full parameter map; tracking the whole map
 *      keeps reactivity consistent across multi-param resets (preset
 *      load, randomize) without per-row rebinds. **The host must
 *      reassign the property when a value changes**
 *      (`currentValues = Object.assign({}, currentValues, {id: v})`); a
 *      silent in-place mutation (`currentValues[id] = v`) doesn't fire
 *      QML's property-change signal and the row will appear stale.
 *
 * Optional:
 *   - `lockedParams`: var — `{ paramId: bool }` lock map
 *   - `enableLocking`: bool — show per-row lock toggle
 *   - `enableImage`: bool — render image-picker controls for image params
 *   - `sliderValueLabelWidth`, `colorButtonSize`, `colorLabelWidth`:
 *      visual size hints; defaults match the editor's dialog.
 */
Item {
    id: paramDelegate

    required property var paramData
    required property var currentValues
    property var lockedParams: ({})
    property bool enableLocking: true
    /// Show the per-row "randomize this one" button (right of the lock
    /// toggle). Rolls a single value within the param's schema range and
    /// emits it through `randomizeRequested`. Disabled while the param is
    /// locked; hidden for `image` params (no sensible random image).
    property bool enableRandomize: true
    property bool enableImage: true
    /// Compact mode: slider/spinbox/swatch use fixed widths matching the
    /// settings-app `SettingsSlider` aesthetic (16gu slider, 3gu value
    /// label, 2gu swatch). When false (default), controls fill the
    /// available row width — the editor dialog's wide-slider layout.
    property bool compact: false
    property int sliderValueLabelWidth: compact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 4
    /// Fixed width for the slider control in compact mode. Ignored when
    /// `compact` is false — non-compact rows let the slider fill width.
    property int sliderControlWidth: Kirigami.Units.gridUnit * 16
    property int colorButtonSize: compact ? Kirigami.Units.gridUnit * 2 : Kirigami.Units.gridUnit * 3
    // Sized for the 9-char #AARRGGBB hex the swatch label shows (alpha-first).
    property int colorLabelWidth: compact ? Kirigami.Units.gridUnit * 6 : Kirigami.Units.gridUnit * 8
    readonly property string paramType: paramData ? (paramData.type || "") : ""
    /// `number` and `float` render identically (a slider + value readout);
    /// shader effects declare `float`, autotile algorithms declare `number`.
    readonly property bool _isNumber: paramType === "float" || paramType === "number"
    readonly property bool isSvgImage: paramType === "image" && imagePickerButton.currentPath.length > 0 && (imagePickerButton.currentPath.toLowerCase().endsWith(".svg") || imagePickerButton.currentPath.toLowerCase().endsWith(".svgz"))

    signal valueChanged(string paramId, var value)
    signal lockToggled(string paramId, bool locked)
    /// Emitted when the per-row randomize button is clicked; the host
    /// (ParameterEditor) rolls a fresh value for this one param.
    signal randomizeRequested(string paramId)
    signal requestColorPicker(string paramId, string paramName, color current)
    signal requestImagePicker(string paramId)

    // The bindings below call `_value` / `_isLocked` from inside their
    // expression. QML's QQmlPropertyCapture tracks property reads through
    // function calls, so reading `currentValues` / `lockedParams` inside
    // these helpers is enough to register them as binding dependencies —
    // no separate `_valuesRef` / `_locksRef` proxy properties needed.
    function _value(fallback) {
        if (!paramData || !currentValues || currentValues[paramData.id] === undefined)
            return fallback;

        return currentValues[paramData.id];
    }

    function _isLocked() {
        if (!paramData || !lockedParams)
            return false;

        return lockedParams[paramData.id] === true;
    }

    /// Coerce a value (typically from JSON via QVariantMap) to a finite
    /// number, falling back when the input is missing, non-numeric, or
    /// produces NaN. Slider.from/to and SpinBox.from/to silently misbehave
    /// (clamp to 0 in some Qt builds, no-op in others) when handed NaN, so
    /// validate at the row-property boundary.
    function _numberOr(value, fallback) {
        if (value === undefined || value === null)
            return fallback;

        var n = Number(value);
        return isFinite(n) ? n : fallback;
    }

    implicitHeight: contentLayout.implicitHeight
    implicitWidth: contentLayout.implicitWidth

    RowLayout {
        id: contentLayout

        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        // ── FLOAT ────────────────────────────────────────────────────
        Slider {
            id: floatSlider

            visible: paramDelegate._isNumber
            Layout.fillWidth: !paramDelegate.compact
            Layout.preferredWidth: paramDelegate.compact ? paramDelegate.sliderControlWidth : -1
            Accessible.name: paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : ""
            // `_numberOr` rejects NaN / Infinity / non-numeric strings so the
            // slider always receives finite bounds.
            from: paramDelegate.paramData ? paramDelegate._numberOr(paramDelegate.paramData.min, 0) : 0
            to: paramDelegate.paramData ? paramDelegate._numberOr(paramDelegate.paramData.max, 1) : 1
            stepSize: paramDelegate.paramData ? paramDelegate._numberOr(paramDelegate.paramData.step, 0.01) : 0.01
            ToolTip.text: paramDelegate.paramData ? (paramDelegate.paramData.description || "") : ""
            ToolTip.visible: hovered && paramDelegate.paramData && paramDelegate.paramData.description !== undefined && paramDelegate.paramData.description !== ""
            ToolTip.delay: Kirigami.Units.toolTipDelay
            onMoved: {
                if (paramDelegate.paramData)
                    paramDelegate.valueChanged(paramDelegate.paramData.id, value);
            }

            Binding on value {
                value: {
                    if (!paramDelegate.paramData)
                        return 0.5;

                    var fallback = paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : 0.5;
                    var v = paramDelegate._value(fallback);
                    // _numberOr rejects NaN/Infinity/non-numeric so a legitimate
                    // stored 0 survives (a `Number(x) || 0.5` coercion would
                    // wrongly turn 0 into 0.5).
                    return paramDelegate._numberOr(v, paramDelegate._numberOr(fallback, 0.5));
                }
                // Defer host-driven updates while the user is interacting,
                // INCLUDING keyboard arrow adjustments — `pressed` is only
                // true for mouse/touch drags, so `activeFocus` covers the
                // arrow-key path.
                when: !floatSlider.pressed && !floatSlider.activeFocus
                restoreMode: Binding.RestoreNone
            }
        }

        Label {
            visible: paramDelegate._isNumber
            text: floatSlider.value.toFixed(2)
            Layout.preferredWidth: paramDelegate.sliderValueLabelWidth
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.fixedWidthFont
        }

        // ── INT ──────────────────────────────────────────────────────
        SpinBox {
            id: intSpinBox

            visible: paramDelegate.paramType === "int"
            Accessible.name: paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : ""
            // SpinBox.from/to are integers — `_numberOr` validates as a
            // finite number first, then we round. The schema's optional
            // `step` is honoured the same way the float slider honours it
            // (minimum 1 — a fractional step is meaningless for an int).
            from: paramDelegate.paramData ? Math.round(paramDelegate._numberOr(paramDelegate.paramData.min, 0)) : 0
            to: paramDelegate.paramData ? Math.round(paramDelegate._numberOr(paramDelegate.paramData.max, 100)) : 100
            stepSize: paramDelegate.paramData ? Math.max(1, Math.round(paramDelegate._numberOr(paramDelegate.paramData.step, 1))) : 1
            ToolTip.text: paramDelegate.paramData ? (paramDelegate.paramData.description || "") : ""
            ToolTip.visible: hovered && paramDelegate.paramData && paramDelegate.paramData.description !== undefined && paramDelegate.paramData.description !== ""
            ToolTip.delay: Kirigami.Units.toolTipDelay
            onValueModified: {
                if (paramDelegate.paramData)
                    paramDelegate.valueChanged(paramDelegate.paramData.id, value);
            }

            Binding on value {
                value: {
                    if (!paramDelegate.paramData)
                        return 0;

                    var fallback = paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : 0;
                    var v = paramDelegate._value(fallback);
                    var num = parseInt(v, 10);
                    return isNaN(num) ? (parseInt(fallback, 10) || 0) : num;
                }
                when: !intSpinBox.activeFocus
                restoreMode: Binding.RestoreNone
            }
        }

        Item {
            visible: paramDelegate.paramType === "int" && !paramDelegate.compact
            Layout.fillWidth: true
        }

        // ── BOOL ─────────────────────────────────────────────────────
        CheckBox {
            id: boolCheckBox

            visible: paramDelegate.paramType === "bool"
            Accessible.name: paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : ""
            // In compact mode the description is already rendered in the
            // row's left-hand label column, so repeating it as the checkbox
            // label would duplicate it. Only the wide layout (name-only left
            // label) needs the checkbox to carry the description.
            text: (paramDelegate.compact || !paramDelegate.paramData) ? "" : (paramDelegate.paramData.description || "")
            onToggled: {
                if (paramDelegate.paramData)
                    paramDelegate.valueChanged(paramDelegate.paramData.id, checked);
            }

            Binding on checked {
                value: {
                    if (!paramDelegate.paramData)
                        return false;

                    var fallback = paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : false;
                    return Boolean(paramDelegate._value(fallback));
                }
                when: !boolCheckBox.activeFocus
                restoreMode: Binding.RestoreNone
            }
        }

        Item {
            visible: paramDelegate.paramType === "bool" && !paramDelegate.compact
            Layout.fillWidth: true
        }

        // ── ENUM ─────────────────────────────────────────────────────
        ComboBox {
            id: enumCombo

            visible: paramDelegate.paramType === "enum"
            // Fill the row in wide mode; take the fixed slider-column width in
            // compact mode so it lines up with the numeric rows above/below.
            Layout.fillWidth: !paramDelegate.compact
            Layout.preferredWidth: paramDelegate.compact ? paramDelegate.sliderControlWidth : -1
            Accessible.name: paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : ""
            model: paramDelegate.paramData ? (paramDelegate.paramData.enumOptions || []) : []
            // -1 (not 0) when the stored value is out of vocabulary (e.g. the
            // schema changed and dropped an option) so the combo doesn't
            // silently display a different option than the value holds.
            currentIndex: {
                if (!paramDelegate.paramData)
                    return -1;

                var opts = paramDelegate.paramData.enumOptions || [];
                var fallback = paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : (opts.length > 0 ? opts[0] : "");
                return opts.indexOf(paramDelegate._value(fallback));
            }
            // On an out-of-vocab miss (currentIndex -1) surface the stored raw
            // value rather than a blank combo.
            displayText: {
                if (currentIndex >= 0)
                    return currentText;

                var v = paramDelegate.paramData ? paramDelegate._value(undefined) : undefined;
                return v !== undefined ? String(v) : "";
            }
            ToolTip.text: paramDelegate.paramData ? (paramDelegate.paramData.description || "") : ""
            ToolTip.visible: hovered && paramDelegate.paramData && paramDelegate.paramData.description !== undefined && paramDelegate.paramData.description !== ""
            ToolTip.delay: Kirigami.Units.toolTipDelay
            onActivated: {
                if (paramDelegate.paramData)
                    paramDelegate.valueChanged(paramDelegate.paramData.id, currentText);
            }
        }

        // ── COLOR ────────────────────────────────────────────────────
        // Use AbstractButton for first-class keyboard activation: it
        // accepts Tab focus, fires `clicked` on Space/Return, and the
        // surrounding Rectangle visual is a custom `contentItem`. A
        // raw Rectangle+MouseArea (the previous shape) was unreachable
        // from a screen reader and inactivable from a keyboard.
        AbstractButton {
            id: colorSwatch

            // Theme-derived neutral fallback for malformed/missing color
            // values. Avoids hardcoding `"#ffffff"` (CLAUDE.md: never
            // hardcode color hex codes); the actual displayed colour is
            // whatever the metadata's `default` field declares.
            readonly property color _missingColorFallback: Kirigami.Theme.backgroundColor
            readonly property color currentColor: {
                if (!paramDelegate.paramData)
                    return _missingColorFallback;

                var fallback = (typeof paramDelegate.paramData.default === "string" && paramDelegate.paramData.default.length > 0) ? paramDelegate.paramData.default : _missingColorFallback;
                if (paramDelegate.paramType !== "color")
                    return fallback;

                var colorStr = paramDelegate._value(fallback);
                if (typeof colorStr !== "string" || colorStr.length === 0 || colorStr.charAt(0) !== "#")
                    return fallback;

                // Qt 6 doesn't guarantee .valid on Qt.color() results; validate hex format
                // and accept #RGB / #RGBA / #RRGGBB / #RRGGBBAA.
                var hexRe = /^#([0-9A-Fa-f]{3,4}|[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$/;
                return hexRe.test(colorStr) ? colorStr : fallback;
            }

            visible: paramDelegate.paramType === "color"
            implicitWidth: paramDelegate.colorButtonSize
            implicitHeight: paramDelegate.colorButtonSize
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button", "Choose %1 color", paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id) : "")
            onClicked: {
                if (!paramDelegate.paramData)
                    return;

                paramDelegate.requestColorPicker(paramDelegate.paramData.id, paramDelegate.paramData.name || paramDelegate.paramData.id, colorSwatch.currentColor);
            }

            contentItem: Rectangle {
                radius: Kirigami.Units.smallSpacing
                color: colorSwatch.currentColor
                // 1 device-independent px (2 when focused) — Qt scales DIPs
                // to physical pixels itself; multiplying by devicePixelRatio
                // here would double-scale into a thicker, not crisper, line.
                border.width: colorSwatch.activeFocus ? 2 : 1
                border.color: colorSwatch.activeFocus ? Kirigami.Theme.focusColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.NoButton // pass clicks to AbstractButton
                }
            }

            // suppress default Button frame
            background: Item {}
        }

        Label {
            visible: paramDelegate.paramType === "color"
            // #AARRGGBB (alpha-first), not currentColor.toString() — the latter
            // emits #RRGGBB and hides the alpha channel this param carries.
            // Mirrors ColorSwatchRow's display and the stored wire form.
            text: {
                function pad(v) {
                    return Math.round(v * 255).toString(16).padStart(2, '0');
                }
                var c = colorSwatch.currentColor;
                return ("#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)).toUpperCase();
            }
            Layout.preferredWidth: paramDelegate.colorLabelWidth
            font: Kirigami.Theme.fixedWidthFont
            opacity: 0.7
        }

        Item {
            visible: paramDelegate.paramType === "color" && !paramDelegate.compact
            Layout.fillWidth: true
        }

        // ── IMAGE ────────────────────────────────────────────────────
        Button {
            id: imagePickerButton

            property string currentPath: {
                if (!paramDelegate.paramData)
                    return "";

                var fallback = paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : "";
                return String(paramDelegate._value(fallback) || "");
            }

            visible: paramDelegate.enableImage && paramDelegate.paramType === "image"
            Layout.fillWidth: true
            Accessible.name: i18nc("@action:button", "Choose image for %1", paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : "")
            text: {
                if (!currentPath || currentPath.length === 0)
                    return i18nc("@action:button", "Choose Image…");

                var lastSlash = currentPath.lastIndexOf("/");
                return lastSlash >= 0 ? currentPath.substring(lastSlash + 1) : currentPath;
            }
            onClicked: {
                if (paramDelegate.paramData)
                    paramDelegate.requestImagePicker(paramDelegate.paramData.id);
            }
        }

        ToolButton {
            visible: paramDelegate.enableImage && paramDelegate.paramType === "image" && imagePickerButton.currentPath.length > 0
            icon.name: "edit-clear"
            Accessible.name: i18nc("@action:button", "Clear image")
            ToolTip.text: i18nc("@info:tooltip", "Clear image")
            onClicked: {
                if (paramDelegate.paramData)
                    paramDelegate.valueChanged(paramDelegate.paramData.id, "");
            }
        }

        Label {
            visible: paramDelegate.enableImage && paramDelegate.isSvgImage
            text: i18nc("@label:spinbox", "Size:")
            opacity: 0.7
        }

        SpinBox {
            id: svgSizeSpinBox

            visible: paramDelegate.enableImage && paramDelegate.isSvgImage
            Accessible.name: i18nc("@label SVG render resolution", "SVG size")
            from: 64
            to: 4096
            stepSize: 128
            editable: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 6
            ToolTip.text: i18nc("@info:tooltip", "SVG render resolution (longest side in pixels)")
            ToolTip.visible: hovered
            ToolTip.delay: Kirigami.Units.toolTipDelay
            onValueModified: {
                if (paramDelegate.paramData)
                    paramDelegate.valueChanged(paramDelegate.paramData.id + "_svgSize", value);
            }

            // Mirror the float/int/bool reactivity pattern: Binding-on-value
            // gated by `!activeFocus` so a host-side update (e.g. preset
            // load) doesn't yank the value while the user is typing.
            Binding on value {
                value: {
                    if (!paramDelegate.paramData || !paramDelegate.currentValues)
                        return 1024;

                    var v = paramDelegate.currentValues[paramDelegate.paramData.id + "_svgSize"];
                    // _numberOr for pattern-consistency with the float/int rows
                    // (a `Number(v) || 1024` coercion misreads falsy values).
                    // The spinbox's own `from: 64` floor means a stored 0 would
                    // display as 64 regardless.
                    return paramDelegate._numberOr(v, 1024);
                }
                when: !svgSizeSpinBox.activeFocus
                restoreMode: Binding.RestoreNone
            }
        }

        Item {
            visible: paramDelegate.enableImage && paramDelegate.paramType === "image" && !paramDelegate.compact
            Layout.fillWidth: true
        }

        // ── LOCK TOGGLE ──────────────────────────────────────────────
        ToolButton {
            readonly property bool isLocked: paramDelegate._isLocked()

            visible: paramDelegate.enableLocking
            icon.name: isLocked ? "object-locked" : "object-unlocked"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            opacity: isLocked ? 1 : 0.4
            display: ToolButton.IconOnly
            Accessible.name: isLocked ? i18nc("@action:button", "Unlock %1", paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : "") : i18nc("@action:button", "Lock %1", paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : "")
            ToolTip.text: isLocked ? i18nc("@info:tooltip", "Won't be randomized") : i18nc("@info:tooltip", "Will be randomized")
            ToolTip.visible: hovered
            ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: {
                if (paramDelegate.paramData)
                    paramDelegate.lockToggled(paramDelegate.paramData.id, !isLocked);
            }
        }

        // ── RANDOMIZE (per-parameter) ────────────────────────────────
        ToolButton {
            visible: paramDelegate.enableRandomize && paramDelegate.paramType !== "image"
            // Locked params are excluded from randomize (matches the lock's
            // "won't be randomized" contract); grey the button rather than
            // hide it so the row layout stays stable across lock toggles.
            enabled: !paramDelegate._isLocked()
            icon.name: "roll"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            display: ToolButton.IconOnly
            Accessible.name: i18nc("@action:button", "Randomize %1", paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id || "") : "")
            ToolTip.text: i18nc("@info:tooltip", "Randomize this parameter")
            ToolTip.visible: hovered
            ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: {
                if (paramDelegate.paramData)
                    paramDelegate.randomizeRequested(paramDelegate.paramData.id);
            }
        }
    }
}
