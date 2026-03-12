// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    // Layout constants (previously from monolith's QtObject)
    readonly property int sliderPreferredWidth: 200
    readonly property int sliderValueLabelWidth: 40
    // Capture the context property so child components can access it
    readonly property var kcmModule: kcm

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════════
        // ANIMATIONS CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: animationsCard.implicitHeight

            Kirigami.Card {
                id: animationsCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Animations")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // Enable toggle
                    CheckBox {
                        id: animationsEnabledCheck

                        Layout.fillWidth: true
                        text: i18n("Smooth window geometry transitions")
                        checked: kcm.animationsEnabled
                        onToggled: kcm.animationsEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Animate windows when snapping to zones or tiling. Applies to both manual snapping and autotiling.")
                    }

                    // Easing curve editor with animated preview
                    EasingPreview {
                        id: easingPreview

                        Layout.fillWidth: true
                        Layout.maximumWidth: 500
                        Layout.alignment: Qt.AlignHCenter
                        curve: kcm.animationEasingCurve
                        animationDuration: kcm.animationDuration
                        previewEnabled: animationsEnabledCheck.checked
                        opacity: animationsEnabledCheck.checked ? 1 : 0.4
                        onCurveEdited: function(newCurve) {
                            kcm.animationEasingCurve = newCurve;
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    // Easing controls (extracted component)
                    EasingSettings {
                        Layout.fillWidth: true
                        kcm: root.kcmModule
                        constants: root
                        animationsEnabled: animationsEnabledCheck.checked
                        easingPreview: easingPreview
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // ON-SCREEN DISPLAY CARD (extracted component)
        // ═══════════════════════════════════════════════════════════════════════
        OsdCard {
            Layout.fillWidth: true
            kcm: root.kcmModule
        }

    }

}
