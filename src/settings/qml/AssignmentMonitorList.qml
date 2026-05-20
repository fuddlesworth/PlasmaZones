// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Shared disable-toggles surface for per-mode Assignments pages.
 *
 * Renders the per-monitor (with optional per-desktop expander) and
 * per-activity ListViews that gate a single mode (snapping / tiling /
 * scrolling) on every screen × desktop × activity context. The page
 * supplies a settings bridge — any object exposing the SharedBridge
 * disable API (`isMonitorDisabled`, `setMonitorDisabled`, the per-desktop
 * and per-activity equivalents, plus the `disabledMonitorsTick` /
 * `disabledDesktopsTick` / `disabledActivitiesTick` bump counters).
 *
 * Switches bind directly to `!settingsBridge.isMonitorDisabled(...)`.
 * Each binding references the matching tick property so QML records it
 * as a dependency; when the SharedBridge `disabled*Changed` signal fires
 * and bumps the tick, the switch re-evaluates and tracks the new state.
 * That replaces the cached-property + Connections race the inline
 * pages used to have, where the initial signal-target wiring could
 * miss the first emission.
 *
 * The composed display label (connector + manufacturer + model) is
 * computed once per delegate and reused for both the visible Label and
 * the Switch's Accessible.name so screen readers announce the same
 * string a sighted user reads — see PR #493.
 */
ColumnLayout {
    id: root

    /** @brief Settings bridge driving the toggles (Snapping / Tiling / Scrolling). */
    required property var settingsBridge

    /** @brief Lower-case mode name used inside i18n strings ("snapping", "tiling", "scrolling"). */
    required property string modeName

    /** @brief Header for the Activities card body explaining the per-activity gates. */
    required property string activitiesDescription

    /** @brief Inline message shown when the activities service is unavailable. */
    required property string activitiesUnavailableMessage

    /// Compose the visible label for a monitor entry — connector name plus
    /// manufacturer/model when available, joined identically to the visible
    /// `Label` and the Switch's Accessible.name so screen readers announce
    /// the same string a sighted user reads. Single source of truth shared
    /// by the Monitors list (above) and the per-activity Monitors expander
    /// (below); previously each delegate inlined its own copy and the two
    /// drifted into subtly different fallback strings.
    function _composeMonitorLabel(modelData) {
        const name = modelData.name || i18n("Unknown Monitor");
        const mfr = modelData.manufacturer || "";
        const mdl = modelData.model || "";
        const parts = [mfr, mdl].filter(function (s) {
            return s !== "";
        });
        const info = parts.join(" ");
        return info ? name + " — " + info : name;
    }

    spacing: Kirigami.Units.largeSpacing

    // ── Monitors ──────────────────────────────────────────────────────
    SettingsCard {
        Layout.fillWidth: true
        headerText: i18n("Monitors")
        collapsible: true

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: (root.settingsBridge.screens && root.settingsBridge.screens.length > 0) ? contentHeight : Kirigami.Units.gridUnit * 4
                Layout.margins: Kirigami.Units.smallSpacing
                clip: true
                interactive: false
                model: root.settingsBridge.screens
                Accessible.name: i18n("Monitor list")
                Accessible.role: Accessible.List

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: parent.count === 0
                    text: i18n("No monitors detected")
                    explanation: i18n("Make sure the PlasmaZones daemon is running")
                }

                delegate: Item {
                    id: monitorDelegate

                    required property var modelData
                    required property int index
                    property string screenName: modelData.name || ""
                    // Composed display label — see root._composeMonitorLabel
                    // for the join recipe (centralised so the visible text
                    // and the Switch's Accessible.name always read the same
                    // string — screen readers should announce
                    // "LG ULTRAGEAR" not the raw "DP-1" connector id, per
                    // PR #493).
                    readonly property string composedDisplayName: root._composeMonitorLabel(modelData)
                    property bool expanded: false

                    width: ListView.view.width
                    // implicitHeight + both vertical margins — anchors.fill
                    // insets the content by anchors.margins on every side,
                    // so the delegate must reserve top + bottom.
                    height: monitorCol.implicitHeight + Kirigami.Units.smallSpacing * 2

                    ColumnLayout {
                        id: monitorCol

                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: "video-display"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: monitorDelegate.composedDisplayName
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        let info = monitorDelegate.modelData.resolution || "";
                                        if (monitorDelegate.modelData.isPrimary)
                                            info += (info ? " • " : "") + i18n("Primary");

                                        return info;
                                    }
                                    opacity: 0.7
                                    font: Kirigami.Theme.smallFont
                                    elide: Text.ElideRight
                                }
                            }

                            Switch {
                                id: monitorSwitch

                                // Direct binding: every tick bump in the
                                // bridge re-evaluates this expression so
                                // the switch tracks external mutations
                                // (e.g. another tab flipping the same
                                // gate) without the cached-property +
                                // Connections race the page used to have.
                                checked: {
                                    // Reference the tick so QML records
                                    // it as a binding dependency.
                                    void root.settingsBridge.disabledMonitorsTick;
                                    return !root.settingsBridge.isMonitorDisabled(monitorDelegate.screenName);
                                }
                                Accessible.name: i18nc("@accessible:label monitor enable toggle, %1 = mode (snapping/tiling/scrolling), %2 = monitor name", "%1 mode on monitor %2", root.modeName, monitorDelegate.composedDisplayName)
                                onToggled: {
                                    root.settingsBridge.setMonitorDisabled(monitorDelegate.screenName, !checked);
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: checked ? i18nc("@tooltip %1 = mode name", "Disable %1 mode on this monitor", root.modeName) : i18nc("@tooltip %1 = mode name", "Enable %1 mode on this monitor", root.modeName)
                            }

                            ToolButton {
                                visible: root.settingsBridge.virtualDesktopCount > 1
                                enabled: monitorSwitch.checked
                                icon.name: monitorDelegate.expanded ? "go-up" : "go-down"
                                text: monitorDelegate.expanded ? "" : i18n("Per-desktop")
                                display: AbstractButton.TextBesideIcon
                                Accessible.name: monitorDelegate.expanded ? i18n("Hide per-desktop toggles") : i18n("Show per-desktop toggles")
                                onClicked: monitorDelegate.expanded = !monitorDelegate.expanded
                                ToolTip.visible: hovered
                                ToolTip.text: monitorDelegate.expanded ? i18n("Hide per-desktop toggles") : i18n("Show per-desktop toggles")
                            }
                        }

                        // Per-desktop toggles (expandable)
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.gridUnit * 2
                            visible: monitorDelegate.expanded && root.settingsBridge.virtualDesktopCount > 1
                            enabled: monitorSwitch.checked
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Separator {
                                Layout.fillWidth: true
                            }

                            Repeater {
                                model: root.settingsBridge.virtualDesktopCount

                                RowLayout {
                                    id: desktopRow

                                    required property int index
                                    property int desktopNumber: index + 1
                                    property string desktopName: root.settingsBridge.virtualDesktopNames[index] || i18n("Desktop %1", desktopNumber)

                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    Kirigami.Icon {
                                        source: "preferences-desktop-virtual"
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                        opacity: 0.7
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: desktopRow.desktopName
                                        elide: Text.ElideRight
                                    }

                                    Switch {
                                        checked: {
                                            void root.settingsBridge.disabledDesktopsTick;
                                            return !root.settingsBridge.isDesktopDisabled(monitorDelegate.screenName, desktopRow.desktopNumber);
                                        }
                                        Accessible.name: i18nc("@accessible:label desktop enable toggle, %1 = mode, %2 = desktop name", "%1 mode on %2", root.modeName, desktopRow.desktopName)
                                        onToggled: {
                                            root.settingsBridge.setDesktopDisabled(monitorDelegate.screenName, desktopRow.desktopNumber, !checked);
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: checked ? i18nc("@tooltip %1 = mode, %2 = desktop name", "Disable %1 mode on %2", root.modeName, desktopRow.desktopName) : i18nc("@tooltip %1 = mode, %2 = desktop name", "Enable %1 mode on %2", root.modeName, desktopRow.desktopName)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Activities ────────────────────────────────────────────────────
    SettingsCard {
        Layout.fillWidth: true
        visible: root.settingsBridge.activitiesAvailable
        headerText: i18n("Activities")
        collapsible: true

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                text: root.activitiesDescription
                wrapMode: Text.WordWrap
                opacity: 0.7
            }

            ListView {
                Layout.fillWidth: true
                // Same empty-state fallback as the monitors list above —
                // a `Layout.preferredHeight: contentHeight` binding collapses
                // to 0 on an empty model, hiding the placeholder message and
                // making the card look broken until activities arrive.
                Layout.preferredHeight: count > 0 ? contentHeight : Kirigami.Units.gridUnit * 4
                Layout.margins: Kirigami.Units.smallSpacing
                clip: true
                interactive: false
                model: root.settingsBridge.activities
                Accessible.name: i18n("Activities list")
                Accessible.role: Accessible.List

                delegate: Item {
                    id: activityDelegate

                    required property var modelData
                    required property int index
                    property string activityId: modelData.id || ""
                    property string activityName: modelData.name || ""

                    width: ListView.view.width
                    height: activityCol.implicitHeight + Kirigami.Units.smallSpacing * 2

                    ColumnLayout {
                        id: activityCol

                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: activityDelegate.modelData.icon && activityDelegate.modelData.icon !== "" ? activityDelegate.modelData.icon : "activities"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                            }

                            Label {
                                Layout.fillWidth: true
                                text: activityDelegate.activityName
                                font.weight: activityDelegate.activityId === root.settingsBridge.currentActivity ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }

                            Label {
                                visible: activityDelegate.activityId === root.settingsBridge.currentActivity
                                text: i18n("Current")
                                font.italic: true
                                opacity: 0.7
                            }
                        }

                        Repeater {
                            model: root.settingsBridge.screens

                            RowLayout {
                                id: activityScreenRow

                                required property var modelData
                                required property int index
                                property string screenName: modelData.name || ""
                                // Composed display label — same join as the
                                // monitor list above (root._composeMonitorLabel)
                                // so the Accessible.name reads the manufacturer
                                // + model, not the raw connector id.
                                readonly property string composedDisplayName: root._composeMonitorLabel(modelData)

                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.gridUnit * 2
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: "video-display"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    opacity: 0.7
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: activityScreenRow.composedDisplayName
                                    elide: Text.ElideRight
                                }

                                Switch {
                                    checked: {
                                        void root.settingsBridge.disabledActivitiesTick;
                                        return !root.settingsBridge.isActivityDisabled(activityScreenRow.screenName, activityDelegate.activityId);
                                    }
                                    Accessible.name: i18nc("@accessible:label activity enable toggle, %1 = mode, %2 = activity name, %3 = monitor", "%1 mode for %2 on %3", root.modeName, activityDelegate.activityName, activityScreenRow.composedDisplayName)
                                    onToggled: {
                                        root.settingsBridge.setActivityDisabled(activityScreenRow.screenName, activityDelegate.activityId, !checked);
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: checked ? i18nc("@tooltip %1 = mode, %2 = activity, %3 = monitor", "Disable %1 mode for %2 on %3", root.modeName, activityDelegate.activityName, activityScreenRow.composedDisplayName) : i18nc("@tooltip %1 = mode, %2 = activity, %3 = monitor", "Enable %1 mode for %2 on %3", root.modeName, activityDelegate.activityName, activityScreenRow.composedDisplayName)
                                }
                            }
                        }
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                visible: root.settingsBridge.activities.length === 0
                type: Kirigami.MessageType.Information
                text: i18n("No activities found. Create activities in System Settings → Activities.")
            }
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: !root.settingsBridge.activitiesAvailable && root.settingsBridge.screens.length > 0
        type: Kirigami.MessageType.Information
        text: root.activitiesUnavailableMessage
    }
}
