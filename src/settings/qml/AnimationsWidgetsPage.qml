// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: page

    // Viewport-gated lazy build: only AnimationEventCards whose y-range
    // intersects the visible viewport (plus a buffer) are instantiated.
    // Each card embeds a heavy AnimationProfileEditor (curve Canvas,
    // shader picker, sliders, dialogs); this page lists ~22 events, so
    // building them all eagerly was the dominant first-visit cost. The
    // page stays a SettingsFlickable so the Kirigami.WheelHandler (Plasma
    // wheel-scroll speed, bug 385836) is preserved untouched. Cards are
    // recycle-safe: AnimationEventCard holds no persistent state of its
    // own (Component.onCompleted re-reads from settingsController.
    // animationsPage), and each Loader latches active once it enters the
    // viewport and never unloads — so no transient UI is lost on scroll.
    // See AnimationsWindowsPage.qml for the same pattern + rationale.
    readonly property var eventModel: [
        {
            "eventPath": "widget",
            "eventLabel": i18n("All Widget Events"),
            "isParentNode": true
        },
        {
            "eventPath": "widget.hover",
            "eventLabel": i18n("Hover"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.press",
            "eventLabel": i18n("Press"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.toggleOn",
            "eventLabel": i18n("Toggle On"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.toggleOff",
            "eventLabel": i18n("Toggle Off"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.badgeShow",
            "eventLabel": i18n("Show (badge)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.badgeHide",
            "eventLabel": i18n("Hide (badge)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.badgePulse",
            "eventLabel": i18n("Pulse (badge)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.tint",
            "eventLabel": i18n("Tint"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.dim",
            "eventLabel": i18n("Dim"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.fadeIn",
            "eventLabel": i18n("Fade In"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.fadeOut",
            "eventLabel": i18n("Fade Out"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.reorder",
            "eventLabel": i18n("Reorder"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.accordionExpand",
            "eventLabel": i18n("Expand (accordion)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.accordionCollapse",
            "eventLabel": i18n("Collapse (accordion)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.progress",
            "eventLabel": i18n("Progress"),
            "isParentNode": false
        },
        // Zone-rect widget. Embedded across the runtime overlay, settings
        // dialogs, layout thumbnails, the new-layout dialog, shared
        // previews. The animation lives with the widget; the surface
        // hosting it is incidental.
        {
            "eventPath": "widget.zoneHighlight",
            "eventLabel": i18n("Zone Highlight"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.zoneHighlight.pop",
            "eventLabel": i18n("Zone Highlight: Pop"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.zoneHighlight.border",
            "eventLabel": i18n("Zone Highlight: Border"),
            "isParentNode": false
        },
        // One-shot flash on the main zone-overlay surface when the active
        // layout switches mid-drag.
        {
            "eventPath": "widget.zoneOverlayFlash",
            "eventLabel": i18n("Zone Overlay: Layout-Switch Flash"),
            "isParentNode": false
        },
        {
            "eventPath": "cursor.hover",
            "eventLabel": i18n("Cursor Hover"),
            "isParentNode": false
        },
        {
            "eventPath": "cursor.click",
            "eventLabel": i18n("Cursor Click"),
            "isParentNode": false
        }
    ]
    // Build-ahead margin above/below the viewport so a card is built
    // slightly before it scrolls into view.
    readonly property real buildBuffer: Kirigami.Units.gridUnit * 20
    // Placeholder height a not-yet-built card reserves so contentHeight
    // (and scroll position) stays stable until the real card materialises.
    readonly property real placeholderHeight: Kirigami.Units.gridUnit * 7

    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Widget animation events")

    ColumnLayout {
        id: col

        width: page.width
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: page.eventModel

            // One latching, viewport-gated Loader per event.
            Loader {
                id: cardLoader

                required property var modelData

                // Latch: once the card has entered the viewport it stays
                // built. `_everInView` flips true and never back.
                property bool _everInView: false

                Layout.fillWidth: true
                Layout.preferredHeight: active && item ? item.implicitHeight : page.placeholderHeight
                active: _everInView
                asynchronous: true
                visible: active

                // Imperative one-shot latch — see AnimationsWindowsPage.qml
                // for why a declarative `Binding { when: <reads y> }` forms a
                // binding loop here. `y` read live but never bound;
                // placeholderHeight (constant) estimates the slot bottom so
                // the build height never feeds back in.
                function _checkInView() {
                    if (cardLoader._everInView)
                        return;
                    const top = cardLoader.y;
                    if ((top + page.placeholderHeight) >= (page.contentY - page.buildBuffer) && top <= (page.contentY + page.height + page.buildBuffer))
                        cardLoader._everInView = true;
                }
                onYChanged: cardLoader._checkInView()
                Component.onCompleted: cardLoader._checkInView()

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
                    eventPath: cardLoader.modelData.eventPath
                    eventLabel: cardLoader.modelData.eventLabel
                    isParentNode: cardLoader.modelData.isParentNode === true
                }
            }
        }
    }
}
