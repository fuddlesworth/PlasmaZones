// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief ComboBox with a popup that always fits the widest item text.
 *
 * The KDE desktop style's Menu-based popup binds its width to the ComboBox
 * width and ignores external popup.width overrides.  This component replaces
 * the popup with a plain T.Popup + ListView (the same approach used by
 * LayoutComboBox) so the dropdown is never truncated.
 */
ComboBox {
    id: root

    // Cached widest-item width — recalculated only when model or count changes.
    // Using a separate TextMetrics avoids the binding loop caused by the
    // _longestItemWidth → _metrics.text → advanceWidth → _longestItemWidth cycle.
    property real _longestItemWidth: 0

    function _recalcLongestWidth() {
        let maxW = 0;
        for (let i = 0; i < root.count; ++i) {
            _metrics.text = root.textAt(i) || "";
            maxW = Math.max(maxW, _metrics.advanceWidth);
        }
        _longestItemWidth = maxW;
    }

    implicitContentWidthPolicy: ComboBox.WidestTextWhenCompleted
    onCountChanged: Qt.callLater(_recalcLongestWidth)
    onModelChanged: Qt.callLater(_recalcLongestWidth)
    Component.onCompleted: _recalcLongestWidth()

    TextMetrics {
        id: _metrics

        font: root.font
    }

    popup: T.Popup {
        // Re-parent to the application Overlay so the popup escapes any
        // hosting popup (e.g. Kirigami.OverlaySheet) that would otherwise
        // out-z-order it. Position is mapped from the ComboBox's local
        // coordinates into the overlay so the dropdown still anchors under
        // the combo. The bumped `z` puts us above the sheet's own popup
        // stack — without it, the sheet's modal layer wins and the dropdown
        // renders behind the sheet content. Modal stays off so the host
        // window keeps focus and the sheet doesn't auto-close.
        // `popupType: Popup.Item` pins the popup to in-scene rendering so the
        // KDE / Plasma style can't auto-promote it to a top-level OS window
        // (the promotion behaviour differs between Qt 6.x style versions and
        // would re-trigger the focus-loss regression that closes the host
        // OverlaySheet the moment a real window grabs focus).
        popupType: T.Popup.Item
        parent: Overlay.overlay
        z: 999999
        // `modal: true` is required for close-on-press-outside to fire: the
        // popup must intercept the outside click for the close heuristic to
        // see it. The earlier "auto-close-on-focus-loss" regression came
        // from `popupType: Popup.Window` (which creates a real OS window
        // that steals focus); plain Popup-item modality keeps focus on the
        // host window so the hosting OverlaySheet stays open.
        // `dim: false` avoids painting a darkening rectangle over the rest
        // of the sheet — modal still blocks input to the sheet while the
        // dropdown is open, which is the desired ComboBox UX anyway.
        modal: true
        dim: false
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside | T.Popup.CloseOnPressOutsideParent
        // Guard on `parent` (the popup's effective parent — `Overlay.overlay`
        // once attached, null during early construction) rather than the
        // window. `mapToItem(null, …)` is undefined behaviour and can leave
        // the dropdown stuck at (0,0) on slow startup paths. The fallback
        // `y: root.height` mirrors the historical default-popup position so
        // first-frame layout pre-attach lines up under the combo.
        x: parent ? root.mapToItem(parent, 0, root.height).x : 0
        y: parent ? root.mapToItem(parent, 0, root.height).y : root.height
        width: Math.max(root.width, root._longestItemWidth + Kirigami.Units.gridUnit * 3)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (root.Window.window ? root.Window.window.height : 600) - topMargin - bottomMargin)
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            highlightMoveDuration: 0

            ScrollBar.vertical: ScrollBar {
            }

        }

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }

    }

    delegate: ItemDelegate {
        required property var modelData
        required property int index
        readonly property bool isCurrentSelection: root.currentIndex === index

        width: root.popup.availableWidth
        highlighted: root.highlightedIndex === index

        background: Rectangle {
            color: parent.highlighted ? Kirigami.Theme.highlightColor : parent.isCurrentSelection ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : Kirigami.Theme.backgroundColor
        }

        contentItem: Label {
            text: root.textRole ? modelData[root.textRole] : modelData
            color: parent.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            font.weight: (parent.highlighted || parent.isCurrentSelection) ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

    }

}
