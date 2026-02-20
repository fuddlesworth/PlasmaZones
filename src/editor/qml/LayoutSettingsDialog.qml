// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Layout-specific settings dialog
 *
 * Per-layout gap overrides: zone padding and edge gap can be set
 * independently of the global defaults from System Settings.
 */
Kirigami.Dialog {
    id: root

    required property var editorController

    title: i18nc("@title:window", "Layout Settings")
    standardButtons: Kirigami.Dialog.NoButton

    footer: Item {
        implicitHeight: footerLayout.implicitHeight + Kirigami.Units.largeSpacing * 2
        implicitWidth: footerLayout.implicitWidth

        RowLayout {
            id: footerLayout
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing

            Item { Layout.fillWidth: true }

            Button {
                text: i18nc("@action:button", "Apply")
                icon.name: "dialog-ok-apply"
                onClicked: root.close()
            }
        }
    }
    preferredWidth: Kirigami.Units.gridUnit * 22
    padding: Kirigami.Units.largeSpacing

    // Refresh checkbox/spinbox state every time the dialog opens,
    // so stale imperative assignments from Connections are overwritten.
    onOpened: {
        if (!root.editorController) return
        zonePaddingOverrideCheck.checked = root.editorController.hasZonePaddingOverride
        zonePaddingSpin.value = root.editorController.hasZonePaddingOverride
            ? root.editorController.zonePadding
            : root.editorController.globalZonePadding
        outerGapOverrideCheck.checked = root.editorController.hasOuterGapOverride
        outerGapSpin.value = root.editorController.hasOuterGapOverride
            ? root.editorController.outerGap
            : root.editorController.globalOuterGap
        fullScreenGeomCheck.checked = root.editorController.useFullScreenGeometry
    }

    // Sync UI state when values change externally (undo/redo, load layout)
    Connections {
        target: root.editorController
        enabled: root.editorController !== null && root.editorController !== undefined

        function onZonePaddingChanged() {
            zonePaddingOverrideCheck.checked = root.editorController.hasZonePaddingOverride
            zonePaddingSpin.value = root.editorController.hasZonePaddingOverride
                ? root.editorController.zonePadding
                : root.editorController.globalZonePadding
        }
        function onOuterGapChanged() {
            outerGapOverrideCheck.checked = root.editorController.hasOuterGapOverride
            outerGapSpin.value = root.editorController.hasOuterGapOverride
                ? root.editorController.outerGap
                : root.editorController.globalOuterGap
        }
        function onGlobalZonePaddingChanged() {
            if (!root.editorController.hasZonePaddingOverride)
                zonePaddingSpin.value = root.editorController.globalZonePadding
        }
        function onGlobalOuterGapChanged() {
            if (!root.editorController.hasOuterGapOverride)
                outerGapSpin.value = root.editorController.globalOuterGap
        }
        function onUseFullScreenGeometryChanged() {
            fullScreenGeomCheck.checked = root.editorController.useFullScreenGeometry
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.mediumSpacing

        // Description
        Label {
            text: i18nc("@info", "Per-layout overrides for this layout only.")
            wrapMode: Text.WordWrap
            opacity: 0.7
            Layout.fillWidth: true
            Layout.maximumWidth: root.preferredWidth - root.padding * 2
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ─── Spacing section ────────────────────────────────
        ColumnLayout {
            id: spacingSection
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            SectionHeader {
                title: i18nc("@title:group", "Spacing")
                icon: "format-indent-more"
            }

            Label {
                text: i18nc("@info", "Override global gap defaults from System Settings.")
                wrapMode: Text.WordWrap
                opacity: 0.7
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.maximumWidth: root.preferredWidth - root.padding * 2 - Kirigami.Units.largeSpacing
            }

            GridLayout {
                columns: 3
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.mediumSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.fillWidth: true

                // Row 1: Zone Padding
                CheckBox {
                    id: zonePaddingOverrideCheck
                    text: i18nc("@option:check", "Zone Padding")
                    checked: root.editorController ? root.editorController.hasZonePaddingOverride : false
                    Layout.fillWidth: true
                    onToggled: {
                        if (root.editorController) {
                            if (checked) {
                                root.editorController.zonePadding = root.editorController.globalZonePadding
                            } else {
                                root.editorController.clearZonePaddingOverride()
                            }
                        }
                    }
                }

                SpinBox {
                    id: zonePaddingSpin
                    from: 0
                    to: 100
                    value: root.editorController ? root.editorController.globalZonePadding : 0
                    enabled: zonePaddingOverrideCheck.checked
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    Accessible.name: i18nc("@label", "Zone padding value")
                    onValueModified: {
                        if (root.editorController && zonePaddingOverrideCheck.checked)
                            root.editorController.zonePadding = value
                    }
                }

                Label {
                    text: zonePaddingOverrideCheck.checked
                        ? i18nc("@label", "px")
                        : i18nc("@label showing global default", "px (global)")
                    opacity: zonePaddingOverrideCheck.checked ? 1.0 : 0.6
                    Layout.preferredWidth: implicitWidth
                }

                // Row 2: Edge Gap
                CheckBox {
                    id: outerGapOverrideCheck
                    text: i18nc("@option:check", "Edge Gap")
                    checked: root.editorController ? root.editorController.hasOuterGapOverride : false
                    Layout.fillWidth: true
                    onToggled: {
                        if (root.editorController) {
                            if (checked) {
                                root.editorController.outerGap = root.editorController.globalOuterGap
                            } else {
                                root.editorController.clearOuterGapOverride()
                            }
                        }
                    }
                }

                SpinBox {
                    id: outerGapSpin
                    from: 0
                    to: 100
                    value: root.editorController ? root.editorController.globalOuterGap : 0
                    enabled: outerGapOverrideCheck.checked
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    Accessible.name: i18nc("@label", "Edge gap value")
                    onValueModified: {
                        if (root.editorController && outerGapOverrideCheck.checked)
                            root.editorController.outerGap = value
                    }
                }

                Label {
                    text: outerGapOverrideCheck.checked
                        ? i18nc("@label", "px")
                        : i18nc("@label showing global default", "px (global)")
                    opacity: outerGapOverrideCheck.checked ? 1.0 : 0.6
                    Layout.preferredWidth: implicitWidth
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ─── Geometry section ────────────────────────────────
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            SectionHeader {
                title: i18nc("@title:group", "Geometry")
                icon: "view-fullscreen"
            }

            Label {
                text: i18nc("@info", "Control which screen area zones occupy.")
                wrapMode: Text.WordWrap
                opacity: 0.7
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.maximumWidth: root.preferredWidth - root.padding * 2 - Kirigami.Units.largeSpacing
            }

            CheckBox {
                id: fullScreenGeomCheck
                text: i18nc("@option:check", "Include area behind panels and taskbars")
                checked: root.editorController ? root.editorController.useFullScreenGeometry : false
                Layout.leftMargin: Kirigami.Units.largeSpacing
                onToggled: {
                    if (root.editorController)
                        root.editorController.useFullScreenGeometry = checked
                }
            }
        }
    }

}
