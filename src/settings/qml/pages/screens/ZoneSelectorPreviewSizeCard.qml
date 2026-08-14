// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Preview-size card for a zone selector variant (live preview,
 * Auto/Small/Medium/Large/Custom presets, custom slider).
 *
 * Shared by the snapping selector and the scrolling strip selector — the
 * host supplies effective values, write callbacks, and scope method names,
 * exactly like ZoneSelectorPositionCard.
 */
Item {
    id: root

    required property var controller
    required property QtObject constants
    required property bool featureEnabled
    required property real safeAspectRatio
    required property int effectiveSizeMode
    required property int effectivePreviewWidth
    required property int effectivePreviewHeight
    /// The host's PerScreenOverrideHelper (scope-switch reevaluation).
    required property var scopeHelper
    required property string scopeHasOverridesMethod
    required property string scopeClearerMethod
    /// function(mode): writes the SizeMode setting (0 Auto, 1 Fixed).
    required property var writeSizeMode
    /// function(presetWidth): writes PreviewWidth plus the aspect-derived,
    /// clamped PreviewHeight (the host owns the clamp bounds).
    required property var writePreviewSize

    Layout.fillWidth: true
    implicitHeight: previewCard.implicitHeight

    SettingsCard {
        id: previewCard

        anchors.fill: parent
        enabled: root.featureEnabled
        headerText: i18n("Preview size")
        searchAnchor: "previewSize"
        collapsible: true
        scopeEnabled: true
        scopeAppSettings: root.controller
        scopeHasOverridesMethod: root.scopeHasOverridesMethod
        scopeClearerMethod: root.scopeClearerMethod

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            // Live preview - centered
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.effectivePreviewHeight + Kirigami.Units.gridUnit * 3

                // Preview container
                Item {
                    id: sizePreviewContainer

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    width: root.effectivePreviewWidth
                    height: root.effectivePreviewHeight

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        radius: Kirigami.Units.smallSpacing * 1.5
                        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)
                        border.width: 1

                        // Sample zones. The small integer literals below
                        // (2px inset, 1px inter-zone gap, 2px corner radius,
                        // 1px borders, the 20px label-visibility threshold)
                        // are intentional fixed drawing dimensions of this
                        // miniature preview — diagram detail, not layout
                        // spacing, so they are deliberately not theme-scaled.
                        Row {
                            anchors.fill: parent
                            anchors.margins: 2
                            spacing: 1

                            Repeater {
                                model: 3

                                Rectangle {
                                    width: (parent.width - 2) / 3
                                    height: parent.height
                                    radius: 2
                                    color: Kirigami.Theme.backgroundColor
                                    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                                    border.width: 1

                                    Label {
                                        anchors.centerIn: parent
                                        text: (index + 1).toString()
                                        font.pixelSize: Math.min(parent.width, parent.height) * 0.3
                                        font.weight: Font.DemiBold
                                        color: Kirigami.Theme.textColor
                                        opacity: 0.5
                                        visible: parent.width >= 20
                                    }
                                }
                            }
                        }
                    }

                    // Size label
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.bottom
                        anchors.topMargin: Kirigami.Units.smallSpacing
                        text: i18nc("@info preview dimensions in pixels", "%1 × %2 px", root.effectivePreviewWidth, root.effectivePreviewHeight)
                        font: Kirigami.Theme.fixedWidthFont
                        opacity: 0.7
                    }
                }
            }

            // Size selection - segmented button style
            RowLayout {
                id: sizeButtonRow

                // Track explicit Custom mode selection
                property bool customModeActive: false
                // Track which size is selected
                // 0=Auto, 1=Small, 2=Medium, 3=Large, 4=Custom. A preset is
                // considered selected when the width is within half a slider
                // step (stepSize 10 → 5px) of that preset's value.
                readonly property int presetMatchTolerance: 5
                property int selectedSize: {
                    if (root.effectiveSizeMode === 0)
                        return 0; // Auto

                    if (customModeActive)
                        return 4; // Explicit Custom selection

                    var w = root.effectivePreviewWidth;
                    if (Math.abs(w - root.constants.zoneSelectorPreviewSmall) <= presetMatchTolerance)
                        return 1; // Small

                    if (Math.abs(w - root.constants.zoneSelectorPreviewMedium) <= presetMatchTolerance)
                        return 2; // Medium

                    if (Math.abs(w - root.constants.zoneSelectorPreviewLarge) <= presetMatchTolerance)
                        return 3; // Large

                    return 4;
                }

                Layout.alignment: Qt.AlignHCenter
                spacing: 0

                // The same exclusive-group semantics SettingsButtonGroup and
                // PositionPicker publish: without the roles below this row is
                // five unrelated push buttons to a screen reader, with no way
                // to tell which size is active or that the choice is
                // exclusive. (Kept in place rather than ported to
                // SettingsButtonGroup: the Custom-mode latch above owns
                // selection state in a way that group's
                // caller-owns-currentIndex contract does not model.)
                Accessible.role: Accessible.Grouping
                Accessible.name: i18n("Preview size")

                // RTL-aware arrow walk between the options (children holds
                // exactly the five Buttons; Connections are not Items).
                function focusStep(delta) {
                    const step = Qt.application.layoutDirection === Qt.RightToLeft ? -delta : delta;
                    for (var i = 0; i < children.length; ++i) {
                        if (children[i].activeFocus) {
                            children[(i + step + children.length) % children.length].forceActiveFocus();
                            return;
                        }
                    }
                }

                // Drop the explicit Custom selection when the newly resolved
                // size no longer warrants it: a scope switch or an external
                // width change can land on a preset width (or Auto mode), and
                // a stale Custom highlight would otherwise persist. A size
                // that is genuinely custom keeps the Custom chip highlighted.
                // Suppresses the reevaluation queued by our own Custom click:
                // switching Auto → Custom changes effectivePreviewWidth (Auto's
                // derived width gives way to the stored one), and without the
                // flag that self-inflicted change would instantly clear the
                // selection the user just made.
                property bool _selfWrite: false

                function reevaluateCustomMode(fromScopeSwitch) {
                    if (!customModeActive)
                        return;

                    if (_selfWrite)
                        return;

                    if (customSizeSlider.pressed)
                        return;

                    // The activeFocus guard keeps a keyboard-driven Custom
                    // edit from being yanked away, but the slider retains
                    // activeFocus after a finished drag — so it must not
                    // suppress the scope-switch path, or switching monitors
                    // would keep a stale Custom chip for the new scope.
                    if (!fromScopeSwitch && customSizeSlider.activeFocus)
                        return;

                    if (root.effectiveSizeMode !== 1) {
                        customModeActive = false;
                        return;
                    }
                    var w = root.effectivePreviewWidth;
                    if (Math.abs(w - root.constants.zoneSelectorPreviewSmall) <= presetMatchTolerance || Math.abs(w - root.constants.zoneSelectorPreviewMedium) <= presetMatchTolerance || Math.abs(w - root.constants.zoneSelectorPreviewLarge) <= presetMatchTolerance)
                        customModeActive = false;
                }

                // Scope-switch reevaluation. A distinct function (not
                // reevaluateCustomMode with an argument) because Qt.callLater
                // coalesces by function: the width-change path below queues
                // the plain variant, and a shared function would let the
                // last-queued call's arguments win, dropping the flag.
                function reevaluateAfterScopeSwitch() {
                    reevaluateCustomMode(true);
                }

                // Monitor scope switch: the effective* properties re-resolve
                // against the new scope's overrides. Deferred via callLater so
                // the re-resolved values are in place before the check runs
                // (signal-handler vs binding-refresh order is not guaranteed).
                // Runs with the scope-switch flag: only the pressed guard
                // applies, not the lingering-activeFocus one.
                Connections {
                    target: root.scopeHelper
                    function onSelectedScreenNameChanged() {
                        Qt.callLater(sizeButtonRow.reevaluateAfterScopeSwitch);
                    }
                }

                // External width change (override cleared, another client
                // writing the setting). The slider pressed/activeFocus guards
                // inside reevaluateCustomMode keep a live Custom drag or
                // keyboard edit that passes through a preset width from
                // yanking the slider away.
                Connections {
                    target: root
                    function onEffectivePreviewWidthChanged() {
                        Qt.callLater(sizeButtonRow.reevaluateCustomMode);
                    }
                }

                Button {
                    text: i18n("Auto")
                    flat: sizeButtonRow.selectedSize !== 0
                    highlighted: sizeButtonRow.selectedSize === 0
                    onClicked: {
                        sizeButtonRow.customModeActive = false;
                        root.writeSizeMode(0);
                    }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: text
                    Accessible.checkable: true
                    Accessible.checked: highlighted
                    Accessible.onPressAction: clicked()
                    Keys.onLeftPressed: sizeButtonRow.focusStep(-1)
                    Keys.onRightPressed: sizeButtonRow.focusStep(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.text: i18n("Size scales automatically with your screen resolution")
                }

                Button {
                    text: i18n("Small")
                    flat: sizeButtonRow.selectedSize !== 1
                    highlighted: sizeButtonRow.selectedSize === 1
                    onClicked: {
                        sizeButtonRow.customModeActive = false;
                        root.writeSizeMode(1);
                        root.writePreviewSize(root.constants.zoneSelectorPreviewSmall);
                    }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: text
                    Accessible.checkable: true
                    Accessible.checked: highlighted
                    Accessible.onPressAction: clicked()
                    Keys.onLeftPressed: sizeButtonRow.focusStep(-1)
                    Keys.onRightPressed: sizeButtonRow.focusStep(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.text: i18n("%1px width", root.constants.zoneSelectorPreviewSmall)
                }

                Button {
                    text: i18n("Medium")
                    flat: sizeButtonRow.selectedSize !== 2
                    highlighted: sizeButtonRow.selectedSize === 2
                    onClicked: {
                        sizeButtonRow.customModeActive = false;
                        root.writeSizeMode(1);
                        root.writePreviewSize(root.constants.zoneSelectorPreviewMedium);
                    }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: text
                    Accessible.checkable: true
                    Accessible.checked: highlighted
                    Accessible.onPressAction: clicked()
                    Keys.onLeftPressed: sizeButtonRow.focusStep(-1)
                    Keys.onRightPressed: sizeButtonRow.focusStep(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.text: i18n("%1px width", root.constants.zoneSelectorPreviewMedium)
                }

                Button {
                    text: i18n("Large")
                    flat: sizeButtonRow.selectedSize !== 3
                    highlighted: sizeButtonRow.selectedSize === 3
                    onClicked: {
                        sizeButtonRow.customModeActive = false;
                        root.writeSizeMode(1);
                        root.writePreviewSize(root.constants.zoneSelectorPreviewLarge);
                    }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: text
                    Accessible.checkable: true
                    Accessible.checked: highlighted
                    Accessible.onPressAction: clicked()
                    Keys.onLeftPressed: sizeButtonRow.focusStep(-1)
                    Keys.onRightPressed: sizeButtonRow.focusStep(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.text: i18n("%1px width", root.constants.zoneSelectorPreviewLarge)
                }

                Button {
                    text: i18n("Custom")
                    flat: sizeButtonRow.selectedSize !== 4
                    highlighted: sizeButtonRow.selectedSize === 4
                    onClicked: {
                        // The flag outlives the width-changed reevaluation this
                        // write can queue (callLater runs in FIFO order), then
                        // clears itself.
                        sizeButtonRow._selfWrite = true;
                        sizeButtonRow.customModeActive = true;
                        root.writeSizeMode(1);
                        Qt.callLater(function () {
                            sizeButtonRow._selfWrite = false;
                        });
                    }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: text
                    Accessible.checkable: true
                    Accessible.checked: highlighted
                    Accessible.onPressAction: clicked()
                    Keys.onLeftPressed: sizeButtonRow.focusStep(-1)
                    Keys.onRightPressed: sizeButtonRow.focusStep(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.text: i18n("Custom size with slider")
                }
            }

            // Custom size slider - only visible when Custom is selected
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: Kirigami.Units.gridUnit * 25
                visible: sizeButtonRow.selectedSize === 4
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18n("Size:")
                }

                Slider {
                    id: customSizeSlider

                    Layout.fillWidth: true
                    from: root.constants.zoneSelectorPreviewWidthMin
                    to: root.constants.zoneSelectorPreviewWidthMax
                    stepSize: 10
                    Accessible.name: i18n("Preview size")
                    onMoved: root.writePreviewSize(value)

                    Binding on value {
                        value: root.effectivePreviewWidth
                        when: !customSizeSlider.pressed
                        restoreMode: Binding.RestoreNone
                    }
                }

                Label {
                    text: i18nc("pixel-unit suffix after slider value", "%1 px", root.effectivePreviewWidth)
                    Layout.preferredWidth: root.constants.sliderValueLabelWidth + Kirigami.Units.largeSpacing
                    horizontalAlignment: Text.AlignRight
                    font: Kirigami.Theme.fixedWidthFont
                }
            }

            // Info text for auto mode
            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                visible: root.effectiveSizeMode === 0
                text: i18n("Preview size adjusts automatically based on your screen resolution.")
                opacity: 0.7
                font: Kirigami.Theme.smallFont
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
