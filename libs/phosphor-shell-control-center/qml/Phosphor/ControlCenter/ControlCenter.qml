// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Registry-backed quick settings with shared appearance and live media.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    // Tile source: an object with createTile(id, parent) -> Item.
    property var provider: null
    // Tile ids to materialise, in display order. The shell feeds this from
    // the registry (and, later, from the user's tile arrangement); a test
    // passes a literal list.
    property list<string> tileIds: []
    // Detail view currently open, or "" for the grid. Read-only for
    // consumers; drive it through openDetail() / closeDetail().
    readonly property alias detailTileId: priv.detailTileId

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

    signal detailOpened(string tileId)
    signal detailClosed(string tileId)

    /// The surface's width. Fixed rather than derived from the cards,
    /// because the cards divide whatever width they are given and would
    /// otherwise collapse to their text. Matches the bar's other panels, so
    /// moving between them is not re-reading a differently shaped surface.
    property real panelWidth: Appearance.stage ? 1040 : Appearance.panelWidth
    readonly property bool shelf: Appearance.stage && width >= 780
    onShelfChanged: arrangeTiles()
    function arrangeTiles() {
        for (const id in priv.tiles) {
            const tile = priv.tiles[id];
            tile.parent = shelf && tile.controlGroup === "levels" ? levels : grid;
        }
    }

    implicitWidth: root.panelWidth
    // The taller of the two views, not just the grid. A host that sizes
    // itself to this would otherwise clip a detail view taller than the
    // grid behind it, and neither view scrolls or clips, so the overflow
    // would simply be cut off.
    implicitHeight: Math.max(main.implicitHeight, detail.implicitHeight) + 2 * Appearance.padding

    /// Where this surface sits along the screen, 0..1, for the stroke and
    /// the top band. Set by the host from the chip that opened it.
    property real railT: 0.5

    ShellSurface {
        anchors.fill: parent
        railT: root.railT
    }
    property var mediaPlayer: null

    QtObject {
        id: priv

        property string detailTileId: ""
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
        if (priv.detailTileId !== "")
            root.closeDetail();
        priv.detailTileId = tileId;
        root.detailOpened(tileId);
        return true;
    }

    // Close the detail view and return to the grid. Safe to call when
    // nothing is open.
    function closeDetail() {
        if (priv.detailTileId === "")
            return;
        const closing = priv.detailTileId;
        priv.detailTileId = "";
        root.detailClosed(closing);
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
                // Layout is the host's job, not the tile's: a tile would
                // otherwise have to know the pane's width to span it. It
                // declares the intent via `spansRow` and this applies it.
                // Tiles come from a provider, so a third-party one may
                // legitimately not span; a tile that declares nothing gets
                // the rail default, which is to span.
                // Cards fill their cell in both directions, so the grid
                // distributes the zone across them instead of leaving the
                // remainder empty. A tile that wants the full width (a
                // level, whose underline is its control and reads better
                // long) spans both columns.
                const wide = item.spansRow === undefined ? false : item.spansRow;
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
                        if (panelId !== "")
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
        anchors.margins: Appearance.padding
        contentWidth: width
        contentHeight: main.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        visible: priv.detailTileId === ""
        Basic.ScrollBar.vertical: Basic.ScrollBar {
            active: scroller.interactive
            contentItem: Rectangle {
                implicitWidth: 4
                radius: 2
                color: Appearance.muted
            }
        }
        ColumnLayout {
            id: main
            width: Math.max(0, scroller.width - (scroller.interactive ? 8 : 0))
            spacing: 14
            visible: priv.detailTileId === ""
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Quick settings")
                        color: Appearance.text
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Connections & controls")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 11
                    }
                }
                ShellButton {
                    iconName: "configure"
                    label: qsTr("Appearance")
                    onClicked: root.panelRequested("appearance")
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.shelf ? 3 : 1
                columnSpacing: 20
                rowSpacing: 14
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 1
                    Text {
                        visible: root.shelf
                        text: qsTr("CONNECTIONS & FOCUS")
                        color: Appearance.muted
                        font.pixelSize: 10
                        font.letterSpacing: 1
                    }
                    GridLayout {
                        id: grid
                        objectName: "connectionsGrid"
                        Layout.fillWidth: true
                        columns: 1
                        rowSpacing: 10
                    }
                }
                ColumnLayout {
                    visible: root.shelf
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 1
                    Text {
                        text: qsTr("SOUND & DISPLAY")
                        color: Appearance.muted
                        font.pixelSize: 10
                        font.letterSpacing: 1
                    }
                    GridLayout {
                        id: levels
                        objectName: "levelsGrid"
                        Layout.fillWidth: true
                        columns: 1
                        rowSpacing: 10
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 1
                    visible: Appearance.media
                    Text {
                        visible: root.shelf
                        text: qsTr("NOW PLAYING")
                        color: Appearance.muted
                        font.pixelSize: 10
                        font.letterSpacing: 1
                    }
                    MediaCard {
                        Layout.fillWidth: true
                        player: root.mediaPlayer
                    }
                }
            }
        }
    }

    DetailPanel {
        id: detail

        anchors.fill: parent
        anchors.margins: Appearance.padding
        tileId: priv.detailTileId
        open: priv.detailTileId !== ""
        // Fed from the tile being drilled into. Without these the panel
        // opened blank and untitled over a hidden grid, which is a dead end
        // the user has to back out of.
        title: root._detailTile ? (root._detailTile.detailTitle ?? "") : ""
        contentComponent: root._detailTile ? (root._detailTile.detailContent ?? null) : null
        onDismissed: root.closeDetail()
    }
}
