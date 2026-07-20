// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "SearchAnchorHelpers.js" as SearchAnchors

/**
 * A SettingsFlickable that renders a list of AnimationEventCards with
 * viewport-gated lazy construction: only cards whose slot intersects the
 * visible viewport (plus a build-ahead buffer) are instantiated. Each
 * AnimationEventCard embeds a heavy AnimationProfileEditor (curve Canvas,
 * shader picker, sliders, dialogs), so building a whole page of them
 * eagerly was the dominant first-visit cost on the animation-event pages.
 *
 * The root stays a SettingsFlickable so the Kirigami.WheelHandler (Plasma
 * wheel-scroll speed, bug 385836) is preserved untouched — no ListView,
 * no re-derived wheel math. Cards are recycle-safe: AnimationEventCard
 * holds no persistent state of its own (its Component.onCompleted re-reads
 * from settingsController.animationsPage), so a freshly-built card always
 * shows the committed tree state. Each Loader LATCHES active once it
 * enters the viewport and never unloads, so no transient UI (open dialogs,
 * focus, master-toggle collapse state) is lost on scroll.
 *
 * Consumers provide `eventModel` (and an Accessible.name):
 *
 *   AnimationEventCardList {
 *       Accessible.name: i18n("Window animation events")
 *       headerText: i18n("…optional orientation banner…")
 *       eventModel: [ { eventPath, eventLabel, isParentNode }, ... ]
 *   }
 */
SettingsFlickable {
    id: page

    /// Ordered list of `{ eventPath: string, eventLabel: string,
    /// isParentNode: bool }` — one AnimationEventCard per entry, in order.
    property var eventModel: []
    /// Optional orientation banner rendered above the cards. Empty = none.
    /// Pages with an "All" cascade parent use it for a short inheritance
    /// explainer, mirroring the decoration surface pages.
    property string headerText: ""
    /// Optional Component instantiated above the event cards (below the
    /// banner) — lets a consumer lead with page-level controls (e.g. the
    /// simple-mode enable + speed card) while the event cards themselves
    /// stay the shared per-event surface.
    property Component headerComponent: null
    /// Forwarded to every card: hide the timing-mode machinery, keeping
    /// duration + shader + parameters (the simple-mode card shape).
    property bool simpleTiming: false
    /// Optional Component instantiated BELOW the event cards — the footer
    /// counterpart of headerComponent (e.g. the simple-mode window
    /// filtering card).
    property Component footerComponent: null

    // Build-ahead margin above/below the visible viewport so a card is
    // built slightly before it scrolls into view — keeps a fast flick from
    // landing on an un-built placeholder.
    readonly property real buildBuffer: Kirigami.Units.gridUnit * 20
    // Placeholder height a not-yet-built card reserves in the layout so
    // contentHeight (and scroll position) stays stable until the real card
    // materialises. Sized to a collapsed card (header + inheritance banner
    // + "Current:" line).
    readonly property real placeholderHeight: Kirigami.Units.gridUnit * 7

    contentHeight: col.implicitHeight
    clip: true

    ColumnLayout {
        id: col

        width: page.width
        spacing: Kirigami.Units.smallSpacing

        // Optional page-level orientation banner, mirroring the decoration
        // surface pages. Pages with an "All" cascade parent set `headerText`
        // to a short inheritance explainer; pages without one leave it empty
        // and rely on the per-card banners alone.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            type: Kirigami.MessageType.Information
            visible: page.headerText.length > 0
            text: page.headerText
        }

        Loader {
            Layout.fillWidth: true
            active: page.headerComponent !== null
            visible: active
            sourceComponent: page.headerComponent
        }

        Repeater {
            model: page.eventModel

            // One latching, viewport-gated Loader per event. The Loader is
            // the layout participant — it carries the placeholder height
            // until built, then tracks the card's real height.
            Loader {
                id: cardLoader

                required property var modelData

                // Latch: once the card has entered the viewport it stays
                // built. `_everInView` flips true and never back.
                property bool _everInView: false

                // Deep-link reveal: register this delegate under its event's
                // path so global search can scroll the page to it. The OUTER
                // Loader always exists (with reserved placeholder height) even
                // before its card builds, so it is the stable reveal target —
                // revealAnchor scrolls to + pulses it, and the card builds
                // lazily as it enters view (card arg is null → scroll+pulse
                // only, no expand). Registration is purely additive: it never
                // touches the viewport/latching logic above.
                readonly property string searchAnchor: cardLoader.modelData.eventPath || ""
                // Only leaf events are addressable. Parent rows ("All X
                // Events") carry isParentNode: true and are page-level group
                // headers, not a single animation event.
                readonly property bool searchable: cardLoader.modelData.isParentNode !== true && cardLoader.searchAnchor.length > 0

                Layout.fillWidth: true
                // Reserve the real card height once built (a Loader's
                // implicitHeight reflects its loaded item's implicitHeight),
                // placeholder otherwise. Reading `cardLoader.implicitHeight`
                // rather than `item.implicitHeight` keeps the access typed —
                // `Loader.item` is an untyped QtObject. `> 0` covers the
                // async window where active is true but item not yet built.
                Layout.preferredHeight: cardLoader.active && cardLoader.implicitHeight > 0 ? cardLoader.implicitHeight : page.placeholderHeight
                active: _everInView
                asynchronous: true
                // Deliberately NOT `visible: active`. QtQuick.Layouts
                // excludes invisible items from layout entirely, so an
                // inactive (not-yet-built) Loader marked invisible would
                // reserve ZERO height instead of `placeholderHeight`. Every
                // card's `y` would then collapse toward 0, the viewport test
                // below would see them all on-screen, and the whole page
                // would build at once — defeating the virtualization. An
                // inactive Loader has no `item`, so it paints nothing while
                // visible; leaving it visible keeps it a layout participant
                // that carries the placeholder height.

                // Imperative one-shot latch. A declarative `Binding { when:
                // <reads y> }` loops: building the card changes
                // Layout.preferredHeight, which makes the ColumnLayout
                // re-assign every child's `y` in the same pass, re-firing
                // the `when` that reads `y` — Qt flags that as a binding
                // loop. Checking imperatively on the relevant change signals
                // and returning once latched breaks the cycle. `y` is read
                // live but never bound; placeholderHeight (constant)
                // estimates the slot's bottom so the build height never
                // feeds back in. A ColumnLayout derives a child's `y` from
                // the items ABOVE it, never from this Loader's own
                // height/active, so reading `y` here is safe.
                function _checkInView() {
                    if (cardLoader._everInView)
                        return;
                    // Fast page-switching can destroy this delegate while a
                    // queued Qt.callLater / onYChanged is still pending; the
                    // dying context resolves `page` to undefined. Bail instead
                    // of throwing a TypeError reading page.placeholderHeight.
                    if (!page)
                        return;
                    const top = cardLoader.y;
                    if ((top + page.placeholderHeight) >= (page.contentY - page.buildBuffer) && top <= (page.contentY + page.height + page.buildBuffer))
                        cardLoader._everInView = true;
                }
                onYChanged: cardLoader._checkInView()
                // Deferred one tick: a ColumnLayout assigns child `y` on a
                // polish pass AFTER Component.onCompleted runs, so a check
                // here would read the pre-layout `y` (0 for every card) and
                // latch the whole page in-view. Qt.callLater runs the first
                // check once layout has positioned this Loader, so the
                // viewport test sees the real slot position. Later re-checks
                // come from onYChanged / the page Connections below.
                Component.onCompleted: {
                    Qt.callLater(cardLoader._checkInView);
                    // Defer registration like SettingsRow does: the parent
                    // chain up to the page only settles after construction.
                    // Register the Loader itself (card arg null) so reveal
                    // scrolls + pulses; the card builds as it scrolls in.
                    Qt.callLater(function () {
                        if (!cardLoader.searchable)
                            return;

                        var pg = SearchAnchors.pageFor(cardLoader);
                        if (pg)
                            pg.registerSearchAnchor(cardLoader.searchAnchor, cardLoader, null);
                    });
                }
                Component.onDestruction: {
                    if (!cardLoader.searchable)
                        return;

                    var pg = SearchAnchors.pageFor(cardLoader);
                    if (pg)
                        pg.unregisterSearchAnchor(cardLoader.searchAnchor);
                }
                // Once the card is built, re-register the anchor WITH its
                // SettingsCard so revealAnchor can expand a collapsed card
                // before scrolling (the initial registration passes a null
                // card because the card doesn't exist until it scrolls in).
                onLoaded: {
                    if (!cardLoader.searchable)
                        return;

                    var pg = SearchAnchors.pageFor(cardLoader);
                    var built = cardLoader.item as AnimationEventCard;
                    if (pg && built)
                        pg.registerSearchAnchor(cardLoader.searchAnchor, cardLoader, built.settingsCard);
                }

                Connections {
                    target: page
                    function onContentYChanged() {
                        cardLoader._checkInView();
                    }
                    function onHeightChanged() {
                        cardLoader._checkInView();
                    }
                }

                sourceComponent: AnimationEventCard {
                    // Width is driven by the Loader (Layout.fillWidth →
                    // Loader.width → item.width); the card's implicitHeight
                    // drives Layout.preferredHeight above.
                    eventPath: cardLoader.modelData.eventPath
                    eventLabel: cardLoader.modelData.eventLabel
                    isParentNode: cardLoader.modelData.isParentNode === true
                    collapsible: true
                    simpleTiming: page.simpleTiming
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            active: page.footerComponent !== null
            visible: active
            sourceComponent: page.footerComponent
        }
    }
}
