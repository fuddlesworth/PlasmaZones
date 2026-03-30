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
    preferredWidth: Kirigami.Units.gridUnit * 22
    padding: Kirigami.Units.largeSpacing
    // Refresh checkbox/spinbox state every time the dialog opens,
    // so stale imperative assignments from Connections are overwritten.
    onOpened: {
        if (!root.editorController)
            return ;

        zonePaddingOverrideCheck.checked = root.editorController.hasZonePaddingOverride;
        zonePaddingSpin.value = root.editorController.hasZonePaddingOverride ? root.editorController.zonePadding : root.editorController.globalZonePadding;
        outerGapOverrideCheck.checked = root.editorController.hasOuterGapOverride;
        outerGapSpin.value = root.editorController.hasOuterGapOverride ? root.editorController.outerGap : root.editorController.globalOuterGap;
        perSideOverrideCheck.checked = root.editorController.usePerSideOuterGap;
        perSideTopSpin.value = root.editorController.outerGapTop >= 0 ? root.editorController.outerGapTop : root.editorController.globalOuterGapTop;
        perSideBottomSpin.value = root.editorController.outerGapBottom >= 0 ? root.editorController.outerGapBottom : root.editorController.globalOuterGapBottom;
        perSideLeftSpin.value = root.editorController.outerGapLeft >= 0 ? root.editorController.outerGapLeft : root.editorController.globalOuterGapLeft;
        perSideRightSpin.value = root.editorController.outerGapRight >= 0 ? root.editorController.outerGapRight : root.editorController.globalOuterGapRight;
        fullScreenGeomCheck.checked = root.editorController.useFullScreenGeometry;
        aspectRatioCombo.currentIndex = root.editorController.aspectRatioClass;
        overlayDisplayModeOverrideCheck.checked = root.editorController.hasOverlayDisplayModeOverride;
        overlayDisplayModeCombo.currentIndex = Math.max(0, Math.min(root.editorController.hasOverlayDisplayModeOverride ? root.editorController.overlayDisplayMode : root.editorController.globalOverlayDisplayMode, 1));
    }

    // Sync UI state when values change externally (undo/redo, load layout)
    Connections {
        function onZonePaddingChanged() {
            zonePaddingOverrideCheck.checked = root.editorController.hasZonePaddingOverride;
            zonePaddingSpin.value = root.editorController.hasZonePaddingOverride ? root.editorController.zonePadding : root.editorController.globalZonePadding;
        }

        function onOuterGapChanged() {
            outerGapOverrideCheck.checked = root.editorController.hasOuterGapOverride;
            outerGapSpin.value = root.editorController.hasOuterGapOverride ? root.editorController.outerGap : root.editorController.globalOuterGap;
            perSideOverrideCheck.checked = root.editorController.usePerSideOuterGap;
            perSideTopSpin.value = root.editorController.outerGapTop >= 0 ? root.editorController.outerGapTop : root.editorController.globalOuterGapTop;
            perSideBottomSpin.value = root.editorController.outerGapBottom >= 0 ? root.editorController.outerGapBottom : root.editorController.globalOuterGapBottom;
            perSideLeftSpin.value = root.editorController.outerGapLeft >= 0 ? root.editorController.outerGapLeft : root.editorController.globalOuterGapLeft;
            perSideRightSpin.value = root.editorController.outerGapRight >= 0 ? root.editorController.outerGapRight : root.editorController.globalOuterGapRight;
        }

        function onGlobalZonePaddingChanged() {
            if (!root.editorController.hasZonePaddingOverride)
                zonePaddingSpin.value = root.editorController.globalZonePadding;

        }

        function onGlobalOuterGapChanged() {
            if (!root.editorController.hasOuterGapOverride)
                outerGapSpin.value = root.editorController.globalOuterGap;

            if (!root.editorController.hasPerSideOuterGapOverride) {
                perSideTopSpin.value = root.editorController.globalOuterGapTop;
                perSideBottomSpin.value = root.editorController.globalOuterGapBottom;
                perSideLeftSpin.value = root.editorController.globalOuterGapLeft;
                perSideRightSpin.value = root.editorController.globalOuterGapRight;
            }
        }

        function onOverlayDisplayModeChanged() {
            overlayDisplayModeOverrideCheck.checked = root.editorController.hasOverlayDisplayModeOverride;
            overlayDisplayModeCombo.currentIndex = Math.max(0, Math.min(root.editorController.hasOverlayDisplayModeOverride ? root.editorController.overlayDisplayMode : root.editorController.globalOverlayDisplayMode, 1));
        }

        function onGlobalOverlayDisplayModeChanged() {
            if (!root.editorController.hasOverlayDisplayModeOverride)
                overlayDisplayModeCombo.currentIndex = Math.max(0, Math.min(root.editorController.globalOverlayDisplayMode, 1));

        }

        function onUseFullScreenGeometryChanged() {
            fullScreenGeomCheck.checked = root.editorController.useFullScreenGeometry;
        }

        function onAspectRatioClassChanged() {
            aspectRatioCombo.currentIndex = root.editorController.aspectRatioClass;
        }

        target: root.editorController
        enabled: root.editorController !== null && root.editorController !== undefined
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
                            if (checked)
                                root.editorController.zonePadding = root.editorController.globalZonePadding;
                            else
                                root.editorController.clearZonePaddingOverride();
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
                            root.editorController.zonePadding = value;

                    }
                }

                Label {
                    text: zonePaddingOverrideCheck.checked ? i18nc("@label", "px") : i18nc("@label showing global default", "px (global)")
                    opacity: zonePaddingOverrideCheck.checked ? 1 : 0.6
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
                            if (checked)
                                root.editorController.outerGap = root.editorController.globalOuterGap;
                            else
                                root.editorController.clearOuterGapOverride();
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
                            root.editorController.outerGap = value;

                    }
                }

                Label {
                    text: outerGapOverrideCheck.checked ? i18nc("@label", "px") : i18nc("@label showing global default", "px (global)")
                    opacity: outerGapOverrideCheck.checked ? 1 : 0.6
                    Layout.preferredWidth: implicitWidth
                }

            }

            // Per-side edge gap override
            CheckBox {
                id: perSideOverrideCheck

                text: i18nc("@option:check", "Set per side")
                checked: root.editorController ? root.editorController.usePerSideOuterGap : false
                enabled: outerGapOverrideCheck.checked
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                onToggled: {
                    if (root.editorController) {
                        root.editorController.usePerSideOuterGap = checked;
                        if (checked) {
                            // Initialize per-side values from uniform gap if not set
                            let uniformGap = root.editorController.outerGap >= 0 ? root.editorController.outerGap : root.editorController.globalOuterGap;
                            if (root.editorController.outerGapTop < 0)
                                root.editorController.outerGapTop = uniformGap;

                            if (root.editorController.outerGapBottom < 0)
                                root.editorController.outerGapBottom = uniformGap;

                            if (root.editorController.outerGapLeft < 0)
                                root.editorController.outerGapLeft = uniformGap;

                            if (root.editorController.outerGapRight < 0)
                                root.editorController.outerGapRight = uniformGap;

                        }
                    }
                }
            }

            GridLayout {
                columns: 6
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                visible: perSideOverrideCheck.checked && outerGapOverrideCheck.checked
                Layout.leftMargin: Kirigami.Units.largeSpacing * 2

                Label {
                    text: i18nc("@label edge gap direction", "Top")
                }

                SpinBox {
                    id: perSideTopSpin

                    from: 0
                    to: 100
                    value: root.editorController ? (root.editorController.outerGapTop >= 0 ? root.editorController.outerGapTop : root.editorController.globalOuterGapTop) : 8
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    Accessible.name: i18nc("@label", "Top edge gap override")
                    onValueModified: {
                        if (root.editorController)
                            root.editorController.outerGapTop = value;

                    }
                }

                Label {
                    text: i18nc("@label", "px")
                }

                Label {
                    text: i18nc("@label edge gap direction", "Bottom")
                }

                SpinBox {
                    id: perSideBottomSpin

                    from: 0
                    to: 100
                    value: root.editorController ? (root.editorController.outerGapBottom >= 0 ? root.editorController.outerGapBottom : root.editorController.globalOuterGapBottom) : 8
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    Accessible.name: i18nc("@label", "Bottom edge gap override")
                    onValueModified: {
                        if (root.editorController)
                            root.editorController.outerGapBottom = value;

                    }
                }

                Label {
                    text: i18nc("@label", "px")
                }

                Label {
                    text: i18nc("@label edge gap direction", "Left")
                }

                SpinBox {
                    id: perSideLeftSpin

                    from: 0
                    to: 100
                    value: root.editorController ? (root.editorController.outerGapLeft >= 0 ? root.editorController.outerGapLeft : root.editorController.globalOuterGapLeft) : 8
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    Accessible.name: i18nc("@label", "Left edge gap override")
                    onValueModified: {
                        if (root.editorController)
                            root.editorController.outerGapLeft = value;

                    }
                }

                Label {
                    text: i18nc("@label", "px")
                }

                Label {
                    text: i18nc("@label edge gap direction", "Right")
                }

                SpinBox {
                    id: perSideRightSpin

                    from: 0
                    to: 100
                    value: root.editorController ? (root.editorController.outerGapRight >= 0 ? root.editorController.outerGapRight : root.editorController.globalOuterGapRight) : 8
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    Accessible.name: i18nc("@label", "Right edge gap override")
                    onValueModified: {
                        if (root.editorController)
                            root.editorController.outerGapRight = value;

                    }
                }

                Label {
                    text: i18nc("@label", "px")
                }

            }

        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ─── Overlay section ────────────────────────────────
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            SectionHeader {
                title: i18nc("@title:group", "Overlay")
                icon: "view-grid"
            }

            Label {
                text: i18nc("@info", "Override the global overlay style for this layout.")
                wrapMode: Text.WordWrap
                opacity: 0.7
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.maximumWidth: root.preferredWidth - root.padding * 2 - Kirigami.Units.largeSpacing
            }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.mediumSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.fillWidth: true

                CheckBox {
                    id: overlayDisplayModeOverrideCheck

                    text: i18nc("@option:check", "Overlay Style")
                    checked: root.editorController ? root.editorController.hasOverlayDisplayModeOverride : false
                    Layout.fillWidth: true
                    onToggled: {
                        if (root.editorController) {
                            if (checked)
                                root.editorController.overlayDisplayMode = root.editorController.globalOverlayDisplayMode;
                            else
                                root.editorController.clearOverlayDisplayModeOverride();
                        }
                    }
                }

                ComboBox {
                    id: overlayDisplayModeCombo

                    model: [i18nc("@item:inlistbox", "Full zone highlight"), i18nc("@item:inlistbox", "Compact preview")]
                    currentIndex: {
                        if (!root.editorController)
                            return 0;

                        let mode = overlayDisplayModeOverrideCheck.checked ? root.editorController.overlayDisplayMode : root.editorController.globalOverlayDisplayMode;
                        return Math.max(0, Math.min(mode, 1));
                    }
                    enabled: overlayDisplayModeOverrideCheck.checked
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                    Accessible.name: i18nc("@label", "Overlay display mode")
                    onActivated: (index) => {
                        if (root.editorController && overlayDisplayModeOverrideCheck.checked)
                            root.editorController.overlayDisplayMode = index;

                    }
                }

            }

        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ─── Aspect Ratio section ────────────────────────────────
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            SectionHeader {
                title: i18nc("@title:group", "Aspect Ratio")
                icon: "transform-crop"
            }

            Label {
                text: i18nc("@info", "Tag this layout for a specific monitor type. Layouts tagged with an aspect ratio class are suggested only on matching screens.")
                wrapMode: Text.WordWrap
                opacity: 0.7
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.maximumWidth: root.preferredWidth - root.padding * 2 - Kirigami.Units.largeSpacing
            }

            RowLayout {
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18nc("@label", "Target:")
                }

                ComboBox {
                    id: aspectRatioCombo

                    model: [i18nc("@item:inlistbox aspect ratio class", "Any"), i18nc("@item:inlistbox aspect ratio class", "Standard (16:9)"), i18nc("@item:inlistbox aspect ratio class", "Ultrawide (21:9)"), i18nc("@item:inlistbox aspect ratio class", "Super-Ultrawide (32:9)"), i18nc("@item:inlistbox aspect ratio class", "Portrait (9:16)")]
                    currentIndex: root.editorController ? root.editorController.aspectRatioClass : 0
                    Layout.fillWidth: true
                    Accessible.name: i18nc("@label", "Aspect ratio class")
                    Accessible.description: i18nc("@info:accessibility", "Set which screen aspect ratio this layout is designed for")
                    onActivated: (index) => {
                        if (root.editorController)
                            root.editorController.aspectRatioClass = index;

                    }
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
                        root.editorController.useFullScreenGeometry = checked;

                }
            }

        }

    }

    footer: Item {
        implicitHeight: footerLayout.implicitHeight + Kirigami.Units.largeSpacing * 2
        implicitWidth: footerLayout.implicitWidth

        RowLayout {
            id: footerLayout

            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: i18nc("@action:button", "Apply")
                icon.name: "dialog-ok-apply"
                onClicked: root.close()
            }

        }

    }

}
