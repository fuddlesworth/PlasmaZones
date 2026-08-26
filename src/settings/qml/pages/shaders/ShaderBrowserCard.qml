// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Compact shader card for a shader-browser grid.
 *
 * Renders one effect as a fixed-width card: preview thumbnail (or an
 * empty area when the pack didn't ship one), name + badges row, two-line
 * description, and a small footer chip showing parameter count and a
 * usage count. Clicking the card emits `showDetails(effect)`.
 *
 * Pack-agnostic: drives both the animation-shaders browser and the
 * snapping-overlay-shaders browser. The host passes a `bridge` property
 * exposing `shaderEffectUsages(id)` so the "Used in:" chip works for
 * either domain (per-event paths for animations, per-layout names for
 * snapping overlays).
 *
 * Required:
 *   - `effect`: var — effect map (id, name, description, parameters,
 *      previewPath, isUserEffect, ...).
 *   - `bridge`: QtObject — exposes `shaderEffectUsages(id)`.
 *
 * Optional:
 *   - `usagesRev`: int — host-owned tick that invalidates the
 *      `shaderEffectUsages(id)` Q_INVOKABLE result on registry /
 *      override mutations. Forwarded into the binding's dependency set.
 *   - `usageChipTextFn`: function(count) → string — domain-tuned copy
 *      for the small "N use(s)" chip in the card footer. Host passes a
 *      closure that calls `i18ncp(..., count)` with the LIVE count so
 *      the right plural form is picked per locale, and so the chip
 *      stays consistent with the dialog header's wording (animations
 *      use "event"; snapping uses "layout"). Default: a generic
 *      "%n use" / "%n uses".
 */
ItemDelegate {
    id: root

    // On the delegate root (not just the background Rectangle) so the fill,
    // the content chrome, and the labels all resolve the same View set.
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    // ItemDelegate defaults to Qt.NoFocus, which left every shader card out of the
    // tab chain: opening a pack's details was mouse-only, and the focus-border
    // branch below could never fire. AbstractButton gives Space to clicked() for
    // free, but only Space — Return and Enter have to be wired, and they are the
    // keys a user actually reaches for. Same trio as SettingsCard's header.
    focusPolicy: Qt.StrongFocus
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()

    required property var effect
    required property var bridge
    property int usagesRev: 0
    property var usageChipTextFn: function (count) {
        return i18ncp("@info shader usage count", "%n use", "%n uses", count);
    }
    /// Returns a short capability label for the card badge (e.g. "Geometry"),
    /// or "" to render no badge (universal shaders, the default majority). The
    /// host supplies the mapping so the badge stays consistent with the
    /// browser's Type filter and Type grouping.
    property var typeBadgeFn: function (e) {
        return "";
    }
    readonly property string _typeBadge: (root.effect && root.typeBadgeFn) ? String(root.typeBadgeFn(root.effect)) : ""

    /// The scrolling viewport this card lives in, supplied by the browser page.
    /// Used only to decide whether the card is on screen. Null disables that
    /// gating and treats the card as always visible, which is the right
    /// degraded behaviour for a host that renders no live preview anyway.
    property Flickable viewport: null

    /// A decoration pack with no baked thumbnail: render the real chain in the
    /// card. Gated on the bridge actually being the decoration one, so the
    /// animation and overlay browsers are untouched.
    readonly property bool _liveDecorationPreview: !!(root.bridge && root.bridge.previewController && root.bridge.previewKind === "decoration" && root.effect && !(root.effect.previewPath && root.effect.previewPath.length > 0))

    /// Set false by a host whose section is collapsed. A collapsed
    /// SettingsCard clips its body to zero height and fades it out, but it
    /// never sets `visible: false` and never unloads the content, so the cards
    /// inside keep their own y and still map into the viewport. Scrolling
    /// alone therefore cannot tell that they are hidden, and every decoration
    /// pack in a collapsed section would keep a live capture chain and one
    /// shader item per stage, producing nothing at opacity 0.
    property bool previewLive: true

    /// Whether this card intersects the viewport, with one card-height of slack
    /// either side so a preview is warm by the time it is scrolled into view.
    ///
    /// mapToItem registers no QML dependency, so every input is read FIRST:
    /// touching them is what makes this re-evaluate. Same idiom
    /// SurfaceDecoration.qml uses for its anchor mapping.
    readonly property bool _inViewport: {
        if (!root.previewLive)
            return false;
        if (!root.viewport)
            return true;
        const contentY = root.viewport.contentY;
        const viewH = root.viewport.height;
        if (viewH <= 0)
            return false;
        // The card's own position matters as much as the scroll offset: the
        // Flow re-wrapping on a resize or a filter change moves a card without
        // moving contentY.
        const selfY = root.y;
        const selfX = root.x;
        void selfY;
        void selfX;
        // And so does an ANCESTOR moving, which the card's own y does not
        // record: a section collapsing above this one slides every later card
        // up the page while both root.y (its offset inside its own Flow) and
        // contentY stay exactly where they were. Walking up to the viewport
        // and touching each ancestor's y and height is what makes those
        // moves re-evaluate this; without it the cards a collapse pulls into
        // view stay covered, showing empty slots until the user scrolls.
        for (let a = root.parent; a && a !== root.viewport.contentItem; a = a.parent) {
            void a.y;
            void a.height;
        }
        const pos = root.mapToItem(root.viewport.contentItem, 0, 0);
        const slack = root.height;
        return (pos.y + root.height) > (contentY - slack) && pos.y < (contentY + viewH + slack);
    }

    signal showDetails(var effect)

    width: Kirigami.Units.gridUnit * 14
    implicitWidth: width
    implicitHeight: contentItem.implicitHeight
    // The inner padding (card border -> text) is applied as a MARGIN inside our
    // own contentItem (anchors.margins on the inner ColumnLayout below), NOT via
    // the Control's padding. The org.kde.desktop ItemDelegate style overrides
    // per-side padding, so `padding:`/`leftPadding:` set here are ignored and the
    // text hugged the border. Control padding is zeroed so contentItem fills the
    // card and the margin is the sole, style-proof inset.
    padding: 0
    readonly property real _cardPad: Math.round(Kirigami.Units.largeSpacing * 1.5)
    hoverEnabled: true
    Accessible.name: effect ? (effect.name || effect.id || "") : ""
    Accessible.description: effect && effect.description ? effect.description : i18nc("@info:tooltip generic shader card", "Shader effect details")
    onClicked: {
        if (effect)
            root.showDetails(effect);
    }

    background: Rectangle {
        radius: Kirigami.Units.smallSpacing
        // Subtle altBg fill; hover signals through the hover border plus a
        // faint hover tint on the fill itself (this card tints its fill on
        // hover, unlike SettingsCard's border-only hover).
        color: root.hovered ? Qt.tint(Kirigami.Theme.alternateBackgroundColor, Qt.alpha(Kirigami.Theme.hoverColor, 0.1)) : Kirigami.Theme.alternateBackgroundColor
        border.width: 1
        border.color: {
            if (root.activeFocus)
                return Kirigami.Theme.focusColor;

            if (root.hovered)
                return Kirigami.Theme.hoverColor;

            return Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast);
        }

        Behavior on border.color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: Kirigami.Units.shortDuration
            }
        }
    }

    contentItem: Item {
        implicitWidth: cardLayout.implicitWidth + root._cardPad * 2
        implicitHeight: cardLayout.implicitHeight + root._cardPad * 2

        ColumnLayout {
            id: cardLayout

            anchors.fill: parent
            anchors.margins: root._cardPad
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                readonly property bool _hasPreview: !!(root.effect && root.effect.previewPath && root.effect.previewPath.length > 0)

                Layout.fillWidth: true
                Layout.preferredHeight: width * 9 / 16
                // A baked preview.png still wins where a pack ships one (that
                // is how every overlay pack thumbnails, and how a third-party
                // decoration pack can). Decoration packs ship none, so they get
                // the live render instead of an empty slot.
                visible: _hasPreview || root._liveDecorationPreview
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    // `encodeURI` percent-encodes spaces and unicode while
                    // preserving path separators, which a raw `"file://" + path`
                    // concat would silently break on (e.g. user-installed packs
                    // under `~/My Shaders/`). It leaves `#` and `?` untouched,
                    // so those two are escaped explicitly or they would be
                    // parsed as fragment/query delimiters in the file:// URL.
                    // Twin site: ShaderBrowserDetailDialog.qml preset dialogs.
                    source: parent._hasPreview ? "file://" + encodeURI(root.effect.previewPath).replace(/#/g, "%23").replace(/\?/g, "%3F") : ""
                    fillMode: Image.PreserveAspectCrop
                    // Guarded like DecorationChainPreview's twin: the Flow
                    // hands a card zero width during first layout, and the
                    // 1px inset would make this negative.
                    sourceSize.width: Math.max(1, Math.round(width * 2))
                    sourceSize.height: Math.max(1, Math.round(height * 2))
                    asynchronous: true
                    cache: true
                    visible: status === Image.Ready
                }

                // Live decoration thumbnail. Decoration packs ship no baked
                // preview.png, and unlike an overlay shader a surface pack has
                // nothing to show without a subject to decorate, so the card
                // renders the real chain over the stand-in card instead.
                //
                // In a Loader gated on _inViewport: the browser lays cards out
                // in a Flow inside a Repeater, so EVERY delegate exists at once
                // and an ungated binding would stand up a capture chain and a
                // shader item for all installed packs simultaneously. The gate
                // keeps that to the handful actually on screen and tears each
                // one down again on scroll-away.
                Loader {
                    id: decorationPreviewLoader

                    anchors.fill: parent
                    anchors.margins: 1
                    active: root._liveDecorationPreview && root._inViewport
                    visible: active

                    sourceComponent: DecorationChainPreview {
                        previewController: root.bridge.previewController
                        packId: root.effect ? (root.effect.id || "") : ""
                        // Declared defaults: the card is a catalogue entry, so
                        // it shows the pack as shipped. Parameter editing is the
                        // detail dialog's job.
                        params: ({})
                        active: true
                        cardTitle: i18nc("@title sample window in the decoration preview", "Sample Window")
                    }
                }

                // Covers the live preview until every part of it has arrived —
                // the wallpaper ground decoded and every stage compiled — so the
                // card shows the empty slot and then the finished thing, the way
                // a pack with a baked preview.png does (its Image is hidden
                // until Ready for the same reason). Without it a grid of cards
                // visibly assembles itself: grey, then wallpaper, then
                // decoration, each on its own clock.
                //
                // COVERS rather than hides: the preview underneath has to keep
                // rendering to reach `ready` at all, because a capture chain
                // that is not visible is starved and never compiles. No label
                // here, unlike the detail dialog — at thumbnail size the empty
                // slot reads better than text, and the baked-preview cards show
                // none either.
                // Stays up for a pack whose shader failed to compile, too. That
                // SETTLES the chain, so `ready` goes true with nothing worth
                // showing under it — and lifting the cover there would leave
                // the grid claiming a pack renders while its own detail dialog
                // says the shader did not compile.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: root._liveDecorationPreview && (!decorationPreviewLoader.item || !decorationPreviewLoader.item.ready || decorationPreviewLoader.item.hasError)
                    color: Kirigami.Theme.alternateBackgroundColor
                    radius: Kirigami.Units.smallSpacing
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: root.effect ? (root.effect.name || root.effect.id || "") : ""
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                // Capability badge — a small rounded chip showing which event
                // class this shader targets, labelled from ShaderBrowserPage's
                // _typeCatalog (the six classes: geometry, drag motion,
                // appearance, desktop, scrolling strip, tab switch). Hidden for universal
                // shaders so the grid only calls out the ones that behave
                // differently.
                MetadataChip {
                    visible: root._typeBadge.length > 0
                    text: root._typeBadge
                    highlighted: true
                    pill: true
                }

                Label {
                    visible: root.effect && root.effect.isUserEffect
                    text: i18nc("@info shader source badge", "User")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.highlightColor
                }
            }

            Label {
                readonly property string _description: root.effect && typeof root.effect.description === "string" ? root.effect.description : ""

                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Kirigami.Units.gridUnit * 2 : 0
                visible: _description.length > 0
                text: _description
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                verticalAlignment: Text.AlignTop
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18np("%n parameter", "%n parameters", (root.effect && root.effect.parameters) ? root.effect.parameters.length : 0)
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                Item {
                    Layout.fillWidth: true
                }

                // Usage chip — visible only when this shader is assigned
                // somewhere. Tooltip carries the full list; the detail dialog
                // renders them inline.
                Label {
                    readonly property var _usages: {
                        root.usagesRev; // reactive dep
                        var id = root.effect ? root.effect.id : "";
                        if (!id || id.length === 0 || !root.bridge)
                            return [];

                        return root.bridge.shaderEffectUsages(id) || [];
                    }

                    visible: _usages.length > 0
                    text: root.usageChipTextFn(_usages.length)
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    ToolTip.visible: chipHover.hovered && _usages.length > 0
                    ToolTip.text: {
                        var names = [];
                        for (var i = 0; i < _usages.length; i++)
                            names.push(_usages[i].label || _usages[i].path);
                        return names.join(", ");
                    }
                    ToolTip.delay: Kirigami.Units.toolTipDelay

                    HoverHandler {
                        id: chipHover
                    }
                }
            }
        }
    }
}
