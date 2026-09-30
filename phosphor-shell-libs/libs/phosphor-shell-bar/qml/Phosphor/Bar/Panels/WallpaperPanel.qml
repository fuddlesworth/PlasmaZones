// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

QuickDetailFrame {
    id: root
    property string screenName: ""
    signal appearanceRequested(string page)
    title: i18n("Wallpaper")
    footerIcon: "preferences-desktop-wallpaper"
    footerText: i18n("A view of your own.")
    readonly property var assignments: AppearanceStore.values.wallpapers
    readonly property var assignment: assignments[screenName] || assignments[""] || ({})
    readonly property string imagePath: assignment.path || ""
    readonly property string imageName: imagePath ? imagePath.split("/").pop() : i18n("Current desktop")
    readonly property url imageUrl: imagePath ? "file://" + imagePath.split("/").map(part => encodeURIComponent(part)).join("/") : ""
    readonly property string fit: assignment.fit || "fill"
    DetailNotice {
        text: AppearanceStore.editing ? i18n("This is your Appearance preview. Apply it there to save your changes.") : ""
    }
    DetailCard {
        title: root.imageName
        titleSize: 16
        description: root.screenName ? i18n("Wallpaper on %1").arg(root.screenName) : i18n("Wallpaper for all displays")
        iconName: "preferences-desktop-wallpaper"
        status: root.fit === "fit" ? i18n("Fit") : root.fit === "stretch" ? i18n("Stretch") : root.fit === "center" ? i18n("Center") : i18n("Fill")
        Rectangle {
            width: parent.width
            height: width * 9 / 16
            radius: Appearance.radius * 0.5
            color: Appearance.recess
            border.width: 1
            border.color: Appearance.outline
            Image {
                id: preview
                anchors.fill: parent
                anchors.margins: 1
                source: root.imageUrl
                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                asynchronous: true
                fillMode: root.fit === "fit" ? Image.PreserveAspectFit : root.fit === "stretch" ? Image.Stretch : root.fit === "center" ? Image.Pad : Image.PreserveAspectCrop
                clip: true
                Accessible.name: i18n("Current wallpaper preview")
            }
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - Tokens.spacing_l * 2
                visible: !root.imagePath || preview.status === Image.Error
                ShellIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Tokens.spacing_xl
                    Layout.preferredHeight: Tokens.spacing_xl
                    source: "preferences-desktop-wallpaper"
                    isMask: true
                    color: Appearance.muted
                }
                DetailText {
                    Layout.fillWidth: true
                    text: root.imagePath ? i18n("This image is no longer available.") : i18n("Choose an image in Appearance.")
                    horizontalAlignment: Text.AlignHCenter
                    muted: true
                }
            }
        }
        ShellButton {
            objectName: "chooseWallpaper"
            text: i18n("Choose wallpaper")
            iconName: "folder-pictures"
            highlighted: true
            onClicked: root.appearanceRequested("wallpaper")
        }
    }
    DetailDeviceRow {
        title: i18n("Display wallpapers")
        subtitle: i18n("Use a shared wallpaper or choose an image for each display.")
        iconName: "video-display"
        onClicked: root.appearanceRequested("wallpaper")
    }
    DetailDeviceRow {
        title: i18n("Style settings")
        subtitle: i18n("Choose colors from your wallpaper in Appearance.")
        iconName: "preferences-desktop-theme"
        onClicked: root.appearanceRequested("style")
    }
}
