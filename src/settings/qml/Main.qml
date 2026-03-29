// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ApplicationWindow {
    id: window

    // Expose the layout context menu so Loader-loaded pages can connect to its signals
    readonly property alias layoutContextMenu: layoutContextMenu
    // Responsive sidebar: collapse to icon-only below 750px
    readonly property bool sidebarCompact: window.width < Kirigami.Units.gridUnit * 50
    // Track page navigation for transitions
    property int _previousIndex: 0
    // Shortcut overlay visibility
    property bool _showShortcuts: false
    // Close-without-save guard
    property bool _closeConfirmed: false
    // ── Drill-down sidebar state ─────────────────────────────────────
    // "main" = top-level list; otherwise the parent name (e.g. "snapping")
    property string _sidebarMode: "main"
    // Main sidebar items
    readonly property var _mainItems: [{
        "name": "overview",
        "label": i18n("Overview"),
        "iconName": "monitor",
        "hasChildren": false,
        "hasDividerAfter": true
    }, {
        "name": "layouts",
        "label": i18n("Layouts"),
        "iconName": "view-grid",
        "hasChildren": false,
        "hasDividerAfter": false
    }, {
        "name": "snapping",
        "label": i18n("Snapping"),
        "iconName": "view-split-left-right",
        "hasChildren": true,
        "hasDividerAfter": false
    }, {
        "name": "tiling",
        "label": i18n("Tiling"),
        "iconName": "window-duplicate",
        "hasChildren": true,
        "hasDividerAfter": false
    }, {
        "name": "apprules",
        "label": i18n("App Rules"),
        "iconName": "application-x-executable",
        "hasChildren": false,
        "hasDividerAfter": true
    }, {
        "name": "exclusions",
        "label": i18n("Exclusions"),
        "iconName": "dialog-cancel",
        "hasChildren": false,
        "hasDividerAfter": false
    }, {
        "name": "editor",
        "label": i18n("Editor"),
        "iconName": "document-edit",
        "hasChildren": false,
        "hasDividerAfter": false
    }, {
        "name": "general",
        "label": i18n("General"),
        "iconName": "configure",
        "hasChildren": false,
        "hasDividerAfter": false
    }, {
        "name": "about",
        "label": i18n("About"),
        "iconName": "help-about",
        "hasChildren": false,
        "hasDividerAfter": false
    }]
    // Children for each parent
    readonly property var _childItems: ({
        "snapping": [{
            "name": "snap-appearance",
            "label": i18n("Appearance"),
            "iconName": "preferences-desktop-color"
        }, {
            "name": "snap-behavior",
            "label": i18n("Behavior"),
            "iconName": "preferences-system"
        }, {
            "name": "snap-zoneselector",
            "label": i18n("Zone Selector"),
            "iconName": "view-choose"
        }, {
            "name": "snap-effects",
            "label": i18n("Effects"),
            "iconName": "preferences-desktop-effects"
        }, {
            "name": "snap-assignments",
            "label": i18n("Assignments"),
            "iconName": "view-list-details"
        }, {
            "name": "snap-shortcuts",
            "label": i18n("Quick Shortcuts"),
            "iconName": "bookmark"
        }],
        "tiling": [{
            "name": "tile-appearance",
            "label": i18n("Appearance"),
            "iconName": "preferences-desktop-color"
        }, {
            "name": "tile-behavior",
            "label": i18n("Behavior"),
            "iconName": "preferences-system"
        }, {
            "name": "tile-algorithm",
            "label": i18n("Algorithms"),
            "iconName": "view-grid"
        }, {
            "name": "tile-assignments",
            "label": i18n("Assignments"),
            "iconName": "view-list-details"
        }, {
            "name": "tile-shortcuts",
            "label": i18n("Quick Shortcuts"),
            "iconName": "bookmark"
        }]
    })
    // Page component map -- loaded on demand by Loader
    readonly property var _pageComponents: ({
        "overview": "MonitorStatePage.qml",
        "layouts": "LayoutsPage.qml",
        "snap-appearance": "SnappingAppearancePage.qml",
        "snap-behavior": "SnappingBehaviorPage.qml",
        "snap-zoneselector": "SnappingZoneSelectorPage.qml",
        "snap-effects": "SnappingEffectsPage.qml",
        "tile-appearance": "TilingAppearancePage.qml",
        "tile-behavior": "TilingBehaviorPage.qml",
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
    // Shared aspect ratio labels (used in context menu + LayoutsPage section headers)
    readonly property var aspectRatioLabels: ({
        "any": i18n("All Monitors"),
        "standard": i18n("Standard (16:9)"),
        "ultrawide": i18n("Ultrawide (21:9)"),
        "super-ultrawide": i18n("Super-Ultrawide (32:9)"),
        "portrait": i18n("Portrait (9:16)")
    })

    function _rebuildSidebar() {
        sidebarModel.clear();
        let searchText = sidebarSearch.text.toLowerCase();
        if (_sidebarMode === "main") {
            for (let i = 0; i < _mainItems.length; i++) {
                let item = _mainItems[i];
                if (searchText) {
                    // Search both item label and children labels
                    let itemMatches = item.label.toLowerCase().indexOf(searchText) >= 0;
                    let matchingChildren = [];
                    if (item.hasChildren) {
                        let children = _childItems[item.name] || [];
                        for (let j = 0; j < children.length; j++) {
                            if (children[j].label.toLowerCase().indexOf(searchText) >= 0)
                                matchingChildren.push(children[j]);

                        }
                    }
                    if (!itemMatches && matchingChildren.length === 0)
                        continue;

                    // Show parent (disable drill-in if showing matched children)
                    sidebarModel.append({
                        "name": item.name,
                        "label": item.label,
                        "iconName": item.iconName,
                        "hasChildren": matchingChildren.length === 0 && item.hasChildren,
                        "isBackButton": false,
                        "hasDividerAfter": false,
                        "isDivider": false
                    });
                    // Show matching children inline
                    for (let j = 0; j < matchingChildren.length; j++) {
                        sidebarModel.append({
                            "name": matchingChildren[j].name,
                            "label": "  " + matchingChildren[j].label,
                            "iconName": matchingChildren[j].iconName,
                            "hasChildren": false,
                            "isBackButton": false,
                            "hasDividerAfter": false,
                            "isDivider": false
                        });
                    }
                    continue;
                }
                sidebarModel.append({
                    "name": item.name,
                    "label": item.label,
                    "iconName": item.iconName,
                    "hasChildren": item.hasChildren,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": false
                });
                if (item.hasDividerAfter)
                    sidebarModel.append({
                    "name": "__divider__",
                    "label": "",
                    "iconName": "",
                    "hasChildren": false,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": true
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
            // Back button row (always visible)
            sidebarModel.append({
                "name": "__back__",
                "label": parentLabel,
                "iconName": "arrow-left",
                "hasChildren": false,
                "isBackButton": true,
                "hasDividerAfter": false,
                "isDivider": false
            });
            // Child items
            let children = _childItems[_sidebarMode] || [];
            for (let i = 0; i < children.length; i++) {
                if (searchText && children[i].label.toLowerCase().indexOf(searchText) < 0)
                    continue;

                sidebarModel.append({
                    "name": children[i].name,
                    "label": children[i].label,
                    "iconName": children[i].iconName,
                    "hasChildren": false,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": false
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
        let children = _childItems[parentName];
        let firstChild = (children && children.length > 0) ? children[0].name : "";
        sidebarTransition.pendingMode = parentName;
        sidebarTransition.pendingPage = firstChild;
        sidebarTransition.restart();
    }

    // Show a toast notification from any child page
    function showToast(msg) {
        toast.show(msg);
    }

    // Show the layout context menu (called from LayoutsPage.qml via window.showLayoutContextMenu)
    function showLayoutContextMenu(layout) {
        layoutContextMenu.showForLayout(layout);
    }

    // Return to the main sidebar list, selecting the parent category
    function _drillOut() {
        sidebarTransition.pendingMode = "main";
        sidebarTransition.pendingPage = _sidebarMode !== "main" ? _sidebarMode : "overview";
        sidebarTransition.restart();
    }

    title: i18n("PlasmaZones Settings")
    width: Kirigami.Units.gridUnit * 80
    height: Kirigami.Units.gridUnit * 48
    visible: true
    onClosing: function(close) {
        settingsController.saveWindowGeometry(window.x, window.y, window.width, window.height);
        if (settingsController.needsSave && !window._closeConfirmed) {
            close.accepted = false;
            unsavedChangesDialog.open();
        }
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

    // Sidebar drill-in/out transition animation
    SequentialAnimation {
        id: sidebarTransition

        property string pendingMode: ""
        property string pendingPage: ""

        NumberAnimation {
            target: sidebar
            property: "opacity"
            to: 0
            duration: 80
            easing.type: Easing.InQuad
        }

        ScriptAction {
            script: {
                window._sidebarMode = sidebarTransition.pendingMode;
                window._rebuildSidebar();
                if (sidebarTransition.pendingPage)
                    settingsController.activePage = sidebarTransition.pendingPage;

            }
        }

        NumberAnimation {
            target: sidebar
            property: "opacity"
            to: 1
            duration: 120
            easing.type: Easing.OutQuad
        }

    }

    Shortcut {
        sequence: "Ctrl+PgUp"
        onActivated: {
            let idx = sidebar.currentIndex;
            for (let i = idx - 1; i >= 0; i--) {
                let item = sidebarModel.get(i);
                if (!item.isBackButton && !item.hasChildren && item.name !== "__divider__") {
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
                if (!item.isBackButton && !item.hasChildren && item.name !== "__divider__") {
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
        sequence: "?"
        enabled: {
            var item = window.activeFocusItem;
            if (!item)
                return true;

            return !(item instanceof TextInput) && !(item instanceof TextEdit);
        }
        onActivated: window._showShortcuts = !window._showShortcuts
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

                // Search field
                TextField {
                    id: sidebarSearch

                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    placeholderText: i18n("Search...")
                    visible: !window.sidebarCompact
                    leftPadding: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2
                    onTextChanged: _rebuildSidebar()

                    Kirigami.Icon {
                        source: "search"
                        anchors.left: parent.left
                        anchors.leftMargin: Kirigami.Units.smallSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        opacity: 0.5
                    }

                }

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
                        required property bool hasDividerAfter
                        required property bool isDivider
                        readonly property bool isActive: {
                            if (isBackButton)
                                return false;

                            if (hasChildren)
                                return false;

                            return name === settingsController.activePage;
                        }

                        width: sidebar.width
                        height: isDivider ? Kirigami.Units.largeSpacing : (isBackButton ? Kirigami.Units.gridUnit * 2.6 : Kirigami.Units.gridUnit * 2.2)
                        enabled: !isDivider
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
                            // If selecting an inline search result child, clear search and drill into parent
                            if (sidebarSearch.text.length > 0 && _sidebarMode === "main") {
                                let parents = Object.keys(_childItems);
                                for (let p = 0; p < parents.length; p++) {
                                    let children = _childItems[parents[p]];
                                    for (let c = 0; c < children.length; c++) {
                                        if (children[c].name === name) {
                                            sidebarSearch.text = "";
                                            _sidebarMode = parents[p];
                                            _rebuildSidebar();
                                            settingsController.activePage = name;
                                            return ;
                                        }
                                    }
                                }
                            }
                            settingsController.activePage = name;
                        }
                        leftPadding: window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing
                        rightPadding: window.sidebarCompact ? 0 : Kirigami.Units.smallSpacing
                        ToolTip.visible: window.sidebarCompact && navDelegate.hovered
                        ToolTip.text: label
                        ToolTip.delay: 300

                        // Section divider rendering (when this delegate is a divider item)
                        Kirigami.Separator {
                            visible: navDelegate.isDivider
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Kirigami.Units.largeSpacing
                            anchors.rightMargin: Kirigami.Units.largeSpacing
                        }

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
                    padding: Kirigami.Units.smallSpacing * 1.5
                    topPadding: Kirigami.Units.smallSpacing * 2
                    bottomPadding: Kirigami.Units.smallSpacing * 2

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
                            Layout.alignment: Qt.AlignVCenter
                            visible: !window.sidebarCompact
                        }

                        SettingsSwitch {
                            Layout.alignment: Qt.AlignVCenter
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

                    Row {
                        spacing: Kirigami.Units.smallSpacing

                        // Parent name (clickable when drilled in)
                        Label {
                            visible: window._sidebarMode !== "main"
                            text: window._parentLabel()
                            opacity: parentMouse.containsMouse ? 0.8 : 0.5
                            font.underline: parentMouse.containsMouse

                            MouseArea {
                                id: parentMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: window._drillOut()
                            }

                        }

                        // Separator
                        Label {
                            visible: window._sidebarMode !== "main"
                            text: "\u203A"
                            opacity: 0.5
                        }

                        // Current page name
                        Label {
                            text: {
                                if (window._sidebarMode !== "main")
                                    return window._subPageLabel(settingsController.activePage);

                                return window._mainItemLabel(settingsController.activePage);
                            }
                            opacity: 0.5
                        }

                    }

                    // Unsaved changes indicator
                    Label {
                        visible: settingsController.needsSave
                        text: "●"
                        color: Kirigami.Theme.highlightColor
                        font: Kirigami.Theme.smallFont

                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            running: settingsController.needsSave

                            NumberAnimation {
                                from: 1
                                to: 0.4
                                duration: 1000
                                easing.type: Easing.InOutSine
                            }

                            NumberAnimation {
                                from: 0.4
                                to: 1
                                duration: 1000
                                easing.type: Easing.InOutSine
                            }

                        }

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

            // ── Update banner (visible on all pages) ─────────────────────
            Pane {
                id: updateBanner

                Layout.fillWidth: true
                visible: settingsController.updateChecker.updateAvailable && settingsController.updateChecker.latestVersion !== settingsController.dismissedUpdateVersion
                padding: Kirigami.Units.smallSpacing
                topPadding: Kirigami.Units.smallSpacing
                bottomPadding: Kirigami.Units.smallSpacing

                RowLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "update-none"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.positiveTextColor
                    }

                    Label {
                        text: i18n("PlasmaZones %1 is available", settingsController.updateChecker.latestVersion)
                        Layout.fillWidth: true
                        color: Kirigami.Theme.positiveTextColor
                    }

                    Button {
                        text: i18n("View Release")
                        flat: true
                        icon.name: "internet-web-browser"
                        visible: settingsController.updateChecker.releaseUrl.length > 0
                        onClicked: Qt.openUrlExternally(settingsController.updateChecker.releaseUrl)
                    }

                    ToolButton {
                        icon.name: "dialog-close"
                        display: ToolButton.IconOnly
                        onClicked: settingsController.dismissUpdate()
                        ToolTip.text: i18n("Dismiss")
                        ToolTip.visible: hovered
                    }

                }

                background: Rectangle {
                    color: Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.15)
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.3)
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

            // ── Layout context menu (lives outside Loader to avoid Qt6 SIGSEGV on Menu destruction) ──
            Menu {
                id: layoutContextMenu

                property var layout: null
                property int viewMode: 0
                property var _screenItems: []
                readonly property bool isAutotile: layout && layout.isAutotile === true
                readonly property string layoutId: layout ? (layout.id || "") : ""

                // Signals for dialogs that live in LayoutsPage
                signal deleteRequested(var layout)
                signal exportRequested(string layoutId)

                function showForLayout(layout) {
                    layoutContextMenu.layout = layout;
                    layoutContextMenu.viewMode = (layout && layout.isAutotile === true) ? 1 : 0;
                    // Rebuild dynamic "Edit on <screen>" items
                    for (let j = 0; j < _screenItems.length; j++) {
                        layoutContextMenu.removeItem(_screenItems[j]);
                        _screenItems[j].destroy();
                    }
                    _screenItems = [];
                    let screens = settingsController.screens || [];
                    if (screens.length > 1) {
                        for (let i = 0; i < screens.length; i++) {
                            let s = screens[i];
                            let parts = [s.manufacturer || "", s.model || ""].filter((p) => {
                                return p !== "";
                            });
                            let label = parts.length > 0 ? parts.join(" ") : (s.name || "");
                            if (s.resolution)
                                label += " (" + s.resolution + ")";

                            let item = screenMenuItemComponent.createObject(layoutContextMenu, {
                                "text": i18n("Edit on %1", label),
                                "icon.name": s.isPrimary ? "starred-symbolic" : "monitor"
                            });
                            item._screenName = s.name;
                            // Insert after the Edit item (index 1+)
                            layoutContextMenu.insertItem(1 + i, item);
                            _screenItems.push(item);
                        }
                    }
                    screenSeparator.visible = _screenItems.length > 0;
                    layoutContextMenu.popup();
                }

                // -- Edit --
                MenuItem {
                    text: i18n("Edit")
                    icon.name: "document-edit"
                    onTriggered: settingsController.editLayout(layoutContextMenu.layoutId)
                }

                // -- Dynamic "Edit on <screen>" items inserted here by showForLayout --
                MenuSeparator {
                    id: screenSeparator

                    visible: false
                }

                // -- State --
                MenuItem {
                    text: i18n("Set as Default")
                    icon.name: "favorite"
                    enabled: {
                        if (!layoutContextMenu.layout)
                            return false;

                        if (layoutContextMenu.viewMode === 1)
                            return layoutContextMenu.layoutId !== ("autotile:" + appSettings.autotileAlgorithm);

                        return layoutContextMenu.layoutId !== appSettings.defaultLayoutId;
                    }
                    onTriggered: {
                        if (layoutContextMenu.viewMode === 1)
                            appSettings.autotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
                        else
                            appSettings.defaultLayoutId = layoutContextMenu.layoutId;
                    }
                }

                MenuItem {
                    text: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? i18n("Show in Zone Selector") : i18n("Hide from Zone Selector")
                    icon.name: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? "view-visible" : "view-hidden"
                    onTriggered: settingsController.setLayoutHidden(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector))
                }

                MenuItem {
                    text: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true ? i18n("Disable Auto-assign") : i18n("Enable Auto-assign")
                    icon.name: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true ? "window-duplicate" : "window-new"
                    visible: !layoutContextMenu.isAutotile
                    onTriggered: settingsController.setLayoutAutoAssign(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true))
                }

                // -- Aspect Ratio (flat, no nested Menu — Qt6 crashes with submenu) --
                MenuSeparator {
                    visible: !layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: "  " + window.aspectRatioLabels["any"]
                    checkable: true
                    visible: !layoutContextMenu.isAutotile
                    checked: layoutContextMenu.layout && (layoutContextMenu.layout.aspectRatioClass || "any") === "any"
                    onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 0)
                }

                MenuItem {
                    text: "  " + window.aspectRatioLabels["standard"]
                    checkable: true
                    visible: !layoutContextMenu.isAutotile
                    checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "standard"
                    onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 1)
                }

                MenuItem {
                    text: "  " + window.aspectRatioLabels["ultrawide"]
                    checkable: true
                    visible: !layoutContextMenu.isAutotile
                    checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "ultrawide"
                    onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 2)
                }

                MenuItem {
                    text: "  " + window.aspectRatioLabels["super-ultrawide"]
                    checkable: true
                    visible: !layoutContextMenu.isAutotile
                    checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "super-ultrawide"
                    onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 3)
                }

                MenuItem {
                    text: "  " + window.aspectRatioLabels["portrait"]
                    checkable: true
                    visible: !layoutContextMenu.isAutotile
                    checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "portrait"
                    onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 4)
                }

                // -- Manage (snapping layouts) --
                MenuSeparator {
                    visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
                    onTriggered: settingsController.duplicateLayout(layoutContextMenu.layoutId)
                }

                MenuItem {
                    text: i18n("Export")
                    icon.name: "document-export"
                    visible: layoutContextMenu.viewMode === 0
                    onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId)
                }

                MenuSeparator {
                    visible: layoutContextMenu.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Delete")
                    icon.name: "edit-delete"
                    visible: layoutContextMenu.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
                    onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
                }

                // -- Manage (algorithms) --
                MenuSeparator {
                    visible: layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    visible: layoutContextMenu.isAutotile
                    onTriggered: {
                        let algoId = layoutContextMenu.layoutId.substring("autotile:".length);
                        settingsController.duplicateAlgorithm(algoId);
                    }
                }

                MenuItem {
                    text: i18n("Export")
                    icon.name: "document-export"
                    visible: layoutContextMenu.isAutotile
                    onTriggered: layoutContextMenu.exportRequested(layoutContextMenu.layoutId)
                }

                MenuSeparator {
                    visible: layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem
                }

                MenuItem {
                    text: i18n("Delete")
                    icon.name: "edit-delete"
                    visible: layoutContextMenu.isAutotile && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem
                    onTriggered: layoutContextMenu.deleteRequested(layoutContextMenu.layout)
                }

            }

            // Component for dynamic "Edit on <screen>" menu items
            Component {
                id: screenMenuItemComponent

                MenuItem {
                    property string _screenName: ""

                    onTriggered: settingsController.editLayoutOnScreen(layoutContextMenu.layoutId, _screenName)
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
                        onClicked: resetConfirmDialog.open()
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
                        onClicked: defaultsConfirmDialog.open()
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

    // ── Unsaved changes confirmation dialog ────────────────────────
    Kirigami.PromptDialog {
        id: unsavedChangesDialog

        title: i18n("Unsaved Changes")
        subtitle: i18n("You have unsaved changes. Do you want to apply them before closing?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Apply")
                icon.name: "dialog-ok-apply"
                onTriggered: {
                    unsavedChangesDialog.close();
                    settingsController.save();
                    window._closeConfirmed = true;
                    window.close();
                }
            },
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-delete"
                onTriggered: {
                    unsavedChangesDialog.close();
                    window._closeConfirmed = true;
                    window.close();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: unsavedChangesDialog.close()
            }
        ]
    }

    // ── Reset confirmation dialog ───────────────────────────────────
    Kirigami.PromptDialog {
        id: resetConfirmDialog

        title: i18n("Discard Changes")
        subtitle: i18n("Are you sure you want to discard all unsaved changes?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-undo"
                onTriggered: {
                    resetConfirmDialog.close();
                    settingsController.load();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: resetConfirmDialog.close()
            }
        ]
    }

    // ── Defaults confirmation dialog ────────────────────────────────
    Kirigami.PromptDialog {
        id: defaultsConfirmDialog

        title: i18n("Restore Defaults")
        subtitle: i18n("Are you sure you want to reset all settings to their default values?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Restore Defaults")
                icon.name: "document-revert"
                onTriggered: {
                    defaultsConfirmDialog.close();
                    settingsController.defaults();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: defaultsConfirmDialog.close()
            }
        ]
    }

    // ── Keyboard shortcut overlay ──────────────────────────────────
    Rectangle {
        id: shortcutOverlay

        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        visible: opacity > 0
        opacity: window._showShortcuts ? 1 : 0
        z: 200
        Keys.onEscapePressed: window._showShortcuts = false
        focus: window._showShortcuts

        MouseArea {
            anchors.fill: parent
            onClicked: window._showShortcuts = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.6, Kirigami.Units.gridUnit * 30)
            height: shortcutContent.implicitHeight + Kirigami.Units.largeSpacing * 3
            radius: Kirigami.Units.smallSpacing * 2
            color: Kirigami.Theme.backgroundColor
            border.width: Math.round(Kirigami.Units.devicePixelRatio)
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

            ColumnLayout {
                id: shortcutContent

                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing * 1.5
                spacing: Kirigami.Units.largeSpacing

                Label {
                    text: i18n("Keyboard Shortcuts")
                    font.weight: Font.DemiBold
                    font.pixelSize: Kirigami.Units.gridUnit * 1.2
                    Layout.alignment: Qt.AlignHCenter
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // Shortcut entries
                Repeater {
                    model: [{
                        "key": "Meta+Shift+P",
                        "action": i18n("Open PlasmaZones Settings")
                    }, {
                        "key": "Meta+Shift+E",
                        "action": i18n("Open Zone Editor")
                    }, {
                        "key": "Ctrl+PgUp",
                        "action": i18n("Previous page")
                    }, {
                        "key": "Ctrl+PgDown",
                        "action": i18n("Next page")
                    }, {
                        "key": "?",
                        "action": i18n("Toggle this overlay")
                    }]

                    delegate: RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: modelData.action
                            Layout.fillWidth: true
                            opacity: 0.7
                        }

                        Rectangle {
                            implicitWidth: keyLabel.implicitWidth + Kirigami.Units.largeSpacing
                            implicitHeight: keyLabel.implicitHeight + Kirigami.Units.smallSpacing
                            radius: Kirigami.Units.smallSpacing / 2
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                            border.width: Math.round(Kirigami.Units.devicePixelRatio)
                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

                            Label {
                                id: keyLabel

                                anchors.centerIn: parent
                                text: modelData.key
                                font: Kirigami.Theme.smallFont
                            }

                        }

                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Label {
                    text: i18n("Press ? or Escape to close")
                    opacity: 0.4
                    Layout.alignment: Qt.AlignHCenter
                    font: Kirigami.Theme.smallFont
                }

            }

        }

        Behavior on opacity {
            NumberAnimation {
                duration: 200
            }

        }

    }

}
