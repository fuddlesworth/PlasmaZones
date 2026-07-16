// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Compact per-monitor scope control for a settings card header.
 *
 * Collapsed it reads "All Monitors"; clicking opens a popover with the spatial
 * DisplayMap to switch to a specific output. When scoped to a monitor the chip
 * shows the connector name + an override dot, and the popover offers a reset.
 *
 * Drives the shared `appSettings.scopeScreenName`, so the pick persists across
 * every per-monitor card and page. Lives in SettingsCard's header next to the
 * title (see SettingsCard `scopeEnabled`).
 */
Item {
    id: chip

    // The SettingsController (NOT the raw Settings object): the chip reads
    // `scopeScreenName` and invokes `physicalScreenId`, the override poll, and
    // the clearer — all of which live on the controller. Binding the bare
    // Settings object here would make those calls undefined at runtime.
    required property var appSettings
    // Q_INVOKABLE names on appSettings for this card's per-screen domain.
    property string hasOverridesMethod: ""
    property string clearerMethod: ""

    // The shared scope token. Invariant: it is always a physical-output id
    // (the popover's DisplayMap is physical-only, so every value written here
    // is physical-collapsed). The override poll below keys by it directly, so a
    // future caller that writes a virtual ("id/vs:N") scope would break the
    // override-dot lookup — keep scope physical.
    readonly property string scope: appSettings.scopeScreenName
    readonly property bool isPerScreen: scope !== ""
    property bool _hasOverride: false

    // Connector-first label for the current scope (tracks scope + screen list).
    readonly property string label: {
        if (scope === "")
            return i18n("All Monitors");
        var arr = appSettings.screens || [];
        for (var i = 0; i < arr.length; i++) {
            var nm = arr[i].name || "";
            var phys = appSettings.physicalScreenId(nm);
            if (nm === scope || phys === scope)
                return arr[i].connectorName || phys || scope;
        }
        return scope;
    }

    function _refresh() {
        // Gate on `scope` directly, NOT the derived isPerScreen property:
        // _refresh() runs from onScopeChanged, and on the ""→monitor transition
        // the isPerScreen binding has not necessarily recomputed yet when this
        // handler fires. Reading a stale isPerScreen (false) would leave
        // _hasOverride false — hiding the override dot and the "Reset this
        // monitor" action when switching from "All Monitors" to a scoped monitor
        // that has overrides (discussion #661).
        _hasOverride = scope !== "" && hasOverridesMethod !== "" && appSettings[hasOverridesMethod](scope) === true;
    }
    onScopeChanged: _refresh()
    onHasOverridesMethodChanged: _refresh()
    Component.onCompleted: _refresh()

    Connections {
        target: chip.appSettings
        function onPerScreenOverridesChanged() {
            chip._refresh();
        }
    }

    implicitWidth: pill.implicitWidth
    implicitHeight: pill.implicitHeight

    Rectangle {
        id: pill

        implicitWidth: pillRow.implicitWidth + Kirigami.Units.largeSpacing
        implicitHeight: pillRow.implicitHeight + Kirigami.Units.smallSpacing
        radius: height / 2
        color: pillMouse.containsMouse || popup.visible ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.05)
        border.width: 1
        border.color: chip.isPerScreen || popup.visible ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

        RowLayout {
            id: pillRow

            anchors.centerIn: parent
            spacing: Kirigami.Units.smallSpacing / 2

            Kirigami.Icon {
                source: "monitor"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.leftMargin: Kirigami.Units.smallSpacing / 2
                color: chip.isPerScreen ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
            }

            Label {
                text: chip.label
                font: Kirigami.Theme.smallFont
            }

            // Override marker for the current scope.
            Rectangle {
                visible: chip._hasOverride
                Layout.preferredWidth: Kirigami.Units.smallSpacing
                Layout.preferredHeight: Kirigami.Units.smallSpacing
                Layout.alignment: Qt.AlignVCenter
                radius: width / 2
                color: Kirigami.Theme.highlightColor
            }

            Kirigami.Icon {
                source: "arrow-down"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.rightMargin: Kirigami.Units.smallSpacing / 2
                opacity: 0.6
                rotation: popup.visible ? 180 : 0

                Behavior on rotation {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }
            }
        }

        MouseArea {
            id: pillMouse

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.ButtonDropDown
            Accessible.name: i18nc("@action", "Scope: %1", chip.label)
            Accessible.focusable: true
            Keys.onSpacePressed: popup.visible ? popup.close() : popup.open()
            Keys.onReturnPressed: popup.visible ? popup.close() : popup.open()
            // Numpad Enter alias, matching the sibling card components.
            Keys.onEnterPressed: popup.visible ? popup.close() : popup.open()
            onClicked: popup.visible ? popup.close() : popup.open()
        }

        Behavior on color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: Kirigami.Units.shortDuration
            }
        }
        Behavior on border.color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: Kirigami.Units.shortDuration
            }
        }
    }

    Popup {
        id: popup

        y: pill.height + Kirigami.Units.smallSpacing
        padding: Kirigami.Units.largeSpacing
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            DisplayMap {
                Layout.alignment: Qt.AlignHCenter
                appSettings: chip.appSettings
                hasOverridesMethod: chip.hasOverridesMethod
                // Highlight the shared scope; a pick writes it back.
                selectedScreenName: chip.scope
                onScreenPicked: name => chip.appSettings.scopeScreenName = name
            }

            Button {
                Layout.alignment: Qt.AlignRight
                visible: chip.isPerScreen && chip._hasOverride
                text: i18n("Reset this monitor")
                icon.name: "edit-clear"
                flat: true
                onClicked: {
                    if (chip.clearerMethod !== "")
                        chip.appSettings[chip.clearerMethod](chip.scope);
                }
            }
        }

        background: Rectangle {
            radius: Kirigami.Units.smallSpacing * 1.5
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        }
    }
}
