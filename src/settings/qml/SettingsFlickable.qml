// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

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

    function registerSearchAnchor(anchorId, item, card) {
        if (!anchorId || anchorId.length === 0 || !item)
            return;

        settingsFlickable._searchAnchors[anchorId] = {
            "item": item,
            "card": card || null
        };
    }

    function unregisterSearchAnchor(anchorId) {
        if (anchorId && settingsFlickable._searchAnchors[anchorId])
            delete settingsFlickable._searchAnchors[anchorId];
    }

    function revealAnchor(anchorId) {
        var entry = settingsFlickable._searchAnchors[anchorId];
        if (!entry || !entry.item)
            return;

        var card = entry.card;
        if (card && card.collapsible === true && card.collapsed === true) {
            // Expand, then settle: connecting to expandFinished would leak (and
            // never fire) if the card is re-collapsed mid-animation, so drive the
            // post-expand scroll off a one-shot timer (≈ the expand duration).
            // _scrollToReveal still defers a frame for final layout.
            settingsFlickable._revealPendingItem = entry.item;
            card.collapsed = false;
            revealSettleTimer.restart();
        } else {
            settingsFlickable._scrollToReveal(entry.item);
        }
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
            if (settingsFlickable._revealPendingItem) {
                settingsFlickable._scrollToReveal(settingsFlickable._revealPendingItem);
                settingsFlickable._revealPendingItem = null;
            }
        }
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
        border.width: Math.max(1, Math.round(Screen.devicePixelRatio * 2))
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
