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
    // shader picker, sliders, dialogs); building all ~22 eagerly is the
    // first-visit cost this page is optimising away.
    //
    // The page stays a SettingsFlickable so the Kirigami.WheelHandler
    // (Plasma wheel-scroll speed, bug 385836) is preserved untouched —
    // no ListView, no re-derived wheel math.
    //
    // Cards are recycle-safe: AnimationEventCard holds no persistent
    // state of its own. Its Component.onCompleted re-reads everything
    // from settingsController.animationsPage (refreshFromTree +
    // refreshShaderFromTree), so a freshly-built card always shows the
    // committed tree state. We therefore LATCH each Loader active once
    // it enters the viewport and never unload it — this avoids losing
    // transient UI (open dialogs, focus, the master toggle's collapse
    // state) on scroll and avoids rebuild churn, while keeping the
    // first-paint cost proportional to what's visible.

    // Model for the event cards. Order matches the on-screen order; the
    // parent node ("All Window Events") leads, then the per-event leaves.
    //
    // `window.snapResize` (resize-only branch of applySnapGeometry) is
    // intentionally NOT listed: no kwin-effect callsite routes a
    // resize-only event through tryBeginShaderForEvent today, so a card
    // here would be runtime-dead.
    readonly property var eventModel: [
        {
            "eventPath": "window",
            "eventLabel": i18n("All Window Events"),
            "isParentNode": true
        },
        {
            "eventPath": "window.open",
            "eventLabel": i18n("Open"),
            "isParentNode": false
        },
        {
            "eventPath": "window.close",
            "eventLabel": i18n("Close"),
            "isParentNode": false
        },
        {
            "eventPath": "window.minimize",
            "eventLabel": i18n("Minimize"),
            "isParentNode": false
        },
        {
            "eventPath": "window.maximize",
            "eventLabel": i18n("Maximize"),
            "isParentNode": false
        },
        {
            "eventPath": "window.move",
            "eventLabel": i18n("Move"),
            "isParentNode": false
        },
        {
            "eventPath": "window.resize",
            "eventLabel": i18n("Resize"),
            "isParentNode": false
        },
        {
            "eventPath": "window.focus",
            "eventLabel": i18n("Focus"),
            "isParentNode": false
        },
        // Snap-into-zone window animations driven by the kwin-effect.
        // The window quad animates when it snaps into / out of a zone
        // or when a layout switch repositions it.
        {
            "eventPath": "window.snapIn",
            "eventLabel": i18n("Snap Into Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.snapOut",
            "eventLabel": i18n("Snap Out of Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.layoutSwitch",
            "eventLabel": i18n("Layout Switch"),
            "isParentNode": false
        }
    ]
    // Build-ahead margin above and below the visible viewport so a card
    // is instantiated slightly before it scrolls into view — keeps the
    // edge of a fast flick from showing an un-built placeholder.
    readonly property real buildBuffer: Kirigami.Units.gridUnit * 20
    // Placeholder height a not-yet-built card reserves in the layout so
    // contentHeight (and therefore scroll position) stays stable before
    // the real card materialises. Sized to a collapsed card (header +
    // inheritance banner + "Current:" line). Cards that build above the
    // viewport replace this with their real height, so cumulative offset
    // for already-passed cards is always exact and scrolling never
    // jumps.
    readonly property real placeholderHeight: Kirigami.Units.gridUnit * 7

    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Window animation events")

    ColumnLayout {
        id: col

        width: page.width
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: page.eventModel

            // One latching, viewport-gated Loader per event. The Loader
            // is the layout participant — it carries the placeholder
            // height until built, then tracks the card's real height.
            Loader {
                id: cardLoader

                required property var modelData

                // Latch: once the card has entered the viewport it stays
                // built. `_everInView` flips true and never back.
                property bool _everInView: false

                Layout.fillWidth: true
                // Reserve the real card height once built, placeholder
                // otherwise. ColumnLayout lays children out top-to-
                // bottom, so this Loader's `y` is already in flickable
                // content-space.
                Layout.preferredHeight: active && item ? item.implicitHeight : page.placeholderHeight
                active: _everInView
                asynchronous: true
                visible: active

                // Imperative one-shot latch. A declarative `Binding { when:
                // <reads y> }` loops: building the card changes
                // Layout.preferredHeight, which makes the ColumnLayout
                // re-assign every child's `y` in the same pass, re-firing the
                // `when` that reads `y` — Qt flags that as a binding loop.
                // Checking imperatively on the relevant change signals and
                // returning once latched breaks the cycle (no binding over
                // the layout geometry the build perturbs). `y` is read live
                // but never bound. placeholderHeight (constant) estimates the
                // slot's bottom so the build height never feeds back in.
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
                    // Width is driven by the Loader (Layout.fillWidth on
                    // the Loader → Loader.width → item.width); the card's
                    // implicitHeight drives Layout.preferredHeight above.
                    eventPath: cardLoader.modelData.eventPath
                    eventLabel: cardLoader.modelData.eventLabel
                    isParentNode: cardLoader.modelData.isParentNode === true
                }
            }
        }
    }
}
