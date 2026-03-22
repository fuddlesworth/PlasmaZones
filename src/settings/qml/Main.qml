// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ApplicationWindow {
    id: window

    // Responsive sidebar: collapse to icon-only below 750px
    readonly property bool sidebarCompact: window.width < Kirigami.Units.gridUnit * 50
    // Track page navigation for transitions
    property int _previousIndex: 0
    // ── Drill-down sidebar state ─────────────────────────────────────
    // "main" = top-level list; otherwise the parent name (e.g. "snapping")
    property string _sidebarMode: "main"
    // Main sidebar items
    readonly property var _mainItems: [{
        "name": "layouts",
        "label": "Layouts",
        "iconName": "view-grid",
        "hasChildren": false
    }, {
        "name": "snapping",
        "label": "Snapping",
        "iconName": "view-split-left-right",
        "hasChildren": true
    }, {
        "name": "tiling",
        "label": "Tiling",
        "iconName": "window-duplicate",
        "hasChildren": true
    }, {
        "name": "apprules",
        "label": "App Rules",
        "iconName": "application-x-executable",
        "hasChildren": false
    }, {
        "name": "exclusions",
        "label": "Exclusions",
        "iconName": "dialog-cancel",
        "hasChildren": false
    }, {
        "name": "editor",
        "label": "Editor",
        "iconName": "document-edit",
        "hasChildren": false
    }, {
        "name": "general",
        "label": "General",
        "iconName": "configure",
        "hasChildren": false
    }, {
        "name": "about",
        "label": "About",
        "iconName": "help-about",
        "hasChildren": false
    }]
    // Children for each parent
    readonly property var _childItems: ({
        "snapping": [{
            "name": "snap-appearance",
            "label": "Appearance",
            "iconName": "preferences-desktop-color"
        }, {
            "name": "snap-behavior",
            "label": "Behavior",
            "iconName": "preferences-system"
        }, {
            "name": "snap-gaps",
            "label": "Gaps",
            "iconName": "distribute-horizontal"
        }, {
            "name": "snap-zoneselector",
            "label": "Zone Selector",
            "iconName": "view-choose"
        }, {
            "name": "snap-effects",
            "label": "Effects",
            "iconName": "preferences-desktop-effects"
        }, {
            "name": "snap-assignments",
            "label": "Assignments",
            "iconName": "view-list-details"
        }, {
            "name": "snap-shortcuts",
            "label": "Quick Shortcuts",
            "iconName": "bookmark"
        }],
        "tiling": [{
            "name": "tile-appearance",
            "label": "Appearance",
            "iconName": "preferences-desktop-color"
        }, {
            "name": "tile-behavior",
            "label": "Behavior",
            "iconName": "preferences-system"
        }, {
            "name": "tile-gaps",
            "label": "Gaps",
            "iconName": "distribute-horizontal"
        }, {
            "name": "tile-algorithm",
            "label": "Algorithm",
            "iconName": "view-grid"
        }, {
            "name": "tile-assignments",
            "label": "Assignments",
            "iconName": "view-list-details"
        }, {
            "name": "tile-shortcuts",
            "label": "Quick Shortcuts",
            "iconName": "bookmark"
        }]
    })
    // Page component map -- loaded on demand by Loader
    readonly property var _pageComponents: ({
        "layouts": "LayoutsPage.qml",
        "snap-appearance": "SnappingAppearancePage.qml",
        "snap-behavior": "SnappingBehaviorPage.qml",
        "snap-gaps": "SnappingGapsPage.qml",
        "snap-zoneselector": "SnappingZoneSelectorPage.qml",
        "snap-effects": "SnappingEffectsPage.qml",
        "tile-appearance": "TilingAppearancePage.qml",
        "tile-behavior": "TilingBehaviorPage.qml",
        "tile-gaps": "TilingGapsPage.qml",
        "tile-algorithm": "TilingAlgorithmPage.qml",
        "snap-assignments": "SnappingAssignmentsPage.qml",
        "snap-shortcuts": "SnappingQuickShortcutsPage.qml",
        "tile-assignments": "TilingAssignmentsPage.qml",
        "tile-shortcuts": "TilingQuickShortcutsPage.qml",
        "apprules": "AssignmentsAppRulesPage.qml",
        "exclusions": "ExclusionsPage.qml",
        "editor": "EditorPage.qml",
        "general": "GeneralPage.qml",
        "about": "AboutPage.qml"
    })

    function _rebuildSidebar() {
        sidebarModel.clear();
        if (_sidebarMode === "main") {
            for (let i = 0; i < _mainItems.length; i++) {
                let item = _mainItems[i];
                sidebarModel.append({
                    "name": item.name,
                    "label": item.label,
                    "iconName": item.iconName,
                    "hasChildren": item.hasChildren,
                    "isBackButton": false
                });
            }
        } else {
            // Find parent label
            let parentLabel = _sidebarMode;
            for (let i = 0; i < _mainItems.length; i++) {
                if (_mainItems[i].name === _sidebarMode) {
                    parentLabel = _mainItems[i].label;
                    break;
                }
            }
            // Back button row
            sidebarModel.append({
                "name": "__back__",
                "label": parentLabel,
                "iconName": "arrow-left",
                "hasChildren": false,
                "isBackButton": true
            });
            // Child items
            let children = _childItems[_sidebarMode] || [];
            for (let i = 0; i < children.length; i++) {
                sidebarModel.append({
                    "name": children[i].name,
                    "label": children[i].label,
                    "iconName": children[i].iconName,
                    "hasChildren": false,
                    "isBackButton": false
                });
            }
        }
    }

    // Helper: find the parent label for the current sidebar mode
    function _parentLabel() {
        for (let i = 0; i < _mainItems.length; i++) {
            if (_mainItems[i].name === _sidebarMode)
                return _mainItems[i].label;

        }
        return _sidebarMode;
    }

    // Helper: find a subpage label by name
    function _subPageLabel(pageName) {
        let children = _childItems[_sidebarMode];
        if (!children)
            return pageName;

        for (let i = 0; i < children.length; i++) {
            if (children[i].name === pageName)
                return children[i].label;

        }
        return pageName;
    }

    // Helper: find a main-item label by name
    function _mainItemLabel(pageName) {
        for (let i = 0; i < _mainItems.length; i++) {
            if (_mainItems[i].name === pageName)
                return _mainItems[i].label;

        }
        return pageName;
    }

    // Drill into a parent category and select the first child
    function _drillIn(parentName) {
        _sidebarMode = parentName;
        _rebuildSidebar();
        let children = _childItems[parentName];
        if (children && children.length > 0)
            settingsController.activePage = children[0].name;

    }

    // Return to the main sidebar list, selecting the parent item
    function _drillOut() {
        let parent = _sidebarMode;
        _sidebarMode = "main";
        _rebuildSidebar();
        // Navigate to the first non-parent item (e.g. "layouts")
        // so the sidebar has a valid highlight
        settingsController.activePage = "layouts";
    }

    title: i18n("PlasmaZones Settings")
    width: Kirigami.Units.gridUnit * 80
    height: Kirigami.Units.gridUnit * 48
    visible: true
    onClosing: {
        settingsController.saveWindowGeometry(window.x, window.y, window.width, window.height);
    }
    Component.onCompleted: {
        // Restore window geometry
        var geo = settingsController.loadWindowGeometry();
        if (geo.width > 0 && geo.height > 0) {
            window.width = geo.width;
            window.height = geo.height;
        }
        if (geo.hasPosition) {
            window.x = geo.x;
            window.y = geo.y;
        }
        // Build initial sidebar
        _rebuildSidebar();
        // If the active page is a child of a category, drill in
        let page = settingsController.activePage;
        let parents = Object.keys(_childItems);
        for (let p = 0; p < parents.length; p++) {
            let children = _childItems[parents[p]];
            for (let c = 0; c < children.length; c++) {
                if (children[c].name === page) {
                    _sidebarMode = parents[p];
                    _rebuildSidebar();
                    return ;
                }
            }
        }
    }

    // Auto-drill-out if feature disabled while viewing its subpages
    Connections {
        function onSnappingEnabledChanged() {
            if (!appSettings.snappingEnabled && _sidebarMode === "snapping")
                _drillOut();

        }

        function onAutotileEnabledChanged() {
            if (!appSettings.autotileEnabled && _sidebarMode === "tiling")
                _drillOut();

        }

        target: appSettings
    }

    // Visible sidebar model (rebuilt when _sidebarMode changes)
    ListModel {
        id: sidebarModel
    }

    Shortcut {
        sequence: "Ctrl+PgUp"
        onActivated: {
            let idx = sidebar.currentIndex;
            for (let i = idx - 1; i >= 0; i--) {
                let item = sidebarModel.get(i);
                if (!item.isBackButton && !item.hasChildren) {
                    settingsController.activePage = item.name;
                    return ;
                }
            }
            // At boundary — drill out if in a sub-category
            if (_sidebarMode !== "main")
                _drillOut();

        }
    }

    Shortcut {
        sequence: "Ctrl+PgDown"
        onActivated: {
            let idx = sidebar.currentIndex;
            for (let i = idx + 1; i < sidebarModel.count; i++) {
                let item = sidebarModel.get(i);
                if (!item.isBackButton && !item.hasChildren) {
                    settingsController.activePage = item.name;
                    return ;
                }
            }
            // At boundary — drill out if in a sub-category
            if (_sidebarMode !== "main")
                _drillOut();

        }
    }

    RowLayout {
        id: mainContent

        anchors.fill: parent
        spacing: 0

        // =================================================================
        // SIDEBAR
        // =================================================================
        Pane {
            id: sidebarPane

            Layout.fillHeight: true
            Layout.preferredWidth: window.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12
            Layout.minimumWidth: window.sidebarCompact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 12
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Navigation list
                ListView {
                    id: sidebar

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: sidebarModel
                    currentIndex: {
                        for (var i = 0; i < sidebarModel.count; i++) {
                            if (sidebarModel.get(i).name === settingsController.activePage)
                                return i;

                        }
                        return -1;
                    }
                    clip: true

                    delegate: ItemDelegate {
                        id: navDelegate

                        required property int index
                        required property string name
                        required property string label
                        required property string iconName
                        required property bool hasChildren
                        required property bool isBackButton
                        readonly property bool isActive: {
                            if (isBackButton)
                                return false;

                            if (hasChildren)
                                return false;

                            return name === settingsController.activePage;
                        }

                        width: sidebar.width
                        height: isBackButton ? Kirigami.Units.gridUnit * 2.6 : Kirigami.Units.gridUnit * 2.2
                        highlighted: isActive
                        onClicked: {
                            if (isBackButton) {
                                window._drillOut();
                                return ;
                            }
                            if (hasChildren) {
                                // Block drill-down if the feature is disabled
                                if (name === "snapping" && !appSettings.snappingEnabled)
                                    return ;

                                if (name === "tiling" && !appSettings.autotileEnabled)
                                    return ;

                                window._drillIn(name);
                                return ;
                            }
                            window._previousIndex = sidebar.currentIndex;
                            settingsController.activePage = name;
                        }
                        leftPadding: window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing
                        rightPadding: window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing
                        ToolTip.visible: window.sidebarCompact && navDelegate.hovered
                        ToolTip.text: label
                        ToolTip.delay: 300

                        background: Rectangle {
                            color: {
                                if (navDelegate.isActive)
                                    return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12);

                                if (navDelegate.hovered)
                                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);

                                return "transparent";
                            }
                            radius: Kirigami.Units.smallSpacing

                            // Left accent bar for active item
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.round(Kirigami.Units.devicePixelRatio * 2.5)
                                height: parent.height * 0.5
                                radius: width / 2
                                color: Kirigami.Theme.highlightColor
                                visible: navDelegate.isActive
                            }

                            // Bottom separator after back button
                            Rectangle {
                                visible: navDelegate.isBackButton
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Kirigami.Units.smallSpacing
                                anchors.rightMargin: Kirigami.Units.smallSpacing
                                height: Math.round(Kirigami.Units.devicePixelRatio)
                                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                            }

                            Behavior on color {
                                ColorAnimation {
                                    duration: 120
                                }

                            }

                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: navDelegate.iconName
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.fillWidth: window.sidebarCompact
                                Layout.alignment: Qt.AlignVCenter
                                opacity: {
                                    if (navDelegate.isBackButton)
                                        return 0.7;

                                    if (navDelegate.isActive)
                                        return 1;

                                    return 0.7;
                                }

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 120
                                    }

                                }

                            }

                            Label {
                                text: navDelegate.label
                                Layout.fillWidth: true
                                font.weight: {
                                    if (navDelegate.isBackButton)
                                        return Font.DemiBold;

                                    if (navDelegate.isActive)
                                        return Font.DemiBold;

                                    return Font.Normal;
                                }
                                opacity: {
                                    if (navDelegate.isBackButton)
                                        return 0.8;

                                    if (navDelegate.isActive)
                                        return 1;

                                    return 0.7;
                                }
                                visible: !window.sidebarCompact

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 120
                                    }

                                }

                            }

                            // Enable/disable toggle for snapping and tiling
                            SettingsSwitch {
                                visible: (navDelegate.name === "snapping" || navDelegate.name === "tiling") && !window.sidebarCompact
                                checked: navDelegate.name === "snapping" ? appSettings.snappingEnabled : appSettings.autotileEnabled
                                accessibleName: navDelegate.label
                                onToggled: {
                                    if (navDelegate.name === "snapping")
                                        appSettings.snappingEnabled = !appSettings.snappingEnabled;
                                    else
                                        appSettings.autotileEnabled = !appSettings.autotileEnabled;
                                }
                            }

                            // Right chevron for items with children
                            Kirigami.Icon {
                                source: "go-next"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                Layout.alignment: Qt.AlignVCenter
                                opacity: {
                                    if (navDelegate.name === "snapping" && !appSettings.snappingEnabled)
                                        return 0.15;

                                    if (navDelegate.name === "tiling" && !appSettings.autotileEnabled)
                                        return 0.15;

                                    return 0.3;
                                }
                                visible: navDelegate.hasChildren && !window.sidebarCompact
                            }

                        }

                    }

                }

                // Daemon status
                Pane {
                    Layout.fillWidth: true
                    padding: Kirigami.Units.smallSpacing
                    topPadding: Kirigami.Units.smallSpacing * 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: Kirigami.Units.smallSpacing

                        Rectangle {
                            width: Kirigami.Units.smallSpacing * 1.5
                            height: Kirigami.Units.smallSpacing * 1.5
                            radius: width / 2
                            Layout.alignment: Qt.AlignVCenter
                            color: settingsController.daemonRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor

                            // Pulsing glow when running
                            SequentialAnimation on opacity {
                                loops: settingsController.daemonRunning ? Animation.Infinite : 0
                                running: settingsController.daemonRunning

                                NumberAnimation {
                                    from: 1
                                    to: 0.4
                                    duration: 1500
                                    easing.type: Easing.InOutSine
                                }

                                NumberAnimation {
                                    from: 0.4
                                    to: 1
                                    duration: 1500
                                    easing.type: Easing.InOutSine
                                }

                            }

                        }

                        Label {
                            text: settingsController.daemonRunning ? i18n("Running") : i18n("Stopped")
                            opacity: 0.7
                            Layout.fillWidth: true
                            visible: !window.sidebarCompact
                        }

                        SettingsSwitch {
                            checked: settingsController.daemonRunning
                            accessibleName: i18n("Toggle daemon")
                            onToggled: {
                                if (settingsController.daemonRunning)
                                    settingsController.daemonController.stopDaemon();
                                else
                                    settingsController.daemonController.startDaemon();
                            }
                        }

                    }

                    background: Rectangle {
                        color: "transparent"

                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: Math.round(Kirigami.Units.devicePixelRatio)
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                        }

                    }

                }

            }

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.InOutQuad
                }

            }

            Behavior on Layout.minimumWidth {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.InOutQuad
                }

            }

            background: Rectangle {
                color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 1)

                // Subtle right edge shadow
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.round(Kirigami.Units.devicePixelRatio)
                    color: Kirigami.Theme.separatorColor !== undefined ? Kirigami.Theme.separatorColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                }

            }

        }

        // =================================================================
        // CONTENT AREA
        // =================================================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // -- Breadcrumb header ----------------------------------------
            // Breadcrumb — always visible (essential in compact mode for context)
            Pane {
                Layout.fillWidth: true
                padding: Kirigami.Units.largeSpacing
                topPadding: Kirigami.Units.smallSpacing * 2
                bottomPadding: Kirigami.Units.smallSpacing * 2

                RowLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: {
                            // Show "Parent > Child"

                            if (window._sidebarMode !== "main")
                                return window._parentLabel() + "  \u203A  " + window._subPageLabel(settingsController.activePage);

                            // Top-level: just show page name
                            return window._mainItemLabel(settingsController.activePage);
                        }
                        opacity: 0.5
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                }

                background: Rectangle {
                    color: "transparent"

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.round(Kirigami.Units.devicePixelRatio)
                        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06)
                    }

                }

            }

            // Page content with crossfade transition
            Item {
                id: pageContainer

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                // Opaque background to prevent bleed-through
                Rectangle {
                    anchors.fill: parent
                    color: Kirigami.Theme.backgroundColor
                }

                Loader {
                    id: pageLoader

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    source: Qt.resolvedUrl(window._pageComponents[settingsController.activePage] || "LayoutsPage.qml")
                    asynchronous: false
                    // Fade in on page change
                    onLoaded: {
                        fadeIn.restart();
                    }

                    NumberAnimation {
                        id: fadeIn

                        target: pageLoader.item
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 180
                        easing.type: Easing.OutCubic
                    }

                }

                // -- Toast notification -----------------------------------
                Rectangle {
                    id: toast

                    property string message: ""

                    function show(msg) {
                        message = msg;
                        toastShow.restart();
                        toastHide.restart();
                    }

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Kirigami.Units.largeSpacing * 2
                    width: toastLabel.implicitWidth + Kirigami.Units.largeSpacing * 3
                    height: toastLabel.implicitHeight + Kirigami.Units.largeSpacing * 1.5
                    radius: height / 2
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.85)
                    opacity: 0
                    visible: opacity > 0
                    z: 100

                    Label {
                        id: toastLabel

                        anchors.centerIn: parent
                        text: toast.message
                        color: Kirigami.Theme.backgroundColor
                        font.weight: Font.Medium
                    }

                    NumberAnimation {
                        id: toastShow

                        target: toast
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 200
                        easing.type: Easing.OutCubic
                    }

                    SequentialAnimation {
                        id: toastHide

                        PauseAnimation {
                            duration: 2000
                        }

                        NumberAnimation {
                            target: toast
                            property: "opacity"
                            from: 1
                            to: 0
                            duration: 400
                            easing.type: Easing.InCubic
                        }

                    }

                }

            }

            // -- Footer action bar ----------------------------------------
            Rectangle {
                Layout.fillWidth: true
                height: Math.round(Kirigami.Units.devicePixelRatio)
                color: settingsController.needsSave ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

                Behavior on color {
                    ColorAnimation {
                        duration: 300
                    }

                }

            }

            Pane {
                Layout.fillWidth: true
                padding: Kirigami.Units.smallSpacing * 1.5
                topPadding: Kirigami.Units.smallSpacing * 2
                bottomPadding: Kirigami.Units.smallSpacing * 2

                RowLayout {
                    anchors.fill: parent

                    Button {
                        text: i18n("Reset")
                        icon.name: "edit-undo"
                        enabled: settingsController.needsSave
                        flat: true
                        onClicked: settingsController.load()
                        opacity: enabled ? 1 : 0.4

                        Behavior on opacity {
                            NumberAnimation {
                                duration: 150
                            }

                        }

                    }

                    Button {
                        text: i18n("Defaults")
                        icon.name: "document-revert"
                        flat: true
                        onClicked: settingsController.defaults()
                        opacity: 0.7
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        id: applyButton

                        text: i18n("Apply")
                        icon.name: "dialog-ok-apply"
                        enabled: settingsController.needsSave
                        highlighted: settingsController.needsSave
                        onClicked: {
                            settingsController.save();
                            toast.show(i18n("Settings applied"));
                        }

                        // Reset scale when not pulsing
                        Binding {
                            target: applyButton
                            property: "scale"
                            value: 1
                            when: !settingsController.needsSave
                            restoreMode: Binding.RestoreBinding
                        }

                        // Subtle pulsing glow when changes are pending
                        SequentialAnimation on scale {
                            loops: Animation.Infinite
                            running: settingsController.needsSave

                            NumberAnimation {
                                from: 1
                                to: 1.03
                                duration: 800
                                easing.type: Easing.InOutSine
                            }

                            NumberAnimation {
                                from: 1.03
                                to: 1
                                duration: 800
                                easing.type: Easing.InOutSine
                            }

                        }

                        Behavior on opacity {
                            NumberAnimation {
                                duration: 150
                            }

                        }

                    }

                }

            }

        }

    }

}
