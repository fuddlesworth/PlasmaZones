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
    // QColor can stringify HSL-derived values as an SVG-incompatible extended
    // hex color. Encode ordinary sRGB channels explicitly for the SVG renderer.
    readonly property string strokeColor: "#" + [color.r, color.g, color.b].map(channel => ("0" + Math.round(channel * 255).toString(16)).slice(-2)).join("")
    readonly property string drawing: {
        if (source === "phosphor-tray")
            return '<path d="M3 14 H8 L10 17 H14 L16 14 H21 V21 H3 Z M5 10 V4 H19 V10 M9 7 H15"/>';
        if (source === "cpu")
            return '<rect x="5" y="5" width="14" height="14" rx="3"/><rect x="9" y="9" width="6" height="6" rx="1"/><path d="M9 2 V5 M15 2 V5 M9 19 V22 M15 19 V22 M2 9 H5 M2 15 H5 M19 9 H22 M19 15 H22"/>';
        if (source === "video-card")
            return '<rect x="3" y="5" width="18" height="13" rx="2"/><circle cx="11" cy="11.5" r="3.5"/><path d="M2 3 V20 M7 18 V21 H16 V18 M17 8 H18 M17 12 H18"/>';
        if (source === "memory")
            return '<rect x="2" y="5" width="20" height="12" rx="1"/><path d="M6 8 V13 M10 8 V13 M14 8 V13 M18 8 V13 M5 17 V20 M9 17 V20 M13 17 V20 M17 17 V20 M21 17 V20"/>';
        if (source === "network-transfer")
            return '<path d="M7 3 V21 M3 7 L7 3 L11 7 M17 3 V21 M13 17 L17 21 L21 17"/>';
        if (source === "drive-harddisk")
            return '<path d="M6 3 H18 L22 14 V21 H2 V14 Z M2 14 H22 M17 18 H18"/>';
        if (source === "preferences-system")
            return '<path d="M3 7 H21 M3 17 H21"/><circle cx="8" cy="7" r="3" fill="none"/><circle cx="16" cy="17" r="3" fill="none"/>';
        if (source === "utilities-system-monitor")
            return '<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21 H16 M12 17 V21 M5 12 L8 9 L11 13 L15 7 L19 10"/>';
        if (source === "preferences-desktop-wallpaper" || source === "image-x-generic")
            return '<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="8" cy="8" r="1.5"/><path d="M3 17 L8 12 L12 16 L16 10 L21 17"/>';
        if (source === "view-split-left-right")
            return '<rect x="3" y="3" width="8" height="18" rx="2"/><rect x="15" y="3" width="6" height="7" rx="1.5"/><rect x="15" y="14" width="6" height="7" rx="1.5"/>';
        if (source === "view-grid")
            return '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>';
        if (source === "object-locked" || source === "object-unlocked")
            return '<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M9 10 V6 A3 3 0 0 1 15 6' + (source === "object-locked" ? ' V10' : '') + ' M12 14 V17"/>';
        if (source === "input-keyboard")
            return '<rect x="2" y="5" width="20" height="14" rx="3"/><path d="M5 9 H6 M9 9 H10 M13 9 H14 M17 9 H18 M5 12 H6 M9 12 H10 M13 12 H14 M17 12 H18 M7 15 H17"/>';
        if (source === "notifications")
            return '<path d="M4 17 H20 L18 14 V9 A6 6 0 0 0 6 9 V14 Z M10 21 H14"/>';
        if (source === "system-shutdown")
            return '<path d="M12 2 V12 M6 5 A9 9 0 1 0 18 5"/>';
        if (source === "arrow-right")
            return '<path d="M4 12 H20 M14 6 L20 12 L14 18"/>';
        if (source === "media-skip-backward")
            return '<path d="M17 6 L8 12 L17 18 Z M5 5 V19"/>';
        if (source === "media-skip-forward")
            return '<path d="M7 6 L16 12 L7 18 Z M19 5 V19"/>';
        if (source.indexOf("network-wireless") === 0)
            return '<path d="M3 8 Q12 1 21 8 M6 12 Q12 7 18 12 M9 16 Q12 13 15 16"/><circle cx="12" cy="20" r="0.7"/>';
        if (source.indexOf("bluetooth") === 0)
            return '<path d="M7 7 L17 17 L12 21 L12 3 L17 7 L7 17"/>';
        if (source.indexOf("brightness") === 0 || source === "preferences-desktop-theme")
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
        source: root.drawing ? "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><g fill="none" stroke="' + root.strokeColor + '" stroke-opacity="' + root.color.a + '" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' + root.drawing + '</g></svg>') : ""
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
