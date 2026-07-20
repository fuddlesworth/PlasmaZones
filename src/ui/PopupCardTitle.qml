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
 */
Label {
    font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.4
    font.weight: Font.DemiBold
    color: Kirigami.Theme.textColor
}
