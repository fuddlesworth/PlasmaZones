// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    objectName: "shortcutReference"
    property string screenName: ""
    property var map: null
    property var catalog: []
    property bool catalogAvailable: true
    property bool catalogLoading: false
    property string catalogError: ""
    property bool layoutsAvailable: true
    property string workspaceName: ""
    property Component decoration: null
    property var surfaceEffects: null
    readonly property rect materialRect: Qt.rect(sheet.x, sheet.y, sheet.width, sheet.height)
    readonly property real materialRadius: sheet.radius
    readonly property bool materialBlurred: visible && Window.window && Window.window.visible && Appearance.settings.material !== "solid"
    function applyMaterial(): void {
        if (!surfaceEffects || !root.Window.window)
            return;
        const region = materialBlurred ? root.mapToItem(null, materialRect.x, materialRect.y, materialRect.width, materialRect.height) : Qt.rect(0, 0, 0, 0);
        surfaceEffects.setBlurBehind(root, region, Qt.rect(0, 0, 0, 0), materialRadius);
    }
    onMaterialRectChanged: Qt.callLater(applyMaterial)
    onMaterialRadiusChanged: Qt.callLater(applyMaterial)
    onMaterialBlurredChanged: Qt.callLater(applyMaterial)
    onSurfaceEffectsChanged: Qt.callLater(applyMaterial)
    onParentChanged: Qt.callLater(applyMaterial)
    onXChanged: Qt.callLater(applyMaterial)
    onYChanged: Qt.callLater(applyMaterial)
    Window.onWindowChanged: Qt.callLater(applyMaterial)
    property bool open: false
    property string filter: ""
    property string scope: "tiling"
    property bool assignedOnly: false
    property bool showGuide: true
    property bool scopeChosen: false
    property var expandedRows: ({})
    readonly property int mode: map ? map.mode : -1
    readonly property int currentDesktop: map ? map.currentDesktop : -1
    readonly property string currentScope: mode === 0 ? "snapping" : mode === 1 ? "tiling" : mode === 2 ? "scrolling" : "general"
    readonly property bool placementScope: ["tiling", "snapping", "scrolling"].includes(scope)
    readonly property color accent: Appearance.stops[scope === "snapping" ? 0 : scope === "scrolling" ? 2 : scope === "shell" ? 3 : 1]
    readonly property bool unavailable: !catalogAvailable || catalogError.length > 0
    readonly property bool fullScreen: true
    readonly property int actionCount: reference.count
    readonly property var groups: reference.groups
    readonly property string guideId: scope === "tiling" ? "focus_master" : scope === "scrolling" ? "scroll_center_column" : "snap_to_zone_1"
    readonly property var guideBindings: {
        const row = catalog.find(row => row.id === guideId);
        return row ? (row.triggers || []).map(trigger => reference.keyPartsForTrigger(trigger)) : [];
    }
    property real progress: 0
    property bool wasOpened: false
    signal closeRequested
    signal released
    signal retryRequested
    implicitWidth: 1440
    implicitHeight: 900
    visible: open || progress > 0
    enabled: open
    opacity: progress
    focus: true
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Keyboard shortcuts")

    ShortcutReferenceModel {
        id: reference
        rows: root.catalog
        scope: root.scope
        query: root.filter
        assignedOnly: root.assignedOnly
        layoutsAvailable: root.layoutsAvailable
    }
    function focusSearch(): void {
        search.forceActiveFocus(Qt.OtherFocusReason);
    }
    function activate(): void {
        wasOpened = true;
        scopeChosen = false;
        filter = "";
        scope = currentScope;
        progress = 1;
        Qt.callLater(focusSearch);
    }
    function handleEscape(): void {
        if (filter.length) {
            filter = "";
            focusSearch();
        } else
            closeRequested();
    }
    function selectScope(value): void {
        scopeChosen = true;
        scope = value;
        viewport.contentY = 0;
    }
    function toggleRow(id): void {
        const next = Object.assign({}, expandedRows);
        next[id] = !next[id];
        expandedRows = next;
    }
    function cycleFocus(backward): void {
        let next = root.Window.window?.activeFocusItem || root;
        for (let i = 0; i < 1000; ++i) {
            next = next.nextItemInFocusChain(!backward);
            let parent = next;
            while (parent && parent !== root)
                parent = parent.parent;
            if (parent === root && next !== root && next.visible && next.enabled && next.activeFocusOnTab) {
                next.forceActiveFocus(backward ? Qt.BacktabFocusReason : Qt.TabFocusReason);
                return;
            }
        }
    }
    onOpenChanged: {
        if (open)
            activate();
        else
            progress = 0;
    }
    onCurrentScopeChanged: if (open && !scopeChosen)
        scope = currentScope
    Component.onCompleted: if (open)
        activate()
    onProgressChanged: if (!open && wasOpened && progress === 0) {
        wasOpened = false;
        released();
    }
    Behavior on progress {
        NumberAnimation {
            duration: Appearance.motion ? (root.open ? 180 : 140) : 0
            easing.type: Easing.OutCubic
        }
    }
    Keys.onEscapePressed: handleEscape()
    Keys.onTabPressed: event => cycleFocus(!!(event.modifiers & Qt.ShiftModifier))
    Keys.onBacktabPressed: cycleFocus(true)
    Shortcut {
        sequence: "Tab"
        enabled: root.open
        onActivated: root.cycleFocus(false)
    }
    Shortcut {
        sequence: "Shift+Tab"
        enabled: root.open
        onActivated: root.cycleFocus(true)
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: root.open
        onActivated: {
            root.focusSearch();
            search.selectAll();
        }
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            const item = root.Window.window.activeFocusItem;
            let parent = item;
            while (parent && parent !== viewport.contentItem)
                parent = parent.parent;
            if (!parent || !item)
                return;
            const point = item.mapToItem(viewport.contentItem, 0, 0);
            if (point.y < viewport.contentY)
                viewport.contentY = Math.max(0, point.y - 8);
            else if (point.y + item.height > viewport.contentY + viewport.height)
                viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, point.y + item.height - viewport.height + 8));
        }
    }
    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(Appearance.recess, .48)
        MouseArea {
            anchors.fill: parent
            onClicked: root.closeRequested()
        }
    }
    Item {
        id: sheet
        objectName: "shortcutSheet"
        readonly property real radius: Appearance.radius
        readonly property bool narrow: width < 650
        readonly property bool shortScreen: height < 600
        readonly property int sidebarWidth: width < 1050 ? 180 : 218
        readonly property real inset: width < 1050 ? 20 : 28
        width: Math.max(1, Math.min(1180, root.width - Math.min(68, root.width * .08)))
        height: Math.max(1, Math.min(724, root.height - (root.height < 650 ? 76 : 128)))
        x: (root.width - width) / 2
        y: Math.min(root.height - height - 16, (root.height - height) / 2 + (root.height < 650 ? 0 : 22))
        ShellSurface {
            id: ground
            property bool shaderAnchor: true
            anchors.fill: parent
            accented: true
        }
        DecorationSlot {
            anchors.fill: parent
            component: root.decoration
            contentItem: ground
            surfacePath: "shell.phosphor.cheatsheet"
            focused: root.activeFocus
            layeredStages: true
        }
        MouseArea {
            anchors.fill: parent
        }
        Rectangle {
            x: 1
            y: 1
            width: sheet.narrow ? sheet.width - 2 : sheet.sidebarWidth
            height: sheet.narrow ? 61 : sheet.height - 2
            color: Qt.alpha(Appearance.recess, .3)
            radius: Appearance.radius
            Rectangle {
                visible: !sheet.narrow
                anchors.right: parent.right
                width: parent.radius
                height: parent.height
                color: parent.color
            }
        }
        Rectangle {
            x: sheet.narrow ? 0 : sheet.sidebarWidth
            y: sheet.narrow ? 62 : 0
            width: sheet.narrow ? sheet.width : 1
            height: sheet.narrow ? 1 : sheet.height
            color: Appearance.outline
        }
        ShortcutSidebar {
            id: sidebar
            width: sheet.narrow ? sheet.width : sheet.sidebarWidth
            height: sheet.narrow ? 62 : sheet.height
            scope: root.scope
            currentMode: root.currentScope
            workspaceName: root.workspaceName.length ? root.workspaceName : root.currentDesktop >= 0 ? qsTr("Workspace %1").arg(root.currentDesktop + 1) : ""
            horizontal: sheet.narrow
            onChosen: scope => root.selectScope(scope)
        }
        ColumnLayout {
            id: main
            x: sheet.narrow ? 0 : sheet.sidebarWidth + 1
            y: sheet.narrow ? 63 : 0
            width: sheet.width - x
            height: sheet.height - y
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: sheet.inset
                Layout.rightMargin: sheet.inset
                Layout.topMargin: sheet.shortScreen ? 14 : Appearance.compact ? 20 : 25
                Layout.bottomMargin: sheet.shortScreen ? 10 : 17
                spacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    ShortcutText {
                        Layout.fillWidth: true
                        text: qsTr("Keyboard shortcuts")
                        size: sheet.shortScreen ? 21 : 24.5
                        font.weight: Font.Medium
                        font.letterSpacing: -.65
                    }
                    ShortcutText {
                        visible: !sheet.shortScreen
                        Layout.fillWidth: true
                        text: qsTr("Find your next move.")
                        muted: true
                    }
                }
                ShellButton {
                    objectName: "shortcutClose"
                    iconName: "window-close"
                    label: qsTr("Close shortcut reference")
                    outlined: true
                    flat: true
                    implicitWidth: 32
                    implicitHeight: 32
                    onClicked: root.closeRequested()
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: sheet.inset
                Layout.rightMargin: sheet.inset
                implicitHeight: Math.max(43, search.implicitHeight + 8)
                color: Qt.alpha(Appearance.recess, .78)
                border.color: search.activeFocus ? root.accent : Appearance.outline
                radius: Appearance.radius * .5
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    spacing: 10
                    ShellIcon {
                        Layout.preferredWidth: 17
                        Layout.preferredHeight: 17
                        source: "edit-find"
                        color: Appearance.muted
                    }
                    Basic.TextField {
                        id: search
                        objectName: "shortcutSearch"
                        Layout.fillWidth: true
                        text: root.filter
                        onTextEdited: {
                            root.filter = text;
                            viewport.contentY = 0;
                        }
                        placeholderText: qsTr("Find an action or key…")
                        placeholderTextColor: Appearance.muted
                        color: Appearance.text
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 12 * Appearance.textScale
                        background: null
                        selectByMouse: true
                        Accessible.name: qsTr("Search shortcuts")
                        Keys.onEscapePressed: root.handleEscape()
                    }
                    ShortcutKeys {
                        visible: !root.filter.length
                        bindings: [["Ctrl", "F"]]
                        Layout.preferredWidth: visible ? implicitWidth : 0
                    }
                    ShellButton {
                        visible: root.filter.length > 0
                        objectName: "shortcutClearSearch"
                        iconName: "edit-clear"
                        label: qsTr("Clear shortcut search")
                        implicitWidth: 26
                        implicitHeight: 26
                        flat: true
                        onClicked: {
                            root.filter = "";
                            root.focusSearch();
                        }
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: sheet.inset
                Layout.rightMargin: sheet.inset
                Layout.topMargin: 8
                Layout.bottomMargin: 10
                spacing: 10
                ShortcutText {
                    width: Math.max(100, parent.width - assigned.implicitWidth - guideToggle.implicitWidth - 20)
                    height: Math.max(30, implicitHeight)
                    verticalAlignment: Text.AlignVCenter
                    text: root.catalogLoading ? qsTr("Loading shortcuts…") : root.unavailable ? qsTr("Catalog unavailable") : root.placementScope ? qsTr("%1 actions · includes shared shortcuts").arg(reference.count) : qsTr("%1 actions").arg(reference.count)
                    muted: true
                    size: 10
                }
                Basic.CheckBox {
                    id: assigned
                    objectName: "shortcutAssignedOnly"
                    text: qsTr("Assigned only")
                    checked: root.assignedOnly
                    onToggled: root.assignedOnly = checked
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 10 * Appearance.textScale
                    palette.windowText: Appearance.muted
                    palette.highlight: root.accent
                    implicitHeight: Math.max(30, contentItem.implicitHeight + 8)
                    indicator: Rectangle {
                        x: assigned.leftPadding
                        y: (assigned.height - height) / 2
                        implicitWidth: 13 * Appearance.textScale
                        implicitHeight: implicitWidth
                        radius: 2
                        color: assigned.checked ? root.accent : Appearance.recess
                        border.color: assigned.visualFocus ? root.accent : Appearance.outline
                        ShellIcon {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: "checkmark"
                            color: Appearance.recess
                            visible: assigned.checked
                        }
                    }
                    contentItem: ShortcutText {
                        leftPadding: assigned.indicator.width + assigned.spacing
                        text: assigned.text
                        size: 10
                        muted: true
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                ShellButton {
                    id: guideToggle
                    objectName: "shortcutGuideToggle"
                    visible: root.placementScope
                    implicitWidth: visible ? contentItem.implicitWidth + 12 : 0
                    text: root.showGuide ? qsTr("Hide field guide") : qsTr("Show field guide")
                    iconName: "view-grid"
                    labelSize: 10
                    foreground: Appearance.muted
                    flat: true
                    checkable: true
                    checked: root.showGuide
                    onClicked: root.showGuide = !root.showGuide
                }
            }
            Flickable {
                id: viewport
                objectName: "shortcutViewport"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: sheet.inset
                Layout.rightMargin: sheet.inset
                contentWidth: width
                contentHeight: content.implicitHeight + 24
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Basic.ScrollBar.vertical: Basic.ScrollBar {
                    width: 5
                }
                Column {
                    id: content
                    width: viewport.width
                    spacing: 23
                    ShortcutGuide {
                        width: parent.width
                        visible: root.showGuide && root.placementScope && !root.filter.length && !root.unavailable && !root.catalogLoading
                        scope: root.scope
                        accent: root.accent
                        bindings: root.guideBindings
                    }
                    ShortcutText {
                        visible: root.scope === "shell" && !root.unavailable && !root.catalogLoading
                        width: parent.width
                        text: qsTr("Shell actions use compositor shortcuts. A missing binding means it hasn’t been provided here.")
                        muted: true
                    }
                    GridLayout {
                        id: grid
                        width: parent.width
                        visible: !root.unavailable && !root.catalogLoading && reference.count > 0
                        columns: sheet.width < 1050 ? 1 : 2
                        columnSpacing: 26
                        rowSpacing: 22
                        Repeater {
                            model: grid.visible ? reference.groups : []
                            delegate: ShortcutGroup {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: (grid.width - (grid.columns - 1) * grid.columnSpacing) / grid.columns
                                Layout.alignment: Qt.AlignTop
                                group: modelData
                                accent: root.accent
                                expandedRows: root.expandedRows
                                onExpansionToggled: id => root.toggleRow(id)
                            }
                        }
                    }
                    Column {
                        visible: root.unavailable || root.catalogLoading || reference.count === 0
                        width: parent.width
                        spacing: 16
                        topPadding: 28
                        ShellIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 30
                            height: 30
                            source: root.unavailable || root.catalogLoading ? "input-keyboard" : "edit-find"
                            color: root.accent
                        }
                        ShortcutText {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            size: 16
                            objectName: "shortcutEmptyTitle"
                            text: root.catalogLoading ? qsTr("Loading your shortcuts…") : root.unavailable ? qsTr("Shortcuts aren’t available yet.") : root.filter.length ? qsTr("No matching shortcuts.") : root.assignedOnly ? qsTr("No assigned shortcuts here.") : qsTr("No shortcuts in this section.")
                        }
                        ShortcutText {
                            width: Math.min(parent.width, 360)
                            anchors.horizontalCenter: parent.horizontalCenter
                            horizontalAlignment: Text.AlignHCenter
                            muted: true
                            text: root.catalogLoading ? qsTr("Reading your configured bindings.") : root.unavailable ? qsTr("The shortcut service couldn’t be reached. Try reconnecting to load your bindings.") : root.filter.length ? qsTr("Try an action, a key, or another section.") : root.assignedOnly ? qsTr("Turn off Assigned only to see actions you can bind.") : qsTr("Choose another section to browse the available actions.")
                        }
                        ShellButton {
                            objectName: "shortcutRecover"
                            visible: !root.catalogLoading && (root.unavailable || root.filter.length || root.assignedOnly)
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.unavailable ? qsTr("Try again") : root.filter.length ? qsTr("Clear search") : qsTr("Show unassigned actions")
                            outlined: true
                            implicitHeight: 36
                            onClicked: {
                                if (root.unavailable)
                                    root.retryRequested();
                                else if (root.filter.length)
                                    root.filter = "";
                                else
                                    root.assignedOnly = false;
                                root.focusSearch();
                            }
                        }
                        ShellButton {
                            visible: !root.unavailable && !root.catalogLoading && root.filter.length && root.scope !== "all"
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("Search all shortcuts")
                            flat: true
                            onClicked: root.selectScope("all")
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Appearance.outline
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: sheet.shortScreen ? 10 : 14
                Layout.leftMargin: sheet.inset
                Layout.rightMargin: sheet.inset
                spacing: 7
                ShellIcon {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    source: "input-keyboard"
                    color: Appearance.muted
                }
                ShortcutText {
                    Layout.fillWidth: true
                    text: qsTr("Meta is the Super / Windows key")
                    size: 10
                    muted: true
                }
                ShortcutKeys {
                    bindings: [[qsTr("Esc")]]
                }
                ShortcutText {
                    text: root.filter.length ? qsTr("Clear search") : qsTr("Close reference")
                    size: 10
                    muted: true
                }
            }
        }
    }
}
