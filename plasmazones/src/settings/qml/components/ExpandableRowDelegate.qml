// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief The expandable list-row shell shared by RuleRow and the decoration
 * pack rows — extracted verbatim from RuleRow so every expandable row in the
 * app carries the SAME delegate look (hover/press highlight), the SAME
 * whole-row click-to-expand interaction, and the SAME animated expansion.
 *
 * An ItemDelegate whose contentItem stacks a consumer-filled header RowLayout
 * over a clipped, lazily-loaded expansion body:
 *
 *   ExpandableRowDelegate {
 *       expansionContent: Component { ... }   // lazy body, null = no expansion
 *       Label { ... }                          // header children (default alias)
 *       ExpandChevron { expanded: parent... }  // place the indicator anywhere
 *   }
 *
 * Interaction: a click anywhere on the row toggles `expanded` (buttons and
 * other interactive controls in the header consume their own clicks first).
 * The expansion body animates via the project's accordion/fade motion
 * profiles, and its Loader stays alive through the collapse animation so the
 * body fades out instead of being torn down mid-transition (see the inline
 * comments, moved as-is from RuleRow).
 */
ItemDelegate {
    id: row

    /// Current expansion state — toggled by the whole-row click. Lives on the
    /// delegate so each row expands independently; resets on page reload.
    property bool expanded: false
    /// Whether the row expands at all. False = the whole-row click is inert
    /// (a row with no body to show, e.g. a parameter-less decoration pack).
    property bool expandable: true
    /// Lazily-instantiated expansion body. Null disables expansion entirely —
    /// the clip stays collapsed and nothing is ever loaded.
    property Component expansionContent: null
    /// Left inset of the expansion body relative to the row edge.
    property real expansionLeftMargin: Kirigami.Units.gridUnit * 2
    /// Header row children — the consumer's title/summary/badges/buttons.
    default property alias headerContent: headerRow.data
    /// Header row spacing (RuleRow's largeSpacing default).
    property alias headerSpacing: headerRow.spacing

    hoverEnabled: true
    // Whole-row click toggles expansion (Option C). Toolbar buttons, switches
    // and other interactive controls in the header consume their own clicks,
    // so a click on those never reaches this handler — only clicks on the
    // row's passive body (labels, badges, empty space) toggle. The
    // ExpandChevron's rotation signals the state.
    onClicked: {
        if (row.expandable)
            row.expanded = !row.expanded;
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            id: headerRow

            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
        }

        // ── Expansion area ───────────────────────────────────────────────

        // Clipped container that animates the expansion in/out instead of
        // snapping height between 0 and the full body. Mirrors SettingsCard's
        // expand/collapse pattern: clip:true keeps the loaded body from
        // bleeding above the row during the height interpolation, and
        // PhosphorMotionAnimation on `Layout.preferredHeight` + `opacity`
        // hands the timing off to the project's motion profile (so
        // animation-speed and reduce-motion preferences propagate without
        // a hardcoded duration). The Loader stays `active` for one extra
        // animation cycle when collapsing so the body can fade out
        // gracefully instead of being torn down mid-transition.
        Item {
            id: expansionClip

            // `_active` lags `row.expanded` through the collapse animation by
            // also gating on `Layout.preferredHeight > 0` — Qt's animation
            // updates the height property directly during the transition,
            // so the value is positive while interpolating from full→0 and
            // only reaches 0 when the animation lands. Without this lag the
            // Loader would unload synchronously on collapse-start and the
            // user would see the viewport blank before the height finished
            // shrinking. Re-expanding mid-collapse keeps the Loader active
            // (no reload thrash) because the height never reaches 0.
            property bool _active: row.expansionContent !== null && (row.expanded || Layout.preferredHeight > 0)

            Layout.fillWidth: true
            Layout.leftMargin: row.expansionLeftMargin
            Layout.preferredHeight: row.expanded ? expansionLoader.implicitHeight + Kirigami.Units.smallSpacing : 0
            clip: true
            opacity: row.expanded ? 1 : 0
            visible: Layout.preferredHeight > 0 || opacity > 0

            Behavior on Layout.preferredHeight {
                PhosphorMotionAnimation {
                    profile: row.expanded ? "widget.accordionExpand" : "widget.accordionCollapse"
                    durationOverride: Kirigami.Units.shortDuration
                }
            }

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: row.expanded ? "widget.fadeIn" : "widget.fadeOut"
                    durationOverride: Kirigami.Units.veryShortDuration * 2
                }
            }

            // Lazy-loaded expansion body. `active` gates instantiation on the
            // row actually being expanded — collapsed rows pay zero cost.
            Loader {
                id: expansionLoader

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                active: expansionClip._active
                visible: active
                sourceComponent: row.expansionContent
            }
        }
    }
}
