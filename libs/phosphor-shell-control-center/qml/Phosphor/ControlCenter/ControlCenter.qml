// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.ControlCenter, the control-tile surface.
//
// A vertical list of control RAILS (network, bluetooth, audio,
// brightness, ...), each a 52 px row whose 2 px underline is the control
// (A3 §2b), with a slide-over detail panel for the rail the user drills
// into. No header, no tile grid, no filled buttons.
//
// Like OSDHost and ToastHost, this renders into whatever item it is
// parented to. It owns no surface of its own, so the shell decides how it
// is presented: hung from the bar as a tethered pane, or parented into a
// standalone layer-shell popout opened through PopoutController. Neither
// choice reaches into this file.
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
    property real panelWidth: 360

    implicitWidth: root.panelWidth
    // The taller of the two views, not just the grid. A host that sizes
    // itself to this would otherwise clip a detail view taller than the
    // grid behind it, and neither view scrolls or clips, so the overflow
    // would simply be cut off.
    implicitHeight: Math.max(grid.implicitHeight, detail.implicitHeight) + 2 * Tokens.spacing_m

    /// Where this surface sits along the screen, 0..1, for the stroke and
    /// the top band. Set by the host from the chip that opened it.
    property real railT: 0.5

    // The surface's own material. It had none: as an engine-placed pane it
    // borrowed PaneHost's ground, and the moment it became an ordinary
    // popout the cards were left floating on the bare desktop with no
    // surface under them. The layers are 05 §5's, the same ones the bar's
    // other panels draw.
    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_l
        color: Theme.isDark ? Qt.rgba(0.027, 0.059, 0.133, 0.94) : Qt.rgba(0.96, 0.976, 1, 0.94)
    }

    SpectrumStroke {
        anchors.fill: parent
        radius: Tokens.radius_l
        t: root.railT
    }

    // The top edge IS the rail over this surface's x-range, so the panel and
    // the bar above it match hue for hue, and it carries the gleam.
    SpectrumRail {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Tokens.radius_l
        anchors.rightMargin: Tokens.radius_l
        thickness: 2
        gleam: true
        sliceStart: Math.max(0, root.railT - 0.09)
        sliceEnd: Math.min(1, root.railT + 0.09)
    }

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
                item.Layout.columnSpan = wide ? 2 : 1;
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

    GridLayout {
        id: grid

        // Top-anchored, and the surface is sized to IT rather than the other
        // way round. This surface is a transient now, not an engine-placed
        // tile, so it gets the size it asks for.
        //
        // It has been both other things and both were wrong. Filling a
        // zone-sized pane stretched five controls across 830 px, so each
        // card became enormous. Hugging the top of a zone-sized pane left
        // two thirds of it empty. Neither is fixable by layout, because the
        // fault was the surface taking a whole zone at all.
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Tokens.spacing_m
        columns: 2
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
        anchors.margins: Tokens.spacing_m
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
