// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Info message at top level, matching KCM placement
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Windows from excluded applications or with excluded window classes will be ignored by both snapping and autotiling.")
            visible: true
        }

        // --- Window Filtering ---
        Item {
            Layout.fillWidth: true
            implicitHeight: filteringCard.implicitHeight

            Kirigami.Card {
                id: filteringCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    text: i18n("Window Filtering")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Transient:")
                        text: i18n("Exclude transient windows (dialogs, popups, toolbars)")
                        checked: kcm.excludeTransientWindows
                        onToggled: kcm.excludeTransientWindows = checked
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Minimum Window Size")
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Width:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: 1000
                            stepSize: 10
                            value: kcm.minimumWindowWidth
                            onValueModified: kcm.minimumWindowWidth = value
                            textFromValue: function(value) {
                                return value === 0 ? i18n("Disabled") : value + " px";
                            }
                            Accessible.name: i18n("Minimum window width")
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Height:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: 1000
                            stepSize: 10
                            value: kcm.minimumWindowHeight
                            onValueModified: kcm.minimumWindowHeight = value
                            textFromValue: function(value) {
                                return value === 0 ? i18n("Disabled") : value + " px";
                            }
                            Accessible.name: i18n("Minimum window height")
                        }

                    }

                    Label {
                        text: i18n("Windows smaller than these dimensions will be excluded. Set to 0 to disable.")
                        font.italic: true
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                }

            }

        }

        // --- Excluded Applications ---
        Item {
            Layout.fillWidth: true
            implicitHeight: appsCard.implicitHeight

            Kirigami.Card {
                id: appsCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    text: i18n("Excluded Applications")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing

                        TextField {
                            id: appInput

                            Layout.fillWidth: true
                            placeholderText: i18n("Application name (e.g., firefox, konsole)")
                            onAccepted: {
                                if (text.length > 0) {
                                    let apps = kcm.excludedApplications.slice();
                                    let entry = text.trim();
                                    if (entry.length > 0 && apps.indexOf(entry) === -1) {
                                        apps.push(entry);
                                        kcm.excludedApplications = apps;
                                    }
                                    text = "";
                                }
                            }
                        }

                        Button {
                            text: i18n("Add")
                            icon.name: "list-add"
                            enabled: appInput.text.trim().length > 0
                            onClicked: {
                                let apps = kcm.excludedApplications.slice();
                                let entry = appInput.text.trim();
                                if (entry.length > 0 && apps.indexOf(entry) === -1) {
                                    apps.push(entry);
                                    kcm.excludedApplications = apps;
                                }
                                appInput.text = "";
                            }
                        }

                    }

                    ListView {
                        id: appListView

                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(contentHeight, Kirigami.Units.gridUnit * 6)
                        Layout.minimumHeight: Kirigami.Units.gridUnit * 6
                        Layout.margins: Kirigami.Units.smallSpacing
                        clip: true
                        model: kcm.excludedApplications
                        interactive: false
                        Accessible.name: i18n("Excluded Applications")
                        Accessible.role: Accessible.List

                        Kirigami.PlaceholderMessage {
                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.gridUnit * 4
                            visible: parent.count === 0
                            text: i18n("No excluded applications")
                            explanation: i18n("Add application names above to exclude them from snapping and autotiling")
                        }

                        delegate: ItemDelegate {
                            required property string modelData
                            required property int index

                            width: ListView.view.width
                            highlighted: ListView.isCurrentItem
                            onClicked: appListView.currentIndex = index

                            contentItem: RowLayout {
                                Kirigami.Icon {
                                    source: "application-x-executable"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Label {
                                    text: modelData
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                ToolButton {
                                    icon.name: "edit-delete"
                                    onClicked: {
                                        let apps = kcm.excludedApplications.slice();
                                        apps.splice(index, 1);
                                        kcm.excludedApplications = apps;
                                    }
                                    Accessible.name: i18n("Remove %1", modelData)
                                }

                            }

                        }

                    }

                }

            }

        }

        // --- Excluded Window Classes ---
        Item {
            Layout.fillWidth: true
            implicitHeight: classesCard.implicitHeight

            Kirigami.Card {
                id: classesCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    text: i18n("Excluded Window Classes")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing

                        TextField {
                            id: classInput

                            Layout.fillWidth: true
                            placeholderText: i18n("Window class (e.g., org.kde.dolphin)")
                            onAccepted: {
                                if (text.length > 0) {
                                    let classes = kcm.excludedWindowClasses.slice();
                                    let entry = text.trim();
                                    if (entry.length > 0 && classes.indexOf(entry) === -1) {
                                        classes.push(entry);
                                        kcm.excludedWindowClasses = classes;
                                    }
                                    text = "";
                                }
                            }
                        }

                        Button {
                            text: i18n("Add")
                            icon.name: "list-add"
                            enabled: classInput.text.trim().length > 0
                            onClicked: {
                                let classes = kcm.excludedWindowClasses.slice();
                                let entry = classInput.text.trim();
                                if (entry.length > 0 && classes.indexOf(entry) === -1) {
                                    classes.push(entry);
                                    kcm.excludedWindowClasses = classes;
                                }
                                classInput.text = "";
                            }
                        }

                    }

                    ListView {
                        id: classListView

                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(contentHeight, Kirigami.Units.gridUnit * 6)
                        Layout.minimumHeight: Kirigami.Units.gridUnit * 6
                        Layout.margins: Kirigami.Units.smallSpacing
                        clip: true
                        model: kcm.excludedWindowClasses
                        interactive: false
                        Accessible.name: i18n("Excluded Window Classes")
                        Accessible.role: Accessible.List

                        Kirigami.PlaceholderMessage {
                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.gridUnit * 4
                            visible: parent.count === 0
                            text: i18n("No excluded window classes")
                            explanation: i18n("Add window classes above to exclude them from snapping and autotiling")
                        }

                        delegate: ItemDelegate {
                            required property string modelData
                            required property int index

                            width: ListView.view.width
                            highlighted: ListView.isCurrentItem
                            onClicked: classListView.currentIndex = index

                            contentItem: RowLayout {
                                Kirigami.Icon {
                                    source: "window"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Label {
                                    text: modelData
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    font: Kirigami.Theme.fixedWidthFont
                                    elide: Text.ElideRight
                                }

                                ToolButton {
                                    icon.name: "edit-delete"
                                    onClicked: {
                                        let classes = kcm.excludedWindowClasses.slice();
                                        classes.splice(index, 1);
                                        kcm.excludedWindowClasses = classes;
                                    }
                                    Accessible.name: i18n("Remove %1", modelData)
                                }

                            }

                        }

                    }

                }

            }

        }

    }

}
