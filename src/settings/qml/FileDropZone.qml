// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Drag-and-drop import target: a bordered band that highlights while
 *        a file drag hovers it and emits the dropped URL.
 *
 * One shared implementation for every drop-to-import affordance (shader
 * browser packs, layout / algorithm files, shader sets). The host supplies
 * the idle / hover copy and icons and handles the drop:
 *
 *   FileDropZone {
 *       Layout.fillWidth: true
 *       idleText: i18n("Drop a layout file here")
 *       hoverText: i18n("Release to import layout")
 *       onFileDropped: function (url) {
 *           settingsController.importLayout(settingsController.urlToLocalFile(url));
 *       }
 *   }
 *
 * The idle/hover icons and the icon size default to the document-import case
 * (the Layouts card); the shader browser overrides them. The height is an
 * `implicitHeight` default, which a host overrides through its own layout
 * (e.g. `Layout.preferredHeight`).
 *
 * The signal hands over the raw URL string — callers that need a local path
 * convert via `settingsController.urlToLocalFile`.
 */
Rectangle {
    id: root

    required property string idleText
    required property string hoverText
    property string idleIcon: "document-open"
    property string hoverIcon: "document-import"
    property real iconSize: Kirigami.Units.iconSizes.medium

    readonly property bool _highlight: dropArea.containsDrag

    /// First dropped URL, as a string (e.g. "file:///home/…/set.json").
    signal fileDropped(string url)

    Accessible.role: Accessible.Grouping
    Accessible.name: root.idleText
    implicitHeight: Kirigami.Units.gridUnit * 4
    radius: Kirigami.Units.smallSpacing
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false
    color: _highlight ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12) : Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: _highlight ? Kirigami.Theme.highlightColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

    RowLayout {
        anchors.centerIn: parent
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Icon {
            source: root._highlight ? root.hoverIcon : root.idleIcon
            implicitWidth: root.iconSize
            implicitHeight: root.iconSize
            color: root._highlight ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
        }

        Label {
            text: root._highlight ? root.hoverText : root.idleText
            color: root._highlight ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
            font.italic: !root._highlight
        }
    }

    DropArea {
        id: dropArea

        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function (drop) {
            var urls = drop.urls;
            if (!urls || urls.length === 0) {
                drop.accepted = false;
                return;
            }
            root.fileDropped(String(urls[0]));
            drop.accepted = true;
        }
    }
}
