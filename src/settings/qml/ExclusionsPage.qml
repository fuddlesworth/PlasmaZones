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

        // --- Filtering ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Filtering")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Exclude transient windows")
                    checked: kcm.excludeTransientWindows
                    onToggled: kcm.excludeTransientWindows = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Minimum window width:")
                    }

                    SpinBox {
                        from: 0
                        to: 1000
                        value: kcm.minimumWindowWidth
                        onValueModified: kcm.minimumWindowWidth = value
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Minimum window height:")
                    }

                    SpinBox {
                        from: 0
                        to: 1000
                        value: kcm.minimumWindowHeight
                        onValueModified: kcm.minimumWindowHeight = value
                    }

                }

            }

        }

        // --- Excluded Applications ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Excluded Applications")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: appInput

                        Layout.fillWidth: true
                        placeholderText: i18n("Application name or desktop ID...")
                        onAccepted: addAppButton.clicked()
                    }

                    Button {
                        id: addAppButton

                        text: i18n("Add")
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
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 200)
                    clip: true
                    model: kcm.excludedApplications

                    delegate: RowLayout {
                        width: ListView.view.width
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            Layout.fillWidth: true
                            text: modelData
                            elide: Text.ElideRight
                        }

                        Button {
                            icon.name: "edit-delete"
                            flat: true
                            onClicked: {
                                let apps = kcm.excludedApplications.slice();
                                apps.splice(index, 1);
                                kcm.excludedApplications = apps;
                            }
                        }

                    }

                }

            }

        }

        // --- Excluded Window Classes ---
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Excluded Window Classes")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: classInput

                        Layout.fillWidth: true
                        placeholderText: i18n("Window class name...")
                        onAccepted: addClassButton.clicked()
                    }

                    Button {
                        id: addClassButton

                        text: i18n("Add")
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
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 200)
                    clip: true
                    model: kcm.excludedWindowClasses

                    delegate: RowLayout {
                        width: ListView.view.width
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            Layout.fillWidth: true
                            text: modelData
                            elide: Text.ElideRight
                        }

                        Button {
                            icon.name: "edit-delete"
                            flat: true
                            onClicked: {
                                let classes = kcm.excludedWindowClasses.slice();
                                classes.splice(index, 1);
                                kcm.excludedWindowClasses = classes;
                            }
                        }

                    }

                }

            }

        }

    }

}
