// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Global settings search — header field + ranked results dropdown.
 *
 * Mounted in SettingsAppWindow's `headerExtras` slot. Binds the search field to
 * `searchController.query` and the dropdown to `searchController.results`;
 * selecting a result calls `settingsController.navigateTo(address)`, which
 * drills to the page and (for setting/section anchors) reveals the target.
 *
 * `searchOpen` exposes dropdown state so the host can suppress page-step
 * shortcuts while searching.
 */
Item {
    id: root

    readonly property bool searchOpen: resultsPopup.visible

    implicitWidth: Kirigami.Units.gridUnit * 18
    implicitHeight: field.implicitHeight

    function activate(index) {
        const list = searchController.results;
        if (index < 0 || index >= list.length)
            return;

        const entry = list[index];
        if (entry && entry.address) {
            settingsController.navigateTo(entry.address);
            root.dismiss();
        }
    }

    function dismiss() {
        field.text = "";
        searchController.query = "";
        resultsPopup.close();
    }

    Kirigami.SearchField {
        id: field

        anchors.fill: parent
        placeholderText: i18nc("@info:placeholder global settings search", "Search settings…")
        Accessible.name: i18n("Search settings")
        onTextChanged: searchDebounce.restart()
        Keys.onDownPressed: function (event) {
            if (resultsPopup.visible && resultsList.count > 0) {
                resultsList.currentIndex = 0;
                resultsList.forceActiveFocus();
                event.accepted = true;
            }
        }
        Keys.onEscapePressed: function (event) {
            root.dismiss();
            event.accepted = true;
        }
        onAccepted: root.activate(0)
    }

    // Debounce live typing before re-querying the index (matches the per-page
    // filters' 150 ms cadence).
    Timer {
        id: searchDebounce

        interval: 150
        onTriggered: {
            searchController.query = field.text;
            if (field.text.length > 0)
                resultsPopup.open();
            else
                resultsPopup.close();
        }
    }

    Popup {
        id: resultsPopup

        y: field.height + Kirigami.Units.smallSpacing
        x: 0
        width: Math.max(field.width, Kirigami.Units.gridUnit * 24)
        padding: Kirigami.Units.smallSpacing
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            ListView {
                id: resultsList

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 20)
                clip: true
                model: searchController.results
                visible: count > 0
                keyNavigationEnabled: true
                Keys.onReturnPressed: root.activate(currentIndex)
                Keys.onEnterPressed: root.activate(currentIndex)
                Keys.onEscapePressed: function (event) {
                    root.dismiss();
                    event.accepted = true;
                }
                Keys.onUpPressed: function (event) {
                    if (currentIndex <= 0) {
                        field.forceActiveFocus();
                        event.accepted = true;
                    } else {
                        decrementCurrentIndex();
                        event.accepted = true;
                    }
                }

                delegate: ItemDelegate {
                    id: resultDelegate

                    required property int index
                    required property var modelData

                    width: ListView.view ? ListView.view.width : implicitWidth
                    highlighted: ListView.isCurrentItem
                    Accessible.name: resultDelegate.modelData.subtitle.length > 0 ? i18nc("@info:whatsthis search result: title, breadcrumb", "%1, %2", resultDelegate.modelData.title, resultDelegate.modelData.subtitle) : resultDelegate.modelData.title
                    onClicked: root.activate(resultDelegate.index)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: resultDelegate.modelData.icon.length > 0 ? resultDelegate.modelData.icon : "settings-configure"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                text: resultDelegate.modelData.title
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: resultDelegate.modelData.subtitle
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // "Did you mean …" — clicking it re-runs the search with the
            // suggested term. Shown only on a zero-result query.
            ItemDelegate {
                Layout.fillWidth: true
                visible: resultsList.count === 0 && searchController.suggestion.length > 0
                text: i18nc("@action search did-you-mean suggestion", "Did you mean “%1”?", searchController.suggestion)
                icon.name: "edit-find"
                onClicked: {
                    field.text = searchController.suggestion;
                    searchDebounce.restart();
                }
            }

            // No results + no suggestion.
            Label {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                visible: field.text.length > 0 && resultsList.count === 0 && searchController.suggestion.length === 0
                text: i18nc("@info search empty state", "No matches for “%1”", field.text)
                color: Kirigami.Theme.disabledTextColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
