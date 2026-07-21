// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "SearchAnchorHelpers.js" as SearchAnchors

/**
 * @brief The framed algorithm preview, its description and the algorithm
 * picker, shared by TilingAlgorithmPage and TilingSimplePage.
 *
 * Both surfaces show the same block: a bordered live preview of the selected
 * algorithm, an optional caption under it, the algorithm's description from
 * its script metadata, and the LayoutComboBox picker. The pages differ only
 * in where the layout inputs come from and what happens on a pick, so those
 * arrive as properties and the pick leaves through the algorithmActivated
 * signal.
 *
 * masterCount is gated on supportsMasterCount here rather than at each call
 * site, so both pages hand the C++ preview generator the same arguments for
 * the same algorithm.
 */
ColumnLayout {
    id: card

    /// Algorithm currently driving the preview.
    required property string algorithmId
    /// Display name from the algorithm's metadata, shown when showLabel is on.
    property string algorithmName: ""
    /// Description from the algorithm's metadata. Empty hides the label.
    property string description: ""
    /// Algorithm id the picker shows as current. Usually algorithmId, but the
    /// advanced page resolves it through its per-monitor scope first.
    required property string currentAlgorithmId
    /// Layout inputs fed to the preview generator.
    property int windowCount: 4
    property real splitRatio: 0.6
    property int masterCount: 1
    /// Whether the selected algorithm has a master area. When false the
    /// preview is given a master count of 0.
    property bool supportsMasterCount: false
    property var customParams: ({})
    property string zoneNumberDisplay: "all"
    /// Caption under the preview frame. Empty reserves no extra height.
    property string captionText: ""
    property int previewWidth: Kirigami.Units.gridUnit * 18
    property int previewHeight: Kirigami.Units.gridUnit * 10
    /// Deep-link reveal anchor for the picker. Empty = not addressable.
    property string searchAnchor: ""

    /// Emitted with the bare algorithm id (no "autotile:" prefix) when the
    /// user picks one. The hosting page decides what to persist. The id is
    /// empty when the combo's model rebuilt under the selection, which the
    /// page resolves against whatever it treats as the current algorithm.
    signal algorithmActivated(string algorithmId)

    spacing: Kirigami.Units.smallSpacing

    Component.onCompleted: {
        if (card.searchAnchor.length > 0)
            Qt.callLater(card._registerSearchAnchor);
    }
    Component.onDestruction: {
        if (card.searchAnchor.length > 0)
            card._unregisterSearchAnchor();
    }

    // Same deferred walk-up SettingsRow uses: SettingsCard reparents its
    // contentItem, so the chain to the page is only complete after
    // construction settles.
    function _registerSearchAnchor() {
        var pg = SearchAnchors.pageFor(card);
        if (pg)
            pg.registerSearchAnchor(card.searchAnchor, card, SearchAnchors.cardFor(card));
    }
    function _unregisterSearchAnchor() {
        var pg = SearchAnchors.pageFor(card);
        if (pg)
            pg.unregisterSearchAnchor(card.searchAnchor);
    }

    // Live preview, centered at top.
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: card.previewHeight + (card.captionText.length > 0 ? Kirigami.Units.gridUnit * 1.5 : 0)

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: card.previewWidth
            height: card.previewHeight

            Rectangle {
                anchors.fill: parent
                color: Kirigami.Theme.backgroundColor
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                border.width: 1
                radius: Kirigami.Units.smallSpacing

                AlgorithmPreview {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    appSettings: settingsController
                    showLabel: false
                    algorithmId: card.algorithmId
                    algorithmName: card.algorithmName
                    windowCount: card.windowCount
                    splitRatio: card.splitRatio
                    masterCount: card.supportsMasterCount ? card.masterCount : 0
                    customParams: card.customParams
                    zoneNumberDisplay: card.zoneNumberDisplay
                }
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: Kirigami.Units.smallSpacing
                text: card.captionText
                visible: text !== ""
                font: Kirigami.Theme.fixedWidthFont
                opacity: 0.7
            }
        }
    }

    // Algorithm description, from the script metadata.
    Label {
        Layout.fillWidth: true
        Layout.maximumWidth: card.previewWidth
        Layout.alignment: Qt.AlignHCenter
        text: card.description
        visible: text !== ""
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
    }

    // Algorithm picker, with preview thumbnails in the popup.
    ColumnLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Kirigami.Units.smallSpacing
        // Constant cap (not bound to parent.width) — binding a layout child's
        // max width to its enclosing layout's width feeds the child size back
        // into the same layout pass (recursive rearrange).
        Layout.maximumWidth: Kirigami.Units.gridUnit * 25

        LayoutComboBox {
            id: algorithmCombo

            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Accessible.name: i18n("Tiling algorithm")
            appSettings: settingsController
            showPreview: true
            layoutFilter: 1 // Autotile algorithms only
            showNoneOption: false
            currentLayoutId: "autotile:" + card.currentAlgorithmId
            onActivated: {
                // Strip the "autotile:" prefix the combo's ids carry. An empty
                // value passes straight through, so each page applies its own
                // fallback for a model that rebuilt under the selection.
                let selectedId = algorithmCombo.currentValue;
                if (selectedId.startsWith("autotile:"))
                    selectedId = selectedId.substring(9);
                card.algorithmActivated(selectedId);
            }
        }
    }
}
