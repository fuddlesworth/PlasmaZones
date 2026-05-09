// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Settings-page root Flickable with native-feeling wheel scroll.
 *
 * Default Qt 6 Flickable wheel handling translates one wheel notch
 * (`event.angleDelta.y == 120`, the libinput-driven default on
 * Wayland) into a brief flick velocity that decelerates over only
 * ~15 px on Plasma's "1 line per click" wheel-line setting. Native
 * KDE apps move ~80–120 px per notch — that mismatch surfaced as
 * "scrolling in settings is very slow" in discussion #405.
 *
 * Override the default by attaching a `WheelHandler` that translates
 * each notch directly into a `contentY` adjustment scaled by
 * `Kirigami.Units.gridUnit`. The handler runs at the Flickable's
 * level, so children that consume wheel events themselves (Sliders
 * for value adjustment, ComboBoxes for popup navigation) still
 * receive the event first via Qt's pointer-event hit ordering — the
 * Flickable-level handler picks up only when no child has accepted.
 *
 * Drop-in replacement for `Flickable` in settings pages — same
 * properties, same `contentItem` semantics. Pages that need a
 * non-page-level Flickable (nested scroll regions inside a card,
 * for instance) can keep the bare `Flickable`; only the root
 * page-level scroller needs this wrapper.
 */
Flickable {
    id: settingsFlickable

    /// Notches (`event.angleDelta.y / 120`) are multiplied by this
    /// many `Kirigami.Units.gridUnit`s of `contentY` travel. Default
    /// 4 yields ~4 × ~18px = ~72 px per notch on default Plasma
    /// scaling, which matches the feel of native KDE settings.
    property real wheelStepGridUnits: 4

    WheelHandler {
        id: wheelAccelerator

        // Keep `target` empty so the handler doesn't try to apply a
        // transform to its parent — we do the work explicitly in
        // `onWheel`. The handler still attaches to the parent Item
        // (`settingsFlickable`) for hit testing.
        target: null
        // Mouse-only: trackpad two-finger scroll already produces
        // pixel-precise `pixelDelta`s that Flickable handles smoothly
        // and does NOT want this multiplier applied to. Restricting
        // to discrete-mouse devices keeps trackpad scroll feeling
        // native AND fixes the wheel-notch coarseness in one knob.
        acceptedDevices: PointerDevice.Mouse
        onWheel: function(event) {
            if (!settingsFlickable.interactive) {
                event.accepted = false;
                return ;
            }
            var notches = event.angleDelta.y / 120;
            if (notches === 0) {
                event.accepted = false;
                return ;
            }
            var stepPx = notches * settingsFlickable.wheelStepGridUnits * Kirigami.Units.gridUnit;
            // Clamp to the flickable's scrollable range; `contentHeight`
            // can be smaller than `height` when the page hasn't filled
            // its viewport, in which case `maxY` collapses to 0 and
            // any scroll input is a no-op.
            var maxY = Math.max(0, settingsFlickable.contentHeight - settingsFlickable.height);
            var newY = Math.max(0, Math.min(maxY, settingsFlickable.contentY - stepPx));
            settingsFlickable.contentY = newY;
            event.accepted = true;
        }
    }

}
