// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ThemeHelpers.js" as Theme
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable notification banner with slide-in animation
 *
 * Provides a themed notification rectangle with icon, auto-dismiss timer,
 * optional close button, and slide-in/fade-out animations.
 * Used by EditorNotifications.qml for success and error banners.
 */
Rectangle {
    id: banner

    required property color accentColor
    required property string iconSource
    required property int dismissTimeout
    property bool showCloseButton: false
    property string accessibleRoleName: ""
    readonly property alias text: bannerLabel.text

    function show(message) {
        bannerLabel.text = message;
        hideAnim.stop();
        bannerTranslate.y = Kirigami.Units.smallSpacing; // Reset slide offset so animation replays
        showAnim.start();
        dismissTimer.restart();
    }

    function hide() {
        showAnim.stop();
        dismissTimer.stop();
        hideAnim.start();
    }

    height: bannerContent.implicitHeight + Kirigami.Units.gridUnit * 2
    visible: opacity > 0
    opacity: 0
    color: Theme.withAlpha(Kirigami.Theme.backgroundColor, 0.95)
    border.color: accentColor
    border.width: Math.round(Kirigami.Units.devicePixelRatio * 2)
    radius: Kirigami.Units.smallSpacing * Theme.radiusMultiplier
    z: 200
    Accessible.name: accessibleRoleName
    Accessible.description: bannerLabel.text || ""
    Accessible.role: Accessible.AlertMessage

    ColumnLayout {
        id: bannerContent

        anchors.fill: parent
        anchors.margins: Kirigami.Units.gridUnit
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.gridUnit

            Kirigami.Icon {
                source: banner.iconSource
                width: Kirigami.Units.iconSizes.medium
                height: Kirigami.Units.iconSizes.medium
                color: banner.accentColor
            }

            Label {
                id: bannerLabel

                Layout.fillWidth: true
                text: ""
                color: Kirigami.Theme.textColor
                wrapMode: Text.WordWrap
            }

            ToolButton {
                visible: banner.showCloseButton
                icon.name: "window-close"
                onClicked: {
                    dismissTimer.stop();
                    banner.hide();
                }
                ToolTip.text: i18nc("@tooltip", "Dismiss")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@action:button", "Dismiss notification")
                Accessible.role: Accessible.Button
            }

        }

    }

    Timer {
        id: dismissTimer

        interval: banner.dismissTimeout
        onTriggered: banner.hide()
    }

    ParallelAnimation {
        id: showAnim

        NumberAnimation {
            target: banner
            property: "opacity"
            to: 1
            duration: Theme.animDuration
            easing.type: Theme.animEasing
        }

        NumberAnimation {
            target: bannerTranslate
            property: "y"
            to: 0
            duration: Theme.animDuration
            easing.type: Theme.animEasing
        }

    }

    ParallelAnimation {
        id: hideAnim

        NumberAnimation {
            target: banner
            property: "opacity"
            to: 0
            duration: Theme.animDuration
            easing.type: Theme.animEasing
        }

        NumberAnimation {
            target: bannerTranslate
            property: "y"
            to: Kirigami.Units.smallSpacing
            duration: Theme.animDuration
            easing.type: Theme.animEasing
        }

    }

    transform: Translate {
        id: bannerTranslate

        y: Kirigami.Units.smallSpacing
    }

}
