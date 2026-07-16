// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "SearchAnchorHelpers.js" as SearchAnchors
import org.phosphor.animation

/**
 * @brief Styled settings card with hover effects and optional collapse.
 *
 * Features:
 *   - Subtle border highlight on hover (accent color)
 *   - Gentle upward translate on hover (-1px)
 *   - Optional collapsible content (set collapsible: true; use
 *     initiallyCollapsed to start closed without binding `collapsed`)
 *   - Motion-profile driven transitions throughout
 *
 * Usage:
 *   SettingsCard {
 *       headerText: i18n("Appearance")
 *       collapsible: true
 *       contentItem: Kirigami.FormLayout { ... }
 *   }
 *
 * Or with a custom header (same as Kirigami.Card):
 *   SettingsCard {
 *       header: MyCustomHeader { }
 *       contentItem: ColumnLayout { ... }
 *   }
 */
Item {
    id: root

    // ── Public API ──────────────────────────────────────────────────────
    // Simple header: just provide a title string
    property string headerText: ""
    // Right-aligned hint shown after the heading (rule count, "N items", etc.).
    // Empty by default — set to opt the trailing label into the default
    // header. Ignored when a custom `header` Item is provided.
    property string headerTrailingText: ""
    // Custom header: provide any Item (overrides headerText)
    property Item header: null
    // Content (same as Kirigami.Card)
    property Item contentItem: null
    // Collapse
    property bool collapsible: false
    property bool collapsed: false
    /// Collapse state to adopt ONCE, at construction. Use this instead of
    /// binding `collapsed` whenever the initial state derives from data that
    /// can later change (e.g. "start collapsed if the user already has saved
    /// items"): a live binding on `collapsed` would slam the card shut again
    /// the moment that data changed, under the user's cursor, and the header
    /// click writes `collapsed` imperatively anyway, which would break such a
    /// binding on first use. Read only in Component.onCompleted, and applied
    /// without the collapse animation, so the card simply starts closed.
    property bool initiallyCollapsed: false
    /// True once Component.onCompleted has run. The collapse/expand animations
    /// are suppressed before that, so adopting `initiallyCollapsed` sets the
    /// card's resting state instead of animating it shut on load.
    property bool _completed: false
    /// True while the body is interactive. Drives `enabled` on the body, which is
    /// what keeps a shut card's controls out of the tab order.
    ///
    /// Set true at the START of the expand and false at the END of the collapse,
    /// so the rows are measurable for every frame the height is non-zero. The rows
    /// hide themselves when disabled (SettingsRow binds `visible: enabled`), so a
    /// value derived from the ramp would drop them out of the layout while the
    /// card was still visibly sliding.
    property bool _bodyLive: true
    /// How far open the body is: 0 shut, 1 fully open. The collapse animates
    /// THIS, and the body's height stays a live binding on it times the content's
    /// implicit height.
    ///
    /// Animating the height directly cannot work. The animation samples its
    /// target when it starts, and a layout re-measure is asynchronous, so a body
    /// whose size changed while the card was shut (SettingsRow hides itself via
    /// `visible: enabled`, so any master toggle does exactly that) would animate
    /// to a stale height and snap at the end. A live binding tracks the settle
    /// instead, whenever it lands.
    property real _expandProgress: 1
    // Header enable toggle
    property bool showToggle: false
    property bool toggleChecked: false
    /// Deep-link reveal anchor id for this card (section-level target). Empty
    /// = not addressable. See SettingsFlickable.revealAnchor.
    property string searchAnchor: ""
    /// Stable type marker so a contained SettingsRow can identify its hosting
    /// card by walking up the parent chain (used to expand the card on reveal).
    readonly property bool isSettingsCard: true
    /// Opacity applied to the card body when the master toggle is off. Kept
    /// high enough that muted content stays legible — the disabled palette
    /// already greys the text, so a low opacity on top compounds into an
    /// unreadable wash. Note SettingsRows and SettingsSeparators hide themselves
    /// when disabled (their `visible: enabled`), so a row-only body collapses
    /// away entirely; cards with non-row content (editors, custom items) keep
    /// that content visibly muted by this opacity.
    readonly property real disabledContentOpacity: 0.85

    // Per-monitor scope chip (optional). When scopeEnabled, the header shows a
    // monitor scope chip right after the title, collapsed to "All Monitors",
    // opening a spatial popover to switch outputs. The card body is expected to
    // bind its values to scopeAppSettings.scopeScreenName via a
    // PerScreenOverrideHelper, so it reflects the chosen monitor.
    property bool scopeEnabled: false
    property var scopeAppSettings: null
    property string scopeHasOverridesMethod: ""
    property string scopeClearerMethod: ""

    signal toggleClicked(bool checked)

    onCollapsedChanged: {
        // Before Component.onCompleted the card has no resting state yet, so
        // adopting `initiallyCollapsed` must not animate — it would render the
        // card open for a frame and then visibly slam it shut. The clip is
        // zeroed directly below instead.
        if (!root._completed)
            return;

        if (collapsed) {
            expandAnim.stop();
            // _bodyLive goes false at the END of the collapse (collapseAnim's
            // trailing ScriptAction), so the rows stay measurable for the whole
            // slide shut.
            collapseAnim.start();
        } else {
            collapseAnim.stop();
            // Re-enable, THEN ramp, in the same tick. QQuickLayout polishes in the
            // same frame as the animation tick, so the rows are measured before the
            // first height sample. Deferring the start with Qt.callLater would
            // queue a start that a same-tick collapse could not revoke, and the
            // card would fly fully open before sliding shut.
            root._bodyLive = true;
            expandAnim.start();
        }
    }
    // Honour `collapsed: true` (or `initiallyCollapsed: true`) at construction
    // time. The `onCollapsedChanged` handler above only fires on subsequent
    // changes — instantiating `SettingsCard { collapsible: true;
    // collapsed: true }` would otherwise leave the contentClip
    // at its full implicitHeight (the declarative initial value)
    // and the card would render expanded despite the property.
    Component.onCompleted: {
        // Adopt the one-shot initial state, then forget it. `collapsed` is left
        // an ordinary writable property, so the header click owns it from here
        // and no live binding can re-collapse the card later.
        if (root.initiallyCollapsed)
            root.collapsed = true;

        root._completed = true;
        if (collapsed) {
            root._expandProgress = 0;
            root._bodyLive = false;
        }
        if (root.searchAnchor.length > 0)
            Qt.callLater(root._registerSearchAnchor);
    }
    Component.onDestruction: {
        if (root.searchAnchor.length > 0)
            root._unregisterSearchAnchor();
    }

    // Register this card as a section-level reveal target (card == self).
    // Deferred via callLater so the subtree is attached to the page before the
    // shared helper walks the parent chain to find the hosting SettingsFlickable.
    function _registerSearchAnchor() {
        var pg = SearchAnchors.pageFor(root);
        if (pg)
            pg.registerSearchAnchor(root.searchAnchor, root, root);
    }
    function _unregisterSearchAnchor() {
        var pg = SearchAnchors.pageFor(root);
        if (pg)
            pg.unregisterSearchAnchor(root.searchAnchor);
    }
    Layout.fillWidth: true
    implicitHeight: cardBg.height
    implicitWidth: cardBg.width
    // Reparent contentItem into our content area with top padding
    onContentItemChanged: {
        if (contentItem) {
            contentItem.parent = contentColumn;
            contentItem.y = Kirigami.Units.largeSpacing;
            contentItem.width = Qt.binding(function () {
                return contentColumn.width;
            });
        }
    }
    // Handle custom header reparenting
    onHeaderChanged: {
        if (header) {
            header.parent = headerLoader;
            header.width = Qt.binding(function () {
                return headerLoader.width;
            });
            headerLoader.sourceComponent = null;
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    Rectangle {
        id: cardBg

        width: root.width
        height: headerArea.height + contentClip.height
        radius: Kirigami.Units.smallSpacing * 1.5
        // Slightly elevated from page background
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
        border.width: 1
        border.color: {
            if (!root.enabled)
                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04);

            if (hoverHandler.hovered)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4);

            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08);
        }

        // ── Header ─────────────────────────────────────────────────────
        Kirigami.ShadowedRectangle {
            id: headerArea

            width: parent.width
            // Zero height when there is no header. The loader still holds the
            // default header component (it is only nulled for a CUSTOM header),
            // so an invisible headerArea would otherwise keep its band, and both
            // cardBg.height and contentClip's top anchor count it.
            //
            // A custom header is reparented UNDER the loader, which then holds no
            // item at all: a Loader takes no height from its children, so the band
            // has to measure the custom header itself or the body draws over it.
            height: visible ? Math.max(headerLoader.height, root.header ? root.header.implicitHeight : 0) : 0
            visible: root.headerText.length > 0 || root.header !== null
            // Single uniform header fill: the whole header row is one proper
            // header color, distinct from the content rows below. Only the TOP
            // corners are rounded (to match the card); the bottom stays square so
            // the header sits flush against the content. Using per-corner radius
            // on one fill — instead of a rounded rect plus a semi-transparent
            // corner overlay — avoids doubling the alpha into a darker band along
            // the bottom of the header. Tune the alpha to taste.
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
            corners {
                topLeftRadius: cardBg.radius
                topRightRadius: cardBg.radius
                bottomLeftRadius: 0
                bottomRightRadius: 0
            }

            // The disclosure control. Hiding a collapsed body takes its rows out
            // of the focus chain, so the header itself has to be reachable and
            // activatable, or a keyboard-only user could never open the card.
            activeFocusOnTab: root.collapsible
            Accessible.role: root.collapsible ? Accessible.Button : Accessible.StaticText
            // A custom header supplies no headerText, so fall back to whatever it
            // names itself rather than announcing an unnamed button.
            Accessible.name: root.headerText.length > 0 ? root.headerText : (root.header && root.header.Accessible.name ? root.header.Accessible.name : "")
            Accessible.description: root.collapsible ? (root.collapsed ? i18n("Collapsed. Activate to expand.") : i18n("Expanded. Activate to collapse.")) : ""
            Accessible.onPressAction: root.collapsed = !root.collapsed
            Keys.onSpacePressed: if (root.collapsible)
                root.collapsed = !root.collapsed
            Keys.onReturnPressed: if (root.collapsible)
                root.collapsed = !root.collapsed
            Keys.onEnterPressed: if (root.collapsible)
                root.collapsed = !root.collapsed

            // Click to collapse/expand
            MouseArea {
                z: -1
                anchors.fill: parent
                enabled: root.collapsible
                cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.collapsed = !root.collapsed
            }

            // Keyboard focus ring. The header is a focusable control now, so it
            // needs to show where focus is.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: cardBg.radius
                visible: headerArea.activeFocus
                border.width: 1
                border.color: Kirigami.Theme.highlightColor
            }

            // Default header (Heading from headerText)
            Loader {
                id: headerLoader

                width: parent.width
                sourceComponent: root.header !== null ? null : defaultHeaderComponent
            }

            Component {
                id: defaultHeaderComponent

                RowLayout {
                    width: parent ? parent.width : 0
                    spacing: 0

                    Kirigami.Heading {
                        text: root.headerText
                        level: 3
                        padding: Kirigami.Units.smallSpacing
                        // Align the title's left edge with the card's content
                        // rows (SettingsRow insets by largeSpacing) and the
                        // trailing chevron (also largeSpacing), so the header is
                        // uniformly inset rather than hugging the left while the
                        // right controls sit further in.
                        leftPadding: Kirigami.Units.largeSpacing
                    }

                    // Per-monitor scope chip, title-adjacent. Kept clear of the
                    // collapse chevron / enable toggle on the right edge. Loaded
                    // only on scoped cards so its appSettings bindings never
                    // evaluate against null on the (many) non-scoped cards.
                    Loader {
                        active: root.scopeEnabled && root.scopeAppSettings !== null
                        visible: active
                        Layout.leftMargin: active ? Kirigami.Units.smallSpacing : 0
                        Layout.alignment: Qt.AlignVCenter
                        sourceComponent: Component {
                            MonitorScopeChip {
                                appSettings: root.scopeAppSettings
                                hasOverridesMethod: root.scopeHasOverridesMethod
                                clearerMethod: root.scopeClearerMethod
                            }
                        }
                    }

                    // Spacer — pushes the trailing controls to the right edge
                    // (replaces the Heading's former Layout.fillWidth so the
                    // scope chip can sit next to the title instead).
                    Item {
                        Layout.fillWidth: true
                    }

                    // Trailing hint label — right-aligned next to the heading.
                    // Used by the Rules sections for the per-section
                    // rule count (and similar passive metadata callers may add).
                    Label {
                        visible: root.headerTrailingText.length > 0
                        text: root.headerTrailingText
                        opacity: 0.6
                        font.italic: true
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Header enable toggle. When a collapse chevron follows, the
                    // margin is just the inter-control gap (smallSpacing); when
                    // the toggle is the trailing control, it takes the full
                    // largeSpacing edge inset so it lines up with the content
                    // rows and chevron-terminated cards.
                    SettingsSwitch {
                        visible: root.showToggle
                        checked: root.toggleChecked
                        accessibleName: root.headerText
                        Layout.rightMargin: root.collapsible ? Kirigami.Units.smallSpacing : Kirigami.Units.largeSpacing
                        onToggled: function (newValue) {
                            root.toggleClicked(newValue);
                        }
                    }

                    // Collapse chevron
                    Kirigami.Icon {
                        visible: root.collapsible
                        source: "arrow-down"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        rotation: root.collapsed ? -90 : 0
                        opacity: 0.5

                        Behavior on rotation {
                            PhosphorMotionAnimation {
                                profile: "widget.hover"
                                durationOverride: Kirigami.Units.shortDuration
                            }
                        }
                    }
                }
            }
        }

        // ── Content (clipped for collapse animation) ───────────────────
        Item {
            id: contentClip

            anchors.top: headerArea.bottom
            width: parent.width
            // Live binding, never overwritten by an animation. See _expandProgress.
            height: contentColumn.implicitHeight * root._expandProgress
            clip: true
            // The ramp is IN the binding rather than animated on top of it: a
            // property animation does not drop the binding, so an imperative fade
            // was re-evaluated out from under itself whenever the master toggle
            // changed while the card was shut.
            opacity: (root.showToggle && !root.toggleChecked ? root.disabledContentOpacity : 1) * root._expandProgress
            // Clipping a shut card to nothing still leaves its controls in the
            // tab order, so tabbing through the page walks fields nobody can see.
            // Disabling the body takes them out of the focus chain, and the header
            // carries the keyboard affordance in their place.
            //
            // Disabling also hides the rows, because SettingsRow binds
            // `visible: enabled`, and a hidden row stops contributing to the
            // layout's implicit height. That re-measure is asynchronous, which is
            // why _bodyLive is set on the way OPEN before the ramp starts and
            // dropped on the way SHUT only once the ramp has finished.
            enabled: (root.showToggle ? root.toggleChecked : true) && root._bodyLive

            Item {
                id: contentColumn

                width: parent.width
                implicitHeight: root.contentItem ? root.contentItem.implicitHeight + Kirigami.Units.largeSpacing * 2 : 0
            }

            // Only the progress is animated. Height and opacity are both bound to
            // it, so they move together by construction and a reversal mid-flight
            // cannot leave one of them stranded.
            SequentialAnimation {
                id: collapseAnim

                PhosphorMotionAnimation {
                    target: root
                    properties: "_expandProgress"
                    to: 0
                    profile: "widget.accordionCollapse"
                    durationOverride: Kirigami.Units.shortDuration
                }

                ScriptAction {
                    // Height is already 0, so the rows collapsing out of the
                    // layout now costs nothing visible.
                    script: root._bodyLive = false
                }
            }

            SequentialAnimation {
                id: expandAnim

                PhosphorMotionAnimation {
                    target: root
                    properties: "_expandProgress"
                    to: 1
                    profile: "widget.accordionExpand"
                    durationOverride: Kirigami.Units.shortDuration
                }
            }

            // Smooths the master-toggle dim only. Disabled while the card is
            // opening or closing: opacity tracks _expandProgress there, so an
            // easing on every frame's step would drag the fade behind the slide.
            Behavior on opacity {
                enabled: !collapseAnim.running && !expandAnim.running

                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: Kirigami.Units.shortDuration
                }
            }
        }

        Behavior on border.color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: Kirigami.Units.shortDuration
            }
        }
    }

    // Subtle lift on hover: 1 device-independent px. The renderer scales
    // it to physical pixels itself, so it never collapses to a sub-pixel
    // offset on high-DPI displays.
    transform: Translate {
        y: hoverHandler.hovered && root.enabled ? -1 : 0

        Behavior on y {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: Kirigami.Units.shortDuration
            }
        }
    }
}
