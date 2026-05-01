// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Inline parameter editor for an animation shader effect.
 *
 * Embedded inside AnimationEventCard when the user has a shader assigned
 * to the event. Reads parameter metadata from
 * `AnimationsPageController.shaderParameters(effectId)` and binds each
 * row's control by `type` (float / int / bool / color).
 *
 * Pending edits are buffered locally; commits happen on slider release /
 * combo activation via `paramsChanged(QVariantMap)`. The hosting
 * AnimationEventCard merges them into the per-event ShaderProfile and
 * routes through `setShaderOverride`.
 *
 * Required properties:
 *   - effectId: string — empty hides the editor
 *   - currentParams: var — { paramId: value, ... }
 */
ColumnLayout {
    // ── Per-type row components ──────────────────────────────────────
    // SettingsRow's `default property alias content: controlContainer.data`
    // re-parents children into an inner Row, so `parent` inside the
    // control resolves to that inner Row (which has no `value` /
    // `paramInfo` properties) — not to the SettingsRow declaring them.
    // Every delegate below references `row` / `row.value` / `row.paramInfo`
    // by id instead, which sidesteps the silent re-parent and stops the
    // 15× "Unable to assign [undefined] to double" warnings the
    // `parent.value` form was producing on every shader-effect load.

    id: root

    required property string effectId
    required property var currentParams
    readonly property var _paramSchema: effectId.length > 0 ? settingsController.animationsPage.shaderParameters(effectId) : []

    signal paramsChanged(var newParams)

    function paramValue(paramId, fallback) {
        if (currentParams && currentParams[paramId] !== undefined)
            return currentParams[paramId];

        return fallback;
    }

    /// Type-appropriate default when a pack omits `defaultValue`. Shared
    /// between the load path (`paramInitialValue`) and the explicit
    /// "Reset to defaults" button — without the shared fallback,
    /// resetToDefaults would emit `{paramId: undefined}` and persist a
    /// stringified-undefined into the on-disk profile (the animation
    /// runtime tolerates the missing key at read time, but the wire
    /// format gets stale entries that never resolve cleanly).
    function _typeDefault(paramInfo) {
        switch (paramInfo.type) {
        case "bool":
            return false;
        case "color":
            return "#ffffffff";
        case "int":
        case "float":
        default:
            return 0;
        }
    }

    /// Type-safe default for a parameter row. Animation packs frequently
    /// declare parameters without an explicit `default` value in
    /// metadata.json, which left `paramValue` returning undefined and
    /// the row's typed `value` Q_PROPERTY tripping
    /// "Unable to assign [undefined] to double" on every Loader load
    /// (40+ warnings per shader assignment in journalctl). Falling back
    /// to a type-appropriate zero/false/transparent here keeps the
    /// editor showing predictable initial state regardless of how
    /// thorough the pack author was — and matches the on-the-wire
    /// shape the animation runtime would synthesize anyway.
    function paramInitialValue(modelData) {
        var v = paramValue(modelData.id, modelData.defaultValue);
        if (v !== undefined)
            return v;

        return _typeDefault(modelData);
    }

    function commit(paramId, value) {
        var next = Object.assign({
        }, currentParams || {
        });
        next[paramId] = value;
        root.paramsChanged(next);
    }

    function resetToDefaults() {
        var next = {
        };
        for (var i = 0; i < _paramSchema.length; i++) {
            var p = _paramSchema[i];
            // Use the same type-aware fallback the load path uses so
            // packs that omit `defaultValue` round-trip cleanly through
            // a reset (instead of writing `undefined` into the QVariantMap
            // that flows through to the on-disk profile).
            next[p.id] = (p.defaultValue !== undefined) ? p.defaultValue : _typeDefault(p);
        }
        // Push the defaults into every loaded row imperatively BEFORE
        // emitting paramsChanged so the visible UI snaps to the defaults
        // immediately. We can't rely on a declarative Binding to row.value
        // tracking currentParams: that would re-evaluate during a slider
        // drag (the row's own commit() round-trips through the host and
        // back into currentParams synchronously) and snap the slider's
        // thumb to the previous tick's value.
        for (var j = 0; j < paramRepeater.count; ++j) {
            var loader = paramRepeater.itemAt(j);
            if (loader && loader.item)
                loader.item.value = next[loader.modelData.id];

        }
        root.paramsChanged(next);
    }

    spacing: Kirigami.Units.smallSpacing
    visible: effectId.length > 0 && _paramSchema.length > 0

    Repeater {
        id: paramRepeater

        model: root._paramSchema

        delegate: Loader {
            id: paramLoader

            required property var modelData

            Layout.fillWidth: true
            sourceComponent: {
                switch (modelData.type) {
                case "float":
                    return floatRow;
                case "int":
                    return intRow;
                case "bool":
                    return boolRow;
                case "color":
                    return colorRow;
                default:
                    // Unknown type: render a labelled placeholder so a
                    // shader pack shipping a `vec2` (or any unsupported
                    // type) is visible instead of silently absent.
                    return unsupportedRow;
                }
            }
            // Imperative seed: onLoaded fires once when the Loader's
            // sourceComponent resolves; modelData/currentParams updates do
            // NOT re-fire it. That's intentional — we want the row to keep
            // the user's in-flight value during a slider drag (commit()
            // round-trips through the host and back into currentParams
            // synchronously, so a declarative Binding here would overwrite
            // the cursor mid-stroke). Reset-to-defaults uses an explicit
            // imperative re-drive in resetToDefaults() above.
            onLoaded: {
                if (item) {
                    item.paramInfo = modelData;
                    item.value = root.paramInitialValue(modelData);
                }
            }
        }

    }

    SettingsSeparator {
    }

    Button {
        Layout.alignment: Qt.AlignRight
        text: i18n("Reset to defaults")
        icon.name: "edit-undo"
        onClicked: root.resetToDefaults()
    }

    Component {
        id: floatRow

        SettingsRow {
            id: row

            property var paramInfo: ({
            })
            property real value: 0
            // Validate min/max against NaN/Infinity from malformed metadata.
            // A NaN bound silently propagates into Qt's slider where it
            // produces undefined behaviour; pinning to a sane fallback is
            // the correct boundary-input handling.
            readonly property real _from: (paramInfo.minValue !== undefined && isFinite(paramInfo.minValue)) ? paramInfo.minValue : 0
            readonly property real _to: (paramInfo.maxValue !== undefined && isFinite(paramInfo.maxValue) && paramInfo.maxValue > _from) ? paramInfo.maxValue : (_from + 1)

            title: paramInfo.name || ""

            SettingsSlider {
                Accessible.name: row.paramInfo.name || ""
                from: row._from
                to: row._to
                // Honour a metadata-declared `step` if present, otherwise
                // derive a useful default from the range (200 ticks across
                // the full sweep), capped at 0.001 so very narrow ranges
                // still produce non-zero stepSize.
                stepSize: (row.paramInfo.step !== undefined && isFinite(row.paramInfo.step) && row.paramInfo.step > 0) ? row.paramInfo.step : Math.max(0.001, (row._to - row._from) / 200)
                value: row.value
                formatValue: function(v) {
                    return v.toFixed(2);
                }
                onMoved: function(v) {
                    root.commit(row.paramInfo.id, v);
                }
            }

        }

    }

    Component {
        id: intRow

        SettingsRow {
            id: row

            property var paramInfo: ({
            })
            property int value: 0
            readonly property int _from: (paramInfo.minValue !== undefined && isFinite(paramInfo.minValue)) ? Math.round(paramInfo.minValue) : 0
            readonly property int _to: (paramInfo.maxValue !== undefined && isFinite(paramInfo.maxValue) && paramInfo.maxValue > _from) ? Math.round(paramInfo.maxValue) : (_from + 100)

            title: paramInfo.name || ""

            SettingsSlider {
                Accessible.name: row.paramInfo.name || ""
                from: row._from
                to: row._to
                stepSize: 1
                value: row.value
                onMoved: function(v) {
                    root.commit(row.paramInfo.id, Math.round(v));
                }
            }

        }

    }

    Component {
        id: boolRow

        SettingsRow {
            id: row

            property var paramInfo: ({
            })
            property bool value: false

            title: paramInfo.name || ""

            SettingsSwitch {
                checked: row.value
                accessibleName: row.paramInfo.name || ""
                onToggled: function(v) {
                    root.commit(row.paramInfo.id, v);
                }
            }

        }

    }

    Component {
        id: colorRow

        SettingsRow {
            id: row

            property var paramInfo: ({
            })
            property var value: "#ffffffff"

            title: paramInfo.name || ""

            ColorButton {
                color: row.value
                onClicked: {
                    colorDialog.selectedColor = row.value;
                    colorDialog.open();
                }
            }

            ColorDialog {
                id: colorDialog

                title: row.paramInfo.name || i18n("Pick color")
                onAccepted: {
                    row.value = selectedColor;
                    root.commit(row.paramInfo.id, selectedColor.toString());
                }
            }

        }

    }

    Component {
        // Rendered when a shader pack declares an unsupported uniform type
        // (e.g. `vec2`). Gives the user visibility that the parameter
        // exists rather than silently dropping it from the editor — the
        // value cannot be edited here but the daemon still reads the
        // metadata-declared default.
        id: unsupportedRow

        SettingsRow {
            id: row

            property var paramInfo: ({
            })
            // Loader sets `value` via Binding above; declared so the
            // assignment doesn't trip a "no such property" warning.
            property var value

            title: paramInfo.name || ""

            Label {
                text: i18n("(unsupported type: %1)", row.paramInfo.type || "?")
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
            }

        }

    }

}
