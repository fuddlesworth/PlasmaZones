// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.ControlCenter, the control-tile surface.
//
// A grid of control tiles (network, bluetooth, audio, brightness, ...)
// with a slide-over detail panel for the tile the user drills into.
//
// Like OSDHost and ToastHost, this renders into whatever item it is
// parented to. It owns no surface of its own, so the shell decides how it
// is presented: composed into the bar's BarCanvas socket so it grows out
// of the bar as one continuous painted shape (the connected-corner
// design), or parented into a standalone layer-shell popout opened
// through PopoutController. Neither choice reaches into this file.
//
// Tiles come from a `provider` exposing
//   createTile(id, parent) -> Item
// backed in the shell by a Registry<IControlCenterTileFactory>. The host
// stays registry-agnostic so a test can pass any object with that method.
// Per the factory contract a null return means "unavailable in this
// environment" (no service, no hardware) and is not an error.

import QtQuick
import QtQuick.Layouts
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
    // Tiles per row. The grid reflows rather than scrolling: a control
    // surface that scrolls hides controls behind a gesture, and the
    // catalog is small enough not to need it.
    property int columns: 2
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
    signal detailOpened(string tileId)
    signal detailClosed(string tileId)

    implicitWidth: grid.implicitWidth + 2 * Tokens.spacing_l
    // The taller of the two views, not just the grid. A host that sizes
    // itself to this would otherwise clip a detail view taller than the
    // grid behind it, and neither view scrolls or clips, so the overflow
    // would simply be cut off.
    implicitHeight: Math.max(grid.implicitHeight, detail.implicitHeight) + 2 * Tokens.spacing_l

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
            if (existing)
                existing.destroy();
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
                // otherwise have to know the column count to span a row.
                // It declares the intent via `spansRow` and this applies it.
                item.Layout.fillWidth = true;
                if (item.spansRow)
                    item.Layout.columnSpan = root.columns;
                // The tile chrome carries no id of its own; bind the
                // detail request here so Tile.qml stays a pure view.
                if (item.detailRequested !== undefined)
                    item.detailRequested.connect(function () {
                        root.openDetail(id);
                    });
            }
            // Truthiness, not a null comparison: a factory that falls off
            // the end returns undefined, which `!== null` would report as
            // created while nothing was built.
            root.tileResolved(id, !!item);
        }
        priv.tiles = built;
    }

    onProviderChanged: {
        if (priv.completed)
            root.rebuild();
    }
    onTileIdsChanged: {
        if (priv.completed)
            root.rebuild();
    }
    // columnSpan is written imperatively in rebuild(), so it does not track
    // `columns` on its own. The GridLayout below reflows live, which without
    // this would leave every spanning tile pinned to the old span: a slider
    // built at two columns spanning two of three. Re-apply the spans rather
    // than rebuilding, so live service tiles are not torn down and rebuilt
    // just because the grid got wider.
    onColumnsChanged: {
        if (!priv.completed)
            return;
        for (const id in priv.tiles) {
            const tile = priv.tiles[id];
            if (tile && tile.spansRow)
                tile.Layout.columnSpan = root.columns;
        }
    }
    Component.onCompleted: {
        priv.completed = true;
        root.rebuild();
    }

    GridLayout {
        id: grid

        // Anchored to the top three edges rather than filling: a host that
        // gives the surface more height than the tiles need would otherwise
        // have GridLayout spread the rows down the whole surface, leaving
        // the grid floating in its own gaps.
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Tokens.spacing_l
        columns: root.columns
        columnSpacing: Tokens.spacing_m
        rowSpacing: Tokens.spacing_m
        // Hidden, not merely covered, while a detail view is open. The
        // detail panel is a sibling rather than a child, so leaving the grid
        // visible underneath would keep every tile in the accessibility tree
        // and in the tab order behind a panel the user cannot see past.
        // Hiding it is exactly what takes them out of both, which is the
        // intent: the grid is not reachable while the user is drilled in.
        visible: priv.detailTileId === ""
    }

    DetailPanel {
        id: detail

        anchors.fill: parent
        anchors.margins: Tokens.spacing_l
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
