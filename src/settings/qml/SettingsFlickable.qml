// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import "SearchAnchorHelpers.js" as SearchAnchors

/**
 * @brief Settings-page root Flickable with Plasma-aware wheel scroll.
 *
 * Default Qt 6 Flickable wheel handling translates one wheel notch
 * (`event.angleDelta.y == 120`, the libinput-driven default on
 * Wayland) into a flick velocity that decelerates over only ~15 px
 * under Plasma's "1 line per click" wheel-line setting. The user
 * reported this as "scrolling in settings is very slow" — every
 * notch barely moves the page while native KDE apps move ~80–120 px
 * per notch (discussion #405).
 *
 * Kirigami already ships the canonical fix: `Kirigami.WheelHandler`.
 * Installed on a Flickable, it
 *
 *   - reads `verticalStepSize` as `20 * Qt.styleHints.wheelScrollLines`
 *     by default — so Plasma's "Mouse" KCM's "lines per click" knob
 *     end-to-end controls the page step (KdePlatformTheme reads the
 *     `kdeglobals[KDE].WheelScrollLines` config; Qt's `QStyleHints`
 *     surfaces it; Kirigami's WheelHandler multiplies it through);
 *   - smooth-animates the scroll via `QPropertyAnimation` + `OutCubic`
 *     instead of jumping contentY — cosmetic but matches every other
 *     KDE app's feel;
 *   - distinguishes high-res `pixelDelta` events (Logitech MX-class
 *     hi-res wheels, trackpads) from notched `angleDelta` events,
 *     applying the system multiplier only to notches;
 *   - handles Shift = horizontal, Ctrl = page-jump modifiers natively.
 *
 * Drop-in replacement for `Flickable` in settings pages — same
 * properties, same `contentItem` semantics. Kirigami's WheelHandler
 * is the upstream answer to KDE bug 385836 ("Kirigami scrollviews
 * ignore Mouse wheel scroll speed") and the implementation that
 * Kirigami's own ScrollView used to embed before `Kirigami.Scrollable-
 * Page` migrated to `QQC2.ScrollView` (which silently dropped the
 * handler).
 *
 * ## Consumer responsibilities
 *
 * `SettingsFlickable` defaults `flickableDirection` to vertical-only
 * and pins `boundsBehavior` to `StopAtBounds` so wheel events arriving
 * mid-decay don't fight the WheelHandler animation. Otherwise it
 * inherits Flickable's whole property surface unchanged. Each
 * consumer is responsible for:
 *
 *   - Binding `contentHeight` (and `contentWidth` if scrolling
 *     horizontally — not the typical settings-page case). Without it
 *     `WheelHandler` sees a zero scrollable range and silently
 *     refuses to move.
 *   - Avoiding nested Flickable / ListView / TextArea scroll surfaces
 *     unless they are intentionally height-clamped to their content
 *     (the `LayoutsPage.qml` pattern). Nested independent scrollers
 *     compete with this handler for wheel events and wheel-over-the-
 *     inner produces double-scroll.
 *
 * Drag-to-flick (touch drag, middle-button drag) keeps Flickable's
 * native momentum + decay path because `filterMouseEvents` defaults
 * to `false` — only wheel events route through the handler.
 */
Flickable {
    id: settingsFlickable

    /// Vertical-only by default — matches every consumer in the
    /// settings tree. Pages that need horizontal scrolling can set
    /// `flickableDirection: Flickable.AutoFlickDirection` explicitly.
    flickableDirection: Flickable.VerticalFlick
    /// `StopAtBounds` instead of the default `DragAndOvershootBounds`
    /// so wheel events arriving while a previous wheel scroll is
    /// still animating don't compound with overshoot bounce, which
    /// fights the WheelHandler's `QPropertyAnimation` decay and
    /// produces visibly jittery wheel-during-decay frames.
    boundsBehavior: Flickable.StopAtBounds

    /// Top breathing room so the first card's hover lift (-1px) and its
    /// highlighted top border aren't clipped against the page's top edge
    /// (every settings page sets `clip: true`). Without it the topmost card's
    /// top border never fully shows on hover.
    topMargin: Kirigami.Units.smallSpacing

    // ── Deep-link reveal (global search / `--setting`) ───────────────────
    // Rows and cards self-register here by anchor id (see SettingsRow /
    // SettingsCard `searchAnchor`). PageHost duck-calls `revealAnchor()` once
    // this page is built + current for a pending deep link: it expands the
    // containing card if collapsed, smooth-scrolls the target into view, then
    // pulses a highlight. Registration via the page is generic — any page
    // rooted on SettingsFlickable inherits the whole reveal contract.
    property var _searchAnchors: ({})
    // Item awaiting reveal once its containing card finishes expanding.
    property var _revealPendingItem: null
    // Cards reveal speculatively expanded, so a reveal that turns out invisible
    // anyway can put them back instead of leaving them open.
    property var _revealPendingCards: []
    // A revealAnchor() for an id that isn't registered yet — children register
    // via Qt.callLater after the page builds, so a deep link can arrive first.
    // Retained here and retried from registerSearchAnchor (consume-once).
    property string _pendingRevealAnchor: ""

    function registerSearchAnchor(anchorId, item, card) {
        if (!anchorId || anchorId.length === 0 || !item)
            return;

        settingsFlickable._searchAnchors[anchorId] = {
            "item": item,
            "card": card || null
        };

        // Satisfy a deep link that arrived before this anchor registered.
        if (settingsFlickable._pendingRevealAnchor === anchorId) {
            settingsFlickable._pendingRevealAnchor = "";
            pendingRevealExpiry.stop();
            settingsFlickable.revealAnchor(anchorId);
        }
    }

    function unregisterSearchAnchor(anchorId) {
        if (anchorId && settingsFlickable._searchAnchors[anchorId])
            delete settingsFlickable._searchAnchors[anchorId];
    }

    function revealAnchor(anchorId) {
        // Cancel any in-flight expand-then-settle FIRST — before the registry
        // lookup, which can early-return for a not-yet-registered id without
        // touching the timer. A stale settle would otherwise fire against the
        // PREVIOUS target and pulse the wrong row moments before the new
        // anchor registers and scrolls somewhere else.
        settingsFlickable._cancelPendingReveal();

        var entry = settingsFlickable._searchAnchors[anchorId];
        if (!entry || !entry.item) {
            // Not registered yet — retain and retry once it registers, so the
            // reveal isn't lost to the page-build / deferred-registration race.
            settingsFlickable._pendingRevealAnchor = anchorId;
            // Expire it: without this a bogus or wrong-page anchor stays armed
            // for the page's lifetime, and a Repeater that later rebuilds and
            // registers that id fires an unexplained scroll and highlight long
            // after the search that asked for it.
            pendingRevealExpiry.restart();
            return;
        }

        // This reveal wins over any anchor still awaiting registration.
        settingsFlickable._pendingRevealAnchor = "";
        pendingRevealExpiry.stop();

        var card = entry.card;

        // Collapsed-card check FIRST, before any visibility bail. A row inside
        // a collapsed SettingsCard is invisible by construction — the card
        // drops `contentClip.enabled` when its body goes dead, `enabled`
        // inherits down the item chain, and SettingsRow leads its `visible:`
        // binding with `enabled`. So testing visibility first swallowed every
        // collapsed-card target and scrolled to the top of the page instead,
        // making the expand branch below unreachable for them. That is the
        // common case (any card the user shut, plus cards built
        // initiallyCollapsed), not the tier disagreement the guard was written
        // for. Effective visibility is only meaningful once the card is open,
        // so the bail moves after the expand.
        // Every collapsed card on the chain, not just the nearest — a row in a
        // card nested inside another collapsed card stays invisible if only the
        // inner one opens.
        var collapsedChain = SearchAnchors.cardChainFor(entry.item).filter(function (c) {
            return c.collapsible === true && c.collapsed === true;
        });
        if (collapsedChain.length > 0) {
            // Expand, then settle: drive the post-expand scroll off a one-shot
            // timer (≈ the expand duration) rather than an expand-finished signal —
            // a signal would leak / never fire if the card is re-collapsed
            // mid-animation. _scrollToReveal still defers a frame for final layout.
            settingsFlickable._revealPendingItem = entry.item;
            // Remember everything we opened, so a reveal that turns out
            // invisible anyway can put them back rather than leaving cards the
            // user deliberately shut yanked open with the page scrolled
            // somewhere unrelated.
            settingsFlickable._revealPendingCards = collapsedChain;
            for (var ci = 0; ci < collapsedChain.length; ++ci)
                collapsedChain[ci].collapsed = false;
            revealSettleTimer.restart();
        } else if (!entry.item.visible) {
            // Genuinely invisible with no collapsed card to blame: the row is
            // hidden by its own condition, most likely an advanced-only row
            // reached in simple mode. It has no usable geometry, so scrolling
            // to it would pulse the highlight over a zero-height strip. The
            // search index filters these by tier already
            // (SearchEntry.advancedOnly), so reaching here means the index and
            // the row disagreed — fall back to the top of the page, which is at
            // least a real destination. Tests effective `visible` rather than
            // the advancedOnly flag so every hiding condition is covered.
            revealScrollAnim.to = 0;
            revealScrollAnim.restart();
        } else {
            settingsFlickable._scrollToReveal(entry.item);
        }
    }

    /// Drops any pending expand-then-settle. Called at the top of every
    /// revealAnchor so a superseded reveal can never fire against the old
    /// target, whichever branch the new one takes.
    function _cancelPendingReveal() {
        settingsFlickable._revealPendingItem = null;
        settingsFlickable._revealPendingCards = [];
        revealSettleTimer.stop();
    }

    function _scrollToReveal(item) {
        // Defer one tick so a freshly-built / just-expanded layout settles
        // before we measure the target's mapped position.
        Qt.callLater(function () {
            if (!item)
                return;

            var pt = item.mapToItem(settingsFlickable.contentItem, 0, 0);
            var headroom = Kirigami.Units.gridUnit;
            var maxY = Math.max(0, settingsFlickable.contentHeight - settingsFlickable.height);
            revealScrollAnim.to = Math.max(0, Math.min(pt.y - headroom, maxY));
            revealScrollAnim.restart();

            revealHighlight.x = pt.x;
            revealHighlight.y = pt.y;
            revealHighlight.width = item.width;
            revealHighlight.height = item.height;
            revealPulse.restart();
        });
    }

    // Drives the post-expand reveal scroll (see revealAnchor). Interval ≈ the
    // SettingsCard expand animation so geometry is final; _scrollToReveal also
    // defers a frame via Qt.callLater for robustness against reduced-motion.
    Timer {
        id: revealSettleTimer

        interval: Kirigami.Units.shortDuration + Kirigami.Units.veryShortDuration
        onTriggered: {
            var pending = settingsFlickable._revealPendingItem;
            var pendingCards = settingsFlickable._revealPendingCards;
            settingsFlickable._revealPendingItem = null;
            settingsFlickable._revealPendingCards = [];
            if (!pending)
                return;
            // Effective visibility is only final now that the card has finished
            // expanding — revealAnchor deliberately defers this check to here
            // for the collapsed-card path. A row still hidden after the expand
            // is hidden by its own condition (an advanced-only row in simple
            // mode), so fall back to the top of the page rather than pulsing
            // the highlight over a zero-height strip.
            if (!pending.visible) {
                // Still hidden after the expand — the row is gated by its own
                // condition, not by the card. Put the card back: we opened it
                // speculatively, it bought nothing, and leaving it open is a
                // visible side effect of a reveal that failed.
                for (var ri = 0; ri < pendingCards.length; ++ri)
                    pendingCards[ri].collapsed = true;
                revealScrollAnim.to = 0;
                revealScrollAnim.restart();
                return;
            }
            settingsFlickable._scrollToReveal(pending);
        }
    }

    // Drops a deep link whose anchor never registered. The window only has to
    // cover the deferred-registration race it exists for (children register via
    // Qt.callLater after the page builds), not the page's lifetime.
    Timer {
        id: pendingRevealExpiry

        interval: Kirigami.Units.longDuration * 3
        onTriggered: settingsFlickable._pendingRevealAnchor = ""
    }

    NumberAnimation {
        id: revealScrollAnim

        target: settingsFlickable
        properties: "contentY"
        duration: Kirigami.Units.longDuration
        easing.type: Easing.OutCubic
    }

    // Declared as a child → lives in the Flickable's contentItem, so its
    // content-space x/y track the revealed item while the page scrolls.
    Rectangle {
        id: revealHighlight

        z: 100
        radius: Kirigami.Units.smallSpacing
        color: "transparent"
        border.width: 2
        border.color: Kirigami.Theme.highlightColor
        opacity: 0
        visible: opacity > 0

        SequentialAnimation {
            id: revealPulse

            NumberAnimation {
                target: revealHighlight
                properties: "opacity"
                from: 0
                to: 1
                duration: Kirigami.Units.shortDuration
            }

            PauseAnimation {
                duration: Kirigami.Units.veryLongDuration
            }

            NumberAnimation {
                target: revealHighlight
                properties: "opacity"
                to: 0
                duration: Kirigami.Units.longDuration
            }
        }
    }

    Kirigami.WheelHandler {
        // Leave `filterMouseEvents` at its default of `false` — that
        // flag is for nested-Flickable scenarios where you want the
        // inner Flickable to NOT respond to mouse press/release.
        // Setting it true here would kill Flickable's native
        // drag-to-flick path (touch drag, middle-button drag, etc.)
        // because the handler would swallow the press / release
        // events. The wheel path runs through this handler regardless
        // of the flag's value; only the press/release path is gated.
        // `keyNavigationEnabled` is intentionally left at its default
        // (`false`). Settings pages focus form controls (sliders,
        // combos), not the Flickable itself, so Page-Up / Page-Down
        // on the handler would only fire when nothing inside the page
        // had focus — confusing in practice. Pages that genuinely
        // want keyboard scroll should set their own `Keys` handlers
        // on the Flickable with explicit focus management.

        // `target: settingsFlickable` is robust to reparenting; using
        // `parent` would be too if the handler stays a direct child,
        // but is fragile under any future Loader / Component wrapping.
        target: settingsFlickable
    }
}
