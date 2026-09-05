// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The name pill above a workspace cell: the declared name for a named
// workspace, "Workspace N" otherwise. It is the drag source for whole
// workspace moves (reorder within the screen, transfer to another screen),
// the inline rename field (a click, or F2 on the highlighted workspace),
// and it carries the pin toggle that declares or undeclares the workspace
// as a named one. The pill itself never moves: a drag hands the root's
// DragProxy a "pz-workspace" payload.

import QtQuick
import org.kde.kirigami as Kirigami

Rectangle {
    id: label

    required property Item root
    required property Item cell
    property string text: ""
    property bool current: false
    property bool pinned: false
    // A placeholder cell's label is inert: nothing to rename, pin or move.
    property bool inert: false
    // The pin button shows while the pointer is over the cell or the pill.
    property bool cellHovered: false

    readonly property bool renaming: editor.visible
    readonly property var dragPayload: ({
            kind: "pz-workspace",
            windowId: "",
            desktopId: label.cell.desktopId,
            screenId: label.root.screenId,
            windowCount: 0,
            label: label.text
        })

    implicitWidth: row.implicitWidth + Kirigami.Units.largeSpacing * 2
    implicitHeight: row.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: height / 2
    color: label.current ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
    opacity: 0.9

    Accessible.role: Accessible.Button
    Accessible.name: label.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing

        Text {
            id: caption
            anchors.verticalCenter: parent.verticalCenter
            visible: !editor.visible
            text: label.text
            color: label.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            font: Kirigami.Theme.smallFont
            elide: Text.ElideRight
        }

        TextInput {
            id: editor
            anchors.verticalCenter: parent.verticalCenter
            visible: false
            width: Math.max(caption.implicitWidth, Kirigami.Units.gridUnit * 8)
            color: label.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            font: Kirigami.Theme.smallFont
            selectByMouse: true
            maximumLength: 64
            Accessible.name: i18nd("plasmazones", "Workspace name")
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    label.commitRename();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Escape) {
                    label.cancelRename();
                    event.accepted = true;
                }
            }
            onActiveFocusChanged: if (!activeFocus && editor.visible) {
                label.cancelRename();
            }
        }

        // Pin toggle: a pinned workspace is one with a declared name, which
        // survives being emptied. Shown on hover only.
        Rectangle {
            id: pinButton
            anchors.verticalCenter: parent.verticalCenter
            width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
            height: width
            radius: width / 2
            visible: !label.inert && !editor.visible && (label.cellHovered || pillHover.hovered || pinHover.hovered)
            color: pinHover.hovered ? Kirigami.Theme.hoverColor : "transparent"
            Accessible.role: Accessible.Button
            Accessible.name: label.pinned ? i18nd("plasmazones", "Unpin workspace") : i18nd("plasmazones", "Pin workspace")

            Kirigami.Icon {
                anchors.centerIn: parent
                width: Kirigami.Units.iconSizes.small
                height: width
                source: label.pinned ? "window-unpin" : "window-pin"
                color: label.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            }

            HoverHandler {
                id: pinHover
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                enabled: label.root.interactive
                onTapped: label.root.effect.pinWorkspace(label.cell.desktopId, !label.pinned)
            }
        }
    }

    HoverHandler {
        id: pillHover
        enabled: label.root.interactive
    }

    TapHandler {
        id: press
        acceptedButtons: Qt.LeftButton
        enabled: label.root.interactive && !label.inert && !editor.visible
        onPressedChanged: {
            if (pressed) {
                label.root.dragProxy.begin(label, label.dragPayload, point.pressPosition);
            } else if (!drag.active) {
                label.root.dragProxy.cancel();
            }
        }
        onTapped: {
            // The pin button sits inside the pill and has its own handler.
            const p = label.mapToItem(pinButton, point.position.x, point.position.y);
            if (pinButton.visible && pinButton.contains(p)) {
                return;
            }
            label.startRename();
        }
    }

    DragHandler {
        id: drag
        target: null
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        enabled: label.root.interactive && !label.inert && !editor.visible
        onActiveChanged: {
            if (active) {
                label.root.dragProxy.show();
            } else {
                label.root.dragProxy.finish(centroid.scenePosition);
            }
        }
        onActiveTranslationChanged: if (active) {
            label.root.dragProxy.follow(activeTranslation);
        }
    }

    function startRename() {
        if (label.inert || !label.cell.desktopId) {
            return;
        }
        editor.text = label.text;
        editor.visible = true;
        editor.selectAll();
        editor.forceActiveFocus();
        label.root.renaming = true;
    }

    function commitRename() {
        const name = editor.text.trim();
        if (name.length > 0 && name !== label.text) {
            label.root.effect.renameWorkspace(label.cell.desktopId, name);
        }
        label.cancelRename();
    }

    function cancelRename() {
        if (!editor.visible) {
            return;
        }
        editor.visible = false;
        label.root.renaming = false;
        label.root.forceActiveFocus();
    }
}
