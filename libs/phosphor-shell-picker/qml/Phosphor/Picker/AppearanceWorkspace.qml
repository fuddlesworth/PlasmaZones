// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    required property var controller
    property var availableWidgets: []
    property Component decoration: null
    readonly property var pages: [
        {
            id: "wallpaper",
            title: qsTr("Wallpaper"),
            description: qsTr("A different view. The same place to work."),
            icon: "preferences-desktop-wallpaper"
        },
        {
            id: "style",
            title: qsTr("Style"),
            description: qsTr("Give your shell a character of its own."),
            icon: "preferences-desktop-theme"
        },
        {
            id: "bar",
            title: qsTr("Bar"),
            description: qsTr("The things you reach for, right where you want them."),
            icon: "view-split-left-right"
        },
        {
            id: "presets",
            title: qsTr("Presets"),
            description: qsTr("A starting point for your own expression."),
            icon: "folder"
        }
    ]
    readonly property var page: pages.find(p => p.id === controller.page) || pages[0]
    property string exportId: ""
    property bool restoringScroll: true
    implicitWidth: 1224
    implicitHeight: 760
    Accessible.name: qsTr("Appearance")
    Accessible.role: Accessible.Pane
    focus: true
    Keys.onEscapePressed: controller.hide()
    // Keep Tab within the workspace; Basic.Popup contains its own dialog focus.
    Keys.onTabPressed: event => cycleFocus(event.modifiers & Qt.ShiftModifier)
    Keys.onBacktabPressed: cycleFocus(true)
    function cycleFocus(backward) {
        let next = root.Window.window?.activeFocusItem || root;
        for (let i = 0; i < 500; ++i) {
            next = next.nextItemInFocusChain(!backward);
            let ancestor = next;
            while (ancestor && ancestor !== root)
                ancestor = ancestor.parent;
            if (ancestor === root && next !== root && next.visible && next.enabled && next.activeFocusOnTab) {
                next.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }
    function restoreScroll() {
        restoringScroll = true;
        Qt.callLater(() => {
            viewport.contentY = Math.max(0, Math.min(controller.scrollPositions[controller.page] || 0, viewport.contentHeight - viewport.height));
            restoringScroll = false;
        });
    }
    Component.onCompleted: {
        AppearanceLibrary.rescan();
        restoreScroll();
    }
    Connections {
        target: root.controller
        function onPageChanged() {
            root.restoreScroll();
        }
    }
    ShellSurface {
        id: ground
        property bool shaderAnchor: true
        anchors.fill: parent
    }
    DecorationSlot {
        anchors.fill: parent
        component: root.decoration
        contentItem: ground
        surfacePath: "shell.phosphor.picker"
        focused: root.activeFocus
    }
    MouseArea {
        anchors.fill: parent
    }
    Rectangle {
        x: 1
        y: 1
        width: 177
        height: parent.height - 2
        radius: Appearance.radius
        color: Qt.alpha(Appearance.recess, .45)
        Rectangle {
            anchors.right: parent.right
            width: parent.radius
            height: parent.height
            color: parent.color
        }
    }
    Rectangle {
        x: 178
        width: 1
        height: parent.height
        color: Appearance.outline
    }
    Rectangle {
        x: 179
        y: 1
        width: parent.width - 191
        height: 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: Appearance.stops[0]
            }
            GradientStop {
                position: .34
                color: Appearance.stops[1]
            }
            GradientStop {
                position: .68
                color: Appearance.stops[2]
            }
            GradientStop {
                position: 1
                color: Appearance.stops[3]
            }
        }
    }
    Row {
        x: 26
        y: 29
        spacing: 12
        Text {
            text: "φ"
            color: Appearance.stops[0]
            font.pixelSize: 34
            font.weight: Font.Light
        }
        Column {
            y: 3
            spacing: 5
            LookText {
                text: "PHOSPHOR"
                kicker: true
                color: Appearance.text
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
            LookText {
                text: qsTr("Make it yours.")
                muted: true
                size: 9
            }
        }
    }
    Column {
        x: 12
        y: 103
        width: 151
        spacing: 7
        Repeater {
            model: root.pages
            Basic.AbstractButton {
                id: nav
                required property var modelData
                width: parent.width
                height: 44
                Accessible.name: modelData.title
                Accessible.selected: root.controller.page === modelData.id
                onClicked: root.controller.page = modelData.id
                background: Rectangle {
                    radius: 11
                    color: root.controller.page === nav.modelData.id ? Qt.alpha(Appearance.accent, .17) : nav.hovered ? Qt.alpha(Appearance.card, .3) : "transparent"
                    border.color: nav.visualFocus ? Appearance.text : "transparent"
                    Rectangle {
                        x: 0
                        y: 13
                        width: 2
                        height: 18
                        radius: 1
                        visible: root.controller.page === nav.modelData.id
                        color: Appearance.accent
                    }
                }
                contentItem: RowLayout {
                    spacing: 12
                    ShellIcon {
                        Layout.leftMargin: 12
                        implicitWidth: 17
                        implicitHeight: 17
                        source: nav.modelData.icon
                        color: root.controller.page === nav.modelData.id ? Appearance.accent : Appearance.muted
                    }
                    LookText {
                        text: nav.modelData.title
                        size: 11
                        muted: root.controller.page !== nav.modelData.id
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
    Column {
        x: 26
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 26
        spacing: 17
        Item {
            width: 73
            height: 48
            Rectangle {
                x: 0
                y: 1
                width: 37
                height: 45
                rotation: -4
                radius: 3
                color: Qt.alpha(Appearance.stops[0], .13)
                border.color: Appearance.stops[0]
            }
            Rectangle {
                x: 44
                y: 0
                width: 27
                height: 20
                rotation: -4
                radius: 3
                color: Qt.alpha(Appearance.stops[2], .13)
                border.color: Appearance.stops[2]
            }
            Rectangle {
                x: 44
                y: 26
                width: 27
                height: 20
                rotation: -4
                radius: 3
                color: Qt.alpha(Appearance.stops[3], .13)
                border.color: Appearance.stops[3]
            }
        }
        LookText {
            text: qsTr("Same Phosphor.\nYour expression.")
            size: 11
            muted: true
            lineHeight: 1.8
        }
        LookText {
            text: qsTr("PREVIEW IT. MAKE IT YOURS.")
            kicker: true
            font.pixelSize: 6
            font.letterSpacing: .8
        }
    }
    RowLayout {
        x: 206
        y: 21
        width: parent.width - 234
        height: 57
        spacing: 16
        ColumnLayout {
            spacing: 4
            LookText {
                text: root.page.title
                size: 25
                font.weight: Font.Medium
            }
            LookText {
                text: root.page.description
                muted: true
                size: 11
            }
        }
        Item {
            Layout.fillWidth: true
        }
        ShellButton {
            visible: root.controller.page === "wallpaper" || root.controller.page === "presets"
            text: root.controller.page === "wallpaper" ? qsTr("Add images") : qsTr("Import preset")
            iconName: root.controller.page === "wallpaper" ? "list-add" : "document-import"
            outlined: true
            flat: true
            implicitHeight: 37
            enabled: !AppearanceLibrary.busy
            onClicked: root.controller.page === "wallpaper" ? imagesDialog.open() : importDialog.open()
        }
        ShellButton {
            iconName: "window-close"
            label: qsTr("Close Appearance")
            flat: true
            onClicked: root.controller.hide()
        }
    }
    Flickable {
        id: viewport
        x: 206
        y: 93
        width: parent.width - 234
        height: parent.height - 161
        contentWidth: width
        contentHeight: pageLoader.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Basic.ScrollBar.vertical: Basic.ScrollBar {
            width: 5
        }
        onContentYChanged: if (!root.restoringScroll) {
            const positions = Object.assign({}, root.controller.scrollPositions);
            positions[root.controller.page] = contentY;
            root.controller.scrollPositions = positions;
        }
        Loader {
            id: pageLoader
            width: viewport.width
            height: item ? item.implicitHeight : 0
            sourceComponent: root.controller.page === "style" ? stylePage : root.controller.page === "bar" ? barPage : root.controller.page === "presets" ? presetsPage : wallpaperPage
            onLoaded: root.restoreScroll()
        }
    }
    Component {
        id: wallpaperPage
        AppearanceWallpaper {
            controller: root.controller
        }
    }
    Component {
        id: stylePage
        AppearanceStyle {
            controller: root.controller
        }
    }
    Component {
        id: barPage
        AppearanceBar {
            controller: root.controller
            availableWidgets: root.availableWidgets
        }
    }
    Component {
        id: presetsPage
        AppearancePresets {
            controller: root.controller
            onDialogRequested: (mode, preset) => dialogs.show(mode, preset)
            onExportRequested: id => {
                root.exportId = id;
                exportDialog.open();
            }
        }
    }
    Rectangle {
        x: 179
        y: parent.height - 68
        width: parent.width - 179
        height: 1
        color: Appearance.outline
    }
    RowLayout {
        x: 206
        y: parent.height - 56
        width: parent.width - 234
        height: 44
        spacing: 10
        Rectangle {
            implicitWidth: 7
            implicitHeight: 7
            radius: 4
            color: AppearanceStore.dirty ? Appearance.stops[3] : Appearance.stops[0]
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            LookText {
                text: AppearanceLibrary.busy ? qsTr("Preparing your image…") : AppearanceStore.dirty ? qsTr("Previewing changes") : qsTr("Your current look")
                size: 11
            }
            LookText {
                Layout.fillWidth: true
                text: AppearanceLibrary.error || AppearanceStore.error || (AppearanceStore.dirty ? qsTr("Apply when it feels right.") : qsTr("Changes preview here before you apply them."))
                size: 9
                muted: true
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        ShellButton {
            text: qsTr("View desktop")
            iconName: "view-split-left-right"
            flat: true
            labelSize: 10
            onClicked: root.controller.desktopPreview = true
        }
        ShellButton {
            text: qsTr("Revert")
            outlined: true
            flat: true
            implicitHeight: 37
            enabled: AppearanceStore.dirty || AppearanceLibrary.busy
            onClicked: {
                AppearanceLibrary.cancel();
                AppearanceStore.revertPreview();
            }
        }
        ShellButton {
            text: qsTr("Apply changes")
            highlighted: true
            implicitHeight: 37
            enabled: AppearanceStore.dirty && !AppearanceLibrary.busy
            onClicked: root.controller.apply()
        }
    }
    AppearanceDialogs {
        id: dialogs
        controller: root.controller
    }
    FileDialog {
        id: imagesDialog
        title: qsTr("Add wallpapers")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.avif *.jxl *.bmp *.svg)")]
        onAccepted: AppearanceLibrary.importImages(selectedFiles)
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import a Phosphor preset")
        nameFilters: [qsTr("Phosphor presets (*.json)")]
        onAccepted: if (AppearanceLibrary.inspectImport(selectedFile))
            dialogs.show("import", AppearanceLibrary.imported)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export a Phosphor preset")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("Phosphor presets (*.json)")]
        onAccepted: AppearanceLibrary.exportPreset(root.exportId, selectedFile)
    }
}
