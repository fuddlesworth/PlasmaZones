// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * A SettingsFlickable that renders a list of DecorationSurfaceCards with
 * viewport-gated lazy construction — the decoration-side mirror of
 * AnimationEventCardList. Only cards whose slot intersects the visible
 * viewport (plus a build-ahead buffer) are instantiated; each
 * DecorationSurfaceCard embeds a ChainEditor (per-pack ShaderParamsEditor,
 * colour dialogs), so building a whole page eagerly is the dominant
 * first-visit cost. See AnimationEventCardList for the full rationale of the
 * latching viewport gate; this is the same pattern with a surface model.
 *
 * Cards are recycle-safe: DecorationSurfaceCard holds no persistent state of
 * its own (its Component.onCompleted re-reads from the controller), so a
 * freshly-built card always shows the committed tree state. Each Loader
 * LATCHES active once it enters the viewport and never unloads.
 *
 * Consumers provide `surfaceModel` (and an Accessible.name):
 *
 *   DecorationSurfaceCardList {
 *       Accessible.name: i18n("Window decoration surfaces")
 *       headerText: i18n("…optional orientation banner…")
 *       surfaceModel: [ { surfacePath, cardLabel, alwaysEnabled,
 *                         isParentNode }, … ]
 *   }
 */
SettingsFlickable {
    id: page

    /// Ordered list of `{ surfacePath: string, cardLabel: string (i18n),
    /// alwaysEnabled: bool, isParentNode: bool }` —
    /// one DecorationSurfaceCard per entry.
    property var surfaceModel: []
    /// Optional orientation banner rendered above the cards. Empty = none.
    property string headerText: ""

    // Build-ahead margin above/below the visible viewport so a card is built
    // slightly before it scrolls into view.
    readonly property real buildBuffer: Kirigami.Units.gridUnit * 20
    // Placeholder height a not-yet-built card reserves so contentHeight (and
    // scroll position) stays stable until the real card materialises. Sized to
    // a collapsed card (header + inheritance banner + "Current:" line).
    readonly property real placeholderHeight: Kirigami.Units.gridUnit * 7

    contentHeight: col.implicitHeight
    clip: true

    ColumnLayout {
        id: col

        width: page.width
        spacing: Kirigami.Units.smallSpacing

        // Optional page-level orientation banner (the decoration pages carry a
        // short inheritance-model explainer; the animation pages rely on the
        // per-card banners alone, so this is opt-in via `headerText`).
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            type: Kirigami.MessageType.Information
            visible: page.headerText.length > 0
            text: page.headerText
        }

        Repeater {
            model: page.surfaceModel

            // One latching, viewport-gated Loader per surface. The Loader is the
            // layout participant — it carries the placeholder height until built,
            // then tracks the card's real height. See AnimationEventCardList for
            // why this is imperative (not a declarative Binding) and why the
            // Loader stays `visible` while inactive.
            Loader {
                id: cardLoader

                required property var modelData

                property bool _everInView: false

                Layout.fillWidth: true
                Layout.preferredHeight: cardLoader.active && cardLoader.implicitHeight > 0 ? cardLoader.implicitHeight : page.placeholderHeight
                active: _everInView
                asynchronous: true

                function _checkInView() {
                    if (cardLoader._everInView)
                        return;
                    const top = cardLoader.y;
                    if ((top + page.placeholderHeight) >= (page.contentY - page.buildBuffer) && top <= (page.contentY + page.height + page.buildBuffer))
                        cardLoader._everInView = true;
                }
                onYChanged: cardLoader._checkInView()
                Component.onCompleted: Qt.callLater(cardLoader._checkInView)

                Connections {
                    target: page
                    function onContentYChanged() {
                        cardLoader._checkInView();
                    }
                    function onHeightChanged() {
                        cardLoader._checkInView();
                    }
                }

                sourceComponent: DecorationSurfaceCard {
                    surfacePath: cardLoader.modelData.surfacePath
                    cardLabel: cardLoader.modelData.cardLabel
                    alwaysEnabled: cardLoader.modelData.alwaysEnabled === true
                    isParentNode: cardLoader.modelData.isParentNode === true
                }
            }
        }
    }
}
