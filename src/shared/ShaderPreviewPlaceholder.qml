// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * "Preview unavailable" stand-in for a live shader preview that is not showing
 * yet.
 *
 * Sibling of ShaderCompileErrorBanner: that one covers a preview that FAILED,
 * this one covers a preview that has not finished arriving. Shared so every
 * preview in the app says the same thing, in one translation entry, instead of
 * each browser route growing its own wording.
 *
 * The host anchors this over its preview surface and drives `visible` from
 * whatever "not ready yet" means for it — a renderer that has not reached
 * Ready, or a decoration chain whose stages and wallpaper have not all arrived.
 *
 * ## Covering versus hiding
 *
 * `backgroundColor` defaults to transparent, which suits a host that HIDES its
 * preview while it loads: there is nothing underneath to conceal, so only the
 * text is needed.
 *
 * A host whose preview must keep RENDERING while it waits has to set an opaque
 * colour, normally the colour of the slot it sits in. That is not a style
 * preference. A capture chain built on ShaderEffectSource is starved while it
 * is not visible and never reaches its ready state at all, so such a host
 * cannot hide its preview to wait for it — it has to leave it drawing and put
 * this on top.
 */
Control {
    id: root

    /// Fill behind the text. Transparent by default, for a host that hides its
    /// preview while it loads. A host that must leave the preview RENDERING
    /// underneath sets the colour of its surrounding slot so this reads as an
    /// empty one — see the covering-versus-hiding note above.
    property color backgroundColor: "transparent"
    /// Corner rounding, so a cover can match the slot it fills.
    property real radius: 0
    /// What the stand-in says. Defaults to the not-arrived-yet wording every
    /// host shares. A host that can tell WHY a preview is missing overrides it,
    /// so a pack that will never render is not left looking like one that is
    /// merely slow.
    property string text: i18nc("@info:placeholder shader preview", "Preview unavailable")

    padding: Kirigami.Units.largeSpacing

    background: Rectangle {
        color: root.backgroundColor
        radius: root.radius
    }

    contentItem: Label {
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        elide: Text.ElideRight
        color: Kirigami.Theme.disabledTextColor
        text: root.text
    }
}
