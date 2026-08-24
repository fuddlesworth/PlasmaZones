// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The Monitor State dashboard's per-mode preview thumbnails.
 *
 * One LayoutThumbnail per placement mode (snapping layout, tiling algorithm,
 * scrolling strip), exactly one visible at a time, split out of
 * MonitorStatePage when that page crossed the file-size ceiling. Pure view:
 * inputs are read off the page's stateView / root handles (plus the
 * appSettings context property for the shared font settings) so the
 * bindings stay live, and nothing here writes state.
 */
ColumnLayout {
    id: previews

    /// The page's stateView item (local mode/layout/algorithm state, the
    /// strip zones and the preview sizing constants).
    required property var view
    /// The page root (layout lookup helpers, the LayoutComboBox bridge and
    /// the selected screen's aspect ratio).
    required property var page

    spacing: 0

    // Layout preview (snapping)
    LayoutThumbnail {
        id: snappingPreview

        Layout.alignment: Qt.AlignHCenter
        visible: previews.view.isSnapping
        layout: {
            if (previews.view.currentLayout)
                return previews.view.currentLayout;

            // A staged CLEAR means "Default", which resolves to the
            // global default layout — the same layout the selector's
            // own Default row previews. Drawing an empty box here left
            // the big preview blank while the row right below it showed
            // the zones. Gated on the cleared flag, not on any empty
            // id: the daemon also reports an EMPTY layoutId for a
            // context whose active layout is suppressed, and that
            // screen must keep showing as unassigned rather than have
            // the preview reinstate the default the daemon withheld.
            if (previews.view.localLayoutCleared) {
                // The global default can itself be the explicit opt-out (the
                // library card's Clear Default). _findLayout misses it, and
                // the tail literal would then caption the card "Default" —
                // for a state where "Default" IS no layout. Name it honestly.
                if (previews.page._layoutBridge.defaultLayoutId === previews.page._noLayoutToken) {
                    return {
                        "displayName": i18n("None"),
                        "zones": []
                    };
                }
                var fallback = previews.page._findLayout(previews.page._layoutBridge.defaultLayoutId);
                if (fallback)
                    return fallback;
            }
            // The explicit opt-out: no layout at all, on purpose. Its own
            // branch because the daemon reports an empty layoutName for
            // it, and the literal below would then caption the card
            // "Default", which is the state the user just opted out of.
            if (previews.view.localLayoutId === previews.page._noLayoutToken) {
                return {
                    "displayName": i18n("None"),
                    "zones": []
                };
            }
            // The local list does not carry this layout, so there are
            // no zones to draw. The daemon still reports the resolved
            // name, so show that rather than nothing.
            //
            // No raw-id rung here, unlike the tiling preview below, and the
            // asymmetry is deliberate: an algorithm id is a readable word
            // ("bsp"), while a snapping layout id is a braced UUID, so
            // falling back to it would caption the card "{8f3c…}". The
            // InlineMessage on the page already names the unresolved id for
            // this case.
            return {
                "displayName": (previews.view.screenState && previews.view.screenState.layoutName) || i18n("Default"),
                "zones": []
            };
        }
        isSelected: true
        globalAutoAssign: previews.page._layoutBridge.autoAssignAllLayouts
        baseHeight: previews.view._previewHeight
        maxThumbnailWidth: previews.view._previewMaxWidth
        screenAspectRatio: previews.page._selectedScreenAspectRatio
        fontFamily: appSettings.labelFontFamily
        fontSizeScale: appSettings.labelFontSizeScale
        fontWeight: appSettings.labelFontWeight
        fontItalic: appSettings.labelFontItalic
        fontUnderline: appSettings.labelFontUnderline
        fontStrikeout: appSettings.labelFontStrikeout
        Accessible.name: {
            var name = snappingPreview.layout ? (snappingPreview.layout.displayName || "") : "";
            return name ? i18nc("accessible name of the layout preview; %1 is the layout name", "Snapping layout preview, %1", name) : i18nc("accessible name of the layout preview when no layout name is known", "Snapping layout preview");
        }
    }

    // Algorithm preview (tiling)
    LayoutThumbnail {
        id: tilingPreview

        Layout.alignment: Qt.AlignHCenter
        visible: previews.view.isTiling
        layout: {
            var found = previews.page._findLayout(previews.page._autotilePrefix + previews.view.localAlgorithmId);
            if (found)
                return found;

            // Same reasoning AND the same predicate as the snapping
            // preview: an explicit "Default" pick resolves to the
            // global default algorithm the selector's Default row
            // already previews. Gated on the cleared flag, not on an
            // empty id, because the daemon also reports an empty
            // algorithmId for a context whose algorithm is suppressed,
            // and that screen must keep showing as unassigned.
            if (previews.view.localAlgorithmCleared) {
                // Same opt-out-as-default arm as the snapping preview above:
                // without it the tail literal captions the card "Default"
                // (algorithmName is empty for the reserved word and the
                // cleared local id is empty too) — the exact miscaption the
                // comment on that literal warns about, reached via the
                // resolved default instead of the local pick.
                if (previews.page._layoutBridge.defaultAutotileAlgorithm === previews.page._noLayoutToken) {
                    return {
                        "displayName": i18n("None"),
                        "category": 1,
                        "zones": []
                    };
                }
                var fallback = previews.page._findLayout(previews.page._autotilePrefix + previews.page._layoutBridge.defaultAutotileAlgorithm);
                if (fallback)
                    return fallback;
            }
            // The explicit opt-out: autotile mode with nothing tiling, on
            // purpose. Its own branch because the daemon reports an empty
            // algorithmName for the reserved word, and the literal below
            // would fall through to the raw id and caption the card "none".
            if (previews.view.localAlgorithmId === previews.page._noLayoutToken) {
                return {
                    "displayName": i18n("None"),
                    "category": 1,
                    "zones": []
                };
            }
            // getScreenStates reports the algorithm's display name, so
            // prefer it over the raw id ("bsp") the local list missed.
            // category 1 badges it as an algorithm, matching the real
            // entries this literal stands in for — without it the card
            // fell back to 0 and badged the algorithm "Manual".
            return {
                "displayName": (previews.view.screenState && previews.view.screenState.algorithmName) || previews.view.localAlgorithmId || i18n("Default"),
                "category": 1,
                "zones": []
            };
        }
        isSelected: true
        globalAutoAssign: previews.page._layoutBridge.autoAssignAllLayouts
        baseHeight: previews.view._previewHeight
        maxThumbnailWidth: previews.view._previewMaxWidth
        screenAspectRatio: previews.page._selectedScreenAspectRatio
        fontFamily: appSettings.labelFontFamily
        fontSizeScale: appSettings.labelFontSizeScale
        fontWeight: appSettings.labelFontWeight
        fontItalic: appSettings.labelFontItalic
        fontUnderline: appSettings.labelFontUnderline
        fontStrikeout: appSettings.labelFontStrikeout
        Accessible.name: {
            var name = tilingPreview.layout ? (tilingPreview.layout.displayName || "") : "";
            return name ? i18nc("accessible name of the tiling preview; %1 is the algorithm name", "Tiling algorithm preview, %1", name) : i18nc("accessible name of the tiling preview when no algorithm name is known", "Tiling algorithm preview");
        }
    }

    // Strip preview (scrolling): the live strip when the screen has one, else
    // the same card with the empty state in its well. ONE card either way, so
    // the name row and category badge keep naming the template in force even
    // while nothing is on the strip.
    LayoutThumbnail {
        id: scrollingPreview

        Layout.alignment: Qt.AlignHCenter
        visible: previews.view.isScrolling
        // Edge ticks along the screen's resolved strip axis, so a populated
        // card still says which way the strip continues past the box. The
        // empty state reads the same axis for its arrow.
        stripAxisHint: previews.view.stripVertical ? "vertical" : "horizontal"
        stripEmptyCaption: previews.view.scrollingEmptyCaption
        // No onVisibleChanged re-read here: the live timer's
        // triggeredOnStart covers the same visibility and mode
        // transitions (its running binding includes both), and two
        // paths meant two blocking reads in the same frame.
        // category 1 renders the "Dynamic" badge (a live strip
        // snapshot is generated, not editable). Tiles are numbered
        // sequentially in strip order so every visible window gets
        // its own distinct label. Only the first nine of those numbers
        // are reachable from the keyboard: there are nine Snap to Zone
        // digit shortcuts, so a tenth visible tile is drawn with a
        // label no key can address.
        //
        // The daemon normalizes the strip rects against the screen's
        // FULL geometry (the tiles are clipped to the gap-inset work
        // area, so the fractions show the panel gap), which matches
        // this box's screen-shaped aspect. The daemon's own OSD card
        // draws the same shapes on the same basis.
        //
        // No id on this literal, deliberately. LayoutCard reads
        // displayName, category, zones and zoneNumberDisplay off it,
        // and LayoutThumbnail reads aspectRatioClass,
        // referenceAspectRatio, producesOverlappingZones, isAutotile,
        // supportsMasterCount and masterCount — all absent here, all
        // defaulting. A stable id would only invite code elsewhere to
        // treat this throwaway snapshot as a real layout.
        layout: ({
                // The template's name when the screen has one, so the
                // card names what actually shapes the strip. A screen
                // on the default template keeps the bare mode name:
                // the wire field is the raw assignment, so naming a
                // template here would claim an assignment the screen
                // does not have.
                "displayName": previews.view.scrollingTemplateName.length > 0 ? previews.view.scrollingTemplateName : i18nc("tiling mode name", "Scrolling"),
                "category": 1,
                // Unconditionally "all": every tile in this list is a real
                // window and a real digit target. The old "none" arm existed
                // for the placeholder sketch, whose three shapes stood for a
                // strip rather than for tiles and so had to be kept
                // unlabelled. Nothing draws zones for an empty strip now (the
                // card renders StripEmptyState in its well instead), so there
                // is no shape left that a number would misdescribe.
                "zoneNumberDisplay": "all",
                "zones": previews.view.scrollingStripZones
            })
        isSelected: true
        globalAutoAssign: previews.page._layoutBridge.autoAssignAllLayouts
        baseHeight: previews.view._previewHeight
        maxThumbnailWidth: previews.view._previewMaxWidth
        screenAspectRatio: previews.page._selectedScreenAspectRatio
        // Every tile here is a digit target, so no tile may render
        // without its number. ZonePreview hides the label below 16px,
        // and the default 8px floor let a clipped edge column draw at
        // 8px wide — numberless, but still reachable from the keyboard.
        minZoneSize: 16
        fontFamily: appSettings.labelFontFamily
        fontSizeScale: appSettings.labelFontSizeScale
        fontWeight: appSettings.labelFontWeight
        fontItalic: appSettings.labelFontItalic
        fontUnderline: appSettings.labelFontUnderline
        fontStrikeout: appSettings.labelFontStrikeout
        // Counts WINDOWS, not tiles. A tabbed column emits one tile standing
        // for its whole stack, so counting tiles told a screen-reader user
        // "one window" about the five-tab column the pills draw five pills for.
        readonly property int scrollingWindowCount: {
            let total = 0;
            for (const zone of previews.view.scrollingStripZones)
                total += Math.max(1, zone.tabCount || 0);
            return total;
        }
        // An empty strip announces the SAME caption the well draws, rather
        // than a generic "placeholder" phrase. The caption is the whole
        // message (no windows yet, not applied, daemon down), so a screen
        // reader that dropped it would lose the one thing the card says.
        //
        // Keyed on the REASON, not on the zone count and not on the caption
        // text. Two things are going on here.
        //
        // Not the count: the well switches on the caption, and the two part
        // company the moment a caption fires with tiles still in the array (a
        // daemon that died holding a populated strip). Announcing "with 4
        // windows" over a card drawing "PlasmaZones is not running" is worse
        // than either.
        //
        // Not the caption text either: composing one translated sentence into
        // another gives translators a fused clause whose inner half they
        // cannot inflect or reorder. Each state gets a whole sentence instead,
        // chosen off the same branch the caption came from.
        Accessible.name: {
            switch (previews.view.scrollingEmptyReason) {
            case "daemon":
                return i18nc("accessible name of the scrolling strip preview", "Scrolling strip preview, PlasmaZones is not running");
            case "staged":
                return i18nc("accessible name of the scrolling strip preview", "Scrolling strip preview, apply to start scrolling on this screen");
            case "empty":
                return i18nc("accessible name of the scrolling strip preview", "Scrolling strip preview, no windows on the strip yet");
            default:
                return i18np("Scrolling strip preview with %n window", "Scrolling strip preview with %n windows", scrollingPreview.scrollingWindowCount);
            }
        }
    }
}
