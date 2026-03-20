// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Bridge: combine Settings (kcm) with SettingsController editor properties
    readonly property var kcmModule: settingsController
    // Inline constants (from monolith Constants object)
    readonly property int sliderPreferredWidth: 200
    readonly property int sliderValueLabelWidth: 40

    // Helper function to build key sequence string from Qt key event
    function _buildSequence(key, modifiers) {
        var parts = [];
        if (modifiers & Qt.ControlModifier)
            parts.push("Ctrl");

        if (modifiers & Qt.AltModifier)
            parts.push("Alt");

        if (modifiers & Qt.ShiftModifier)
            parts.push("Shift");

        if (modifiers & Qt.MetaModifier)
            parts.push("Meta");

        var keyStr = "";
        if (key >= Qt.Key_A && key <= Qt.Key_Z) {
            keyStr = String.fromCharCode('A'.charCodeAt(0) + (key - Qt.Key_A));
        } else if (key >= Qt.Key_0 && key <= Qt.Key_9) {
            keyStr = String.fromCharCode('0'.charCodeAt(0) + (key - Qt.Key_0));
        } else {
            switch (key) {
            case Qt.Key_F1:
                keyStr = "F1";
                break;
            case Qt.Key_F2:
                keyStr = "F2";
                break;
            case Qt.Key_F3:
                keyStr = "F3";
                break;
            case Qt.Key_F4:
                keyStr = "F4";
                break;
            case Qt.Key_F5:
                keyStr = "F5";
                break;
            case Qt.Key_F6:
                keyStr = "F6";
                break;
            case Qt.Key_F7:
                keyStr = "F7";
                break;
            case Qt.Key_F8:
                keyStr = "F8";
                break;
            case Qt.Key_F9:
                keyStr = "F9";
                break;
            case Qt.Key_F10:
                keyStr = "F10";
                break;
            case Qt.Key_F11:
                keyStr = "F11";
                break;
            case Qt.Key_F12:
                keyStr = "F12";
                break;
            case Qt.Key_Return:
                keyStr = "Return";
                break;
            case Qt.Key_Enter:
                keyStr = "Enter";
                break;
            case Qt.Key_Space:
                keyStr = "Space";
                break;
            case Qt.Key_Tab:
                keyStr = "Tab";
                break;
            case Qt.Key_Backspace:
                keyStr = "Backspace";
                break;
            case Qt.Key_Delete:
                keyStr = "Delete";
                break;
            case Qt.Key_Left:
                keyStr = "Left";
                break;
            case Qt.Key_Right:
                keyStr = "Right";
                break;
            case Qt.Key_Up:
                keyStr = "Up";
                break;
            case Qt.Key_Down:
                keyStr = "Down";
                break;
            case Qt.Key_Home:
                keyStr = "Home";
                break;
            case Qt.Key_End:
                keyStr = "End";
                break;
            case Qt.Key_PageUp:
                keyStr = "PageUp";
                break;
            case Qt.Key_PageDown:
                keyStr = "PageDown";
                break;
            case Qt.Key_Minus:
                keyStr = "-";
                break;
            case Qt.Key_Equal:
                keyStr = "=";
                break;
            case Qt.Key_BracketLeft:
                keyStr = "[";
                break;
            case Qt.Key_BracketRight:
                keyStr = "]";
                break;
            case Qt.Key_Backslash:
                keyStr = "\\";
                break;
            case Qt.Key_Semicolon:
                keyStr = ";";
                break;
            case Qt.Key_Apostrophe:
                keyStr = "'";
                break;
            case Qt.Key_Comma:
                keyStr = ",";
                break;
            case Qt.Key_Period:
                keyStr = ".";
                break;
            case Qt.Key_Slash:
                keyStr = "/";
                break;
            default:
                if (key > 0 && key < 128)
                    keyStr = String.fromCharCode(key);

                break;
            }
        }
        if (keyStr.length > 0) {
            parts.push(keyStr);
            return parts.join("+");
        }
        return "";
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // KEYBOARD SHORTCUTS CARD
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: shortcutsCard.implicitHeight

            Kirigami.Card {
                id: shortcutsCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Keyboard Shortcuts")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    // Duplicate zone shortcut (TextField replacing KeySequenceInput)
                    TextField {
                        id: editorDuplicateShortcutField

                        property bool capturing: false

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Duplicate zone:")
                        text: capturing ? i18n("Press keys...") : (settingsController.editorDuplicateShortcut || "")
                        placeholderText: "Ctrl+D"
                        readOnly: true
                        color: capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => {
                            if (!capturing)
                                return ;

                            event.accepted = true;
                            if (event.key === Qt.Key_Escape) {
                                capturing = false;
                                return ;
                            }
                            var key = event.key;
                            if (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_AltGr || key === Qt.Key_Meta)
                                return ;

                            var seq = _buildSequence(event.key, event.modifiers);
                            if (seq.length > 0) {
                                settingsController.editorDuplicateShortcut = seq;
                                capturing = false;
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus && capturing)
                                capturing = false;

                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to duplicate the selected zone. Click to capture.")

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!editorDuplicateShortcutField.capturing) {
                                    editorDuplicateShortcutField.capturing = true;
                                    editorDuplicateShortcutField.forceActiveFocus();
                                }
                            }
                        }

                    }

                    // Split horizontally shortcut
                    TextField {
                        id: editorSplitHorizontalShortcutField

                        property bool capturing: false

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Split horizontally:")
                        text: capturing ? i18n("Press keys...") : (settingsController.editorSplitHorizontalShortcut || "")
                        placeholderText: "Ctrl+Shift+H"
                        readOnly: true
                        color: capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => {
                            if (!capturing)
                                return ;

                            event.accepted = true;
                            if (event.key === Qt.Key_Escape) {
                                capturing = false;
                                return ;
                            }
                            var key = event.key;
                            if (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_AltGr || key === Qt.Key_Meta)
                                return ;

                            var seq = _buildSequence(event.key, event.modifiers);
                            if (seq.length > 0) {
                                settingsController.editorSplitHorizontalShortcut = seq;
                                capturing = false;
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus && capturing)
                                capturing = false;

                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to split selected zone horizontally. Click to capture.")

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!editorSplitHorizontalShortcutField.capturing) {
                                    editorSplitHorizontalShortcutField.capturing = true;
                                    editorSplitHorizontalShortcutField.forceActiveFocus();
                                }
                            }
                        }

                    }

                    // Split vertically shortcut
                    TextField {
                        id: editorSplitVerticalShortcutField

                        property bool capturing: false

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Split vertically:")
                        text: capturing ? i18n("Press keys...") : (settingsController.editorSplitVerticalShortcut || "")
                        placeholderText: "Ctrl+Alt+V"
                        readOnly: true
                        color: capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => {
                            if (!capturing)
                                return ;

                            event.accepted = true;
                            if (event.key === Qt.Key_Escape) {
                                capturing = false;
                                return ;
                            }
                            var key = event.key;
                            if (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_AltGr || key === Qt.Key_Meta)
                                return ;

                            var seq = _buildSequence(event.key, event.modifiers);
                            if (seq.length > 0) {
                                settingsController.editorSplitVerticalShortcut = seq;
                                capturing = false;
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus && capturing)
                                capturing = false;

                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to split selected zone vertically. Click to capture.")

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!editorSplitVerticalShortcutField.capturing) {
                                    editorSplitVerticalShortcutField.capturing = true;
                                    editorSplitVerticalShortcutField.forceActiveFocus();
                                }
                            }
                        }

                    }

                    // Fill space shortcut
                    TextField {
                        id: editorFillShortcutField

                        property bool capturing: false

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Fill space:")
                        text: capturing ? i18n("Press keys...") : (settingsController.editorFillShortcut || "")
                        placeholderText: "Ctrl+Shift+F"
                        readOnly: true
                        color: capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => {
                            if (!capturing)
                                return ;

                            event.accepted = true;
                            if (event.key === Qt.Key_Escape) {
                                capturing = false;
                                return ;
                            }
                            var key = event.key;
                            if (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_AltGr || key === Qt.Key_Meta)
                                return ;

                            var seq = _buildSequence(event.key, event.modifiers);
                            if (seq.length > 0) {
                                settingsController.editorFillShortcut = seq;
                                capturing = false;
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus && capturing)
                                capturing = false;

                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to expand selected zone to fill available space. Click to capture.")

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!editorFillShortcutField.capturing) {
                                    editorFillShortcutField.capturing = true;
                                    editorFillShortcutField.forceActiveFocus();
                                }
                            }
                        }

                    }

                    Button {
                        Kirigami.FormData.label: i18n("Reset:")
                        text: i18n("Reset to defaults")
                        icon.name: "edit-reset"
                        onClicked: settingsController.resetEditorDefaults()
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Reset all editor shortcuts to their default values")
                    }

                }

            }

        }

        // =================================================================
        // SNAPPING CARD
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: snappingCard.implicitHeight

            Kirigami.Card {
                id: snappingCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Snapping")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Grid snapping:")
                        text: i18n("Enable grid snapping")
                        checked: settingsController.editorGridSnappingEnabled
                        onToggled: settingsController.editorGridSnappingEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Snap zones to a grid while dragging or resizing")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Edge snapping:")
                        text: i18n("Enable edge snapping")
                        checked: settingsController.editorEdgeSnappingEnabled
                        onToggled: settingsController.editorEdgeSnappingEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Snap zones to edges of other zones while dragging or resizing")
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Grid interval X:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: snapIntervalXSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 0.01
                            to: 0.5
                            stepSize: 0.01
                            value: settingsController.editorSnapIntervalX
                            onMoved: settingsController.editorSnapIntervalX = value
                        }

                        Label {
                            text: Math.round(snapIntervalXSlider.value * 100) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Grid interval Y:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: snapIntervalYSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 0.01
                            to: 0.5
                            stepSize: 0.01
                            value: settingsController.editorSnapIntervalY
                            onMoved: settingsController.editorSnapIntervalY = value
                        }

                        Label {
                            text: Math.round(snapIntervalYSlider.value * 100) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
                        }

                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    // Snap override modifier (simplified from ModifierAndMouseCheckBoxes)
                    RowLayout {
                        Kirigami.FormData.label: i18n("Override modifier:")
                        spacing: Kirigami.Units.smallSpacing

                        ComboBox {
                            id: snapOverrideCombo

                            // Qt::KeyboardModifier bit values
                            readonly property var modifierOptions: [{
                                "text": i18n("None"),
                                "value": 0
                            }, {
                                "text": i18n("Shift"),
                                "value": (1 << 25)
                            }, {
                                "text": i18n("Ctrl"),
                                "value": (1 << 26)
                            }, {
                                "text": i18n("Alt"),
                                "value": (1 << 27)
                            }, {
                                "text": i18n("Meta"),
                                "value": (1 << 28)
                            }]

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            model: modifierOptions.map((o) => {
                                return o.text;
                            })
                            currentIndex: {
                                let val = root.kcmModule.editorSnapOverrideModifier || 0;
                                for (let i = 0; i < modifierOptions.length; i++) {
                                    if (modifierOptions[i].value === val)
                                        return i;

                                }
                                return 0;
                            }
                            onActivated: (idx) => {
                                root.kcmModule.editorSnapOverrideModifier = modifierOptions[idx].value;
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Hold this modifier to temporarily override snap behavior")
                        }

                    }

                }

            }

        }

        // =================================================================
        // FILL ON DROP CARD
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: fillOnDropCard.implicitHeight

            Kirigami.Card {
                id: fillOnDropCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Fill on Drop")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: fillOnDropEnabledCheck

                        Kirigami.FormData.label: i18n("Enable:")
                        text: i18n("Fill zone on drop with modifier key")
                        checked: root.kcmModule.fillOnDropEnabled
                        onToggled: root.kcmModule.fillOnDropEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, holding the modifier key while dropping a zone expands it to fill available space")
                    }

                    // Fill on Drop modifier (simplified from ModifierAndMouseCheckBoxes)
                    RowLayout {
                        Kirigami.FormData.label: i18n("Modifier:")
                        spacing: Kirigami.Units.smallSpacing

                        ComboBox {
                            id: fillOnDropModifierCombo

                            readonly property var modifierOptions: [{
                                "text": i18n("None"),
                                "value": 0
                            }, {
                                "text": i18n("Shift"),
                                "value": (1 << 25)
                            }, {
                                "text": i18n("Ctrl"),
                                "value": (1 << 26)
                            }, {
                                "text": i18n("Alt"),
                                "value": (1 << 27)
                            }, {
                                "text": i18n("Meta"),
                                "value": (1 << 28)
                            }]

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            enabled: fillOnDropEnabledCheck.checked
                            model: modifierOptions.map((o) => {
                                return o.text;
                            })
                            currentIndex: {
                                let val = root.kcmModule.fillOnDropModifier || 0;
                                for (let i = 0; i < modifierOptions.length; i++) {
                                    if (modifierOptions[i].value === val)
                                        return i;

                                }
                                return 0;
                            }
                            onActivated: (idx) => {
                                root.kcmModule.fillOnDropModifier = modifierOptions[idx].value;
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Hold this modifier while dropping to fill available space")
                        }

                    }

                }

            }

        }

    }

    // Keep inputs in sync with settingsController properties
    Connections {
        function onEditorDuplicateShortcutChanged() {
            if (!editorDuplicateShortcutField.capturing)
                editorDuplicateShortcutField.text = settingsController.editorDuplicateShortcut;

        }

        function onEditorSplitHorizontalShortcutChanged() {
            if (!editorSplitHorizontalShortcutField.capturing)
                editorSplitHorizontalShortcutField.text = settingsController.editorSplitHorizontalShortcut;

        }

        function onEditorSplitVerticalShortcutChanged() {
            if (!editorSplitVerticalShortcutField.capturing)
                editorSplitVerticalShortcutField.text = settingsController.editorSplitVerticalShortcut;

        }

        function onEditorFillShortcutChanged() {
            if (!editorFillShortcutField.capturing)
                editorFillShortcutField.text = settingsController.editorFillShortcut;

        }

        function onEditorSnapIntervalXChanged() {
            if (!snapIntervalXSlider.pressed)
                snapIntervalXSlider.value = settingsController.editorSnapIntervalX;

        }

        function onEditorSnapIntervalYChanged() {
            if (!snapIntervalYSlider.pressed)
                snapIntervalYSlider.value = settingsController.editorSnapIntervalY;

        }

        target: settingsController
    }

}
