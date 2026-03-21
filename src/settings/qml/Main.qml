// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ApplicationWindow {
    id: window

    // Responsive sidebar: collapse to icon-only below 750px
    readonly property bool sidebarCompact: window.width < Kirigami.Units.gridUnit * 50
    // Track page navigation for transitions
    property int _previousIndex: 0
    // Page component map — loaded on demand by StackView
    property var _pageComponents: ({
        "layouts": "LayoutsPage.qml",
        "snapping": "SnappingPage.qml",
        "autotiling": "AutotilingPage.qml",
        "assignments": "AssignmentsPage.qml",
        "exclusions": "ExclusionsPage.qml",
        "editor": "EditorPage.qml",
        "general": "GeneralPage.qml",
        "about": "AboutPage.qml"
    })

    title: i18n("PlasmaZones Settings")
    width: Kirigami.Units.gridUnit * 80
    height: Kirigami.Units.gridUnit * 48
    visible: true
    Component.onCompleted: {
        var geo = settingsController.loadWindowGeometry();
        if (geo.width > 0 && geo.height > 0) {
            window.width = geo.width;
            window.height = geo.height;
        }
        if (geo.hasPosition) {
            window.x = geo.x;
            window.y = geo.y;
        }
    }
    onClosing: {
        settingsController.saveWindowGeometry(window.x, window.y, window.width, window.height);
    }

    Shortcut {
        sequence: "Ctrl+PgUp"
        onActivated: {
            if (sidebar.currentIndex > 0)
                settingsController.activePage = pageModel.get(sidebar.currentIndex - 1).name;

        }
    }

    Shortcut {
        sequence: "Ctrl+PgDown"
        onActivated: {
            if (sidebar.currentIndex < pageModel.count - 1)
                settingsController.activePage = pageModel.get(sidebar.currentIndex + 1).name;

        }
    }

    ListModel {
        id: pageModel

        // ── Display ──
        ListElement {
            name: "layouts"
            label: "Layouts"
            iconName: "view-grid"
            section: "Display"
        }

        ListElement {
            name: "snapping"
            label: "Snapping"
            iconName: "view-split-left-right"
            section: "Display"
        }

        ListElement {
            name: "autotiling"
            label: "Tiling"
            iconName: "window-duplicate"
            section: "Display"
        }

        // ── Behavior ──
        ListElement {
            name: "assignments"
            label: "Assignments"
            iconName: "view-list-details"
            section: "Behavior"
        }

        ListElement {
            name: "exclusions"
            label: "Exclusions"
            iconName: "dialog-cancel"
            section: "Behavior"
        }

        ListElement {
            name: "editor"
            label: "Editor"
            iconName: "document-edit"
            section: "Behavior"
        }

        // ── System ──
        ListElement {
            name: "general"
            label: "General"
            iconName: "configure"
            section: "System"
        }

        ListElement {
            name: "about"
            label: "About"
            iconName: "help-about"
            section: "System"
        }

    }

    RowLayout {
        id: mainContent

        anchors.fill: parent
        spacing: 0

        // ═══════════════════════════════════════════════════════════════════
        // SIDEBAR
        // ═══════════════════════════════════════════════════════════════════
        Pane {
            id: sidebarPane

            Layout.fillHeight: true
            Layout.preferredWidth: window.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12
            Layout.minimumWidth: window.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Navigation list with sections
                ListView {
                    id: sidebar

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: pageModel
                    currentIndex: {
                        for (var i = 0; i < pageModel.count; i++) {
                            if (pageModel.get(i).name === settingsController.activePage)
                                return i;

                        }
                        return 0;
                    }
                    clip: true
                    section.property: "section"

                    section.delegate: Item {
                        required property string section

                        width: sidebar.width
                        height: window.sidebarCompact ? Kirigami.Units.smallSpacing : (sectionLabel.implicitHeight + Kirigami.Units.largeSpacing)
                        clip: true

                        Label {
                            id: sectionLabel

                            anchors.left: parent.left
                            anchors.leftMargin: Kirigami.Units.largeSpacing
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Kirigami.Units.smallSpacing / 2
                            text: parent.section
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            font.family: Kirigami.Theme.smallFont.family
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1.2
                            opacity: window.sidebarCompact ? 0 : 0.5
                        }

                    }

                    delegate: ItemDelegate {
                        id: navDelegate

                        required property int index
                        required property string name
                        required property string label
                        required property string iconName

                        width: sidebar.width
                        height: Kirigami.Units.gridUnit * 2.5
                        highlighted: ListView.isCurrentItem
                        onClicked: {
                            window._previousIndex = sidebar.currentIndex;
                            settingsController.activePage = name;
                        }
                        leftPadding: window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing
                        rightPadding: window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing
                        ToolTip.visible: window.sidebarCompact && navDelegate.hovered
                        ToolTip.text: navDelegate.label
                        ToolTip.delay: 300

                        background: Rectangle {
                            color: navDelegate.highlighted ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12) : navDelegate.hovered ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                            radius: Kirigami.Units.smallSpacing

                            // Left accent bar
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.round(Kirigami.Units.devicePixelRatio * 2.5)
                                height: parent.height * 0.5
                                radius: width / 2
                                color: Kirigami.Theme.highlightColor
                                visible: navDelegate.highlighted
                            }

                            Behavior on color {
                                ColorAnimation {
                                    duration: 120
                                }

                            }

                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: navDelegate.iconName
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.fillWidth: window.sidebarCompact
                                Layout.alignment: Qt.AlignVCenter
                                opacity: navDelegate.highlighted ? 1 : 0.7

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 120
                                    }

                                }

                            }

                            Label {
                                text: navDelegate.label
                                Layout.fillWidth: true
                                font.weight: navDelegate.highlighted ? Font.DemiBold : Font.Normal
                                opacity: navDelegate.highlighted ? 1 : 0.7
                                visible: !window.sidebarCompact

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 120
                                    }

                                }

                            }

                        }

                    }

                }

                // Daemon status
                Pane {
                    Layout.fillWidth: true
                    padding: Kirigami.Units.smallSpacing
                    topPadding: Kirigami.Units.smallSpacing * 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        Rectangle {
                            width: Kirigami.Units.smallSpacing * 1.5
                            height: Kirigami.Units.smallSpacing * 1.5
                            radius: width / 2
                            Layout.alignment: Qt.AlignVCenter
                            color: settingsController.daemonRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor

                            // Pulsing glow when running
                            SequentialAnimation on opacity {
                                loops: settingsController.daemonRunning ? Animation.Infinite : 0
                                running: settingsController.daemonRunning

                                NumberAnimation {
                                    from: 1
                                    to: 0.4
                                    duration: 1500
                                    easing.type: Easing.InOutSine
                                }

                                NumberAnimation {
                                    from: 0.4
                                    to: 1
                                    duration: 1500
                                    easing.type: Easing.InOutSine
                                }

                            }

                        }

                        Label {
                            text: settingsController.daemonRunning ? i18n("Running") : i18n("Stopped")
                            opacity: 0.7
                            Layout.fillWidth: true
                            visible: !window.sidebarCompact
                        }

                        SettingsSwitch {
                            checked: settingsController.daemonRunning
                            accessibleName: i18n("Toggle daemon")
                            onToggled: {
                                if (settingsController.daemonRunning)
                                    settingsController.daemonController.stopDaemon();
                                else
                                    settingsController.daemonController.startDaemon();
                            }
                        }

                    }

                    background: Rectangle {
                        color: "transparent"

                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: Math.round(Kirigami.Units.devicePixelRatio)
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                        }

                    }

                }

            }

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.InOutQuad
                }

            }

            Behavior on Layout.minimumWidth {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.InOutQuad
                }

            }

            background: Rectangle {
                color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 1)

                // Subtle right edge shadow
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.round(Kirigami.Units.devicePixelRatio)
                    color: Kirigami.Theme.separatorColor !== undefined ? Kirigami.Theme.separatorColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════
        // CONTENT AREA
        // ═══════════════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Breadcrumb header ──────────────────────────────────────
            Pane {
                Layout.fillWidth: true
                padding: Kirigami.Units.largeSpacing
                topPadding: Kirigami.Units.smallSpacing * 2
                bottomPadding: Kirigami.Units.smallSpacing * 2
                visible: !window.sidebarCompact

                RowLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: {
                            let idx = sidebar.currentIndex;
                            if (idx < 0 || idx >= pageModel.count)
                                return "";

                            let section = pageModel.get(idx).section;
                            let label = pageModel.get(idx).label;
                            return section + "  ›  " + label;
                        }
                        opacity: 0.5
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                }

                background: Rectangle {
                    color: "transparent"

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.round(Kirigami.Units.devicePixelRatio)
                        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06)
                    }

                }

            }

            // Page content with crossfade transition
            Item {
                id: pageContainer

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                // Opaque background to prevent bleed-through
                Rectangle {
                    anchors.fill: parent
                    color: Kirigami.Theme.backgroundColor
                }

                Loader {
                    id: pageLoader

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    source: Qt.resolvedUrl(window._pageComponents[settingsController.activePage] || "LayoutsPage.qml")
                    asynchronous: false
                    // Fade in on page change
                    onLoaded: {
                        fadeIn.restart();
                    }

                    NumberAnimation {
                        id: fadeIn

                        target: pageLoader.item
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 180
                        easing.type: Easing.OutCubic
                    }

                }

                // ── Toast notification ──────────────────────────────────
                Rectangle {
                    id: toast

                    property string message: ""

                    function show(msg) {
                        message = msg;
                        toastShow.restart();
                        toastHide.restart();
                    }

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Kirigami.Units.largeSpacing * 2
                    width: toastLabel.implicitWidth + Kirigami.Units.largeSpacing * 3
                    height: toastLabel.implicitHeight + Kirigami.Units.largeSpacing * 1.5
                    radius: height / 2
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.85)
                    opacity: 0
                    visible: opacity > 0
                    z: 100

                    Label {
                        id: toastLabel

                        anchors.centerIn: parent
                        text: toast.message
                        color: Kirigami.Theme.backgroundColor
                        font.weight: Font.Medium
                    }

                    NumberAnimation {
                        id: toastShow

                        target: toast
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 200
                        easing.type: Easing.OutCubic
                    }

                    SequentialAnimation {
                        id: toastHide

                        PauseAnimation {
                            duration: 2000
                        }

                        NumberAnimation {
                            target: toast
                            property: "opacity"
                            from: 1
                            to: 0
                            duration: 400
                            easing.type: Easing.InCubic
                        }

                    }

                }

            }

            // ── Footer action bar ───────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: Math.round(Kirigami.Units.devicePixelRatio)
                color: settingsController.needsSave ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

                Behavior on color {
                    ColorAnimation {
                        duration: 300
                    }

                }

            }

            Pane {
                Layout.fillWidth: true
                padding: Kirigami.Units.smallSpacing * 1.5
                topPadding: Kirigami.Units.smallSpacing * 2
                bottomPadding: Kirigami.Units.smallSpacing * 2

                RowLayout {
                    anchors.fill: parent

                    Button {
                        text: i18n("Reset")
                        icon.name: "edit-undo"
                        enabled: settingsController.needsSave
                        flat: true
                        onClicked: settingsController.load()
                        opacity: enabled ? 1 : 0.4

                        Behavior on opacity {
                            NumberAnimation {
                                duration: 150
                            }

                        }

                    }

                    Button {
                        text: i18n("Defaults")
                        icon.name: "document-revert"
                        flat: true
                        onClicked: settingsController.defaults()
                        opacity: 0.7
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        id: applyButton

                        text: i18n("Apply")
                        icon.name: "dialog-ok-apply"
                        enabled: settingsController.needsSave
                        highlighted: settingsController.needsSave
                        onClicked: {
                            settingsController.save();
                            toast.show(i18n("Settings applied"));
                        }

                        // Reset scale when not pulsing
                        Binding {
                            target: applyButton
                            property: "scale"
                            value: 1
                            when: !settingsController.needsSave
                            restoreMode: Binding.RestoreBinding
                        }

                        // Subtle pulsing glow when changes are pending
                        SequentialAnimation on scale {
                            loops: Animation.Infinite
                            running: settingsController.needsSave

                            NumberAnimation {
                                from: 1
                                to: 1.03
                                duration: 800
                                easing.type: Easing.InOutSine
                            }

                            NumberAnimation {
                                from: 1.03
                                to: 1
                                duration: 800
                                easing.type: Easing.InOutSine
                            }

                        }

                        Behavior on opacity {
                            NumberAnimation {
                                duration: 150
                            }

                        }

                    }

                }

            }

        }

    }

}
