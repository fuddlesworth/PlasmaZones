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
 * Results span both simple and advanced mode: a mode-specific result carries a
 * Simple/Advanced badge, and activating one the live mode does not show flips
 * `settingsController.advancedMode` before navigating.
 *
 * `searchOpen` exposes dropdown state so the host can suppress page-step
 * shortcuts while searching.
 */
Item {
    id: root

    readonly property bool searchOpen: resultsPopup.visible

    /// An action-kind result was selected. The host (Main.qml) dispatches
    /// the app-defined id — the search field never interprets it.
    signal actionTriggered(string actionId)

    implicitWidth: Kirigami.Units.gridUnit * 22
    implicitHeight: field.implicitHeight

    function activate(index) {
        const list = searchController.results;
        if (index < 0 || index >= list.length)
            return;

        const entry = list[index];
        if (!entry)
            return;

        // Action entries carry a command id instead of a navigable address.
        if (entry.actionId) {
            root.actionTriggered(entry.actionId);
            root.dismiss();
            return;
        }
        if (entry.address) {
            // A result the other mode owns (mode: 0 = both, 1 = simple-only,
            // 2 = advanced-only) switches the mode FIRST, so the page gate
            // cannot redirect the navigation away and an advanced-gated row
            // is actually rendered when the reveal fires. Main.qml persists
            // the flip via its advancedModeChanged connection.
            if (entry.mode === 1 && settingsController.advancedMode)
                settingsController.advancedMode = false;
            else if (entry.mode === 2 && !settingsController.advancedMode)
                settingsController.advancedMode = true;
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
        // Keep the field centered in both the placeholder and typing states so
        // the caret/query stay centered instead of jumping left on first input.
        horizontalAlignment: Text.AlignHCenter
        // Kirigami.SearchField auto-fires accepted() shortly after the text
        // changes by default — which would navigate to the top result as the
        // user types. Disable it so accepted() means "the user pressed Enter".
        autoAccept: false
        onTextChanged: searchDebounce.restart()
        // Tab first completes the query to the top result's title
        // (accept-best-match), keeping focus in the field; a second Tab (already
        // at the top title) steps into the dropdown so the results stay
        // tab-cyclable instead of focus escaping to the settings window.
        Keys.onTabPressed: function (event) {
            const list = searchController.results;
            const top = list.length > 0 ? list[0].title : "";
            if (top.length > 0 && top !== field.text) {
                field.text = top;
                field.cursorPosition = field.text.length;
                event.accepted = true;
            } else if (resultsPopup.visible && resultsList.count > 0) {
                resultsList.currentIndex = 0;
                resultsList.forceActiveFocus();
                event.accepted = true;
            } else {
                event.accepted = false;
            }
        }
        // Shift+Tab from the field enters the dropdown from the bottom (keeps the
        // results tab-cyclable in reverse rather than leaving to the window).
        Keys.onBacktabPressed: function (event) {
            if (resultsPopup.visible && resultsList.count > 0) {
                resultsList.currentIndex = resultsList.count - 1;
                resultsList.forceActiveFocus();
                event.accepted = true;
            } else {
                event.accepted = false;
            }
        }
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
        // The clear (X) button calls clear() then unconditionally fires
        // accepted(); clear() empties the text first, so an empty field here
        // means "cleared", not "Enter on a query" — don't navigate in that case.
        onAccepted: {
            if (field.text.length > 0)
                root.activate(0);
        }
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
        // Match the search field exactly so the dropdown's left/right edges line
        // up with the input rather than spilling past it.
        width: field.width
        padding: Kirigami.Units.smallSpacing
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // The View colorSet is pinned on the contentItem and background
        // individually, NOT on the Popup node: Kirigami's theme attachment
        // resolves through parentItem(), and a QQuickPopup's background /
        // contentItem parent to the internal popup item (→ Overlay.overlay),
        // so a pin on the Popup node never reaches them. Upstream
        // qqc2-desktop-style's ToolTip.qml uses this same per-item pattern.
        // The explicit background (mirroring the LayoutComboBox popup frame)
        // exists so the pin has a themed Rectangle to land on — the style's
        // default background can't carry attached properties from here.
        background: Rectangle {
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }

        contentItem: ColumnLayout {
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            spacing: Kirigami.Units.smallSpacing

            ListView {
                id: resultsList

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 20)
                clip: true
                model: searchController.results
                visible: count > 0
                keyNavigationEnabled: true

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

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
                // Tab/Shift+Tab cycle through results and wrap back to the field,
                // so focus stays within the search dropdown while it's open.
                Keys.onTabPressed: function (event) {
                    if (resultsList.currentIndex < resultsList.count - 1)
                        resultsList.incrementCurrentIndex();
                    else
                        field.forceActiveFocus();
                    event.accepted = true;
                }
                Keys.onBacktabPressed: function (event) {
                    if (resultsList.currentIndex > 0)
                        resultsList.decrementCurrentIndex();
                    else
                        field.forceActiveFocus();
                    event.accepted = true;
                }

                delegate: ItemDelegate {
                    id: resultDelegate

                    required property int index
                    required property var modelData

                    // Mode badge text for a result only one mode shows;
                    // empty for a result reachable in both modes.
                    readonly property string modeLabel: modelData.mode === 1 ? i18nc("@info search result mode badge", "Simple") : modelData.mode === 2 ? i18nc("@info search result mode badge", "Advanced") : ""

                    // Reserve the scrollbar's gutter so the row content ends
                    // at the scrollbar's left edge instead of running
                    // underneath it (mirrors the LayoutComboBox popup list).
                    width: ListView.view ? ListView.view.width - (resultsList.ScrollBar.vertical.visible ? resultsList.ScrollBar.vertical.width : 0) : implicitWidth
                    highlighted: ListView.isCurrentItem
                    topPadding: Kirigami.Units.smallSpacing
                    bottomPadding: Kirigami.Units.smallSpacing
                    // Title, then breadcrumb, then the mode badge — each
                    // appended only when present, so screen readers hear the
                    // same context the row shows.
                    Accessible.name: {
                        var parts = [resultDelegate.modelData.title];
                        if (resultDelegate.modelData.subtitle.length > 0)
                            parts.push(resultDelegate.modelData.subtitle);
                        if (resultDelegate.modeLabel.length > 0)
                            parts.push(resultDelegate.modeLabel);
                        return parts.join(i18nc("@info:whatsthis joiner between search-result context parts", ", "));
                    }
                    onClicked: root.activate(resultDelegate.index)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            source: resultDelegate.modelData.icon.length > 0 ? resultDelegate.modelData.icon : "settings-configure"
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                            // Recolors symbolic icons to track the (highlighted)
                            // text; full-color page icons ignore `color`.
                            color: resultDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                text: resultDelegate.modelData.title
                                elide: Text.ElideRight
                                color: resultDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: resultDelegate.modelData.subtitle
                                font: Kirigami.Theme.smallFont
                                color: resultDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                                opacity: resultDelegate.highlighted ? 0.85 : 1.0
                                elide: Text.ElideRight
                            }
                        }

                        // Mode badge: which mode owns this result. Activating
                        // it from the other mode switches automatically (see
                        // activate()), so the pill is information, not a
                        // warning.
                        Rectangle {
                            visible: resultDelegate.modeLabel.length > 0
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: modeBadgeLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
                            implicitHeight: modeBadgeLabel.implicitHeight + Kirigami.Units.smallSpacing
                            radius: height / 2
                            color: "transparent"
                            border.width: 1
                            border.color: resultDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor

                            Label {
                                id: modeBadgeLabel

                                anchors.centerIn: parent
                                text: resultDelegate.modeLabel
                                font: Kirigami.Theme.smallFont
                                color: resultDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
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
                visible: searchController.query.length > 0 && resultsList.count === 0 && searchController.suggestion.length === 0
                text: i18nc("@info search empty state", "No matches for “%1”", searchController.query)
                color: Kirigami.Theme.disabledTextColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
