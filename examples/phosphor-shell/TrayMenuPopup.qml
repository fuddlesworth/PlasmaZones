// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// One level of a com.canonical.dbusmenu tree, rendered as a popup
// anchored to its trigger. Submenus reuse THIS popup by re-binding
// `rootId` to the deeper layout id returned by aboutToShowSubmenu;
// the parent context is lost when cascading, and clicking outside the
// popup dismisses the whole tree. A future enhancement would stack
// one fresh PopupWindow per cascade level (matching Plasma / Qt's
// QMenu cascade) and preserve a breadcrumb.
// Usage from TopPanel.qml:
//   TrayMenuPopup { id: trayMenu }
//   ...
//   trayMenu.openFor(trayDelegate)

import Phosphor.Service.Sni 1.0
import Phosphor.Shell 1.0
import Phosphor.Theme
import QtQuick

// `openFor` snapshots the delegate's dbus service + menu path, calls
// AboutToShow on the dbusmenu root, and maps the popup.
PopupWindow {
    // popupVisible is driven imperatively by menuModel.valid
    // (Connections handler). We deliberately don't bind it as a
    // QML expression: earlier rev bound popupVisible to service
    // / menuPath / valid AND wrote those properties back in the
    // close() handler, which Qt's binding system flagged as a
    // loop (popupVisible → onPopupVisibleChanged → close →
    // service = "" → popupVisible re-eval). Imperative drive
    // breaks the cycle without losing any behaviour.

    id: root

    /// Root id within the dbusmenu tree. 0 = the top of the tree;
    /// cascaded children pass the parent row's id here.
    property int rootId: 0
    // Named metrics. See MprisWidget.qml for the centralisation rationale.
    readonly property int popupWidthPx: 240
    readonly property int popupHeightMax: 420
    readonly property int popupHeightMin: 40
    readonly property int popupContentPad: 16
    readonly property int popupGap: 4
    readonly property int listMargin: 8
    readonly property int separatorRowHeight: 6
    readonly property int regularRowHeight: 28
    readonly property int separatorSideMargin: 6
    readonly property int rowPadX: 8
    readonly property int iconSlotSize: 16
    readonly property int rowSpacing: 8
    readonly property int submenuChevronSize: 10
    readonly property int labelFontSize: 12
    readonly property int shortcutFontSize: 11
    readonly property int checkmarkSize: 13
    readonly property int radioDotSize: 8
    readonly property int radioDotRadius: 4
    readonly property int rowRadius: 6
    readonly property int popupRadius: 10
    readonly property int shortcutMaxWidth: 80
    readonly property int iconSourceSize: 32

    /// Open the popup anchored to a tray delegate. `delegate` must
    /// expose `dbusService` and `menuPath` (the tray Repeater
    /// delegates do). The popup mounts ONLY after the menu model
    /// reaches `valid` state: see the Connections block below.
    function openFor(delegate: Item) {
        // Force-unmap before each open so every click gets visible
        // feedback. Two scenarios this handles:
        //   1. Same icon right-clicked twice with the popup still
        //      technically mapped between (e.g., user clicked the
        //      icon WITHOUT first dismissing): setting popupVisible
        //      to true when it's already true is a no-op, so the
        //      user sees nothing happen. False-then-true forces a
        //      remap.
        //   2. Compositor silently dismissed the popup on some
        //      configurations without firing popup_done back to
        //      Qt, leaving Qt's `popupVisible` stuck at true while
        //      the surface is actually unmapped. Same fix: explicit
        //      false first, then the Connections.onLoaded sets it
        //      true to trigger a fresh mapping.
        if (popupVisible)
            popupVisible = false;

        anchor = delegate;
        rootId = 0;
        const sameSource = menuModel.service === delegate.dbusService && menuModel.path === delegate.menuPath;
        menuModel.service = delegate.dbusService;
        menuModel.path = delegate.menuPath;
        // setService/setPath early-return when the values match the
        // current ones: that's the right behaviour for avoiding
        // redundant GetLayout traffic when QML re-assigns the same
        // properties, but it means re-opening the SAME menu (after
        // dismissal) doesn't trigger a fresh load. Force one
        // explicitly. The model's `loaded` signal will then fire and
        // the Connections handler maps the popup. AboutToShow is
        // still called for spec compliance.
        if (sameSource)
            menuModel.refresh();

        menuModel.aboutToShow();
    }

    popupEdge: PopupWindow.Below
    popupWidth: root.popupWidthPx
    popupHeight: Math.min(root.popupHeightMax, Math.max(root.popupHeightMin, menuList.contentHeight + root.popupContentPad))
    gap: root.popupGap
    popupVisible: false
    // Compositor dismisses the popup on outside-click via the Wayland
    // popup-grab protocol: that flips popupVisible to false from
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

    // Imperative show/hide driven off the model: no popupVisible
    // binding, no binding-loop hazard.
    Connections {
        // Fires on every successful GetLayout, not just the first
        // transition to valid. That makes it the right signal for
        // both initial open AND re-open of the same menu (which
        // doesn't toggle `valid` and so doesn't fire validChanged).
        function onLoaded() {
            root.popupVisible = true;
        }

        // App-side menu broken (wrong path, wrong interface): stay
        // hidden so the user doesn't see a frame of empty floating box.
        function onLoadFailed() {
            root.popupVisible = false;
        }

        // valid → false transitions happen when buildProxy clears
        // between different services; mirror them onto popupVisible so
        // a half-loaded popup doesn't linger across switches.
        function onValidChanged() {
            if (!menuModel.valid)
                root.popupVisible = false;
        }

        target: menuModel
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.933)
        radius: root.popupRadius
        border.color: Qt.rgba(Theme.outline_variant.r, Theme.outline_variant.g, Theme.outline_variant.b, 0.5)
        border.width: 1

        ListView {
            id: menuList

            anchors.fill: parent
            anchors.margins: root.listMargin
            model: menuModel
            clip: true
            interactive: contentHeight > height
            spacing: 0

            delegate: Item {
                id: menuRow

                // Role names are renamed in the model side to avoid
                // collision with QQuickItem's FINAL `visible` and its
                // `enabled` Q_PROPERTY: binding role values onto an
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
                height: itemType === "separator" ? root.separatorRowHeight : (itemVisible ? root.regularRowHeight : 0)
                visible: itemVisible

                // Separator: a hairline rule across the row.
                Rectangle {
                    visible: menuRow.itemType === "separator"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: root.separatorSideMargin
                    anchors.rightMargin: root.separatorSideMargin
                    height: 1
                    color: Theme.outline_variant
                }

                // Standard row: icon + label + (submenu arrow OR toggle indicator).
                Rectangle {
                    id: row

                    visible: menuRow.itemType !== "separator"
                    anchors.fill: parent
                    radius: root.rowRadius
                    color: rowMouse.containsMouse && menuRow.itemEnabled ? Theme.surface_container_high : "transparent"
                    opacity: menuRow.itemEnabled ? 1 : 0.4

                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: root.rowPadX
                        anchors.rightMargin: root.rowPadX
                        spacing: root.rowSpacing

                        // Toggle indicator. Sits in the icon slot when
                        // there is no icon, otherwise overlaps with it.
                        // dbusmenu apps occasionally set BOTH which
                        // looks weird either way: the toggle wins.
                        Item {
                            width: root.iconSlotSize
                            height: root.iconSlotSize
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                visible: menuRow.toggleType === "checkmark" && menuRow.toggleState === 1
                                text: "✓"
                                color: Theme.on_surface
                                font.pixelSize: root.checkmarkSize
                                font.weight: Font.Bold
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                visible: menuRow.toggleType === "radio"
                                width: root.radioDotSize
                                height: root.radioDotSize
                                radius: root.radioDotRadius
                                color: menuRow.toggleState === 1 ? Theme.primary : "transparent"
                                border.color: Theme.on_surface_variant
                                border.width: 1
                            }

                            Image {
                                anchors.centerIn: parent
                                visible: menuRow.toggleType.length === 0 && menuRow.iconUrl.length > 0
                                width: root.iconSlotSize
                                height: root.iconSlotSize
                                source: menuRow.iconUrl
                                sourceSize.width: root.iconSourceSize
                                sourceSize.height: root.iconSourceSize
                                smooth: true
                            }
                        }

                        Text {
                            id: labelText

                            text: menuRow.label
                            color: Theme.on_surface
                            font.pixelSize: root.labelFontSize
                            anchors.verticalCenter: parent.verticalCenter
                            // Width budget for the label slot, computed
                            // against the actual visible Row children
                            // and the Row spacings between them. Row
                            // skips both the invisible child AND its
                            // adjacent spacing slot, so the formula adds
                            // a spacing slot only when the corresponding
                            // sibling is visible. Clamp to >= 0 so a
                            // pathological shortcut string can't drive
                            // the label width negative (Qt would clamp
                            // to 0 and elide silently, leaving an
                            // unlabelled row).
                            readonly property bool shortcutVisible: shortcutText.text.length > 0
                            readonly property bool chevronVisible: chevronText.visible
                            readonly property int reserved: 2 * root.rowPadX + root.iconSlotSize + (shortcutVisible ? shortcutText.width + root.rowSpacing : 0) + (chevronVisible ? chevronText.implicitWidth + root.rowSpacing : 0) + root.rowSpacing
                            width: Math.max(0, row.width - reserved)
                            elide: Text.ElideRight
                        }

                        Text {
                            id: shortcutText

                            // Right-aligned shortcut display ("Ctrl+S")
                            // matching KDE's tray menus. Empty string
                            // means width 0 and no spacing reservation
                            // in the label binding above. Cap the
                            // intrinsic width so a pathological shortcut
                            // string can't crowd out the label slot;
                            // elide so the cap actually clips the
                            // painted glyphs instead of overflowing
                            // the slot.
                            text: menuRow.shortcut
                            color: Theme.on_surface_variant
                            font.pixelSize: root.shortcutFontSize
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text.length > 0
                            width: Math.min(root.shortcutMaxWidth, implicitWidth)
                            elide: Text.ElideRight
                        }

                        // Submenu chevron, only renders when the item
                        // has children.
                        Text {
                            id: chevronText

                            visible: menuRow.childrenDisplay === "submenu"
                            text: "▸"
                            color: Theme.on_surface_variant
                            font.pixelSize: root.submenuChevronSize
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
                                // Submenu: open cascade. We re-use the
                                // SAME root popup but re-bind to a deeper
                                // rootId. A future enhancement: stack
                                // multiple popups so the user can click
                                // back to a parent level.
                                root.rootId = menuModel.aboutToShowSubmenu(menuRow.index);
                            } else {
                                menuModel.triggerItem(menuRow.index);
                                root.popupVisible = false;
                            }
                        }
                    }
                }
            }
        }
    }
}
