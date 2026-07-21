// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations (simple mode) — the pared-down animation surface.
 *
 * Shown ONLY in simple mode (registered SimpleOnly); advanced mode replaces
 * it with the full per-event Animations tree. The page is the SAME
 * viewport-virtualized AnimationEventCardList the advanced pages use — real
 * AnimationEventCards, not forked simplified controls — but each card
 * covers a whole group of analogous events:
 *
 *   - Window opened & closed: the open card, declaring the close leaf as a
 *     write MIRROR (see AnimationEventCard.mirrorPaths), so every edit the
 *     card makes lands on both leaves. The card READS the open leaf, so a
 *     close-leaf value diverged in advanced mode is not shown here and is
 *     overwritten by the next edit on this card — and only by that.
 *   - Window minimized: the single minimize leaf (the restore direction is
 *     the same effect reversed; there is no separate leaf).
 *   - Window movement: the `window.movement` CASCADE PARENT — maximize,
 *     snap in/out, and layout switch all inherit from it natively, so one
 *     card controls the whole geometry class with no mirroring. A child
 *     override set in advanced mode shadows it; the card's built-in
 *     shadowing-children banner surfaces that.
 *   - Window moved: the drag-time `move` class. Its shader class is
 *     opt-in and never inherited, so it cannot ride the movement parent
 *     and keeps its own card.
 *   - Desktop switched: the desktop.switch leaf (peek stays advanced).
 *
 * The header block leads with the Global animation defaults card (enable
 * toggle + the same curve/timing/duration editor the advanced General page
 * uses) followed by the animation window-filter card; the grouped event
 * cards come after. Everything else (focus, OSDs, overlays, panels,
 * widgets, editor, the library) is advanced-mode depth.
 */
AnimationEventCardList {
    id: simplePage

    readonly property QtObject globalSettings: settingsController.settings

    Accessible.name: i18n("Animation essentials")
    simpleTiming: true
    headerText: i18n("Each card covers a whole group of events. Switch to Advanced in the sidebar for full per-event control and the shader library.")
    // The shared GlobalTimingDefaultsCard (curve summary + Customize,
    // Easing/Spring, Duration), the same component the advanced General page
    // hosts — without that page's sequencing / stagger / minimum-distance
    // rows, which it appends as its own children. The editor drives the
    // Global profile every card below inherits from; a per-card Duration
    // overrides it for that event group.
    headerComponent: Component {
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            GlobalTimingDefaultsCard {
                Layout.fillWidth: true
                cardSettings: simplePage.globalSettings
                collapsible: false
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                visible: true
                text: i18n("Filtered windows are not animated. Use a Rule to keep a specific application animated even when a filter would exclude it.")
            }

            // The same animation window filter the advanced General page
            // hosts (animation-specific config group, distinct from the
            // snapping/tiling and decoration filters).
            AnimationWindowFilterCard {
                cardSettings: simplePage.globalSettings
                notificationsAnchor: "simpleExcludeNotificationsAndOsds"
            }
        }
    }
    eventModel: [
        {
            "eventPath": "window.appearance.open",
            "eventLabel": i18n("Window opened & closed"),
            "isParentNode": false,
            "mirrorPaths": ["window.appearance.close"]
        },
        {
            "eventPath": "window.appearance.minimize",
            "eventLabel": i18n("Window minimized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.movement",
            "eventLabel": i18n("Window movement"),
            "isParentNode": true
        },
        {
            "eventPath": "window.movement.move",
            "eventLabel": i18n("Window moved"),
            "isParentNode": false
        },
        {
            "eventPath": "desktop.switch",
            "eventLabel": i18n("Desktop switched"),
            "isParentNode": false
        }
    ]
}
