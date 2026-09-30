// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Registry-backed quick settings with shared appearance and live media.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root

    // Tile source: an object with createTile(id, parent) -> Item.
    property var provider: null
    // Tile ids to materialise, in display order. The shell feeds this from
    // the registry (and, later, from the user's tile arrangement); a test
    // passes a literal list.
    property list<string> tileIds: []
    // Full detail surfaces are supplied by the shell, keeping this module
    // independent of the bar's service-bound panels.
    property var detailPanels: ({})
    readonly property alias detailPanelId: priv.detailPanelId
    // Detail view currently open, or "" for the grid. Read-only for
    // consumers; drive it through openDetail() / closeDetail().
    readonly property alias detailTileId: priv.detailTileId
    // The bar fallback caches this surface after closing the pane. Release
    // detail tasks even when the host hides us instead of destroying us.
    onVisibleChanged: {
        if (!visible && priv.completed)
            root.closeDetail();
    }

    // Anchors, positioners and layouts mirror under a right-to-left locale,
    // but only when this is set; QML does not infer it from the application
    // layout direction. Inherited so the tiles and the detail panel follow.
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    // Emitted when a tile is materialised or refused, so the shell can log
    // an unavailable service without this component knowing what logging
    // is. `created` false means the provider returned null.
    signal tileResolved(string tileId, bool created)
    // Emitted as a detail view opens and after it closes.
    /// A card asked for a full view that lives outside this surface (a bar
    /// panel). Carries the bar-widget id, for the host to open.
    signal panelRequested(string panelId)
    signal closeRequested
    signal focusToggled
    signal nightLightToggled
    property bool focusEnabled: false
    property bool focusAvailable: true
    property bool nightLightEnabled: false
    property bool nightLightAvailable: false
    property string batterySummary: ""
    property string powerSummary: ""
    property string notificationSummary: ""
    property string nightLightSummary: nightLightAvailable ? (nightLightEnabled ? i18n("On") : i18n("Off")) : i18n("Unavailable")
    property string darkModeSummary: ""
    property string airplaneSummary: ""
    property string wallpaperSummary: ""
    property bool darkModeEnabled: false
    property bool airplaneEnabled: false
    readonly property var serviceItems: ({
            focus: {
                title: i18n("Focus"),
                summary: root.focusEnabled ? i18n("On") : i18n("Off"),
                icon: "notifications-disabled",
                selected: root.focusEnabled
            },
            nightlight: {
                title: i18n("Night light"),
                summary: root.nightLightSummary,
                icon: "brightness-high",
                selected: root.nightLightEnabled
            },
            darkmode: {
                title: i18n("Dark mode"),
                summary: root.darkModeSummary,
                icon: "weather-clear-night",
                selected: root.darkModeEnabled
            },
            airplane: {
                title: i18n("Airplane mode"),
                summary: root.airplaneSummary,
                icon: "flightmode-on",
                selected: root.airplaneEnabled
            },
            power: {
                title: i18n("Power profiles"),
                summary: root.powerSummary,
                icon: "speedometer",
                selected: false
            },
            wallpaper: {
                title: i18n("Wallpaper"),
                summary: root.wallpaperSummary,
                icon: "preferences-desktop-wallpaper",
                selected: false
            }
        })

    signal detailOpened(string tileId)
    signal detailClosed(string tileId)

    /// The surface's width. Fixed rather than derived from the cards,
    /// because the cards divide whatever width they are given and would
    /// otherwise collapse to their text. Matches the bar's other panels, so
    /// moving between them is not re-reading a differently shaped surface.
    property real panelWidth: Appearance.panelWidth
    readonly property bool shelf: width >= 780
    onShelfChanged: arrangeTiles()
    function arrangeTiles() {
        for (const id in priv.tiles) {
            const tile = priv.tiles[id];
            tile.parent = tile.controlGroup === "levels" ? levels : grid;
        }
    }

    implicitWidth: detailLoader.active ? 410 : root.panelWidth
    // Give embedded details their own height; the main view scrolls when the
    // output is shorter than its preferred size.
    implicitHeight: detailLoader.active ? (detailLoader.status === Loader.Ready ? detailLoader.item.implicitHeight : 760) : Math.max(main.implicitHeight, detail.implicitHeight) + 2 * (Appearance.padding + 1)

    /// Where this surface sits along the screen, 0..1, for the stroke and
    /// the top band. Set by the host from the chip that opened it.
    property real railT: 0.5
    property Component decoration: null

    ShellSurface {
        id: ground
        visible: !detailLoader.active
        property bool shaderAnchor: true
        anchors.fill: parent
        railT: root.railT
    }
    DecorationSlot {
        visible: !detailLoader.active
        anchors.fill: parent
        component: root.decoration
        contentItem: ground
        surfacePath: "shell.phosphor.popout"
        layeredStages: true
    }
    property var mediaPlayer: null
    property var spectrum: AudioSpectrum

    QtObject {
        id: priv

        property string detailTileId: ""
        property string detailPanelId: ""
        property Item returnFocus: null
        // Rebuilds are suppressed until construction finishes. Setting
        // `provider` and `tileIds` as initial properties fires both change
        // handlers during initialization, and Component.onCompleted then
        // fires a third time, so an unguarded rebuild built every tile
        // twice over. Deliberately NOT coalesced through Qt.callLater:
        // rebuilding synchronously keeps the tile set observable on the
        // line that changed it, which both the host and these tests rely
        // on. Rebuilds are user-scale events, not per-frame ones.
        property bool completed: false
        // Materialised tiles by id, so a rebuild can destroy exactly what
        // it created. The provider owns construction; we own teardown,
        // matching the IControlCenterTileFactory lifetime contract
        // (parent owns, factory does not retain).
        property var tiles: ({})
    }

    // Open the detail view for `tileId`. Returns false when the tile has
    // no materialised instance (an unavailable tile cannot be drilled
    // into).
    // The tile currently drilled into, or null. The detail panel reads its
    // title and content from here, so a tile owns what its detail view
    // contains without knowing how the panel presents it.
    readonly property var _detailTile: priv.detailTileId !== "" ? (priv.tiles[priv.detailTileId] ?? null) : null

    function openDetail(tileId) {
        if (!tileId || priv.tiles[tileId] === undefined)
            return false;
        if (priv.detailTileId === tileId)
            return true;
        // Announce the outgoing view before the incoming one so a listener
        // never sees two detail views open at once.
        if (priv.detailTileId !== "" || priv.detailPanelId !== "")
            root.closeDetail();
        priv.returnFocus = priv.tiles[tileId];
        priv.detailTileId = tileId;
        const panelId = priv.tiles[tileId].detailPanelId ?? "";
        priv.detailPanelId = root.detailPanels[panelId] ? panelId : "";
        root.detailOpened(tileId);
        return true;
    }

    // Service pages do not need a registry tile. Keep the original overview
    // control as the return target when one service page opens another.
    function openPanel(panelId, focusItem) {
        if (!panelId || !root.detailPanels[panelId])
            return false;
        if (priv.detailPanelId === panelId)
            return true;
        const previous = priv.detailTileId || priv.detailPanelId;
        if (previous !== "")
            root.detailClosed(previous);
        if (focusItem)
            priv.returnFocus = focusItem;
        priv.detailTileId = "";
        priv.detailPanelId = panelId;
        root.detailOpened(panelId);
        return true;
    }

    // Close the detail view and return to the grid. Safe to call when
    // nothing is open.
    function closeDetail() {
        if (priv.detailTileId === "" && priv.detailPanelId === "")
            return;
        const closing = priv.detailTileId || priv.detailPanelId;
        const returnFocus = priv.returnFocus;
        priv.detailTileId = "";
        priv.detailPanelId = "";
        priv.returnFocus = null;
        root.detailClosed(closing);
        if (root.visible && returnFocus)
            returnFocus.forceActiveFocus();
    }

    // Rebuild every tile from the current provider + tileIds. Called
    // automatically when either changes.
    function rebuild() {
        // A detail view belongs to a tile instance that is about to be
        // destroyed, so it cannot survive the rebuild.
        root.closeDetail();

        for (const id in priv.tiles) {
            const existing = priv.tiles[id];
            if (existing) {
                // Taken out of the LAYOUT before being destroyed. destroy()
                // is deferred to the event loop while the replacements below
                // are created synchronously, so both generations sat in the
                // grid together for a frame and the whole thing visibly
                // reflowed. Hiding and unmanaging the old one first means the
                // deferred delete is invisible.
                existing.visible = false;
                existing.parent = null;
                existing.destroy();
            }
        }
        priv.tiles = ({});

        if (!root.provider)
            return;
        // Duck-type the seam before using it, the way Slot and ToastHost do
        // for their own provider seams. Without this a provider object that
        // does not implement the contract throws inside the loop below,
        // after priv.tiles has already been emptied, leaving an empty grid
        // and a half-fired tileResolved stream with nothing logged.
        if (typeof root.provider.createTile !== "function") {
            console.warn("ControlCenter: provider does not implement createTile(id, parent); no tiles built");
            return;
        }

        const built = ({});
        for (let i = 0; i < root.tileIds.length; ++i) {
            const id = root.tileIds[i];
            const item = root.provider.createTile(id, grid);
            if (item) {
                built[id] = item;
                // Each tile fills one column in its connection or levels group.
                item.Layout.fillWidth = true;
                // NOT fillHeight: a card keeps its own height. Stretching
                // them to fill was what turned a five-control panel into
                // five 230 px slabs.
                item.Layout.fillHeight = false;
                item.Layout.columnSpan = 1;
                // Step each card along the shared field by its position, so
                // the grid reads as one gradient rather than a set of
                // independently coloured cards (05 R1).
                if (item.railT !== undefined)
                    item.railT = root.tileIds.length > 1 ? i / (root.tileIds.length - 1) : 0.5;
                // The tile chrome carries no id of its own; bind the
                // detail request here so Tile.qml stays a pure view.
                if (item.detailRequested !== undefined) {
                    // A card that names a bar panel hands the request out
                    // rather than opening the in-surface detail view: the
                    // panel already exists and is what the matching chip
                    // opens, so drilling in here and pressing the chip land
                    // in the same place.
                    const panelId = item.detailPanelId === undefined ? "" : item.detailPanelId;
                    item.detailRequested.connect(function () {
                        if (panelId !== "" && !root.detailPanels[panelId])
                            root.panelRequested(panelId);
                        else
                            root.openDetail(id);
                    });
                }
            }
            // Truthiness, not a null comparison: a factory that falls off
            // the end returns undefined, which `!== null` would report as
            // created while nothing was built.
            root.tileResolved(id, !!item);
        }
        priv.tiles = built;
        arrangeTiles();
    }

    onProviderChanged: {
        if (priv.completed)
            root.rebuild();
    }
    onTileIdsChanged: {
        if (priv.completed)
            root.rebuild();
    }
    Component.onCompleted: {
        priv.completed = true;
        root.rebuild();
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        anchors.margins: Appearance.padding + 1
        contentWidth: width
        contentHeight: main.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        visible: priv.detailTileId === "" && priv.detailPanelId === ""
        Basic.ScrollBar.vertical: Basic.ScrollBar {
            active: scroller.interactive
        }
        ColumnLayout {
            id: main
            width: Math.max(0, scroller.width - (scroller.interactive ? 8 : 0))
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: root.shelf ? 21 : 20
                spacing: 10
                Text {
                    text: qsTr("Quick settings")
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((root.shelf ? 21 : 19) * Appearance.textScale)
                    font.weight: Font.Medium
                }
                Item {
                    Layout.fillWidth: true
                }
                ShellButton {
                    id: batteryButton
                    objectName: "quickSettingsBattery"
                    text: root.batterySummary
                    label: i18n("Battery details, %1").arg(root.batterySummary)
                    iconName: "battery"
                    foreground: Appearance.muted
                    labelSize: 10
                    flat: true
                    onClicked: {
                        if (!root.openPanel("battery", batteryButton))
                            root.panelRequested("battery");
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ShellButton {
                    text: "×"
                    outlined: true
                    label: qsTr("Close quick settings")
                    implicitWidth: 30
                    labelSize: 17
                    flat: true
                    onClicked: root.closeRequested()
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.shelf ? 3 : 1
                columnSpacing: root.shelf ? 30 : 0
                rowSpacing: 0
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: root.shelf ? (main.width - 60) / 3.05 : main.width
                    spacing: 0
                    Text {
                        visible: root.shelf
                        Layout.bottomMargin: 18
                        text: qsTr("CONNECTIONS & FOCUS")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: Math.round((10) * Appearance.textScale)
                        font.letterSpacing: 1
                    }
                    GridLayout {
                        id: grid
                        objectName: "connectionsGrid"
                        Layout.fillWidth: true
                        columns: 1
                        rowSpacing: 8
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        Layout.bottomMargin: 20
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            model: ["focus", "nightlight", "darkmode", "airplane", "power", "wallpaper"]
                            delegate: Basic.Button {
                                id: serviceButton
                                required property string modelData
                                readonly property var service: root.serviceItems[modelData]
                                objectName: "quickSettings-" + modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: Math.max(62, contentItem.implicitHeight + 20)
                                padding: 10
                                enabled: modelData !== "focus" || root.focusAvailable
                                Accessible.name: i18n("%1, %2").arg(service.title).arg(service.summary)
                                Accessible.checkable: modelData === "focus"
                                Accessible.checked: modelData === "focus" && root.focusEnabled
                                onClicked: {
                                    if (modelData === "focus")
                                        root.focusToggled();
                                    else if (!root.openPanel(modelData, serviceButton))
                                        root.panelRequested(modelData);
                                }
                                background: Rectangle {
                                    radius: 9
                                    color: serviceButton.service.selected ? Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[2], 0.14)) : serviceButton.hovered ? Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[1], 0.08)) : Appearance.recess
                                    border.width: 1
                                    border.color: serviceButton.visualFocus ? Appearance.text : Appearance.outline
                                }
                                contentItem: RowLayout {
                                    spacing: 9
                                    ShellIcon {
                                        Layout.preferredWidth: 19
                                        Layout.preferredHeight: 19
                                        source: serviceButton.service.icon
                                        isMask: true
                                        color: serviceButton.service.selected ? Appearance.stops[2] : Appearance.muted
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Text {
                                            Layout.fillWidth: true
                                            text: serviceButton.service.title
                                            color: Appearance.text
                                            font.family: Tokens.font_family_ui
                                            font.pixelSize: Math.round(11 * Appearance.textScale)
                                            wrapMode: Text.Wrap
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: serviceButton.service.summary
                                            color: Appearance.muted
                                            font.family: Tokens.font_family_ui
                                            font.pixelSize: Math.round(10 * Appearance.textScale)
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: root.shelf ? (main.width - 60) * 1.1 / 3.05 - 30 : main.width
                    Layout.leftMargin: root.shelf ? 30 : 0
                    spacing: 0
                    Item {
                        Layout.preferredHeight: 0
                        Layout.preferredWidth: 0
                        Rectangle {
                            visible: root.shelf
                            x: -30
                            height: parent.parent.height
                            width: 1
                            color: Appearance.outline
                        }
                    }
                    Text {
                        visible: root.shelf
                        Layout.bottomMargin: 18
                        text: qsTr("SOUND & DISPLAY")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: Math.round((10) * Appearance.textScale)
                        font.letterSpacing: 1
                    }
                    GridLayout {
                        id: levels
                        objectName: "levelsGrid"
                        Layout.fillWidth: true
                        Layout.topMargin: root.shelf ? 0 : 0
                        columns: 1
                        rowSpacing: 8
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: root.shelf ? (main.width - 60) * 0.95 / 3.05 - 30 : main.width
                    Layout.leftMargin: root.shelf ? 30 : 0
                    Layout.topMargin: root.shelf ? 0 : 12
                    spacing: 0
                    Item {
                        Layout.preferredHeight: 0
                        Layout.preferredWidth: 0
                        Rectangle {
                            visible: root.shelf
                            x: -30
                            height: parent.parent.height
                            width: 1
                            color: Appearance.outline
                        }
                    }
                    Text {
                        visible: root.shelf
                        Layout.bottomMargin: 18
                        text: qsTr("NOW PLAYING")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: Math.round((10) * Appearance.textScale)
                        font.letterSpacing: 1
                    }
                    MediaCard {
                        Layout.fillWidth: true
                        player: root.mediaPlayer
                        spectrum: root.spectrum
                        shelf: root.shelf
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16
                spacing: Tokens.spacing_m
                ShellButton {
                    id: powerButton
                    objectName: "quickSettingsPower"
                    implicitWidth: contentItem.implicitWidth + 2 * Tokens.spacing_s
                    implicitHeight: root.shelf ? 26 : 27
                    text: root.powerSummary
                    foreground: Appearance.accent
                    label: i18n("Power profiles, %1").arg(root.powerSummary)
                    labelSize: 10
                    flat: true
                    onClicked: {
                        if (!root.openPanel("power", powerButton))
                            root.panelRequested("power");
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ShellButton {
                    objectName: "quickSettingsAppearance"
                    foreground: Appearance.accent
                    implicitWidth: contentItem.implicitWidth + 2 * Tokens.spacing_s
                    implicitHeight: root.shelf ? 26 : 27
                    text: qsTr("Appearance ↗")
                    labelSize: 10
                    flat: true
                    onClicked: root.panelRequested("appearance")
                }
            }
        }
    }

    DetailPanel {
        id: detail

        anchors.fill: parent
        anchors.margins: Appearance.padding + 1
        tileId: priv.detailTileId
        open: priv.detailTileId !== "" && priv.detailPanelId === ""
        // Fed from the tile being drilled into. Without these the panel
        // opened blank and untitled over a hidden grid, which is a dead end
        // the user has to back out of.
        title: root._detailTile ? (root._detailTile.detailTitle ?? "") : ""
        contentComponent: root._detailTile ? (root._detailTile.detailContent ?? null) : null
        onDismissed: root.closeDetail()
    }

    Loader {
        id: detailLoader
        objectName: "quickDetailLoader"
        anchors.fill: parent
        active: priv.detailPanelId !== ""
        sourceComponent: root.detailPanels[priv.detailPanelId] ?? null
        onLoaded: {
            item.embedded = true;
            item.railT = Qt.binding(() => root.railT);
            item.forceActiveFocus();
        }
    }
    Connections {
        target: detailLoader.status === Loader.Ready ? detailLoader.item : null
        ignoreUnknownSignals: true
        function onBackRequested(): void {
            root.closeDetail();
        }
        function onCloseRequested(): void {
            root.closeDetail();
            root.closeRequested();
        }
        function onPanelRequested(panelId: string): void {
            if (!root.openPanel(panelId))
                root.panelRequested(panelId);
        }
    }
}
