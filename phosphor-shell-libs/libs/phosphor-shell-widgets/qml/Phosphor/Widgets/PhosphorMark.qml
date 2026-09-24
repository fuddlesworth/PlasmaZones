// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Kirigami.Icon {
    implicitWidth: 32
    implicitHeight: 32
    isMask: true
    color: Appearance.stops[0]
    roundToIconSize: false
    // Resolve the bundled artwork directly so an installed icon theme cannot
    // substitute a different mark. Disable isMask to use the original gradient.
    source: Qt.resolvedUrl(isMask ? "assets/phosphor-mark-symbolic.svg" : "assets/phosphor-mark.svg")
    Accessible.ignored: true
}
