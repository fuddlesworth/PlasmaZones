// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var controller: typeof StatusIcons !== "undefined" ? StatusIcons : null
    property string page: "settings"
    property real railT: 0.93
    property Component decoration: null
    property real maximumHeight: Math.max(220, (Screen.height || 900) - Appearance.barHeight - Tokens.spacing_xl * 3)
    property var history: []
    property string preferredFocusId: ""
    readonly property bool settingsPage: page === "settings"
    readonly property bool overflowPage: page === "overflow"
    readonly property var entry: controller ? controller.icons.find(item => item.id === page) || ({}) : ({})
    readonly property string heading: settingsPage ? i18n("Status icons") : overflowPage ? i18n("More status") : entry.title || i18n("Indicator unavailable")
    readonly property string description: settingsPage ? i18n("Choose what stays in view.") : overflowPage ? i18n("Your other indicators.") : entry.summary || ""
    readonly property real bodyPadding: settingsPage ? Tokens.spacing_l : Tokens.spacing_s
    signal detailRequested(string panelId)
    signal closeRequested
    implicitWidth: Math.min(settingsPage ? 480 : 314, Math.max(1, (Screen.width || 1440) - Tokens.spacing_xl * 2))
    implicitHeight: Math.min(maximumHeight, header.implicitHeight + footer.implicitHeight + body.implicitHeight + bodyPadding * 2 + Tokens.spacing_l * 2 + Tokens.spacing_s * 2 + 2)
    focus: true
    Accessible.role: Accessible.Dialog
    Accessible.name: heading
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    function showPage(value: string): void {
        if (value === page)
            return;
        preferredFocusId = "";
        history = history.concat([page]);
        page = value;
    }
    function back(): void {
        if (!history.length) {
            closeRequested();
            return;
        }
        const previous = history.slice();
        preferredFocusId = page;
        page = previous.pop();
        history = previous;
    }
    function focusPage(): void {
        if (body.status === Loader.Ready && preferredFocusId && typeof body.item.focusEntry === "function")
            body.item.focusEntry(preferredFocusId);
        else if (body.status === Loader.Ready && typeof body.item.focusFirst === "function")
            body.item.focusFirst();
        else
            close.forceActiveFocus();
    }
    onPageChanged: {
        scroller.contentY = 0;
        Qt.callLater(root.focusPage);
    }
    Keys.onEscapePressed: event => {
        root.closeRequested();
        event.accepted = true;
    }
    ShellSurface {
        id: ground
        anchors.fill: parent
        railT: root.railT
        accented: true
    }
    DecorationSlot {
        anchors.fill: parent
        component: root.decoration
        contentItem: ground
        surfacePath: "shell.phosphor.popout"
        layeredStages: true
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.margins: Tokens.spacing_l
            spacing: Tokens.spacing_m
            ShellButton {
                visible: root.history.length > 0
                iconName: "go-previous-symbolic"
                label: i18n("Back")
                flat: true
                onClicked: root.back()
            }
            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: Tokens.radius_s
                color: Qt.alpha(Appearance.stops[1], 0.12)
                border.width: 1
                border.color: Appearance.outline
                ShellIcon {
                    anchors.centerIn: parent
                    width: 20
                    height: width
                    source: root.settingsPage ? "configure" : root.overflowPage ? "view-more-symbolic" : root.entry.iconName || "configure"
                    isMask: true
                    color: Appearance.stops[1]
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing_xs
                DetailText {
                    Layout.fillWidth: true
                    text: i18n("QUICK SETTINGS")
                    muted: true
                    font.pixelSize: Tokens.font_size_label_s
                }
                DetailText {
                    Layout.fillWidth: true
                    text: root.heading
                    font.pixelSize: root.settingsPage ? Tokens.font_size_display_s : Tokens.font_size_title_m
                    font.weight: Font.Medium
                }
                DetailText {
                    Layout.fillWidth: true
                    text: root.description
                    visible: text !== ""
                    muted: true
                    font.pixelSize: Tokens.font_size_label_s
                }
            }
            ShellButton {
                id: close
                objectName: "statusClose"
                iconName: "window-close-symbolic"
                label: i18n("Close status icons")
                implicitWidth: 28
                implicitHeight: 28
                flat: true
                onClicked: root.closeRequested()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.outline
        }
        Flickable {
            id: scroller
            objectName: "statusScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight + root.bodyPadding * 2
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            Basic.ScrollBar.vertical: Basic.ScrollBar {
                active: scroller.interactive
            }
            Loader {
                id: body
                x: root.bodyPadding
                y: root.bodyPadding
                width: Math.max(0, scroller.width - root.bodyPadding * 2)
                sourceComponent: root.settingsPage ? settingsComponent : root.overflowPage ? overflowComponent : menuComponent
                onLoaded: Qt.callLater(root.focusPage)
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.outline
        }
        RowLayout {
            id: footer
            Layout.fillWidth: true
            Layout.margins: Tokens.spacing_s
            spacing: Tokens.spacing_s
            ShellButton {
                objectName: "statusReset"
                visible: root.settingsPage
                text: i18n("Reset icons")
                foreground: Appearance.stops[1]
                enabled: root.controller !== null
                flat: true
                onClicked: root.controller.resetIcons()
            }
            DetailText {
                visible: root.settingsPage
                Layout.fillWidth: true
                text: i18n("Changes apply immediately")
                muted: true
                font.pixelSize: Tokens.font_size_label_s
            }
            ShellButton {
                objectName: "statusDone"
                visible: root.settingsPage
                text: i18n("Done")
                highlighted: true
                onClicked: root.closeRequested()
            }
            ShellButton {
                objectName: "statusArrange"
                visible: !root.settingsPage
                Layout.fillWidth: true
                text: i18n("Arrange status icons")
                iconName: "configure"
                flat: true
                onClicked: root.showPage("settings")
            }
        }
    }
    Component {
        id: settingsComponent
        StatusIconsSettings {
            controller: root.controller
            onPageRequested: page => root.showPage(page)
            onDetailRequested: panelId => root.detailRequested(panelId)
        }
    }
    Component {
        id: menuComponent
        StatusIconMenu {
            controller: root.controller
            entry: root.entry
            onDetailRequested: panelId => root.detailRequested(panelId)
        }
    }
    Component {
        id: overflowComponent
        Column {
            id: overflowBody
            spacing: Tokens.spacing_xs
            function focusFirst(): void {
                focusEntry("");
            }
            function focusEntry(id: string): void {
                for (let index = 0; index < overflowRows.count; ++index) {
                    const button = overflowRows.itemAt(index);
                    if (button.modelData.id === id) {
                        button.forceActiveFocus();
                        return;
                    }
                }
                if (overflowRows.count)
                    overflowRows.itemAt(0).forceActiveFocus();
                else
                    close.forceActiveFocus();
            }
            Keys.onPressed: event => {
                if (![Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key) || !overflowRows.count)
                    return;
                let current = 0;
                for (let i = 0; i < overflowRows.count; ++i)
                    if (overflowRows.itemAt(i).activeFocus)
                        current = i;
                const next = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? overflowRows.count - 1 : (current + (event.key === Qt.Key_Up ? -1 : 1) + overflowRows.count) % overflowRows.count;
                overflowRows.itemAt(next).forceActiveFocus();
                event.accepted = true;
            }
            Repeater {
                id: overflowRows
                model: root.controller ? root.controller.overflowIcons : []
                delegate: StatusMenuItem {
                    required property var modelData
                    objectName: "statusOverflow-" + modelData.id
                    text: modelData.title
                    subtitle: modelData.summary
                    statusEntry: modelData
                    onClicked: root.showPage(modelData.id)
                }
            }
            DetailText {
                visible: overflowRows.count === 0
                width: parent.width
                text: i18n("All your visible icons fit in the bar.")
                muted: true
                font.pixelSize: Tokens.font_size_body_s
            }
        }
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged(): void {
            const item = root.Window.window.activeFocusItem;
            let ancestor = item;
            while (ancestor && ancestor !== body)
                ancestor = ancestor.parent;
            if (!ancestor || !item)
                return;
            const position = item.mapToItem(scroller, 0, 0);
            const margin = Tokens.spacing_s;
            const offset = position.y < margin ? position.y - margin : Math.max(0, position.y + item.height - scroller.height + margin);
            scroller.contentY = Math.max(0, Math.min(Math.max(0, scroller.contentHeight - scroller.height), scroller.contentY + offset));
        }
    }
}
