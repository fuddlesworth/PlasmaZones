// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Custom exclusive button group with accent-colored active state.
 *
 * A row of options where one is selected at a time, with a sliding
 * accent background indicator. Matches the SettingsSwitch aesthetic.
 *
 * The caller owns currentIndex. A click only emits indexChanged, so bind
 * currentIndex to the state the handler writes and the selection follows.
 * Assigning it a plain value instead leaves the group inert, and writing to
 * it from here would sever the caller's binding on the first click, which
 * strands every later state change the caller pushes back.
 *
 * Usage:
 *   SettingsButtonGroup {
 *       model: [i18n("Snapping"), i18n("Tiling")]
 *       currentIndex: view.mode
 *       onIndexChanged: (index) => { view.mode = index; }
 *   }
 */
Row {
    id: root

    property var model: []
    property int currentIndex: 0

    signal indexChanged(int index)

    spacing: Kirigami.Units.smallSpacing / 2

    Repeater {
        model: root.model

        delegate: Rectangle {
            id: optionDelegate

            required property int index
            required property string modelData
            readonly property bool isActive: root.currentIndex === index

            width: optionMetrics.width + Kirigami.Units.largeSpacing * 2
            height: Kirigami.Units.gridUnit * 2
            radius: Kirigami.Units.smallSpacing
            color: {
                if (isActive)
                    return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2);

                if (optionMouse.containsMouse)
                    return Qt.tint(Kirigami.Theme.alternateBackgroundColor, Qt.alpha(Kirigami.Theme.hoverColor, 0.1));

                return Kirigami.Theme.alternateBackgroundColor;
            }
            border.width: optionDelegate.activeFocus ? 2 : 1
            border.color: optionDelegate.activeFocus ? Kirigami.Theme.focusColor : isActive ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4) : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            Accessible.role: Accessible.RadioButton
            Accessible.name: optionDelegate.modelData
            Accessible.checked: optionDelegate.isActive
            Accessible.focusable: true
            // Keyboard path matching WizardTemplateCard: each option is a Tab
            // stop, Return/Enter/Space selects, and focus shows through the
            // highlight border. Activation reuses the click path's same-index
            // guard so a re-select of the current option stays a no-op.
            activeFocusOnTab: true
            Keys.onReturnPressed: optionDelegate.activate()
            Keys.onEnterPressed: optionDelegate.activate()
            Keys.onSpacePressed: optionDelegate.activate()

            function activate() {
                if (root.currentIndex !== optionDelegate.index)
                    root.indexChanged(optionDelegate.index);
            }

            TextMetrics {
                id: optionMetrics

                text: optionDelegate.modelData
            }

            Label {
                anchors.centerIn: parent
                text: optionDelegate.modelData
                font.weight: Font.Normal
                opacity: optionDelegate.isActive ? 1 : 0.5

                Behavior on opacity {
                    PhosphorMotionAnimation {
                        profile: "widget.press"
                        durationOverride: Kirigami.Units.shortDuration * 1.5
                    }
                }
            }

            MouseArea {
                id: optionMouse

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                onClicked: {
                    // Move active focus to the clicked option so a previously
                    // keyboard-focused option doesn't keep the focus ring and
                    // the key handlers.
                    optionDelegate.forceActiveFocus();
                    optionDelegate.activate();
                }
            }

            Behavior on color {
                PhosphorMotionAnimation {
                    profile: "widget.press"
                    durationOverride: Kirigami.Units.shortDuration * 1.5
                }
            }

            Behavior on border.color {
                PhosphorMotionAnimation {
                    profile: "widget.press"
                    durationOverride: Kirigami.Units.shortDuration * 1.5
                }
            }

            Behavior on border.width {
                PhosphorMotionAnimation {
                    profile: "widget.press"
                    durationOverride: Kirigami.Units.shortDuration * 1.5
                }
            }
        }
    }
}
