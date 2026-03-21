// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief On-Screen Display settings card for the General sub-KCM.
 *
 * Contains OSD toggle, style, and overlay display mode settings.
 *
 * Required properties:
 *   - kcm: the KCM backend object
 */
Item {
    id: osdRoot

    required property var kcm

    Layout.fillWidth: true
    implicitHeight: osdCard.implicitHeight

    Kirigami.Card {
        id: osdCard

        anchors.fill: parent

        header: Kirigami.Heading {
            level: 3
            text: i18n("On-Screen Display")
            padding: Kirigami.Units.smallSpacing
        }

        contentItem: Kirigami.FormLayout {
            CheckBox {
                id: showOsdCheckbox

                Kirigami.FormData.label: i18n("Layout switch:")
                text: i18n("Show OSD when switching layouts")
                checked: osdRoot.kcm.showOsdOnLayoutSwitch
                onToggled: osdRoot.kcm.showOsdOnLayoutSwitch = checked
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Keyboard navigation:")
                text: i18n("Show OSD when using keyboard navigation")
                checked: osdRoot.kcm.showNavigationOsd
                onToggled: osdRoot.kcm.showNavigationOsd = checked
            }

            WideComboBox {
                id: osdStyleCombo

                readonly property int osdStyleNone: 0
                readonly property int osdStyleText: 1
                readonly property int osdStylePreview: 2

                Kirigami.FormData.label: i18n("OSD style:")
                enabled: showOsdCheckbox.checked || osdRoot.kcm.showNavigationOsd
                currentIndex: Math.max(0, Math.min(osdRoot.kcm.osdStyle, 2))
                model: [i18n("None"), i18n("Text only"), i18n("Visual preview")]
                onActivated: (index) => {
                    osdRoot.kcm.osdStyle = index;
                }
                ToolTip.visible: hovered
                ToolTip.text: currentIndex === 0 ? i18n("No OSD shown. Enable layout switch or keyboard navigation above to show OSD.") : (currentIndex === 1 ? i18n("Show layout name as text only") : i18n("Show visual layout preview"))
            }

            WideComboBox {
                Kirigami.FormData.label: i18n("Overlay style:")
                currentIndex: Math.max(0, Math.min(osdRoot.kcm.overlayDisplayMode, 1))
                model: [i18n("Full zone highlight"), i18n("Compact preview")]
                onActivated: (index) => {
                    osdRoot.kcm.overlayDisplayMode = index;
                }
                ToolTip.visible: hovered
                ToolTip.text: currentIndex === 0 ? i18n("Highlight each zone as a full-size translucent rectangle while dragging") : i18n("Show a small layout thumbnail inside each zone while dragging")
            }

        }

    }

}
