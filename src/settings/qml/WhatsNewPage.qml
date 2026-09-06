// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/*
 * Browsable release history. The bundled whatsnew.json carries every release
 * PlasmaZones has ever shipped (150+ of them, 590 highlights), so this is a
 * two-pane browser rather than one scrolling list: a rail of version series on
 * the left and a reading pane on the right.
 *
 * The pane shows one of three views. A single release; the digest of
 * everything since the user's last version, grouped by kind; or, whenever a
 * search or kind filter is active, every matching highlight across the whole
 * history under a heading per release. That last one is why filtering pins
 * "All matches" to the top of the rail: a query is a question about the
 * history, not about whichever release happened to be selected when it was
 * typed.
 *
 * Both panes are ListViews over flat row arrays, so the delegate count stays
 * proportional to what is on screen even when a bare "Fixed" filter matches
 * 305 highlights.
 */
Kirigami.Dialog {
    id: root

    readonly property color subtleBg: Kirigami.Theme.alternateBackgroundColor
    readonly property color subtleBorder: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    readonly property int thinBorder: 1

    readonly property var entries: settingsController.whatsNewEntries
    readonly property int unseenCount: settingsController.unseenWhatsNewReleaseCount
    readonly property string baselineVersion: settingsController.whatsNewBaselineVersion

    // Live filter state. `query` matches highlight text and version strings,
    // `kindFilter` is "" (all) or one of the three highlight kinds.
    property string query: ""
    property string kindFilter: ""

    // What the reading pane shows: an index into `entries` for a single
    // release, or one of the two cross-release views below.
    readonly property int digestIndex: -1
    readonly property int resultsIndex: -2
    property int selectedIndex: root.digestIndex
    // Series names the rail has expanded. Seeded in reset() with the newest.
    property var expandedSeries: []

    title: i18n("What's New in PlasmaZones %1", Qt.application.version)
    preferredWidth: Kirigami.Units.gridUnit * 46
    // Use maximumHeight instead of preferredHeight to avoid Kirigami.Dialog
    // binding loop on "y" (its overlay centering feeds back into itself).
    maximumHeight: Kirigami.Units.gridUnit * 32
    standardButtons: Dialog.NoButton
    padding: Kirigami.Units.largeSpacing

    onOpened: {
        root.resetView();
        settingsController.markWhatsNewSeen();
    }

    // ── Model helpers ───────────────────────────────────────────────
    // All of these are plain JS over the CONSTANT `entries` list, recomputed
    // when the filter state changes rather than bound per delegate.

    function resetView() {
        root.query = "";
        root.kindFilter = "";
        root.selectedIndex = root.unseenCount > 0 ? root.digestIndex : 0;
        root.expandedSeries = root.entries.length > 0 ? [root.entries[0].series] : [];
    }

    /// The highlights of `release` that pass the current kind filter and query.
    function filteredHighlights(release) {
        const q = root.query.toLowerCase();
        const kind = root.kindFilter;
        let out = [];
        for (let i = 0; i < release.highlights.length; ++i) {
            const h = release.highlights[i];
            if (kind !== "" && h.kind !== kind)
                continue;
            if (q !== "" && h.text.toLowerCase().indexOf(q) === -1 && release.version.indexOf(q) === -1)
                continue;
            out.push(h);
        }
        return out;
    }

    /// Rail rows, as a flat array so one ListView renders headers and
    /// releases together. The series structure survives filtering: a series
    /// with matches force-expands and one without drops out entirely, so the
    /// user keeps their bearings instead of reading a bare list of numbers.
    function railRows() {
        const filtering = root.query !== "" || root.kindFilter !== "";
        let rows = [];
        // While filtering, the pinned row is the cross-release result set;
        // the digest is itself a "since version X" slice and stacking the two
        // reads as two competing answers to the same question.
        if (filtering)
            rows.push({
                "type": "results",
                "hits": root.totalHits()
            });
        else if (root.unseenCount > 0)
            rows.push({
                "type": "digest"
            });
        // Group first, so a series header can carry the count of what
        // actually survives the filter underneath it.
        let series = [];
        let bySeries = {};
        for (let i = 0; i < root.entries.length; ++i) {
            const release = root.entries[i];
            const hits = filtering ? root.filteredHighlights(release).length : release.highlights.length;
            if (filtering && hits === 0)
                continue;
            if (bySeries[release.series] === undefined) {
                bySeries[release.series] = [];
                series.push(release.series);
            }
            bySeries[release.series].push({
                "type": "release",
                "index": i,
                "release": release,
                "hits": hits
            });
        }
        for (let s = 0; s < series.length; ++s) {
            const name = series[s];
            rows.push({
                "type": "series",
                "series": name,
                "count": bySeries[name].length
            });
            if (filtering || root.expandedSeries.indexOf(name) !== -1)
                rows = rows.concat(bySeries[name]);
        }
        return rows;
    }

    function totalHits() {
        let n = 0;
        for (let i = 0; i < root.entries.length; ++i)
            n += root.filteredHighlights(root.entries[i]).length;
        return n;
    }

    function toggleSeries(series) {
        let next = root.expandedSeries.slice();
        const at = next.indexOf(series);
        if (at === -1)
            next.push(series);
        else
            next.splice(at, 1);
        root.expandedSeries = next;
    }

    /// Every highlight from the releases the user has not seen yet, grouped by
    /// kind. This is the "what changed while I was away" view, so it reads by
    /// kind rather than by version.
    function digestGroups() {
        let byKind = {
            "new": [],
            "changed": [],
            "fixed": []
        };
        for (let i = 0; i < root.entries.length; ++i) {
            const release = root.entries[i];
            if (!release.unseen)
                continue;
            const hs = root.filteredHighlights(release);
            for (let j = 0; j < hs.length; ++j)
                if (byKind[hs[j].kind] !== undefined)
                    byKind[hs[j].kind].push(hs[j].text);
        }
        let groups = [];
        const order = ["new", "changed", "fixed"];
        for (let k = 0; k < order.length; ++k)
            if (byKind[order[k]].length > 0)
                groups.push({
                    "kind": order[k],
                    "items": byKind[order[k]]
                });
        return groups;
    }

    /// The reading pane's rows, flattened for the ListView. All three views
    /// share one row vocabulary: title, kind, release, item, empty.
    function paneRows() {
        if (root.selectedIndex === root.resultsIndex)
            return root.resultRows();
        if (root.selectedIndex === root.digestIndex)
            return root.digestRows();
        return root.releaseRows(root.selectedIndex);
    }

    /// Every matching highlight in the whole file, under a heading per
    /// release. This is what a search or a kind chip lands on, so a query is
    /// answered across the history rather than inside one release.
    function resultRows() {
        let body = [];
        let total = 0;
        for (let i = 0; i < root.entries.length; ++i) {
            const release = root.entries[i];
            const hs = root.filteredHighlights(release);
            if (hs.length === 0)
                continue;
            total += hs.length;
            body.push({
                "type": "release",
                "index": i,
                "version": release.version,
                "date": release.date
            });
            for (let j = 0; j < hs.length; ++j)
                body.push({
                    "type": "item",
                    "kind": hs[j].kind,
                    "text": hs[j].text
                });
        }
        let rows = [
            {
                "type": "title",
                "text": i18nc("@title the reading pane is showing search results", "All matches"),
                "sub": i18np("%n highlight", "%n highlights", total)
            }
        ];
        if (body.length === 0)
            rows.push({
                "type": "empty",
                "text": root.query !== "" ? i18n("Nothing matches “%1”.", root.query) : i18n("No highlights of this kind.")
            });
        return rows.concat(body);
    }

    /// Everything the user missed, grouped by kind rather than by version.
    function digestRows() {
        let rows = [
            {
                "type": "title",
                "text": i18n("Since %1", root.baselineVersion),
                "sub": i18np("%n release", "%n releases", root.unseenCount)
            }
        ];
        const groups = root.digestGroups();
        for (let g = 0; g < groups.length; ++g) {
            rows.push({
                "type": "kind",
                "kind": groups[g].kind
            });
            for (let i = 0; i < groups[g].items.length; ++i)
                rows.push({
                    "type": "item",
                    "kind": "",
                    "text": groups[g].items[i]
                });
        }
        return rows;
    }

    function releaseRows(index) {
        if (index < 0 || index >= root.entries.length)
            return [];
        const release = root.entries[index];
        let rows = [
            {
                "type": "title",
                "text": release.version,
                "sub": release.date
            }
        ];
        const hs = root.filteredHighlights(release);
        for (let i = 0; i < hs.length; ++i)
            rows.push({
                "type": "item",
                "kind": hs[i].kind,
                "text": hs[i].text
            });
        return rows;
    }

    function kindLabel(kind) {
        if (kind === "new")
            return i18nc("@label a release highlight that adds a feature", "New");
        if (kind === "changed")
            return i18nc("@label a release highlight that changes existing behaviour", "Changed");
        if (kind === "fixed")
            return i18nc("@label a release highlight that fixes a bug", "Fixed");
        return "";
    }

    function kindColor(kind) {
        if (kind === "new")
            return Kirigami.Theme.positiveTextColor;
        if (kind === "changed")
            return Kirigami.Theme.neutralTextColor;
        if (kind === "fixed")
            return Kirigami.Theme.linkColor;
        return Kirigami.Theme.disabledTextColor;
    }

    // ── Reusable pieces ─────────────────────────────────────────────

    component KindBadge: Rectangle {
        id: badge

        required property string kind

        readonly property color tint: root.kindColor(badge.kind)

        implicitWidth: badgeLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
        implicitHeight: badgeLabel.implicitHeight + Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing * 1.5
        visible: badge.kind !== ""
        color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, badge.tint, 0.18)
        border.width: root.thinBorder
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, badge.tint, 0.45)

        Label {
            id: badgeLabel

            anchors.centerIn: parent
            text: root.kindLabel(badge.kind)
            color: badge.tint
            font: Kirigami.Theme.smallFont
        }
    }

    component HighlightRow: RowLayout {
        id: highlight

        required property string kind
        required property string text

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        KindBadge {
            kind: highlight.kind
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Math.round(Kirigami.Units.smallSpacing / 2)
        }

        Label {
            visible: highlight.kind === ""
            text: "\u2022"
            opacity: 0.4
            Layout.alignment: Qt.AlignTop
            Layout.leftMargin: Kirigami.Units.smallSpacing
        }

        Label {
            Layout.fillWidth: true
            text: highlight.text
            wrapMode: Text.WordWrap
        }
    }

    // ── Layout ──────────────────────────────────────────────────────

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Filter bar: free-text search plus the three kind chips.
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField

                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                placeholderText: i18n("Search releases…")
                Accessible.name: i18n("Search release highlights")
                onTextChanged: {
                    root.query = text;
                    // A query rebuilds the rail around matches, so the old
                    // selection index would point at an unlisted release.
                    root.selectSensibleRow();
                }
            }

            Repeater {
                model: [
                    {
                        "kind": "",
                        "label": i18nc("@option:radio show every kind of release highlight", "All")
                    },
                    {
                        "kind": "new",
                        "label": root.kindLabel("new")
                    },
                    {
                        "kind": "changed",
                        "label": root.kindLabel("changed")
                    },
                    {
                        "kind": "fixed",
                        "label": root.kindLabel("fixed")
                    }
                ]

                Button {
                    required property var modelData

                    text: modelData.label
                    checkable: true
                    checked: root.kindFilter === modelData.kind
                    Accessible.name: i18n("Show only %1 highlights", modelData.label)
                    onClicked: {
                        root.kindFilter = modelData.kind;
                        root.selectSensibleRow();
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // Fixed height. The reading pane's content height varies by an order
        // of magnitude between a one-line release and the full digest, and a
        // dialog that resizes under the user as they filter is unusable, so
        // the split gets a definite height and scrolls inside it.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 24
            spacing: Kirigami.Units.largeSpacing

            // ── Version rail ────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                Layout.fillHeight: true
                radius: Kirigami.Units.smallSpacing * 1.5
                color: root.subtleBg
                border.width: root.thinBorder
                border.color: root.subtleBorder

                ListView {
                    id: rail

                    anchors.fill: parent
                    anchors.margins: root.thinBorder
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    model: root.railRows()

                    ScrollBar.vertical: ScrollBar {
                        id: railScrollBar
                    }

                    // One delegate covers all three row kinds. A Loader per
                    // row would be tidier to read, but a Loader does not
                    // inject its own properties into the component it loads,
                    // so each variant would need its own context object for
                    // no visual gain.
                    delegate: ItemDelegate {
                        id: railRow

                        required property var modelData

                        readonly property bool isDigest: railRow.modelData.type === "digest"
                        readonly property bool isResults: railRow.modelData.type === "results"
                        readonly property bool isSeries: railRow.modelData.type === "series"
                        // The two cross-release views share the pinned slot at
                        // the top of the rail; only one is ever present.
                        readonly property bool isPinned: railRow.isDigest || railRow.isResults
                        readonly property bool seriesExpanded: railRow.isSeries && root.expandedSeries.indexOf(railRow.modelData.series) !== -1
                        readonly property bool filtering: root.query !== "" || root.kindFilter !== ""

                        width: rail.width
                        leftPadding: railRow.modelData.type === "release" ? Kirigami.Units.largeSpacing * 2 : Kirigami.Units.largeSpacing
                        // Keep the trailing count clear of the overlay
                        // scrollbar, which otherwise sits on top of it.
                        rightPadding: Kirigami.Units.largeSpacing + railScrollBar.width
                        highlighted: {
                            if (railRow.isDigest)
                                return root.selectedIndex === root.digestIndex;
                            if (railRow.isResults)
                                return root.selectedIndex === root.resultsIndex;
                            return !railRow.isSeries && root.selectedIndex === railRow.modelData.index;
                        }

                        Accessible.name: {
                            if (railRow.isDigest)
                                return i18n("Everything since version %1", root.baselineVersion);
                            if (railRow.isResults)
                                return i18n("Every matching highlight");
                            if (railRow.isSeries)
                                return railRow.seriesExpanded ? i18n("Collapse the %1 releases", railRow.modelData.series) : i18n("Expand the %1 releases", railRow.modelData.series);
                            return i18n("Version %1, released %2", railRow.modelData.release.version, railRow.modelData.release.date);
                        }

                        onClicked: {
                            if (railRow.isSeries)
                                root.toggleSeries(railRow.modelData.series);
                            else if (railRow.isDigest)
                                root.selectedIndex = root.digestIndex;
                            else if (railRow.isResults)
                                root.selectedIndex = root.resultsIndex;
                            else
                                root.selectedIndex = railRow.modelData.index;
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                visible: railRow.isSeries
                                source: railRow.seriesExpanded ? "go-down-symbolic" : "go-next-symbolic"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }

                            // Unseen marker, and the digest's own bullet.
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                visible: railRow.isPinned || (!railRow.isSeries && railRow.modelData.release.unseen)
                                implicitWidth: Kirigami.Units.smallSpacing
                                implicitHeight: Kirigami.Units.smallSpacing
                                radius: width / 2
                                color: Kirigami.Theme.highlightColor
                            }

                            Kirigami.Heading {
                                visible: railRow.isSeries
                                Layout.fillWidth: railRow.isSeries
                                level: 4
                                text: railRow.isSeries ? railRow.modelData.series : ""
                            }

                            Label {
                                visible: !railRow.isSeries
                                Layout.fillWidth: !railRow.isSeries
                                elide: Text.ElideRight
                                text: {
                                    if (railRow.isDigest)
                                        return i18n("Since %1", root.baselineVersion);
                                    if (railRow.isResults)
                                        return i18nc("@item the rail row that shows every match", "All matches");
                                    return railRow.isSeries ? "" : railRow.modelData.release.version;
                                }
                            }

                            // Release count on a series row, match count on
                            // the results row and on each release while a
                            // filter is narrowing it.
                            Label {
                                visible: railRow.isSeries || railRow.isResults || (!railRow.isDigest && railRow.filtering)
                                Layout.rightMargin: Kirigami.Units.smallSpacing
                                opacity: 0.5
                                font: Kirigami.Theme.smallFont
                                text: {
                                    if (railRow.isSeries)
                                        return railRow.modelData.count;
                                    if (railRow.isResults)
                                        return railRow.modelData.hits;
                                    return railRow.isDigest ? "" : railRow.modelData.hits;
                                }
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    visible: rail.count === 0
                    text: root.query !== "" ? i18n("No release mentions “%1”.", root.query) : i18n("No release has highlights of this kind.")
                }
            }

            // ── Reading pane ────────────────────────────────────────
            // A ListView rather than a Column in a ScrollView: the results
            // view can run to every highlight in the file (a bare "Fixed"
            // filter is 305 of them across 150 releases), and those have to
            // be virtualised rather than all instantiated at once.
            ListView {
                id: readingPane

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: Kirigami.Units.smallSpacing
                model: root.paneRows()

                ScrollBar.vertical: ScrollBar {
                    id: paneScrollBar
                }

                // Selection and filter changes rebuild the model; the old
                // scroll offset means nothing against new content, so every
                // view opens at its top.
                onModelChanged: readingPane.positionViewAtBeginning()

                delegate: Column {
                    id: paneRow

                    required property var modelData

                    readonly property real rowWidth: readingPane.width - paneScrollBar.width - Kirigami.Units.smallSpacing

                    width: readingPane.width
                    spacing: 0

                    // Pane title: which view this is.
                    ColumnLayout {
                        visible: paneRow.modelData.type === "title"
                        width: paneRow.rowWidth
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 2
                                text: paneRow.modelData.text || ""
                            }

                            Label {
                                Layout.alignment: Qt.AlignBaseline
                                opacity: 0.5
                                font: Kirigami.Theme.smallFont
                                text: paneRow.modelData.sub || ""
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Kirigami.Separator {
                            Layout.fillWidth: true
                            Layout.topMargin: Kirigami.Units.smallSpacing
                        }
                    }

                    // Kind heading for a digest group.
                    KindBadge {
                        visible: paneRow.modelData.type === "kind"
                        kind: paneRow.modelData.type === "kind" ? paneRow.modelData.kind : ""
                    }

                    // Release heading in the results view. Clicking it drops
                    // the results down to that one release.
                    ItemDelegate {
                        visible: paneRow.modelData.type === "release"
                        width: paneRow.rowWidth
                        horizontalPadding: 0
                        Accessible.name: paneRow.modelData.type === "release" ? i18n("Show only version %1", paneRow.modelData.version) : ""
                        onClicked: root.selectedIndex = paneRow.modelData.index

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Heading {
                                level: 4
                                text: paneRow.modelData.type === "release" ? paneRow.modelData.version : ""
                            }

                            Label {
                                Layout.alignment: Qt.AlignBaseline
                                opacity: 0.5
                                font: Kirigami.Theme.smallFont
                                text: paneRow.modelData.type === "release" ? paneRow.modelData.date : ""
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }

                    HighlightRow {
                        visible: paneRow.modelData.type === "item"
                        width: paneRow.rowWidth
                        kind: paneRow.modelData.kind || ""
                        text: paneRow.modelData.text || ""
                    }

                    Label {
                        visible: paneRow.modelData.type === "empty"
                        width: paneRow.rowWidth
                        wrapMode: Text.WordWrap
                        opacity: 0.6
                        text: paneRow.modelData.text || ""
                    }
                }
            }
        }
    }

    // Where the selection goes when the filter state changes. Typing a query
    // is a question about the whole history, so it lands on the results view
    // rather than leaving the user on one release wondering why the other
    // matches the rail is counting are nowhere to be seen. Clearing the
    // filter hands them back the digest, or the newest release.
    function selectSensibleRow() {
        if (root.query !== "" || root.kindFilter !== "") {
            root.selectedIndex = root.resultsIndex;
            return;
        }
        if (root.selectedIndex === root.resultsIndex)
            root.selectedIndex = root.unseenCount > 0 ? root.digestIndex : 0;
    }
}
