// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Preview sketch — Animations App Rules page (Option B for #405-followup).
 *
 * Mirrors the existing `AssignmentsAppRulesPage` shape: a list of rules
 * keyed on a window-class pattern, scoped to one of the animation event
 * paths (`window.open`, `window.close`, `window.minimize`, etc.), each
 * carrying an optional shader override + per-event timing override.
 *
 * STATUS: visual sketch only. Backed by an in-memory `_previewRules`
 * array, NOT the real `Settings::shaderProfileTree`. No controller
 * methods are called. Wire to a real bridge before merging.
 *
 * The page deliberately reuses the same row layout as
 * `AppRulesCard.qml` so the muscle-memory carries across; the
 * differences are:
 *
 *   - "Layout:" combo replaced with "Event:" combo (selects which
 *     window-lifecycle event the rules apply to).
 *   - "Zone:" SpinBox replaced with a `ShaderPickerButton` (which
 *     animation shader fires for the matched window).
 *   - The hint at the bottom calls out that rules override the
 *     `Animations → Windows` per-event defaults for matching
 *     windows; non-matched windows fall through to the per-event
 *     defaults.
 *
 * Open question for the v1 implementation:
 *
 *   - Storage: extend `Settings::shaderProfileTree` with per-rule
 *     overlay entries keyed by `(eventPath, classPattern)` so the
 *     daemon's `tryBeginShaderForEvent` can resolve them by walking
 *     `pattern → event → parent → global` (mirrors the
 *     ShaderProfileTree::resolve cascade we already have for events).
 *   - Engine: kwin-effect's `windowAdded` handler reads
 *     `EffectWindow::windowClass()` and looks up the rule before
 *     falling through to the global event resolution.
 *
 * Both decisions are deferred — this file just shows the UI.
 */
SettingsFlickable {
    id: root

    /// Dummy rule data for the sketch. Real implementation pulls
    /// from `settingsController.animationsPage.appRulesForEvent(path)`.
    property var _previewRules: [{
        "pattern": "firefox",
        "eventPath": "window.open",
        "shaderEffectId": "popin",
        "shaderName": "Pop-in"
    }, {
        "pattern": "org.kde.dolphin",
        "eventPath": "window.open",
        "shaderEffectId": "fly-in",
        "shaderName": "Fly-In"
    }, {
        "pattern": "code",
        "eventPath": "window.close",
        "shaderEffectId": "dissolve",
        "shaderName": "Dissolve"
    }, {
        "pattern": "Spotify",
        "eventPath": "window.minimize",
        "shaderEffectId": "tv",
        "shaderName": "TV"
    }]
    /// Dummy event-path catalogue. Real implementation pulls from
    /// `PhosphorAnimation::ProfilePaths::Window*` constants.
    readonly property var _previewEvents: [{
        "id": "window.open",
        "label": i18n("Window — Open")
    }, {
        "id": "window.close",
        "label": i18n("Window — Close")
    }, {
        "id": "window.minimize",
        "label": i18n("Window — Minimize")
    }, {
        "id": "window.maximize",
        "label": i18n("Window — Maximize")
    }, {
        "id": "window.move",
        "label": i18n("Window — Move")
    }, {
        "id": "window.resize",
        "label": i18n("Window — Resize")
    }, {
        "id": "window.focus",
        "label": i18n("Window — Focus")
    }]
    /// Dummy shader catalogue. Real implementation pulls from
    /// `settingsController.animationsPage.availableShaderEffects()`.
    readonly property var _previewShaders: [{
        "id": "",
        "name": i18n("None"),
        "category": ""
    }, {
        "id": "popin",
        "name": "Pop-in",
        "category": "Scale"
    }, {
        "id": "fly-in",
        "name": "Fly-In",
        "category": "Slide"
    }, {
        "id": "dissolve",
        "name": "Dissolve",
        "category": "Fade"
    }, {
        "id": "morph",
        "name": "Morph",
        "category": "Distortion"
    }, {
        "id": "tv",
        "name": "TV",
        "category": "Slide"
    }]

    function _filteredRules() {
        var picked = eventCombo.currentValue || "window.open";
        return _previewRules.filter(function(r) {
            return r.eventPath === picked;
        });
    }

    contentHeight: mainCol.implicitHeight

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Override the global per-event animation shader for specific windows. Useful when one app should dissolve while everything else uses the default open animation.")
            visible: true
        }

        SettingsCard {
            id: rulesCard

            Layout.fillWidth: true
            headerText: i18n("Per-Window Animation Rules")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Event-path selector — analogous to the Layout
                // selector in AppRulesCard. Each event path has its
                // own ordered rule list.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Event:")
                    }

                    ComboBox {
                        id: eventCombo

                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "id"
                        model: root._previewEvents
                        currentIndex: 0
                        Accessible.name: i18n("Animation event for window rules")
                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // Add-rule row.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: patternField

                        Layout.fillWidth: true
                        placeholderText: i18n("Window class pattern (e.g., firefox, org.kde.dolphin)")
                        Accessible.name: i18n("Window class pattern")
                    }

                    Label {
                        text: i18n("Shader:")
                    }

                    PZCommon.ShaderPickerButton {
                        id: shaderPicker

                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        shaders: root._previewShaders
                        currentShaderId: ""
                        noneShaderId: ""
                        includeNoneEntry: true
                        placeholderText: i18nc("@action:button", "Pick…")
                    }

                    Button {
                        text: i18n("Add")
                        icon.name: "list-add"
                        enabled: patternField.text.length > 0 && shaderPicker.currentShaderId.length > 0
                        ToolTip.text: i18n("Add per-window animation rule")
                        ToolTip.visible: hovered
                    }

                    ToolButton {
                        icon.name: "crosshairs"
                        ToolTip.text: i18n("Pick from running windows")
                        ToolTip.visible: hovered
                        Accessible.name: i18n("Pick from running windows")
                    }

                }

                // Rules list — filtered to the selected event path.
                ListView {
                    id: rulesListView

                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(contentHeight, Kirigami.Units.gridUnit * 5)
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 5
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    model: root._filteredRules()
                    interactive: false

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        visible: rulesListView.count === 0
                        text: i18n("No rules for this event")
                        explanation: i18n("Add rules above to override the default animation for specific windows on this event.")
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index

                        width: ListView.view.width

                        contentItem: RowLayout {
                            Kirigami.Icon {
                                source: "application-x-executable"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Label {
                                text: modelData.pattern
                                Layout.alignment: Qt.AlignVCenter
                                elide: Text.ElideRight
                                Layout.maximumWidth: Math.min(implicitWidth, parent.width * 0.4)
                            }

                            Label {
                                text: i18n("→ %1", modelData.shaderName)
                                opacity: 0.7
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ToolButton {
                                icon.name: "edit-delete"
                                Accessible.name: i18n("Remove rule for %1", modelData.pattern)
                            }

                        }

                    }

                }

                Label {
                    text: i18n("Rules are checked in order. The first match wins. Non-matched windows fall through to the per-event defaults configured under Animations → Windows.")
                    font.italic: true
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                }

            }

        }

    }

}
