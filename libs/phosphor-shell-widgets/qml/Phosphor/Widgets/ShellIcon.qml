// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Item {
    id: root
    property string source: ""
    property color color: Appearance.text
    property bool isMask: true
    property string fallback: "application-x-executable"
    implicitWidth: 19
    implicitHeight: 19
    readonly property string drawing: {
        if (source.indexOf("network-wireless") === 0)
            return '<path d="M3 8 Q12 1 21 8 M6 12 Q12 7 18 12 M9 16 Q12 13 15 16"/><circle cx="12" cy="20" r="0.7"/>';
        if (source.indexOf("bluetooth") === 0)
            return '<path d="M7 7 L17 17 L12 21 L12 3 L17 7 L7 17"/>';
        if (source.indexOf("brightness") === 0)
            return '<circle cx="12" cy="12" r="4"/><path d="M12 2 V4 M12 20 V22 M2 12 H4 M20 12 H22 M5 5 L6.5 6.5 M17.5 17.5 L19 19 M5 19 L6.5 17.5 M17.5 6.5 L19 5"/>';
        if (source.indexOf("audio-volume") === 0)
            return '<path d="M4 9 H8 L13 5 V19 L8 15 H4 Z"/>' + (source.indexOf("muted") >= 0 ? '<path d="M17 9 L22 15 M22 9 L17 15"/>' : '<path d="M17 8 Q21 12 17 16 M20 5 Q27 12 20 19"/>');
        if (source === "weather-clear-night")
            return '<path d="M20 15 A9 9 0 0 1 9 4 A9 9 0 1 0 20 15 Z"/>';
        if (source.indexOf("battery") === 0)
            return '<rect x="2" y="6" width="18" height="12" rx="3"/><path d="M23 10 V14 M6 9 V15 M10 9 V15 M14 9 V15"/>';
        if (source === "media-playback-pause")
            return '<path d="M9 7 V17 M15 7 V17"/>';
        if (source === "media-playback-start")
            return '<path d="M8 5 L19 12 L8 19 Z"/>';
        if (source.indexOf("go-next") === 0)
            return '<path d="M9 5 L16 12 L9 19"/>';
        if (source.indexOf("go-previous") === 0)
            return '<path d="M15 5 L8 12 L15 19"/>';
        if (source === "window-close")
            return '<path d="M7 7 L17 17 M17 7 L7 17"/>';
        if (source === "audio-x-generic")
            return '<path d="M9 18 V5 L20 3 V16 M9 8 L20 6"/><ellipse cx="6" cy="18" rx="3" ry="3"/><ellipse cx="17" cy="16" rx="3" ry="3"/>';
        if (source === "system-search")
            return '<circle cx="10" cy="10" r="7"/><path d="M15 15 L21 21"/>';
        if (source === "utilities-terminal")
            return '<rect x="2" y="3" width="20" height="18" rx="4"/><path d="M6 8 L10 12 L6 16 M14 16 H18"/>';
        if (source === "internet-web-browser")
            return '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12 H21"/>';
        if (source === "folder")
            return '<path d="M3 6 H10 L12 9 H21 V20 H3 Z M3 6 V4 H10 L12 6 H20 V9"/>';
        return "";
    }
    Image {
        anchors.fill: parent
        visible: root.drawing !== ""
        sourceSize.width: width * Screen.devicePixelRatio
        sourceSize.height: height * Screen.devicePixelRatio
        source: root.drawing ? "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><g fill="none" stroke="' + root.color + '" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' + root.drawing + '</g></svg>') : ""
    }
    Kirigami.Icon {
        anchors.fill: parent
        visible: root.drawing === ""
        source: visible ? root.source : ""
        color: root.color
        isMask: root.isMask
        fallback: root.fallback
    }
}
