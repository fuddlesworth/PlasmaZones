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

    readonly property real _longestItemWidth: {
        let maxW = 0;
        let role = root.textRole || "";
        for (let i = 0; i < root.count; ++i) {
            _metrics.text = root.textAt(i) || "";
            maxW = Math.max(maxW, _metrics.advanceWidth);
        }
        return maxW;
    }

    implicitContentWidthPolicy: ComboBox.WidestTextWhenCompleted

    TextMetrics {
        id: _metrics

        font: root.font
    }

    popup: T.Popup {
        y: root.height
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
            font.bold: parent.highlighted || parent.isCurrentSelection
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

    }

}
