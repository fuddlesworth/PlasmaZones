// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Shared title label for PopupFrame-based overlay cards (layout picker,
 * shortcut cheatsheet). Single source for the popup title typography so
 * the cards cannot drift apart; the caller anchors it (top + centered,
 * one paddingSide down, by convention).
 *
 * Cards whose body text tracks the user's overlay font pass their
 * fontFamily / fontSizeScale through so the title scales with the body
 * (otherwise group headings outgrow the title past a 1.28x scale). The
 * defaults preserve the plain themed look for callers that do not.
 */
Label {
    property string fontFamily: ""
    property real fontSizeScale: 1

    font.family: fontFamily.length > 0 ? fontFamily : Kirigami.Theme.defaultFont.family
    font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 1.4 * fontSizeScale)
    font.weight: Font.DemiBold
    color: Kirigami.Theme.textColor
}
