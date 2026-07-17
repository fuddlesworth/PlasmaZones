// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * Single row in the Sidebar's ListView delegate.
 *
 * Extracted from Sidebar.qml so the (originally ~270-line) ItemDelegate
 * body can be reviewed and tested independently. The parent Sidebar
 * threads the model role values in as required properties + a small set
 * of visual settings (compact, navRowHeight); navigation
 * intent flows back out through the three Q_SIGNALS below.
 *
 * Naming: the model role is called `pageId` (not `id`) because `id`
 * shadows the QML `id:` directive and breaks qmlformat 6.11's parser.
 * Sidebar.qml's `_visibleItems()` produces dicts with the same key.
 */
QQC2.ItemDelegate {
    id: rowItem

    /// Model roles (populated by ListView's required-property binding).
    required property string pageId
    required property string title
    required property string iconSource
    required property bool hasQmlSource
    required property int _depth
    required property bool _isCollapsibleHeader
    required property bool _isDrillParent
    required property bool _isExpanded
    required property bool _isDivider

    /// Threaded in by the parent Sidebar.
    required property bool isCurrent
    required property bool compact
    required property real navRowHeight
    property Component trailingDelegate: null

    /// Navigation intents. Parent Sidebar wires these to
    /// controller.currentPageId / toggleCategory / drillInto so the
    /// delegate doesn't reach into Sidebar's internals.
    signal navigationRequested(string pageId)
    signal categoryToggleRequested(string pageId)
    signal drillIntoRequested(string pageId)

    // Guard against a null attached view: a pooled/destroyed delegate is
    // detached from its ListView and the bare access would throw.
    width: ListView.view ? ListView.view.width : 0
    // Legacy row height — explicit so the rail's vertical rhythm is
    // stable regardless of label metrics. Dividers get a shorter slot
    // than nav rows so the breaks read as breathing-room rather than
    // empty rows. `highlighted` is intentionally NOT used because we
    // paint the background ourselves below.
    implicitHeight: rowItem._isDivider ? Kirigami.Units.largeSpacing : rowItem.navRowHeight
    // Accessible.name is always the row's title — in compact mode the
    // visible Label is hidden, so a screen reader would otherwise have
    // no announceable content. Divider rows are flagged so AT tools
    // skip them rather than reading "Button".
    Accessible.name: rowItem._isDivider ? "" : rowItem.title
    Accessible.role: rowItem._isDivider ? Accessible.Separator : Accessible.Button
    // Active row reads as "checked" to AT tools (matches the visual
    // highlight + bold label affordance). Dividers stay
    // un-checkable — they're ornament with role Separator.
    Accessible.checkable: !rowItem._isDivider
    Accessible.checked: !rowItem._isDivider && rowItem.isCurrent
    // Dividers are visual ornament — disable click routing and any
    // focus/hover state so the cursor doesn't change passing over them.
    enabled: !rowItem._isDivider
    hoverEnabled: !rowItem._isDivider
    // Compact mode collapses leftPadding to zero so the icon centers
    // visually in the narrow rail. Depth indent is also dropped —
    // there's no room to express hierarchy when only the icon is
    // visible, and the user has the tooltip + active accent to orient
    // by.
    leftPadding: rowItem.compact ? 0 : (Kirigami.Units.largeSpacing + (rowItem._depth * Kirigami.Units.gridUnit))
    rightPadding: rowItem.compact ? 0 : Kirigami.Units.largeSpacing
    // Tooltip surfaces the row label when compact mode has hidden it.
    // Delay uses the Kirigami platform unit so theming / accessibility
    // overrides (slow-motion mode, high-feedback profiles) flow
    // through. Held off for divider rows because they have no label.
    // `QtQuick.Controls` is imported as QQC2 so the attached property
    // has to be namespaced too — unqualified `ToolTip.*` attached
    // properties resolve against a non-existent attached object.
    QQC2.ToolTip.visible: rowItem.compact && rowItem.hovered && !rowItem._isDivider
    QQC2.ToolTip.text: rowItem.title
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    onClicked: {
        if (rowItem._isDivider)
            return;

        if (rowItem._isCollapsibleHeader)
            rowItem.categoryToggleRequested(rowItem.pageId);
        else if (rowItem._isDrillParent)
            rowItem.drillIntoRequested(rowItem.pageId);
        else if (rowItem.hasQmlSource)
            rowItem.navigationRequested(rowItem.pageId);
    }

    background: Rectangle {
        id: delegateBackground

        // Default Rectangle color is white; gate the tint Behavior on
        // Component.completed so the first paint lands without an
        // animated white→transparent flash.
        //
        // Shared pattern with SidebarBackButton.qml — Qt's
        // `Behavior on X` block must be a sibling of the animated
        // property, so we can't extract this into a reusable
        // Component. If you change the gate/profile here, mirror
        // the edit there.
        property bool _behaviorReady: false

        Component.onCompleted: _behaviorReady = true
        // Active row: an opaque background/highlight mix (scheme-correct
        // selection surface, no alpha fabrication). Hover: the scheme's
        // DecorationHover role mixed the same way for a subtle
        // "interactive" cue — kept in visual parity with
        // SidebarBackButton. Both transitions run through
        // `widget.tint.fast` so they feel snappy without flicker.
        color: {
            // Dividers paint nothing — the Separator child below
            // provides their only visual.
            if (rowItem._isDivider)
                return Qt.rgba(0, 0, 0, 0);

            if (rowItem.isCurrent)
                return Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, 0.18);

            if (rowItem.hovered)
                return Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.hoverColor, 0.15);

            return Qt.rgba(0, 0, 0, 0);
        }
        radius: Kirigami.Units.smallSpacing

        // Section divider line — only present on divider rows.
        // Centered vertically with largeSpacing horizontal margins so
        // the line visually clears row hover backgrounds above and
        // below.
        Kirigami.Separator {
            visible: rowItem._isDivider
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Kirigami.Units.largeSpacing
            anchors.rightMargin: Kirigami.Units.largeSpacing
        }

        Behavior on color {
            enabled: delegateBackground._behaviorReady

            PhosphorMotionAnimation {
                profile: "widget.tint.fast"
            }
        }
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing
        // Divider rows hide all row content — the background's
        // Kirigami.Separator carries the entire visual. Leaving the
        // RowLayout present (and just zeroing its visibility) keeps
        // the delegate's geometry stable.
        visible: !rowItem._isDivider

        // Per-row icon. Collapsible-category headers don't have their
        // own icon (the rotating chevron at the end of the row does
        // that duty) — they show no leading icon in legacy.
        Kirigami.Icon {
            visible: !rowItem._isCollapsibleHeader && rowItem.iconSource !== ""
            source: rowItem.iconSource
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            // In compact mode the icon is the only visible row
            // content; fillWidth lets the RowLayout center it in the
            // rail. In normal mode it sits at its natural width with
            // the label fillWidth'ing after it.
            Layout.fillWidth: rowItem.compact
            Layout.alignment: rowItem.compact ? Qt.AlignHCenter : Qt.AlignLeft
            // Legacy opacity model: active rows go to 1.0, everything
            // else sits at 0.7. The `widget.hover` transition matches
            // the label below so the row's accent changes feel
            // synchronous.
            opacity: rowItem.isCurrent ? 1 : 0.7

            Behavior on opacity {
                enabled: delegateBackground._behaviorReady

                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: Kirigami.Units.shortDuration * 1.2
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            elide: Text.ElideRight
            text: rowItem.title
            // Compact mode hides the label entirely — the row reads
            // as icon-only with a tooltip. Labels are still kept in
            // the delegate so the screen reader's Accessible.name
            // still gets a string.
            visible: !rowItem.compact
            // Demi-bold on active rows AND collapsible category
            // headers — both read as "anchored" surfaces. Other
            // leaves get Normal so the active row pops.
            font.weight: (rowItem.isCurrent || rowItem._isCollapsibleHeader) ? Font.DemiBold : Font.Normal
            opacity: rowItem.isCurrent ? 1 : 0.7

            Behavior on opacity {
                enabled: delegateBackground._behaviorReady

                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: Kirigami.Units.shortDuration * 1.2
                }
            }
        }

        // Stable QtObject view of the row — bindings update per field,
        // so a downstream `modelData.iconSource` consumer only re-
        // evaluates when iconSource actually changes (vs. the prior
        // `property var modelData: ({...})` form, which allocated a
        // fresh-identity dict on every dependency tick and force-
        // invalidated every `modelData.foo` binding in the consumer's
        // trailingDelegate on hover/active flips).
        QtObject {
            id: trailingModelData

            readonly property string pageId: rowItem.pageId
            readonly property string title: rowItem.title
            readonly property string iconSource: rowItem.iconSource
            readonly property bool hasQmlSource: rowItem.hasQmlSource
            readonly property int _depth: rowItem._depth
            readonly property bool _isCollapsibleHeader: rowItem._isCollapsibleHeader
            readonly property bool _isDrillParent: rowItem._isDrillParent
            readonly property bool _isExpanded: rowItem._isExpanded
            readonly property bool _isDivider: rowItem._isDivider
        }

        Loader {
            id: trailingLoader

            // Stable object identity (the QtObject above) so the
            // consumer's `modelData.foo` bindings only re-evaluate
            // when `foo` actually changes.
            property var modelData: trailingModelData

            sourceComponent: rowItem.trailingDelegate
            // Trailing widgets (e.g. Phosphor's snapping/tiling
            // Switch + dirty badge) are suppressed in compact mode —
            // there's no horizontal room and the row reads as
            // icon-only. Consumers that need a compact-mode trailing
            // affordance can express it through the badge slot once
            // that exists.
            active: rowItem.trailingDelegate !== null && !rowItem.compact && !rowItem._isDivider
            visible: active
            Layout.alignment: Qt.AlignVCenter
        }

        // Right chevron — single icon, rotated 90° when an inline
        // category is expanded. Shown for drill-parents AND
        // collapsible category headers; not shown for leaves or in
        // compact mode (no room, and the active accent + drill change
        // carries the affordance). Reduced opacity so it reads as
        // ornament, not action.
        Kirigami.Icon {
            visible: (rowItem._isDrillParent || rowItem._isCollapsibleHeader) && !rowItem.compact
            source: "go-next"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.alignment: Qt.AlignVCenter
            opacity: 0.3
            rotation: rowItem._isCollapsibleHeader && rowItem._isExpanded ? 90 : 0

            Behavior on rotation {
                enabled: delegateBackground._behaviorReady

                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: Kirigami.Units.shortDuration * 1.5
                }
            }
        }
    }
}
