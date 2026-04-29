// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "EasingCurve.js" as EC
import "SpringPhysics.js" as SP

Dialog {
    id: root

    property string eventLabel: ""
    property int timingMode: 0
    property string easingCurve: "0.33,1.00,0.68,1.00"
    property real springOmega: 12.0
    property real springZeta: 0.8

    signal curveApplied(string curve)
    signal springApplied(real omega, real zeta)

    title: eventLabel.length > 0 ? i18n("Edit Curve — %1", eventLabel) : i18n("Edit Curve")
    modal: true
    standardButtons: Dialog.Apply | Dialog.Cancel
    width: Math.min(parent.width * 0.9, Kirigami.Units.gridUnit * 28)

    property string _workingCurve: easingCurve
    property real _workingOmega: springOmega
    property real _workingZeta: springZeta

    onAboutToShow: {
        _workingCurve = easingCurve;
        _workingOmega = springOmega;
        _workingZeta = springZeta;
    }

    onApplied: {
        if (timingMode === 0)
            curveApplied(_workingCurve);
        else
            springApplied(_workingOmega, _workingZeta);
        close();
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        TabBar {
            id: modeTab

            Layout.fillWidth: true
            currentIndex: root.timingMode

            TabButton {
                text: i18n("Easing")
            }
            TabButton {
                text: i18n("Spring")
            }

            onCurrentIndexChanged: root.timingMode = currentIndex
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: modeTab.currentIndex

            // ─── Easing tab ───
            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CurveThumbnail {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 240
                    implicitHeight: 140
                    curve: root._workingCurve
                    timingMode: 0
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Preset:")
                    }

                    ComboBox {
                        id: easingPreset

                        Layout.fillWidth: true
                        model: [i18n("OutCubic (default)"), i18n("Linear"), i18n("OutQuad"), i18n("InOutCubic"), i18n("OutBack"), i18n("OutExpo")]

                        readonly property var presetCurves: ["0.33,1.00,0.68,1.00", "0.00,0.00,1.00,1.00", "0.25,0.46,0.45,0.94", "0.65,0.05,0.36,1.00", "0.18,0.89,0.32,1.28", "0.19,1.00,0.22,1.00"]

                        onActivated: {
                            root._workingCurve = presetCurves[currentIndex];
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Curve: %1", root._workingCurve)
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }
            }

            // ─── Spring tab ───
            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SpringPreview {
                    Layout.fillWidth: true
                    omega: root._workingOmega
                    zeta: root._workingZeta
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Preset:")
                    }

                    ComboBox {
                        id: springPreset

                        Layout.fillWidth: true
                        model: [i18n("Snappy"), i18n("Smooth"), i18n("Bouncy"), i18n("Custom")]

                        onActivated: {
                            if (currentIndex === 0) {
                                root._workingOmega = 18.0;
                                root._workingZeta = 0.8;
                            } else if (currentIndex === 1) {
                                root._workingOmega = 10.0;
                                root._workingZeta = 1.0;
                            } else if (currentIndex === 2) {
                                root._workingOmega = 14.0;
                                root._workingZeta = 0.4;
                            }
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Frequency (ω)")
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.1
                        to: 200.0
                        value: root._workingOmega
                        onMoved: {
                            root._workingOmega = value;
                            springPreset.currentIndex = 3;
                        }
                    }

                    Label {
                        text: i18n("Damping (ζ)")
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.0
                        to: 10.0
                        value: root._workingZeta
                        onMoved: {
                            root._workingZeta = value;
                            springPreset.currentIndex = 3;
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("ω = %1, ζ = %2", root._workingOmega.toFixed(1), root._workingZeta.toFixed(2))
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }
    }
}
