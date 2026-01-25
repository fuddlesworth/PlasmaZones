// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Visual zone editor for creating and modifying layouts
 * Supports both grid-based and canvas-style editing
 */
Kirigami.ApplicationWindow {
    id: root

    // Constants for visual styling
    QtObject {
        id: constants
        readonly property int thinBorderWidth: 1  // 1px - thin border for subtle elements
    }

    title: i18n("PlasmaZones Layout Editor")
    width: Kirigami.Units.gridUnit * 75
    height: Kirigami.Units.gridUnit * 50
    minimumWidth: Kirigami.Units.gridUnit * 50
    minimumHeight: Kirigami.Units.gridUnit * 37.5

    property var layout: null
    property var zones: layout ? layout.zones : []
    property string selectedZoneId: ""  // Use zone ID instead of index for stable selection
    property string editMode: "canvas" // "canvas" or "grid"

    // Constants
    readonly property real defaultZoneSize: 0.25
    readonly property real defaultZoneOffset: 0.125
    readonly property int minZoneSize: 50
    readonly property var resizeHandles: ["nw", "n", "ne", "e", "se", "s", "sw", "w"]

    // Helper function to get selected zone by ID (stable selection)
    function getSelectedZone() {
        if (selectedZoneId && zones) {
            for (var i = 0; i < zones.length; i++) {
                if (zones[i].id === selectedZoneId) {
                    return zones[i]
                }
            }
        }
        return null
    }

    property var selectedZone: getSelectedZone()

    signal layoutSaved(var layout)
    signal layoutCanceled()

    // Keyboard shortcuts
    Shortcut {
        sequence: "Ctrl+N"
        onActivated: addNewZone()
    }

    Shortcut {
        sequence: "Delete"
        enabled: root.selectedZoneId !== ""
        onActivated: deleteSelectedZone()
    }

    Shortcut {
        sequence: "Ctrl+S"
        onActivated: saveLayout()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: root.layoutCanceled()
    }

    // Toolbar
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            // Layout type selector
            ComboBox {
                id: layoutTypeCombo
                model: [
                    { text: i18n("Canvas (Free-form)"), value: "canvas" },
                    { text: i18n("Grid"), value: "grid" }
                ]
                textRole: "text"
                valueRole: "value"
                onActivated: root.editMode = currentValue
            }

            ToolSeparator {}

            // Grid controls (visible in grid mode)
            RowLayout {
                visible: root.editMode === "grid"
                spacing: Kirigami.Units.smallSpacing

                Label { text: i18n("Columns:") }
                SpinBox {
                    id: columnsSpin
                    from: 1
                    to: 12
                    value: 3
                    onValueChanged: regenerateGrid()
                }

                Label { text: i18n("Rows:") }
                SpinBox {
                    id: rowsSpin
                    from: 1
                    to: 8
                    value: 1
                    onValueChanged: regenerateGrid()
                }
            }

            // Canvas controls
            RowLayout {
                visible: root.editMode === "canvas"
                spacing: Kirigami.Units.smallSpacing

                Button {
                    icon.name: "list-add"
                    text: i18n("Add Zone")
                    ToolTip.text: i18n("Add a new zone to the layout")
                    ToolTip.visible: hovered
                    onClicked: addNewZone()
                }

                Button {
                    icon.name: "edit-delete"
                    text: i18n("Delete Zone")
                    enabled: root.selectedZoneId !== ""
                    ToolTip.text: i18n("Delete the selected zone")
                    ToolTip.visible: hovered
                    onClicked: deleteSelectedZone()
                }
            }

            Item { Layout.fillWidth: true }

            // Layout name
            TextField {
                id: layoutNameField
                placeholderText: i18n("Layout Name")
                text: root.layout ? root.layout.name : ""
                Layout.preferredWidth: 200
            }

            ToolSeparator {}

            // Save/Cancel
            Button {
                text: i18n("Cancel")
                ToolTip.text: i18n("Cancel editing and discard changes")
                ToolTip.visible: hovered
                onClicked: root.layoutCanceled()
            }

            Button {
                text: i18n("Save Layout")
                icon.name: "document-save"
                highlighted: true
                ToolTip.text: i18n("Save the current layout")
                ToolTip.visible: hovered
                onClicked: saveLayout()
            }
        }
    }

    // Main content
    RowLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        // Zone editor canvas
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.Theme.disabledTextColor
            border.width: constants.thinBorderWidth
            radius: Kirigami.Units.smallSpacing  // Use theme spacing

            // Screen representation
            Rectangle {
                id: screenCanvas
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                color: Kirigami.Theme.backgroundColor
                radius: Kirigami.Units.smallSpacing  // Use theme spacing

                // Grid lines (visible in grid mode)
                Canvas {
                    id: gridCanvas
                    anchors.fill: parent
                    visible: root.editMode === "grid"

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.clearRect(0, 0, width, height);
                        ctx.strokeStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1);
                        ctx.lineWidth = 1;

                        // Draw vertical lines
                        var colWidth = width / columnsSpin.value;
                        for (var i = 1; i < columnsSpin.value; i++) {
                            ctx.beginPath();
                            ctx.moveTo(i * colWidth, 0);
                            ctx.lineTo(i * colWidth, height);
                            ctx.stroke();
                        }

                        // Draw horizontal lines
                        var rowHeight = height / rowsSpin.value;
                        for (var j = 1; j < rowsSpin.value; j++) {
                            ctx.beginPath();
                            ctx.moveTo(0, j * rowHeight);
                            ctx.lineTo(width, j * rowHeight);
                            ctx.stroke();
                        }
                    }

                    Connections {
                        target: columnsSpin
                        function onValueChanged() { gridCanvas.requestPaint() }
                    }

                    Connections {
                        target: rowsSpin
                        function onValueChanged() { gridCanvas.requestPaint() }
                    }

                    Connections {
                        target: root
                        function onEditModeChanged() { 
                            if (root.editMode === "grid") {
                                gridCanvas.requestPaint()
                            }
                        }
                    }
                }

                // Zone items
                Repeater {
                    model: root.zones

                    delegate: EditableZone {
                        id: editableZone
                        required property var modelData
                        required property int index

                        // Convert relative geometry to canvas pixels
                        x: modelData.relativeGeometry.x * screenCanvas.width
                        y: modelData.relativeGeometry.y * screenCanvas.height
                        width: modelData.relativeGeometry.width * screenCanvas.width
                        height: modelData.relativeGeometry.height * screenCanvas.height

                        zoneNumber: modelData.zoneNumber || (index + 1)
                        zoneName: modelData.name || ""
                        zoneId: modelData.id || ""
                        isSelected: modelData.id && modelData.id === root.selectedZoneId
                        editMode: root.editMode

                        onSelected: root.selectedZoneId = modelData.id || ""
                        onGeometryChanged: function(newGeo) {
                            updateZoneGeometry(modelData.id, newGeo);
                        }
                    }
                }

                // Click to add zone (canvas mode)
                MouseArea {
                    anchors.fill: parent
                    enabled: root.editMode === "canvas"
                    acceptedButtons: Qt.LeftButton

                    onDoubleClicked: function(mouse) {
                        addZoneAtPosition(mouse.x / width, mouse.y / height);
                    }
                }
            }
        }

        // Properties panel
        Kirigami.FormLayout {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            visible: root.selectedZoneId !== ""

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Zone Properties")
            }

            TextField {
                Kirigami.FormData.label: i18n("Name:")
                text: selectedZone ? selectedZone.name : ""
                onTextChanged: if (selectedZone) selectedZone.name = text
            }

            SpinBox {
                Kirigami.FormData.label: i18n("Zone Number:")
                from: 1
                to: 99
                value: selectedZone ? selectedZone.zoneNumber : 1
                onValueChanged: if (selectedZone) selectedZone.zoneNumber = value
            }

            TextField {
                Kirigami.FormData.label: i18n("Shortcut:")
                text: selectedZone ? selectedZone.shortcut : ""
                placeholderText: i18n("e.g., Meta+Alt+1")
                onTextChanged: if (selectedZone) selectedZone.shortcut = text
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Appearance")
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Custom colors:")
                checked: selectedZone ? selectedZone.useCustomColors : false
                onCheckedChanged: if (selectedZone) selectedZone.useCustomColors = checked
            }

            Button {
                Kirigami.FormData.label: i18n("Highlight:")
                text: selectedZone ? selectedZone.highlightColor : Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5)
                visible: selectedZone && selectedZone.useCustomColors
                onClicked: colorDialog.open()
            }

            Slider {
                Kirigami.FormData.label: i18n("Opacity:")
                from: 0
                to: 1
                value: selectedZone ? selectedZone.opacity : 0.5
                visible: selectedZone && selectedZone.useCustomColors
                onValueChanged: if (selectedZone) selectedZone.opacity = value
            }

            SpinBox {
                Kirigami.FormData.label: i18n("Border radius:")
                from: 0
                to: 50
                value: selectedZone ? selectedZone.borderRadius : 8
                visible: selectedZone && selectedZone.useCustomColors
                onValueChanged: if (selectedZone) selectedZone.borderRadius = value
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Geometry")
            }

            Label {
                Kirigami.FormData.label: i18n("Position:")
                text: selectedZone ?
                    Math.round(selectedZone.relativeGeometry.x * 100) + "%, " +
                    Math.round(selectedZone.relativeGeometry.y * 100) + "%" : ""
            }

            Label {
                Kirigami.FormData.label: i18n("Size:")
                text: selectedZone ?
                    Math.round(selectedZone.relativeGeometry.width * 100) + "% × " +
                    Math.round(selectedZone.relativeGeometry.height * 100) + "%" : ""
            }
        }
    }

    // Functions
    function regenerateGrid() {
        if (editMode !== "grid") return;

        var newZones = [];
        var colWidth = 1.0 / columnsSpin.value;
        var rowHeight = 1.0 / rowsSpin.value;
        var num = 1;

        for (var row = 0; row < rowsSpin.value; row++) {
            for (var col = 0; col < columnsSpin.value; col++) {
                newZones.push({
                    id: generateUuid(),
                    name: "",
                    zoneNumber: num++,
                    relativeGeometry: {
                        x: col * colWidth,
                        y: row * rowHeight,
                        width: colWidth,
                        height: rowHeight
                    },
                    useCustomColors: false
                });
            }
        }

        zones = newZones;
    }

    function addNewZone() {
        addZoneAtPosition(defaultZoneSize, defaultZoneSize);
    }

    function addZoneAtPosition(x, y) {
        var newZone = {
            id: generateUuid(),
            name: "",
            zoneNumber: zones.length + 1,
            relativeGeometry: {
                x: Math.max(0, Math.min(1.0 - defaultZoneSize, x - defaultZoneOffset)),
                y: Math.max(0, Math.min(1.0 - defaultZoneSize, y - defaultZoneOffset)),
                width: defaultZoneSize,
                height: defaultZoneSize
            },
            useCustomColors: false
        };

        var newZones = zones.slice();
        newZones.push(newZone);
        zones = newZones;
        selectedZoneId = newZone.id;
    }

    function deleteSelectedZone() {
        if (!selectedZoneId) return;

        // Find zone by ID and remove it
        var newZones = [];
        for (var i = 0; i < zones.length; i++) {
            if (zones[i].id !== selectedZoneId) {
                newZones.push(zones[i]);
            }
        }

        // Renumber zones
        for (var j = 0; j < newZones.length; j++) {
            newZones[j].zoneNumber = j + 1;
        }

        zones = newZones;
        selectedZoneId = "";
    }

    function updateZoneGeometry(zoneId, newGeo) {
        if (!zoneId) return;

        // Find zone by ID
        var zone = null;
        for (var i = 0; i < zones.length; i++) {
            if (zones[i].id === zoneId) {
                zone = zones[i];
                break;
            }
        }

        if (!zone) return;

        zone.relativeGeometry = {
            x: newGeo.x / screenCanvas.width,
            y: newGeo.y / screenCanvas.height,
            width: newGeo.width / screenCanvas.width,
            height: newGeo.height / screenCanvas.height
        };

        // Clamp to screen bounds
        zone.relativeGeometry.x = Math.max(0, Math.min(1 - zone.relativeGeometry.width, zone.relativeGeometry.x));
        zone.relativeGeometry.y = Math.max(0, Math.min(1 - zone.relativeGeometry.height, zone.relativeGeometry.y));

        zonesChanged();
    }

    function saveLayout() {
        var savedLayout = {
            id: layout ? layout.id : generateUuid(),
            name: layoutNameField.text || i18n("Custom Layout"),
            type: editMode === "grid" ? 1 : 0, // LayoutType enum
            zones: zones
        };

        root.layoutSaved(savedLayout);
    }

    function generateUuid() {
        return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
            var r = Math.random() * 16 | 0;
            var v = c === 'x' ? r : (r & 0x3 | 0x8);
            return v.toString(16);
        });
    }
}

/**
 * Editable zone component with drag and resize handles
 * Styling matches ZoneItem.qml for consistent appearance with overlay
 */
component EditableZone: Item {
    id: editableZone

    property int zoneNumber: 1
    property string zoneName: ""
    property string zoneId: ""  // Zone ID for stable selection
    property bool isSelected: false
    property string editMode: "canvas"

    readonly property int minSize: 50

    // Styling properties - match ZoneItem.qml overlay colors
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property real activeOpacity: 0.75
    property real inactiveOpacity: 0.55
    property int borderWidth: Kirigami.Units.smallSpacing  // 4px - match overlay
    property int borderRadius: Kirigami.Units.gridUnit  // 8px - use theme spacing

    signal selected()
    signal geometryChanged(var newGeo)

    // Zone background - styled to match ZoneItem.qml overlay
    Rectangle {
        id: background
        anchors.fill: parent
        radius: editableZone.borderRadius

        color: editableZone.isSelected ? editableZone.highlightColor : editableZone.inactiveColor
        opacity: editableZone.isSelected ? editableZone.activeOpacity : editableZone.inactiveOpacity

        border.width: editableZone.borderWidth
        border.color: editableZone.isSelected ? Kirigami.Theme.highlightColor : editableZone.borderColor

        Behavior on color {
            ColorAnimation { duration: 150 }
        }

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        // Zone number
        Label {
            anchors.centerIn: parent
            text: editableZone.zoneNumber
            font.pixelSize: Math.min(parent.width, parent.height) * 0.3
            font.bold: true
            color: Kirigami.Theme.textColor
            opacity: editableZone.isSelected ? 1.0 : 0.7
        }

        // Zone name
        Label {
            anchors {
                top: parent.verticalCenter
                topMargin: parent.height * 0.15
                horizontalCenter: parent.horizontalCenter
            }
            text: editableZone.zoneName
            font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.875)  // ~14px for default size - use theme font
            color: Kirigami.Theme.textColor
            opacity: editableZone.isSelected ? 0.9 : 0.5
            visible: editableZone.zoneName.length > 0
        }
    }

    // Drag to move (canvas mode)
    MouseArea {
        anchors.fill: parent
        enabled: editableZone.editMode === "canvas"
        drag.target: parent
        drag.minimumX: 0
        drag.minimumY: 0
        drag.maximumX: parent.parent.width - parent.width
        drag.maximumY: parent.parent.height - parent.height

        onClicked: editableZone.selected()
        onReleased: {
            editableZone.geometryChanged({
                x: editableZone.x,
                y: editableZone.y,
                width: editableZone.width,
                height: editableZone.height
            });
        }
    }

    // Resize handles (visible when selected in canvas mode)
    readonly property var resizeHandles: ["nw", "n", "ne", "e", "se", "s", "sw", "w"]
    
    Repeater {
        model: editableZone.isSelected && editableZone.editMode === "canvas" ? editableZone.resizeHandles : []

        delegate: Rectangle {
            id: handle
            required property string modelData

            z: 10  // Ensure handles are above the zone's drag MouseArea
            width: Kirigami.Units.gridUnit * 1.5  // Use theme spacing (12px)
            height: Kirigami.Units.gridUnit * 1.5  // Use theme spacing (12px)
            radius: Kirigami.Units.smallSpacing * 1.5  // Use theme spacing (6px)
            color: Kirigami.Theme.highlightColor
            border.color: Kirigami.Theme.backgroundColor
            border.width: constants.thinBorderWidth

            x: {
                switch(modelData) {
                    case "nw": case "w": case "sw": return -6;
                    case "n": case "s": return editableZone.width / 2 - 6;
                    case "ne": case "e": case "se": return editableZone.width - 6;
                }
            }
            y: {
                switch(modelData) {
                    case "nw": case "n": case "ne": return -6;
                    case "w": case "e": return editableZone.height / 2 - 6;
                    case "sw": case "s": case "se": return editableZone.height - 6;
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: {
                    switch(modelData) {
                        case "nw": case "se": return Qt.SizeFDiagCursor;
                        case "ne": case "sw": return Qt.SizeBDiagCursor;
                        case "n": case "s": return Qt.SizeVerCursor;
                        case "e": case "w": return Qt.SizeHorCursor;
                    }
                }
                hoverEnabled: true
                preventStealing: true

                property point startPos
                property rect startGeo
                property bool isDragging: false

                onPressed: function(mouse) {
                    isDragging = true
                    // Get mouse position relative to the parent zone's parent (screenCanvas)
                    var globalPos = mapToItem(editableZone.parent, mouse.x, mouse.y)
                    startPos = Qt.point(globalPos.x, globalPos.y)
                    startGeo = Qt.rect(editableZone.x, editableZone.y,
                                       editableZone.width, editableZone.height)
                    mouse.accepted = true
                }

                onPositionChanged: function(mouse) {
                    if (!isDragging) return
                    
                    // Get current mouse position relative to the parent zone's parent (screenCanvas)
                    var currentPos = mapToItem(editableZone.parent, mouse.x, mouse.y)
                    var dx = currentPos.x - startPos.x
                    var dy = currentPos.y - startPos.y
                    var minSize = editableZone.minSize

                    switch(modelData) {
                        case "nw":
                            editableZone.x = Math.min(startGeo.x + dx, startGeo.x + startGeo.width - minSize)
                            editableZone.y = Math.min(startGeo.y + dy, startGeo.y + startGeo.height - minSize)
                            editableZone.width = startGeo.width - (editableZone.x - startGeo.x)
                            editableZone.height = startGeo.height - (editableZone.y - startGeo.y)
                            break
                        case "n":
                            editableZone.y = Math.min(startGeo.y + dy, startGeo.y + startGeo.height - minSize)
                            editableZone.height = startGeo.height - (editableZone.y - startGeo.y)
                            break
                        case "ne":
                            editableZone.y = Math.min(startGeo.y + dy, startGeo.y + startGeo.height - minSize)
                            editableZone.width = Math.max(minSize, startGeo.width + dx)
                            editableZone.height = startGeo.height - (editableZone.y - startGeo.y)
                            break
                        case "e":
                            editableZone.width = Math.max(minSize, startGeo.width + dx)
                            break
                        case "se":
                            editableZone.width = Math.max(minSize, startGeo.width + dx)
                            editableZone.height = Math.max(minSize, startGeo.height + dy)
                            break
                        case "s":
                            editableZone.height = Math.max(minSize, startGeo.height + dy)
                            break
                        case "sw":
                            editableZone.x = Math.min(startGeo.x + dx, startGeo.x + startGeo.width - minSize)
                            editableZone.width = startGeo.width - (editableZone.x - startGeo.x)
                            editableZone.height = Math.max(minSize, startGeo.height + dy)
                            break
                        case "w":
                            editableZone.x = Math.min(startGeo.x + dx, startGeo.x + startGeo.width - minSize)
                            editableZone.width = startGeo.width - (editableZone.x - startGeo.x)
                            break
                    }
                    mouse.accepted = true
                }

                onReleased: function(mouse) {
                    if (isDragging) {
                        editableZone.geometryChanged({
                            x: editableZone.x,
                            y: editableZone.y,
                            width: editableZone.width,
                            height: editableZone.height
                        })
                    }
                    isDragging = false
                    mouse.accepted = true
                }
            }
        }
    }
}
