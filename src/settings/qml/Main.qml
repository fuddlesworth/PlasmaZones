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
        "name": "virtualscreens",
        "label": i18n("Virtual Screens"),
        "iconName": "virtual-desktops",
        "hasChildren": false,
        "hasDividerAfter": false
    }, {
        "name": "layouts",
        "label": i18n("Layouts"),
        "iconName": "view-grid",
        "hasChildren": false,
        "hasDividerAfter": true
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
        "hasDividerAfter": true
    }, {
        "name": "animations",
        "label": i18n("Animations"),
        "iconName": "media-playback-start",
        "hasChildren": true,
        "hasDividerAfter": true
    }, {
        "name": "exclusions",
        "label": i18n("Exclusions"),
        "iconName": "dialog-cancel",
        "hasChildren": false,
        "hasDividerAfter": true
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
            "name": "snapping-appearance",
            "label": i18n("Appearance"),
            "iconName": "preferences-desktop-color"
        }, {
            "name": "snapping-effects",
            "label": i18n("Effects"),
            "iconName": "preferences-desktop-effects",
            "hasDividerAfter": true
        }, {
            "name": "snapping-behavior",
            "label": i18n("Behavior"),
            "iconName": "preferences-system"
        }, {
            "name": "snapping-zoneselector",
            "label": i18n("Zone Selector"),
            "iconName": "view-choose",
            "hasDividerAfter": true
        }, {
            "name": "snapping-assignments",
            "label": i18n("Assignments"),
            "iconName": "view-list-details"
        }, {
            "name": "snapping-apprules",
            "label": i18n("App Rules"),
            "iconName": "application-x-executable"
        }, {
            "name": "snapping-ordering",
            "label": i18n("Priority"),
            "iconName": "view-sort"
        }, {
            "name": "snapping-shortcuts",
            "label": i18n("Quick Shortcuts"),
            "iconName": "bookmark"
        }],
        "animations": [{
            "name": "animations-general",
            "label": i18n("General"),
            "iconName": "configure"
        }, {
            "name": "animations-presets",
            "label": i18n("Presets"),
            "iconName": "bookmark",
            "hasDividerAfter": true
        }, {
            "name": "animations-snap",
            "label": i18n("Snap"),
            "iconName": "view-split-left-right"
        }, {
            "name": "animations-layout",
            "label": i18n("Layout Switch"),
            "iconName": "view-grid"
        }, {
            "name": "animations-border",
            "label": i18n("Autotile Border"),
            "iconName": "draw-rectangle",
            "hasDividerAfter": true
        }, {
            "name": "animations-zonehighlight",
            "label": i18n("Zone Highlight"),
            "iconName": "gradient"
        }, {
            "name": "animations-osd",
            "label": i18n("OSD"),
            "iconName": "dialog-information"
        }, {
            "name": "animations-popup",
            "label": i18n("Popups"),
            "iconName": "window-new"
        }, {
            "name": "animations-zoneselector",
            "label": i18n("Zone Selector"),
            "iconName": "view-list-icons"
        }, {
            "name": "animations-preview",
            "label": i18n("Preview"),
            "iconName": "view-preview"
        }, {
            "name": "animations-dim",
            "label": i18n("Dim"),
            "iconName": "contrast",
            "hasDividerAfter": true
        }, {
            "name": "animations-shaders",
            "label": i18n("Shaders"),
            "iconName": "preferences-desktop-effects"
        }],
        "tiling": [{
            "name": "tiling-appearance",
            "label": i18n("Appearance"),
            "iconName": "preferences-desktop-color",
            "hasDividerAfter": true
        }, {
            "name": "tiling-behavior",
            "label": i18n("Behavior"),
            "iconName": "preferences-system"
        }, {
            "name": "tiling-algorithm",
            "label": i18n("Algorithms"),
            "iconName": "view-grid",
            "hasDividerAfter": true
        }, {
            "name": "tiling-assignments",
            "label": i18n("Assignments"),
            "iconName": "view-list-details"
        }, {
            "name": "tiling-ordering",
            "label": i18n("Priority"),
            "iconName": "view-sort"
        }, {
            "name": "tiling-shortcuts",
            "label": i18n("Quick Shortcuts"),
            "iconName": "bookmark"
        }]
    })
    // Page component map -- loaded on demand by Loader
    readonly property var _pageComponents: ({
        "overview": "MonitorStatePage.qml",
        "virtualscreens": "VirtualScreensPage.qml",
        "layouts": "LayoutsPage.qml",
        "snapping-appearance": "SnappingAppearancePage.qml",
        "snapping-behavior": "SnappingBehaviorPage.qml",
        "snapping-zoneselector": "SnappingZoneSelectorPage.qml",
        "snapping-effects": "SnappingEffectsPage.qml",
        "tiling-appearance": "TilingAppearancePage.qml",
        "tiling-behavior": "TilingBehaviorPage.qml",
        "tiling-algorithm": "TilingAlgorithmPage.qml",
        "snapping-assignments": "SnappingAssignmentsPage.qml",
        "snapping-apprules": "AssignmentsAppRulesPage.qml",
        "snapping-shortcuts": "SnappingQuickShortcutsPage.qml",
        "snapping-ordering": "SnappingOrderingPage.qml",
        "tiling-assignments": "TilingAssignmentsPage.qml",
        "tiling-shortcuts": "TilingQuickShortcutsPage.qml",
        "tiling-ordering": "TilingOrderingPage.qml",
        "animations-general": "AnimationsGeneralPage.qml",
        "animations-snap": "AnimationsSnapPage.qml",
        "animations-layout": "AnimationsLayoutPage.qml",
        "animations-border": "AnimationsBorderPage.qml",
        "animations-zonehighlight": "AnimationsZoneHighlightPage.qml",
        "animations-osd": "AnimationsOsdPage.qml",
        "animations-popup": "AnimationsPopupPage.qml",
        "animations-zoneselector": "AnimationsZoneSelectorPage.qml",
        "animations-preview": "AnimationsPreviewPage.qml",
        "animations-dim": "AnimationsDimPage.qml",
        "animations-presets": "AnimationsPresetsPage.qml",
        "animations-shaders": "AnimationsShadersPage.qml",
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
                        let childDivider = matchingChildren[j].hasDividerAfter || false;
                        sidebarModel.append({
                            "name": matchingChildren[j].name,
                            "label": "  " + matchingChildren[j].label,
                            "iconName": matchingChildren[j].iconName,
                            "hasChildren": false,
                            "isBackButton": false,
                            "hasDividerAfter": false,
                            "isDivider": false
                        });
                        if (childDivider)
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

                let childDivider = children[i].hasDividerAfter || false;
                sidebarModel.append({
                    "name": children[i].name,
                    "label": children[i].label,
                    "iconName": children[i].iconName,
                    "hasChildren": false,
                    "isBackButton": false,
                    "hasDividerAfter": false,
                    "isDivider": false
                });
                if (childDivider)
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

    function showWhatsNew() {
        whatsNewDialog.open();
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

                            // Unsaved changes badge
                            Rectangle {
                                width: Kirigami.Units.smallSpacing * 1.5
                                height: Kirigami.Units.smallSpacing * 1.5
                                radius: width / 2
                                color: Kirigami.Theme.neutralTextColor
                                visible: navDelegate.isActive && settingsController.needsSave && !navDelegate.isDivider && !navDelegate.isBackButton
                                Layout.alignment: Qt.AlignVCenter

                                SequentialAnimation {
                                    id: dirtyBadgePulse

                                    property Item target: parent

                                    loops: Animation.Infinite
                                    running: navDelegate.isActive && settingsController.needsSave
                                    onRunningChanged: {
                                        if (!running)
                                            target.opacity = 1;

                                    }

                                    NumberAnimation {
                                        target: dirtyBadgePulse.target
                                        property: "opacity"
                                        from: 1
                                        to: 0.4
                                        duration: 1000
                                        easing.type: Easing.InOutSine
                                    }

                                    NumberAnimation {
                                        target: dirtyBadgePulse.target
                                        property: "opacity"
                                        from: 0.4
                                        to: 1
                                        duration: 1000
                                        easing.type: Easing.InOutSine
                                    }

                                }

                            }

                            // Enable/disable toggle for snapping and tiling
                            SettingsSwitch {
                                visible: (navDelegate.name === "snapping" || navDelegate.name === "tiling") && !window.sidebarCompact
                                checked: navDelegate.name === "snapping" ? appSettings.snappingEnabled : appSettings.autotileEnabled
                                accessibleName: navDelegate.label
                                onToggled: function(newValue) {
                                    if (navDelegate.name === "snapping")
                                        appSettings.snappingEnabled = newValue;
                                    else
                                        appSettings.autotileEnabled = newValue;
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
                            id: daemonDot

                            width: Kirigami.Units.smallSpacing * 1.5
                            height: Kirigami.Units.smallSpacing * 1.5
                            radius: width / 2
                            Layout.alignment: Qt.AlignVCenter
                            color: settingsController.daemonRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor

                            // Pulsing glow when running
                            SequentialAnimation {
                                id: daemonPulse

                                loops: settingsController.daemonRunning ? Animation.Infinite : 0
                                running: settingsController.daemonRunning
                                onRunningChanged: {
                                    if (!running)
                                        daemonDot.opacity = 1;

                                }

                                NumberAnimation {
                                    target: daemonDot
                                    property: "opacity"
                                    from: 1
                                    to: 0.4
                                    duration: 1500
                                    easing.type: Easing.InOutSine
                                }

                                NumberAnimation {
                                    target: daemonDot
                                    property: "opacity"
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
                            onToggled: function(newValue) {
                                settingsController.daemonController.setEnabled(newValue);
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

            // ── What's New banner (visible when unseen changes exist) ──
            Pane {
                id: whatsNewBanner

                Layout.fillWidth: true
                visible: settingsController.hasUnseenWhatsNew
                padding: Kirigami.Units.smallSpacing
                topPadding: Kirigami.Units.smallSpacing
                bottomPadding: Kirigami.Units.smallSpacing

                RowLayout {
                    anchors.fill: parent
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "documentinfo"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.linkColor
                    }

                    Label {
                        text: i18n("See what's new in PlasmaZones %1", Qt.application.version)
                        Layout.fillWidth: true
                        color: Kirigami.Theme.linkColor
                    }

                    Button {
                        text: i18n("What's New")
                        flat: true
                        icon.name: "go-next"
                        onClicked: whatsNewDialog.open()
                    }

                    ToolButton {
                        icon.name: "dialog-close"
                        display: ToolButton.IconOnly
                        onClicked: settingsController.markWhatsNewSeen()
                        ToolTip.text: i18n("Dismiss")
                        ToolTip.visible: hovered
                    }

                }

                background: Rectangle {
                    color: Qt.rgba(Kirigami.Theme.linkColor.r, Kirigami.Theme.linkColor.g, Kirigami.Theme.linkColor.b, 0.15)
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: Qt.rgba(Kirigami.Theme.linkColor.r, Kirigami.Theme.linkColor.g, Kirigami.Theme.linkColor.b, 0.3)
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
                property bool _aspectRatioMenuInserted: false
                readonly property bool isAutotile: layout && layout.isAutotile === true
                readonly property string layoutId: layout ? (layout.id || "") : ""
                // Aspect ratio options: [key, label, settingsIndex]
                readonly property var _aspectRatioOptions: [["any", window.aspectRatioLabels["any"], 0], ["standard", window.aspectRatioLabels["standard"], 1], ["ultrawide", window.aspectRatioLabels["ultrawide"], 2], ["super-ultrawide", window.aspectRatioLabels["super-ultrawide"], 3], ["portrait", window.aspectRatioLabels["portrait"], 4]]

                // Signals for dialogs that live in LayoutsPage
                signal deleteRequested(var layout)
                signal exportRequested(string layoutId)

                function showForLayout(layout) {
                    layoutContextMenu.layout = layout;
                    layoutContextMenu.viewMode = (layout && layout.isAutotile === true) ? 1 : 0;
                    // Add/remove aspect ratio submenu (Qt6 ignores visible on inline Menu submenus,
                    // so we imperatively insert/remove it like screen items)
                    if (_aspectRatioMenuInserted) {
                        layoutContextMenu.removeMenu(aspectRatioSubMenu);
                        _aspectRatioMenuInserted = false;
                    }
                    if (!layoutContextMenu.isAutotile) {
                        aspectRatioSubMenu.updateChecks();
                        // Insert after the aspectRatioMarker separator
                        let markerIdx = -1;
                        for (let k = 0; k < layoutContextMenu.count; k++) {
                            if (layoutContextMenu.itemAt(k) === aspectRatioMarker) {
                                markerIdx = k;
                                break;
                            }
                        }
                        if (markerIdx >= 0)
                            layoutContextMenu.insertMenu(markerIdx + 1, aspectRatioSubMenu);
                        else
                            layoutContextMenu.addMenu(aspectRatioSubMenu);
                        _aspectRatioMenuInserted = true;
                    }
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
                            let item = screenMenuItemComponent.createObject(layoutContextMenu, {
                                "text": i18n("Edit on %1", s.displayLabel || s.name || ""),
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

                // -- Open in Editor (external text editor) --
                MenuItem {
                    text: i18n("Open in Text Editor")
                    icon.name: "document-open"
                    Accessible.name: text
                    onTriggered: {
                        if (layoutContextMenu.isAutotile)
                            settingsController.openAlgorithm(settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId));
                        else
                            settingsController.openLayoutFile(layoutContextMenu.layoutId);
                    }
                }

                MenuSeparator {
                }

                // -- State --
                MenuItem {
                    text: i18n("Set as Default")
                    icon.name: "favorite"
                    enabled: {
                        if (!layoutContextMenu.layout)
                            return false;

                        if (layoutContextMenu.viewMode === 1)
                            return layoutContextMenu.layoutId !== ("autotile:" + appSettings.defaultAutotileAlgorithm);

                        return layoutContextMenu.layoutId !== appSettings.defaultLayoutId;
                    }
                    onTriggered: {
                        if (layoutContextMenu.viewMode === 1)
                            appSettings.defaultAutotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
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

                // -- Aspect Ratio insertion point (submenu managed imperatively in showForLayout) --
                MenuSeparator {
                    id: aspectRatioMarker

                    visible: !layoutContextMenu.isAutotile
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
                    visible: layoutContextMenu.viewMode === 0 && !layoutContextMenu.isAutotile
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

                // -- Algorithms: Manage --
                MenuSeparator {
                    // Only show if at least one item below is visible (Duplicate/Export always
                    // are, so this fires for any autotile entry — but keeps the separator hidden
                    // when !isAutotile, avoiding a dangling line for snapping layouts).
                    visible: layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    visible: layoutContextMenu.isAutotile
                    onTriggered: settingsController.duplicateAlgorithm(settingsController.algorithmIdFromLayoutId(layoutContextMenu.layoutId))
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

            // Component for aspect ratio submenu items (ItemDelegate, not MenuItem —
            // avoids Qt6 finalizeExitTransition crash; same pattern as editor shader menu)
            Component {
                id: aspectRatioItemComponent

                ItemDelegate {
                    property string _arKey: ""
                    property int _arIndex: 0
                    property bool isSelected: false

                    icon.name: isSelected ? "checkmark" : ""
                    Accessible.name: text
                    onClicked: {
                        let layoutId = layoutContextMenu.layoutId;
                        let idx = _arIndex;
                        Qt.callLater(function() {
                            aspectRatioSubMenu.visible = false;
                            layoutContextMenu.visible = false;
                            settingsController.setLayoutAspectRatio(layoutId, idx);
                        });
                    }
                }

            }

            // Aspect ratio submenu (managed imperatively by showForLayout —
            // Qt6 ignores visible on inline Menu submenus, so we add/remove it)
            Menu {
                id: aspectRatioSubMenu

                property var _items: []
                property bool _built: false

                function buildOnce() {
                    if (_built)
                        return ;

                    _built = true;
                    var options = layoutContextMenu._aspectRatioOptions;
                    for (var i = 0; i < options.length; i++) {
                        var item = aspectRatioItemComponent.createObject(aspectRatioSubMenu, {
                            "text": options[i][1],
                            "_arKey": options[i][0],
                            "_arIndex": options[i][2]
                        });
                        aspectRatioSubMenu.addItem(item);
                        _items.push(item);
                    }
                }

                function updateChecks() {
                    buildOnce();
                    var currentClass = (layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass) || "any";
                    for (var i = 0; i < _items.length; i++) {
                        if (_items[i])
                            _items[i].isSelected = (_items[i]._arKey === currentClass);

                    }
                }

                title: i18n("Aspect Ratio")
                icon.name: "view-fullscreen"
                onAboutToShow: updateChecks()

                enter: Transition {
                }

                exit: Transition {
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

            // -- Unsaved changes notification bar -------------------------
            Item {
                id: unsavedBar

                Layout.fillWidth: true
                implicitHeight: settingsController.needsSave ? unsavedBarContent.implicitHeight : 0
                clip: true

                Rectangle {
                    id: unsavedBarContent

                    width: parent.width
                    implicitHeight: unsavedBarRow.implicitHeight + Kirigami.Units.smallSpacing * 3
                    anchors.bottom: parent.bottom
                    color: Qt.rgba(Kirigami.Theme.neutralTextColor.r, Kirigami.Theme.neutralTextColor.g, Kirigami.Theme.neutralTextColor.b, 0.12)

                    // Top accent line
                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: Math.round(Kirigami.Units.devicePixelRatio)
                        color: Kirigami.Theme.neutralTextColor
                        opacity: 0.4
                    }

                    RowLayout {
                        id: unsavedBarRow

                        anchors.fill: parent
                        anchors.leftMargin: Kirigami.Units.largeSpacing
                        anchors.rightMargin: Kirigami.Units.largeSpacing
                        anchors.topMargin: Kirigami.Units.smallSpacing * 1.5
                        anchors.bottomMargin: Kirigami.Units.smallSpacing * 1.5
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "dialog-information"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            color: Kirigami.Theme.neutralTextColor
                        }

                        Label {
                            text: i18n("Unsaved changes")
                            color: Kirigami.Theme.neutralTextColor
                            Layout.fillWidth: true
                        }

                        Button {
                            text: i18n("Defaults")
                            icon.name: "document-revert"
                            flat: true
                            onClicked: defaultsConfirmDialog.open()
                        }

                        Button {
                            text: i18n("Discard")
                            icon.name: "edit-undo"
                            flat: true
                            onClicked: resetConfirmDialog.open()
                        }

                        Button {
                            text: i18n("Save")
                            icon.name: "document-save"
                            highlighted: true
                            onClicked: {
                                settingsController.save();
                                toast.show(i18n("Settings saved"));
                            }
                        }

                    }

                }

                Behavior on implicitHeight {
                    NumberAnimation {
                        duration: 250
                        easing.type: Easing.OutCubic
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

    // ── What's New dialog ──────────────────────────────────────────
    WhatsNewPage {
        id: whatsNewDialog
    }

    // Auto-show What's New dialog on first launch after update
    Timer {
        interval: 500
        running: settingsController.hasUnseenWhatsNew
        onTriggered: whatsNewDialog.open()
    }

}
