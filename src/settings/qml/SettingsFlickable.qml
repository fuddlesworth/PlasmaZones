// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
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
 *   - handles Shift = horizontal, Ctrl = page-jump modifiers natively;
 *   - exposes `keyNavigationEnabled` for arrow / Page-Up / Page-Down
 *     parity with the wheel.
 *
 * Drop-in replacement for `Flickable` in settings pages — same
 * properties, same `contentItem` semantics. Kirigami's WheelHandler
 * is the upstream answer to KDE bug 385836 ("Kirigami scrollviews
 * ignore Mouse wheel scroll speed") and the implementation that
 * Kirigami's own ScrollView used to embed before `Kirigami.Scrollable-
 * Page` migrated to `QQC2.ScrollView` (which silently dropped the
 * handler).
 */
Flickable {
    id: settingsFlickable

    Kirigami.WheelHandler {
        target: settingsFlickable
        // Leave `filterMouseEvents` at its default of `false` — that
        // flag is for nested-Flickable scenarios where you want the
        // inner Flickable to NOT respond to mouse press/release.
        // Setting it true here would kill Flickable's native
        // drag-to-flick path (touch drag, middle-button drag, etc.)
        // because the handler would swallow the press / release
        // events. The wheel path runs through this handler regardless
        // of the flag's value; only the press/release path is gated.
        // `keyNavigationEnabled` mirrors arrow / Page-Up / Page-Down
        // onto the Flickable so keyboard scroll matches mouse step.
        keyNavigationEnabled: true
    }

}
