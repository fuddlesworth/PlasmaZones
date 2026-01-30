// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Assignments tab - Monitor, activity, and quick layout slot assignments
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign different layouts to each monitor and configure quick-switch keyboard shortcuts.")
            visible: true
        }

        // Monitor Assignments Card
        Kirigami.Card {
            Layout.fillWidth: true
            Layout.fillHeight: false

            header: Kirigami.Heading {
                level: 3
                text: i18n("Monitor Assignments")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: count > 0 ? contentHeight : Kirigami.Units.gridUnit * 4
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    model: kcm.screens
                    interactive: false

                    delegate: Item {
                        id: monitorDelegate
                        width: ListView.view.width
                        height: monitorContent.implicitHeight
                        required property var modelData
                        required property int index

                        property bool expanded: false
                        property string screenName: modelData.name || ""

                        ColumnLayout {
                            id: monitorContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: Kirigami.Units.smallSpacing
                            anchors.rightMargin: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            // Top spacer
                            Item { height: Kirigami.Units.smallSpacing }

                            // Header row - always visible
                            RowLayout {
                                id: monitorHeaderRow
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: "video-display"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    Layout.alignment: Qt.AlignTop
                                }

                                ColumnLayout {
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                    spacing: 0

                                    Label {
                                        id: monitorNameLabel
                                        text: modelData.name || i18n("Unknown Monitor")
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: {
                                            let info = modelData.resolution || ""
                                            if (modelData.isPrimary) {
                                                info += (info ? " • " : "") + i18n("Primary")
                                            }
                                            return info
                                        }
                                        opacity: root.constants.labelSecondaryOpacity
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                // "All Desktops" label
                                Label {
                                    text: i18n("All Desktops:")
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                ComboBox {
                                    id: screenLayoutCombo
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                    model: {
                                        let items = [{text: i18n("Default"), value: "", layout: null}]
                                        for (let i = 0; i < kcm.layouts.length; i++) {
                                            items.push({
                                                text: kcm.layouts[i].name,
                                                value: kcm.layouts[i].id,
                                                layout: kcm.layouts[i]
                                            })
                                        }
                                        return items
                                    }
                                    textRole: "text"
                                    valueRole: "value"

                                    function updateFromAssignment() {
                                        let assignedId = kcm.getLayoutForScreen(monitorDelegate.screenName)
                                        if (assignedId && assignedId !== "") {
                                            for (let i = 0; i < model.length; i++) {
                                                if (model[i].value === assignedId) {
                                                    currentIndex = i
                                                    return
                                                }
                                            }
                                        }
                                        currentIndex = 0
                                    }

                                    Component.onCompleted: updateFromAssignment()

                                    Connections {
                                        target: kcm
                                        function onScreenAssignmentsChanged() {
                                            screenLayoutCombo.updateFromAssignment()
                                        }
                                    }

                                    onActivated: {
                                        let selectedValue = model[currentIndex].value
                                        if (selectedValue === "") {
                                            kcm.clearScreenAssignment(monitorDelegate.screenName)
                                        } else {
                                            kcm.assignLayoutToScreen(monitorDelegate.screenName, selectedValue)
                                        }
                                    }
                                }

                                ToolButton {
                                    icon.name: "edit-clear"
                                    onClicked: {
                                        kcm.clearScreenAssignment(monitorDelegate.screenName)
                                        screenLayoutCombo.currentIndex = 0
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: i18n("Clear assignment")
                                }

                                Item { Layout.fillWidth: true }

                                // Expand button - only show if multiple virtual desktops
                                ToolButton {
                                    visible: kcm.virtualDesktopCount > 1
                                    icon.name: monitorDelegate.expanded ? "go-up" : "go-down"
                                    text: monitorDelegate.expanded ? "" : i18n("Per-desktop")
                                    display: AbstractButton.TextBesideIcon
                                    onClicked: monitorDelegate.expanded = !monitorDelegate.expanded
                                    ToolTip.visible: hovered
                                    ToolTip.text: monitorDelegate.expanded ?
                                        i18n("Hide per-desktop assignments") :
                                        i18n("Show per-desktop assignments")
                                }
                            }

                            // Disable PlasmaZones on this monitor (no overlay, zone picker, or snapping)
                            CheckBox {
                                Layout.fillWidth: true
                                text: i18n("Disable PlasmaZones on this monitor")
                                checked: kcm.disabledMonitors.indexOf(monitorDelegate.screenName) >= 0
                                onToggled: kcm.setMonitorDisabled(monitorDelegate.screenName, checked)
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("When enabled, the zone overlay and zone picker will not appear on this monitor, and windows will not snap to zones here.")
                            }

                            // Per-desktop assignments section - expandable
                            ColumnLayout {
                                id: perDesktopSection
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.gridUnit * 2
                                visible: monitorDelegate.expanded && kcm.virtualDesktopCount > 1
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Separator {
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: i18n("Per-Desktop Overrides")
                                    font.bold: true
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    opacity: 0.8
                                }

                                Label {
                                    text: i18n("Override the default layout for specific virtual desktops")
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    opacity: root.constants.labelSecondaryOpacity
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                // Per-desktop combo boxes
                                Repeater {
                                    model: kcm.virtualDesktopCount

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        required property int index

                                        // Desktop number is 1-based for display but index is 0-based
                                        property int desktopNumber: index + 1
                                        property string desktopName: kcm.virtualDesktopNames[index] || i18n("Desktop %1", desktopNumber)

                                        Kirigami.Icon {
                                            source: "preferences-desktop-virtual"
                                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                            Layout.alignment: Qt.AlignVCenter
                                        }

                                        Label {
                                            text: desktopName
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                            Layout.alignment: Qt.AlignVCenter
                                            elide: Text.ElideRight
                                        }

                                        ComboBox {
                                            id: desktopLayoutCombo
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12

                                            property int desktopNum: desktopNumber

                                            model: {
                                                let items = [{text: i18n("Use default"), value: "", layout: null}]
                                                for (let i = 0; i < kcm.layouts.length; i++) {
                                                    items.push({
                                                        text: kcm.layouts[i].name,
                                                        value: kcm.layouts[i].id,
                                                        layout: kcm.layouts[i]
                                                    })
                                                }
                                                return items
                                            }
                                            textRole: "text"
                                            valueRole: "value"

                                            function updateFromAssignment() {
                                                // Check if there's an EXPLICIT assignment (not inherited from fallback)
                                                let hasExplicit = kcm.hasExplicitAssignmentForScreenDesktop(monitorDelegate.screenName, desktopNum)
                                                if (!hasExplicit) {
                                                    // No explicit assignment - show "Use default"
                                                    currentIndex = 0
                                                    return
                                                }
                                                // Has explicit assignment - find and select it
                                                let assignedId = kcm.getLayoutForScreenDesktop(monitorDelegate.screenName, desktopNum)
                                                if (assignedId && assignedId !== "") {
                                                    for (let i = 0; i < model.length; i++) {
                                                        if (model[i].value === assignedId) {
                                                            currentIndex = i
                                                            return
                                                        }
                                                    }
                                                }
                                                currentIndex = 0
                                            }

                                            Component.onCompleted: updateFromAssignment()

                                            Connections {
                                                target: kcm
                                                function onScreenAssignmentsChanged() {
                                                    desktopLayoutCombo.updateFromAssignment()
                                                }
                                            }

                                            onActivated: {
                                                let selectedValue = model[currentIndex].value
                                                if (selectedValue === "") {
                                                    kcm.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopNum)
                                                } else {
                                                    kcm.assignLayoutToScreenDesktop(monitorDelegate.screenName, desktopNum, selectedValue)
                                                }
                                            }
                                        }

                                        ToolButton {
                                            icon.name: "edit-clear"
                                            onClicked: {
                                                kcm.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopLayoutCombo.desktopNum)
                                                desktopLayoutCombo.currentIndex = 0
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.text: i18n("Clear assignment for this desktop")
                                        }

                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }

                            // Bottom spacer for consistent padding
                            Item { height: Kirigami.Units.smallSpacing }
                        }
                    }

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        visible: parent.count === 0
                        text: i18n("No monitors detected")
                        explanation: i18n("Make sure the PlasmaZones daemon is running")
                    }
                }

                // Info message when only one virtual desktop
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    visible: kcm.virtualDesktopCount <= 1 && kcm.screens.length > 0
                    type: Kirigami.MessageType.Information
                    text: i18n("Per-desktop layout assignments are available when using multiple virtual desktops. Add more desktops in System Settings → Virtual Desktops.")
                }
            }
        }

        // Activity Assignments Card
        Kirigami.Card {
            Layout.fillWidth: true
            Layout.fillHeight: false
            visible: kcm.activitiesAvailable

            header: Kirigami.Heading {
                level: 3
                text: i18n("Activity Assignments")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    text: i18n("Assign different layouts to KDE Activities. When you switch activities, the layout will change automatically.")
                    wrapMode: Text.WordWrap
                    opacity: root.constants.labelSecondaryOpacity
                }

                // Activities list
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    model: kcm.activities
                    interactive: false

                    delegate: Item {
                        width: ListView.view.width
                        height: activityRowLayout.implicitHeight + Kirigami.Units.smallSpacing * 2
                        required property var modelData
                        required property int index

                        property string activityId: modelData.id || ""
                        property string activityName: modelData.name || ""
                        property string activityIcon: modelData.icon && modelData.icon !== "" ? modelData.icon : "activities"

                        ColumnLayout {
                            id: activityRowLayout
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            // Activity header row
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: activityIcon
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: activityName
                                    font.bold: activityId === kcm.currentActivity
                                    elide: Text.ElideRight
                                }

                                // Show "Current" badge if this is the current activity
                                Label {
                                    visible: activityId === kcm.currentActivity
                                    text: i18n("Current")
                                    font.italic: true
                                    opacity: 0.7
                                }
                            }

                            // Per-screen layout assignments for this activity
                            Repeater {
                                model: kcm.screens

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: Kirigami.Units.gridUnit * 2
                                    spacing: Kirigami.Units.smallSpacing

                                    required property var modelData
                                    property string screenName: modelData.name || ""

                                    Kirigami.Icon {
                                        source: "video-display"
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                        opacity: 0.7
                                    }

                                    Label {
                                        text: screenName
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                        elide: Text.ElideRight
                                    }

                                    ComboBox {
                                        id: activityLayoutCombo
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12

                                        textRole: "text"
                                        valueRole: "value"

                                        model: {
                                            let items = [{text: i18n("Use default"), value: ""}]
                                            for (let i = 0; i < kcm.layouts.length; i++) {
                                                items.push({
                                                    text: kcm.layouts[i].name,
                                                    value: kcm.layouts[i].id
                                                })
                                            }
                                            return items
                                        }

                                        function updateFromAssignment() {
                                            let hasExplicit = kcm.hasExplicitAssignmentForScreenActivity(screenName, activityId)
                                            if (!hasExplicit) {
                                                currentIndex = 0
                                                return
                                            }
                                            let assignedId = kcm.getLayoutForScreenActivity(screenName, activityId)
                                            if (assignedId && assignedId !== "") {
                                                for (let i = 0; i < model.length; i++) {
                                                    if (model[i].value === assignedId) {
                                                        currentIndex = i
                                                        return
                                                    }
                                                }
                                            }
                                            currentIndex = 0
                                        }

                                        Component.onCompleted: updateFromAssignment()

                                        Connections {
                                            target: kcm
                                            function onActivityAssignmentsChanged() {
                                                activityLayoutCombo.updateFromAssignment()
                                            }
                                        }

                                        onActivated: {
                                            let selectedValue = model[currentIndex].value
                                            if (selectedValue === "") {
                                                kcm.clearScreenActivityAssignment(screenName, activityId)
                                            } else {
                                                kcm.assignLayoutToScreenActivity(screenName, activityId, selectedValue)
                                            }
                                        }
                                    }

                                    ToolButton {
                                        icon.name: "edit-clear"
                                        onClicked: {
                                            kcm.clearScreenActivityAssignment(screenName, activityId)
                                            activityLayoutCombo.currentIndex = 0
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: i18n("Clear assignment")
                                    }

                                    // Spacer to push elements left (consistent with Monitor Assignments)
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }
                    }
                }

                // Message when no activities
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    visible: kcm.activities.length === 0 && kcm.activitiesAvailable
                    type: Kirigami.MessageType.Information
                    text: i18n("No activities found. Create activities in System Settings → Activities.")
                }

                // Message when no screens
                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    visible: kcm.screens.length === 0 && kcm.activities.length > 0
                    type: Kirigami.MessageType.Warning
                    text: i18n("No screens detected. Make sure the PlasmaZones daemon is running.")
                }

                // Bottom spacer for consistent padding with Monitor Assignments card
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.smallSpacing
                }
            }
        }

        // Info message when Activities not available
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !kcm.activitiesAvailable
            type: Kirigami.MessageType.Information
            text: i18n("KDE Activities support is not available. Activity-based layout assignments require the KDE Activities service to be running.")
        }

        // Quick Layout Slots Card
        Kirigami.Card {
            Layout.fillWidth: true
            Layout.fillHeight: false

            header: Kirigami.Heading {
                level: 3
                text: i18n("Quick Layout Shortcuts")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    text: i18n("Assign layouts to keyboard shortcuts for instant switching.")
                    wrapMode: Text.WordWrap
                    opacity: root.constants.labelSecondaryOpacity
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    model: 9
                    interactive: false

                    delegate: Item {
                        width: ListView.view.width
                        height: shortcutRowLayout.implicitHeight + Kirigami.Units.smallSpacing * 2
                        required property int index

                        RowLayout {
                            id: shortcutRowLayout
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                property string shortcut: kcm.getQuickLayoutShortcut(index + 1)
                                text: shortcut !== "" ? shortcut : i18n("Not assigned")
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 14 + Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                                font.family: "monospace"
                                opacity: shortcut !== "" ? 1.0 : 0.6
                            }

                            Item { Layout.fillWidth: true }

                            ComboBox {
                                id: slotLayoutCombo
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 16

                                // Store the shortcut slot number for this delegate
                                property int slotNumber: index + 1

                                model: {
                                    let items = [{text: i18n("None"), value: "", layout: null}]
                                    for (let i = 0; i < kcm.layouts.length; i++) {
                                        items.push({
                                            text: kcm.layouts[i].name,
                                            value: kcm.layouts[i].id,
                                            layout: kcm.layouts[i]
                                        })
                                    }
                                    return items
                                }
                                textRole: "text"
                                valueRole: "value"

                                // Custom delegate for popup items with layout preview
                                delegate: ItemDelegate {
                                    width: slotLayoutCombo.popup.width
                                    highlighted: slotLayoutCombo.highlightedIndex === index

                                    required property var modelData
                                    required property int index

                                    contentItem: RowLayout {
                                        spacing: Kirigami.Units.smallSpacing

                                        // Mini layout preview
                                        Rectangle {
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                                            radius: Kirigami.Units.smallSpacing / 2
                                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                                            border.color: highlighted ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                            border.width: highlighted ? 2 : 1
                                            visible: modelData.layout != null

                                            Item {
                                                id: zonePreviewContainer
                                                anchors.fill: parent
                                                anchors.margins: Math.round(Kirigami.Units.smallSpacing * 0.75)

                                                property var zones: modelData.layout?.zones || []

                                                Repeater {
                                                    model: zonePreviewContainer.zones
                                                    Rectangle {
                                                        required property var modelData
                                                        required property int index
                                                        property var relGeo: modelData.relativeGeometry || {}
                                                        x: (relGeo.x || 0) * zonePreviewContainer.width
                                                        y: (relGeo.y || 0) * zonePreviewContainer.height
                                                        width: Math.max(2, (relGeo.width || 0.25) * zonePreviewContainer.width)
                                                        height: Math.max(2, (relGeo.height || 1) * zonePreviewContainer.height)
                                                        // Match LayoutThumbnail colors for consistency
                                                        color: highlighted ?
                                                            Qt.rgba(Kirigami.Theme.highlightedTextColor.r, Kirigami.Theme.highlightedTextColor.g, Kirigami.Theme.highlightedTextColor.b, 0.85) :
                                                            Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
                                                        border.color: highlighted ?
                                                            Kirigami.Theme.highlightedTextColor :
                                                            Kirigami.Theme.highlightColor
                                                        border.width: Math.round(Kirigami.Units.devicePixelRatio * 2)
                                                        radius: Kirigami.Units.smallSpacing * 0.5
                                                    }
                                                }
                                            }
                                        }

                                        // "None" placeholder - opposite colors (starts visible, highlighted contrasts)
                                        Rectangle {
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                                            radius: Kirigami.Units.smallSpacing / 2
                                            color: highlighted ?
                                                Qt.rgba(Kirigami.Theme.highlightedTextColor.r, Kirigami.Theme.highlightedTextColor.g, Kirigami.Theme.highlightedTextColor.b, 0.15) :
                                                Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                                            border.color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.highlightColor
                                            border.width: Math.round(Kirigami.Units.devicePixelRatio * 2)
                                            visible: modelData.layout == null

                                            Kirigami.Icon {
                                                anchors.centerIn: parent
                                                source: "action-unavailable-symbolic"
                                                width: Kirigami.Units.iconSizes.smallMedium
                                                height: Kirigami.Units.iconSizes.smallMedium
                                                color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.highlightColor
                                                opacity: 0.7
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 0

                                            Label {
                                                text: modelData.text
                                                font.bold: highlighted
                                                color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Label {
                                                text: modelData.layout ? i18n("%1 zones", modelData.layout.zoneCount || 0) : i18n("No shortcut assigned")
                                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                                color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                                opacity: 0.7
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }
                                    }
                                }

                                function updateFromSlot() {
                                    let assignedId = kcm.getQuickLayoutSlot(slotNumber)
                                    if (assignedId && assignedId !== "") {
                                        for (let i = 0; i < model.length; i++) {
                                            if (model[i].value === assignedId) {
                                                currentIndex = i
                                                return
                                            }
                                        }
                                    }
                                    currentIndex = 0
                                }

                                Component.onCompleted: updateFromSlot()

                                Connections {
                                    target: kcm
                                    // Quick layout slots are also cleared when defaults are reset
                                    function onScreenAssignmentsChanged() {
                                        slotLayoutCombo.updateFromSlot()
                                    }
                                }

                                onActivated: {
                                    let selectedValue = model[currentIndex].value
                                    kcm.setQuickLayoutSlot(slotNumber, selectedValue)
                                }
                            }

                            ToolButton {
                                icon.name: "edit-clear"
                                onClicked: {
                                    kcm.setQuickLayoutSlot(index + 1, "")
                                    slotLayoutCombo.currentIndex = 0
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Clear shortcut")
                            }
                        }
                    }
                }
            }
        }
    }
}
