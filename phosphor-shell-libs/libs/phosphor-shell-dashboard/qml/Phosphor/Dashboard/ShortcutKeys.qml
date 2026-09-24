// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import Phosphor.Theme

Item {
    id: root
    property var bindings: []
    property color keyColor: Appearance.text
    readonly property real keyHeight: Math.max(24, metrics.height + 8)
    readonly property real naturalWidth: bindings.reduce((sum, binding, index) => sum + chordWidth(binding, index) + (index ? 6 : 0), 0)
    implicitWidth: naturalWidth
    implicitHeight: chords.implicitHeight
    Accessible.ignored: true
    function chordWidth(parts, index) {
        // advanceWidth() alone does not subscribe a binding to font changes.
        // Keep both chord and total widths in step with live text scaling.
        if (metrics.font.pixelSize <= 0)
            return 0;
        return parts.reduce((sum, part) => sum + Math.max(23, metrics.advanceWidth(part) + 10) + 3, index ? metrics.advanceWidth(qsTr("or")) + 7 : 0);
    }
    FontMetrics {
        id: metrics
        font.family: Tokens.font_family_mono
        font.pixelSize: Math.round(10 * Appearance.textScale)
    }
    Flow {
        id: chords
        width: root.width
        spacing: 6
        Repeater {
            model: root.bindings
            delegate: Flow {
                id: chord
                required property var modelData
                required property int index
                width: Math.min(root.width, root.chordWidth(modelData, index))
                height: childrenRect.height
                spacing: 3
                ShortcutText {
                    visible: chord.index > 0
                    width: visible ? implicitWidth + 4 : 0
                    height: root.keyHeight
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("or")
                    muted: true
                    size: 9
                }
                Repeater {
                    model: chord.modelData
                    delegate: Rectangle {
                        required property string modelData
                        width: Math.min(chord.width, Math.max(23, keyLabel.implicitWidth + 10))
                        height: root.keyHeight
                        radius: 5
                        color: Appearance.card
                        border.color: Qt.tint(Appearance.outline, Qt.alpha(Appearance.text, .13))
                        border.width: 1
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 1
                            height: 1
                            color: Qt.alpha(Appearance.text, .16)
                            radius: 1
                        }
                        Text {
                            id: keyLabel
                            anchors.fill: parent
                            anchors.margins: 3
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: parent.modelData
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font: metrics.font
                            color: root.keyColor
                        }
                    }
                }
            }
        }
    }
}
