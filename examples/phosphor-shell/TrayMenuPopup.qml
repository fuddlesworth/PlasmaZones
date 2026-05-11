// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// One level of a com.canonical.dbusmenu tree, rendered as a popup
// anchored to its trigger. Submenus open as cascading TrayMenuPopups
// — each cascade is a fresh PopupWindow whose anchor is the row that
// spawned it, so a 3-level menu produces 3 wl_surfaces (matches what
// Plasma / Qt's QMenu cascade produces under the hood).
// Usage from TopPanel.qml:
//   TrayMenuPopup { id: trayMenu }
//   ...
//   trayMenu.openFor(trayDelegate)

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick

// `openFor` snapshots the delegate's dbus service + menu path, calls
// AboutToShow on the dbusmenu root, and maps the popup.
PopupWindow {
    // popupVisible is driven imperatively by menuModel.valid
    // (Connections handler). We deliberately don't bind it as a
    // QML expression — earlier rev bound popupVisible to service
    // / menuPath / valid AND wrote those properties back in the
    // close() handler, which Qt's binding system flagged as a
    // loop (popupVisible → onPopupVisibleChanged → close →
    // service = "" → popupVisible re-eval). Imperative drive
    // breaks the cycle without losing any behaviour.

    id: root

    required property var shellState
    /// Root id within the dbusmenu tree. 0 = the top of the tree;
    /// cascaded children pass the parent row's id here.
    property int rootId: 0

    /// Open the popup anchored to a tray delegate. `delegate` must
    /// expose `dbusService` and `menuPath` (the tray Repeater
    /// delegates do). The popup mounts ONLY after the menu model
    /// reaches `valid` state — see the Connections block below.
    function openFor(delegate) {
        anchor = delegate;
        rootId = 0;
        menuModel.service = delegate.dbusService;
        menuModel.path = delegate.menuPath;
        menuModel.aboutToShow();
        // Re-show case: when the same tray icon is right-clicked
        // twice in a row (with a dismissal between), service/path
        // don't change, the model's setService/setPath are no-ops,
        // and `valid` stays true the whole time — so validChanged
        // never fires and the Connections handler below never gets
        // a chance to set popupVisible. Drive it directly here for
        // the already-valid case. For fresh menus (different service
        // or first-ever open), valid starts at false and the
        // Connections handler takes over once GetLayout completes.
        if (menuModel.valid)
            popupVisible = true;

    }

    popupEdge: PopupWindow.Below
    popupWidth: 240
    popupHeight: Math.min(420, Math.max(40, menuList.contentHeight + 16))
    gap: 4
    popupVisible: false
    // Compositor dismisses the popup on outside-click via the Wayland
    // popup-grab protocol — that flips popupVisible to false from
    // C++. We notify the model so its AboutToHide / Event "closed"
    // bookkeeping stays correct. No service-clearing here (that's
    // what caused the binding loop). Next openFor() reassigns
    // service / path cleanly.
    onPopupVisibleChanged: {
        if (!popupVisible)
            menuModel.aboutToHide();

    }

    DBusMenuModel {
        id: menuModel

        rootId: root.rootId
    }

    // Imperative show/hide driven off the model — no popupVisible
    // binding, no binding-loop hazard.
    Connections {
        // Show when (and only when) the model successfully loads. The
        // valid signal also fires the OTHER direction (true → false)
        // when buildProxy() clears between menu opens; in that
        // direction we want popup to stay hidden until the next
        // GetLayout completes.
        function onValidChanged() {
            root.popupVisible = menuModel.valid;
        }

        // App-side menu broken (wrong path, wrong interface): stay
        // hidden. menuModel.valid is already false here so this is
        // belt-and-braces, but explicit is better for the case where
        // a late LayoutUpdated signal toggles validity unexpectedly.
        function onLoadFailed() {
            root.popupVisible = false;
        }

        target: menuModel
    }

    Rectangle {
        anchors.fill: parent
        color: "#ee1e1e2e"
        radius: 10
        border.color: "#80a6adc8"
        border.width: 1

        ListView {
            id: menuList

            anchors.fill: parent
            anchors.margins: 8
            model: menuModel
            clip: true
            interactive: contentHeight > height
            spacing: 0

            delegate: Item {
                id: menuRow

                // Role names are renamed in the model side to avoid
                // collision with QQuickItem's FINAL `visible` and its
                // `enabled` Q_PROPERTY — binding role values onto an
                // Item with the same property name would either shadow
                // (enabled) or fail at load (visible is FINAL).
                required property int index
                required property int menuId
                required property string itemType
                required property string label
                required property bool itemEnabled
                required property bool itemVisible
                required property string iconUrl
                required property string toggleType
                required property int toggleState
                required property string childrenDisplay
                required property string shortcut

                width: ListView.view.width
                // Separators are thin, regular rows pop to a usable height.
                height: itemType === "separator" ? 6 : (itemVisible ? 28 : 0)
                visible: itemVisible

                // Separator: a hairline rule across the row.
                Rectangle {
                    visible: menuRow.itemType === "separator"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    height: 1
                    color: "#3a3a4a"
                }

                // Standard row: icon + label + (submenu arrow OR toggle indicator).
                Rectangle {
                    id: row

                    visible: menuRow.itemType !== "separator"
                    anchors.fill: parent
                    radius: 6
                    color: rowMouse.containsMouse && menuRow.itemEnabled ? "#3a3a4a" : "transparent"
                    opacity: menuRow.itemEnabled ? 1 : 0.4

                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        // Toggle indicator. Sits in the icon slot when
                        // there is no icon, otherwise overlaps with it.
                        // dbusmenu apps occasionally set BOTH which
                        // looks weird either way — the toggle wins.
                        Item {
                            width: 16
                            height: 16
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                visible: menuRow.toggleType === "checkmark" && menuRow.toggleState === 1
                                text: "✓"
                                color: "#cdd6f4"
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                visible: menuRow.toggleType === "radio"
                                width: 8
                                height: 8
                                radius: 4
                                color: menuRow.toggleState === 1 ? "#cba6f7" : "transparent"
                                border.color: "#a6adc8"
                                border.width: 1
                            }

                            Image {
                                anchors.centerIn: parent
                                visible: menuRow.toggleType.length === 0 && menuRow.iconUrl.length > 0
                                width: 16
                                height: 16
                                source: menuRow.iconUrl
                                sourceSize.width: 32
                                sourceSize.height: 32
                                smooth: true
                            }

                        }

                        Text {
                            id: labelText

                            text: menuRow.label
                            color: "#cdd6f4"
                            font.pixelSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                            // Eat remaining width so the submenu arrow
                            // (or shortcut text) hugs the right edge.
                            // Reserved: 16 icon + 8 + 8 label-side
                            // padding + 16 right slot (chevron or
                            // shortcut) + the shortcut's intrinsic
                            // width when present.
                            width: row.width - 16 - 8 - 8 - 16 - 8 - shortcutText.width - (shortcutText.text.length > 0 ? 8 : 0)
                            elide: Text.ElideRight
                        }

                        Text {
                            id: shortcutText

                            // Right-aligned shortcut display ("Ctrl+S")
                            // matching KDE's tray menus. Empty string
                            // → width 0 → no spacing reservation in
                            // the label binding above.
                            text: menuRow.shortcut
                            color: "#7f849c"
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text.length > 0
                        }

                        // Submenu chevron — only renders when the item
                        // has children.
                        Text {
                            visible: menuRow.childrenDisplay === "submenu"
                            text: "▸"
                            color: "#a6adc8"
                            font.pixelSize: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }

                    }

                    MouseArea {
                        id: rowMouse

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: menuRow.itemEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                        enabled: menuRow.itemEnabled
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: menuRow.label
                        onClicked: {
                            if (menuRow.childrenDisplay === "submenu") {
                                // Submenu — open cascade. We re-use the
                                // SAME root popup but re-bind to a deeper
                                // rootId. A future enhancement: stack
                                // multiple popups so the user can click
                                // back to a parent level.
                                root.rootId = menuModel.aboutToShowSubmenu(menuRow.index);
                            } else {
                                menuModel.triggerItem(menuRow.index);
                                root.close();
                            }
                        }
                    }

                }

            }

        }

    }

}
